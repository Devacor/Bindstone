# Script-extensible C++ objects: `script_extensible` + `engine::extend`

Pinned by `Hybrid Serialization Tests` (polymorphic_serialization_tests.cpp; every assertion
on a slot-carrying base and a map-only base, both backends). The mechanism
(slot hooks, the archive envelope, the initializer hook, migration internals) is §5;
serialization shape and archive contracts stay where they are (`SERIALIZATION.md`). This doc
states the contract; when the two could disagree, this one wins for *what a hybrid is*,
`SERIALIZATION.md` wins for *how bytes look*.

## 1. What it is

A script class may derive a bound C++ class:

```jaiscript
class Voidling : ServerCreature {
    int chompCount = 0;
    void update(double dt) { chompCount = chompCount + 1; attack(nearestEnemyId(2.0)); }
}
```

Such an instance is a **hybrid**: one script object that *is* the C++ object plus a script
layer (typed fields, overrides, methods). `extend` attaches that layer to an object that
already exists — and the script decides which class:

```jaiscript
self.spawn = [=](var& creature) -> void { extend(creature, Voidling).spawn(); };
```

C++ never names the class. Its side is one line, when it wants one at all:

```cpp
engine->extend(creature, "Voidling");                 // add_global("hero", creature), before or after, hands script the layer
```

Nothing is registered, cast, keyed, or looked up by hand. From that line on, every road back
into script — a bound method returning `shared_ptr<Creature>`, a signal argument, a container
element, a serialized `shared_ptr<Base>` — hands script *the Voidling*, not a sliced view of
its base. That is the C++ rule "a base pointer to a derived object is the derived object",
carried across the boundary.

## 2. The contract

- **S1 — One object, one identity.** A C++ object has one script identity: its layer, or
  before any extend, the plain wrapper `make_object` first handed out, so an extend after
  binding re-classes what every holder has. Every path that hands the object to script
  (`make_object`, boundary returns, archive load) yields that identity while it lives. There
  is no second wrapper, no per-crossing instance.
- **S2 — Ownership follows creation.** A hybrid the *script constructed* (`Voidkin()`) owns
  its C++ object (the instance's native object is a strong handle; the object's slot points back weakly). An
  object that was *extended* owns its layer for its lifetime (strong slot; the layer holds the
  object without owning it, weakly for `shared_ptr` extraction). Neither direction cycles.
  When an extended object dies while a script still holds its layer, the layer is retired
  (S7). A type without the slot cannot own anything, so its extended layers are engine-owned
  until `script_layer_detach` (the layer keeps the object alive meanwhile).
- **S3 — `extend` is not construction.** No constructor runs. Field initializers run in
  declaration order, base-first, exactly as construction would seed them. A class meant for
  extension needs no constructor; a class that *has* one is still extendable (its body simply
  never runs). Script-side `Voidling()` construction runs the constructor as usual.
