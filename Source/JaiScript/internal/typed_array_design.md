# Typed array storage

**Goal.** `array<int>` / `array<float>` (and every other spelled element type) backed by raw
buffers (`std::vector<script_int>` / `std::vector<script_float>` / ...) instead of
`std::vector<script_value>` (32 B/element), behind the SAME script-visible API and the
SAME semantics. Attacks GLOOM's INDEX family, memory bandwidth (13.6k-element grids:
435 KB → 109 KB), value-semantics clone cost (typed clone = memcpy), and makes parallel
all-primitive proofs true by construction. Read with `invariants.md` (§1 layout, §3
reference cells, §6 parity) and `reference_model.md`.

## Decision: one TYPEID, a tagged node class

`TYPEID_ARRAY` is 6. Its pointee is `class script_array`, which holds a `kind_t` (15 kinds,
types.hpp) and one buffer per storage form: `values_` (hetero), `ints_`, `floats_`,
`bytes_` (bool, char, u8, i8 and math vectors at their stride), `strs_`, `objs_`. Rationale:

- ALL raw variant access to alt 6 is confined to value.hpp/value.cpp (~11 sites); every
  other consumer (224 sites / 34 files) goes through five accessors
  (`as_array`, `unchecked_as_array`, `checked_as_array`, `get_array_storage`,
  `unchecked_get_array_storage`). Changing the pointee type makes the COMPILER enumerate
  every site — no silent misses, unlike a new TYPEID which leaves every `is_array()`
  guard site free to mishandle the new alternative at runtime.
- The `script_array*` node pointer stays the ONE stable identity: `operator<=>` ordering,
  parallel borrow tags, parallel_transform node-alias keys and the `detail::uniquely_held`
  COW probe all key on it.
- `strong_ptr<script_array>` is pointer-sized + nothrow-move (invariants §1 holds).

Node sketch:

```cpp
class script_array {           // heap node behind strong_ptr; identity = this
    kind_t kind_;              // hetero, i64, f64, b8, c8, str, obj, u8, i8, v2 .. rgba
    std::vector<script_value> values_;   // hetero
    std::vector<script_int>   ints_;     // i64
    std::vector<script_float> floats_;   // f64
    std::vector<uint8_t>      bytes_;    // b8/c8/u8/i8 and the vector kinds
    std::vector<strong_ptr<script_string>> strs_;   // str
    std::vector<obj_slot>     objs_;     // obj
    // API: size/empty/clear/reserve; get(i) materializes; typed raw accessors;
    // push/set with node-keyed coercion at call sites; clone() is kind-preserving
    // (typed = buffer copy).
};
```

Separate vectors (not a union) — arrays are heap nodes, node count is small; simplicity
and branch-predictable kind dispatch win.

## The soundness invariant: tag ⟺ node kind

The store-enforced element type is what makes raw buffers sound. The facts it rests on:

1. Element type is carried by the runtime VALUE (`type_info_` with `type_params[0]`),
   minted at `make_array(element_type)` and typed decls — not per-variable-only.
2. Value semantics protect the tag: decl-from-lvalue and by-value param binding go
   through `clone_for_assignment` (both backends) → every tag-stamp site
   (`set_type_info(any)` on `var` binding) operates on a UNIQUE node (fresh clone or moved
   temporary). Shallow-sharing views (`var&` refs, subscript-read copies, pushed-element
   reads) all PRESERVE the tag.
3. **A `var` keeps a typed array typed**: a var slot stamps a typed
   array with the `dynamic_slot` twin of its own tag (`engine::get_type_info_array_dynamic`,
   `detail::var_stamp`): same name, id and element type, so every element boundary still
   reads `array<T>` (`var v = ints; v.push("s")` raises like `ints.push("s")`), while every
   store path reads the twin as a var slot (`detail::var_slot`), so `v = "hello"` rebinds. A
   typed or `auto` declaration from the var takes the plain spelling again. **Demote-on-stamp**
   remains the net for `any` and open-element (`array<var>`) tags: a typed node is reachable
   ONLY through views whose tag names its element type — write paths on typed nodes never need
   promotion machinery; incompatible writes raise `array_element_type_mismatch`.

**Lane rule (fuzz seed 129):** a fast lane that touches a buffer raw tests the
*kind* it was written for (`kind() == i64 || kind() == f64`), never `is_typed()`. `is_typed()` only says
"no script_value elements exist"; the byte and string kinds are typed too and must take the enforcing
`get`/`set` path (a lane that tests `is_typed()` stores an int into a string buffer unchecked); the
pins live in the Strong Types suite, and the fuzz generator draws typed declarations so the shape
stays covered.

### Stage 0: the airtight tag

