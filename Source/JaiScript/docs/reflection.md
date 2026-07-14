# Script-side reflection: the `reflect::` API

**Status: DESIGN — the front door.** This doc is the whole story for using reflection.
The machinery behind it (access enforcement mechanics, retention work, entry-shape details,
parity proofs, staging, costs) lives in the companion: [reflection_design.md](reflection_design.md).
Read this one first; dip into the companion only when you need the *why*.

Reflection is three small groups of builtins over data the runtime already keeps.
Everything below is a live read of the engine's registered classes — hot reload a class and
the next call sees the new shape. Results are ordinary value-semantic arrays/maps: what you
already hold never mutates under you.

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
justification: companion §3.

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
reflect::instances("Creature") // -> array of live instances of the class (engine-scoped)
reflect::generation("Creature")// -> int, bumps on hot reload/migration — poll this, re-ask on change
reflect::methods(x)            // -> signature list; the detailed entry shape is companion §2
```

Namespaces need no machinery of their own: `reflect::functions()` returns qualified names, so
a namespace listing is a prefix filter (`f.starts_with("combat::")`). A dedicated
`reflect::functions("combat")` overload is sugar the companion stages for v2.

`instances` and `generation` are the tool loop: an editor polls one integer, and re-enumerates
only when it moved. Everything reflects only what **this engine** registered — a sandbox engine
that bound nothing reflects nothing (the three-doors rule; see the binder chapter).

---

## The three contracts

1. **`reflect::` is a reserved namespace.** It is C++-registered truth; script code cannot
   declare into it (declaration-time error, both backends). Contrast `math::`, which is
   deliberately open to script extension — reflection answers must never be impersonatable.
2. **Deterministic order, always.** Fields come back in declaration order (base's fields first),
   methods sorted by (name, arity), classes sorted. Never hash order — reflection output is
   replay-safe by contract.
3. **Live source, stable results.** Queries read the live class definition (reload → re-ask →
   new shape, including from inside a `hot_reload_migrate` hook); returned containers are
   snapshots minted at call time.

## What this deliberately is not

No expression trees, no runtime type emission (hot reload is the better answer), no
process-global reflection, no monkey-patching. Attributes/editor hints (`@range(0,100)`-style)
are a separate design shared with the editor campaign — companion §6. Errors are ordinary
catchable runtime errors with one consistent family text — companion §2.
