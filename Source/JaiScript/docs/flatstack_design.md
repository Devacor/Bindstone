# Flat-stack VM design (phase 1: design only)

**As-built deltas (stages 1–2 landed, 2026-07-08).** Stage 1 = b961e251 (callee IC +
return-conversion classes + the interpreter pool-release lifetime fix per the Dev ruling),
stage 2a = 91b004aa (accessor funnel), stage 2 core landed with three deliberate deviations
from §2 as designed, reflected in `invariants.md` §2b: (1) **flat vector + growth-chokepoint
rebasing instead of chunked segments** — segment indexing would tax every operand push/pop
(the hot-loop 44–48 band), while the sanctioned raw-pointer set into frames is exactly
enumerable (counted-for fast states; cells make everything else owner-pinned), so
`value_stack::grow_push` + `rebase_window_pointers` (byte-offset; covers payload-interior
int pointers) is strictly cheaper; (2) **frame metadata flattening deferred** —
`call_record`/`frames_` stay as-is (that win folds into stage 3's scrub pass); (3) **fiber
frames stay `call_frame`-backed** (the accessors' `window_backed == false` branch) until
stage 4's per-fiber stacks — mixed-mode frames coexist on one stack. In-place binding's
Ruling-2 elision proof: the elided copy is the old stack→locals transfer whose source died
at the post-bind erase before any observation point, so the per-backend transient-count
audit is STATIONARY (vm `2:1` unchanged; no `transient_k` retune). `call_frame` survives on
record frames as frame-kind metadata only (this/static/closure_env/name).

**Stage-6 addendum (Dev ruling 2026-07-08): callee-first sequencing IS the language**
(matching C++17); the full args-first flip that would have unlocked classic call-op fusion
was considered and REJECTED. The retained middle path landed as the probe pair:
`op_probe_callee` resolves the identifier callee (and fires `not_a_function`) at the
pre-args observation point into a pending-callee register stack — no value-stack
materialization — and `op_call_from_scratch` calls it with the args at the stack top and
no callee slot. The probe also aligned a latent vm divergence (the old post-args
not-callable check) to the ruled interpreter order. Semantics guards:
`callee_first_*` pins in vm_backend_tests.cpp (missing-fn/not-a-function before arg side
effects, rebind-in-args calls the original, throw/yield pending-register hygiene — the
pending stack is per-fiber and unwind-truncated like the value stack).

**Status: DESIGN — no implementation.** Roadmap item 3 ("Flat-stack VM"), the committed fix
for the call-dense recursion gap vs Lua (`PERFORMANCE.md` Known gaps: fib(15) 697 µs VM vs
75 µs Lua 5.4, ~9×; "Recurse, 10 locals" ~14×). Function calls are already ~0–1 µs compiled
and opcode counts are at parity with Lua (~6–7 ops per fib call) — the whole gap is
**per-call frame overhead**, not dispatch. This doc diagnoses that overhead per component
with a scratch instrumented build, specifies the target architecture (contiguous value
stack with base-relative frame windows, Lua-style), answers each hard interaction, and
lays out a cells-style staging plan with gates.

Read together with: `invariants.md` (§2b frame-slot stability, §3 cells, §5 Debug frame
ceiling, §6 parity, §7 benchmark methodology), `reference_model.md` (owner-pinned holder
modes — the cells stage-C property this design leans on hardest), `DEBUGGER_DESIGN.md` phase 5
(chunk-derived lazy locals), `PERFORMANCE.md` (the numbers being attacked).

---

## 1. Precise diagnosis: where the ~350 ns/call goes

### Methodology

Scratch build in a worktree (detached at 90536c94; never committed): all 21 library TUs +
a micro-bench `main`, MSVC `/O2 /Ob2 /Oi /Ot /MT` **without** `/GL` (no LTCG — single-TU
link keeps the edit-measure loop at ~1 min), `__rdtsc()` section accumulators inside
`push_script_frame` / `bind_parameters` / `return_from_script_frame` /
`pop_script_frame_core` / `exec_call`, plus an opcode histogram in `run_dispatch`.
Calibration anchors: fib(15) has exactly **1973 calls**; the official Release BENCHMARKS
(`/GL /LTCG`) band is 697 µs → **353 ns/call all-in**; Lua 5.4 is 75 µs → **38 ns/call**;
Squirrel (same in-loop call architecture as our VM) is 165 µs → **84 ns/call**.
Build-config sanity: the UNinstrumented twin of the scratch config runs fib(15) at
792 µs → **401 ns/call**, only 1.14× the official band (LTCG is worth ~12% here), so the
scratch build's shape is representative. The instrumented exe runs 599 ns/call — the 16
`__rdtsc` reads + counters cost ~200 ns/call on this shape (~105 on the sparser 0-param
shape; rdtsc cost is OoO-scheduling-dependent), so **each measured section carries
roughly 5–12 ns of read overhead and the small sections are upper bounds**. The scaled
column below ≈ (measured − ~10 ns section overhead) × 0.88 (plain→official whole-run
ratio), rounded — a map, not a ledger. Sub-µs numbers from the integer-µs Foundry harness
are never used (invariants §7); everything here is rdtsc totals over 300 × fib(15) =
591,900 calls, warm, on a machine verified quiet (a concurrent agent build invalidated
one earlier session — check for cl/ninja before trusting a run).

### fib(15), `auto` params/return (the published row)

6.0 ops/call: 1.5 `op_load`, 1.0 `op_binary_fused`, 1.0 `op_fused_cmp_jump`, 0.5
`op_binary`, 1.0 `op_call`, 1.0 `op_return`. Every call takes the in-loop fast path
(`exec_call` peek → `push_script_frame`; `env_lazy` = true — fib's body provably never
needs a scope env, so **no environment is created**: the eager-env twin below shows what
that elision already saves).

| component | what it is | ns/call (instr.) | scaled |
|---|---|---:|---:|
| `exec_call` pre | callee peek: `is_function`, `as_function().target<script_callable_thunk>()` (RTTI), arity check | 39.3 | ~26 |
| `push_script_frame` | | **153.7** | **~88** |
| — checks | depth cap + `execution_limit_exhausted` (budget tick) | 9.0 | ~2 |
| — record acquire/init | `backend_body_cache` read, `call_record` pool slot, `callee_pin` move, `prev_env` move, 3 base snapshots (try/iter/cfor), `function_name`, `reserve_locals`, `env_lazy` calc | 37.1 | ~24 |
| — env setup (lazy!) | `locals.closure_env = rec.prev_env; environment_ = rec.prev_env` — two `shared_ptr` copies (atomic incs) **on the elided path** | 27.8 | ~16 |
| — frame init | 6 field writes + `frames_.push_back` | 8.5 | ~1 |
| — `bind_parameters` | ONE matching-type int arg: fast path still does `script_value(arg)` copy + `boxed_param` branch + `set_local` push_back into the record's separate `locals` vector | 55.1 | ~40 |
| — tail | `stack_.erase` of the callee+args slice | 16.1 | ~5 |
| return path | | **109.3** | **~53** |
| — result move | pop result, any-type deref check | 18.5 | ~8 |
| — `pop_script_frame_core` | env restore 20.4 + truncation checks 8.8 + **locals clear / `callee_pin = null` / record scrub / `frames_.pop_back` 33.0** | 62.2 | ~30 |
| — tail | result push_back, `fp` switch, ip inc | ~28.6 | ~16 |
| **call machinery total** | | **302** | **~170** |
| dispatch + 6 ops | loads (incl. the per-call copy of the `fib` function value out of the env cache), fused compare/sub, dispatch loop | ~297 | **~183** |
| **whole run** | | **599** | **353** |

Readings:

- **Call machinery is roughly HALF of fib (~170 of 353 ns); the other half is the 6
  ops.** Both halves matter, but they are different projects: this design attacks the
  ~170; the op half is value traffic (each `op_load` pushes a 32-byte `script_value`
  copy; the callee load refcounts a `strong_ptr` per call) and dispatch, addressed only
  partially here (stage 6, §4).
- **The single biggest machinery line is binding one already-correct int argument
  (55 ns instr.)** — a copy into a *different* vector than the one the value already sits
  in, plus the erase that removes it from the stack (16 ns), plus its destruction at pop
  (inside the 33 ns locals-clear line). Corroboration from the 0-param twin below: with
  no argument to bind, measured machinery drops 302 → 223 ns/call — **parameter traffic
  is ~80 ns/call of real work for one int arg**. The value stack already holds
  callee+args contiguously in call order; the flat design makes those slots *be* the
  frame.
- **The lazy path still pays ~45 ns (instr.) of environment traffic** (two `shared_ptr`
  copy-ins at push, move-back at pop) for frames that provably never use an env.
- **`callee_pin` round-trip**: the callee value is moved off the stack into the record,
  then destroyed (`make_null()`) at pop — pure bookkeeping; the stack slot it came from
  is truncated anyway. In a flat frame the callee slot below the args IS the pin (Lua's
  layout), and the return value overwrites it.
- **Record scrub at pop (~33 ns instr.)**: clearing the locals vector (script_value
  dtors — real work), then 8 field resets so the pooled record is inert. A window design
  truncates the value stack (destroying exactly the same values) and resets a small
  frame struct.

### 0-param twin (the fixed per-call floor)

`function f0() -> auto { return 1; }` called from a counted loop: measured machinery
223 ns/call (vs fib's 302) with `bind_parameters` at pure-overhead level — the fixed
cost of frame push+pop alone (record + lazy-env traffic + frame init + return path)
before any argument exists. This floor is what stages 1–3 attack; ~80 ns/call rides on
top per bound argument.

### The typed twin: `int` params / `-> int` pays a per-return conversion (finding)

fib(15) with `int n -> int`: identical shape and op count, but the return path pays
`convert_return_value` → `try_convert_for_parameter` per call: **+58 ns/call**
(ret_result 76.4 vs 18.5 instr.). The conversion outcome is statically knowable (an int
return of an int-typed expression); today it re-derives it per return, including two
`type_name` string compares. Cheap independent fix folded into stage 1: precompute a
per-chunk "return conversion needed?" decision at compile time.

### Eager-env twin (what lazy elision is already worth, and what methods still pay)

fib with a dead `try{}` in the body (forces `needs_frame_env`; +1 op/call for the guard):
push_env 27.8 → **80.7** ns/call (`acquire_scope_env` pool hit + `environment::reset` —
which **bumps the global env epoch on EVERY call**, invalidating every env-lookup inline
cache in the engine; and because the body now runs in its own scope env rather than the
global one, `env_lookup_cached`'s gate disengages and the callee's own `fib` load walks
the parent chain each call instead of hitting the per-ip cache), frame init 8.5 → 20.0 (the
`entry_env` pin), pop_locals 33.0 → **101.2** (`release_scope_env`: use-count guard +
`reset(nullptr)` + pool push). Net: measured machinery 302 → **410 ns/call (+36%)**,
whole run 599 → 742. This premium is what **every method call** pays today
(`acquire_method_scope_env` in `push_method_frame` — methods never elide), a big slice of
why the method-dispatch row sits at ~2.4 µs. The flat design's escape boundary (§2)
exists to extend elision, not just relocate slots.

### Recurse-10-locals (the ~14× row)

Same machinery (~307 ns/call instr.) but 28 ops/call: 10 × `op_decl_var` + 10 ×
`op_const` + loads. Each decl is a full dispatch → `exec_decl_var` → node deref →
`define_decl_value` → `set_local` push_back of a 32-byte value. Lua runs 10 LOADK
register writes at ~2 ns each. Frame windows fix the storage half (slots exist at entry;
a decl becomes "write slot k"), but the per-decl dispatch remains — this row needs stage
6's dest-addressed ops to fully close, and honesty says it stays a multiple of Lua.

### Where the flat stack does NOT help (kept from the same numbers)

Dispatch + value traffic (~183 ns of fib's 353): 32-byte values with engine + type_info
(the language, per invariants §1 — not up for eviction), `strong_ptr` refcounts on
function-value loads, checked overflow, the switch dispatch itself. See §5.

---

## 2. Target architecture

### One contiguous value stack; frames are windows

Today one script→script activation owns FOUR value homes: the shared `stack_` (operands,
already contiguous callee+args in call order), the record's `call_frame::locals` vector
(params + slot locals), the `call_record` (callee_pin + metadata), and — for non-elided
frames — a pooled `environment`. The flat design collapses the first three:

```
stack_:  [ ... caller window ... | callee | arg0 arg1 | local2 .. localN | operand tmps ]
                                  ^base-1  ^base                          ^top
```

- **Frame window** = `[base, base + chunk->local_count)`. Parser-assigned `slot_index`
  values (unchanged) address base-relative: slot k = `stack_[base + k]`. Operand temps
  live above the window, exactly where `stack_` puts them today.
- **Call**: compiler already emits callee then args contiguously. `op_call` validates the
  callee, sets `base = args_base`; matching-type args are ALREADY slots 0..argc-1 —
  **zero copy, zero erase**. Conversion/boxing/default/ref params write their slot in
  place (same `bind_parameters` decision ladder, same error text, same order). Remaining
  slots up to `local_count` are nulled (or left to `op_decl_var`'s first write, matching
  today's not-yet-live `get_local` nullptr semantics via a `live_top` per frame — decide
  in stage 2 by parity tests). Not-yet-live matters: today a slot load BEFORE its decl
  executes returns nullptr from `get_local` and falls through to the env lookup
  ("Undefined variable ..."), and the untaken-branch slot-gap fill
  (`call_frame::set_local`'s slot>size path) must reproduce — a naively pre-nulled
  window would silently return null where today errors.
- **Callee slot is the pin**: the function value sits at `base-1` for the frame's
  lifetime (replaces `callee_pin` move + `make_null` scrub). Hot reload redefining the
  name mid-call can't kill the executing function: the slot holds the strong ref, and the
  chunk is pinned by `chunk_cache_` as today.
- **Return**: convert/deref while the frame is live (unchanged order), write the result
  into `base-1`, truncate to `base`. Truncation destroys args+locals+temps at exactly the
  point `pop_script_frame_core` destroys them today ("deferring destruction is
  observable" — same timing, same order: result materialized first, then callee locals).
- **Frame metadata** shrinks to one flat struct in one vector (replacing
  `call_record`-behind-`unique_ptr` + the parallel `frames_` vector of `frame*`):
  `{chunk* code, ip, base, caller_index, prev_env (only when eager), entry_env (only when
  eager), return_type, this_slot/flags, try/iter/cfor bases, debug_stmt, debug_stmt_ip}`.
  Raw `frame*` held across pushes today (`try_record.owner`, `rec.caller`,
  `counted_for` implicit) become indices, so the frames vector may grow freely.
- **Native-entry frames** (`call_script_function`, `run_program`, `eval_expression`,
  default-arg chunks, `run_fiber`) stay what they are — a `frame` struct on the C++ stack
  entering `run()` — but their locals storage becomes a window at the current stack top
  instead of a borrowed `call_frame`. `top_level` frames keep env-backed globals
  (unchanged).

### Window pointer stability (invariant 2b transplanted)

`counted_for_state` caches raw `script_value*` into frame slots; references bound through
`bind_reference_to_storage` may box slots in place. Today's rule: slot storage NEVER
reallocates mid-frame (full `local_count` reserve at frame creation). The flat stack must
keep an equivalent rule while the stack GROWS on nested calls. Design choice: **chunked
segments** (e.g. 16K-value blocks): a frame window must fit one segment; a call whose
window would straddle starts the window at the next segment (the few dead tail slots are
truncated with the frame). Pointers into live windows are stable for the run's lifetime —
strictly stronger than today's guarantee, no fixup pass, no `std::deque` iteration cost on
the hot path (the active segment's span is cached; only the cross-segment call path
consults the segment table). Rejected alternatives: single `reserve(huge)` (memory), and
Lua-5.1-style reallocation fixup (enumerable today — only `cfor_states_` and
`env_lookup_cache` hold raw pointers, and the latter never points at frames — but every
future cached pointer becomes a landmine; segments make the invariant structural).

### The escape boundary (lean on the cells refactor hard)

The cells model already answers "what if a local outlives its frame":

- The parser's `ref_escape_marker` (parser.cpp; conservative, function-granular) marks
  decls/params whose refs may escape; those **box into a CELL at decl/bind**
  (`make_cell_reference`) — the slot holds the handle, reads/stores see through it.
- Anything the marker can't see boxes ON DEMAND at first bind through
  `share_env_ref`/`bind_reference_to_storage` — which operate on `script_value& storage`
  and work byte-identically on a window slot (they already work on `call_frame` slots).
- References are owner-pinned handles (CELL/element/field/map-entry) — **no reference
  ever holds a raw pointer into a frame**, so frames dying (stack truncation) under a
  live reference is already a non-event. This is the cells-stage-C property that makes a flat
  stack lawful at all; pre-cells, the old weak-env reference mode would have died here.

So: **flat windows hold non-escaping locals as plain values; escaping locals hold a cell
handle in the same slot** — the slot layout does not change shape either way, and no new
escape analysis is required. `[&]` capture of a slot local keeps today's documented
snapshot-by-value boundary (reference_model.md "Deliberate boundaries"); env-stored
variables keep live-cell capture. (Unifying those remains reference_model.md's own
"possible stage D" note on the cells roadmap, orthogonal to this design.)

### Environments after the fold

Environments remain for what they are actually for: globals/top-level, closures' capture
envs, method/static field-precedence scopes, `op_scope_push` bodies (try/switch/decl-in-
block), include/import. Changes:

- **Lazy frames stop touching `environment_` entirely** (today: 2 copies in, move-back
  at pop, ~45 ns instr.). The env "stack" becomes per-frame `prev_env` only on eager frames;
  `environment_` simply stays put across a lazy call.
- Eager frames keep `acquire_scope_env`/pool/release exactly as today (methods, statics,
  closures, bodies with env-visible constructs). Extending elision to methods (the 2.4 µs
  row) is follow-on work enabled by this design but staged separately (stage 5, §4).

### Call-site monomorphic callee cache

`exec_call`'s peek costs ~39 ns/call (instr.), most of it `std::function::target<>()` (RTTI). The
`call_site` struct grows an inline cache: `{const void* callee_identity,
script_defined_function* fn}` validated by one pointer compare against the loaded function
value's payload identity; hit → straight into the frame push. Miss/polymorphic falls back
to today's path (parity: the cache is an identity shortcut, never a semantic change).

---

## 3. The hard interactions (each with its design answer)

1. **Coroutine fibers (frames suspending mid-stack).** Today `run_fiber` copies the
   fiber's stack/try/iter/cfor slices out on suspend and back in above the caller's tops
   on resume (`vm_coroutine_state::saved_*`), rebasing try-record sizes. Load-bearing
   simplification: `op_yield` can only execute in a fiber's ENTRY frame (parser
   guarantees; `assert(call_records_top_ == records_base)`) — in-fiber script calls have
   always returned by yield time. Design: **per-fiber stack objects** (own segment list +
   frames vector), the VM's "current stack" is a pointer swapped by
   `run_fiber`/`resume_coroutine`. Suspends become O(1) (no copy-out/copy-in, no
   rebasing); the fiber's window just persists on its own stack. Cells make references
   into/out of fibers already-legal. Fallback if the state-plumbing surface (every
   `stack_.` touch) proves too wide in review: keep today's snapshot model — windows
   copy exactly like slices do now; both preserve semantics, per-fiber stacks are the
   cleaner end-state.
2. **Closures capturing frame locals.** Capture is already "box/copy at creation"
   (`exec_closure`: slot locals snapshot by value into a capture env; env vars share
   cells via `share_env_ref`; escape-marked slot decls already hold cells, so by-ref
   capture shares the cell). Windows change the *address* the snapshot reads from
   (`stack_[base+slot]` instead of `locals[slot]`), nothing else. Coroutine
   `outer_slot_plan` snapshots identically.
3. **The debugger (extend 304eb982's approach).** Frame walk: the flat frames vector IS
   the walk (name from `chunk->function_name`, line from `stmt_nodes[ip]`) — same as
   `frames_` today, one indirection fewer. Lazy locals: `get_current_frame_locals`
   already derives slot→name from chunk operands + fused side tables; only the storage
   read changes (`stack_[f.base + slot]` bounds-checked against the frame's live top —
   preserving the "reserved capacity only: not live yet" invisibility of not-yet-declared
   slots). `debug_paused_frame_` becomes an index. Statement-edge detection
   (`debug_stmt`/`debug_stmt_ip` per frame) moves into the frame struct unchanged. The
   frame-count handed to `on_statement` must keep today's numbering (step-over/out depth
   machine): pin with the existing both-backends debugger tests.
4. **Hot reload mid-execution.** Old bodies finish on old frames because (a) chunks are
   immutable and pinned engine-lifetime by `chunk_cache_`, (b) the executing function
   value is pinned by the callee slot (was `callee_pin`), (c) method frames keep
   `ast_pin` (resolved overload outlives redefinition) — kept, in the frame struct or a
   side vector indexed by frame. `environment::clear_all_parent_caches` and the env-epoch
   bump interact with envs only — windows carry no name caches to invalidate.
5. **Budget/limits.** Unchanged: depth check + `execution_limit_exhausted()` at call
   entry (now `op_call`'s prologue), 1024-tick clock sample, back-edge checks in
   `op_loop_back`/`op_cfor_back`/`op_iter_next`, terminal-error latch, memory cap.
   Frame-window pushes are not memory_cap-charged (parity: `call_frame` reserves aren't
   today). The budget tick stays cheap (it's 8 ns of the 353 — not a target).
6. **Parallel workers.** Already per-worker `vm_backend` instances with private
   stack_/frames_/pools — per-worker flat stacks fall out for free and
   `configure_parallel_worker`'s slot-reuse reset SHRINKS (one stack + one frames vector
   to clear instead of stack/frames/records/try/iter/cfor + arg pools). Workers never run
   top-level frames and never consult the env caches — unchanged. This is a strict
   cleanup, no new cross-thread surface.
7. **C++ boundary calls (`execute_callable`, opaque `std::function`, dynamic_binder
   callbacks re-entering).** Native entries (`call_script_function` et al.) push a window
   at the current top of the CURRENT stack (main or fiber) and run to completion —
   re-entrancy works because windows nest like the record stack does today.
   `pending_site_ctx_` / `external_site_stack_` (stateless ref-binding handover) are
   untouched. `native_stack_low()` stays on the native-entry paths (ctor chains still
   recurse natively; in-loop frames still don't).
8. **Deep-recursion guard.** `JAI_MAX_CALL_DEPTH` (10000) checked at the same points with
   the SAME message text (`JAI_MAX_CALL_DEPTH_MESSAGE`, shared with the interpreter —
   parity is byte-exact and both triggers/text are pinned by `recursion_depth_error`
   tests). Stack segments grow on demand up to depth×window; the depth cap fires first
   for sane window sizes, so the error surfaces at the identical depth with identical
   text. A pathological single frame (compile-time `local_count` beyond a segment)
   fails at compile-choice time (cap `local_count` per function ≤ segment size; parser
   depth caps make this unreachable in practice) — no new runtime error text.
   invariants §5 (MSVC Debug frame ceiling): stage 2 touches `run_dispatch` cases —
   keep `VM_TRY_OP_SHARED` for anything new and re-verify the 0xC00000FD canary
   (`recursion_depth_error` must fail catchably in Debug).
9. **Interpreter parity (behavioral, not structural).** The interpreter keeps
   `call_stack_` + env frames. Every observable that must stay byte-identical:
   - **Error text & codes**: recursion depth (both triggers), budget, memory cap, arity
     ("Function expected {0} arguments but got {1}"), undefined variable, non-lvalue ref
     args, conversion/type-mismatch texts, overflow texts naming the real op.
   - **Stack traces**: same function names/file/line frames, caller ip parked on the
     call op, `<anonymous>` naming, trace captured once at first unwind.
   - **Reference semantics**: cells share/box-on-demand identically; ref returns pass the
     handle through `detail::ref_return_pass_through`; value returns flatten; boxing
     demotes active cfor fast states; "Reference target environment has been destroyed"
     stays dead.
   - **Value lifetime**: callee locals/args destroyed at return (weak_ptr-observable),
     method-result keep-alive anchor, `clear_this_reference` timing on eager method
     frames only.
   - **Hot reload**: old body finishes; instance migration on the boundary it happens
     today.
   - **Debugger-visible state**: locals naming (incl. fused-operand naming and the two
     documented vm deltas: loop-header re-fire, never-referenced param invisible),
     frame-count numbering, park/resume/step behavior, parallel-region atomicity.
   - **Coroutines**: yield-value deref rule, completion return-type conversion skip on
     fall-off, failed-status latching, range-for driving.
   - **Implicit results**: fall-off returns null (no conversion), implicit `this` return
     rules (single-level lazy rule vs two-level eager walk — preserved per frame kind).
   Mechanism: the full suite on `--backend=vm` IS the parity gate (1844+ tests), plus the
   fuzz gate and demoreel/crawler STATE_HASH byte-parity runs.

---

## 4. Staging plan (cells-style: reviewable stages, full-suite gates, bench targets)

Every stage: full Foundry suite green on BOTH backends (Debug + Release BENCHMARKS), fuzz
gate, demoreel + crawler STATE_HASH parity across backends, debugger suite, and min-of-5
bench rows recorded in the stage's commit message. fib(15) baseline band: 687–741 µs
(anchor 697); hot loop 44–48 µs must not move in ANY stage (it makes no calls — a moved
band means collateral damage).

| stage | content | risk | est. fib(15) |
|---|---|---|---:|
| **1** | Return-path fold on the EXISTING records: result written into the callee stack slot (callee_pin deleted — the stack slot pins), no post-bind arg erase (frame claims the slice; pop truncates), per-chunk precomputed return-conversion decision (fixes the `int`-twin regression), call-site monomorphic callee cache | low — no addressing change, machinery-local | ~590–630 µs |
| **2** | **The atomic core**: value-stack frame windows. `call_frame::locals` dies for VM frames; parser slots address `stack_[base+slot]`; `bind_parameters` binds in place; frame metadata flattens to one vector (indices replace `frame*`); chunked-segment stack with the never-reallocate-a-live-window rule; native-entry + default-arg + method frames on windows | HIGH — touches every slot read/write (~85 locals-addressing sites in vm_backend.cpp), all pop/unwind paths, coroutine snapshot bases | ~440–500 µs |
| **3** | Env-swap elision for lazy frames (env stack only moves on eager frames); record/frame scrub minimization (truncate + reset the 40-byte struct) | medium — env restore paths interact with unwinding and `clear_this_on_frame_exit` | ~390–430 µs |
| **4** | Per-fiber stacks (coroutine suspend/resume become O(1) stack swaps; today's slice copy + try-record rebasing deleted) | medium, isolated to fiber machinery | no fib change; coroutine rows improve |
| **5** | (follow-on, separate design note) Method-frame elision: receiver/`this` in the frame struct instead of `acquire_method_scope_env` when the body's member access is compile-provable — attacks the 2.4 µs method row and the eager-env cost measured in §1 | high (field-vs-shadowing precedence rules) | method row, not fib |
| **6** | (follow-on) Dest-addressed superinstructions: fused ops write straight to a target slot (incl. outgoing call-arg slots — `fib(n-1)`'s subtract lands in the arg window with no stack push), extending today's `fused_operand`/`compound_fused` pattern | compiler-heavy | ~300–350 µs |

What lands incrementally: stages 1, 3, 4, 5, 6 are each independently shippable behind the
full gates. **Stage 2 is atomic** — half-moved locals addressing is not a reviewable intermediate
state; it should be one commit built green with the whole suite, prepared by a mechanical
refactor commit (accessor funnel: route every `f.locals->get_local/set_local` through
`frame_slot(f, k)` helpers FIRST, while still record-backed, so the B diff is the helper
implementation + frame lifecycle only).

Cumulative honest target: **fib(15) ≈ 350–430 µs after stages 1–3** (call machinery ~170 → ~50–70
ns/call, scaled), **≈ 300–350 µs with stage 6** — i.e. Squirrel-class (165 µs is 84 ns/call with values
half our size; we carry 32-byte typed values by design), roughly 2–2.3× today, NOT
Lua-class (§5). Interpreter fib (~1877 µs) is untouched; the published guidance
("loop/method-shaped scripts don't feel this") stays true throughout.

### Risk register — the top ways this breaks parity, and how each is caught

1. **Dangling raw pointers on stack growth** (cfor cached `var/end_ptr/step_ptr`, ref
   binds mid-frame; invariant 2b's failure mode was silent wrong loop counts in Release).
   Caught by: the segment design making reallocation structurally impossible for live
   windows + a Debug assert (window span never crosses a segment realloc) + the existing
   counted-for/demotion suites + fuzz gate + demoreel finding-2 regression tests.
2. **Value-lifetime drift at return** (args/locals destroyed later or earlier than today;
   observable via weak_ptr, destructor-ordering, method-result keep-alive). Caught by:
   ref_*/escape_* suites, aliasing tests, plus NEW pinned tests written in stage 1
   *before* stage 2: weak_ptr expiry at the return boundary, temp-receiver chaining anchor.
3. **Unwind-path truncation mistakes** (throw across N in-loop frames with live
   try/iter/cfor state; try_record stack_size snapshots vs window bases; catch running in
   the right frame with the right stack top). Caught by: the exception suites (dual error
   model), `review_regression_tests` loop-throw cases, fuzz seeds, and try-record
   assertions in Debug.
4. **Debugger-visible drift** (locals from windows: not-yet-live slots leaking as nulls,
   frame numbering shifting the step-over depth machine, paused-frame identity across the
   flat vector growing). Caught by: the both-backends parametrized debugger tests
   (breakpoint/step/resume, loop-body re-fire, named frame locals incl. fused operands)
   + the DAP attach smoke in mv_tests.
5. **Coroutine state corruption** (per-fiber stacks: try-record rebasing deleted wrongly,
   resume above live outer frames, yield-deref of a window local, fiber abandoned with
   live cells). Caught by: coroutine + range-for-driven suites, the fuzz livelock seeds
   (3507/8285/1213/8356/8391 class), and stage 4 shipping separately from stage 2 so
   bisection is clean.
6. *(watch item)* **MSVC Debug native-frame regression** in `run_dispatch` (invariant §5):
   stage-2 opcodes/cases must use `VM_TRY_OP_SHARED`; canary = `recursion_depth_error` failing
   catchably, not 0xC00000FD/exit-127.

---

## 5. What this does NOT fix (expectations, in writing)

- **Loop dispatch vs Lua** (hot loop 88 unfused / 47 fused vs Lua 7–10): per-iteration
  dispatch + value traffic, attacked by fusion coverage (the separate single-operand-RHS
  fusion item) and stage 6, not by frame windows. A flat stack changes nothing about a
  loop that makes no calls.
- **The dispatch half of fib** (~183 of 353 ns): switch dispatch, 32-byte value moves,
  checked arithmetic, budget ticks. Windows shave the load/push traffic only where stage
  6 redirects it. Lua's 38 ns/call all-in is below OUR MACHINERY FLOOR alone unless
  values shrink — and `script_value`'s engine + type_info members are the language
  (invariants §1), so they stay. Squirrel-class (~84 ns/call), not Lua-class, is the
  architectural ceiling with jaiscript semantics: **~75-85% of the machinery gap is
  recoverable; ~half of fib total is**.
- **Method dispatch** (~2.4 µs/call): binder/lookup path + mandatory method env. Stage 5
  is scoped as follow-on; this design only builds its substrate.
- **Class (re)definition bookkeeping** (175 µs row), C++ interop dispatch (sol2 18 vs 49),
  BST reference-shape costs beyond the call component — separate roadmap lines.
- **The interpreter**: keeps env frames + `call_stack_` untouched; its fib stays ~1877 µs.
  Parity is behavioral (§3.9), never structural.

---

## Appendix: reproduction

Worktree: `git worktree add --detach <dir> HEAD`; build all library TUs + a fib micro-main
with MSVC `/O2 /Ob2 /Oi /Ot /MT /DNOMINMAX` (no `/GL` for iteration speed); ALWAYS build
the uninstrumented twin of the same config — it anchors the build-config factor vs the
official band AND exposes the instrumentation's own cost (16 rdtsc reads measured ~105–200
ns/call depending on shape). Instrument `push_script_frame`/`bind_parameters`/
`return_from_script_frame`/`pop_script_frame_core`/`exec_call` with `__rdtsc()` section
accumulators + an opcode histogram in `run_dispatch`; run 300 × fib(15) after warmup on a
verified-quiet machine (check for competing cl/ninja — a concurrent agent build invalidated
one session here). Cases: fib auto, fib int, fib with a dead `try{}` (eager-env twin),
0-param call loop, recurse-10-locals. The instrumentation is scratch: it violates the
no-non-const-statics rule by design and must never land; remove the worktree afterward.
