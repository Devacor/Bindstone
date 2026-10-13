# Design Memo — Element-Read Overhead: Cheapen the Reference, Fix the Classifier, Don't Swap Semantics

*Produced by a multi-agent investigation (5 read-only scouts → synthesis → 4-lens adversarial
red-team → memo), 2026-07, VM-perf. The red-team overturned the first-draft recommendation;
this memo reflects the corrected, verified plan.*

**Decision:** Ship a corrected **Option A** (context-driven lvalue classification +
reference-holder alloc-elision), landed on **both backends atomically behind a single shared
predicate**. **Drop Option B.** Land the C1/C2 interpreter elisions opportunistically.

---

## Root cause (well-attributed)

A named-array element read — `arr[i]`, `grid[y][x]`, `self.cells[n]` — is classified as an
lvalue **from the syntactic shape of its base**, with no knowledge of how the result is
consumed. Every such read therefore takes the write-target path and mints a heap
`reference_holder` via `make_element_reference` (`value.cpp:263-279`): a `make_strong`
control-block alloc, a `get_type_info_reference` hash, a container refcount bump, and a later
holder-walk `deref` + free — all to hand one scalar to an operator that discards the wrapper.
VM: `vm_compiler.cpp:1249-1254` → `vm_backend.cpp:4511-4519`. Interpreter:
`interpreter.cpp:3504-3521`, where the array branch ignores `want_lvalue_write` entirely.
~90-180ns of avoidable overhead on a ~50ns baseline reproduces the measured 370-575ns/element.

**The 32-byte value and non-atomic `strong_ptr` are not the villain; the operand-delivery
layer is.** This is a mis-classification of *all* reads — which is exactly why the fused
`ident OP ident` micro-suite looks green while GLOOM's tens of thousands of
`subscript OP subscript` reads/frame do not. The comparison operators themselves are already
clean (read operands by `const&`, touch raw scalars); 100% of the cost is operand production.

## Why the naive first-draft fix is unsound (4 red-team lenses converged)

The draft proposed: gate the array branch on `want_lvalue_write`, drop to `push_value(array[index])`
for reads, "mirror the map branch," then mirror to VM. **Not shipping it.** The objections:

1. **It regresses reference binds to a hard error.** `auto& r = arr[i]`, `int& r = arr[i]`,
   and `return arr[i];` do **not** set `want_lvalue_write` (set in one place only, the direct
   store target, `interpreter.cpp:5425`). Under the gate these take the copy path; the
   reference-decl site (`:5840-5852`) then errors "Cannot take reference of non-lvalue
   expression." Exercised today (`vm_backend_tests.cpp:809/819/942/972/998/1019`).
   `want_lvalue_write` means "auto-insert this store's map key," **not** "consumed as lvalue."
2. **"Mirror the map branch" is self-contradicting.** The map branch does *not* copy on an
   lvalue read (`:3565` mints a reference to the existing entry so nested mutation works).
   Faithfully mirroring = keep minting = zero win.
