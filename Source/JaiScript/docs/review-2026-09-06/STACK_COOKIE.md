# VM stack-cookie experiment, 2026-09-06

The VM-only `/GS-` candidate is faster in every accepted baseline/candidate
pair, in both normal and padded layouts, on all three whole-program
configurations. The changes exceed the observed padding variation in this
snapshot. This measures the compiler option's whole-program effect; it does not
isolate the cost of the cookie helper alone. Adoption remains Dev's decision.

## Warmed results and padding bands

CPU: Intel Core Ultra 9 285HX, 24 physical and logical cores. The topology
reports eight distinct cores in its fastest efficiency class. Serial runs use P core CPU 0. Eight-worker GLOOM is restricted to
eight distinct P cores, CPUs 0, 1, 10, 11, 12, 13, 22, 23. Each row has six
accepted measurements per layout from three ABAB rounds.

The padding band is the minimum/maximum paired percentage change from the
normal binary to its 40-line cold-function padding control. It belongs to both
medians on that row. It is an observed placement/variation band, not a confidence
interval. Units are shown per workload.

| Workload | P cores | Build | Normal median | Padded median | Paired padding band |
|---|---:|---|---:|---:|---|
| GLOOM serial (ms/tick) | 1 | Default /GS | 2.059322 | 2.048733 | -0.80% to +0.21% |
| GLOOM serial (ms/tick) | 1 | VM-only /GS- | 1.953170 | 1.953149 | -0.45% to +0.17% |
| GLOOM eight workers (ms/tick) | 8 | Default /GS | 1.773933 | 1.775714 | -0.45% to +0.26% |
| GLOOM eight workers (ms/tick) | 8 | VM-only /GS- | 1.702119 | 1.708630 | -0.56% to +1.18% |
| JaiDOOM timedemo (ms/tic) | 1 | Default /GS | 5.452937 | 5.444277 | -0.30% to +0.00% |
| JaiDOOM timedemo (ms/tic) | 1 | VM-only /GS- | 5.083710 | 5.046812 | -0.99% to -0.49% |

Candidate changes below are relative to the baseline median of the same layout.
Negative means less elapsed time. Each delta carries its observed paired
candidate/baseline range; the corresponding padding bands are in the table above.

| Workload | Normal median change [paired range] | Padded median change [paired range] |
|---|---:|---:|
| GLOOM serial (ms/tick) | -5.15% [-5.57%, -4.77%] | -4.67% [-5.25%, -4.52%] |
| GLOOM eight workers (ms/tick) | -4.05% [-5.13%, -3.69%] | -3.78% [-4.48%, -3.51%] |
| JaiDOOM timedemo (ms/tic) | -6.77% [-6.93%, -6.48%] | -7.30% [-7.40%, -7.10%] |

All paired candidate/baseline ranges remain below zero. The direction also holds
after the cold-function padding change. These are whole-program movements; no
micro-benchmark result is being credited under the 15% rule.

## Method and audit

The one combined command ran GLOOM serial, GLOOM with eight workers, and JaiDOOM
on the VM. For each workload and round it executed normal baseline, normal
candidate, padded baseline, padded candidate, then repeated that order. That
interleaves both the compiler variants and their padding controls. Exactly
72 timing processes were accepted: two per build/layout/workload in each of
three rounds. Another 20 processes performed the initial conformance gates.

GLOOM runs 600 warm-up ticks, resets the game in the same runtime, then measures
600 ticks at seed 666. The host reports ms/tick. JaiDOOM first executes the full
3984-tic replay, then calls its existing `main()` again in the same engine.
Both replay hashes must match. The measured replay's integer elapsed milliseconds
divided by 3984 gives ms/tic; the script's rounded printed rate is also retained.
Level/demo setup is outside that script timer. No profiler was recording.

The desktop background ceiling stayed at 1.0 CPU core, matching the reference
campaign. This is an explicit activity allowance, not an idle-machine claim.
Accepted processes recorded 0.425 to 0.996 background-core equivalents. Four
timing attempts exceeded the ceiling and were rejected and retried. None
overlapped a detected compiler, test, fuzz, client or benchmark process. Process
checks run every 250 ms and at exit; a shorter-lived process can escape polling.

The final audit independently recomputed counts, accepted ABAB order, medians
and padding bands. It verified that all executable hashes, workload hashes and
the entire frozen source manifest stayed unchanged. The exact runner source
is preserved with the raw session and matches its recorded SHA256.

## Build control and identities

`JAISCRIPT_EXPERIMENT_VM_NO_GS=ON` applies `/GS-` only to
`source/implementation/vm/vm_backend.cpp` through a source-file compile option.
It defaults OFF and requires MSVC. No VM source, checker, value implementation,
interpreter source or game script was edited.

The baseline and candidate use a plain source archive of
`725cba38d18c15cc0fa47004f99165972a1af351` plus the measurement-file overlays
listed in `out/astra/gs-20260906/source-inputs.json`. This is not a git worktree.
The snapshot includes its own GLOOM and JaiDOOM scripts, demo and WAD. The runner
is the committed `f9d16f0e` version.

