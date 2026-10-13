# Script-side reflection: the `reflect::` API

**Status: DESIGN — the front door.** This doc is the whole story for using reflection.
The machinery behind it (access enforcement mechanics, retention work, entry-shape details,
parity proofs, staging, costs) lives in the companion: [reflection_design.md](reflection_design.md).
Read this one first; dip into the companion only when you need the *why*.

Reflection is three small groups of builtins over data the runtime already keeps: registered
classes and functions, live objects, and the engine's enabled privileges. Class queries are
live — hot reload a class and the next call sees the new shape. Returned arrays/maps have
ordinary value semantics: what you already hold never mutates under you.

---

## A — Ask: what is this thing?

```jaiscript
reflect::type_name(x)          // "Creature", "array<int>", "int" — the ONE name (matches $type)
reflect::is_cpp_bound(x)       // -> bool: C++-backed object or host-registered class (vs script-declared)
reflect::fields(x)             // -> array of {name, type, kind, access, static, from} — all members
reflect::has_field(x, "hp")    // -> bool (inherited included)
reflect::has_method(x, "roar") // -> bool (inherited included)
```

- `x` is an object **or** a class name string — `reflect::fields("Creature")` works before any
  instance exists.
- `kind` is `"field"`, `"observable"`, or `"signal"` — signals are members here, so one
  enumeration covers them (no separate signals call).
- Type comparison is string comparison: `reflect::type_name(a) == reflect::type_name(b)`, or
  `== "Creature"`. The name is always the naming-ladder name — the same string serialization
  writes as `$type` and the same name script constructs with. There is no third name.
- Types are **semantic**: references and C++-bound values report what they hold, never their
  storage wrapper.

```jaiscript
for (auto f : reflect::fields(obj)) {
    print("[{}] {} {} ({})", f["access"], f["type"], f["name"], f["kind"]);
}
```

## B — Act: read, write, call, construct

```jaiscript
reflect::get(x, "hp")                   // reads like x.hp
reflect::set(x, "hp", 50)              // writes like x.hp = 50 — same type + access enforcement
reflect::invoke(x, "heal", 25)         // calls like x.heal(25) — overloads, defaults, access
reflect::bind(x, "heal")               // x's heal as a callable (holds x, keeps it alive); a value type's methods need a lambda
reflect::call("spawn_wave", 3)         // calls a FREE function by name — script or host-registered
reflect::construct("Goblin", 30)       // == Goblin(30), value semantics
reflect::construct_shared("Goblin", 30) // == new Goblin(30), reference semantics
```

The introspect-then-invoke loop closes: enumerate a callable's parameter types
(`reflect::method_arguments(x, "heal")` / `reflect::function_arguments("spawn_wave")` — one
param-array **per overload**), build a matching args array, and `invoke`/`call` it.

The one rule that governs all of B: **reflection is a spelling, not a bypass.** A write or call
through `reflect::` enforces exactly what direct syntax enforces, with byte-identical error
text — private stays private from outside, typed fields still convert-or-error, observable
properties still fire their change signals. Reads see everything (with `access` reported), the
same rule `to_json` and the debugger already follow. The full access model and its
justification: companion §3. A host function registered with a required privilege keeps that
check through a function alias or `reflect::call`; reflection cannot grant permission.

Spawn-by-name, the canonical use:

```jaiscript
for (auto row : wave_table) {
    spawned.push(reflect::construct_shared(row["type"], row["hp"]));
}
```

## C — Navigate: the wider world

```jaiscript
reflect::classes()             // -> array<string>, sorted — everything THIS engine registered
reflect::functions()           // -> array<string>, sorted, namespace-QUALIFIED ("utils::add")
reflect::globals()             // -> array<string>, sorted — global variable names
reflect::bases(x)              // -> array<string>, declaration order; [] at a root
reflect::instances("Creature") // -> array of live instances of the class (engine-scoped), in creation order
reflect::generation("Creature")// -> int, bumps on hot reload/migration — poll this, re-ask on change
reflect::methods(x)            // -> signature list; the detailed entry shape is companion §2
reflect::is_enabled(jai::filesystem) // -> bool: may this engine perform gated filesystem operations?
```

