# OSSUARY single-thread gap: operation counts × cost per operation

The workload is scene 15 at 100x40 from t=3.0 with seed 20260705: five warmed frames and stream hash f5ca54c216432104. It compares the natural JaiScript VM (master 1c3e7974, `skull_natural.jai`) with Lua 5.4.8 (the pinned demoreel-deps source). Each line is marked **[measured]** or **[inferred]**.

## Headline

| | JaiScript VM | Lua 5.4.8 | Ratio |
|---|---:|---:|---:|
| Time, ms/frame (median of 6, range) **[measured]** | 281.33 (277.55-284.13) | 164.46 (163.81-164.92) | 1.711 |
| Core clock, GHz (12 readings, 5.025-5.139) **[measured]** | 5.103 | 5.103 | |
| Core cycles per frame **[derived]** | 1,435.5M | 839.2M | 1.711 |
| Dispatched operations per frame **[measured]** | 52.24M | 89.04M | 0.587 |
| Core cycles per dispatch **[derived]** | 27.5 | 9.4 | 2.92 |

**The gap is entirely costlier operations.** JaiScript dispatches 0.59x as many operations as Lua, and each one costs 2.92x as many cycles. In log terms the time ratio is ln 1.711 = +0.54. That total is +1.07 from cost per dispatch and -0.53 from the dispatch count.
- At Lua's cost per dispatch, JaiScript's 52.24M dispatches would take 96 ms/frame, 0.59x Lua. **[derived]**
- At JaiScript's cost per dispatch, Lua's 89.04M would take 480 ms. **[derived]**

Both runtimes do the same computation. Each frame performs about 43M arithmetic operations (JaiScript 215.6M over 5 frames, Lua 214.9M). Each also performs 3.93M math-library calls (JaiScript C_MATH/MATH_TEMP/MATH 19.64M over 5 frames, Lua C calls 19.67M). **[measured]**

JaiScript's lower dispatch count has three sources: pair superinstructions (C_ARITH_P2 carries two operations), math as in-block intrinsics, and inlined script functions.
- Lua instead spends dispatches on GETUPVAL (18.4M/frame, module constants and `math.*` locals), MOVE (7.3M), CALL (5.0M) and GETTABLE (5.0M).
- JaiScript makes 1.07M Lua-equivalent function calls per frame into 0.03M CALL_TYPED. **[measured]**

## Where the cycles go

### JaiScript (37,210 samples over 10 runs) **[measured]**

| Category | Share | Mcyc/frame | ms/frame |
|---|---:|---:|---:|
| Cell-block instructions, excluding libm | 58.6% | 840.9 | 164.8 |
| — of which the block loop's shared dispatch (instruction fetch, switch, status tail) | 11.7% | 167.4 | 32.8 |
| Cell C_MATH time inside libm (sin, cos, sqrt, `__remainder_piby2`) | 6.5% | 93.3 | 18.3 |
| General-form instructions, excluding libm | 25.5% | 365.8 | 71.7 |
| — of which the general dispatcher's shared dispatch | 4.5% | 64.6 | 12.7 |
| General MATH_TEMP inside libm | 0.3% | 4.9 | 1.0 |
| Block entry and exit, total | 9.05% | 129.9 | 25.5 |
| — entry: guards, unboxing, site resolution (`cells_enter`, `cells_resolve_sites`) | 7.3% | 105.2 | 20.6 |
| — exit: C_EXIT flush and reboxing | 1.5% | 21.2 | 4.2 |
| — block-run glue | 0.25% | 3.5 | 0.7 |
| Outside the VM (host, parallel region setup) | 0.04% | 0.6 | 0.1 |

**Host calls.** The worker makes no native host calls apart from the math intrinsics. Script-to-script CALL_TYPED (itrunc, sk_trace) runs 0.03M/frame and costs 1.1% of time. **[measured]**

**Worker engine.** OSSUARY runs its rows through `parallel_transform` even with one worker. Worker 0 runs on the calling thread with its own VM backend, and it does 99.6% of the dispatches (general 6.16M/frame, cells 45.90M/frame). The main engine does general 0.04M and cells 0.14M. **[measured]**

**Block counts.** 74,013 block entries per frame, no misses, each running about 620 cell dispatches. **[measured]**

### Lua (32,402 samples over 15 runs) **[measured]**