- **S3b — `extend(x, C)` works in place: idempotent, re-deriving, on pure script values too.**
  The instance `x` holds is re-classed to `C` where it stands (`class_instance::reclass`: field
  cells move to `C`'s slots without relocating, the way hot reload migrates), every field both
  classes declare keeps its value, `C`'s own fields take their initializers, and ids only the
  old class declared stay reachable in overflow. So every handle already pointing at the
  object follows — `extend(kin, Voidling); kin.chompCount = 1;` is the idiom, no rebinding.
  The result names the argument (C++'s `T& extend(T&, Class)`): a chained call reaches the
  object, `var& w = extend(v, C)` aliases it and `var w = extend(v, C)` copies it, the call-result rule of every function.
  `x` a C++ wrapper or a layer: `C` must derive `x`'s C++ base; the current layer's class is
  a no-op. `x` a pure script instance: `C` must derive `x`'s class. Only a layer owned by
  *another* engine is rebuilt (symbols are engine-local) and the old one retired (S6).
- **S3c — A stale wrapper is the layer.** A wrapper handle made before its object was extended
  re-points itself at the object's layer on first member access (`resolve_member_target`
  rewrites the handle's holder), so reads, writes, getters, setters and bound methods all
  reach the layer through it. Gated per C++ class: a definition no object of which was ever
  extended pays nothing.
- **S4 — Copy semantics follow the C++ base.** Assignment (`var b = a;`) deep-copies: a
  copyable C++ base copies its object too (a new hybrid, two identities); a non-copyable base
  throws at the copy site, like a deleted copy constructor. By-value capture (`[=]`,
  `[this]`) *shares* — a hybrid is an engine object and capture pins the exact object
  (`closures_and_captures.md`). Reads through boundary returns and call results are handles.
- **S5 — Zero static state.** The layer slot lives *in the object* (`script_extensible`) or
  in the owning engine's map — never in a global. Type traits (is this type extensible?) are
  compile-time; every registration is engine-held.
- **S6 — Layers move between engines; they are never shared between threads.** A script
  layer belongs to exactly one engine (its symbols, its class definition). Moving it is an
  explicit handoff through the name-keyed representation (`$script` JSON), on one thread,
  with both engines alive. `B.extend(obj, cls)` on an object layered by `A` *is* that move.
- **S7 — Error, never silently degrade.** `extend` with a class that does not derive the
  object's C++ base (or, for a script value, its class) is refused, naming both. A layer
  retired by `script_layer_detach`, a re-derive, a move, or its object's death errors on any
  C++ member access instead of reaching the object (its own script fields remain readable —
  they are its values). Inside the engine every refusal is an error code
  (`extend_rejected`, `script_layer_retired`); only the host-facing `engine::extend` throws.
- **S8 — The one-name ladder holds.** A hybrid's `$type`, `reflect::type_name`, and the name
  `extend` takes are all the script class name. Its C++ base is `$val`'s type.
- **S9 — Backend parity.** Both backends run field initializers for `extend` through the
  same `execution_backend::apply_field_initializers` entry; the hook that records the layer
  (`class_instance::set_native`) is shared, so binder-ctor `super()`, `extend`,
  and archive load register identically by construction.
- **S10 — C++ properties are members.** Inside a hybrid's methods (and the lambdas made
  there) a bare `.property()` name resolves on the member rung of the lookup ladder
  (locals → members → globals, `detail/member_scope.hpp`): it reads through `_get_name` and
  stores through `_set_name` exactly like `this.name` — plain, compound and `++`/`--` — with
  the same texts; a same-named script field shadows it for both spellings. No field is ever
  materialized on the instance for a C++ property. By-reference bindings (`auto& r = name`,
  `f(name)` into a `&` parameter, `return name` from a `-> T&` method, `[&]` captures) bind
  the MEMBER ITSELF for a box-decodable member-pointer property and the accessor pair for
  every other property (internal/reference_model.md CPP-MEMBER / PROPERTY); `[=]` snapshots.
  A member METHOD name stays `Cannot take reference of undefined variable`.

## 3. Decisions

| # | Decision | Why |
|---|---|---|
| D1 | The slot in `jai::script_extensible` is a strong `shared_ptr<class_instance>` (set only for extended objects) beside the weak lookup pointer — 32 bytes, no engine pointer (the instance knows its engine). | One slot serves both ownership directions of S2; the engine is derivable; the destructor retires a surviving layer. |
| D1b | `extend` is a script builtin (engine-core, every engine has it) taking the bare class name — its constructor value carries the class definition — or a string. | Script controls extension entirely; C++ never bakes in a class name. |
| D2 | `property_owner<Derived, Bases...>` derives `script_extensible` unless a base already does; with several extensible bases the first one's slot is re-exported (`jai_script_slot`). | Every reflected type is extensible for free; no ambiguous member under multiple inheritance. |
| D3 | Types without the mixin still work through the engine-owned weak map keyed by complete-object pointer. | Third-party bases can't derive anything; the map is the universal fallback. |
| D4 | Slot writes and reads go through per-class hooks registered by `dynamic_binder<T>` when `T` is extensible; the `set_native` hook is the single choke point. | One place decides slot-vs-map; every adoption path already funnels there. |
| D5 | `make_object(shared_ptr<T>)`, `write_shared_payload`, and `engine::script_layer` branch statically on `is_base_of_v<script_extensible, T>`: slot deref (no `dynamic_cast`, no hash) else map. | The hot path is a pointer read; the map only pays where it must. |
| D6 | `extend` requires the class to derive the object's registered C++ class (through any script chain); the object is taken as it crossed the boundary (its dynamic class). | No pointer adjustment, no static-type guessing; a mismatch is a one-line error naming both. |
| D7 | `extend` on an object layered by *another* engine moves the layer: fields saved by name from the old engine, old layer retired, new layer hydrated. `script_layer_detach` does the first half alone. | S6 as a one-liner for extensible types; explicit two-step (`B.extend(obj, cls, A.script_layer_detach(obj))`) for map-only types, which no engine can see into another's map for. |
| D8 | A retired layer carries a flag; the C++ extraction funnels and the property-getter paths report on it. A retired *extended* layer references an object it no longer owns (fine: the object outlives it or the flag catches the access); a retired *constructed* layer keeps owning its object. | Stale use must not reach an object another layer speaks for (S7); no holder surgery, one flag. |
| D8b | An extended object's native object is non-owning: `bound_ptr` for member access (zero cost, guarded by the retired flag), `weak_data` for `shared_ptr` extraction (locks the object's own owner). | The object must be free to die; the layer must still serve `T&` at pointer-read cost and `shared_ptr` with real ownership. |
| D9 | Archive load runs field initializers before `$script` hydration. | Fields untouched during play were never materialized, so they were never saved; without this they load as null. |
| D10 | Names: `script_extensible` (members `script_layer`, `script_layer_to_json`, `script_layer_from_json`, `attach_script_layer`, `own_script_layer`, `detach_script_layer`, `jai_script_slot`), `engine::extend`, `engine::script_layer_detach`, `engine::script_layer`; engine internals prefixed `script_layer_`; the script builtin `extend`. Nothing generic (`detach`, `attach`) lands on property_owner-derived types — components own those names. | `adopt` already means definition-adoption in the binder; `extend` is the word the vision doc reserved for exactly this. One vocabulary, no pollution of derived classes. |
| D13 | Engine literals are interned once (`post_load`); errors travel as `checked_result` codes with symbol placeholders, thrown only at the host boundary. | House rules: interned lookups, error codes inside, throw at `execute`-level doors. |
| D14 | Re-derive re-classes the instance in place (`class_instance::reclass`) rather than minting a replacement and retiring the old one; a stale wrapper re-points its handle at the layer on first touch (S3c). | "Extend in place in all cases". Handles never go stale within an engine; no retired twins to explain. |
| D11 | Two-level chains (`Voidling : CreatureKind : ServerCreature`) are hybrids: every hybrid-semantic site resolves the C++ base with `class_definition::find_cpp_base_class()` (chain walk), never the direct-base field. | `cpp_base_class_` is only set on the class whose *direct* base is C++; every mid-class design needs the walk. |

