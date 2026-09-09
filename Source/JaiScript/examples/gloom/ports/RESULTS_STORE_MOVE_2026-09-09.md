# Scalar store and move reuse: paired result, 2026-09-09

Neither increment receives speed credit from this window. Store reuse produces
a small serial GLOOM change without a corresponding JaiDOOM gain. Move reuse
makes serial GLOOM slightly slower in both layouts; its JaiDOOM improvement
depends strongly on layout. Worker results remain noisy.

The three frozen revisions isolate each mechanism: 68aeae4d contains temporary
reuse; e85a793b adds store reuse; ef3a6d86 adds move reuse. Every pair is projected
from one interleaved window, with a complete accepted series and its own padding
control. Counters showing avoided construction do not substitute for these
whole-program results.

## Measurements

GLOOM values are ms/tick; JaiDOOM values are ms/tic. The padding band is the
range of six paired padded/normal deltas, not a confidence interval.

| Workload | P cores | Revision | Normal | Padding | Padding band |
|---|---:|---|---:|---:|---:|
| JaiScript VM serial | 1 | 68aeae4d | 1.347312 | 1.355757 | -0.33% to +1.58% |
| JaiScript VM serial | 1 | e85a793b | 1.343625 | 1.344971 | -0.36% to +0.56% |
| JaiScript VM serial | 1 | ef3a6d86 | 1.352434 | 1.354208 | -0.52% to +0.37% |
| JaiScript VM 8 workers | 8 | 68aeae4d | 0.942845 | 0.956220 | -1.86% to +4.93% |
| JaiScript VM 8 workers | 8 | e85a793b | 0.951457 | 0.950279 | -4.59% to +3.52% |
| JaiScript VM 8 workers | 8 | ef3a6d86 | 0.959926 | 0.945887 | -4.95% to +3.40% |
| JaiDOOM VM timedemo | 1 | 68aeae4d | 4.633409 | 4.585216 | -1.27% to -0.56% |
| JaiDOOM VM timedemo | 1 | e85a793b | 4.653740 | 4.586471 | -1.76% to -1.33% |
| JaiDOOM VM timedemo | 1 | ef3a6d86 | 4.568022 | 4.579568 | -1.65% to +0.46% |

| Workload | Comparison | Layout | Delta | Paired revision-delta range |
|---|---|---|---:|---:|
| JaiScript VM serial | 68aeae4d to e85a793b | normal | -0.27% | -1.04% to +0.00% |
| JaiScript VM serial | 68aeae4d to ef3a6d86 | normal | +0.38% | -0.53% to +0.80% |
| JaiScript VM serial | e85a793b to ef3a6d86 | normal | +0.66% | +0.23% to +1.04% |
| JaiScript VM serial | 68aeae4d to e85a793b | padding | -0.80% | -1.26% to -0.29% |
| JaiScript VM serial | 68aeae4d to ef3a6d86 | padding | -0.11% | -0.92% to +0.35% |
| JaiScript VM serial | e85a793b to ef3a6d86 | padding | +0.69% | +0.18% to +0.97% |
| JaiScript VM 8 workers | 68aeae4d to e85a793b | normal | +0.91% | -1.71% to +7.20% |
| JaiScript VM 8 workers | 68aeae4d to ef3a6d86 | normal | +1.81% | -2.57% to +4.71% |
| JaiScript VM 8 workers | e85a793b to ef3a6d86 | normal | +0.89% | -2.32% to +3.91% |
| JaiScript VM 8 workers | 68aeae4d to e85a793b | padding | -0.62% | -4.90% to +2.34% |
| JaiScript VM 8 workers | 68aeae4d to ef3a6d86 | padding | -1.08% | -6.17% to +3.14% |
| JaiScript VM 8 workers | e85a793b to ef3a6d86 | padding | -0.46% | -1.71% to +0.78% |
| JaiDOOM VM timedemo | 68aeae4d to e85a793b | normal | +0.44% | +0.20% to +0.62% |
| JaiDOOM VM timedemo | 68aeae4d to ef3a6d86 | normal | -1.41% | -1.66% to +0.17% |
| JaiDOOM VM timedemo | e85a793b to ef3a6d86 | normal | -1.84% | -1.94% to -0.09% |
| JaiDOOM VM timedemo | 68aeae4d to e85a793b | padding | +0.03% | -0.33% to +0.10% |
| JaiDOOM VM timedemo | 68aeae4d to ef3a6d86 | padding | -0.12% | -0.58% to +0.01% |
| JaiDOOM VM timedemo | e85a793b to ef3a6d86 | padding | -0.15% | -0.66% to +0.07% |

Store reuse changes serial GLOOM by -0.27% normally and -0.80% padded. Its
JaiDOOM changes are +0.44% and +0.03%. This does not establish a useful gain
across the two whole-program gates.

Move reuse changes serial GLOOM by +0.66% and +0.69% relative to store-only.
Every paired serial delta is positive. That is a small slowdown in this window,
not an improvement. The JaiDOOM deltas are -1.84% normally and -0.15% padded;
the padded paired range crosses zero. A stable improvement is not established.

Combined, the two increments change serial GLOOM by +0.38% normally and -0.11%
padded. Worker signs reverse between layouts. No worker gain is credited.

## Protocol and verification

The [temporary-reuse protocol](RESULTS_TEMP_REUSE_2026-09-09.md) supplies the
workloads, compiler flags and source-freezing method. This window adds a third
named variant: baseline, store, move in fixed order for each normal/padding
sweep, repeated twice in each of three rounds. Each pair preserves ABAB order.
Every variant has six accepted timings per layout and workload.

The Core Ultra 9 285HX has 24 physical/logical CPUs. Serial uses CPU 0; eight
workers use CPUs 0, 1, 10, 11, 12, 13, 22 and 23. One workload process runs at
a time, with a recorded background allowance of 1.0 core and competing-process
screening. Accepted outliers remain in the bands.

Verified by Astra running: 141 processes, every exit and hash correct; 108
accepted timings, 30 conformance gates and three busy exclusions. The audit
reparses raw logs, recomputes screening and medians, verifies the complete
accepted plan and checks unchanged binary, workload and build manifests.
GLOOM 300 ticks retains frame 7fecc09815bb64e0 and state 3580805725;
600 ticks retains frame dfd969ed5a336dca and state 1503537018. JaiDOOM retains
state 1746772898 on both warm-up and the measured 3984-tic replay.

Read/source-audited: the store branch avoids a temporary and redundant type
stamp; the move branches avoid a full value assignment when kind, stamp and
engine match. Fable's counters separate the mechanisms: 2,503,181 store reuse
hits and 788,814 move reuse hits per 600 GLOOM ticks. Those are coverage counts.

Fable ran the full correctness gates on the combined state: Debug 3011 tests
on each backend, XTHREAD 119 on each, Release 2910 on each, fuzz 3000 plus 1500
with proofs disabled, and both workload hashes. The intermediate store-only
state was compile-checked. These owner gates are distinct from Astra's runs.

Raw evidence relative to Source/JaiScript:

- out/astra/temp-reuse-20260909/landing-comparison/{audit,metadata,runs,summary,differences}.json
- out/astra/temp-reuse-20260909/landing-comparison/*.log and recorded runner.py
- out/astra/temp-reuse-20260909/{candidate,store,move}/source-inputs.json
- out/astra/temp-reuse-20260909/{candidate,store,move}/release/{compile_commands,build-inputs}.json
- out/astra/temp-reuse-20260909/{measure-landings.py,landing-comparison-launch.json,landing-audit-report.log}