3. **Reference-vs-copy is observable.** `use_count()`/`cpp_ref_count` (commit `54c0d46f`)
   deliberately observe the non-atomic refcount; under reallocation, aliasing copy vs
   re-resolving reference diverge (#41). A `var` language can't rule these out statically.
   **The alloc is the villain; the reference *semantics* are load-bearing.**
4. **Interpreter-first staging breaks the parity invariant** during the two-commit window.

## Corrected Option A

- **(a) Cheapen the reference.** For genuine lvalue consumers, still return a reference — but
  elide the `make_strong<reference_holder>` heap alloc and the `get_type_info_reference` hash
  (`value.cpp:272-273`). Same semantics, no malloc. The safe bulk of the win; invisible to
  `use_count` and realloc.
- **(b) Copy only where provably transient.** Shallow `array[index]` copy (refcount-correct:
  shares the heavy inner `strong_ptr`, survives source realloc; primitives copy refcount-free)
  **only** for immediate operands of a *builtin* value-op / condition — the one context where
  neither refcount nor aliasing is observable.
- **(c) A real consumer-threaded `wants_lvalue` predicate** (NEW, not a rename of
  `want_lvalue_write`), default *mint-when-unknown*, true for: store/compound target, ref-param,
  reference-decl initializer, reference-return, mutating receiver, `++/--`, and any
  subscript/member that is the base of an outer lvalue chain.

## Why Option B is dropped

"Promote `TYPEID_PARALLEL_BORROW` to the sequential read path" fails independently:
- **No win:** the borrow's element read (`parallel_transform.cpp:1293-1303`) returns a fresh
  copy for primitives (zero delta over A) and a full deep clone for heavy elements (a
  *regression*). Its `clone()` materializes the whole container, not the element.
- **Unsound sequentially:** the borrow is a raw non-owning pointer sound only under the
  parallel region's write wall + barrier anchor. `arr[i] < f()` where `f()` does `arr.push(x)`
  reallocates under a live borrow → use-after-free; `arr[i] + (arr[i] = 5)` breaks
  mutation-visibility. A user `operator<` override (`interpreter.cpp:3454`) can realloc under
  a "safe-looking" `arr[i] < k` — the no-mutation gate is undecidable.
- **Safe subset ≈ existing C3 fast paths** (`fused_cmp_operand`/`fused_ident_value`).

## Staged plan

- **STAGE 1 (safe, high-value, both backends, one commit):** the shared `wants_lvalue`
  classifier as a single predicate consumed by both `vm_compiler` emit and the interpreter
  subscript branch (parity by construction, per the `detail/ref_lvalue.hpp` precedent) +
  reference-holder alloc-elision (a) + shallow-copy fast path (b) for builtin-value-op
  operands. Wire reference-decl and reference-return into the predicate in the *same* change.
  Deletes malloc + hash + container bump from the hot read; reference semantics intact where
  observed; identical on both backends, no divergence window.
- **STAGE 2:** C1/C2 interpreter elisions — `deref()` as a reference not a second `optional`
  copy (`:3411`); hoist the per-op operator-override probe behind `has_custom_numeric_ops_`
  (`:3454`). Independently safe; land opportunistically.
- **STAGE 3 (optional):** fusion — `arr[local] OP scalar` → one `op_fused_cmp_jump`; fuse
  `op_index`+`op_index_assign`; cache interned reference-element type_info. Only after Stage 1.
- **Dropped:** Option B.

## Mandatory guardrails (from the red-team)

1. Classifier is a true consumer-set predicate, default-mint-when-unknown; explicitly covers
   reference-decl and reference-return. Not a rename of `want_lvalue_write`.
2. No cross-backend staging — emission change lands in `vm_compiler.cpp` + `interpreter.cpp` in
   one commit, validated under `--backend=vm` in the same run.
3. Parity by construction — one shared classifier predicate + the already-shared read kernel.
4. Preserve reference semantics where observable — lvalue-read stays a (lighter) reference; the
   win there is alloc-elision, never reference→copy. Copy fast path only for immediate
   builtin-value-op operands.
5. Reference/mint stays mandatory for assignment/compound targets, ref-params, ref-binds/returns,
   mutating receivers, `++/--` — #41 realloc-safety depends on it.
6. Gate behind the differential fuzzer (backend-vs-backend *and* copy-path-vs-reference-path
   byte compare, incl. `use_count`) + Debug `0xDDDDDDDD` alias-torture. Keep OOB error text
   byte-parallel; do not route sequential reads through the parallel kernel's error text.

## Expected GLOOM impact (honest)

- **Per-element** (well-attributed): **~370-575ns → ~50-100ns**, a 4-7× collapse, from deleting
  malloc + type-info hash + container bump. Int-array residual copy is refcount-free.
- **Frame** (estimate, pending a profile run): GLOOM's 31ms also carries compound stores
  (`pix[gi] += x` with its own `environment_->get("+")` lookup + clone), dispatch, and
  non-subscript work. If element reads are ~half the inner loop and drop ~3-4×, **Stage 1
  plausibly lands ~18-22ms**; Stage-3 store-side fusion could push toward **~14-16ms**.
  Beating Python (2.5ms) is NOT a Stage-1 outcome — it is a multi-stage target (this stage +
  the compound-store/dispatch work). First measurement gate after Stage 1.

## Stage 1 status (landed, 2026-07)

Implemented as specified, both backends atomically:

- **(c) Classifier**: `detail/transient_read.hpp` — a parse-time pass (sibling of
  `ref_escape_marker`) stamping `binary_expr::transient_element_read`; both backends consume
  the same bit (parity by construction). Default mint-when-unknown; the transient set is
  builtin-binary-op operands (left only under a `callout_free` sibling), `&&`/`||` operands,
  if/while/for/ternary conditions, subscript index/key positions, transient-chain bases, and
  assignment RHS under a `callout_free` target (both backends deref the RHS before storing —
  verified byte-parallel). Unary operands are deliberately excluded (`-arr[i]`/`~arr[i]` do
  not deref references today; marking would tie that pre-existing error to the runtime gate).
- **(b) Copy fast path**: interpreter subscript branch + `vm_backend::exec_index` take the
  existing temporary-copy branch when marked, runtime-gated on a new engine-wide
  `has_custom_binary_ops_` (any binary-operator or `"[]"` global registration flips it via
  the `register_overload_impl`/`add_global` chokepoints; `set_has_custom_numeric_operators(true)`
  latches it). Map transient reads use the never-insert copy branch.
- **(a) Mint elision**: `get_type_info_reference` caches the interned `T&` twin on the
  referenced `type_info` itself (+ an engine slot for null), eliding the intern-map lookup;
  write is skipped inside parallel regions (prewarm fills it beforehand). Holder blocks come
  from a per-engine free-list pool (`script_value::reference_holder_pool`, value.cpp) via
  `detail::adopt_pooled` — pop + placement-new instead of malloc; blocks self-describe via
  `dealloc_fn`; engine death with escaped references orphans the pool (last release frees it).
  Debug keeps the 0xDD-style canary: parked blocks scramble `cb_magic` (it caught the one
  real bug during landing: parallel workers DO mint cell references in their bodies, so
  in-region acquires fall back to plain `make_strong` — the pool stays single-threaded).
- Pins in `Element Read Elision` suite (review_regression_tests.cpp): operand/condition/RHS
  elision values, chain reads, map never-insert, impure-sibling mint retention, ref-decl
  binds, override fallback via the gate, pool churn, pool-orphan release after engine death,
  class-operator dispatch over elided operands, and cross-engine reference minting.

**Post-landing review fixes (same day):** the twin cache stamps only type_infos this engine
interned (pointer identity via `type_id_index_`) — a cross-engine value must not carry a
foreign twin pointer that dangles when the other engine dies; the pool free-list reserves up
front (release runs inside noexcept `~strong_ptr`) and scrambles the Debug magic on the delete
branch too. **Ruling recorded:** class operator/"[]" METHODS do not latch the runtime gate —
an engine-wide latch would kill the elision for any host binding operator types
(dynamic_binder overloading in MV/Bindstone). Elided operands reach class operators as
evaluation-time copies, which is the interpreter's historical order (the old VM's late deref
silently diverged from it); use_count inside class operator methods shifts by the documented
transient-copy calibration, not semantics. Global operator/`"[]"` registration still gates
(no false-positive cost there).