## 4. API

Script:

```jaiscript
class Voidling : ServerCreature { int chompCount = 0; ... }
var v = extend(creature, Voidling);      // the layer; creature's next crossing hands it back too
v = extend(v, Voidspawn);                // re-derive (fields both classes declare carry over)
var q = extend(p, Pt3);                  // a pure script value re-derived in place; q copies it (var& q aliases)
```

C++:

```cpp
namespace jai {

// Mixin: lets an object carry its own script layer. property_owner derives it for you.
class script_extensible {
public:
    std::shared_ptr<class_instance> script_layer() const;    // live layer or null
    bool has_script_layer() const;
    std::string script_layer_to_json() const;                // {"$type": class, "$script": fields}; "" without a layer
    bool script_layer_from_json(const std::string& json);    // hydrate the live layer (re-derives on another $type)
    script_extensible& jai_script_slot();                     // the slot (re-exported under MI)
};

class engine {
public:
    // The builtin's door: the layer as a value. Throws at this boundary (codes inside).
    script_value extend(const script_value& object, const script_value& script_class);
    script_value extend(const script_value& object, const script_value& script_class, const std::string& fields_json);
    template<typename T> script_value extend(const std::shared_ptr<T>& object, std::string_view script_class,
                                             const std::string& fields_json = {});
    // The layer's fields as JSON; the object no longer carries a layer (the old layer is
    // retired). Empty string when there was no layer.
    template<typename T> std::string script_layer_detach(const std::shared_ptr<T>& object);
    // The live layer, if any — slot for extensible types, map otherwise.
    template<typename T> std::shared_ptr<class_instance> script_layer(const T& object) const;
};

}
```

Overrides need `override`; `this` is method-only; callbacks re-derive the creature from their
arguments and get the live layer back through the boundary (S1).

## 5. Mechanism

**The slot and the hook.** `class_instance::set_native` calls
`note_script_layer_owner`. It resolves the C++ base through `find_cpp_base_class()` (D11) and
asks the base's `class_definition` for slot hooks (D4): present → `attach(void*, weak)` writes
the object's slot; absent → `engine::note_script_layer(const void* cpp_object, const std::weak_ptr<class_instance>&)`
into the map. The
hooks are two function pointers the binder installs at `build()` when
`std::is_base_of_v<script_extensible, T>`; the `void*` is the native object's pointer, which by
the holder contract is a `T*` for the holder's type name.

