# Clustered typed-loop pilot C, 2026-09-09

Clustering improves serial GLOOM against the same-source stock build, but falls
below the four-percent bar for added complexity. The shared handler refactor
has a measured JaiDOOM cost that clustering does not fully recover. This is
experimental evidence, not a broad shipping win.

## Whole-program measurements

GLOOM uses ms/tick; JaiDOOM uses ms/tic. Padding bands are the ranges of six
paired padding/normal deltas, not confidence intervals.

| Workload | P cores | Configuration | Normal | Padding | Padding band |
|---|---:|---|---:|---:|---:|
| JaiScript VM serial | 1 | 98180a54 reference | 1.345887 | 1.338458 | -1.05% to +0.00% |
| JaiScript VM serial | 1 | Same-source stock | 1.342464 | 1.344520 | -0.69% to +1.45% |
| JaiScript VM serial | 1 | Cluster C | 1.304202 | 1.315895 | +0.27% to +1.33% |
| JaiScript VM 8 workers | 8 | 98180a54 reference | 1.061608 | 1.134856 | -2.23% to +11.02% |
| JaiScript VM 8 workers | 8 | Same-source stock | 1.110675 | 1.139477 | -0.64% to +3.41% |
| JaiScript VM 8 workers | 8 | Cluster C | 1.049596 | 1.080379 | -3.75% to +11.20% |
| JaiDOOM VM timedemo | 1 | 98180a54 reference | 4.526857 | 4.540035 | +0.08% to +0.37% |
| JaiDOOM VM timedemo | 1 | Same-source stock | 4.601908 | 4.598394 | -0.42% to +0.28% |
| JaiDOOM VM timedemo | 1 | Cluster C | 4.552083 | 4.587224 | +0.35% to +4.41% |

| Workload | Comparison | Layout | Delta | Paired revision-delta range |
|---|---|---|---:|---:|
| JaiScript VM serial | 98180a54 reference to Same-source stock | normal | -0.25% | -0.52% to +0.01% |
| JaiScript VM serial | 98180a54 reference to Cluster C | normal | -3.10% | -3.62% to -2.87% |
| JaiScript VM serial | Same-source stock to Cluster C | normal | -2.85% | -3.37% to -2.42% |
| JaiScript VM serial | 98180a54 reference to Same-source stock | padding | +0.45% | -0.02% to +1.48% |
| JaiScript VM serial | 98180a54 reference to Cluster C | padding | -1.69% | -2.55% to -0.83% |
| JaiScript VM serial | Same-source stock to Cluster C | padding | -2.13% | -3.22% to -1.11% |
| JaiScript VM 8 workers | 98180a54 reference to Same-source stock | normal | +4.62% | -9.17% to +8.28% |
| JaiScript VM 8 workers | 98180a54 reference to Cluster C | normal | -1.13% | -8.99% to +0.43% |
| JaiScript VM 8 workers | Same-source stock to Cluster C | normal | -5.50% | -10.43% to +0.19% |
| JaiScript VM 8 workers | 98180a54 reference to Same-source stock | padding | +0.41% | -5.82% to +4.25% |
| JaiScript VM 8 workers | 98180a54 reference to Cluster C | padding | -4.80% | -10.76% to -2.86% |
| JaiScript VM 8 workers | Same-source stock to Cluster C | padding | -5.19% | -9.99% to -0.60% |
| JaiDOOM VM timedemo | 98180a54 reference to Same-source stock | normal | +1.66% | +1.42% to +2.10% |
| JaiDOOM VM timedemo | 98180a54 reference to Cluster C | normal | +0.56% | +0.28% to +0.63% |
| JaiDOOM VM timedemo | Same-source stock to Cluster C | normal | -1.08% | -1.49% to -0.79% |
| JaiDOOM VM timedemo | 98180a54 reference to Same-source stock | padding | +1.29% | +0.99% to +1.50% |
| JaiDOOM VM timedemo | 98180a54 reference to Cluster C | padding | +1.04% | +0.67% to +4.69% |
| JaiDOOM VM timedemo | Same-source stock to Cluster C | padding | -0.24% | -0.73% to +3.50% |

Serial GLOOM improves 2.85% normally and 2.13% padded against the same-source
stock. Every paired serial delta is negative. The normal saving is 38.263
microseconds per tick. Against the earlier reference the net change is -3.10%
and -1.69%, below the four-percent complexity bar.

The disabled-clustering stock costs JaiDOOM +1.66% normally and +1.29% padded
against the earlier reference. Clustering recovers some of that, but its net
changes remain +0.56% and +1.04%. The comparison cannot credit the relative
clustering gain while hiding the shared infrastructure cost.

