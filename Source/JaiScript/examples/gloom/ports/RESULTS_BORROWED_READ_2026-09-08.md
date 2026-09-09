# Borrowed-table read comparison, 2026-09-08

Frozen VM comparison: c85ea0dd against b306133b. The baseline implementation
and headers match d5ad64b3, the master immediately before the borrowed-table read
increment. The baseline executable is the verified c85ea0dd build; its source
identity remains c85ea0dd.

| Configuration | Revision | P cores | Normal | Padded | Padding band |
|---|---|---:|---:|---:|---:|
| JaiScript VM serial (ms/tick) | c85ea0dd | 1 | 1.378016 | 1.378337 | -2.87% to +0.56% |
| JaiScript VM serial (ms/tick) | b306133b | 1 | 1.384346 | 1.385727 | -0.87% to +0.64% |
| JaiScript VM 8 workers (ms/tick) | c85ea0dd | 8 | 1.363836 | 1.365291 | -1.07% to +0.79% |
| JaiScript VM 8 workers (ms/tick) | b306133b | 8 | 1.251263 | 1.254243 | -1.21% to +2.32% |
| JaiDOOM VM timedemo (ms/tic) | c85ea0dd | 1 | 4.705321 | 4.683609 | -1.12% to -0.11% |
| JaiDOOM VM timedemo (ms/tic) | b306133b | 1 | 4.705196 | 4.715863 | -0.20% to +0.97% |

GLOOM with eight workers improves 8.25% in the normal layout and 8.13% in the
padded layout. Every paired revision delta is negative: -9.28% to -7.21% normal,
-8.83% to -7.10% padded. The candidate padding band is -1.21% to +2.32%; the
baseline band is -1.07% to +0.79%. This is a whole-program worker gain.

Serial GLOOM changes +0.46% normal and +0.54% padded. Its candidate padding band
is -0.87% to +0.64%, and the baseline band is -2.87% to +0.56%. No serial gain
is credited. JaiDOOM changes -0.003% normal and +0.69% padded, against candidate
and baseline padding bands of -0.20% to +0.97% and -1.12% to -0.11%. No JaiDOOM
gain is credited.

## Method

- Intel Core Ultra 9 285HX, 24 physical/logical cores, MSVC 19.51.36252.
- Serial and JaiDOOM: P core 0. Eight-worker setting: P cores 0, 1, 10, 11, 12,
  13, 22, 23. One workload process at a time, with the other agent's builds paused.
- Three interleaved ABAB rounds, six accepted readings per configuration/layout.
  Each process uses the runner's warm-up protocol. GLOOM uses a 600-tick warm-up and 600 measured ticks;
  JaiDOOM uses the 3984-tic timedemo. Metrics exclude load/bootstrap.
- Each executable has a separately linked 40-line cold-code padding control.
  The band is the observed min/max of paired padding deltas, not a confidence interval.
- The preset background screen is at most 1.0 background CPU core. Three busy
  timing attempts are retained in raw data and excluded from the medians.
- Release flags: /O2 /Ob2 /Oi /Ot /GL /Zi /MT, linked with /LTCG /OPT:REF
  /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO. Default /GS is enabled. No VM profiling
  instrumentation is enabled. The standard CMake library definitions
  JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES and JAISCRIPT_ENABLE_DEBUGGER are present in
  both builds; no debugger is attached. Complete commands are in release/compile_commands.json.
- The untracked doom1.wad was copied into the frozen workload after a preflight
  caught its absence. SHA256: 1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771.
  The failed preflight produced no timing data.

## Verification and interpretation

Executed: 72 accepted timings, 20 conformance gates and three busy exclusions.
All 95 processes exited zero and matched their reference hashes. GLOOM300:
frame 7fecc09815bb64e0, state 3580805725. GLOOM600: frame dfd969ed5a336dca,
state 1503537018. JaiDOOM: state 1746772898. The audit re-parsed every metric,
checked exact interleaving and affinity, recomputed screening and medians, and
verified unchanged binaries, workload inputs and build manifests.

Read: b306133b lets the VM declaration-read paths reach borrowed numeric buffers
and scalar elements. Borrowed strings continue through the byte-copy kernel.
The timing data attributes the worker gain to that complete increment.

Limits: the small serial and JaiDOOM changes fall within the observed bands and
receive no performance credit. The normal serial baseline contains a slow accepted
sample, retained in its padding band. This snapshot predates the separate small-region
cutoff and the nested-string barrier correction; it verifies these two workloads,
not the absence of every parallel-runtime defect. The [2026-09-09 snapshot](RESULTS_2026-09-09.md) records the later revision range.

## Raw evidence

All paths below are under Source/JaiScript/out/astra/borrowed-read-20260908/:

- comparison/{metadata,runs,summary,differences,audit}.json and per-process logs
- comparison/runner.py and comparison-launch.json
- source-inputs.json, workload-assets.json, baseline-equivalence.patch
- release/{build-inputs.json,compile_commands.json}, Release-{configure,build}.log
- comparison-run.log

Publication validation: the standing Debug, matrix, fuzz, game-hash and client gates passed in the fcab0058 cycle; the original timing audit remains unchanged.