**The three reads (D5).** `engine_impl.hpp make_object(shared_ptr<T>)`: extensible → slot
deref + engine check (`layer->get_engine() == this`, a foreign layer is not ours) → return
`make_object(layer->get_class_definition()->get_name(), layer, this)`; else the map
(`object_instance_of`) by complete-object pointer (`dynamic_cast<const void*>` for a
polymorphic `T`, `static_cast<const void*>` otherwise), which also returns the plain wrapper
already handed out. `archive_impl.hpp write_shared_payload`: same branch on `elem_type`, then
`engine::script_layer_save(layer, type, fields)`. `engine::script_layer(obj)`: same.

**`extend` internals** (`engine::extend_checked`, error codes): resolve the class
(`resolve_script_class`: a constructor value's `script_callable_thunk` payload carries the
definition — both backends mint constructors as that thunk — else a name through
`class_registry::find_script_class`); inspect the object value (a class-instance wrapper).
Pure script instance: `derives_from` check → `create_instance` → `apply_field_initializers` →
copy fields by id → return. Wrapper or layer: base = the wrapper's own definition, or the
layer's `find_cpp_base_class()` (D11); `derives_from(base)` (D6); object pointer from the
holder (`data`, else `weak_data.lock()`); current layer via slot hooks or map: same engine +
same class → return it; same engine + other class → carry its fields, `detach_object`; other
engine → `script_layer_save` there, `detach_object` there (D7). Then `build_script_layer`:
`create_instance` → `set_native(make_cpp_reference(base, object*, weak))` + `hooks->own`
for slot types, `make_cpp_object` (strong) + engine-owned entry otherwise → hook fires →
`backend->apply_field_initializers` → carried fields → `fields_json` hydration → `post_load`.

