# GLOOM and JaiDOOM: final committed tips, 2026-09-12

This comparison uses frozen 121a and the final committed stable/string tips. The candidates contain no runtime overlays. The earlier experimental snapshot remains a separate measurement.

GLOOM warms for 600 ticks, reboots the game inside the runtime, then measures 600 ticks. JaiDOOM warms with one 3984-tic timedemo and measures a second main() invocation in the same engine. GLOOM reports ms/tick; JaiDOOM reports integer elapsed milliseconds divided by 3984, in ms/tic. Values are medians. The padding band is the observed minimum/maximum paired change from normal to padding, including residual run variation; it is not a confidence interval.

| Workload | Source | P cores | Normal ms/tick or ms/tic | Padded ms/tick or ms/tic | Padding band |
|---|---|---:|---:|---:|---|
| JaiScript VM serial | 121a bace83ee | 1 | 1.206337 | 1.203967 | -1.219% to +0.214% |
| JaiScript VM serial | Stable 7fc75044 | 1 | 1.223038 | 1.218309 | -1.069% to +3.814% |
| JaiScript VM serial | Strings ee2af60c | 1 | 1.214854 | 1.217646 | -0.562% to +0.886% |
| JaiScript VM 8 workers | 121a bace83ee | 8 | 0.809738 | 0.803957 | -1.587% to +1.453% |
| JaiScript VM 8 workers | Stable 7fc75044 | 8 | 0.812380 | 0.821011 | -1.290% to +31.091% |
| JaiScript VM 8 workers | Strings ee2af60c | 8 | 0.819337 | 0.811549 | -3.533% to -0.165% |
| JaiDOOM VM timedemo | 121a bace83ee | 1 | 2.815512 | 2.816516 | -0.480% to +0.465% |
| JaiDOOM VM timedemo | Stable 7fc75044 | 1 | 2.885919 | 2.877510 | -0.574% to -0.035% |
| JaiDOOM VM timedemo | Strings ee2af60c | 1 | 2.552711 | 2.561370 | +0.059% to +0.638% |

Paired changes:

| Sources | Workload | Layout | Median change | Paired range | Before padding band | After padding band |
|---|---|---|---:|---|---|---|
| 121a bace83ee to Stable 7fc75044 | JaiScript VM serial | normal | +1.384% | +0.398% to +1.806% | -1.219% to +0.214% | -1.069% to +3.814% |
| 121a bace83ee to Stable 7fc75044 | JaiScript VM serial | padding | +1.191% | +1.003% to +5.699% | -1.219% to +0.214% | -1.069% to +3.814% |
| 121a bace83ee to Stable 7fc75044 | JaiScript VM 8 workers | normal | +0.326% | -1.686% to +4.112% | -1.587% to +1.453% | -1.290% to +31.091% |
| 121a bace83ee to Stable 7fc75044 | JaiScript VM 8 workers | padding | +2.121% | -1.247% to +30.959% | -1.587% to +1.453% | -1.290% to +31.091% |
| 121a bace83ee to Stable 7fc75044 | JaiDOOM VM timedemo | normal | +2.501% | +2.206% to +2.828% | -0.480% to +0.465% | -0.574% to -0.035% |
| 121a bace83ee to Stable 7fc75044 | JaiDOOM VM timedemo | padding | +2.166% | +1.930% to +2.418% | -0.480% to +0.465% | -0.574% to -0.035% |
| 121a bace83ee to Strings ee2af60c | JaiScript VM serial | normal | +0.706% | +0.091% to +1.533% | -1.219% to +0.214% | -0.562% to +0.886% |
| 121a bace83ee to Strings ee2af60c | JaiScript VM serial | padding | +1.136% | +0.999% to +1.946% | -1.219% to +0.214% | -0.562% to +0.886% |
| 121a bace83ee to Strings ee2af60c | JaiScript VM 8 workers | normal | +1.185% | -2.584% to +4.592% | -1.587% to +1.453% | -3.533% to -0.165% |
| 121a bace83ee to Strings ee2af60c | JaiScript VM 8 workers | padding | +0.944% | -3.391% to +2.924% | -1.587% to +1.453% | -3.533% to -0.165% |
| 121a bace83ee to Strings ee2af60c | JaiDOOM VM timedemo | normal | -9.334% | -10.014% to -9.093% | -0.480% to +0.465% | +0.059% to +0.638% |
| 121a bace83ee to Strings ee2af60c | JaiDOOM VM timedemo | padding | -9.059% | -9.231% to -8.645% | -0.480% to +0.465% | +0.059% to +0.638% |
| Stable 7fc75044 to Strings ee2af60c | JaiScript VM serial | normal | -0.669% | -1.566% to +0.067% | -1.069% to +3.814% | -0.562% to +0.886% |
| Stable 7fc75044 to Strings ee2af60c | JaiScript VM serial | padding | -0.055% | -4.252% to +0.660% | -1.069% to +3.814% | -0.562% to +0.886% |
| Stable 7fc75044 to Strings ee2af60c | JaiScript VM 8 workers | normal | +0.856% | -0.913% to +2.771% | -1.290% to +31.091% | -3.533% to -0.165% |
| Stable 7fc75044 to Strings ee2af60c | JaiScript VM 8 workers | padding | -1.152% | -26.230% to +0.803% | -1.290% to +31.091% | -3.533% to -0.165% |
| Stable 7fc75044 to Strings ee2af60c | JaiDOOM VM timedemo | normal | -11.546% | -11.956% to -11.288% | -0.574% to -0.035% | +0.059% to +0.638% |
| Stable 7fc75044 to Strings ee2af60c | JaiDOOM VM timedemo | padding | -10.987% | -11.203% to -10.449% | -0.574% to -0.035% | +0.059% to +0.638% |

