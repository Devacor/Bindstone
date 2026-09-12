# GLOOM and JaiDOOM: reviewed-source experiments, 2026-09-11

This snapshot compares frozen 121a with two isolated repair experiments and refreshes the conforming GLOOM rivals. The experiments are uncommitted source overlays. They are not current master or a claim that Fable has adopted the repair.

GLOOM uses 600 warm-up ticks followed by 600 measured ticks after rebooting the game inside the same runtime. JaiDOOM runs its 3984-tic timedemo once to warm the engine, then invokes main again in the same process; ms/tic is its reported integer elapsed milliseconds divided by 3984. Values are medians. The padding band is the minimum and maximum observed paired change from normal to padding, not a confidence interval.

| Workload | Source | P cores | Normal ms/tick or ms/tic | Padded ms/tick or ms/tic | Padding band |
|---|---|---:|---:|---:|---|
| JaiScript VM serial | 121a | 1 | 1.197611 | 1.200222 | -0.346% to +2.911% |
| JaiScript VM serial | Stable repair experiment | 1 | 1.208721 | 1.208916 | -0.878% to +1.289% |
| JaiScript VM serial | String repair experiment | 1 | 1.211485 | 1.205147 | -0.805% to -0.023% |
| JaiScript VM 8 workers | 121a | 8 | 0.783109 | 0.780717 | -29.490% to +2.694% |
| JaiScript VM 8 workers | Stable repair experiment | 8 | 0.775648 | 0.774589 | -3.855% to +2.441% |
| JaiScript VM 8 workers | String repair experiment | 8 | 0.783012 | 0.776636 | -0.912% to +0.763% |
| JaiDOOM VM timedemo | 121a | 1 | 2.807103 | 2.809237 | -0.045% to +0.322% |
| JaiDOOM VM timedemo | Stable repair experiment | 1 | 2.986195 | 2.994478 | -0.126% to +1.337% |
| JaiDOOM VM timedemo | String repair experiment | 1 | 2.651230 | 2.654242 | -1.196% to +0.732% |

The rival binaries are identical in all three variant positions. The following pools those 18 accepted measurements per layout and retains every paired padding observation. All use one P core.

| GLOOM rival | Normal ms/tick | Padded ms/tick | Padding band |
|---|---:|---:|---|
| Squirrel 3.2 | 1.173193 | 1.182134 | -2.018% to +3.022% |
| Lua 5.4.8 | 0.387006 | 0.381493 | -3.308% to +2.920% |
| Lua 5.5.1 | 0.393417 | 0.393921 | -0.827% to +2.514% |
| LuaJIT 2.1 JIT | 0.095365 | 0.092845 | -32.462% to +17.058% |

Per-position rival medians and bands remain in summary.json. Their A/A differences measure variation with identical binaries. LuaJIT 2.1 JIT is a runtime-code-generation ceiling, not an iOS-shippable Bindstone target.

Paired JaiScript changes, with each source's padding band beside the delta:

| Sources | Workload | Layout | Median change | Paired change range | Before padding band | After padding band |
|---|---|---|---:|---|---|---|
| 121a to Stable repair experiment | JaiScript VM serial | normal | +0.928% | +0.272% to +1.487% | -0.346% to +2.911% | -0.878% to +1.289% |
| 121a to Stable repair experiment | JaiScript VM serial | padding | +0.724% | -2.307% to +1.596% | -0.346% to +2.911% | -0.878% to +1.289% |
| 121a to Stable repair experiment | JaiScript VM 8 workers | normal | -0.953% | -29.163% to +2.219% | -29.490% to +2.694% | -3.855% to +2.441% |
| 121a to Stable repair experiment | JaiScript VM 8 workers | padding | -0.785% | -2.302% to +0.817% | -29.490% to +2.694% | -3.855% to +2.441% |
| 121a to Stable repair experiment | JaiDOOM VM timedemo | normal | +6.380% | +6.217% to +6.549% | -0.045% to +0.322% | -0.126% to +1.337% |
| 121a to Stable repair experiment | JaiDOOM VM timedemo | padding | +6.594% | +6.206% to +7.648% | -0.045% to +0.322% | -0.126% to +1.337% |
| 121a to String repair experiment | JaiScript VM serial | normal | +1.158% | +0.333% to +1.856% | -0.346% to +2.911% | -0.805% to -0.023% |
| 121a to String repair experiment | JaiScript VM serial | padding | +0.410% | -2.796% to +1.411% | -0.346% to +2.911% | -0.805% to -0.023% |
| 121a to String repair experiment | JaiScript VM 8 workers | normal | -0.012% | -29.147% to +2.725% | -29.490% to +2.694% | -0.912% to +0.763% |
| 121a to String repair experiment | JaiScript VM 8 workers | padding | -0.523% | -2.583% to +3.187% | -29.490% to +2.694% | -0.912% to +0.763% |
| 121a to String repair experiment | JaiDOOM VM timedemo | normal | -5.553% | -6.002% to -4.291% | -0.045% to +0.322% | -1.196% to +0.732% |
| 121a to String repair experiment | JaiDOOM VM timedemo | padding | -5.517% | -5.884% to -5.272% | -0.045% to +0.322% | -1.196% to +0.732% |
| Stable repair experiment to String repair experiment | JaiScript VM serial | normal | +0.229% | -0.399% to +1.117% | -0.878% to +1.289% | -0.805% to -0.023% |
| Stable repair experiment to String repair experiment | JaiScript VM serial | padding | -0.312% | -0.974% to +0.461% | -0.878% to +1.289% | -0.805% to -0.023% |
| Stable repair experiment to String repair experiment | JaiScript VM 8 workers | normal | +0.949% | -1.518% to +3.784% | -3.855% to +2.441% | -0.912% to +0.763% |
| Stable repair experiment to String repair experiment | JaiScript VM 8 workers | padding | +0.264% | -1.869% to +3.108% | -3.855% to +2.441% | -0.912% to +0.763% |
| Stable repair experiment to String repair experiment | JaiDOOM VM timedemo | normal | -11.217% | -11.504% to -10.057% | -0.126% to +1.337% | -1.196% to +0.732% |
| Stable repair experiment to String repair experiment | JaiDOOM VM timedemo | padding | -11.362% | -12.161% to -10.901% | -0.126% to +1.337% | -1.196% to +0.732% |

Source identities:

| Label | Runtime base commit | Overlay |
|---|---|---|
|121a|`bace83ee5a1ad1697c7753340028bf561350b19d`|None|
|Stable repair experiment|`e5b8c64976c99f9d1864d480e4a59b7ec2510fef`|`always-env-guard.patch` and its guard-count test expectation|
|String repair experiment|`e542c0b6626d78c9dbe9c4231942388feb6776d1`|Same compiler patch and matching rebased test expectation|

Compiler patch SHA-256: `ed2e0b6be78f41388683fa6eb84a49f709014cdcb42892f7d528d976e92e74a2`. It keeps an explicit ordered snapshot for environment-name containers in two lowering paths, preserving a captured null and avoiding the later clearing of a reused temporary. The full corrective-stack comparison includes other correctness fixes since 121a; it does not isolate this patch's cost. The stable-to-string pair isolates the rebased string-cell row with the same repair on both sides.

Rivals: Squirrel 3.2 with `SQUSEDOUBLE` and `_SQ64`; Lua 5.4.8; Lua 5.5.1; LuaJIT 2.1 JIT at `24c20c94e7db195b640854619577441f9b4bc6be`, built with `msvcbuild.bat lua52compat static`. Lua embedding uses pinned sol2 3.3.0. These are pinned sources, not a claim about the latest upstream revisions.

