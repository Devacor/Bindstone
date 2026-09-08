# Whole-program snapshot: 2026-09-08, getter-corrected build

Runtime source `c85ea0dd5fce58b620c0dee9464567168bd73ebb`. The build contains
typed scalar operations, typed element loads and the property-getter replay fix.
The reference is `605b6520e78242bf916f11f903f541a0df3a85c2`. Source archives are
fixed; the only overlay is the separately gated regression test in `144c6343`.

Intel Core Ultra 9 285HX, 24 physical/logical processors, Windows x64.
Serial affinity is P CPU 0. Eight-worker affinity is P CPUs
0, 1, 10, 11, 12, 13, 22 and 23; the worker count includes the caller.
Default stack-cookie protection and checked integer overflow are enabled.

## Matched JaiScript comparison

| Runtime | Source | P cores | Unit | Normal median | Padding median | Paired padding band |
|---|---|---:|---|---:|---:|---|
| JaiScript VM serial | 605b6520 | 1 | ms/tick | 1.389456 | 1.400270 | -0.05% to +0.93% |
| JaiScript VM serial | c85ea0dd | 1 | ms/tick | 1.368986 | 1.364490 | -1.07% to +0.37% |
| JaiScript VM 8 workers | 605b6520 | 8 | ms/tick | 1.364493 | 1.364028 | -1.51% to +0.60% |
| JaiScript VM 8 workers | c85ea0dd | 8 | ms/tick | 1.376440 | 1.370503 | -1.58% to +0.56% |
| JaiDOOM VM timedemo | 605b6520 | 1 | ms/tic | 4.670808 | 4.680346 | +0.01% to +0.33% |
| JaiDOOM VM timedemo | c85ea0dd | 1 | ms/tic | 4.680723 | 4.648469 | -0.99% to -0.26% |

The padding band applies to both medians on each row.

| Runtime | Layout | Elapsed-time change | Range of paired changes |
|---|---|---:|---|
| JaiScript VM serial | normal | -1.47% | -2.08% to -1.01% |
| JaiScript VM serial | padding | -2.56% | -2.94% to -1.86% |
| JaiScript VM 8 workers | normal | +0.88% | +0.44% to +1.85% |
| JaiScript VM 8 workers | padding | +0.47% | -1.11% to +2.19% |
| JaiDOOM VM timedemo | normal | +0.21% | -0.21% to +0.47% |
| JaiDOOM VM timedemo | padding | -0.68% | -0.93% to -0.49% |

Serial GLOOM improves on both layouts and every paired comparison. This is a
small serial gain. Eight-worker medians increase, but the changes sit within
the observed padding/run bands; no worker speed gain is established.
JaiDOOM changes direction with layout, so this snapshot establishes no general
timedemo gain from this increment.

The comparison measures typed loads together with the getter correction.
It does not isolate the cost or benefit of either change.

## Fresh GLOOM comparison with Lua

| Runtime | Source | P cores | Unit | Normal median | Padding median | Paired padding band |
|---|---|---:|---|---:|---:|---|
| JaiScript VM serial | c85ea0dd | 1 | ms/tick | 1.366639 | 1.371563 | -0.46% to +0.69% |
| JaiScript VM 8 workers | c85ea0dd | 8 | ms/tick | 1.377145 | 1.380521 | -0.50% to +20.56% |
| Lua 5.4.8 | pinned reference binary | 1 | ms/tick | 0.385276 | 0.385729 | -1.28% to +1.14% |
| Lua 5.5.1 | pinned reference binary | 1 | ms/tick | 0.390491 | 0.392748 | -1.98% to +1.26% |
| LuaJIT 2.1 JIT | pinned reference binary | 1 | ms/tick | 0.100875 | 0.095740 | -14.67% to +14.49% |

The normal serial VM/Lua 5.4.8 ratio is **3.55x** and the padded ratio is
**3.56x**, with the per-runtime padding bands above.

All runtimes match the reference hashes. Lua 5.4.8 and 5.5.1 have overlapping
observed ranges. LuaJIT 2.1 JIT has a wide band and coarse short-process CPU
diagnostics; small deltas involving it are unresolved. LuaJIT JIT is a
runtime-code-generation ceiling outside Bindstone's iOS shipping target.

The eight-worker padded series contains an accepted 1.662452 ms/tick run, which
widens its band to +20.56%. That run passed the hash and background screens and
remains in the data. Its cause is unestablished. This configuration supports no
small-delta conclusion in the Lua window.

## Method and screening

Three interleaved rounds give six accepted processes per executable layout.
The revision comparison runs reference normal, candidate normal, reference
padding, candidate padding, twice per workload/round. The Lua snapshot runs
normal/padding A B A B per configuration/round.

