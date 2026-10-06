# Fast-path census: which fast lanes can become the only path

The VM pairs many fast lanes with a general twin: a guarded head that declines into the general code. This census counts, for every
such pair, how often each side runs over the four game workloads and the Foundry suite on the VM. It classifies each pair as one of:

- (a) the twin is dead or nearly dead;
- (b) the lane can be widened to cover what falls to the twin;
- (c) both sides are needed;
- (d) the same semantics are written twice inside one backend and can collapse to one kernel.

It ranks the resulting work by value per effort. Counts are **[measured]** (exact, from a counting build). Causes read from the code are
**[read]**. Estimates of lines and performance are **[inferred]**.

Duplication between the interpreter and the VM is deliberate (the interpreter is the oracle) and is not counted against either. Only
duplication inside one backend counts. Line counts refer to `slowpath` at `b6500b24c`, which is master `e56c81143` plus twenty VM
commits; function names are the stable anchors. Rows that exist only on `slowpath` are marked *slowpath*.

## The tool

The census uses the coverage build (`-DJAISCRIPT_VM_COVERAGE=ON`; see `docs/review-2026-10-01/COVERAGE_CENSUS.md`) plus a lane table:

- `include/jaiscript/vm/lane_census.hpp` declares one counter per lane outcome as `X(id, group, text)`.
- `JAI_LANE(id)` in `vm_backend.cpp` counts at the decision point.
- `engine::vm_coverage_report` appends two tables: the lanes, and general dispatches and cell instructions per opcode.
- A Foundry run, which builds and destroys thousands of engines, folds each backend's counters into process totals as the backend
  dies. With `JAI_LANE_DUMP=<file>` set, the totals are written at exit.
- A build without the define compiles every hook to nothing.

The tooling is on branch `fastpath-census-lanes`, on top of `slowpath`.

A group's shares are shares of that group's executions. Each lane counts once, where it decides, so a twin that runs a sibling lane
again shows up in both groups.

## Workloads

| workload | run | operations | in cells | typed general | untyped general | state |
|---|---|---:|---:|---:|---:|---|
| Gator Quest (GQ) | smoke, 2000 ticks, 2 bots | 688.7M | 86.7% | 7.6% | 5.7% | hash 3218840367 |
| OSSUARY (OSS) | demoreel scene 15, 100x40, 5 frames, 1 worker | 339.2M | 92.1% | 5.8% | 1.9% | scene f5ca54c216432104 |
| GLOOM | smoke, 600 ticks, seed 666, serial | 75.0M | 88.7% | 3.3% | 7.8% | 1503537018, frame parity OK |
| JaiDOOM (DOOM) | p10 timedemo, 120x78 | 1307.8M | 92.4% | 3.2% | 4.1% | 1746772898 |
| Foundry | `jaiscript_tests --backend=vm` | — | — | — | — | 3980 tests green, 22,607 backends |

The general form, typed and untyped together, is 8-13% of executed operations in the four games. Every number below is a share of that
general traffic or of a lane's own executions. Cell misses are at most 0.1% of operations in every game. The cell machinery and its
general form are both live, and the general form is the oracle (class c, below).

## Ranked rows