| Category | Share | Mcyc/frame | ms/frame |
|---|---:|---:|---:|
| Shared instruction fetch, trap check and switch dispatch | 39.3% | 330.2 | 64.7 |
| CALL, total | 24.3% | 204.0 | 40.0 |
| — inside libm | 10.5% | 87.7 | 17.2 |
| — call machinery (`luaD_precall`, `precallC`, `moveresults`, `luaD_poscall`, `prepCallInfo`) | 8.4% | 70.6 | 13.8 |
| — C function bodies (`math_*`, `luaL_checknumber`, `lua_pushnumber`) | 4.3% | 35.9 | 7.0 |
| — interpreter code of the CALL case itself | 1.2% | 9.8 | 1.9 |
| Other instruction bodies (arithmetic, GETUPVAL, GETTABLE, MOVE, compares, loops) | 36.3% | 304.6 | 59.7 |
| Outside the VM | 0.07% | 0.6 | 0.1 |

### Same mechanisms, side by side **[derived]**

| Mechanism | JaiScript | Lua |
|---|---:|---:|
| Shared dispatch cost per dispatch | 3.64 cycles (cells), 10.43 (general) | 3.71 cycles |
| libm per math call | 25.1 cycles | 22.3 cycles |
| Math call overhead excluding libm | 16.2 cycles (the C_MATH intrinsic) | 23.2 cycles (call machinery 14.1 + C body 9.1), plus GETUPVAL and MOVE for the callee and argument |
| Instruction body per dispatch, excluding dispatch and libm | cells 14.6 cycles, general ~49 cycles | 4.7 cycles |

The cell loop's dispatch is as cheap as Lua's. The gap is in the instruction bodies, in the general form, and in block entry. The in-block math intrinsic saves only about 7 cycles per call over Lua's complete C call.

## Top 15 operations by time **[measured counts and shares, derived cycles]**

- "Excl." is the samples on the instruction's own lines.
- "With dispatch" adds its count-proportional share of its dispatcher's shared lines.
- Cycles are per dispatch at 5.103 GHz. Grouped rows share one case in the dispatcher, so the split inside a group is not observable.

### JaiScript

| # | Operation | Dispatches/frame | Time | ms/frame | Cycles/op excl. | With dispatch |
|---:|---|---:|---:|---:|---:|---:|
| 1 | C_ARITH_P2 (two arithmetic ops) | 16.26M | 23.4% | 65.7 | 20.6 | 24.3 |
| 2 | C_MATH (includes libm) | 3.72M | 10.7% | 30.1 | 41.3 | 45.0 |
| 3 | T_ADD_FF..T_MOD_II group (general typed arithmetic; T_MUL_FF 1.45M, T_ADD_FF 0.91M, T_SUB_FF 0.69M) | 3.17M | 8.1% | 22.7 | 36.5 | 46.9 |
| 4 | CELLS_ENTER, CELLS_BACK (block entry, run and flush) | 0.048M | 6.0% | 16.8 | 1,753 | 1,764 |
| 5 | C_MOV | 9.57M | 5.7% | 16.0 | 8.5 | 12.1 |
| 6 | C_ARITH_P+C_LOAD | 2.72M | 4.8% | 13.5 | 25.4 | 29.0 |
| 7 | T_DECL_MOV | 0.70M | 1.7% | 4.8 | 34.7 | 45.2 |
| 8 | CELLS_CFOR_BACK | 0.036M | 1.6% | 4.5 | 640 | 651 |
| 9 | MATH_TEMP | 0.20M | 1.6% | 4.5 | 115.5 | 125.9 |
| 10 | C_JCMP_P | 3.84M | 1.3% | 3.7 | 4.9 | 8.6 |
| 11 | C_ARITH_P | 3.66M | 1.3% | 3.5 | 4.9 | 8.6 |
| 12 | T_LOAD | 0.21M | 1.2% | 3.4 | 85.6 | 96.0 |
| 13 | C_LOAD | 1.12M | 1.2% | 3.4 | 15.5 | 19.2 |
| 14 | CALL_TYPED | 0.03M | 1.1% | 3.1 | 587 | 597 |
| 15 | C_LOAD2 | 0.27M | 1.0% | 2.9 | 54.1 | 57.7 |

The full cell histogram per frame, largest first: C_ARITH_P2 16.26M, C_MOV 9.57M, C_JCMP_P 3.84M, C_MATH 3.72M, C_ARITH_P 3.66M, C_ARITH_P+C_LOAD 2.72M, C_JUMP 1.86M, C_LOAD 1.12M, C_JCMP_ANY 0.56M, C_ARITH_P+C_JUMP 0.50M, C_LOAD+C_JCMP_P 0.39M, C_CMP_P 0.34M. **[measured]**

The most frequent consecutive cell pairs:
- C_ARITH_P2 → C_ARITH_P2: 23.2% of pairs.
- C_MOV → C_MOV: 6.8%.
- C_MOV → C_ARITH_P2: 5.4%.
- C_MATH → C_MOV: 4.7%. **[measured]**