Namespaces need no machinery of their own: `reflect::functions()` returns qualified names, so
a namespace listing is a prefix filter (`f.starts_with("combat::")`). A dedicated
`reflect::functions("combat")` overload is sugar the companion stages for v2.

`instances` and `generation` are the tool loop: an editor polls one integer, and re-enumerates
only when it moved. Queries concern **this engine**, not another engine's host registrations.
Builtins are still present on a fresh engine; an unregistered host class does not become
visible merely because its C++ type exists (the three-doors rule; see the binder chapter).

### Enabled privileges

`reflect::is_enabled(requirements...)` requires at least one argument and returns true only
if **all** supplied requirements are enabled. Arguments may mix built-in integer flags and
permission-name strings. Invalid bits or other argument types raise; unknown names return
false without interning or registering anything. `jai::filesystem` is the only built-in
flag, enabled by default, and `"filesystem"` queries the same grant.

```jaiscript
reflect::is_enabled(jai::filesystem)                    // built-in integer flag
reflect::is_enabled("filesystem")                       // same grant by name
reflect::is_enabled(jai::filesystem, "savegame.write")   // BOTH required; unknown name = false
```

The host defines custom permissions; the script query can neither register nor grant one:

```cpp
eng->register_privilege("savegame.write");  // starts disabled
eng->add_function("save_game", saveGame, {"filesystem", "savegame.write"});
eng->enable("savegame.write");
bool allowed = eng->is_enabled({"filesystem", "savegame.write"});
auto checked = eng->require_privilege("savegame.write");  // checked_result<void>
```

`register_privilege` returns nothing. Re-registering a name preserves its grant; registering
`"filesystem"` refers to the existing builtin without granting it. A string name or an
initializer list of names works with `enable`, `disable`, `is_enabled`, `require_privilege`
and the optional requirement on `add_function`, `add_variadic_function` and
`dynamic_binder::method`. All names in a list are required. Built-in flag overloads remain.

Names can be reused across engines with independent registration and grants. Unknown queries
return false; enabling, disabling, requiring or binding an unknown name rejects it, never
auto-registers it. Register names before attaching them to callables. Names are stored in
the engine's interner; binding resolves requirements once into internal slot IDs. Invocation
checks add no name lookups or allocations, although registration and binding may allocate.
A missing builtin is reported first, then missing custom permissions in registration order,
not list order.

A disabled filesystem privilege makes gated operations raise
`Insufficient privilege: filesystem.` on both backends. The error is catchable; unhandled,
it reaches the host. A C++ callback can revoke access during execution, so a successful query
does not reserve permission for a later call. Already-started work completes; subsequent
operations check again. Native bindings participate by checking the privilege or declaring
it with an optional callable requirement as above. Custom denial names the missing permission,
for example `Insufficient privilege: savegame.write.`. Register/change grants only while idle
or on the engine execution thread, never concurrently from another thread or during an active
parallel region. No new synchronization is added. Arbitrary callbacks, streams and shared native
objects remain the
host's responsibility, not a process sandbox. See the
[filesystem boundary](site/embedding/05-safety-rails.html#filesystem-privilege) for the exact
scope, including skipped disk caches and allowed in-memory programs.

---

## The three contracts

1. **`reflect::` and `jai::` are reserved namespaces.** Script code cannot declare into
   either root or its subnames (declaration-time error, both backends); the host may
   register their contents. `jai::` is reserved for engine capabilities. Contrast
   `math::`, which remains deliberately open to script extension.
2. **Deterministic order, always.** Fields come back in declaration order (base's fields first),
   methods sorted by (name, arity), classes sorted. Never hash order — reflection output is
   replay-safe by contract.
3. **Live source, stable results.** Queries read the live class definition (reload → re-ask →
   new shape, including from inside a `hot_reload_migrate` hook); returned containers are
   snapshots minted at call time.

## What this deliberately is not

No expression trees, no runtime type emission (hot reload is the better answer), no
process-global reflection, no monkey-patching. Attributes/editor hints (`@range(0,100)`-style)
are a separate design shared with the editor — companion §6. Errors are ordinary
catchable runtime errors with one consistent family text — companion §2.