| # | Row | Class | Evidence [measured] | Lines [inferred] | Effort | Risk | Perf [inferred] |
|---|---|---|---|---:|---|---|---|
| 1 | **Delete the lanes nothing reaches** (`binary_fast_shape`, `typed_cmp_jump_head`, the vector twins in `exec_call_method`, the profile-only `pc.fn` branch, the constructor carrier tail, the constant-string map read, dead `cells_run_cold` cases) and fold the `_entry` return twins | a | 0 hits in all five runs, Foundry included | −470 | low | very low | neutral; GQ and DOOM each stop running 1.28M declining probes |
| 2 | **One scalar math table for the general form and the cells**: `sin cos tan sqrt floor ceil abs` and the angle conversions are rows of the cell form (`exec_c_math`) and of the alias kernel (`eval_math_alias`) but not of `math_scalar_unary`, so `op_math_temp` and `op_math` send them to `eval_math_intrinsic` | b + d | `op_math_temp` reaches the kernel for 100% of OSS's 841k executions, 11% of GLOOM's, 2.5% of DOOM's | −40 net | low | low | gain: OSS's ~1M kernel calls per run become raw landings (~1-2% of the run) |
| 3 | **Lowered instructions their lane cannot take**: float compound `op_t_*_ff` (100% miss), `op_d_j*` on strings (DOOM 99.6% miss), `op_d_add` on strings (DOOM, GLOOM 16% miss), through one compare-row and one string-row helper | b + d | see top 5 | −30 net | low | low | small gain |
| 4 | **T_LOAD and `typed_decl_read` are one lane written twice**: a T_LOAD miss re-runs the proto form, which succeeds 0% (GQ) and ≤0.7% (DOOM, 2.19M retries) | d | see top 5 | −40 | low | low | small gain |
| 5 | **`exec_call`'s slice head into `invoke_callee`**; `op_return_binary` through `exec_binary`'s numeric head | a + b + d | head takes 44 of 211k `op_call`s; `return a op b` reaches `binary_general` 100% of the time | −90 | low | low | neutral (off the game paths) |
| 6 | **One numeric row kernel and one compare kernel inside the VM** (`detail/scalar_ops.hpp`, already included and unused) | d | all ten lanes that carry the rows are live (96-100% hits) | −250 | medium | medium (hot) | must be neutral (G-perf) |
| 7 | Environment-variable stores through the load's cache | b | the store head declines env stores 97% (GQ), 96% (GLOOM), 49% (DOOM, 516k), 68% (Foundry); loads of the same names hit `env_lookup_cached`'s fast head 99.9% | +40 | medium | medium | gain: DOOM's 516k uncached walks per run |
| 8 | Verbatim twin bodies: LOAD ladder (`exec_load_impl` / `fused_ident_value`), method push (`push_method_frame_sliced` / `push_method_frame`), the env-lazy block of the two script pushes, the index fold of `index_fused_read` / `exec_index_store_fused`, four member-IC probe copies | d | both sides live | −450 | high | medium-high (hot) | must be neutral |
| 9 | String append row in the compound head *slowpath* | b | DOOM's compound head misses 97.5% (470k: `row += PAL_FG[top]`); its compound fused misses 100% (159k) | +15 | low | low | small gain |
| 10 | T_STORE_MOV int-to-float widening row (T_DECL_MOV has one) | b | DOOM T_STORE_MOV misses 30% (170k), and the same 170k miss the store head's tag rule; cause [inferred] | +8 | low | low | small gain |
| 11 | Plain-frame plan widening (plan-2 binds that need a conversion, plan-1 parameters, worker frames) | b | DOOM: 27.5% of parked calls take the pinned push (528k plan-2 bind misses, 323k plan-1); OSS worker: 60% (189k `plain_worker`) | +? | medium-high | medium | DOOM call path |
| 12 | Exception landing twins (`parked_call_threw` ≡ `call_threw`) and the namespace store twin (`store_static_target` / `exec_set_static`) | d | trivial | −35 | low | very low | none |
| 13 | Subsystems idle in all four games: raw calls and frame programs; the unpinned cell forms behind `ir_passes(false)` | c (Dev decision) | `raw_probe` 0 hits and `C_RET` 0 in the games, 709 raw entries in Foundry; `C_ARITH` / `C_CMP` / `C_JCMP` / `C_LOAD_ANY` 0 in the games | −450 / −120 if retired | — | — | none in the games |

Rows 1-5 and 12 delete code without restructuring a hot path. Rows 6 and 8 are the larger deletions, but they restructure hot paths
and owe the full perf gate. Rows 7, 9, 10 and 11 add lanes rather than delete them; they follow the principle only where widening lets
a twin's row go.

## The top five

### 1. Delete the lanes nothing reaches (a)

Every entry below counted 0 in all five runs, the 3980 Foundry tests included.

