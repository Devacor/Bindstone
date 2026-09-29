# Typed flat array storage (design + staging)

**Goal.** `array<int>` / `array<float>` backed by raw primitive buffers
(`std::vector<script_int>` / `std::vector<script_float>`) instead of
`std::vector<script_value>` (32 B/element), behind the SAME script-visible API and the
SAME semantics. Attacks GLOOM's INDEX family (~16k ops/tick × ~450 cy), memory bandwidth
(13.6k-element grids: 435 KB → 109 KB), value-semantics clone cost (typed clone = memcpy),
and makes parallel all-primitive proofs true by construction. Part of the 4-point
structural plan (2026-07-10); read with `invariants.md` (§1 layout, §3 reference cells,
§6 parity) and `reference_model.md`.

## Decision: one TYPEID, a tagged node class

`TYPEID_ARRAY` stays 6. The pointee of variant alt 6 changes from
`std::vector<script_value>` to a new `class script_array` (kind ∈ {hetero, i64, f64};
hetero wraps the old vector, typed kinds hold raw buffers). Rationale (scouted 2026-07-10):

- ALL raw variant access to alt 6 is confined to value.hpp/value.cpp (~11 sites); every
  other consumer (224 sites / 34 files) goes through five accessors
  (`as_array`, `unchecked_as_array`, `checked_as_array`, `get_array_storage`,
  `unchecked_get_array_storage`). Changing the pointee type makes the COMPILER enumerate
  every site — no silent misses, unlike a new TYPEID which leaves every `is_array()`
  guard site free to mishandle the new alternative at runtime.
- The `script_array*` node pointer stays the ONE stable identity: `operator<=>` ordering,
  parallel borrow tags, parallel_transform node-alias keys, `is_unique_reference()` COW
  probe all carry over unchanged.
- `strong_ptr<script_array>` is still pointer-sized + nothrow-move (invariants §1 holds).

Node sketch:

```cpp
class script_array {           // heap node behind strong_ptr; identity = this
    kind kind_;                // hetero | i64 | f64
    std::vector<script_value> values_;   // hetero
    std::vector<script_int>   ints_;     // i64
    std::vector<script_float> floats_;   // f64
    // API: size/empty/clear/reserve; get(i) materializes; typed raw accessors
    // (int_at/float_at/int_data/...); push/set with node-keyed coercion at call sites;
    // hetero_values() asserts kind==hetero (for provably-untyped paths);
    // clone() is kind-preserving (typed = buffer copy).
};
```

Three vectors (not a union) v1 — arrays are heap nodes, node count is small; simplicity
and branch-predictable kind dispatch win.

## The soundness invariant: tag ⟺ node kind

The store-enforced element type is what makes raw buffers sound. Scouted facts it rests on:

1. Element type is carried by the runtime VALUE (`type_info_` with `type_params[0]`),
   minted at `make_array(element_type)` and typed decls — not per-variable-only.
2. Value semantics protect the tag: decl-from-lvalue and by-value param binding go
   through `clone_for_assignment` (interpreter.cpp:6070, :11874 + vm twins) → every
   tag-stamp site (`set_type_info(any)` on `var` binding, interpreter.cpp:6141,
   :4997-5011, vm_backend.cpp:3007-3013) operates on a UNIQUE node (fresh clone or moved
   temporary). Shallow-sharing views (`var&` refs, subscript-read copies, pushed-element
   reads) all PRESERVE the tag.
3. **A `var` keeps a typed array typed** (Dev ruling 2026-09-28): a var slot stamps a typed
   array with the `dynamic_slot` twin of its own tag (`engine::get_type_info_array_dynamic`,
   `detail::var_stamp`): same name, id and element type, so every element boundary still
   reads `array<T>` (`var v = ints; v.push("s")` raises like `ints.push("s")`), while every
   store path reads the twin as a var slot (`detail::var_slot`), so `v = "hello"` rebinds. A
   typed or `auto` declaration from the var takes the plain spelling again. **Demote-on-stamp**
   remains the net for `any` and open-element (`array<var>`) tags: a typed node is reachable
   ONLY through views whose tag names its element type — write paths on typed nodes never need
   promotion machinery; incompatible writes are exactly today's `array_element_type_mismatch`.

