# Script-side reflection: the `reflect::` API

The `reflect::` API as built (`include/jaiscript/stdlib/reflection.hpp`). This doc is the whole
story for using reflection. The machinery behind it (access enforcement mechanics, retention,
entry-shape details, parity) lives in the companion: [reflection_design.md](reflection_design.md).
Read this one first; dip into the companion only when you need the *why*.

Reflection is three small groups of builtins over data the runtime already keeps: registered
classes and functions, live objects, and the engine's enabled permissions. Class queries are
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

The one rule that governs all of B: **reflection ignores script access control, by design.**
Reflecting is an intentional request for a type's whole shape, so get/set/invoke/bind/construct
reach private members and private constructors of script classes; `private` is a convention at
that level. Typed fields still convert-or-error and observable properties still fire their change
signals. The security boundary is the host and the engine: a C++ binding exposes only what it
registered, builtin values expose no internals, and a host function registered with a required
permission keeps that check through a function alias or `reflect::call`; reflection cannot grant
permission.

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
jai::permissions::is_enabled(jai::permissions::filesystem) // -> bool: may this engine perform gated filesystem operations?
```

Namespaces need no machinery of their own: `reflect::functions()` returns qualified names, so
a namespace listing is a prefix filter (`f.starts_with("combat::")`). `reflect::functions()`
takes no arguments.

`instances` and `generation` are the tool loop: an editor polls one integer, and re-enumerates
only when it moved. Queries concern **this engine**, not another engine's host registrations.
Builtins are still present on a fresh engine; an unregistered host class does not become
visible merely because its C++ type exists (the three-doors rule; see the binder chapter).

### Enabled permissions

`jai::permissions::is_enabled(ids...)` requires at least one argument and returns true only if
**all** supplied permissions are enabled. Each argument is an id that fits an `int32_t`: a
`jai::permissions` value (`filesystem`, `scoped_template`, `network`, `debug`, `trusted`,
`untrusted`) or a host's. Anything else raises.

```jaiscript
jai::permissions::is_enabled(jai::permissions::filesystem)
jai::permissions::is_enabled(jai::permissions::filesystem, 7)   // BOTH required; 7 is a host's id
```

The host defines its own as an enum over `std::int32_t` starting at `jai::permissions::count`;
any enum passed where a permission goes is one, its underlying value its id. The script query
can neither define nor grant one:

```cpp
enum class GamePermission : std::int32_t { saves = jai::permissions::count };
eng->add_function("save_game", saveGame, jai::permissions::filesystem, GamePermission::saves);
eng->enable(GamePermission::saves);
bool allowed = eng->is_enabled(jai::permissions::filesystem, GamePermission::saves);
auto checked = eng->require_permission(GamePermission::saves);  // checked_result<void>
eng->register_permission(GamePermission::saves, "savegame.write");  // optional: the name a denial shows
```

Grants and names are per engine. A requirement is a set fixed at binding: ids 0 to 255 as
four words, any other in a sorted spill. A call's check over the low ids is four word tests,
no lookup or allocation. A denial names the lowest missing id.

A disabled filesystem permission makes gated operations raise
`Insufficient permission: filesystem.` on both backends. The error is catchable; unhandled,
it reaches the host. A C++ callback can revoke access during execution, so a successful query
does not reserve permission for a later call. Already-started work completes; subsequent
operations check again. Native bindings participate by checking the permission or declaring
it with an optional callable requirement as above. Custom denial names the missing permission,
for example `Insufficient permission: savegame.write.`. Register/change grants only while idle
or on the engine execution thread, never concurrently from another thread or during an active
parallel region. No new synchronization is added. Arbitrary callbacks, streams and shared native
objects remain the
host's responsibility, not a process sandbox. See the
[filesystem boundary](../docs/site/embedding/05-safety-rails.html#filesystem-permission) for the exact
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
