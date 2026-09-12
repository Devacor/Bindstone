# Frozen compiler comparison, 2026-09-11

GLOOM runs 600 measured ticks after 600 warm-up ticks and a reboot in the same runtime. JaiDOOM replays all 3,984 tics once to warm the engine and again for the measured main call. All timings use the VM backend. Every process passes its reference hashes.

Each variant has six accepted runs per layout across three interleaved rounds. Normal and padded binaries differ by 40 retained volatile statements in a cold help function. The padding band is the minimum and maximum paired padded/normal change across the six samples, not a confidence interval.

| Workload | P cores | Source | Normal ms/tick or ms/tic | Padded ms/tick or ms/tic | Padding band |
|---|---:|---|---:|---:|---|
| JaiScript VM serial | 1 | 112 | 1.206827 | 1.199782 | -0.768% to -0.264% |
| JaiScript VM serial | 1 | 113 | 1.207016 | 1.205744 | -0.489% to +0.321% |
| JaiScript VM serial | 1 | 114b | 1.216485 | 1.212412 | -1.025% to +0.451% |
| JaiScript VM serial | 1 | 115 | 1.230116 | 1.222167 | -1.095% to +0.115% |
| JaiScript VM serial | 1 | 121a | 1.196219 | 1.196717 | -1.306% to +1.465% |
| JaiScript VM 8 workers | 8 | 112 | 0.819673 | 0.824473 | -10.928% to +4.722% |
| JaiScript VM 8 workers | 8 | 113 | 0.821277 | 0.806849 | -7.042% to +0.960% |
| JaiScript VM 8 workers | 8 | 114b | 0.828663 | 0.818504 | -6.307% to +4.465% |
| JaiScript VM 8 workers | 8 | 115 | 0.822925 | 0.800880 | -7.181% to +1.444% |
| JaiScript VM 8 workers | 8 | 121a | 0.799645 | 0.795924 | -6.336% to +5.386% |
| JaiDOOM VM timedemo | 1 | 112 | 3.710969 | 3.713102 | -0.228% to +0.453% |
| JaiDOOM VM timedemo | 1 | 113 | 3.510417 | 3.511546 | -0.086% to +0.143% |
| JaiDOOM VM timedemo | 1 | 114b | 3.260291 | 3.263680 | -0.092% to +0.416% |
| JaiDOOM VM timedemo | 1 | 115 | 3.220005 | 3.213604 | -0.288% to +0.016% |
| JaiDOOM VM timedemo | 1 | 121a | 2.805723 | 2.807103 | -0.134% to +0.278% |

The paired increments below isolate the environment-cell change (112 to 113), counted-loop cells (114b to 115), and the cumulative reviewed source (112 to 121a). The change column is the ratio of medians. The observed paired range keeps all six accepted pairs, including outliers.

| Increment | Workload | Layout | Change | Observed paired range | Baseline padding band | Candidate padding band |
|---|---|---|---:|---|---|---|
| 112 to 113 | JaiScript VM serial | normal | +0.016% | -0.110% to +0.497% | -0.768% to -0.264% | -0.489% to +0.321% |
| 112 to 113 | JaiScript VM serial | padding | +0.497% | +0.103% to +0.653% | -0.768% to -0.264% | -0.489% to +0.321% |
| 112 to 113 | JaiScript VM 8 workers | normal | +0.196% | -12.415% to +1.999% | -10.928% to +4.722% | -7.042% to +0.960% |
| 112 to 113 | JaiScript VM 8 workers | padding | -2.138% | -2.961% to -0.052% | -10.928% to +4.722% | -7.042% to +0.960% |
| 112 to 113 | JaiDOOM VM timedemo | normal | -5.404% | -6.109% to -5.188% | -0.228% to +0.453% | -0.086% to +0.143% |
| 112 to 113 | JaiDOOM VM timedemo | padding | -5.428% | -5.853% to -5.286% | -0.228% to +0.453% | -0.086% to +0.143% |
| 114b to 115 | JaiScript VM serial | normal | +1.121% | +0.044% to +1.550% | -1.025% to +0.451% | -1.095% to +0.115% |
| 114b to 115 | JaiScript VM serial | padding | +0.805% | -0.276% to +1.382% | -1.025% to +0.451% | -1.095% to +0.115% |
| 114b to 115 | JaiScript VM 8 workers | normal | -0.692% | -4.932% to +3.879% | -6.307% to +4.465% | -7.181% to +1.444% |
| 114b to 115 | JaiScript VM 8 workers | padding | -2.153% | -5.097% to +1.318% | -6.307% to +4.465% | -7.181% to +1.444% |
| 114b to 115 | JaiDOOM VM timedemo | normal | -1.236% | -1.486% to -1.147% | -0.092% to +0.416% | -0.288% to +0.016% |
| 114b to 115 | JaiDOOM VM timedemo | padding | -1.534% | -1.803% to -1.386% | -0.092% to +0.416% | -0.288% to +0.016% |
| 112 to 121a | JaiScript VM serial | normal | -0.879% | -1.240% to +0.696% | -0.768% to -0.264% | -1.306% to +1.465% |
| 112 to 121a | JaiScript VM serial | padding | -0.256% | -0.894% to +0.936% | -0.768% to -0.264% | -1.306% to +1.465% |
| 112 to 121a | JaiScript VM 8 workers | normal | -2.443% | -12.569% to +5.082% | -10.928% to +4.722% | -6.336% to +5.386% |
| 112 to 121a | JaiScript VM 8 workers | padding | -3.463% | -8.061% to +5.602% | -10.928% to +4.722% | -6.336% to +5.386% |
| 112 to 121a | JaiDOOM VM timedemo | normal | -24.394% | -24.825% to -24.241% | -0.228% to +0.453% | -0.134% to +0.278% |
| 112 to 121a | JaiDOOM VM timedemo | padding | -24.400% | -24.897% to -24.272% | -0.228% to +0.453% | -0.134% to +0.278% |

