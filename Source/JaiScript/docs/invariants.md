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
offset (`cb_from_object`, :101-108, via `cb_storage_offset = offsetof(control_block<T>, storage)`
:93 — never hardcode the offset: over-aligned T and the Debug-only magic member both move it).
Sound only because `make_strong` is the sole producer of a non-null handle: the raw-`T*` ctor is
deleted (:148) and the cross-type converting ctors are deleted by design (class comment :125-128
— a Derived→Base conversion would adjust the pointer off the storage head AND slice the
static-type destroy). `ptr_` MUST always be exactly `control_block<T>::storage`'s head.
Enforced: negative `is_constructible` static_asserts (strong_ptr_tests.cpp:224-225),
`sizeof == sizeof(void*)` gates (strong_ptr.hpp:412-414), and a Debug canary
`cb_magic_live = 0x5B0CB10C` (:22) asserted at every derivation (:106) and scrambled to
`0x0DEADCB0` on dealloc — a canary assert means a dead/foreign block, i.e. someone minted a
handle outside make_strong or used it after free.

## 2b. Frame slot storage never reallocates mid-frame

`call_frame::locals` is a plain vector, and the VM caches raw `script_value*` into it across
loop iterations (`counted_for_state.var/end_ptr/step_ptr`, vm_backend.cpp `exec_cfor_prep`);
references can also target it. Sound only because EVERY path that creates a frame reserves the
parser's full `local_count` up front (`reserve_locals(std::max(function.local_count,
body_chunk->local_count))` — push_script_frame, push_method_frame, call_script_function,
coroutine states; both backends' `execute_method_ast` must pass `ast->local_count` through).
The 2026-07 failure mode: method frames compiled/reserved with local_count 0, so a loop-body
`DECL_VAR` push_back reallocated the vector and the cached counted-for pointers read freed
memory (demoreel finding 2 — silent wrong loop counts, Release; delayed detonation shapes).
Never add a frame-creation path that skips the full-slot reserve.

## 3. Ref-anchor environments are never pooled/reused

References carry `weak_ptr<environment> sourceEnv` (`reference_holder`, value.hpp:1581-1583).
A frame's `ref_anchor` (environment.hpp:304-314) is a liveness token created on demand
(`ensure_ref_anchor`) for refs bound to that frame's slot storage. Call *records* are pooled
and recycled — the anchor is not: it is `reset()` when the record is recycled
(vm_backend.cpp:7838, "expire references into this frame's slots"), so an escaped reference
expires cleanly instead of dangling into reused slot storage. Never cache, share, or
re-arm a ref_anchor across frames/recyclings; a reused anchor keeps stale refs "alive"
pointing at someone else's locals.

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