**Field initializers (S3, D9).** The pure virtual
`execution_backend::apply_field_initializers(instance, class_def)`; each backend builds the
init environment its constructor path builds (parented on the global environment, `this`
bound, access context = the class) and runs its existing `evaluate_field_initializers`
recursion. Limitation, stated: a class declared inside a function (whose initializers read
that function's locals) sees globals under `extend`; every extension-shaped class is top-level.

**Retired layers (D8).** `detach_object` clears the slot/map entry and marks the old layer
retired (`class_instance::retire_script_layer`); the native object stays as it was, so the
object lives as long as any old handle. `class_instance::get_cpp_object`/`get_cpp_object_as`
and the conversion registry's `checked_native_reference`/`checked_native_object` refuse a
retired layer, naming the class and the cause, and member access reports
`script_layer_retired`. The property-getter path in both backends wraps the
getter call in a fallback catch, so it checks retirement itself before calling a *bound* C++
getter (script getters and synthesized field accessors are left alone: the layer's own script
fields stay readable) and rethrows the report through the catch.

**Copy (S4).** `class_instance::deep_copy` registers the copy as the layer of the copied C++
object explicitly — its native-object write happens before the copy has a class definition,
so the `set_field` hook alone could not.

**Archive.** Write: unchanged envelope (`$type` script class, `$script`, `$val`). Read:
`read_shared_payload` → `engine::extend_loaded(script_class, void_ptr, fields)` = the fresh
path above with hydration; a slot type's object owns the layer and the plain pointer
returns, a map type's layer owns the object and the returned pointer aliases the layer.

## 6. Behavior

1. **Boundary identity (S1).** A bound method returning `shared_ptr<Base>` hands back the
   owning layer every call, not a fresh wrapper.
2. **Assignment copies, capture shares (S4).** `deep_copy` copies the C++ object through the
   registered copy function; `clone_for_capture` shares instances holding a native object.
   Alternative considered: make hybrid assignment share too (entity semantics). Rejected: it
   would make `var b = a;` mean two things depending on whether `a`'s class has a C++ base.
3. **`extend` moves foreign layers and re-derives silently-on-purpose (D7, S3b).**
   Alternative: throw and require `script_layer_detach`. Chosen: the object
   can only have one layer, so "extend here as C" has exactly one coherent meaning; the old
   layer's handles report on use (D8).
4. **32 bytes per `property_owner` object (D1/D2).** Every scene node/component grows by a
   `shared_ptr` + `weak_ptr`.
5. **By-value returns copy at the return site (C++ [stmt.return]).** A
   by-value `return e;` yields a value that aliases nothing the callee could name: a temporary
   (`return [1,2];`, `return make();`, `return a + b;`, `return T();`, `return this;`) is moved
   out; an lvalue read of a global, an outer-scope/captured variable, a field (`return items;`,
   `return this.items;`), an element or map entry (`return arr[0];`, `return m["k"];`), a boxed
   (reference-shared) local, or a call that returns a reference (`return g();` for `T& g()`) is
   deep-copied through the one store kernel exactly like `T tmp = e;` would copy it -
   `shared_ptr<T>` handles share, coroutine handles share, scalars and strings are copies
   already; the callee's OWN unboxed local or by-value parameter is moved out of its dying
   frame slot (NRVO / implicit move: one object, one destructor, no copy - a cell a finished
   ref-param bind left behind counts as own). A `-> T&` / `-> auto&` function keeps returning
   the reference handle. Consequently `f().push(4)`, `f().x = 5` and `for (auto& v : f())`
   touch the temporary, never the callee's storage; to share, spell it: `T& f()`,
   `shared_ptr<T>`, or `var& r = ...` on a ref-returning function. Coroutine completion values
   (`return x;` in a coroutine body) follow the same rule; `yield x` is unchanged. A
   top-level (script-level) `return x;` is not a function return and is unchanged. Kernel:
   `detail/return_conv.hpp` (`classify_return_operand`, `materialize_return_value`,
   `return_own_slot_payload`), one site per backend (interpreter `visit_return_stmt`, vm
   `materialize_return` at every return door). Handing such returns back as a reference handle
   and copying at `var p = f()` is rejected: it would make `f().push(4)` write through a
   by-value signature, and `var& p = f_value()` is an error.
   Gate: `Return Value Semantics` suite (both backends), `coroutine_completion_value_copies`,
   `typed_return_lands_on_copy`, plus the example hashes.

## 7. Risk register

- **MI ambiguity** (two extensible bases): D2 re-exports the first base's slot; the second
  base's slot is dead weight. Test: compile + runtime on a `property_owner<D, A, B>` shape.
- **Foreign-engine layer read through the slot**: `make_object` must check the layer's engine
  or a value from engine A leaks into engine B (symbol ids differ). Test: two engines, one
  object.
- **Engine death with live layers**: an instance's `engine_` is raw; a host-held layer that
  outlives its engine is the pre-existing "no script_value outlives its engine" rule, restated.
- **`keep_alive` on retired layers holds the object as long as any old handle lives**: by
  design; leak-shaped only if a host hoards handles it should have dropped.
- **Header layering**: `script_extensible.hpp` forward-declares `class_instance` and includes
  only `<memory>` and `<string>`.
- **Hot-path cost**: slot path is a `weak_ptr::lock` (one atomic increment — `std::weak_ptr`,
  not `strong_ptr`) per boundary crossing, with no `create_instance` allocation.

## 8. Test plan (both backends, `Hybrid Serialization Tests`)

Run every assertion twice: on a `script_extensible` C++ base and on a plain registered base
(map path), so slot and map cannot drift.

- extend: defaults applied (no ctor); C++ state reads through; override dispatches; hybrid
  survives json + binary; `extend` twice same class keeps the layer; another class re-derives
  in place (carried fields, initialized extras, new dispatch, every handle follows, a stale
  wrapper reads/writes/dispatches through to the layer); the wrong base names both; unknown
  class named; script sees refusals as catchable exceptions; arity and non-class arguments
  refused; `extend(kin, Slotkin); kin.chomp = 17;` pins the handle upgrade on both lookup paths.
- pure script re-derive: base fields kept, derived initialized on the same instance, chained
  access reaches it, same class returns the instance, non-derived class refused.
- ownership: an extended object's death retires a held layer (fields stay, C++ access
  reports); a slotless object's layer keeps it alive until `script_layer_detach`.
- mixin state door: `script_layer_to_json` carries class + fields; `from_json` hydrates the
  same class in place and re-derives on another `$type`.
- identity: boundary return of `shared_ptr<Base>` is the layer; mutation through it reaches
  the instance; a local bound to the return shares.
- two-level chain: fields at both script levels survive; leaf override dispatches; base name
  resolves.
- copy semantics (S4): assignment on copyable base yields two identities; non-copyable base
  throws at the copy site; `[=]` capture shares.
- migration: A extends, B extends the same object → fields intact on B, A's handle reports on
  C++ member access, object alive; `script_layer_detach` returns fields and clears; plain-base
  migration via the explicit two-step.
- no pollution: a plain C++ object crossing the boundary registers no layer.
- property_owner: `is_base_of_v<script_extensible, Component-shaped type>`; MI shape compiles
  and attaches to the first base's slot.
- archive: untouched initialized field survives a round trip (D9); back-references to a
  hybrid share the layer-owning control block.