Compiler: MSVC 19.51.36252, x64. Shared Release flags include
`/O2 /Ob2 /Oi /Ot /GL /DNDEBUG /Zi /MT /EHsc /permissive- /bigobj`.
Link flags include `/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`.
After normalizing build output paths, the compile-command audit finds exactly
one difference: `/GS-` on `vm_backend.cpp`. The candidate emits C4652,
explicitly stating that the current command-line security-check option overrides
the precompiled header's option. Every other translation unit retains its
normal compile setting. Final code layout and inlining can still change under
link-time optimization.

Both layouts compile the same sources. The padded GLOOM executable has 40
retained volatile statements in the host's cold help function; the padded
standalone runner has the corresponding 40 statements in its cold help
function. Neither help function executes during these benchmarks.

## Correctness verification

| Gate | Result |
|---|---|
| Default Debug Foundry, interpreter and VM | 116 suites, 2740 tests passed on each |
| Candidate Debug Foundry, interpreter and VM | 116 suites, 2740 tests passed on each |
| Debug GLOOM, both backends, 300 ticks, seed 666 | Frame `7fecc09815bb64e0`, state `3580805725` |
| Release baseline/candidate, normal/padding, GLOOM serial/eight | 300-tick reference above and warmed 600-tick frame `dfd969ed5a336dca`, state `1503537018` |
| Release baseline/candidate, normal/padding, JaiDOOM VM | Warm-up and measured replay state `1746772898`; 3984 tics |

All 96 processes in the completed session exited 0 and matched their hashes,
including the four timing attempts rejected for background CPU activity.
The Debug suites and 300-tick gates were run against the built configurations
before timing; the report does not substitute measurements for correctness tests.

## Security trade-off and limits

Disabling `/GS` removes the compiler's stack-buffer-cookie protection for the
affected translation unit. Some stack overwrites that would terminate the process
under the normal check can continue undetected. Correctness tests and matching
game hashes do not test that protection. Microsoft describes the coverage and
limitations in [/GS (Buffer Security Check)](https://learn.microsoft.com/en-us/cpp/build/reference/gs-buffer-security-check?view=msvc-170).
This report makes no adoption recommendation.

The earlier 2.8% serial CPU share of `__security_check_cookie` is an exclusive
sampled-function share from the older C7EF13D9 binary. It excludes some caller
prolog/epilog work and is not a speedup prediction for this newer source snapshot.
The measured difference includes compiler register/stack-layout and placement
effects. It cannot be labelled the isolated execution cost of that helper.

The completed numbers describe this warmed, pinned desktop snapshot. The bands
are empirical ranges from three rounds, not general performance guarantees or
confidence intervals. The Bindstone client uses its own build layout; this
experiment does not establish its shipping-client speedup.

The interrupted `measurement/` and `measurement-quiet/` attempts remain
invalidated. Their times are not trusted as performance evidence and do not
contribute to any table here.

## Reproduction and raw evidence

Build/capture helpers and artifacts live under
`Source/JaiScript/out/astra/gs-20260906/`. `build.ps1` builds the fixed
source variants. `measure-final.ps1` records the command and closes the shared
timing window in a finally block. A new run must use a fresh output directory.

```powershell
python -u Source/JaiScript/examples/gloom/ports/run_comparison.py --engines jai --workloads gloom,timedemo --rounds 3 --build Source/JaiScript/out/astra/gs-20260906/baseline --compare-build Source/JaiScript/out/astra/gs-20260906/candidate --jai-workload-root Source/JaiScript/out/astra/gs-20260906/tree/Source/JaiScript --output Source/JaiScript/out/astra/gs-20260906/NEW-RUN --max-background-cores 1.0
```

- `measurement-final/runs.json` and per-process logs: every gate, accepted
  timing, rejected attempt, command, affinity and background/overlap reading.
- `measurement-final/summary.json` and `differences.json`: medians,
  full run ranges, per-build padding bands and paired candidate/baseline bands.
- `measurement-final/metadata.json`, `runner.py`, `cpu.json` and
  `audit.json`: topology, frozen input/binary identities, exact protocol
  source and independent completion audit.
- `source-inputs.json`, `source.tar` and `tree/`: fixed source inputs.
- `baseline/`, `candidate/` and `debug-candidate/`: libraries,
  executables, PDBs, compile commands and input manifests.
- `compile-option-audit.json`, `*-gs-flags.json` and build logs:
  per-translation-unit option audit and compiler diagnostics.
- `debug-interpreter-tests.log`, `debug-vm-tests.log` and
  `debug-gloom-300.log`: candidate Debug gates. Default Debug and
  marker-build gate copies are under `out/astra/gates-item1/`.

Verified by running: builds, compile-command audit, both full Debug suites,
all hash gates, three complete combined timing rounds, padding controls and
unchanged-input checks. Inferred from reading: security coverage and why sampled
helper cost is not a whole-program speedup prediction. Untrusted: timings from
the two interrupted attempts, and any claim that the completed result predicts
an isolated helper cost or a general shipping-client gain.