Padding adds 40 retained volatile statements to a cold help function that the
workload never executes. The band is the observed minimum/maximum of
`100 * (padding / normal - 1)` for neighboring accepted pairs.
Revision-change ranges use `100 * (candidate / reference - 1)` with matching
layouts. These bands include placement and run variation.

GLOOM warms 600 ticks, resets the same runtime, then measures 600 ticks,
seed 666, 100x40 quad output. Script execution, frame-string production and
host hashing are timed. JaiDOOM warms with a complete replay and repeats main()
in the same engine, 3984 tics at 120x78. Its reported integer elapsed milliseconds
are divided by 3984; the script timer excludes level/demo setup.

One benchmark runs at a time. Fable's builds, tests and benchmarks are held
behind the timing window. The desktop CPU screen is 1.0 core-equivalent across
24 cores. Detected compiler, test, fuzz, client and benchmark processes prevent
launch and invalidate overlaps, checked every 250 ms and at exit.

The paired run accepted 72 timings and excluded 22 busy attempts. Its 20 gates
and all 114 process exits/hashes pass. The Lua window accepted 60 timings and
excluded 64 busy attempts; all 144 exits/hashes, including 20 gates, pass.
Both complete ABAB plans, CPU affinity, unchanged input identities, raw-log
metrics, screening verdicts and medians pass independent audits.

## Versions and compiler flags

| Component | Version / source |
|---|---|
| JaiScript | c85ea0dd runtime, fixed source archive |
| Lua 5.4 | 5.4.8, unchanged reference executable |
| Lua 5.5 | 5.5.1, unchanged reference executable |
| LuaJIT JIT | 2.1.1788460057, v2.1 at 24c20c94e7db195b640854619577441f9b4bc6be |
| Binding | sol2 3.3.0 and the isolated Lua 5.5 compatibility changes in prepare_comparison.py |
| Compiler | MSVC 19.51.36252 x64, toolset 14.51.36231 |
| Build tools | CMake 4.3.2, Ninja 1.13.2 |

JaiScript, host, adapters and stock Lua use Release
`/O2 /Ob2 /Oi /Ot /GL /Zi /DNDEBUG /MT`. C++ uses C++20,
`/EHsc /permissive-`. Linking uses
`/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`.
No profiling instrumentation is active in these executables.

The LuaJIT JIT runtime uses upstream `msvcbuild.bat lua52compat static`,
x64 GC64, FFI and JIT enabled, Lua 5.2 compatibility and default JIT settings.
Its native runtime has `/O2 /Zi` without LTCG; its adapter/host use the
shared flags. Reused Lua executable hashes and actual compiled script paths are
recorded. The 21 actual Lua source files match repository workload copies after
CRLF/LF normalization, with both byte hashes preserved.

## Correctness and profiling

| Workload | Required hashes |
|---|---|
| GLOOM 300 ticks, seed 666 | Frame 7fecc09815bb64e0, state 3580805725 |
| GLOOM 600 ticks, seed 666 | Frame dfd969ed5a336dca, state 1503537018 |
| JaiDOOM 3984 tics | State 1746772898 in warm-up and measured replay |

The final Debug suites pass 118 suites / 2752 tests on each backend, including
the growing-getter regression. All 48 constant-getter and 16 growing-getter
probes agree between backends: one getter evaluation and preserved bounds errors.

The corrected profiler separately passes reset, host-entry, nested-native and
suspension controls. Three full profiles per game retain their hashes and
identical opcode counts. [The profile report](../../../docs/review-2026-09-08/SCRIPT_PROFILE_REFRESH.md)
contains the self-ranked function tables and inclusive columns.
[The getter replay review](../../../docs/review-2026-09-08/GETTER_REPLAY.md)
records the behavioral counterexample.

## Evidence and limits

Raw root: `Source/JaiScript/out/astra/getter-fixed-20260908/`.

- `comparison/{metadata,runs,summary,differences,audit}.json` and logs:
  paired revision comparison.
- `lua-comparison/{metadata,runs,summary,audit}.json` and logs:
  fresh Lua window, including the retained worker padding outlier.
- `source-inputs.json`, `regression-final-overlay.json`,
  `reused-lua-inputs.json` and `actual-lua-source-inputs.json`:
  source and executable provenance. The test-only refinement preserves every
  runtime/workload input; earlier manifests and its initial harness log remain.
- `regression-full-interpreter.log`, `regression-full-vm.log`,
  `gloom-debug-300.log` and `verification-audit.json`: final gates.
- `getter-probes/`, `captures.json`, `profile-audit.json` and
  `profile/`: getter checks and diagnostic captures.

Verified by running: all gates, scripts, controls and completed measurement
plans. Ratios and bands are arithmetic from the recorded data. No worker or
JaiDOOM speed gain is claimed for the final increment. Small changes involving
the wide-band worker/LuaJIT JIT data are unresolved. No universal language-speed
ratio or other-machine result follows from this snapshot.