**Lane rule (as built, 2026-09-02, fuzz seed 129):** a fast lane that touches a buffer raw tests the
*kind* it was written for (`kind() == i64 || kind() == f64`), never `is_typed()`. `is_typed()` only says
"no script_value elements exist"; the byte and string kinds are typed too and must take the enforcing
`get`/`set` path. The vm's unfused element-store and compound-store lanes broke this once and stored an
int into a string buffer unchecked (the interpreter raised); the pins live in the Strong Types battery,
and the fuzz generator now draws typed declarations so the shape stays covered.

### Stage-0 prerequisite: the tag is NOT airtight today (two real holes)

- **enforce_type_compatibility same-base fast path** (interpreter.cpp:6195 + vm twin):
  `var a = ["s"]; array<int> b = a;` passes with NO element validation — a string can
  land behind an `array<int>` tag TODAY. This already breaks `statically_all_primitive`'s
  premise (parallel_transform.cpp:1023-1047) — latent parallel unsoundness independent of
  this design. Fix: element-wise validate/convert (int↔float coerce, else error) when an
  array/map value crosses into an `array<T>`/`map<K,V>` binding whose element type is
  concrete. All-match = pass-through scan; any-convert = rebuild (the value at these
  boundaries is already a fresh clone).
- **Typed literal init skips element conversion**: `array<float> a = [1,2,3]` stamps the
  tag but keeps int payloads, contradicting the decl comment (interpreter.cpp:6125 "convert
  their initializer exactly like assignment does") and push semantics. Same fix covers it.

Both are red-first behavior FIXES (error where silence was; conversion where passthrough
was) aligned with the language's documented contract. Flagged to Dev in the session report.

Stage-0 fallout finding: retagging typed array FIELDS surfaced a latent element-predicate
blind spot — `is_element_type_compatible` never accepted shared_ptr-tagged instances into
object-typed element slots (`children.push(child)` on `array<ScriptNode>` with handle
children), which had only "worked" because field-typed arrays were silently unenforced.
Fixed in the shared kernel to mirror field-write semantics (handles store shared, exact
class-name match); pushing a handle into a LOCAL typed array now also works (it errored
before — widening consistent with `var p = new P()` ≡ `shared_ptr<P>`).

Non-goals v1: `slice`/`filter`/builtins still return untyped (hetero) results (element-type
retention is a separate ruling); `array<bool>`/`array<char>`/typed maps deferred; `array<T>`
where T is non-primitive stays hetero with today's one-level enforcement.

## Element lvalues (the hard part)

Typed nodes have no `script_value` elements, so nothing can hand out `script_value&`.

- **Reads** materialize a value (int64 + type_info + engine — cheap): `exec_index` rvalue,
  range-for `auto` (vm_backend.cpp:8656, interpreter.cpp:9629), borrows
  (parallel_transform.cpp:1953 — returns raw primitive, no boxing at all), fused subscript
  operands (vm_backend.cpp:4037-4130 return `const script_value*` — typed branch fills a
  caller scratch, same shape as `bound_decoded_temp`, invariants §4).
- **Direct-write fast paths** get FASTER: `exec_index_store` (vm_backend.cpp:4914),
  `exec_index_compound_fused` (:4964), interpreter compound fast path (:4761) branch on
  node kind → coerce + raw primitive store, no script_value destruct/construct.
- **reference_holder ELEMENT mode** (invariants §3) carries over: container field becomes
  `strong_ptr<script_array>`, still owner-pinned + index re-resolve per touch. On typed
  nodes: `deref() const` materializes into a holder-embedded scratch, rebuilt only when the
  slot's handle changed for obj/str kinds (`view_current`; scalar kinds re-store per touch)
  (legal: the documented contract already forbids caching the returned address);
  `assign_through` dispatches on node kind. `resolve_target()` (script_value*) CANNOT
  represent a typed element — it asserts hetero, and its ~6 caller chokepoints
  (vm exec_index_assign :4761→4779, exec_index_compound :4866, interpreter :5585/:5608,
  ref_lvalue store-through, value.cpp assignment twins) check
  `holder->typed_element_kind()` FIRST and take a load/compute/store path.
- **Mutable `deref()` callers** (++/-- on ref locals, interpreter.cpp:3801→3833 etc.) must
  be audited: each gets a typed-element branch (load-modify-store). A scratch write-back
  would be silent data loss — this is the one place wrongness hides; Debug-assert that
  mutable deref is never called on a typed-element holder once callers are converted.
- **ref_lvalue.hpp** subscript steps (:294-313) mint the same ELEMENT holder; store-through
  converts from `refLocal.deref() = v` to mode-dispatched assign (identical semantics on
  hetero — pin with tests).

Bounds/OOB semantics unchanged: hard error both directions, no grow-on-write (arrays);
holder re-resolve still bounds-checks per touch ("removed array element" behavior kept).

## What gets simpler

- Store-side element type checks (`vm_is_element_type_compatible` + twins) become a node
  kind check; conversion is a static int↔float coerce.
- `normalize_element_inplace` and the parallel barrier: typed nodes are all-primitive by
  construction — `statically_all_primitive` = kind check, borrows read raw primitives,
  `raw_input` workers never touch a script_value element.
- `clone()` / `parallel_detached_copy`: typed = buffer copy (memcpy-class) + one
  memory_charge. Value-semantics copies of typed grids collapse in cost.
- transient_read distinction collapses for typed reads (every read is already a value).

## Staging (every stage: Debug-first, red-first, full suites ×2 backends, fuzz, GLOOM
hashes 1503537018@600t + frame parity, Release A/B same-session; one-line commits)

