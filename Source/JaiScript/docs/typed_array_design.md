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
3. Therefore **demote-on-stamp**: when a stamp site writes `any` over an array value, it
   also demotes the (unique) node to hetero. After that, a typed node is reachable ONLY
   through views whose tag still says `array<T>` — write paths on typed nodes never need
   promotion machinery; incompatible writes are exactly today's `array_element_type_mismatch`.
   (Cost: laundering a typed array into by-value `var` reboxes O(n) — it already paid a
   deep clone there; `var&` stays free and tag-safe.)

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
  nodes: `deref() const` materializes into a holder-embedded scratch refreshed per call
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
- **Stage 3 — parallel + GLOOM payoff.** Typed borrows/raw_input; kind-checked static
  proof; type GLOOM's PIX/zbuf/ROWIDX/mappack as `array<int>`/`array<float>` (globals get
  typed decls; `var&` param plumbing already tag-safe, banked as hash-safe). Measure:
  Release A/B, expect INDEX/INDEX_STORE counts unchanged but cycles down + bandwidth win.
- **Stage 4 — typed builtin fast paths.** clone/slice/reverse/join/sort-default on raw
  buffers; C++ interop bulk paths (`vector<T>` ⇄ typed array near-memcpy,
  conversion_registry_impl.hpp:15-45, engine_impl.hpp:166-195). Measure.

## Risk register

1. **Scratch write-back loss** (mutable deref on typed elements) — silent. Caught by:
   caller audit + Debug assert + ref-mutation suites (++ through ref, compound through ref).
2. **Stamp site missed** (an any-tagged view over a live typed node) — writes would error
   where today they succeed. Caught by: enumerating `set_type_info` call sites on array
   values (grep-audit in stage 2), laundering tests (`var a = typed; a.push("s")` must
   still succeed via demote), fuzz.
3. **Node identity drift** (borrow tags / `<=>` / parallel node-alias keys) — the node
   pointer must never change meaning; demote mutates IN PLACE (same allocation). Caught
   by: parallel suite + borrow identity tests.
4. **Hetero-path regression** (extra indirection/tag check on untyped arrays — most
   scripts). Caught by: stage-1 NEUTRAL gate on GLOOM (all arrays still untyped there)
   + container benchmark rows.
5. **Parity drift** — every element path is twinned (invariants §6); shared kernels where
   possible (stage-0 fix, holder logic lives in value.cpp shared by construction).