Sources:

- 121a: bace83ee5a1ad1697c7753340028bf561350b19d, the unchanged previously frozen binaries.
- Stable: 7fc7504443885bccb24b7825d4a08671a038107b.
- Strings: ee2af60c6052094fcea6692116e0b0f405ebd340.

The two new libraries and hosts were built from independent local Git archives under out/astra/final-tips-20260912/. Only standalone verification-host CMake declarations were added to those copies. The 49 GLOOM/JaiDOOM script, host, demo and WAD inputs are byte-identical to frozen 121a. The common measured workload tree remains out/astra/backlog-drain-20260911/tip/tree/Source/JaiScript/. The runner's recorded repository HEAD identifies measurement infrastructure; the library sources are the commits above.

Build: MSVC 19.51.36252 x64, CMake/Ninja Release, /O2 /DNDEBUG /Zi, project /Ob2 /Oi /Ot /GL, static CRT; linker /DEBUG:FULL /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF. The project's debugger support is compiled in with no active debugger hook; no profile instrumentation is enabled. Padding adds 40 retained volatile statements to a cold help function, which the workloads do not execute. Every build retains its CMakeCache.txt, compile_commands.json, commands and logs.

Machine: Intel Core Ultra 9 285HX, Windows x64, 24 physical/logical cores (8P+16E). P-core logical IDs [0,1,10,11,12,13,22,23]. Serial and timedemo use one P core; eight workers use eight P cores. One workload process runs at a time. Competing builds/tests/profiles/workloads are polled every 0.25 seconds. Samples above 1.0 background core equivalent or with a competing process are excluded and retried at the same position. The threshold is unchanged from the previous snapshot.

Order: three interleaved rounds. For each workload in a round, normal baseline/stable/strings, padded baseline/stable/strings, repeated twice. Each extracted two-variant sequence retains ABAB order. Each source/configuration/layout has six accepted timings.

Independent gates: full Debug suites on both backends, 3099 tests per backend at the stable tip and 3101 at the string tip; 384 semantic-corpus outcomes per tip; 160 original focused outcomes plus 32 new guard-path outcomes per configuration (Debug and Release) per tip; 18 Release game-hash runs per tip. String-specific gates add 56 script outcomes and 16 native getter/quota outcomes per configuration. All pass. The new guard probes' listings prove guarded cell loops and calls are exercised. Fable separately reports its full Release suites, XTHREAD and three fuzz chains green; those were read from its handoff, not independently rerun here.

The stable fix removes the null-sentinel protocol and blanket clearing of historical guard slots. It uses an explicit saved value for environment containers. Its block guard compares the saved array with the site's current node before resolving the source; entry refusal and post-call site revalidation remain in place. The original null-getter and temporary-reuse failures are independently fixed on these commits. The final string-row runtime delta matches the previously reviewed row ignoring diff offsets.

Timing audit: 145 processes, 30 conformance gates, 108 accepted timings, 7 excluded busy attempts. Every exit and hash passes. The independent audit reparses every log, checks exact order, warm-up and affinity, recomputes medians and bands, and rehashes measured binaries, workloads and actual candidate source files.

GLOOM 300: frame 7fecc09815bb64e0, state 3580805725. GLOOM 600: frame dfd969ed5a336dca, state 1503537018. JaiDOOM state 1746772898.

Raw evidence: out/astra/final-tips-20260912/comparison/ (metadata.json, runs.json, summary.json, differences.json, audit.json, runner.py and every process log). Source/build identities: identities.json, binaries/*/build-inputs.json, stable/source-inputs.json and strings/source-inputs.json. Correctness results: each tip's debug-suites.json, pass-debug/, focused-debug/, focused-release/, guard-paths-debug/, guard-paths-release/, release-checks/; strings/strings-debug/ and strings/strings-release/. Source review: REVIEW.md and string-rebase-review.json. Reported Fable gates: fable-BASE.txt and fable-reported-status-tail.txt.

The earlier overlay used different runtime source and ran in a different window. Comparing its normalized baseline delta with this one describes the combined final repair; it does not isolate the temp-first guard check. Fable's increased general-form operand-snapshot count is operation-count evidence, not a measured CPU-time attribution. Small changes within the paired or padding ranges receive no performance credit.

Interpretation: the stable correctness repair has a clear remaining JaiDOOM cost relative to 121a in both layouts, but its normalized penalty is smaller than in the earlier overlay snapshot. The string row reduces JaiDOOM time on that repaired base in both layouts, beyond their observed bands, and the final string tip is faster than 121a on this workload.

GLOOM serial moves slightly upward relative to 121a in every paired comparison for both final tips. The size is close to the observed layout variation, so it is reported as a small consistent increase rather than a precise instruction-cost attribution. The string row's serial GLOOM difference against the stable tip is within variation.

The stable worker padding series contains an accepted slow observation, giving a +31.091% padding extreme. It remains in the medians and bands. Worker differences are inconclusive at this scale; no small gain or regression is assigned to them. The exact paired observations and screening records are in noise-review.json.

Next measurement recommendation for Fable: continue the explicit snapshot-state work, then test guard elision as a separate increment. Preserve the three permanent regressions and the definition-based fallback rules. This pair establishes remaining whole-program cost; it does not establish how much belongs to block guards versus general-form snapshots, and the operation census alone is not a time profile.