## Stage 2 status (landed, 2026-07-09) — the store side

Dev ruling redirected Stage 2 to the highest-value target: the VM store path (`pix[gi] += x`
barely moved in Stage 1) and the operator-dispatch machinery itself ("always interned ids,
never strings; operators are a finite set — flat array keyed by an operator enum; invest in
the non-fast-path operator machinery; op[] and += get true fast paths").

- **Flat operator table** (`detail/operator_table.hpp` — THE operator enumeration, single
  source of truth): per-engine `engine_operator_table` holds the current global dispatcher
  per operator (overload/arity/type matching stays inside that value); refreshed at the
  registration chokepoints; backends hold a wired const pointer. Replaced every operator
  probe in both backends — the interpreter's per-binary-op global-env hash probe, the
  string-keyed whole-env-chain walks in both compound-store paths (one ran on EVERY
  VM compound store), both object-"[]" env probes, and the shared constrained-compound
  kernel's probe (`ref_lvalue.hpp` now takes the table). The interpreter's builtin
  binary-handler unordered_map became a flat array indexed by the same enum.
- **Fused subscript stores** (VM): `op_index_store` (a[i] = v) and `op_index_compound_fused`
  (a[i] op= v) resolve container+index once and write in place — no reference_holder mint,
  no holder re-resolve, no operator consult on the numeric path. Fast-path gates: lvalue
  shape + plain array + int index + in-bounds (+ int/float element and raw int/float rhs +
  empty operator table for compounds). EVERYTHING else replays the exact old
  INDEX(+flags)+INDEX_ASSIGN/INDEX_COMPOUND sequence by calling those very exec functions
  on a rearranged stack — semantics and error text byte-identical by construction.
  New opcodes ride the exec_extended grouped dispatch (invariant §5, Debug frame ceiling).
- Interpreter store paths keep their existing shapes (parity is output-identical, not
  implementation-mirrored); its table wins: no more per-op env probe on the generic binary
  path, no string construction per compound store.

## Stage 3b: N-level chains + the profile that closes the campaign (2026-07-09)

- **N-level chain fusion**: `fused_operand` holds a subscript CHAIN (outermost-application
  order, sanity-capped at 8 levels — not a semantic limit). Array levels step a pointer in
  place; ANY other level (map key, borrow, OOB, non-int) replays that one level through the
  real `op_index` and the walk continues — so `m["rows"][y][x]` fuses its array suffix while
  the map level keeps byte-identical never-insert semantics. Truly general N-level.
- **Landing bug caught by pin**: the env-lookup cache is provenance-({env,epoch})-checked but
  NOT symbol-checked; sharing role slot ip*3+2 for both operands' indexes aliased `i`/`j` at
  top level. Index lookups now pass a SIZE_MAX sentinel that skips the cache (hot code
  resolves indexes from frame slots anyway).
- **JAISCRIPT_VM_PROFILE**: rdtsc opcode self-time histogram + in-loop-miss callee counts in
  `run_dispatch`/`exec_call_from_scratch`, dumped by `~vm_backend`. Compile-flag-gated, zero
  cost off; build a separate dir with `/DJAISCRIPT_VM_PROFILE` (the flag changes the class
  layout — build-wide or not at all). The main engine's dump is the LARGEST "total N Mcycles"
  block; small dumps are worker/startup engines.
- **The GLOOM profile verdict** (600 ticks): element traffic (INDEX/INDEX_STORE/BINARY*)
  is ~15% combined — this campaign is done as a frame lever. The wall is the CALL BOUNDARY:
  `op_call_from_scratch` = 56.5% of cycles, and the in-loop-miss table names it — `itrunc`
  80.5k, `ifloor` 78k, `sqrt` 12k (~290 math-builtin boundary crossings/tick, each paying
  arg-vector + value copies + std::function for one machine instruction), `parallel_transform`
  3/tick (main thread holds the whole region: barrier grid scan + palette snapshots),
  `brain.resume()` fiber switches under CALL_METHOD. Plus 39k `op_load`/tick at ~37ns = the
  structural stack-vs-register gap. Next campaign: builtin-call fast lane (intrinsic dispatch
  for pure-numeric natives), then region-entry cost, then method/coroutine cost.

## Open risks

- Frame-level payoff unconfirmed until a GLOOM profile run (first gate after Stage 1).
- Classifier completeness: the whole correctness exposure is "a genuine lvalue consumer
  mis-tagged as a read." Mitigated by default-mint + explicit enumeration + the fuzzer; any
  *new* lvalue-consuming syntax must be added to the predicate (checklist comment at its def).
- `use_count` observability at the copy boundary — the copy fast path must be provably confined
  to discarded operands; the fuzzer's refcount byte-compare is the backstop.
- Compound-store path untouched by Stage 1 — its string-keyed operator lookup is Stage 3 / a
  separate compound-store pass.