| Lane | Where | Why nothing reaches it [read] | Lines |
|---|---|---|---:|
| `binary_fast_shape` and its guards | the body, plus guards in `exec_binary`, `exec_return_binary`, `exec_return_binary_entry`; `binary_shape_*` in `opcode.hpp`; `shape=` in the disassembler | The compiler emits `op_binary` only with `binary_shape_none` (audit row X1, still present). `binary_shape()` becomes a bool fusability test at compile time. Lane counts: `stack_binary_shape`, `ret_binary_shape`, `ret_binary_entry_shape` | ~212 |
| `typed_cmp_jump_head` and its call in `exec_fused_cmp_jump` | | A fused compare-jump whose operands are direct slots and whose class is proven is already an `op_t_j*`. What remains has an environment or subscript operand, which `direct_operand` cannot read, so the head declines every time: 1.28M (GQ), 1.28M (DOOM), 32k (GLOOM), 1.60M (Foundry). The numeric rows below it decide the same comparisons (`fcmp_row_int` takes the 1.29M in GQ) | ~44 |
| Vector-section twins in `exec_call_method` | worker-pin enter; method-cache hit; array push | A found pin returns in the slice section. The method-cache guard is the slice's own: static sites return there, and a resolution decline repeats identically. The vector push has the sliced push's gate. The unpinned-worker raise stays | ~60 |
| `pc.fn` branch of `exec_call_from_scratch_general` | | Reached only in VM_PROFILE builds, where the door's fast check is compiled out. Drop the `#ifndef` in `exec_call_from_scratch` too | ~10 |
| Carrier tail of `enter_constructor_in_loop` | | The block before it returns on every path (audit X3) | ~35 |
| Constant-string map lane of `index_fused_read` | | The scalar-key map lane below admits the same shape with the same hit and miss semantics; only the probe differs (audit X13). No test reaches it either | ~30 |
| `cells_run_cold` cases for `op_c_rebind` / `op_c_mload` / `op_c_mstore` | | The hot switch takes them first | 3 |
| `exec_return_binary_entry`, `exec_return_ident_entry` | | Once the shape lane is gone, each differs from its twin only in the finish call (`entry_return_finish` against `return_with_result`). One template parameter, or a finish flag, folds each pair | ~40 |
| Bound-thunk vector lane of `invoke_callee` | | 0 in five runs. Bound thunks are entered earlier (`cfsg_bound_sliced`, the stack head). Delete it after a test either reaches it or proves it unreachable | ~20 |

**Proof owed.** G-suite, manifest identity (`binary_shape` is 0 in every chunk today), G-hash, and a perf spot check. The
`typed_cmp_jump_head` removal changes no compiled code. Its only runtime effect is that the declining probe is gone.

### 2. One scalar math table for the general form and the cells (b, with d)

The common one-argument math functions have three implementations, and the general form's lanes use none of them:

- **The cell form.** `exec_c_math` hand-codes rows for `abs` (int kept int, `INT64_MIN` misses), `floor`, `ceil`, `sqrt`, `sin`,
  `cos`, `tan` and the two angle conversions. The block compiler's math gate in `lower_typed_ops` lists the same functions.
- **The alias kernel.** `eval_math_alias` repeats the float rows for the stdlib aliases.
- **The general form.** `exec_math_temp` (`op_math_temp`) and `exec_math` (`op_math`) take only `math_scalar_unary` /
  `eval_scalar_unary` (trunc, cbrt, exp, logs, inverse and hyperbolic trig, saturate, sign) and the pair rows. Every
  `math::sin(x)`, `math::sqrt(x)` or `math::floor(x)` outside a block therefore goes to `eval_math_intrinsic`, with its arity check,
  argument reading, value mint and `checked_result`.

| | GQ | OSS | GLOOM | DOOM | Foundry |
|---|---:|---:|---:|---:|---:|
| `op_math_temp` executions (unary, non-alias) | 8 | 841k | 105k | 990k | 19k |
| of which the kernel | 4 | **841k (100%)** | 12k (11%) | 25k (2.5%) | 18k |
| `op_math` executions (no lane counter; the same rows) | 0 | 166k | 7.1k | 146k | 29k |

OSSUARY's scene uses `math::sin` 167 times, `math::sqrt` 111 and `math::cos` 60 in its sources. The block form runs them as
`C_SIN_F` / `C_COS_F` / `C_SQRT_F` (10.1M / 5.8M / 3.6M). Outside blocks, every one goes to the kernel.

**Fix:**
- Extend `math_scalar_unary` / `eval_scalar_unary` with the alias-kernel set: `abs` keeps an int an int, with a miss for `INT64_MIN`
  so the kernel raises its text; the others return a float.
- `exec_math_temp`, `exec_math` and the default row of `exec_c_math` all use the extended table.
- `exec_c_math`'s nine hand-coded rows go, `eval_math_alias` becomes a call into the table, and the compiler gate reads the table
  instead of its own list.

The table is in `detail/math_intrinsics.hpp`, and the VM and its compiler are its only readers. The results are the same `std::`
calls, so they are byte-identical.

**Proof owed.** G-suite (math suites on both backends), G-fuzz math band, G-hash (OSS scene hash), and G-perf on OSSUARY, where the
gain is expected.

### 3. Lowered instructions their lane cannot take (b, with d)

Three lowerings emit a fast instruction for a shape its lane declines by construction. Each miss then walks two or three general
executors.

**Float compound** (`x += y` on a float slot in statement position).
- `lower_typed_ops` lowers `op_compound_fused` of class float/float to `op_t_add_ff`, `op_t_sub_ff`, `op_t_mul_ff` or `op_t_div_ff`
  with destination `k_typed_dst_compound`.