- **Stage 0 — tag hardening.** Red tests for the two holes (+ map analog) + pins for the
  value-semantics guarantees the design leans on (decl/param clone, `var&` preserves tag).
  Fix in a shared detail/ kernel used by both backends (invariants §6). Independent value:
  closes today's statically_all_primitive soundness hole.
- **Stage 1 — the node funnel (mechanical, zero behavior).** Introduce `script_array`
  hetero-only; alt 6 pointee flips; compiler drives conversion of all ~224 sites (mostly
  `->values()`); `make_element_reference`/holder/borrow signatures follow. Suites must be
  bit-for-bit; GLOOM A/B must be NEUTRAL (guard against accidental indirection cost).
  This is the flatstack stage-2a "accessor funnel" trick: mechanical first, so the typed
  diff reviews small.
- **Stage 2 — typed kinds.** Mint at typed decls; demote-on-stamp at the enumerated stamp
  sites; element paths + holder + ref_lvalue + builtins get/set-generic; typed fast paths
  in the INDEX family; mutable-deref audit. The big test stage: aliasing through refs,
  range-for auto&/auto, f(arr[i]) by-ref, ++ through refs, OOB, shrink-under-ref, COW
  uniqueness, hot reload with typed fields, coroutines holding element refs.

  **As-built (2a, 2026-07-10):** typed kinds live behind `engine::typed_array_storage()`
  (scaffold flag, default OFF — DELETE when the default flips after stage 3 bake).
  Deviations from the plan above, each deliberate:
  - The **boundary kernel is the one typed-node producer** (not make_array alone):
    crossing into `array<int>`/`array<float>` rebuilds into the raw-buffer kind;
    same-kind sources pass by proof-of-kind. `make_array(element_type)` mints typed
    only under the flag (empty literals ride the rebuild).
  - **Demote-on-stamp lives inside `set_type_info`** (the ONE chokepoint) rather than
    at the ~16 enumerated stamp sites — a missed site is impossible; a demote of a
    shared node is value-correct (identity + contents preserved), merely losing form.
  - Holder ELEMENT mode: typed deref materializes into **cell_storage as a per-touch
    scratch** (`materialize_typed_element`, `has_elem_scratch`); `resolve_target()`
    returns null for typed (callers pre-check `typed_element()` and use the
    scratch-target + buffer-commit shape); in-place ++/-- commits via
    `commit_typed_element_scratch`; kernel stores go through `ref_write_through`.
  - INDEX-family fast paths **bail to slow replay** on typed nodes (2a correctness;
    2b adds the raw load/store fast paths with the profiler watching).
  - **Parallel: the stage-2a bridge demotes a typed SOURCE in place at the barrier**
    (parallel_for + parallel_transform); typed CAPTURES already borrow as raw reads
    (`parallel_borrow_subscript_read` typed branch). Stage 3 replaces the source
    demotion with raw typed worker access.
  - `unchecked_as_array`/`as_array`/`checked_as_array` **Debug-assert hetero** — any
    unconverted path trips loudly under tests/fuzz instead of reading empty.
  - Tests pin a 4-way battery: every snippet runs interp/vm × flag off/on and all four
    outputs must be byte-identical ("typed storage is observationally invisible"),
    plus C++-level checks that buffers actually engage and laundering actually demotes.
  **As-built (2026-09-02, Dev ruling: the representation follows the declared type).**
  The scaffold flag is DELETED: `array<int>`/`array<float>` are raw buffers always,
  `array<var>` (and `array<auto>` without a uniform inferred type) is the only
  `script_value`-vector form, and moving between them is an explicit language step
  (`array<var> x = ints;`): the boundary kernel (`enforce_container_boundary`, the one
  table behind decl, assignment, parameter, return, field store and `cast`) REBUILDS a
  typed source into a fresh vector node under the `array<var>` tag — a full copy, the
  source keeps its buffer — exactly as `array<int> back = x;` rebuilds the buffer the
  other way; `set_type_info` demotes any typed node that still meets an open-element
  array tag (the net under the kernel). No path demotes silently (a demote is a full
  copy the type never asked for). Consequences at the C++ boundary, consistent in every build:
  - `as_array()` / `checked_as_array()` are the `array<var>` view; on a typed node they
    return the one error (`typed_array_vector_view_message(kind)`, naming the element type) instead of asserting in
    Debug and reading empty in Release. `unchecked_as_array()` keeps its precondition.
  - Readers iterate kind-generically: `script_array::for_each` (hetero by reference,
    typed as boxed temporaries) in the json/binary archive writers, stdlib to_json,
    the hybrid-archive reader, `concatenate`; homogeneity validators and the parallel
    primitive scans short-circuit on typed nodes.
  - `bound_array<T>` sits on the NODE (a value handle keeps it alive) and dispatches
    through get/set/push/truncate: `bound_array<int64_t>&` mutates the raw buffer in
    place; a typed buffer binds arithmetic element types only; `bound_array<script_value>&`
    and `const std::vector<script_value>&` bind `array<var>` only.
  - `as<std::vector<T>>` / the conversion registry hand exact-width payloads over from
    typed buffers without boxing (`std::vector<script_int>` from i64, `std::vector<script_float>`
    from f64); narrower targets keep the per-element range-checked path.
  - parallel_transform no longer demotes a typed input at the barrier: it reads a boxed
    snapshot (stage 3's raw typed worker reads replace the snapshot).
  Pinned: `Script Container Tests` (typed_buffer_bound_array_mutates_in_place,
  script_value_vector_view_is_array_var_only, ...), the strong-types battery (now both
  backends on the one representation), the DOOM tape (5.96 -> 5.70 ms/tic).

- **Stage 3 — parallel + GLOOM payoff.** Typed borrows/raw_input; kind-checked static
  proof; type GLOOM's PIX/zbuf/ROWIDX/mappack as `array<int>`/`array<float>` (globals get
  typed decls; `var&` param plumbing already tag-safe, banked as hash-safe). Measure:
  Release A/B, expect INDEX/INDEX_STORE counts unchanged but cycles down + bandwidth win.
