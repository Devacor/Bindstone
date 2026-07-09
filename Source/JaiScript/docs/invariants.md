# Load-bearing invariants (VM-perf era, 2026-07)

Things that are easy to break silently. Each has a source anchor; if you're about to violate
one, read the anchor first. Background: `thin_value_spec.md` / `thin_value_rebaseline.md`.

## 1. script_value layout gates

`value.hpp:1858-1860`: `static_assert(sizeof(script_value) == 32 && alignof(script_value) == 8)`
plus nothrow-move; `value.hpp:1671-1672` additionally pins every variant alternative as
nothrow-move (`valueless_by_exception` + the noexcept move ctor depend on it). Layout is
8B `type_info_ptr` + 8B `engine*` + 16B variant (8B max payload + index). The `engine_` and
`type_info_` members are **the language**, not fat: zero-statics/multi-engine and the
var/auto/int type ladder live in them — never evict either to buy payload bytes. A new variant
alternative must be pointer-sized and nothrow-move or the asserts fire.

## 2. strong_ptr single-pointer rules

`strong_ptr.hpp`: the handle is ONE object pointer; the control block is *derived* at a negative
offset (`cb_from_object`, via `cb_storage_offset = offsetof(control_block<T>, storage)`
— never hardcode the offset: over-aligned T and the Debug-only magic member both move it).
Sound only because `make_strong` and `detail::adopt_pooled` are the only producers of a non-null
handle: the raw-`T*` ctor is deleted and the cross-type converting ctors are deleted by design
(a Derived→Base conversion would adjust the pointer off the storage head AND slice the
static-type destroy). `adopt_pooled` exists solely for the per-engine `reference_holder` block
pool (`script_value::reference_holder_pool`, value.cpp): pooled blocks derive `control_block<T>`
without moving `storage`'s offset (static_assert-gated) and reset counts + Debug magic on reuse,
so the derivation invariant is preserved. `ptr_` MUST always be exactly `control_block<T>::storage`'s head.
Enforced: negative `is_constructible` static_asserts (strong_ptr_tests.cpp:224-225),
`sizeof == sizeof(void*)` gates (strong_ptr.hpp:412-414), and a Debug canary
`cb_magic_live = 0x5B0CB10C` (:22) asserted at every derivation (:106) and scrambled to
`0x0DEADCB0` on dealloc — a canary assert means a dead/foreign block, i.e. someone minted a
handle outside make_strong or used it after free.

## 2b. Frame slot pointers survive stack growth via the ONE chokepoint