Every crossing into an `array<T>`/`map<K,V>` binding runs `enforce_container_boundary`
(element-wise validate/convert: int↔float coerce, else error; all-match passes through,
any-convert rebuilds), and a typed literal initializer converts its elements the same way.
Element stores check `detail::container_element_compatible` (container_enforce.hpp), which
mirrors field-write semantics: a `shared_ptr`-tagged instance stores shared into an
object-typed element slot, and a class or subclass matches.

## Element lvalues (the hard part)

Typed nodes have no `script_value` elements, so nothing can hand out `script_value&`.

- **Reads** materialize a value (int64 + type_info + engine — cheap): `exec_index` rvalue,
  range-for `auto`, borrows (`parallel_borrow_subscript_read` returns a raw primitive, no
  boxing at all), fused subscript operands (return `const script_value*` — the typed branch
  fills a caller scratch, same shape as `bound_decoded_temp`, invariants §4).
- **Direct-write fast paths**: `exec_index_store`, `exec_index_compound_fused` and the
  interpreter's compound fast path branch on node kind → coerce + raw primitive store, no
  script_value destruct/construct.
- **reference_holder ELEMENT mode** (invariants §3): the container field is
  `strong_ptr<script_array>`, owner-pinned + index re-resolve per touch. On typed
  nodes: `deref() const` materializes into a holder-embedded scratch, rebuilt only when the
  slot's handle changed for obj/str kinds (`view_current`; scalar kinds re-store per touch)
  (legal: the documented contract already forbids caching the returned address);
  `assign_through` dispatches on node kind. `resolve_target()` (script_value*) CANNOT
  represent a typed element — it returns null for one, and its caller chokepoints
  (vm `exec_index_assign`, the index compound paths, the interpreter's twins,
  ref_lvalue store-through, value.cpp assignment twins) check
  `holder->typed_element()` FIRST and take a load/compute/store path, committing through
  `commit_scratch()`.
- **Mutable `deref()` callers** (++/-- on ref locals etc.) each take a typed-element branch
  (load-modify-store). A missing scratch write-back would be silent data loss — this is
  the one place wrongness hides.
- **ref_lvalue.hpp** subscript steps mint the same ELEMENT holder; store-through is a
  mode-dispatched assign (`ref_write_through`), identical semantics on hetero.

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

## Stages

- **Stage 0 — tag hardening.** The airtight tag above, in a shared detail/ kernel used by
  both backends (invariants §6).
- **Stage 1 — the node funnel.** `script_array` behind alt 6; every consumer goes through
  its accessors, so hetero arrays pay no indirection cost.
- **Stage 2 — typed kinds.** The representation follows the declared type.
  - The **boundary kernel is the one typed-node producer**: crossing into
    `array<int>`/`array<float>` rebuilds into the raw-buffer kind; same-kind sources pass
    by proof-of-kind.
  - Holder ELEMENT mode: typed deref materializes into the holder's inline scratch
    (`materialize_typed_element`, `has_elem_scratch`); `resolve_target()` returns null for
    typed (callers pre-check `typed_element()` and use the scratch-target + buffer-commit
    shape); in-place ++/-- commits via `commit_scratch()`; kernel stores go through
    `ref_write_through`.
  - Typed CAPTURES in a parallel body borrow as raw reads (`parallel_borrow_subscript_read`
    typed branch).

  `array<int>`/`array<float>` are raw buffers always,
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
  - parallel_transform does not demote a typed input at the barrier: it reads a boxed
    snapshot.
  Pinned: `Script Container Tests` (typed_buffer_bound_array_mutates_in_place,
  script_value_vector_view_is_array_var_only, ...) and the strong-types suite (both
  backends on the one representation).

- **Stage 3 — parallel + GLOOM.** Typed borrows/raw_input; kind-checked static proof;
  GLOOM's grids (`PIX`, `ROWIDX`, `zbuf`, ...) are `array<int>`/`array<float>`.
- **Stage 4 — typed builtin paths.** clone, slice and reverse keep the kind and work on raw
  buffers (`assign_buffer`, `retain_range`, `reverse`); C++ interop (`as<std::vector<T>>`)
  hands exact-width payloads over without boxing.