Source identities:

| Label | Commit |
|---|---|
| 112 | `bc037edd7eb26e896ff15566c6941dfa04c66059` |
| 113 | `45cf6fdde275787e4a05377373527080d6c92018` |
| 114b | `cf9c69c6d656582bf6a49d2d1df3c1ffa2993918` |
| 115 | `efe7cc040a7a3cff18bfdaef70f52ed85c9291c3` |
| 121a | `bace83ee5a1ad1697c7753340028bf561350b19d` |

The five libraries and hosts were built independently from local Git archives under `Source/JaiScript/out/astra/backlog-drain-20260911/`. GLOOM and JaiDOOM script/host inputs are byte-identical across the five archived revisions. The common frozen workload tree is `tip/tree/Source/JaiScript/`. The repository HEAD recorded by the runner identifies the measurement infrastructure checkout; the library sources are the commits above. Exact runner SHA-256: `cb95d0ea94e2f3450c18e33dc51e0215ef97c10728c3119f8997dd061a5cf528`.

Build: MSVC 19.51.36252, x64, CMake/Ninja Release, `/O2 /DNDEBUG /Zi`, the project release options `/Ob2 /Oi /Ot /GL`, static CRT, `JAISCRIPT_ENABLE_DEBUGGER` and the project's `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` define, and link options `/DEBUG:FULL /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF`. No debugger hook or profiling build is used. Exact commands and per-translation-unit flags are retained in `release-build-results.json`, the build logs, each `CMakeCache.txt` and `compile_commands.json`.

Machine: Intel Core Ultra 9 285HX, Windows x64. Serial and timedemo processes use one P core; the eight-worker configuration uses eight P cores. Recorded P-core logical CPU IDs: `[0, 1, 10, 11, 12, 13, 22, 23]`. One workload process runs at a time. Competing build/test/workload processes are disallowed, polled every 0.25 seconds. Samples above 1.0 background core equivalent are excluded and retried in the same position.

Order: for each round and workload, normal variants in the order 112, 113, 114b, 115, 121a, then padded variants in that order; repeat that sequence twice. Every extracted two-variant series retains ABAB order.

Verification: 247 processes, 50 conformance gates, 180 accepted timings and 17 excluded busy attempts. Every exit and hash passed. The independent audit reparsed every raw log, verified exact order/affinity/warm-up, recalculated medians and bands, and rehashed measured executables and workload files. The 300-tick GLOOM gate is frame `7fecc09815bb64e0`, state `3580805725`; the 600-tick gate is frame `dfd969ed5a336dca`, state `1503537018`. JaiDOOM state is `1746772898`.

Raw evidence: `out/astra/backlog-drain-20260911/historical-comparison/` (`metadata.json`, `runs.json`, `summary.json`, `differences.json`, `audit.json`, `runner.py`, every process log). Source manifests, archive identities, build commands, and `workload-identity.json` are in its parent directory.

This is a frozen-source measurement. Revision 121a has the independently reproduced evaluation-order and declaration-stamp defects described in the backlog review. Its benchmark workloads conform. The corrected-tip comparison and rival refresh are reported separately.

Environment cells reduce JaiDOOM time in both layouts. Counted-loop cells also reduce JaiDOOM time, with a small GLOOM serial increase close to the layout-control range. The cumulative serial GLOOM change remains inside its layout range. The worker ranges are wide; the small worker deltas receive no performance credit. These measurements do not establish that a small change comes from instruction cost rather than code placement. Source correctness is gated separately.
