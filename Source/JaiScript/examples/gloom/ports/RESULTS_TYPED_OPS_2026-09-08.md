# Typed scalar operations: whole-program snapshot, 2026-09-08

JaiScript runtime source `605b6520e78242bf916f11f903f541a0df3a85c2`,
containing the typed scalar instructions, typed compound/push forms and the
custom-numeric-operator guard. The reference is
`e6f84abaeb5ebb0ee7815bbfc615f97dda2bcdf9`.
Both are fixed plain source archives. All 45 JaiScript workload files and assets
match byte for byte.

Intel Core Ultra 9 285HX, 24 physical/logical processors, Windows x64.
Serial runs use P CPU 0. Eight-worker runs use P CPUs 0, 1, 10, 11, 12, 13, 22
and 23; the worker count includes the caller. Default stack-cookie protection
and checked integer overflow are enabled.

## Matched JaiScript comparison

| Configuration | Source | P cores | Unit | Normal median | Padding median | Paired padding band |
|---|---|---:|---|---:|---:|---|
| JaiScript VM serial | e6f84aba | 1 | ms/tick | 1.597487 | 1.582911 | -1.08% to -0.41% |
| JaiScript VM serial | 605b6520 | 1 | ms/tick | 1.401136 | 1.407445 | -0.59% to +0.88% |
| JaiScript VM 8 workers | e6f84aba | 8 | ms/tick | 1.491443 | 1.479610 | -1.41% to +1.80% |
| JaiScript VM 8 workers | 605b6520 | 8 | ms/tick | 1.339255 | 1.339282 | -1.18% to +0.79% |
| JaiDOOM VM timedemo | e6f84aba | 1 | ms/tic | 5.011170 | 5.043424 | +0.26% to +0.93% |
| JaiDOOM VM timedemo | 605b6520 | 1 | ms/tic | 4.687123 | 4.697038 | +0.10% to +0.42% |

The padding band applies to both medians on each row.

| Configuration | Layout | Elapsed-time change | Paired revision-change band |
|---|---|---:|---|
| JaiScript VM serial | normal | -12.29% | -12.48% to -11.34% |
| JaiScript VM serial | padding | -11.09% | -11.26% to -10.86% |
| JaiScript VM 8 workers | normal | -10.20% | -10.75% to -9.51% |
| JaiScript VM 8 workers | padding | -9.48% | -11.37% to -8.69% |
| JaiDOOM VM timedemo | normal | -6.47% | -6.74% to -6.18% |
| JaiDOOM VM timedemo | padding | -6.87% | -7.15% to -6.58% |

Both games improve on both executable layouts. Every accepted matched
reference/candidate pair has lower elapsed time in the candidate.

## Fresh GLOOM comparison with Lua

| Runtime | P cores | Unit | Normal median | Padding median | Paired padding band | Normal observed range |
|---|---:|---|---:|---:|---|---|
| JaiScript VM serial | 1 | ms/tick | 1.401709 | 1.406005 | -0.53% to +1.13% | 1.396208 to 1.408952 |
| JaiScript VM 8 workers | 8 | ms/tick | 1.342820 | 1.346534 | -0.39% to +1.50% | 1.317954 to 1.351338 |
| Lua 5.4.8 | 1 | ms/tick | 0.384441 | 0.387128 | -1.03% to +2.10% | 0.383658 to 0.388794 |
| Lua 5.5.1 | 1 | ms/tick | 0.392995 | 0.392879 | -1.78% to +1.21% | 0.388486 to 0.398593 |
| LuaJIT 2.1 JIT | 1 | ms/tick | 0.099499 | 0.095005 | -27.32% to +17.20% | 0.088558 to 0.133763 |

All five configurations pass the 300- and 600-tick conformance gates.
Three normal/padding A B A B rounds give six accepted runs per layout.
This separate window uses the same fixed JaiScript candidate and exact reused
Lua executables from the reference build. The normal serial VM/Lua 5.4.8 ratio
is **3.65x**; the padded ratio is **3.63x**, with each runtime's padding band
in the table.

Lua 5.4.8 and Lua 5.5.1 have overlapping observed ranges. LuaJIT 2.1 JIT has a
wide padding band and short-process CPU-accounting granularity; this window
does not resolve small changes involving that configuration. LuaJIT JIT is a
runtime-code-generation ceiling outside Bindstone's iOS shipping target.

