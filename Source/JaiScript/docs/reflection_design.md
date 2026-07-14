# Script-side reflection — the companion (machinery, rulings, proofs)

**Status: DESIGN, ready to build.** The front door is [reflection.md](reflection.md) — the
A+B+C read that tells users everything they need. THIS doc is the D+E companion: the probe
inventory, entry shapes, the access model, retention work, parity proofs, staging, and costs.
The two documents cross-reference; when they could disagree, the front door states the contract
and this one explains the mechanism.

The premise: the runtime already keeps rich type information (class definitions, declared field
types, method ASTs, access labels, host binding signatures) — script just has no syntax to ask
about it beyond `type_of`/`to_json`/`from_json`. Reflection v1 is a set of ordinary builtins over
data that (almost entirely) already exists.

**Naming (ruling, 2026-07-14):** everything lives in the **reserved `reflect::` namespace** —
C++-registered only; script declarations into it error at declaration time on both backends
(engine-held reserved-namespace set, today `{reflect}`). Contrast `math::`, which is
deliberately OPEN to script extension (intrinsics claim their names, user functions coexist;
intrinsic wins a collision) — reflection is engine truth and must not be impersonatable.
Sigils (`$`, `^`) were considered and rejected on grammar collisions (template-string
substitution, XOR). Spelling is decoupled from dispatch: registry functions in v1, intrinsic
promotion later without script changes. Earlier drafts of this doc used flat names
(`fields_of`, `type_name_of`); the namespaced spellings below are canonical.

---

## 1. What exists today (probe inventory, 2026-07)

### THERE — retained at runtime, reachable now

