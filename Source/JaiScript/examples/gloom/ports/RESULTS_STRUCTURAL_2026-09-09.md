# Structural pilot matrix: final measurements, 2026-09-09

The serial target remains open. The combined pilot measures 1.308406 ms/tick
normally and 1.308702 padded, with a padding band of -0.14% to +0.34%. Its net
gain over the clean reference is 1.41% and 1.48%. None of the raw-cell, clustered
or combined pilots reaches the four-percent bar for added complexity.

The experiments are measured, hash-correct implementations. The results do not
justify enabling them as performance wins. They also do not establish a ceiling
for a broader typed-region design: these pilots retain kind checks, boxed slots
and an inner instruction selector, and optimize a bounded loop family.

## Configurations

| Label | Source | Purpose |
|---|---|---|
| Clean reference | e85a793b | Temporary and store reuse, before move reuse and the pilot handler split |
| Pre-inline stock | 7fd299f4 + frozen D patch a4dee388 | Both switches off; isolates the subsequent inlining change |
| A | 2ff8c2c1 | Final common source, both switches off |
| B | 2ff8c2c1 | JAISCRIPT_PILOT_CELLS |
| C | 2ff8c2c1 | JAISCRIPT_PILOT_CLUSTER |
| D | 2ff8c2c1 | Both switches |

All four factorial configurations use byte-identical source. The build flags
are audited independently. Forty-five workload files match the clean reference.
No GLOOM or JaiDOOM source changes are part of the comparison.

## Medians and padding bands

GLOOM is ms/tick; JaiDOOM is ms/tic. Each band is the observed range of six
paired padding/normal deltas, not a confidence interval.

| Workload | P cores | Configuration | Normal | Padding | Padding band |
|---|---:|---|---:|---:|---:|
| JaiScript VM serial | 1 | Clean reference e85a793b | 1.327113 | 1.328425 | -0.28% to +0.33% |
| JaiScript VM serial | 1 | Pre-inline stock | 1.350115 | 1.347256 | -0.98% to +2.45% |
| JaiScript VM serial | 1 | A: final stock | 1.329792 | 1.320067 | -0.87% to +6.89% |
| JaiScript VM serial | 1 | B: raw cells | 1.320146 | 1.325178 | -0.10% to +1.61% |
| JaiScript VM serial | 1 | C: clustering | 1.311614 | 1.308455 | -1.90% to +0.29% |
| JaiScript VM serial | 1 | D: cells + clustering | 1.308406 | 1.308702 | -0.14% to +0.34% |
| JaiScript VM 8 workers | 8 | Clean reference e85a793b | 0.965902 | 0.945232 | -5.02% to +1.99% |
| JaiScript VM 8 workers | 8 | Pre-inline stock | 0.962490 | 0.975956 | -2.63% to +8.14% |
| JaiScript VM 8 workers | 8 | A: final stock | 0.958070 | 0.963305 | -3.94% to +12.40% |
| JaiScript VM 8 workers | 8 | B: raw cells | 0.935556 | 0.963502 | -14.65% to +7.95% |
| JaiScript VM 8 workers | 8 | C: clustering | 0.948820 | 0.945246 | -7.06% to +7.22% |
| JaiScript VM 8 workers | 8 | D: cells + clustering | 0.938679 | 0.943411 | -8.94% to +5.65% |
| JaiDOOM VM timedemo | 1 | Clean reference e85a793b | 4.616968 | 4.544679 | -2.35% to -1.10% |
| JaiDOOM VM timedemo | 1 | Pre-inline stock | 4.619729 | 4.583333 | -0.86% to -0.54% |
| JaiDOOM VM timedemo | 1 | A: final stock | 4.591993 | 4.567395 | -0.86% to -0.12% |
| JaiDOOM VM timedemo | 1 | B: raw cells | 4.547691 | 4.538027 | -0.32% to -0.02% |
| JaiDOOM VM timedemo | 1 | C: clustering | 4.606175 | 4.569654 | -1.13% to -0.47% |
| JaiDOOM VM timedemo | 1 | D: cells + clustering | 4.571034 | 4.565889 | -0.25% to +0.11% |

