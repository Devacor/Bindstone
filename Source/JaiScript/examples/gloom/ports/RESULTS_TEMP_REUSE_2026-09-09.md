# Typed temporary reuse: paired whole-program result, 2026-09-09

Frozen `9dced7bf` and `68aeae4d` isolate the typed temporary reuse guard, borrowed
type-info accessor and engine-identity check. Serial GLOOM improves in both
layouts, as does JaiDOOM. The eight-worker change remains inside its observed
variation and receives no gain credit.

## Measured values

Each padding band is the range of six paired padding/normal deltas for that
configuration. It is not a confidence interval. GLOOM uses ms/tick; JaiDOOM uses
ms/tic.

| Workload | P cores | Build | Normal | Padding | Padding band |
|---|---:|---|---:|---:|---:|
| JaiScript VM serial | 1 | 9dced7bf | 1.370639 | 1.374851 | -0.83% to +1.42% |
| JaiScript VM serial | 1 | 68aeae4d | 1.344663 | 1.352957 | +0.01% to +1.42% |
| JaiScript VM 8 workers | 8 | 9dced7bf | 0.955934 | 0.962423 | +0.02% to +1.77% |
| JaiScript VM 8 workers | 8 | 68aeae4d | 0.947254 | 0.954428 | -2.35% to +4.11% |
| JaiDOOM VM timedemo | 1 | 9dced7bf | 4.748494 | 4.698921 | -1.22% to +0.04% |
| JaiDOOM VM timedemo | 1 | 68aeae4d | 4.630522 | 4.589357 | -1.09% to -0.22% |

| Workload | Layout | Candidate delta | Paired candidate/baseline range |
|---|---|---:|---:|
| JaiScript VM serial | normal | -1.90% | -2.28% to -1.45% |
| JaiScript VM serial | padding | -1.59% | -2.81% to -0.06% |
| JaiScript VM 8 workers | normal | -0.91% | -3.11% to +0.32% |
| JaiScript VM 8 workers | padding | -0.83% | -4.14% to +2.31% |
| JaiDOOM VM timedemo | normal | -2.48% | -2.61% to -2.34% |
| JaiDOOM VM timedemo | padding | -2.33% | -3.40% to -1.82% |

The normal serial pair removes 25.976 microseconds per GLOOM tick. All six
serial paired deltas are negative in each layout; one padded pair is near zero.
JaiDOOM improves consistently across both layouts. Worker paired ranges cross
zero, and the candidate padding band spans -2.35% to +4.11%.

These figures credit the isolated revision pair. The earlier 1.400805 ms/tick
snapshot was a different timing window; subtracting this candidate from that
older absolute value would overstate the measured improvement.

## Protocol and build

- Core Ultra 9 285HX, 24 physical/logical CPUs. Serial runs use CPU 0; eight-worker
  runs use CPUs 0, 1, 10, 11, 12, 13, 22 and 23.
- Three interleaved rounds, two baseline/candidate comparisons per layout per
  round, one workload process at a time. Every configuration has six accepted
  measurements per layout. Normal and 40-line cold-help padding builds interleave.
- GLOOM: 600 warm-up ticks, reboot in the same runtime, then 600 measured ticks;
  seed 666, quad-pixel host. The 300-tick gate is frame 7fecc09815bb64e0, state
  3580805725. The 600-tick result is frame dfd969ed5a336dca, state 1503537018.
- JaiDOOM: p10_timedemo.jai, 120x78, complete initial warm-up replay followed by
  the measured 3984-tic replay. Both produce state 1746772898.
- MSVC 19.51.36252, C++20, static runtime. Release flags include /O2 /Ob2 /Oi /Ot
  /GL /Zi /MT /DNDEBUG; link uses /LTCG /OPT:REF /OPT:ICF /DEBUG:FULL
  /INCREMENTAL:NO. Default /GS remains enabled. VM profiling and ETW marker
  instrumentation are disabled. The standard debugger-support define and legacy
  environment-cycle define are present; the latter has no active code in these
  sources.
- Source trees were frozen with git archive under out/astra. Both builds use
  identical frozen scripts and WAD inputs. The only source-level runtime change
  between revisions is the temporary landing mechanism; profiling-counter fields
  added by the change are compiled out of these builds.
- The recorded background allowance is 1.0 core, with competing build/test/
  benchmark processes screened every 250 ms. This is a desktop allowance, not
  a claim of zero background activity. Fable held builds during the window.

## Verification and limits

Verified by Astra running: all 95 processes exit successfully and hash-match;
72 accepted timings, 20 conformance gates, three excluded busy timings. The
audit reparses results, recomputes screening and medians, verifies the exact
accepted ABAB sequence and checks unchanged binary, workload and build manifests.

Read/source-audited: scalar constructors install the canonical type while the
old temp guard required no type. The new guard accepts the same-engine canonical
scalar. Fable reports 4,222,280 reuse hits and 330,941 typed-temp constructions
per 600 ticks after the fix, with other landing categories unchanged. These are
counts, not estimates of allocation or CPU time.

Correctness gates run by Fable on 68aeae4d: Debug 3011 tests on each backend,
XTHREAD 119 on each, Release 2910 on each, fuzz 3000 plus 1500 with proofs
disabled with no new divergences, and both whole-program hashes. These owner
gate results are distinct from Astra's independent benchmark runs.

No worker gain is claimed. Instrumented opcode-cycle charges are not used as
wall-time costs. Fresh native attribution remains unavailable: WPR rejected the
current token and the Windows elevation request was canceled. This does not
affect the uninstrumented paired evidence above.

Raw evidence relative to Source/JaiScript:

- out/astra/temp-reuse-20260909/comparison/{audit,metadata,runs,summary,differences}.json
- out/astra/temp-reuse-20260909/comparison/*.log and recorded runner.py
- out/astra/temp-reuse-20260909/{baseline,candidate}/source-inputs.json
- out/astra/temp-reuse-20260909/{baseline,candidate}/release/compile_commands.json
- out/astra/temp-reuse-20260909/{baseline,candidate}/release/build-inputs.json
- out/astra/temp-reuse-20260909/audit-report.log, measure.py and comparison-launch.json