| Information | Where it lives | Anchor |
|---|---|---|
| Script-class field names + defaults | `class_definition::field_defaults_` (id → default), names via symbolizer | class_definition.hpp:1302 |
| Declared field types (`int hp` vs `var` vs `auto`) | `field_declared_types_` (id → `type_info_ptr`; `auto` = no entry, `var` = any tag) | class_definition.hpp:366-403 |
| Statics (which + values) | `static_fields_` / `static_field_values_` | class_definition.hpp:1305-1306 |
| Access labels (private/protected) | `nonpublic_members_` per declaring class; `find_nonpublic_declarer` walks the chain | class_definition.hpp:936-962 |
| Base chain (incl. multiple inheritance) | `parent_classes_` (source order) + `cpp_base_class_` | class_definition.hpp:769-827 |
| Per-instance field values | `class_instance::get_fields()` / `get_field(id)` (host-API, unrestricted) | class_definition.hpp:98, 1705 |
| Script method signatures — param **names**, declared types, ref-ness, defaults, return type, static/coroutine flags | full `function_decl` ASTs in `method_overloads_` / `script_class_definition::method_asts_`, ctors in `constructor_asts_` | script_class.hpp:94-135, ast.hpp:92-104 |
| Host (dynamic_binder) property names | binder registers a field placeholder per `.property()` → `get_property_names()` | dynamic_binder.hpp:729, class_definition.hpp:875 |
| Host property **types** + read-only flag | serialization registry `property_metadata{name, type: type_info_ptr, read_only}` per class | serialization_metadata.hpp:284-313, filled at dynamic_binder.hpp:732-735 |
| Host method arities + per-overload param types | `cpp_method_overloads_` entries carry `std::vector<param_type_info>` (base_type + `std::type_index`, mappable to a class name via the engine's classes-by-type lookup) | class_definition.hpp:1278-1282, engine.hpp:62-73 |
| Class-by-name lookup + construction | `engine::get_class_definition(name)`; ctor callables in the global env (script classes define their name at class-decl execution; host classes via `add_overloaded_function(class_name, arity, …)`) | engine.cpp:1767, script_class.hpp:112, dynamic_binder.hpp:398+ |
| Backend-neutral construction w/ overload resolution | `script_callable::kind_type::constructor` through `execution_backend::execute_callable` | execution_backend.hpp:26, interpreter.cpp:10524-10536 |
| Field enumeration precedent | `to_json` walks raw `instance->get_fields()` for script classes, getter list for host classes | json_archive.hpp:303-335 |
| Field-name→member write dispatch precedent (setter-first, data-not-code) | `reconstruct_typed_object` in from_json/from_binary | stdlib/json.hpp:463-518 |
| Hot-reload field walk | `redefine_class` + `class_instance::migrate_fields` (defaults replaced **before** the migrate-hook loop runs) | class_definition.hpp:1077-1230, 1588-1639 |
| Access-enforcement kernel + caller context | shared `detail::enforce_member_access(cls, member_id, ctx)`; both backends pass `environment_->find_access_context()` | class_definition.hpp:1527-1542, environment.hpp:160, interpreter.cpp:3861, vm_backend.cpp:5662 |
| Coarse dynamic type | `type_of` (kind names only — objects report `"object"`, never the class name) | stdlib/io.hpp:349-366 |
| Opaque-token predicate | `is_registered_type` (§13 ruling: unregistered host pass-through tokens answer false) | stdlib/io.hpp:336-347 |
| Registration-surface enumeration precedent | `registration_fingerprint()` iterates sorted registered function/class names | engine.hpp:190-193 |

The debugger's Locals view (`controller::list_locals`, controller.cpp:113-140) stringifies
frame locals only — object fields are **not** expanded today (`variablesReference: 0`,
connector.cpp:667). `reflect::fields` + `reflect::get` would incidentally give the DAP connector its
future field-expansion path for free.

### NEEDS RETENTION — gaps a v1 must fill

1. **Field declaration order.** `field_defaults_` is an `unordered_map`; the only ordered
   record (`script_class_definition::field_initializer_asts_`, script_class.hpp:138-149) covers
   only fields *with* initializers. The class-decl AST (`decl->members`) has the order but is
   only walked transiently at definition (interpreter.cpp:9716, vm_backend.cpp:~6890). Fix: an
   ordered `std::vector<uint64_t> field_order_` (instance fields, then statics, each in source
   order) on `class_definition`, appended by both backends' class-decl builders and replaced in
   `store_field_defaults` on reload. Cost: **8 bytes per field per class** — negligible.
   This is not optional polish: symbol ids are interning-order-dependent per engine, so
   unordered_map iteration order differs between two engines running the same script —
   declaration order (or sorting) is *required* for both-backend byte parity (invariant #6).
2. **Host method param names.** Never existed (C++ doesn't carry them). v1 reports host params
   by type only (`params: [{type: "int"}, …]`). Retaining names would need a `dynamic_binder`
   API extension (e.g. `.method("damage", &T::damage, {"amount", "source"})`) — the memory cost
   is trivial (rough order: 30 classes × 15 methods × 2-3 params × ~24 B ≈ 30 KB/engine); the
   real cost is API churn at every registration site. Deferred to v2.
3. **Class enumeration accessor.** `impl->classes` (host) + `class_registry_.script_classes_`
   (script, already a sorted `std::map`) exist but have no public union accessor. One small
   engine method.
4. **Caller-access-context accessor for builtins.** Builtins are opaque `std::function`s and
   don't receive the environment. Both backends already maintain the context on the environment
   chain (`find_access_context`); a builtin invoked from script does not push a new script frame,
   so the backend's *current* environment at builtin-call time is the caller's. Needed: one
   `execution_backend::current_access_context()` virtual (each backend: return
   `current env → find_access_context()`). ~10 lines total.

Everything else in the API below reads data that is already retained.

---

## 2. API surface (v1 builtins)

All are ordinary engine builtins (registered once per engine like `format_value` — see §4).
`v|"Class"` means: an object value (uses its live `class_definition`) or a class-name string
(engine lookup; unknown name is a catchable error). All results are **snapshots** — ordinary
arrays/maps minted at call time, never live views (§4).

```cpp
reflect::fields(v | "Class")     // -> array<map>: [{name, type, kind, access, static, from}, ...]
reflect::methods(v | "Class")    // -> array<map>: [{name, arity, static, access, from, params}, ...]
reflect::reflect::has_field(v | "Class", "name")   // -> bool (inherited included; host properties included)
reflect::reflect::has_method(v | "Class", "name")  // -> bool (inherited included)
reflect::get(v, "name")              // read like `v.name` from the call site (see §3)
reflect::get("Class", "name")        // static field read (Class::name)
reflect::set(v, "name", value)       // write like `v.name = value` — full type + access enforcement
reflect::set("Class", "name", value)
reflect::invoke(v, "name", args...)  // call like `v.name(args...)` — overloads, defaults, access
reflect::reflect::construct("Class", args...) // == Class(args...) — value semantics
reflect::reflect::construct_shared("Class", args...) // == new Class(args...) — reference semantics
reflect::classes()                   // -> array<string>, sorted (script + host-bound classes)
reflect::bases(v | "Class")          // -> array<string>, declaration order (MI-aware); [] at a root
reflect::type_name(v)                // precise type: "Creature", "array<int>", "map<string,int>", "int"
reflect::instances("Class" | v)      // -> array of live instances (the existing weak instance
                                     //    registry class_definition::instances_ — purge-expired walk)
reflect::generation("Class" | v)     // -> int; bumps on redefine_class/migration — the cheap
                                     //    "should I re-ask" poll for tools (live-query model)
reflect::call("name", args...)       // free-function twin of invoke: resolves the (possibly
                                     //    namespace-qualified) name in the global env and calls —
                                     //    the same callable direct syntax uses (construct's pattern)
reflect::functions()                 // -> array<string>, sorted, namespace-QUALIFIED — the
                                     //    registration surface (registration_fingerprint precedent)
reflect::function_arguments("name")  // -> array of param-arrays, ONE PER OVERLOAD
reflect::method_arguments(v|"Class", "name")  // -> same shape; sugar over reflect::methods
                                     //    entries (one param-array per overload — overloads make
                                     //    any single-signature answer ambiguous by construction)
reflect::globals()                   // -> array<string>, sorted — global variable names
reflect::is_cpp_bound(v | "Class")   // -> bool; VALUE form: C++-backed (cpp_bound storage or an
                                     //    object holder with a bound C++ payload — semantic, post-
                                     //    deref); CLASS form: host-registered vs script-declared
```

`type_of` is unchanged (back-compat: coarse kinds). `reflect::type_name` is its precise sibling —
class name for objects (**the naming-ladder name**: `class_definition::get_name()`, which the
does-all-things work keeps identical to the serialization `$type` and the script-visible
constructor name — reflection must never mint a third name), the type_info `canonical_name()`
for typed containers, the kind name for primitives, `"any"`-element containers report the open
form (`"array<>"`). Types are SEMANTIC: deref references and unmask `TYPEID_CPP_BOUND` via the
semantic index — reflection must never report storage wrappers (the var-flattening trap family).

### reflect::fields entry shape

- `name` (string), `type` (string: declared type's `canonical_name()`; `"var"` for any-tagged;
  `"auto"` for undeclared-inferred), `kind` (`"field"|"observable"|"signal"` — signals are
  members here, so ONE enumeration covers them; the schema's `is_signal`/`is_observable` flags
  drive it and no separate signals call exists), `access` (`"public"|"private"|"protected"`),
  `static` (bool), `from` (string: declaring class — `find_field_declaring_class` exists, and
  property grids group by it).
- **Script classes:** all declared fields including inherited ones. The synthetic
  `__cpp_object__` runtime field is excluded (same rule as to_json, json_archive.hpp:329).
- **Host-bound types:** `.property()` entries — name from the binder's field placeholder, type
  from the serialization registry's `property_metadata.type`, `access: "public"` (host members
  have no labels), plus `read_only` reported as `access: "public"` with no setter → `reflect::set`
  errors with the setter-missing family text. (Alternative considered: a fifth `read_only` key
  for host properties only; recommended — it's one bool we already retain.)
- **Arrays / maps / primitives / functions:** empty array. Fields are a class concept; the
  element/key/value types are `reflect::type_name`'s job (`"array<int>"`). Not an error — a property
  grid should be able to call `reflect::fields` on anything.
- **Opaque host tokens** (unregistered `make_value(T*)` pass-throughs): empty array, consistent
  with `is_registered_type` answering false (io.hpp:336-347 §13 ruling).

### Deterministic field order (the ruling this doc proposes)

Declaration order: **base-class fields first (in base declaration order), then own fields in
source order; statics after instance fields, in source order.** Base-first matches what the
language already does — `create_instance` seeds parent defaults before own defaults
(class_definition.hpp:645-658) and field initializers run in declaration order by explicit
design (script_class.hpp:138-143). Requires retention item 1. Methods have no layout meaning,
so `reflect::methods` sorts by (name, arity) — deterministic with zero retention; one entry **per
overload** (host same-arity type-overloads produce multiple entries).

### reflect::methods entry shape

`{name, arity, static, access, from, params}` where `params` is an array of
`{name, type, ref}` for script methods (full `function_decl` fidelity: ast.hpp:92-104,
defaults reported as `arity_min`/`arity` if we want the window — recommended:
`arity` = param count, `defaults` = number of trailing defaulted params) and `{type}` for host
methods (param_type_info; `std::type_index` mapped to the registered class name where possible,
else the base-type kind name). Synthesized members are filtered: `_get_*`/`_set_*` accessors,
the constructor (reported by `construct`ability, not as a method), `~destructor`,
`hot_reload_migrate`/`post_load` hooks stay visible (they're real methods). Operator methods
(`operator+` → name `"+"`) are included — they're introspectable like any method.

### Errors (catchable, one family)

Consistent family text, mirroring `parallel_transform:`'s prefix style (grammar.md §15):

```
reflect::construct: unknown class 'Gremlin'
reflect::get: unknown field 'hp' on class 'Creature'
reflect::set: unknown field 'hp' on class 'Creature'
reflect::invoke: unknown method 'roar' on class 'Creature'
reflect::fields: unknown class 'Gremlin'
```

All raise ordinary catchable runtime errors (reusing the nearest existing
`runtime_error_code`: `class_not_found`, the field/method-missing codes). `reflect::set` type
failures produce the **same** error as direct assignment (it routes through
`enforce_field_write`, class_definition.hpp:62/1546 — same text, same code). Access violations
produce the exact `enforce_member_access` text ("Cannot access private member 'x' of class
'C'"). Byte-identical on both backends because it is the same kernel.

---

## 3. Access model — RECOMMENDATION (Dev ruling pending)

**Reads see everything, with access reported; writes and invokes enforce exactly like direct
syntax; a separate host-side C++ API stays unrestricted.**

- **`reflect::fields` / `reflect::get` / `reflect::methods`: unrestricted, access reported in the entry.**
  Precedent is established and shipping: `to_json` serializes private fields today — the
  script-class path walks raw `instance->get_fields()` with no access filter
  (json_archive.hpp:324-335; verified: `nonpublic_members_` is consulted nowhere in
  serialization). The debugger's Locals view likewise shows everything in scope. grammar.md
  design note 12 records the ruling: "the host C++ API (`reflect::get`/`reflect::set`, serialization,
  reflection) is deliberately unrestricted." Reflection *reads* are tooling (property grids,
  save systems, debuggers) — hiding private state from them makes the features impossible, and
  the information already leaks through `to_json` anyway. The `access` key keeps the label
  visible so tools can *choose* to respect it (see example 3).
- **`reflect::set` / `reflect::invoke`: enforce from the caller's context, exactly like
  `v.name = x` / `v.name(args)`.** Reflection must not be an access-control bypass:
  `reflect::set(other, "hp", 0)`
  from top-level code on a private field errors precisely like `other.hp = 0` would. Mechanism:
  the builtin asks the backend for `current_access_context()` (retention item 4) and calls the
  same `detail::enforce_member_access` kernel both backends use, then `enforce_field_write`
  for the typed store. Inside a method of the declaring class, reflection can therefore write
  private fields of that class — same as direct syntax. This keeps one security story:
  *the access rules are a property of the language, not of the spelling.*
- **Host-side privileged API (editors): `jai::reflect` namespace in C++, not script-reachable.**
  Shape:

  ```cpp
  namespace jai::reflect {
      // Unrestricted, type-enforcing (routes through class_instance::set_field —
      // the conversion kernel still applies; access labels do not).
      void host_set_field(script_value obj, std::string_view name, const script_value& v);
      script_value host_get_field(const script_value& obj, std::string_view name);
      std::vector<field_desc> host_fields(engine&, std::string_view class_name);
  }
  ```

  This adds **no new capability** — `class_instance::set_field`/`reflect::get` are already public
  and unrestricted (note 12) — it just gives editor/tooling code a blessed, stable name so
  in-engine editors don't reach into `class_instance` internals directly. The MV editor's
  property grid uses this; script-side reflection never routes through it.

Alternative rejected: making reads access-filtered by caller context. It would diverge from
to_json (which can't take a context — it's called from C++), break the save-system use case for
private state, and cost a chain walk per field per call for a protection the host boundary
doesn't have anyway.

---

## 4. Parity + invariants

- **One registration, both backends, byte-identical.** The builtins register once per engine in
  `engine::initialize_engine_reference()` next to `format_value` — whose comment is the pattern
  citation: "One definition = byte-identical output and error text on both backends"
  (engine.cpp:89-91, 1177-1190). Both backends invoke registered functions through the same
  `std::function`, so parity is by construction (invariant #6) with **zero twins** — except the
  one `current_access_context()` accessor, which is a data read of state both backends already
  maintain for the shared enforcement kernel. (Registration could equally live in
  `stdlib/reflection.hpp` beside `type_of`; recommended: stdlib header, engine-core wiring
  decision at implementation. Either way it is once-per-engine, backend-neutral.)
- **Zero cost when unused.** Builtins only cost when called. No dispatch path, no member-access
  site, and no vm run-loop opcode changes (nothing near invariant #5's frame ceiling). The only
  always-paid cost is retention item 1: one `vector<uint64_t>` append per field at class
  *definition* time (cold path, 8 B/field). The access-context accessor is called only inside
  the builtins.
- **Deterministic output.** Field order = declaration order (retained vector); reflect::classes() and
  reflect::methods = sorted. Never expose raw unordered_map iteration order — symbol interning order
  differs per engine, so it is not reproducible across engines/backends (this is the §2 parity
  argument).
- **Results are snapshots, not live views.** `reflect::fields`/`reflect::methods`/`classes`/`bases_of`
  return ordinary value-semantic arrays/maps minted at call time; a later hot reload or
  `reflect::set` does not mutate a result you already hold. `reflect::get` returns exactly what
  reading `v.name` returns (heavy types are O(1) shallow `strong_ptr` copies — normal value
  semantics, not a reference into the instance; use existing `&` reference declarations if you
  want aliasing).
- **Hot reload.** `class_definition` objects are mutated in place by `redefine_class`, so
  `reflect::fields` after a redefinition reports the NEW shape with no extra work. During migration:
  `store_field_defaults` runs *before* the per-instance `hot_reload_migrate` hook loop
  (class_definition.hpp:1160-1172), so a hook calling `reflect::fields(this)` sees the new shape —
  which is what a migration hook wants. The order vector must be replaced at the same point as
  `field_defaults_` (same-commit rule as the backends' twin class-decl builders,
  interpreter.cpp:10086 / vm_backend.cpp:6900).
- **Budget + memory cap.** The builtins run under the caller's execution budget; result
  arrays/maps charge the memory cap like any script allocation. No terminal-error interaction.
- **construct == the call syntax.** `reflect::construct("C", args...)` resolves `"C"` in the global
  environment and calls it — the identical callable `C(args...)` uses (script ctor thunks
  defined at class-decl execution; host `add_overloaded_function` overload sets). Everything
  follows for free: ctor overload resolution by arity+type, trailing-default windows, the
  "defaulted-parameter ctor is NOT a converting ctor" ruling, host ctor dispatch.
  `construct_shared` applies the same reference-semantics tag `new` applies (grammar.md note 9).

---

## 5. Use-case validation (these become the integration tests)

### 5.1 Property-grid dump of any object

```cpp
function dump(obj) {
    print("=== {} ===", reflect::type_name(obj));
    for (auto f : reflect::fields(obj)) {
        if (f["static"]) { continue; }
        print("  [{}] {} {} = {}", f["access"], f["type"], f["name"],
              to_string(reflect::get(obj, f["name"])));
    }
}

class Creature {
public:
    string name = "grub";
    int hp = 100;
private:
    int secret_rage = 7;
}
dump(Creature());
// === Creature ===
//   [public] string name = grub
//   [public] int hp = 100
//   [private] int secret_rage = 7        <- reads see all; access reported
```

### 5.2 Spawn-by-name from a data table

```cpp
class Goblin { int hp = 30;  Goblin(int h) { hp = h; } }
class Troll  { int hp = 120; Troll(int h)  { hp = h; } }

var wave = [
    {"type": "Goblin", "hp": 25},
    {"type": "Troll",  "hp": 150},
    {"type": "Goblin", "hp": 35}
];

var spawned = [];
for (auto row : wave) {
    if (!reflect::has_field(row["type"], "hp")) { throw `bad spawn table row: ${row["type"]}`; }
    spawned.push_back(reflect::construct_shared(row["type"], row["hp"]));
}
print("{} spawned, first hp {}", spawned.size(), spawned[0].hp);   // 3 spawned, first hp 25
// reflect::construct("NoSuchClass") -> catchable: "construct: unknown class 'NoSuchClass'"
```

### 5.3 Save-system sketch: persist public non-static fields only

```cpp
function save_public(obj) {
    var out = {"_type_": reflect::type_name(obj)};
    for (auto f : reflect::fields(obj)) {
        if (f["access"] != "public" || f["static"]) { continue; }
        out[f["name"]] = reflect::get(obj, f["name"]);
    }
    return to_json(out);
}

function load_public(json) {
    var data = from_json(json);            // yields a plain map (keep _type_ a string key)
    var obj = construct(data["_type_"]);
    for (auto kv : data) {
        if (kv.first == "_type_") { continue; }
        if (reflect::has_field(obj, kv.first)) { reflect::set(obj, kv.first, kv.second); }
    }
    return obj;
}
```

This is `to_json`/`from_json` with a *policy* — exactly the composability reflection exists for
(the built-in pair serializes everything; this one filters by access, and the declaration-order
guarantee keeps the output diffable).

---

## 6. What v1 excludes (staging)

**v1 (all builtins over existing data + retention items 1/3/4):**
`reflect::fields, methods, has_field, has_method, get, set, invoke, call, construct,
construct_shared, classes, functions, function_arguments, method_arguments, globals, bases,
type_name, is_cpp_bound, instances, generation` — plus the reserved-namespace enforcement
itself (declaration-time error, both backends) and its two contract pins: script extension of
`math::` WORKS (openness is a contract too), and a script `namespace reflect {}` errors
identically on both backends.

`reflect::instances` walks the existing weak instance registry
(`class_definition::instances_` — the hot-reload migration list), purging expired entries;
engine-scoped by construction. `reflect::generation` is one counter on `class_definition`
bumped by `redefine_class`/migration — the cheap "should I re-ask" poll that completes the
live-query model (front door §C).

`invoke` is deliberately IN v1 — it looked like v2 but is the cheapest of the set:
`class_definition::get_method(id)` already returns the bound dispatcher
(`script_method_dispatch`), and calling it with `args[0] = receiver` is precisely what every
call site does; overload resolution, defaults, and hot-reload re-resolution all live in the
dispatcher already (script_class.hpp:187-211). Implementation is ~20 lines plus the access
check. Same story made `construct` cheap (env lookup + call).

**v2 candidates (each needs new machinery, none blocks v1):**
- **Metatype values / `typeof(T)`** — a first-class type value (new `script_value` alternative
  or an interned handle). Touches value layout → invariant #1 territory. Strings-as-type-names
  are v1's deliberate stand-in (ladder names compare with `==` and never drift from `$type`).
- **Attributes/annotations** (`@editor_range(0, 100) int hp;`) — parser + AST + retention work;
  the natural consumer is the editor property grid. Design separately, ONCE, with the editor
  campaign's hints-metadata gap — **reference design is GDScript's export-hint system** (the
  only one designed editor-consumer-first), not C# attributes.
- **Host param names** — dynamic_binder API extension (retention gap 2).
- **Field type constraints as values** (querying `array<int>`'s element type as a navigable
  object rather than parsing `reflect::type_name`'s string) — falls out of metatypes.
- **Namespace enumeration sugar** — free functions are v1 (`reflect::functions()` with
  QUALIFIED names, `reflect::call`, `reflect::function_arguments`), so a namespace listing is
  already a prefix filter; `reflect::functions("combat")` and `reflect::namespaces()` are v2
  conveniences over the same data. Enum reflection — different registry, same pattern, v2.
- **Debugger integration** — DAP `variablesReference` field expansion reusing `reflect::fields`.
- **Opt-in member-miss hook** (Lua-metatable/`__getattr__`-shaped interception) — member-miss
  RAISES today, so a per-class fallback hook lives entirely on the error path: zero hot cost
  when unused. Unlocks RPC stubs, mocks, lazy proxies. Future-spec; must not touch the member
  dispatch fast path.

---

## 7. Cost estimate

**Files touched:**

| File | Change | ~Lines |
|---|---|---|
| `include/jaiscript/stdlib/reflection.hpp` (new) | the 12 builtins + shared helpers (member resolution reusing the setter-first dispatch shape from stdlib/json.hpp:463-518) | 450-600 |
| `include/jaiscript/core/class_definition.hpp` | `field_order_` vector + accessor; replace in `store_field_defaults` | ~20 |
| `source/implementation/interpreter.cpp` | append to `field_order_` in the class-decl builder (~:9784) | ~5 |
| `source/implementation/vm/vm_backend.cpp` | twin of the above (~:6900) — same commit | ~5 |
| `include/jaiscript/core/execution_backend.hpp` | `virtual const class_definition* current_access_context() const` | ~3 |
| interpreter.hpp / vm_backend.hpp | the two overrides (`env->find_access_context()`) | ~8 |
| `engine.cpp` / `engine.hpp` | class-name enumeration accessor; wire `register_reflection_functions` | ~30 |
| `docs/grammar.md` + site | builtin catalog note (like the §15 parallel-builtins note) | ~20 |

Rough total: **600-750 lines of implementation**.

**Test plan** (`source/tests/stdlib/reflection_tests.cpp`, new file → CMake reconfigure; whole
suite runs on both backends via `--backend=vm`):
- The three §5 scripts verbatim (integration).
- Field-order pin: declaration order incl. base-first, stable across two engines, stable across
  a cosmetic hot reload, updated by a field-add/remove/retype reload (new shape).
- Access matrix: get/set/invoke × public/private/protected × caller = top-level / declaring
  class method / subclass method / unrelated class — set/invoke errors match direct-syntax
  error text byte-for-byte; reads succeed everywhere with correct `access` values.
- Error-text pins for every family message in §2 (parity: identical on vm).
- Host-bound type: property list + types from a `dynamic_binder` fixture; read_only property
  set_field error; method params report types; opaque token → empty + `is_registered_type`
  false; array/map/primitive → empty; `reflect::type_name` covers typed/any containers.
- construct: overload resolution, trailing defaults, non-converting-ctor ruling holds through
  `construct`, `construct_shared` reference semantics (mutation visible through the copy).
- Hot-reload: `reflect::fields` inside a `hot_reload_migrate` hook sees the new shape; snapshot
  results unaffected by a subsequent reload.
- Perf guard: existing benchmark suite unchanged (no hot path touched — assert no new
  regressions in the core-ops rows rather than adding a reflection bench; reflection itself is
  cold-path by definition).

Rough total: **500-700 lines of tests**. Overall: a focused 2-4 day change, dominated by the
access-matrix and parity pins, not the mechanism.