## Mechanism comparisons

| Change | GLOOM serial delta, normal / padded | Candidate serial padding band | JaiDOOM delta, normal / padded | Candidate JaiDOOM padding band |
|---|---:|---:|---:|---:|
| Inlining: pre-stock to A | -1.51% / -2.02% | -0.87% to +6.89% | -0.60% / -0.35% | -0.86% to -0.12% |
| Raw cells: A to B | -0.73% / +0.39% | -0.10% to +1.61% | -0.96% / -0.64% | -0.32% to -0.02% |
| Clustering: A to C | -1.37% / -0.88% | -1.90% to +0.29% | +0.31% / +0.05% | -1.13% to -0.47% |
| Combined: A to D | -1.61% / -0.86% | -0.14% to +0.34% | -0.46% / -0.03% | -0.25% to +0.11% |
| Net: clean reference to D | -1.41% / -1.48% | -0.14% to +0.34% | -0.99% / +0.47% | -0.25% to +0.11% |

Raw cells alone reverse sign between serial layouts. Clustering and the
combination have small serial gains, but remain below the bar. B has a small
JaiDOOM gain in both layouts; D does not preserve all of it. The net combined
JaiDOOM change changes sign between layouts. Worker bands are broad, so no
worker improvement is credited.

The inlining change improves the pre-inline stock comparison, particularly the
normal layout. The residual against the clean reference is small and changes
sign by layout. This supports treating it as an infrastructure fix, not claiming
bit-identical recovery or another large language-speed gain.

An accepted final-stock serial padding outlier widens that band to +6.89%.
It remains in the data. Small deltas involving that series, and worker deltas
with bands reaching roughly fifteen percent, are not precise performance wins.

## Interaction

For each matched run, the interaction is D - B - C + A in absolute microseconds.
A negative value means the combination saves more than adding the two separate
absolute savings. This is an experiment-level interaction, not a purified
hardware cost.

| Workload | Layout | Median interaction, us | Matched interaction range, us |
|---|---|---:|---:|
| JaiScript VM serial | normal | +6.547 | -13.884 to +14.080 |
| JaiScript VM serial | padding | -5.972 | -17.072 to +99.602 |
| JaiScript VM 8 workers | normal | -6.101 | -168.896 to +95.823 |
| JaiScript VM 8 workers | padding | -17.180 | -120.814 to +85.297 |
| JaiDOOM VM timedemo | normal | +10.291 | -1.506 to +25.602 |
| JaiDOOM VM timedemo | padding | +22.716 | +8.785 to +60.994 |

The serial interaction ranges cross zero, and their medians change sign with
layout. There is no stable extra serial benefit to credit to combining these
implementations. The measured combined saving over final stock is 21.387 us
normally and 11.365 us padded; the clean-reference savings are 18.707 and
19.723 us. These are far short of the original roughly 401-us target budget.

## What the pilots actually do

C keeps the boxed values and per-access guards. It runs a closed typed loop
inside a small inner interpreter after the first ordinary iteration. It still
selects inner steps; one outer entry is not one operation per iteration.

B copies the loop into raw-cell instructions dispatched through the ordinary
VM. Its entry guards, per-read kind checks, exact-point miss flushes, budget
flushes and ordinary fallback remain. D runs those same cell handlers inside
the inner interpreter. Fiber frames use the general path in this bounded pilot.

Activation captures cover 600 warmed GLOOM ticks: 59,200 entries, 2,058,559
cell iterations after the first ordinary iterations, 355,200 unboxes, zero
misses and 236,800 in-place flushes. No full boxed replacements occur in those
flushes. B and D have the same counts.

The resident raw area is seven cells, 63 bytes of payload plus kind capacity
on one of eight allocated records. Existing boxed slots remain allocated.
The captured profiling build reports sizeof(call_record)=488, sizeof(frame)=128,
sizeof(cell_state)=64 and sizeof(script_value)=32. These are profiling-build
sizes; they are not a claim that the entire frame shrank from 32 to 8 bytes.
Allocation-during-measurement counters were zero because warm-up allocated
the retained area; the resident counters supply the footprint.

## Method and verification