### Lua 5.4.8

| # | Operation | Dispatches/frame | Time | ms/frame | Cycles/op excl. | With dispatch |
|---:|---|---:|---:|---:|---:|---:|
| 1 | CALL (includes the C callee and libm) | 4.99M | 24.3% | 40.0 | 40.9 | 44.6 |
| 2 | MUL | 17.02M | 7.0% | 11.5 | 3.4 | 7.1 |
| 3 | GETTABLE | 4.99M | 5.9% | 9.7 | 9.9 | 13.6 |
| 4 | GETUPVAL | 18.39M | 5.1% | 8.3 | 2.3 | 6.0 |
| 5 | SUB | 8.40M | 4.3% | 7.0 | 4.3 | 8.0 |
| 6 | ADD | 11.84M | 3.9% | 6.3 | 2.7 | 6.4 |
| 7 | MOVE | 7.32M | 1.5% | 2.4 | 1.7 | 5.4 |
| 8 | MULK | 3.26M | 1.3% | 2.1 | 3.3 | 7.0 |
| 9 | FORPREP | 0.20M | 1.1% | 1.8 | 47.7 | 51.4 |
| 10 | LTI | 1.12M | 0.9% | 1.5 | 7.0 | 10.8 |
| 11 | LT | 1.71M | 0.7% | 1.2 | 3.5 | 7.2 |
| 12 | FORLOOP | 0.86M | 0.7% | 1.1 | 6.8 | 10.5 |
| 13 | LE | 0.66M | 0.6% | 1.0 | 7.4 | 11.1 |
| 14 | RETURN1 | 1.05M | 0.5% | 0.8 | 3.7 | 7.4 |
| 15 | GTI | 0.45M | 0.4% | 0.6 | 7.1 | 10.8 |

**Comparable operations.** A JaiScript arithmetic operation costs 12.2 cycles in the pair form (24.3 per C_ARITH_P2) and 8.6 alone (C_ARITH_P). A Lua ADD, SUB or MUL costs 6.4-8.0. A C_MOV costs 12.1 cycles, a Lua MOVE 5.4. **[derived]**

The pair's two operations are usually dependent (`a*b + c`). The pair costing more per operation than a lone C_ARITH_P fits a latency-bound chain: each result makes a round trip through the memory-resident cell payload before the next operation reads it. **[inferred]**

## Code-level causes (read from the source that was built)

- **`exec_c_arith` runs a second switch per operation.** The cell opcode tells only "proven arithmetic". The arithmetic kind is decoded from `ins.a >> k_cell_op_shift`, then an int/float branch and an inner `switch (op)` pick the operation. Every call also branches on constant versus register for the right operand (a constant is read as a `script_value` through `unchecked_as_float`). Every call writes three things: payload, kind byte and dirty bit. C_ARITH_P2 does all of this twice per dispatch. Lua's MUL tests two tags, computes and stores the value and tag.
- **`cell_land` is out of line.** C_MOV and C_MATH land through `vm_backend::cell_land`, which reads the destination cell's descriptor and walks the declared-type rules before the three-field store. `cell_land` is 3.5% of all JaiScript samples as a leaf function (about 50 Mcyc/frame), and `chunk::cell_mask::set` another 0.8%. **[measured]**
- **`typed_arith_fast` re-switches.** All general typed arithmetic ops share one dispatcher case. It re-switches on `ins.op`, resolves both operands through `typed_left`/`typed_right`, and lands through `typed_land_float`/`typed_land_int`. Those land functions switch on the destination kind and compare type_info and engine before an in-place store. `fused_cmp_operand`, `env_lookup_cached`, `typed_arith_fast`, `typed_land_float`, `unchecked_as_float` and `script_value` construction and destruction together are about 10% of samples as leaf functions. **[measured]**
- **Block entry and exit cost about 1,760 cycles per block run.** That is 9.05% of time over 74k entries per frame. Entry checks every cell's guard and unboxes (2.86M unboxes per 5 frames), and resolves container sites (`cells_resolve_sites`). Exit reboxes: 1.61M cells fully replaced and 0.94M flushed in place per 5 frames. Each block runs about 620 cell dispatches, so entry and exit are about 12% of the block's own cost. The block-count and entry figures are **[measured]**, the per-entry cycles are **[derived]**.
- **The general form still runs 25.5% of the time on 12% of the dispatches,** at about 60 cycles per dispatch against about 20 in cells. The ops are the typed scalar rows (T_MUL_FF, T_ADD_FF, T_SUB_FF, T_DECL_MOV, T_LOAD, T_STORE_MOV), guards (INLINE_GUARD, MATH_GUARD) and CALL_TYPED. They are code outside closed cell loops. **[measured]** The profile does not name the source spans: the worker backends keep no per-function tables. **[inferred]**