Flat-stack era (stage 2): vm frame slots live in value-stack WINDOWS
(`frame::window_base/window_live`, slot k at `stack_[window_base + k]`), and the stack is a
flat vector that reallocates on growth. The VM caches raw pointers into slots across
dispatch iterations (`counted_for_state.var/end_ptr/step_ptr`, `exec_cfor_prep` — end/step
may point INSIDE a slot's int payload). Sound only because ALL growth funnels through
`value_stack::grow_push` (vm_backend.hpp), which calls `rebase_window_pointers` — a
byte-offset rebase of exactly that enumerated pointer set — before the push. THE RULE:
never cache a raw pointer into stack/window storage across anything that can push, unless
you add it to `rebase_window_pointers` in the same commit; references never qualify
(owner-pinned holders, §3). Windows are created FULL (null placeholders up to `local_count`
behind `window_live`), so operand temps always start above the whole window. Fiber frames
(`vm_coroutine_state::locals`) still use `call_frame` vectors and keep the pre-stage-2 rule:
reserve the full `local_count` up front, never reallocate mid-frame (the 2026-07 demoreel
finding-2 failure mode was silent wrong loop counts in Release). The interpreter's
`call_stack_` frames keep the old rule unchanged.

## 3. Reference cells (mode 1 is DELETED, 2026-07 stage C)

The old ref-anchor rule is dead machinery: `reference_holder` no longer has a raw
`target` pointer or a `weak_ptr<environment> sourceEnv` — a reference NEVER holds a
raw address + lifetime guess. Exactly four owner-pinned holder modes exist
(`reference_holder`, value.hpp; long-form: docs/reference_model.md): CELL (the holder
OWNS the value in inline storage — Lua upvalue), vector element (container+index
re-resolve), instance field (owner+id re-resolve), and map entry (pinned map + key
re-resolve). Rules:

- Holder blocks come from the per-engine pool (`script_value::acquire_reference_holder`,
  value.cpp — one pop + placement-new per mint, memory_cap-charged for cells). The pool is
  single-threaded: parallel workers DO mint cell references inside their bodies, so acquire
  falls back to plain self-deleting `make_strong` blocks while a region is active (blocks
  self-describe their release via `dealloc_fn`, so both kinds coexist). Cells are minted
  ONLY by `make_cell_reference`; the boxed value is never itself a reference.
- Subscript reads whose consumer provably discards identity skip the holder mint entirely
  (`detail/transient_read.hpp`, parse-time classifier consumed by BOTH backends; runtime
  gate `has_custom_binary_ops_`). Any NEW lvalue-consuming syntax must keep its operand
  positions unmarked there — see the checklist comment at the classifier. Escape-marked
  declarations (`parser.cpp` ref_escape_marker) box at decl/bind; unmarked storage
  boxes ON DEMAND at the first ref bind through the ONE box-in-place + share kernel
  pair (`vm_backend::share_env_ref` / interpreter `share_boxed_env_storage`; ref-param
  binds ride it via `bind_reference_to_storage`). The vm kernel must demote active
  counted-for fast states (they cache raw payload pointers).
- Escape is LEGAL: a reference returned (incl. typed ref returns `int& f()`), stored,
  captured, or held across yields/executes keeps its target alive through the holder.
  Never reintroduce a raw-pointer + weak-env reference to frame slots or env storage;
  "Reference target environment has been destroyed" must never come back as an error.
- Every write path resolves by MODE, never by cached address: deref()/assign_through()
  re-resolve per touch; the immediate-use assignment twins go through
  `reference_holder::resolve_target()` (value.cpp). A new consumer must do one or the
  other — a stored `script_value*` into a container/frame is the bug class this
  section exists to prevent.
- Reference binding is STATELESS: the call site travels as an argument
  (vm `bind_parameters(site, caller_locals, caller_code)`; interpreter
  `call_site_context`). Opaque `std::function` boundaries hand it over via a single
  consumed-once pending pointer (`pending_site_ctx_` / `pending_call_ctx_`), shelved
  across external invocations by `external_call_guard`. Never add a second stateful
  metadata channel.
- Direct assignment to an escape-boxed variable writes the cell WITH the variable's
  typed-store enforcement (`names_value_decl` / `store_flag_ref_alias` pick the
  semantics); writes through ref aliases keep the unconstrained store-through rules.
- Ref returns pass the HANDLE out through the ONE epilogue kernel
  (`detail::ref_return_pass_through`); value returns still flatten (deref) at the
  epilogue. Both backends must keep using the shared kernel.

## 4. S8 bound-operand prologue (TYPEID_CPP_BOUND = 14)

Since the thin-value fold, cpp-bound values report raw storage index 14, not their semantic
shadow index. Any NEW binary/unary/compound handler that dispatches on `raw_storage_index()`
MUST normalize bound operands first via `bound_decoded_temp()` (value.hpp:350) — copy the
prologue shape from an existing handler (e.g. `handle_add`, interpreter_dispatch.cpp:47-49, and
its VM twin). The failure mode is SILENT: index 14 falls into an erroring `type_mismatch`
default, or worse a wrong-constant default (`same_as` → false). Raw-index *fast-path guards*
(`idx == TYPEID_INT`) are safe as-is — they exclude bound values and fall through.

## 5. VM run_dispatch MSVC Debug frame ceiling

`run_dispatch` (vm_backend.cpp:7101) executes calls in-loop, so its native frame sits on the
`JAI_MAX_CALL_DEPTH` recursion path in Debug. Each `VM_TRY_OP` case adds a temp to that frame;
the ceiling was reached, so ops added since go through the grouped `exec_extended` dispatch
(:7203-7226) with `VM_TRY_OP_SHARED` (:7060), which reuses one function-scope temp. Follow that
pattern for new opcodes — do NOT add fresh `VM_TRY_OP` cases to the main switch. Canary:
`recursion_depth_error` (vm_backend_tests.cpp:246) must fail *catchably*; if the Debug frame
grows too big, that test instead kills the exe with a native stack overflow (0xC00000FD,
surfaces in the shell as exit code 127) mid-suite.

## 6. Backend parity is byte-identical, including error text

Interpreter and VM must produce identical results AND identical error codes/messages; the suite
runs green on both (`--backend=vm`). Preferred mechanism: shared kernels in `detail/` used
verbatim by both backends — `detail/ref_lvalue.hpp` ("shared VERBATIM ... parity by
construction") is the model, `detail/builtin_methods.hpp` the older one. Legacy mechanism:
byte-parallel twins (`is_truthy` in interpreter.hpp:872 vs vm_backend.cpp:965; the nine
arithmetic/ordering handlers in interpreter_dispatch.cpp vs vm_backend.cpp) — if you touch one
twin, touch the other in the same commit. New shared logic goes in `detail/`; don't grow the
twin population.

## 7. Benchmark methodology

The Foundry benchmark harness (Release BENCHMARKS config) reports **integer µs/iteration with
±50% run-to-run variance** (Source/JaiScript/CLAUDE.md "Benchmarks"). Compare min-of-3 runs,
and never claim a sub-µs win from it — anything under ~1 µs needs a dedicated ns micro-bench
(see `execution_mode_metrics.md` for the methodology that resolved the 0.4 µs regime).
