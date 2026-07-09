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
  binds, override fallback via the gate, pool churn, and pool-orphan release after engine death.

## Open risks

- Frame-level payoff unconfirmed until a GLOOM profile run (first gate after Stage 1).
- Classifier completeness: the whole correctness exposure is "a genuine lvalue consumer
  mis-tagged as a read." Mitigated by default-mint + explicit enumeration + the fuzzer; any
  *new* lvalue-consuming syntax must be added to the predicate (checklist comment at its def).
- `use_count` observability at the copy boundary — the copy fast path must be provably confined
  to discarded operands; the fuzzer's refcount byte-compare is the backstop.
- Compound-store path untouched by Stage 1 — its string-keyed operator lookup is Stage 3 / a
  separate compound-store pass.