- Core Ultra 9 285HX, 24 physical/logical CPUs. Serial affinity CPU 0; workers
  use CPUs 0, 1, 10, 11, 12, 13, 22 and 23.
- Three interleaved rounds, two normal/padded sweeps per round. Every build has
  six accepted measurements per layout/workload. All 45 pair projections across
  the three workloads retain ABAB order. One workload process runs at a time.
- GLOOM: 600 warm-up ticks, reboot in the same runtime, 600 measured ticks, seed
  666, quad output. 300-tick gate: frame 7fecc09815bb64e0, state 3580805725;
  600-tick result: frame dfd969ed5a336dca, state 1503537018.
- JaiDOOM: p10_timedemo.jai at 120x78, full warm-up and measured 3984-tic replay;
  both state hashes 1746772898. Elapsed integer milliseconds / tics is the metric.
- MSVC 19.51.36252, C++20, /O2 /Ob2 /Oi /Ot /GL /Zi /MT /DNDEBUG; link
  /LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO. Default /GS. No VM
  profiling or ETW marker instrumentation in measured binaries. Standard debugger
  support is present; the legacy environment-cycle define is inert.
- Forty never-executed volatile statements in cold help functions provide each
  padding control. The recorded background allowance is 1.0 core; competing
  builds/tests/benchmarks are screened. Busy attempts retry their original slot.
  Accepted outliers are retained.

Verified by Astra running: 288 processes, every exit and hash correct; 216
accepted timings, 60 conformance gates and 12 excluded busy attempts. The audit
reparses outputs, validates screening and exact accepted order, recomputes
medians/bands, checks immutable binary/workload/build identities and computes
matched factorial interactions. Unlike the earlier C window, this final window
has no checkpoint interruption.

Read/source-audited: the common-source switches, guard and flush contracts,
compiler selection, raw-area lifetime, original-index fallback and inlining
change. Fable ran final-source Debug 3037 and fully reconfigured Release 3065
tests per backend, fuzz 3000 plus 1500 with proofs disabled, and both workload
hashes. B and D each passed their activated-config Debug/Release suites, fuzz
and hashes before the final annotation change. The final matrix independently
hash-gates every activated configuration.

Native ETW attribution was not captured: WPR lacked the required privilege and
the Windows elevation request was canceled. No CPU-time ledger is inferred
from the opcode counts or instrumented cycle charges.

## Recommendation under the agreed rule

Temporary reuse has a separately verified whole-program gain. The store
simplification stays on its code merits without speed credit; move reuse was
removed. The inlining fix has its isolated control above. Do not enable B, C
or D as performance wins from this evidence: their added complexity does not
meet the four-percent bar. Preserve these experiment results and discuss
whether the unused pilot implementations belong on a research branch.

The earlier [technique controls](../../../docs/review-2026-09-09/TECHNIQUE_CONTROLS.md)
show a useful compiler-coverage gap: the typed-local array read is substantially
faster despite more instructions. This and the remaining cell-access/inner-
dispatch overhead are possible next measurements, not promised GLOOM savings.
The native profile remains the missing attribution step.

## Evidence paths

Relative to Source/JaiScript:

- out/astra/final-pilot-postinline-20260909/comparison/{audit,metadata,runs,summary,differences,factorial-analysis}.json
- out/astra/final-pilot-postinline-20260909/comparison/*.log and recorded runner.py
- out/astra/final-pilot-postinline-20260909/{source-inputs,workload-equivalence,build-flag-audit,plan-audit}.json
- out/astra/final-pilot-postinline-20260909/{stock,cells,cluster,both}/release/{compile_commands,build-inputs}.json
- out/astra/final-pilot-postinline-20260909/{audit-comparison.py,analyze-factorial.py,comparison-launch.json}
- out/astra/temp-reuse-20260909/store/release and its source-inputs.json: clean reference
- out/astra/final-pilot-matrix-20260909/stock/release and source-inputs.json: pre-inline control
- out/astra/cells-pilot-20260909/{hash-gates/results,profile-audit}.json and profile-gloom.err
- out/fable/final_source/SOURCE.txt and the row 94/95/96 gate handoffs