## Levers and their ceilings

The gap to close is 596 Mcyc per frame (JaiScript 1,435.5, Lua 839.2), which is 41.5% of JaiScript's time. The time each lever addresses is **[measured]**; the ceilings are **[inferred]** estimates of the best case, and the levers overlap.

| Lever | Time it addresses | Ceiling (Mcyc/frame, % of JaiScript time, ms) | Basis |
|---|---|---:|---|
| General-form code into cells (straight-line regions, remaining open loops) | 370.7 Mcyc, 6.20M dispatches at ~60 cycles | ~245 (17%, 48 ms) | the same dispatches at the cell average of 20.3 cycles |
| Cell executor cost per instruction: an opcode per arithmetic kind with cells in the instruction word, no inner switch, constants pre-decoded, `cell_land` inlined or specialized per kind, guards and dirty-bit marking hoisted to entry and flush | 673 Mcyc of cell instruction bodies (14.6 cycles per dispatch, excluding dispatch and libm) | ~150-300 (10-21%) | the upper end brings bodies to about 8 cycles per dispatch, a pair carrying two operations against Lua's 4.7 per single op; the measured pieces are `cell_land` ~50 Mcyc and the C_MATH non-libm overhead of 60 Mcyc (16 cycles against about 5 for an inlined intrinsic) |
| Cheaper block entry and exit | 129.9 Mcyc (~1,760 cycles × 74k) | ~110 (8%, 22 ms) | entry plus exit at about 200 cycles; entering blocks further out (once per ray, not per inner loop) removes entries outright |
| C_MOV elimination (copy propagation and coalescing in the translation) | 9.57M/frame at 12.1 cycles = 116 Mcyc | ~116 (8.1%, 23 ms) if every move goes; ~70-90 if 60-80% go | C_MOV → C_MOV and C_MATH → C_MOV are 11% of cell pairs |
| Expression fusion (arithmetic chains of any length, intermediates in machine registers) | 23.3M arithmetic dispatches/frame, ~84 ms | ~80-190 (6-13%) | removed dispatches at 3.64 cycles each (~40 Mcyc if the count halves), plus the store-forwarding latency of dependent operations (~5 cycles × 20-30M) |
| Native code (AOT/JIT) as the bound | everything but libm | leaves ~230 Mcyc (~45 ms/frame, ~0.27x Lua) | libm 98 Mcyc stays; ~50M scalar ops at ~2 cycles in registers ≈ 100 Mcyc; ~30 Mcyc of loop and memory traffic |

Taken together, the interpreter levers reach Lua only if nearly all of them land. Their ceilings sum to about 700-960 Mcyc against the 596 Mcyc needed; the figures overlap (fusion and per-instruction cost remove partly the same cycles), and a realistic 60-70% of each ceiling lands near parity rather than clearly past it. **[inferred]** Native code clears the gap with room to spare. **[inferred]** No single lever closes it.

**Recommended order:**
1. **General-form code into cells.** The largest single lever, it improves the general path, and every later cell improvement then covers that code too.
2. **Cell executor cost per instruction.** Specialized per-kind opcodes, inlined or specialized landing, hoisted guards and dirty marking. It touches every cell dispatch and is the precondition for fusion paying its latency term.
3. **Block entry and exit.** A concentrated 9%, all in `cells_enter`, `cells_resolve_sites` and `cells_flush`.
4. **C_MOV elimination.** A translation-time change of up to 8%.
5. **Longer expression fusion with register-held intermediates.** Most of its value is removed latency, which needs item 2's register-direct handlers.
6. **Native code.** The bound and the long-term route, well past Lua.

## Method

- **Builds.** From a snapshot of master 1c3e7974: MSVC 19.51 Release, /O2 /Ob2 /Oi /Ot /GL, /LTCG /OPT:REF /OPT:ICF, static CRT, the audit's flags. A second build adds /Zi /DEBUG for symbols. Lua is built from the pinned source identities in `demoreel-deps/manifest.json`.
  - The /Zi twins time within 0.3% (JaiScript 282.07 ms) and 0.7% (Lua 165.59 ms) of the plain builds in the same rounds. **[measured]**
  - In the same session the 2026-09-29 audited binaries measure 278.12 ms (JaiScript 4343df54) and 165.56 ms (Lua), against their audited 275.68 and 164.25. **[measured]**