- **Stage 4 — typed builtin fast paths.** clone/slice/reverse/join/sort-default on raw
  buffers; C++ interop bulk paths (`vector<T>` ⇄ typed array near-memcpy,
  conversion_registry_impl.hpp:15-45, engine_impl.hpp:166-195). Measure.

- **Stage 5 — every element type (Dev direction 2026-09-02).** As-built the same night:
  `b8`/`c8` (bool/char, one shared byte buffer) and `str` (string handles, 8 bytes,
  sharing exactly the object a boxed element shares) joined `i64`/`f64`; `auto`/`array<auto>`
  uniform primitive literals infer the element type and take the buffer. Byte and string
  kinds box through `get`/`set` in the INDEX family (raw lanes stay int/float — a
  `raw_bool` decl sink is the cheap next lane). The parallel all-primitive check excludes
  `str` (handles never borrow raw; `statically_all_primitive` already excluded strings).
  - **byte kinds — BUILT (`kind_t::u8` / `kind_t::i8`, Dev ruling 2026-09-28).** `array<uint8>` / `array<int8>`
    share the `b8`/`c8` byte buffer, the kind carrying the sign (1 byte per element against 8 for `i64`). The element
    type is an int type_info named `uint8`/`int8` (`native_size` 1, `is_byte_element()`, interned apart from `int`);
    `get` widens to int, `set`/`push` take a pre-converted in-range int, and the range kernel
    (`detail::byte_range_error`) sits after the int rows of `convert_container_element`, so every store path meets it.
    `convert_byte_buffer` converts byte <-> int/float buffers on the raw payloads. The raw lanes (vm index stores,
    `op_t_load`, typed index stores, cell loads/stores/pushes, push append) take the kinds and miss on anything out of
    range, which the enforcing path then raises.
  - **object kind — BUILT (`kind_t::obj`, 2026-09-02).** `array<UserType>` (script or
    bound C++ class) is a buffer of `obj_slot{strong_ptr<object_holder> handle, type_info_ptr tag}`
    (16 bytes vs 32 boxed). The per-slot tag reproduces the boxed value exactly on `get`
    (a `shared_ptr<K>`-tagged element stays shared, a plain one stays a value), so every
    reader/writer that goes through get/set/push/for_each is unchanged. Homogeneity is by
    **exact class** — that is the element rule both backends enforce today (a `Derived`
    is rejected from `array<Base>` on push, declaration and array-to-array; probed before
    building), so the kernel's proof-by-kind shortcut also compares the array tag's class
    (`same_object_class`); object sources into a different class take the per-element
    check (mismatch text unchanged), and a rebuild copies handles as-is — ownership
    (clone or share) is the assignment/binding site's decision exactly as before. `clone()`
    and the parallel detach deep-clone per slot; by-value range-for clones the slot like a
    boxed element (the first probe diff caught the raw-read aliasing on both backends);
    default `sort` orders by held-object identity like the boxed compare; parallel
    workers exclude the kind (handles never borrow raw); `bound_array<Class>` binds it
    (every access boxes through get/set). 37 behaviour probes captured on the pre-change
    build are byte-identical on both backends after the change and live in the Strong
    Types battery. **Ruling needed from Dev:** Dev's target shape `array<CreatureBase>
    {VoidElemental, LifeElemental}` needs subclass admission, which today's exact-class
    rule forbids (C++ `vector<Base>` slices; `vector<shared_ptr<Base>>` does not). The
    slot already carries a per-element tag, so admitting subclasses is a kernel-rule
    change, not a layout change. Until then the C++ boundary payoff is exact-class:
    `std::vector<std::shared_ptr<K>>` / `std::vector<K>` extraction can prove the class
    once per array (next step).
    **RULED 2026-09-02 (Dev): subclasses are admitted.** Every element gate accepts the class
    or a subclass (exact id first, then `class_derives_from`'s parent walk); the per-slot tag
    is what makes a Derived element keep its own fields inside `array<Base>`; whole-array
    conversions prove the source class once; the boundary fast path accepts a derived tag.
    **`parallel_for` over an object-kind source keeps the node typed (Dev ruling 2026-09-04:
    the declared type is a proven fact about every element).** The barrier scans the slot
    tags (16 B each, no instance read) and takes the declared-type proof
    (`declared_class_exclusive`): every present class and every class a reached nested
    field can hold - registered subclasses included - is value-closed and no field the body
    reaches can touch a shared string; with `script_array::live_elem_refs == 0` (direct
    element references AND references minted through an element, `arr[i].f`, both count)
    the element walk is skipped entirely. A body naming a string-reachable field, a live
    reference, a shared_ptr-tagged or host slot, or a non-flat subclass falls back to the
    per-element walk over the slots. Workers bind `auto&` to element references on the typed
    node (the slot's handle materializes per touch) and `auto` to a per-iteration boxed slot.
    Primitive kinds still demote at the barrier until stage 3.
  - **maps** (`map<string,int>` …): the same rule — typed key/value storage keyed by the
    declared types, `map<var,var>` the only dynamic form, ordering contract kept
    (`script_map` stays ordered). Wins: key compare without a `script_value` per entry,
    value footprint. The boundary kernel's map branch is the producer, as for arrays.
  - **parallel_transform stage 3**: workers read typed primitive inputs raw (today a typed
    primitive/string input is read through a boxed snapshot — no demote — one copy per
    region; an object-kind input already lends its slots, `typed_input`, and its elements
    pass the same declared-type proof as `parallel_for`'s).

## Stage 6 — typed maps (designed 2026-09-02, not built)

Dev's rule: "maps follow similar rules" — `map<K,V>` with spelled key and value types is a
typed node, `map<var,var>` (and untyped literals) the only dynamic form, conversion an
explicit assignment, no silent demote, no Debug/Release difference. The ordering contract
(`script_map` is ORDERED by `script_value <=>`: type rank, then content) is kept.

**Size of the job.** `script_map` is a bare `std::map<script_value, script_value>` alias
touched directly at 99 sites (value.hpp 21, conversion_registry.cpp 13, bound_map 11,
value.cpp 10, function_binder 9, reflection/json/archives/backends the rest). Stage 0 is
therefore the array campaign's stage 0 again: a node class (`script_map_node`) with a
`kind` and funnels (`find`/`get`/`set`/`erase`/`size`/`for_each_entry`/`materialize`/
`demote_to_hetero`), every direct user moved onto the funnels while the only kind is
`hetero` (byte-identical behaviour, gated by suites + hashes), THEN typed kinds.

**Kinds.** Key kinds `str` (string handle, compared by content) and `i64`; value kinds
`i64`, `f64`, `str`, `obj` (the array's obj_slot rule: exact class, per-entry tag). A
sorted `std::vector<std::pair<Key, Value>>` per kind keeps the ordering contract with one
allocation and cache-friendly lookups (binary search on 8-byte keys); the dynamic kind
stays the `std::map`. Iteration order is identical to today's (ascending by content;
strings compare by bytes exactly like `script_value <=>` on two strings).

**The hard part is the same as the array's: entry lvalues.** `m[k] = v`, `m[k].field = 1`,
`m[k]++`, `auto& r = m[k]` all go through the reference holder's MAP-ENTRY mode
(`container_map` + inline key → `&it->second`). A typed entry has no `script_value` to
point to, so the holder gets a `typed_entry()` mode mirroring `typed_element()`: materialize
into the holder scratch, commit through `set` (ref_write_through / ref_store_through
already dispatch on `typed_element()` — add the entry twin). Auto-insert-on-read stays
forbidden (the map-read audit fix).

**Producers.** The boundary kernel's map branch mints typed nodes exactly like the array
branch (`make_map(key_type, value_type)` by kinds; same-kind proof; per-entry check
otherwise; rebuild copies entries as-is). Literals with spelled types produce typed nodes;
`auto m = {"a": 1}` infers `map<string,int>` when uniform (the array's inference rule).

**Yardstick.** Foundry row `Map lookup: map<string,int> 100 keys, 1000 dynamic-key reads`
(added with this design; first reading 272 µs interp / 126 µs vm at `c5a47278`) and the existing
`Map copy by value: 100 string keys (x20)` (219 µs). Dynamic string keys build a script
string per read today; a str-keyed typed map compares bytes without one. Measure before
building stage 1: if the dynamic-key row does not move ≥2× in a prototype, the campaign's
value is footprint only.

**Order.** Stage 0 (node + funnels, hetero-only) is the one to ship first and alone; it is
the same shape as `d320732c`'s array stage 0 and the same gates apply.

## Risk register

1. **Scratch write-back loss** (mutable deref on typed elements) — silent. Caught by:
   caller audit + Debug assert + ref-mutation suites (++ through ref, compound through ref).
2. **Stamp site missed** (an any-tagged view over a live typed node, or a var slot read as
   locked) — caught by: enumerating `set_type_info` call sites on array values, the var
   battery (`var a = typed; a.push("s")` raises, `a = "s"` rebinds), fuzz.
3. **Node identity drift** (borrow tags / `<=>` / parallel node-alias keys) — the node
   pointer must never change meaning; demote mutates IN PLACE (same allocation). Caught
   by: parallel suite + borrow identity tests.
4. **Hetero-path regression** (extra indirection/tag check on untyped arrays — most
   scripts). Caught by: stage-1 NEUTRAL gate on GLOOM (all arrays still untyped there)
   + container benchmark rows.
5. **Parity drift** — every element path is twinned (invariants §6); shared kernels where
   possible (stage-0 fix, holder logic lives in value.cpp shared by construction).
