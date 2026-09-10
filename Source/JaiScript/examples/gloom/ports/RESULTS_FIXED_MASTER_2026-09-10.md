# Fixed-master whole-program comparison, 2026-09-10

The fixed master `ed7c3bd0` completes the full comparison against `d6c9959b`. Serial GLOOM is 6.81% faster in the normal layout and 6.66% faster in the padding layout. JaiDOOM improves 6.78% and 8.73%. Serial GLOOM remains above the 1.0 ms/tick target.

This is the cumulative change between the two commits. It includes the earlier temporary-reuse work as well as the later closed-loop and element-read work; the full difference cannot be assigned exclusively to rows 98 through 104.

## Warmed results

Each median contains six accepted processes: two interleaved baseline/candidate pairs in each of three rounds. Normal and padding executables use the same source and compiler options. Padding adds 40 retained volatile statements to a cold help function that is never entered by the workload.

The padding band is the minimum and maximum percentage change from normal to padding for the six matched samples of that build and workload. It measures this placement perturbation; it is not a confidence interval or a bound on every possible binary layout.

| Workload | Build | P cores | Normal | Padding | Padding band |
|---|---|---:|---:|---:|---:|
| GLOOM VM serial, ms/tick | d6c9959b | 1 | 1.371312 | 1.364506 | -0.91% to +0.30% |
| GLOOM VM serial, ms/tick | ed7c3bd0 | 1 | 1.277978 | 1.273685 | -0.56% to +0.50% |
| GLOOM VM 8 workers, ms/tick | d6c9959b | 8 | 0.961172 | 0.968661 | -6.30% to +4.98% |
| GLOOM VM 8 workers, ms/tick | ed7c3bd0 | 8 | 0.874139 | 0.860097 | -8.18% to -0.40% |
| JaiDOOM VM, ms/tic | d6c9959b | 1 | 4.691516 | 4.729167 | -0.21% to +0.98% |
| JaiDOOM VM, ms/tic | ed7c3bd0 | 1 | 4.373619 | 4.316391 | -1.77% to -1.21% |

## Paired changes

Negative values mean lower elapsed time. The median change is the ratio of the two medians; the paired band is the range of the six individual candidate/reference changes in that layout.

| Workload | Layout | Change of medians | Paired change band |
|---|---|---:|---:|
| GLOOM VM serial, ms/tick | normal | -6.81% | -6.91% to -6.42% |
| GLOOM VM serial, ms/tick | padding | -6.66% | -7.22% to -5.88% |
| GLOOM VM 8 workers, ms/tick | normal | -9.05% | -13.98% to +1.85% |
| GLOOM VM 8 workers, ms/tick | padding | -11.21% | -13.27% to -10.37% |
| JaiDOOM VM, ms/tic | normal | -6.78% | -7.64% to -6.56% |
| JaiDOOM VM, ms/tic | padding | -8.73% | -8.90% to -8.54% |

Every serial and timedemo pair improves in both layouts. The eight-worker normal layout has five improving pairs and one regression; that accepted outlier remains in the table. Its padded layout improves in all six pairs. The worker medians improve, but the wider bands do not support a claim that every worker run gains the same amount.

## Method and source identity

The reserved window ran from 2026-09-09 23:43 to 2026-09-10 00:10 America/Chicago. Process screening initially waited for tests and reusable MSBuild workers to finish. No timed workload was launched while those competitors were present.

Hardware: Intel Core Ultra 9 285HX, 24 physical/logical cores, eight P cores. Serial GLOOM and JaiDOOM use logical CPU 0. The eight-worker configuration is constrained to P-core CPUs `0,1,10,11,12,13,22,23`, including the main thread and worker pool. One workload process runs at a time. The runner polls competing processes every 0.25 seconds and rejects timing attempts above 1.0 background core-equivalent.

GLOOM runs 600 warm-up ticks, reboots its state in the same runtime, then measures 600 ticks at seed 666. JaiDOOM runs `p10_timedemo.jai` at 120x78: a complete warm-up followed by one measured `main()` replay in the same engine. Each replay contains 3984 tics; the metric is its reported elapsed integer milliseconds divided by 3984. Each workload/round runs normal baseline/candidate, padding baseline/candidate, then repeats that order.

Compiler: MSVC 19.51.36252, toolset 14.51.36231, x64. CMake 4.3.2 and Ninja 1.13.2. Release uses `/O2 /Ob2 /Oi /Ot /GL /Zi /MT /DNDEBUG`, with `/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`. Stack protection remains at the default; there is no `/GS-`, VM profiling or experimental pilot define. Debugger support is compiled in and disabled for execution.

Reference: `d6c9959babe200d29b29b60939d23a35ab284b91`. Candidate: `ed7c3bd0bd67f9855c67a8711a73633cd196ef3b`. The candidate was prepared from the archived `4bd53021` source plus Fable's exact row 104 patch, SHA-256 `0564e0e076913532f5c7b16e3ff1561a190fc6f3f9684dfd4e2eb791ac848848`. Before timing, 405 source, build and script blobs were checked against the landed candidate, accounting only for Git's checkout line-ending conversion. Fifty workload and host/build inputs were byte-identical to the reference. The ignored WAD was copied with verified SHA-256 `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.

The earlier `4bd53021` comparison stopped on a compiler access violation. The [stopped-audit report](RESULTS_ROW102_ABORTED_2026-09-09.md) preserves that failure and its exact-symbol attribution. This window starts a new complete protocol on the corrected build; no failed row was removed to resume the old window.

## Verification and limits

Astra ran 96 workload processes: 20 conformance gates, 72 accepted timings and four busy-machine exclusions. Every process exited zero and matched its required hashes. GLOOM 300-tick gates held frame `7fecc09815bb64e0` and state `3580805725`; 600-tick runs held `dfd969ed5a336dca` and `1503537018`. JaiDOOM warm and measured replays held `1746772898`.

The independent audit re-parsed the metrics from logs, verified the exact accepted interleaving, recomputed screening verdicts, all medians and both kinds of bands, and checked that binaries, workloads and build-input manifests were unchanged. Four freshly linked minimal compiler controls also returned 13 on both backends and both layouts.

Fable ran the candidate's full gates: Debug 3054 tests per backend, XTHREAD 119 per backend, Release 3082 per backend, fuzz 3000 seeds plus 1500 with proofs disabled, the workload hashes, two clean AddressSanitizer timedemos with enlarged allocation guard zones, and the minimal sanitizer regression. His isolated Release client passed 76 asset checks and the login-shot gate. These are Fable's gate results; Astra independently ran the comparison, source audit and minimal controls described above.

The eight-worker normal outlier limits claims about uniform scaling. The two JaiDOOM layouts differ consistently, so both are reported. Counts and instrumented profiles are not treated as native CPU shares. This report measures the named frozen candidate; later commits require their own comparisons.

## Raw evidence

Paths are relative to `Source/JaiScript/out/astra/row104-audit-20260909/`:

- `comparison/{metadata,runs,summary,differences,audit,paired-analysis}.json`, individual logs and `comparison/runner.py`.
- `landed-source-verification.json`, `verify-landed.py`, `workload-equivalence.json`, `build-flag-audit.json`.
- `candidate/source-inputs.json`, `candidate/release/build-inputs.json`, configure/build logs and `row104.patch`.
- `minimal-controls.json`, `audit-comparison.py`, `audit-output.log`, `measure.py` and `comparison-launch.json`.

The unchanged reference binaries and manifest are under `../row102-audit-20260909/baseline/release/`. The earlier failed comparison and diagnostic captures remain under that sibling directory.
