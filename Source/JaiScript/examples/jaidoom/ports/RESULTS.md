# JaiDOOM Lua comparison, 2026-09-20

The frozen JaiDOOM workload conforms on JaiScript and Lua. This snapshot measures both runtimes on one pinned P core with desktop activity present. Dev authorized a 1.50-core background allowance for this run after the strict 0.20-core gate did not hold.

## Warmed results

| Runtime | Normal median, ms/tic | Padded median, ms/tic | Paired padding band |
|---|---:|---:|---:|
| JaiScript VM 9e078bd6 | 2.538 | 2.531 | -0.80% to +0.44% |
| Lua 5.4.8 | 1.397 | 1.368 | -3.06% to +2.59% |

Each median contains six accepted samples across three interleaved rounds. The padding band is the minimum and maximum of 100 * (padded / normal - 1) for corresponding samples. It includes placement effects and measurement variation; it is not a confidence interval.

JaiScript takes 1.816 times Lua's elapsed time in the normal layout and 1.850 times in the padded layout, using the medians above and their reported padding bands. Lua uses about 45 to 46 percent less time in this workload. These ratios describe JaiScript 9e078bd6. The checkout was at 173bca7a during measurement; its newer runtime was not measured.

| Runtime | Normal sample range, ms/tic | Padded sample range, ms/tic |
|---|---:|---:|
| JaiScript VM 9e078bd6 | 2.527 to 2.553 | 2.523 to 2.538 |
| Lua 5.4.8 | 1.356 to 1.416 | 1.359 to 1.395 |

The 24 required timing samples were accepted, with zero rejected timing attempts. Background activity during accepted runs was 0.473 to 1.153 core equivalents, median 0.651. No competing build or test process was detected. The Lua padding median shifts by -2.08 percent; the JaiScript padding median shifts by -0.28 percent. Small differences within these spreads do not establish an optimization gain. This is a desktop-load comparison, not a strict-idle result.

## Snapshot and method

- JaiScript runtime/source revision: 9e078bd65a62cf1bfab1deeef408dad62c44b546. Port commit: 1f77fcc663da737aed339e322a4f1711a02797b7.
- Lua 5.4.8, int64 integers and double numbers; the unchanged pinned source archive has SHA256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae.
- The 17 original modules, demo and WAD hashes are in SOURCE_INPUTS.json. WAD data is not committed.
- Workload: 3984 recorded E1M1 tics, 120x78, serial simulation, BSP renderer, textures, sprites, weapons, HUD and complete frame strings. Sound is off.
- Hardware: Intel Core Ultra 9 285HX, 24 cores and 24 logical processors. Windows reports eight CPUs in the fastest efficiency class: 0, 1, 10, 11, 12, 13, 22, 23. Every workload process was pinned to logical CPU 0, one P core.
- Compiler: MSVC 19.51.36252, x64, CMake/Ninja. Release C/C++ flags /O2 /DNDEBUG /Zi; the Lua host adds /Ob2 /Oi /Ot /GL, with /LTCG /OPT:REF /OPT:ICF at link. Static CRT. Full commands and source/binary identities are in the raw build manifests.
- Each process executes a complete cold replay followed by a warm replay in the same runtime. The measured interval is the warm replay loop, including normal runtime/GC work and frame construction. Parsing, map setup, cold warm-up and trace hashing are outside that interval.
- Each round runs Jai normal, Lua normal, Jai padded, Lua padded, then repeats that sequence. Three rounds produce six accepted samples per runtime/layout. Only one workload process runs at a time.
- Each padding executable adds 40 retained volatile statements to its unused help function and is relinked.
- ms/tic is the reported integer elapsed milliseconds divided by 3984, not the rounded display value.
- The runner checks CPU activity before each process and across its lifetime, and polls for competing build/test processes. The 1.50-core ceiling applies to this authorized run; the runner's default remains 0.20.

## Verified by running

This measurement completed four normal/padded runtime gates and 24 timed processes. Every process exited successfully and reported state 1746772898 after both cold and warm replays. The audit reparsed all raw logs, checked the exact interleaving and affinity, recomputed medians and padding bands, and rehashed the executable and workload inputs.

The existing full frame certificate was revalidated against those inputs before timing and during the audit. Its traces were captured previously; frame tracing was not enabled during measurement.

| Frozen-build gate | Result |
|---|---|
| Full timedemo state | 1746772898 on all cold/warm replays |
| Per-tic simulation and frame identity | 7968/7968 records agree for JaiScript VM, interpreter, Lua normal and Lua padded |
| Cold frame-stream FNV-1a64 | 0ad5f98ab443f0bb |
| Warm frame-stream FNV-1a64 | c2502bc18d9f42c2 |
| Last frame | 79443 bytes on both replays |
| Lua P4 smoke | ALL GREEN |
| Lua P7 benchmark/feel checks | All 14 checks green |
| Original P4/P7 | Green on both JaiScript backends |
| Full Debug language suites | 3160 tests passed per backend |
| GLOOM 300 ticks, seed 666 | Frame 7fecc09815bb64e0, state 3580805725 on both backends, workers 0/8, normal/padded |
| Comparison-runner regression tests | 22 passed |

The full Debug and GLOOM results are the frozen build's prior gates. They were not rerun during timing. No runtime code or port algorithm changed for this measurement.

## Raw evidence

All paths below are under Source/JaiScript/out/astra/jaidoom-lua-20260916/:

- timing-desktop-20260920-220013/: metadata.json, runs.json, summary.json, audit.py, audit.json, authorization.json, console.log and all 28 workload logs.
- full-warm-conformance/: comparison.json, inputs.json, four traces and logs.
- lua-numeric-loops/: comparison.json and p4_smoke.log / p7_bench.log.
- debug-gates/: results.json and complete suite logs.
- game-gates/: results.json and GLOOM/P4/P7 logs.
- frozen/: source-inputs.json, source.zip and the frozen workload tree.
- build-logs/ and integrated-release/build-inputs.json.
- timing-20260917/ and timing-20260918/: strict attempts that produced zero accepted timings.
- timing-20260920-215149/: strict attempt with four passing cold/warm gates and zero timed samples; stopped after Dev authorized the desktop-load run.

## Reproduce this snapshot

From the repository root, using the existing verified frozen build and a fresh output directory:

    python Source/JaiScript/examples/gloom/ports/run_comparison.py --build Source/JaiScript/out/astra/jaidoom-lua-20260916/integrated-release --jai-workload-root Source/JaiScript/out/astra/jaidoom-lua-20260916/frozen/tree/Source/JaiScript --engines jai-lua --workloads timedemo --rounds 3 --timedemo-conformance Source/JaiScript/out/astra/jaidoom-lua-20260916/full-warm-conformance --max-background-cores 1.50 --output Source/JaiScript/out/astra/jaidoom-lua-20260916/timing-desktop-retry

Reserve the shared timing window first. Omit --max-background-cores to use the strict 0.20-core default; keep its results separate from this desktop-load snapshot. The runner verifies the frame certificate and current inputs before measuring and retains rejected attempts in the raw evidence.