- **Host overlay.** Outside the timed loop, the demoreel host calls `measure_begin` and `measure_end` and signals named events at the loop's two ends. Timings come from the host's own interval.
- **Timing.**
  - Six interleaved rounds (the order reverses on alternate rounds) of JaiScript-rel, Lua-rel, both /Zi twins and both audited binaries, one process at a time.
  - Each process was pinned to CPU 0, a P core (the fastest efficiency class: CPUs 0, 1, 10-13, 22, 23).
  - 36 runs were accepted and 25 rejected by the background ceiling of 3.0 core equivalents. Accepted runs had 0.77-2.99 background cores, median 2.73.
  - Other agents' builds, test suites and benchmarks (cl, link, ninja, `jaiscript_tests`, `jai_gloom`, `jai_demoreel`) ran during 34 of the 36 accepted runs.
  - The spreads stay narrow (JaiScript 2.3%, Lua 0.7%) and the audited binaries reproduce their audited medians within 0.9%, but this is a desktop-load measurement, not a quiet-machine gate.
- **Clock.**
  - Core clock: a chain of dependent one-cycle adds (100 per iteration, MASM) timed against QPC on CPU 0 before and after each round. Median 5.103 GHz, range 5.025-5.139, TSC 3.072 GHz.
  - The Windows `% Processor Performance` counter over the session puts CPU 0's upper quartile at 5.05-5.16 GHz while loaded, consistent with the chain.
  - Cycles are core cycles, not TSC cycles.
- **Counts.**
  - JaiScript comes from a VMPROF build (`/DJAISCRIPT_VM_PROFILE`), whose tables are reset at the measured window's start. The instrumented copy's `engine::vm_profile_reset` also resets the parallel worker backends. Without that reset the worker table covers warm-up plus measured frames, exactly 2x the measured-span counts.
  - "Cell dispatches" are the block loop's switch dispatches. The block's own "cell instructions" counter (316.26M per 5 frames) counts fused pairs' components separately and is not used here.
  - Lua comes from a copy of the pinned source with one counter in `vmfetch` (every dispatched instruction) and counters in `precallC` and in the Lua-function branches of `luaD_precall`/`luaD_pretailcall`. Its counters reset at the window's start.
  - A second run of each counting binary reproduced every per-operation count exactly. **[measured]**
- **Attribution.**
  - A sampler suspends the target's main thread every ~400 µs (±50% jitter) and records the program counter, then walks the stack through dbghelp (`StackWalk64`). Samples are taken only inside the measured window.
  - Addresses are resolved with inline frames (`SymQueryInlineTrace`, `SymGetLineFromInlineContext`).
  - The innermost frame in a dispatcher decides the operation: the case of `run_dispatch`, `cells_run_frame<0>` or `luaV_execute` whose line holds the physical frame's call site. Labels that share one handler are reported as a group.
  - Lines outside the cases are the shared dispatch rows.
  - Sampled runs take 294 ms (JaiScript) and 171 ms (Lua) of running time per frame, 4.6% and 4.1% above unsampled. The sampler perturbs both runtimes alike.
  - Attribution uses the whole-program /Zi binaries. Every sampled run produced the reference hash.
- **Derived cycles.** Per operation: share × cycles per frame ÷ dispatches per frame. The rdtsc cycles inside VMPROF (about 20 cycles per cell instruction whatever the instruction) are instrumentation-dominated and used for nothing here.

## Raw data

All of it is in the gitignored `Source/JaiScript/out/ossuary-decomp-20260930/` of this checkout:
- `raw/timing-runs.json`, `raw/timing-*.log`, `raw/typeperf.csv`
- `raw/sampling-runs.json`, `raw/samples/{jai,lua}-NN.txt`, `raw/samples/*-attribution.json`, `raw/samples/*-analysis.txt`
- `raw/jai-vmprof-{2,3}.log` (+ `.json`), `raw/lua-opcount-{1,2}.log`
- `raw/tables.json`, `raw/tables.md`

The builds and instrumentation are in the same folder:
- `build-rel`, `build-sym`, `build-prof`
- `tree/` with `overlay.diff` and `runtime_patch.py`
- `lua-count/` with `lua-count.diff`
- `tools/`: `sampler.cpp`, `freq.cpp`, `chain.asm`, `analyze.py`, `tables.py`, `measure.py`, `session.sh`, `count_repeat.py`, `parse_vmprof.py`

To reproduce, run `bash tools/session.sh` (it takes the machine's timing lock), then `python tools/analyze.py jai raw/samples/jai-*.txt`, the same for `lua`, and `python tools/tables.py`.