Lua 5.4.8 and 5.5.1 use sol2 3.3.0 and the shared Release compiler/linker flags.
LuaJIT JIT is 2.1.1788460057, v2.1 at
`24c20c94e7db195b640854619577441f9b4bc6be`, built with upstream
`msvcbuild.bat lua52compat static`: x64 GC64, FFI and JIT enabled, default JIT
settings, `/O2 /Zi` without LTCG in the native runtime. Its host/adapter use the
shared Release flags. Dependency archive identities and the sol2 Lua 5.5
compatibility adjustments are recorded by `prepare_comparison.py` and the
[reference snapshot](RESULTS_2026-09-08.md).

The Lua window accepted 60 timings and excluded 16 busy attempts. All 96
processes, including 20 gates and the excluded attempts, exited zero and matched
hashes. The actual compiled source paths are frozen under the reference tree;
all 21 Lua script files match the repository workload copies after line-ending
normalization. Both sets of byte hashes are recorded. No script logic changed.

Additional evidence under the raw root:
`lua-comparison/{metadata,runs,summary,audit}.json`, per-process logs,
`reused-lua-inputs.json` and `actual-lua-source-inputs.json`.

## Method

Three interleaved rounds produce six accepted processes per layout and revision.
For each workload and round, order is reference normal, candidate normal,
reference padding, candidate padding, repeated twice. The padding build adds
40 retained volatile statements to a cold help function that the workload does
not execute. Its band is the observed minimum/maximum of
`100 * (padding / normal - 1)` across paired accepted runs.
The revision-change band uses `100 * (candidate / reference - 1)` on
matching layout pairs.

GLOOM performs 600 warm-up ticks, resets the same runtime, then measures 600
ticks at 100x40 quad output, seed 666. Timing includes script execution,
frame-string production and host frame hashing.

JaiDOOM warms the same engine with a complete replay, then repeats main().
The measured replay contains 3984 tics at 120x78. Reported ms/tic is the script's
integer elapsed milliseconds divided by 3984. Its timer excludes level/demo setup.

One benchmark process runs at a time. Fable's builds, tests and benchmarks are
held behind the timing window. The desktop background screen is 1.0
core-equivalent across 24 cores. Compiler, test, fuzz, client and benchmark
processes block launch and invalidate overlaps; checks run every 250 ms and
at exit. All 72 required timings were accepted. Five busy attempts were excluded.
All 97 processes, including 20 conformance gates and excluded attempts, exited
zero and matched their hashes.

## Versions and flags

MSVC 19.51.36252 x64, toolset 14.51.36231; CMake 4.3.2, Ninja 1.13.2.
JaiScript, host and adapters use C++20 and Release
`/O2 /Ob2 /Oi /Ot /GL /Zi /DNDEBUG /MT /EHsc /permissive-`.
Linking uses `/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`.
No profiling instrumentation is enabled in these executables.
The complete configure/build commands, compiler commands, PDBs and binary hashes
are retained in the evidence directory.

## Correctness

| Workload | Required hashes |
|---|---|
| GLOOM 300 ticks, seed 666 | Frame 7fecc09815bb64e0, state 3580805725 |
| GLOOM 600 ticks, seed 666 | Frame dfd969ed5a336dca, state 1503537018 |
| JaiDOOM 3984 tics | State 1746772898 in both warm-up and measured replay |

The candidate's full Debug suites passed on both backends: 117 suites and
2750 tests each, including the same-frame operator-registration regression
in `0efb8b29`. Debug GLOOM300 hashes pass on both backends.

Identical standalone operator-transition probe sources ran 68 processes per
revision, covering both backends and proofs on/off. The old typed-op library
`4922f220` has eight differing backend comparisons; the corrected candidate
has zero. A native callback registering an operator inside an active function
must affect its next arithmetic operation.

## Evidence

Raw root: `Source/JaiScript/out/astra/typed-ops-20260908/`.

- `comparison/{metadata,runs,summary,differences,audit}.json` and per-process logs:
  complete paired comparison, hashes, CPU pinning, input identities and bands.
- `source-inputs.json`, `tree/`, `workload-input-audit.json`:
  fixed source and identical workload inputs.
- `Debug-*`, `Release-*`, `release/compile_commands.json`:
  build commands, settings and products.
- `transition-full-interpreter.log`, `transition-full-vm.log`,
  `transition-gloom-*.log`: full Debug and GLOOM gates.
- `operator-transition-audit.json`, `operator-transitions/` and
  `../operator-transition-20260908/`: corrected and old behavior.
- Reference build: `../landing-20260908/release/`.

Verified by running: conformance, full suites, exact completed ABAB order,
affinity, unchanged inputs, log-derived metrics, screening decisions and medians.
The source changes identify the measured bundle; this comparison attributes no
individual fraction of the gain to a particular opcode.