CMake targets: MSVC 19.51.36252 x64, CMake/Ninja Release, `/O2 /DNDEBUG /Zi`, project `/Ob2 /Oi /Ot /GL`, static CRT; linker `/DEBUG:FULL /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF`. No profile instrumentation or active debugger hook. Padding adds 40 retained volatile statements to a cold help function never executed by these workloads. Exact commands, CMake caches and compile_commands.json are retained per source. The separately built LuaJIT JIT runtime uses its vendor script's /O2 /W3 /Zi and LUAJIT_ENABLE_LUA52COMPAT, without the CMake /GL option; its adapter uses the CMake flags above. Squirrel's _SQ64 is defined by its header on _WIN64.

Machine: Intel Core Ultra 9 285HX, Windows x64, 24 physical/logical cores (8 P + 16 E). P-core logical IDs `[0,1,10,11,12,13,22,23]`. Serial GLOOM and JaiDOOM use one P core; GLOOM eight workers uses eight P cores. One workload process at a time, no concurrent build/test/profile work. Competitors are polled every 0.25 seconds; samples above 1.0 background core equivalent or with a competing process are excluded and retried at the same position. Excluded runs remain in the raw record.

Order: three rounds. For each workload in a round, normal 121a/stable/string, then padded 121a/stable/string, repeated twice. Every extracted pair retains ABAB order. Each source/layout/configuration has six accepted timings. Scripts, WAD/demo inputs, binaries and experimental sources are frozen and hashed. The runner's repository HEAD identifies the infrastructure checkout; library sources and overlays are identified above and in build-inputs.json.

Independent gates before timing: stable experiment 3098 Debug tests per backend; string experiment 3100 per backend; 384 differential corpus outcomes per experiment; 160 focused Debug and 160 focused Release outcomes per experiment; 18 Release game-hash runs per experiment; 56 string-script and 16 native getter/quota checks per configuration on the string experiment. All pass. The changed test expectation concerns an explicit snapshot guard in disassembly, not a changed language result.

Timing verification: 377 processes, 78 conformance gates, 252 accepted timings, 47 excluded busy attempts. Every exit and hash passed. The independent audit reparsed every log, verified order/affinity/warm-up, recalculated medians and bands, and rehashed measured binaries, workload files, actual experimental sources and overlay patches.

GLOOM 300: frame `7fecc09815bb64e0`, state `3580805725`. GLOOM 600: frame `dfd969ed5a336dca`, state `1503537018`. JaiDOOM state `1746772898`. A nonconforming port would have no timing row; all listed ports conform.

Raw evidence: `out/astra/backlog-drain-20260911/final-comparison/` (`metadata.json`, `runs.json`, `summary.json`, `differences.json`, `audit.json`, `snapshot-derived.json`, `runner.py`, every process log). Commands and identities: `final-command.json`, `final-binaries/*/build-inputs.json`, `guard-experiment/experiment.json`, `string-experiment/experiment.json`. Earlier failures and repairs are documented in the backlog review. Busy gates and rejected timings do not enter the medians.

These numbers describe the identified experiments on these workloads. Small changes inside the padding or paired variation ranges receive no performance credit. A different final implementation needs its own gates and pair; this snapshot does not approve the two known defects in unmodified e5b8c649.

Interpretation: the repaired string row reduces JaiDOOM time in both layouts, beyond either layout's observed band. The complete stable corrective stack costs time on JaiDOOM; this run does not identify which corrective change accounts for that cost. With the string row included, JaiDOOM is faster than frozen 121a in both layouts. GLOOM changes between the JaiScript variants remain within the observed variation.

The frozen 121a eight-worker series contains a slow accepted normal-layout observation, producing the wide negative padding range. It remains in the data. The LuaJIT JIT padding range is also wide despite passing the busy-process screen. Those series support coarse snapshot comparisons, not small improvement claims; neither outlier was removed after inspecting performance. Serial JaiScript's medians are close to Squirrel's; both layouts and their observed bands are reported above. The eight-worker comparison uses eight cores against each rival's one.