- **Stage 5 — every element type.**
  `b8`/`c8` (bool/char, one shared byte buffer) and `str` (string handles, 8 bytes,
  sharing exactly the object a boxed element shares) sit beside `i64`/`f64`; `auto`/`array<auto>`
  uniform primitive literals infer the element type and take the buffer. Byte and string
  kinds box through `get`/`set` in the INDEX family (raw lanes stay int/float; a
  `raw_bool` decl sink is not built). The parallel all-primitive check excludes
  `str` (handles never borrow raw; `statically_all_primitive` already excluded strings).
  - **byte kinds — BUILT (`kind_t::u8` / `kind_t::i8`).** `array<uint8>` / `array<int8>`
    share the `b8`/`c8` byte buffer, the kind carrying the sign (1 byte per element against 8 for `i64`). The element
    type is an int type_info named `uint8`/`int8` (`native_size` 1, `is_byte_element()`, interned apart from `int`);
    `get` widens to int, `set`/`push` take a pre-converted in-range int, and the range kernel
    (`detail::byte_range_error`) sits after the int rows of `convert_container_element`, so every store path meets it.
    `convert_byte_buffer` converts byte <-> int/float buffers on the raw payloads. The raw lanes (vm index stores,
    `op_t_load`, typed index stores, cell loads/stores/pushes, push append) take the kinds and miss on anything out of
    range, which the enforcing path then raises.
  - **vector kinds — BUILT (`kind_t::v2` ... `kind_t::rgba`, internal/math_types_design.md 4.4).** `array<math::vec2>` through
    `array<math::color>` also use the byte buffer: element i is its binary32 or int32 component words at stride
    `dims * 4`. `get` boxes a vector, and `set`/`push` take a value already converted to the kind. The general raw lanes
    decline these kinds; the VM has its own element-component lanes (`op_velem_load` / `op_velem_store`). A parallel body
    borrows them raw like the scalar kinds.
  - **object kind — BUILT (`kind_t::obj`).** `array<UserType>` (script or
    bound C++ class) is a buffer of `obj_slot{strong_ptr<object_holder> handle, type_info_ptr tag}`
    (16 bytes vs 32 boxed). The per-slot tag reproduces the boxed value exactly on `get`
    (a `shared_ptr<K>`-tagged element stays shared, a plain one stays a value), so every
    reader/writer that goes through get/set/push/for_each behaves as on a boxed array. The
    kernel's proof-by-kind shortcut also compares the array tag's class
    (`same_object_class`); object sources into a different class take the per-element
    check, and a rebuild copies handles as-is — ownership (clone or share) is the
    assignment/binding site's decision. `clone()` and the parallel detach deep-clone per
    slot; by-value range-for clones the slot like a boxed element; default `sort` orders
    by held-object identity like the boxed compare; `bound_array<Class>` binds it (every
    access boxes through get/set). The behaviour probes live in the Strong Types suite.
    **Subclasses are admitted.** Every element gate accepts the class
    or a subclass (exact id first, then `class_derives_from`'s parent walk); the per-slot tag
    is what makes a Derived element keep its own fields inside `array<Base>`; whole-array
    conversions prove the source class once; the boundary fast path accepts a derived tag.
    **`parallel_for` over an object-kind source keeps the node typed (the
    declared type is a proven fact about every element).** The barrier scans the slot
    tags (16 B each, no instance read) and takes the declared-type proof
    (`declared_class_exclusive`): every present class and every class a reached nested
    field can hold - registered subclasses included - is value-closed and no field the body
    reaches can touch a shared string; with `script_array::live_elem_refs == 0` (direct
    element references AND references minted through an element, `arr[i].f`, both count)
    the element walk is skipped entirely. A body naming a string-reachable field, a live
    reference, a shared_ptr-tagged or host slot, or a non-flat subclass falls back to the
    per-element walk over the slots. Workers bind `auto&` to element references on the typed
    node (the slot's handle materializes per touch) and `auto` to a per-iteration boxed slot.
    A primitive-kind range the body may write demotes in place at the barrier.
  - **maps**: see Stage 6.
  - **parallel_transform**: a typed primitive/string input is read through a boxed
    snapshot (no demote, one copy per region); an object-kind input lends its slots
    (`typed_input`), and its elements pass the same declared-type proof as
    `parallel_for`'s. Not built: raw typed worker reads of primitive inputs.

## Stage 6 — typed maps

`script_map` is a hashed class (core/script_map.hpp) that keeps key order where it is
observed; typed map storage is a proposal in `internal/typed_map_design.md`. The yardstick
is the Foundry row `Map lookup: map<string,int> 100 keys, 1000 dynamic-key reads`.

## Risk register

1. **Scratch write-back loss** (mutable deref on typed elements) — silent. Caught by:
   caller audit + ref-mutation suites (++ through ref, compound through ref).
2. **Stamp site missed** (an any-tagged view over a live typed node, or a var slot read as
   locked) — caught by: enumerating `set_type_info` call sites on array values, the var
   tests (`var a = typed; a.push("s")` raises, `a = "s"` rebinds), fuzz.
3. **Node identity drift** (borrow tags / `<=>` / parallel node-alias keys) — the node
   pointer must never change meaning; demote mutates IN PLACE (same allocation). Caught
   by: parallel suite + borrow identity tests.
4. **Hetero-path regression** (extra indirection/tag check on untyped arrays — most
   scripts). Caught by: container benchmark rows.
5. **Parity drift** — every element path is twinned (invariants §6); shared kernels where
   possible (stage-0 fix, holder logic lives in value.cpp shared by construction).