- `typed_land_float` has no compound case (its `default: return false`), so the lane misses 100% of the time
  (`t_arith_miss_float_compound`: OSS 28k, GLOOM 28k, which is 3.4% of GLOOM's typed arithmetic).
- The miss runs `exec_typed_fallback` → `exec_compound_fused`, whose fast row takes int targets only (`compound_fused_miss_float_target`:
  OSS 30k, GLOOM 28k) → the verbatim pair → `exec_compound_store`'s head, which finally takes the float (`compound_head_hit` 28k).
- **Fix:** a compound case in `typed_land_float` that updates a float slot's payload in place, the int case's mirror. The values are
  the head's (`t + r` on two floats). About 8 lines.

**`op_d_j*` on strings** *slowpath*.
- `exec_dyn_cmp_jump` takes only numeric rows.
- `d_jcmp_miss_kind` is 99.6% of DOOM's `op_d_j*` executions (235k), 49% of GLOOM's and 19.5% of GQ's.
- Each miss re-runs `exec_fused_cmp_jump`, which reads both operands again, runs the same numeric rows, then takes its string row
  (`fcmp_row_string`: DOOM 235k, GLOOM 52k).
- **Fix:** `exec_fused_cmp_jump`'s numeric, string and null rows become one row function, `fused_cmp_rows(lp, rp, op) -> int`
  (taken-false, taken-true, or "run the pair"). `exec_dyn_cmp_jump` calls it with its instruction-word operands, and its own
  numeric-row copy (~30 lines) goes.

**`op_d_add` on strings** *slowpath*.
- `d_arith_miss_string` is 16% of DOOM's dynamic arithmetic (501k; `tex_composite` `name + "..."`) and 16% of GLOOM's (34k).
- The miss runs `binary_fused_compute` from the top: a second operand resolution, numeric rows that fail, then `binary_fused_tail`'s
  string row.
- **Fix:** `binary_fused_tail`'s string `+` row (memory charge included) becomes a helper. `dyn_arith_fast` calls it for `op_d_add`
  on two plain strings, and `binary_fused_tail` calls the same helper.

**Proof owed.** G-suite and G-fuzz (`--no-proofs`, `--no-passes`, MISS build). The texts are untouched because the rows are the same
rows. G-hash. Manifest identity holds: the lowering does not change.

### 4. T_LOAD and `typed_decl_read`: one lane, two implementations (d)

`typed_load_fast` (40 lines, reading the flattened `chunk::typed_site` row) and `typed_decl_read` (25 lines, reading the
`fused_index_proto`) are the same lane: container, int index with up to two fold steps, landing through `typed_decl_land_element`. On a
T_LOAD miss, `exec_typed_load` runs `exec_index_fused_decl(orig)`. That re-runs `typed_decl_read` on the proto, then falls to
`index_fused_read<false>` and `exec_decl_var`.

| | GQ | OSS | GLOOM | DOOM | Foundry |
|---|---:|---:|---:|---:|---:|
| T_LOAD executions | 21.4M | 844k | 160k | 17.4M | 768k |
| T_LOAD misses | 121k (0.6%) | 1056 | 13k (8.1%) | 2.19M (12.6%) | 10k |
| `typed_decl_read` tries (misses plus standalone `INDEX_FUSED_DECL`) | 121k | 1056 | 36k | 2.21M | 11k |
| of which hit | 0 | 0 | 23k | 16k (≤0.7%) | 754 |

In GQ and DOOM the retry nearly never succeeds. In GLOOM the 23k hits cannot be told apart from the 84k standalone `INDEX_FUSED_DECL`
executions, which reach the same function. The two forms also choose different landing stamps: `site.stamp`, or `temp_stamp_for` for a
temp, against `p.decl_type`.

**Fix:** one kernel that takes the decoded operands (container pointer, index operand list, fold ops, landing slot, expect, stamp).
Both opcodes call it, and the T_LOAD miss goes straight to `index_fused_read`. The drift between the forms disappears with the second
copy.

**Proof owed.** G-suite, G-fuzz MISS build, G-hash. The T_LOAD rows in the manifest stay identical.

### 5. `exec_call`'s head into `invoke_callee`; `op_return_binary` through the stack head (a + b + d)

- **`exec_call`** (103 lines) peeks the callee below the arguments and tries a callee cache, a validated thunk and a bound-thunk slice
  entry before the pooled vector and `invoke_callee`. Over 211k `op_call` executions in Foundry it took 0 cache hits, 44 validated
  thunks and 0 bound slices. The four games run `op_call` at most four times; identifier callees use the probe door.
  - Fold the head into `invoke_callee`, keeping the call-operator check (audit X17). A test with an opaque callee comes first.
- **`op_return_binary`** has no numeric head. `ret_binary_general` is 100% in every run: GLOOM 14k, GQ 4.8k, Foundry 315k, all into
  `binary_general` and the full handler. `exec_binary`'s stack head *slowpath* takes 99% of GQ's `op_binary` and 95.6% of Foundry's.
  - Make that head a helper `stack_numeric_rows(l, r, op, out) -> bool` shared by `exec_binary` and the folded `exec_return_binary`
    (row 1). The return path gains the lane and loses its shape guard.

**Proof owed.** G-suite, a new opaque-callee test, G-hash.

### Next after the five: one numeric row kernel and one compare kernel in the VM (row 6, d)

The int-int arithmetic rows (overflow-checked, zero-divisor checked) and the six comparisons are written out in every lane:

- `binary_fused_compute`
- `typed_binary_head`
- `typed_arith_fast`
- `typed_cmp_jump_fast`
- `dyn_arith_fast` and `exec_dyn_cmp_jump` *slowpath*
- `exec_fused_cmp_jump`
- the stack head of `exec_binary` *slowpath*
- `exec_compound_store`'s head *slowpath*
- `exec_compound_fused`
- `cfor_compare`, `exec_cfor_back` and `cfor_advance`
- the cell ops (`exec_c_arith`, `exec_c_arith_any`, `exec_c_cmp`, `exec_c_jcmp`, `cell_cmp_any`, the inline `op_c_add_ii` /
  `op_c_j*` cases)
- `typed_int_index_row` and `raw_int_arith`
- `binary_fast_shape`, which row 1 deletes

There are about 18 copies of the int arithmetic rows and 12 of the int comparison.

Every one of these lanes is live:

| lane | hit rate |
|---|---|
| `t_arith_hit` | 96.6-100% |
| `t_jcmp_hit` | 100% |
| `d_arith_hit` | 80-99% |
| `fused_typed_head_hit` | 55-91% in the games |
| `stack_binary_head_hit` | 49-100% |
| `cfor_back_fast` | 99-100% |

So the fix is one kernel under all of them, not a deletion. `detail/scalar_ops.hpp` (`int_arith`, `float_arith`, `scalar_compare`) is
the interpreter's single kernel (audit D2, interpreter half landed). `vm_backend.cpp` includes it and calls nothing from it. Each lane
keeps its own operand fetch and its own miss policy (raise, cell miss, or decline); only the row switch moves.

**Proof owed.** G-suite, G-fuzz including the MISS build, manifest identity, G-hash, G-perf in both layouts. The MSVC Debug frame
ceiling applies to anything inside `run_dispatch` (invariant 5).

## Every pair measured

Shares are of the lane's own executions, in the order GQ / OSS / GLOOM / DOOM / Foundry. The full table, every outcome with counts, is
`docs/fast_path_census_lanes.txt`.

### Arithmetic, comparison, moves

| Pair | Fast share | What falls through | Class |
|---|---|---|---|
| `op_t_*` arithmetic over `exec_typed_fallback` | 100 / 99.6 / 96.6 / 100 / 94.3 | float compound (row 3); kind misses where the checker's unknown class speculated int (OSS 25k, Foundry 13k) | c, plus b for float compound |
| `op_t_j*` over `exec_fused_cmp_jump` | 100 everywhere | 477 kind misses in Foundry | c (the twin is the general op the lane was lowered from) |
| `op_d_*` arithmetic over the original op *slowpath* | 98.7 / 99.5 / 80.0 / 83.9 / 98.9 | strings (row 3); GLOOM 9.3k landing or fault | b |
| `op_d_j*` over `exec_fused_cmp_jump` *slowpath* | 80.5 / 97.6 / 50.9 / **0.4** / 98.8 | strings | b (row 3) |
| `typed_binary_head` inside `binary_fused_compute` | 91.1 / 75.5 / 73.5 / 54.6 / 9.0 of all fused binaries; the rest are untyped (class 0) sites, plus Foundry's 2.8M declines | | c |
| `binary_fused_compute` numeric rows over `binary_fused_tail` | the tail's general row takes 22 / 2.5 / 8 / 31 / 68% and its string row 1.2 / 2.2 / 50 / 33 / 1.2% of untyped executions | object operators, strings, bound values | c (the tail is the only path for those) |
| fused operand fast head (`fused_cmp_operand`) over `fused_ident_value` | 100 / 100 / 100 / 100 / 99.998 | 101 in Foundry | c (the ladder raises the undefined-name texts) |
| `typed_cmp_jump_head` inside `exec_fused_cmp_jump` | **0** everywhere | everything | a (row 1) |
| `exec_fused_cmp_jump` rows over the verbatim pair | int / numeric / string / null rows take 100 / 100 / 100 / 98.8 / 17.6 | objects with operators (Foundry 1.54M) | c |
| `exec_binary` stack head over `binary_general` *slowpath* | 99.0 / 100 / 48.8 / 87.2 / 95.6 | strings, objects | c |
| `binary_fast_shape` | **0** | | a (row 1) |
| `op_return_binary` | **0** fast (no head) | 100% `binary_general` | b + d (row 5) |
| `op_compound_store` head over `exec_compound_store_full` *slowpath* | 0 / 52.9 / 100 / 2.5 / 5.7 | strings (DOOM, GQ), global-only and env targets | b for strings (row 9); c for the rest |
| `op_compound_fused` int row over the pair | **0** in all four games; 99.8 in Foundry | games: float targets (row 3) and strings | b (once row 3 lands, only Foundry's int shapes reach it, which the typed lowering also takes) |
| `op_cfor_back` fast state over the generic update | 100 / 99.2 / 100 / 100 / 99.3 | | c (the generic update is the loop's own code) |
| `op_t_decl_mov` / `op_t_decl_const` over LOAD + DECL_VAR | 100 in the games; 94.3 / 100 in Foundry | payload mismatch | c |
| `op_t_store_mov` over LOAD + STORE | 100 / 100 / 100 / 70 / 1 | DOOM 170k (row 10); Foundry reference slots | b (row 10), c |
| `op_store` head over `store_popped_value_full` *slowpath* | 2.6 / 96.0 / 1.4 / 20.7 / 8.1 | environment and top-level stores (97 / 3 / 96 / 49 / 68), reference slots (Foundry 18.6), strings (DOOM 14.8) | b (row 7), c |
| `op_math_temp` scalar rows over `eval_math_intrinsic` | the kernel takes 50 (4 of 8) / **100** / 11 / 2.5 / 95% | `sin cos tan sqrt floor ceil abs` are outside `math_scalar_unary` | b + d (row 2) |
| math alias kernel over the real call | Foundry only (75% hit) | | c |

### Calls

| Pair | Fast share | What falls through | Class |
|---|---|---|---|
| probe callee cache head over the resolver | 98.3 / 15.7 / 52.0 / 99.9 / 65.5 | OSS: the worker never arms (`cached_global_env_` is null on workers: 59.5% unarmed); GLOOM: stale rows 19% | c; b for workers is row 11's companion |
| call door (`exec_call_from_scratch`) parked script over general | 99.6 / 98.1 / 91.0 / 95.0 / 61.5 | host callables (DOOM 164k `cfsg_native`), sibling methods, default-argument and opaque values | c |
| plain frame (`push_plain_frame`) over `push_script_frame_pinned` | 95.0 / 39.9 / 69.8 / 72.3 / 1.2 | workers (OSS 60%), plan-2 bind misses (DOOM 17%), plan 1 (DOOM 10.4%, GLOOM 27.6%), depth gate and block scope (Foundry) | b (row 11), c |
| tail splice | 0 in the games; 39.5% of Foundry's parked calls | | c (the TCO ruling) |
| `exec_call` slice head over `invoke_callee` | **0.02** (44 of 211k in Foundry; ≤4 calls in the games) | everything | a (row 5) |
| `invoke_callee` lanes | constructor 48%, in-loop 43%, bound method value 4.5%, static 2.3%, native 2.8%; bound-thunk vector lane **0** | | a for the bound-thunk lane (row 1), c for the rest |
| graft guard over the fallback call | 100 / 99.7 / 96.4 / 100 / 84.7 | | c |
| `op_call_builtin` inline rows over the registry | 34.7 / 0 / 19.6 / 92.1 / 2.8 of executions; the rest are sites with no inline row. 6 inline declines in Foundry; 0 kind misses anywhere | | c (the kind guard is correctness) |
| `op_call_method` host native lane | OSS 73.5%, DOOM 10.7% of method calls (binary readers) | | c |
| `op_call_method` method cache, slice section | GLOOM 7.7%, Foundry 23.7%; misses ≤1.5% | | c |
| `op_call_method` vector-section cache, worker pin, push | **0** | | a (row 1) |
| `op_call_method` registry direct | 99.0 (GQ), 82.7 (DOOM) of method calls | the checker could not prove the receiver kind, so `op_call_builtin` was not emitted | c (the indexed registry call is the general path for these) |
| raw call probe over the door | 0 in the games; Foundry 0.1% hit (709 of 861k) | `raw_probe_cache` 98.3%: no armed callee cache at the site | c, row 13 |
| raw return over the boxed pad | Foundry 91.5% | | c |
| return identity over `return_with_conversion` | 99.6 / 99.9 / 93.8 / 100 / 100 | | c |
| `op_return_ident` slot rung over `fused_ident_value` | 100 / 100 / 100 / 99.9 / 99.8 | | c (environment returns) |
| frame pop plain over the boxed-cells pop | 100 / 99.7 / 99.5 / 100 / 100 | | c (captured slots) |
| constructor in-loop over the opaque native path | 100 everywhere; the carrier tail **0** | | a for the tail (row 1) |

### Members, elements, variables

| Pair | Fast share | What falls through | Class |
|---|---|---|---|
| env lookup cache fast head over the slow head, fill and walk | 99.9 / 100 / 80.2 / 99.8 / 97.5 | GLOOM slow head 18% (scope-env frames), fills | c |
| LOAD ladder: slot / env cache / this-field IC / member scope | slot 80 / 93 / 53 / 74 / 28; env cache 20 / 0.6 / 40 / 26 / 72; walk **0** everywhere | | c (the walk raises the undefined-name texts) |
| this-field store IC in `store_popped_value_full` | all bare field writes in methods (GLOOM 10k, Foundry 600k) | | c |
| `op_member_temp` proof or IC over `exec_get_member` | 100 / — / 100 / 0.7 / 78.3 | DOOM: 133 executions in total | c |
| `op_get_member` proof or IC over `member_access_value` | 63 / 50 / 99 / 92 / 61 | GQ 116k map receivers (member sugar on maps), Foundry 181k | c (the ladder is the only path for maps, strings, accessors) |
| member write proof or IC over `assign_member` | 98.7 / — / 99.2 / 0 (22 writes) / 6.9 | Foundry 752k (accessors, host objects, statics) | c |
| `op_t_load` over `exec_index_fused_decl` | 99.4 / 99.9 / 91.9 / 87.4 / 98.7 | DOOM 2.19M, then a retry of the same lane | d (row 4) |
| typed index store over the fused store body | 97 / 100 / 57 / 99.6 / 23 | | c |
| `index_fused_read` lanes over the replay | replay 0.003 / 0 / 9.7 / 12.5 / 23.3 | DOOM 559k (reference keys, writes) | c |
| constant-string map lane | **0** | | a (row 1) |
| `op_index_store_fused` lanes over the replay | replay 1.6 / — / 1.5 / 6.5 / 1.2 | | c |
| `op_t_push` / push fused append over the slice kernel and replay | 99.9-100 | | c |
| concat append lane over `binary_general` | 100 everywhere | | c (the twin holds the operator-table and non-string rows) |

## Duplication inside one backend

**VM** (beyond the rows above; locations by function):

- **LOAD ladder twins.** `exec_load_impl` (164 lines) and `fused_ident_value` (158) run the same rungs; one pushes, the other fills a
  scratch. `resolve_local_or_env` and `resolve_local_or_env_cached` differ by the cache rung.
- **Frame pushes.** Eight call-record setups: `push_plain_frame`, `push_script_frame_pinned`, `push_script_frame`,
  `push_method_frame_sliced`, `push_method_frame`, `push_static_method_frame`, `raw_call_enter`, `tail_splice_frame`.
  - The env-lazy block is verbatim in `push_script_frame_pinned` and `push_script_frame`.
  - The method-env block is verbatim in the two method pushes, which are marked KEEP BYTE-PARALLEL.
  - `binds_in_place`'s primitive admission is repeated in `bind_plain_references`.
- **Index paths.** The index fold is verbatim in `index_fused_read` and `exec_index_store_fused` (~90 lines each). There are four
  in-bounds array reads, two map plain reads (`exec_index_impl` repeats its own in its catch), and five element-store enforcement
  copies (`in_place_1` … `in_place_5`).
- **Member IC.** The probe-and-arm is copied in `exec_get_member`, `fused_member_container`, `exec_member_temp` and
  `cells_member_container`; the write IC in `member_field_write_fast` has different fill rules.
- **Array push.** Five array push implementations (`typed_push_append`, `exec_typed_push`, `exec_push_fused`, and the two
  `exec_call_method` pushes, one of them dead).
- **Cells.** "Constant or cell to (kind, bits)" is read in twelve places. Cell math hand-codes rows that `math_intrinsics.hpp` holds.
- **Callee cache.** The identity compare appears four times; the callee-cache fill twice.
- **Exception landing.** `parked_call_threw` ≡ `call_threw`; `guarded_native_call` carries the same catch bodies.

**Interpreter.** `visit_binary`'s former fast paths FP1-3 now fetch operands and call one `scalar_lane` built on
`detail/scalar_ops.hpp`, and `interpreter_dispatch.cpp` is 126 lines of thin calls into `detail/binary_handlers.hpp`. The
"three copies of arithmetic" that `Source/JaiScript/CLAUDE.md` describes are one kernel in the interpreter today. The VM half is row 6,
and the VM's `op_binary` shape operand it names is dead (row 1).

What the interpreter still duplicates:

- the compound-assignment ladders (cpp-bound, in place, implicit this, member) and `evaluate_arithmetic`, about 12 copies of int `+`;
- the counted-loop step and compare.

Those are audit row C1, which waits on rulings R-q3 and R-q4. The coverage build counts the VM only, so the interpreter's lane hit
rates are not measured here.

## Knobs that default to the slow choice

None of the engine settings does:

- `type_proofs`, `ir_passes`, `parameter_alias`, `local_alias` and `jaibite_cache` default on.
- `hot_reload_reporting` defaults off, and `import_recheck_interval` defaults to 0.2 s.
- `execution_budget` and `memory_cap` cost the same tick at every back edge whatever their values.

Not counted:

- the VM-as-default-backend change, handled elsewhere;
- `JAISCRIPT_CHECKED_OVERFLOW`, which is a semantic choice.

The rest are cliffs, not knobs:

- **Custom numeric operator latch.** Any global overload of `+ - * /` sets the latch. It turns off cells, raw calls, the counted-loop
  fast state, the cell compound index store, the typed heads and the interpreter's `scalar_lane`. This is by design (the operator
  must be consulted).
- **Armed debugger session.** It turns off cells, raw calls, grafts and the replicated dispatch tails.
- **`JAI_SIGNALS_NO_THREADSAFE`.** Unset by default, so `jai::signal` takes a mutex. It is a host-side cost; defining it is the fast
  choice for single-threaded hosts.
- **`cells_counters_`.** Release builds still count block entries, misses, resumes and boundaries for the `engine::cells_counters`
  testing lever: one increment per event.
- **`JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES`.** Forced on in `source/CMakeLists.txt` while nothing reads it. Dead, free to remove.

## Not measured

- The prepared builtin callback frame and host direct entry exist only on another agent's branch (`a1eeee2af`), not on master or
  `slowpath`. They take the same shape, a prepared frame with a fallback to the function value, and want the same lane counters when
  they land.
- Lane counts inside the cell block loop are the coverage census's (`COVERAGE_CENSUS.md`). Block entries, refusals and misses come
  from its tables; the new lane table counts only the general dispatcher and the call machinery.

## Reproducing

The coverage build:

```
cmake -S . -B out/build/x64-Coverage -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAISCRIPT_LTCG=OFF -DJAISCRIPT_VM_COVERAGE=ON
cmake --build out/build/x64-Coverage --target jaiscript_runner jai_gloom jai_demoreel jaiscript_tests
```

The runs:

```
jaiscript --no-debug --no-pause --backend=vm --coverage=gq.txt scripts/gatorquest.jai -- smoke ticks 2000 bots 2      (examples/gatorquest)
jai_demoreel --perf 5 --scene 15 --w 100 --h 40 --backend vm --workers 1 --coverage oss.txt                            (examples/demoreel)
jai_gloom --smoke --backend vm --ticks 600 --seed 666 --workers 0 --coverage gloom.txt                                  (examples/gloom)
jaiscript --no-debug --no-pause --backend=vm --coverage=doom.txt p10_timedemo.jai -- 120x78                             (examples/jaidoom/scripts)
JAI_LANE_DUMP=foundry.txt jaiscript_tests --backend=vm                                                                  (the build directory)
```

Each report ends with the lane table (`lane group | outcome | count | share | text`) and the opcode table
(`op NAME | general | cells`). The counting build changes no result: the GLOOM, OSSUARY and timedemo hashes above match their
references, and the Foundry suite is green on it.