Worker variability is large: the cluster padding band extends to +11.20%.
No worker gain is credited. An accepted padded JaiDOOM outlier remains in the
data and expands its cluster padding band to +4.41%.

## Technique control

The arithmetic gauge records one clustered block; the mixed typed-local array
read records none because its store RHS still uses a general binary opcode.
All stock gauge functions record no block.

| Gauge | Configuration | Normal ns/iteration | Padding ns/iteration | Padding band |
|---|---|---:|---:|---:|
| arithmetic | stock | 20.309 | 20.028 | -2.99% to -0.26% |
| arithmetic | cluster | 16.512 | 16.315 | -3.86% to +0.11% |
| loadstore_decl | stock | 42.064 | 41.952 | -0.84% to +1.86% |
| loadstore_decl | cluster | 41.731 | 41.816 | +0.11% to +0.88% |

The arithmetic control improves 18.70% normally and 18.54% padded. The inactive
array control does not establish a meaningful gain. These are complete-pattern
measurements, not isolated dispatch costs or whole-program ceilings.

## Source, protocol and verification

The reference is frozen 98180a54. Stock and C use the same frozen bba97c6a tree
plus Fable's reviewed pilot patch 5ab4714d01a1a770cecc31b62336d6fa5ebd4aadb3e6792190f444400c057d2d.
C alone defines JAISCRIPT_PILOT_CLUSTER. Astra compared all 150 non-test runtime
and header files with landed master 1a229313 and found them byte-identical.
The later test-budget correction and documentation edits do not change these
binaries. The snapshot includes the move optimization, equally on every side;
the final raw-cell matrix must use a common source after its reversion.

The stock build is a necessary control: even with clustering disabled, the
fast-path/wrapper split and unused opcode differ from the older reference.
No identical-machine-code assumption is made.

The [paired protocol](RESULTS_TEMP_REUSE_2026-09-09.md) supplies compiler flags,
workloads and source freezing. This run interleaves all three variants in fixed
normal/padding order twice per round for three rounds. Each configuration has
six accepted measurements per layout. Serial uses CPU 0; eight workers use
CPUs 0, 1, 10, 11, 12, 13, 22 and 23 on the 24-core Core Ultra 9 285HX.

Timing paused after 70 accepted samples when background work exceeded the
unchanged 1.0-core allowance. Only the owned waiting timing controller was
stopped. The same inputs and exact accepted prefix were verified before resume.
A late second-round JaiDOOM padding pair straddles the interruption. The report
retains that fact, all accepted outliers and all rejected attempts.

Verified by Astra running: all 162 whole-program processes exit successfully
and hash-match; 108 accepted timings, 30 conformance gates and 24 busy
exclusions. The audit reparses logs, verifies screening and accepted order,
recomputes medians/bands and checks unchanged input identities. The smaller
control has 48 accepted timings from 59 exact-result processes and an
independent formula/order/input audit. Separate stock/C GLOOM checks cover
VM workers 0/8 and C interpreter workers 0/8.

Read/source-audited: the back-edge entry, scalar load eligibility, per-access
guards, exact instruction fallback, budget control, ordinary first iteration
and source identity. Fable ran cluster and stock Debug suites, 3020 tests per
backend/configuration; cluster Release, 3048 per backend; fuzz 3000 plus 1500
with proofs disabled; and both whole-program hashes.

Fable's counter capture reports 59,200 block entries, 2,117,759 head comparisons,
12,351,354 body selections, 2,058,559 internal back edges, zero misses and
59,200 exits. This is a small inner interpreter with one outer dispatch per
block entry, not one instruction selection per iteration. Instrumented cycle
charges do not provide the reported wall-time gains.

Raw paths relative to Source/JaiScript:

- out/astra/cluster-pilot-20260909/comparison/{audit,metadata,runs,summary,differences}.json
- out/astra/cluster-pilot-20260909/comparison/*.log and resume-01-before-* records
- out/astra/cluster-pilot-20260909/{pause-checkpoint.json,paused-controller.json,resume-watcher.log}
- out/astra/cluster-pilot-20260909/{source-inputs.json,pilot_c.patch,landed-runtime-verification.json}
- out/astra/cluster-pilot-20260909/{reference,stock,cluster}/release/compile_commands.json
- out/astra/cluster-pilot-20260909/gauge-timings/{audit,metadata,runs,summary,differences}.json
- out/astra/cluster-pilot-20260909/{gauge-recognition.json,stock-gauge-bytecode.txt,cluster-gauge-bytecode.txt}
- out/astra/cluster-pilot-20260909/hash-gates/results.json
