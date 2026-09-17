# GLOOM and JaiDOOM whole-program gates

Measured snapshot: [2026-09-08 results](RESULTS_2026-09-08_FINAL.md).

The ports execute the shared host's deterministic 100x40, quad-pixel workload.
Squirrel 3.2 uses the bundled runtime with 64-bit integers and `SQUSEDOUBLE`,
and the existing scripts in `squirrel/scripts`. Its normal and padding builds
run the same conformance gates and timing protocol as the Lua configurations.
Lua 5.4.8 and Lua 5.5.1 use the same Lua scripts and the same sol2 adapter.
LuaJIT 2.1 JIT uses the same adapter with the overrides in `luajit/scripts`;
unmodified modules come from `lua/scripts`.

LuaJIT JIT is a runtime-code-generation ceiling. It is not a shipping target for
Bindstone's iOS configuration, which cannot use runtime code generation.

## Build on Windows

Use Python 3.11 or newer, CMake, Ninja, and an x64 Visual Studio Native Tools
environment. From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File Source/JaiScript/examples/gloom/ports/build_comparison.ps1
python Source/JaiScript/examples/gloom/ports/run_comparison.py
```

All downloaded sources, object files, binaries and evidence stay under
`Source/JaiScript/out/astra`. `build_comparison.ps1 -OutputRoot` accepts another
directory under `Source/JaiScript/out/astra`. Pass its `release` child to the runner's
`--build` option and an ignored evidence directory to `--output`.

`prepare_comparison.py` pins archive SHA256 hashes for Lua 5.4.8, Lua 5.5.1,
sol2 3.3.0, and LuaJIT JIT branch `v2.1` at
`24c20c94e7db195b640854619577441f9b4bc6be`. The branch tip was resolved on
2026-09-06. The script patches only its isolated sol2 dependency copy: it admits
Lua version 505, supplies the removed error-code fallback, and passes Lua 5.5's
seed argument in the custom-allocator state constructor. The actual Lua 5.5
header and version number remain visible. The adapter uses `luaL_newstate`
through sol2's ordinary default constructor.

## Workload and validation

The required 300-tick reference is frame `7fecc09815bb64e0`, state `3580805725`.
The 600-tick reference is frame `dfd969ed5a336dca`, state `1503537018`.
Both use seed 666. Every normal and padded configuration must reproduce both.

`--warmup 600` runs 600 untimed frames, then boots the same runtime again with
the original seed and a new game state. Runtime caches and JIT traces survive.
Only the following original 600 ticks are timed and hashed. The runner verifies
that rebooting after warm-up preserves the unwarmed reference hashes.

Without `--warmup`, smoke behavior is unchanged. An explicit `--backend` now
selects one smoke backend; omitting it still runs both JaiScript backends.
The shared host owns rendering-output hashing, so all ports pay the same host
hashing cost. Startup, script loading, boot, warm-up and final summaries are
outside the reported timed interval. Script execution, frame-string creation
and host frame hashing are inside it.

## Placement controls and evidence

`GLOOM_COMPARISON_PADDING_CONTROLS=ON` builds a second executable for each
runtime. The only source difference is 40 volatile local arithmetic statements
in the shared host's cold help function. That function is never executed by the
benchmark. It remains reachable through `--help`, keeping the padding in the
optimized binary.

The runner executes three rounds of A B A B per configuration, with A the
normal executable and B the padding control. It launches one benchmark process
at a time, pins serial configurations to one performance core and the
eight-worker configuration to eight performance cores, and records the CPU
topology and affinity. It waits for competing compiler/test/benchmark processes
to finish and for the requested background CPU threshold. Busy samples are
retained in the raw JSON but excluded from summary statistics. While a child
runs, the runner checks for competing processes every 250 ms as well as at the
end; any observed competitor invalidates that timing sample. Shorter-lived
processes can still escape polling, so the shared timing window must stay clear.

Each reported median carries the padding control median, the range of paired
padding deltas and the observed run range. The padding band includes normal
run-to-run variation, ASLR and runtime hash-seed variation; it is not a pure
measurement of one compiler mechanism.

`metadata.json` records source and executable hashes. The runner passes the
Squirrel script directory explicitly from `--jai-workload-root` and hashes its
`.nut` inputs, including when that root is a frozen source snapshot. `runs.json` records every
command, output filename, hash verdict, CPU affinity and background CPU reading.
`summary.json` contains only accepted runs. A port that fails conformance has
no timing result.

## JaiDOOM timedemo

The default command runs both whole programs. JaiDOOM now includes the Lua 5.4.8 port as well as JaiScript. For JaiScript increments, omit
the rival configurations with:

```powershell
python Source/JaiScript/examples/gloom/ports/run_comparison.py --engines jai --workloads gloom,timedemo --rounds 3 --build Source/JaiScript/out/astra/release --output Source/JaiScript/out/astra/whole-program-REFERENCE
```

Run `examples/jaidoom/bootstrap.bat` once as described in its README to
provide the git-ignored `examples/jaidoom/wads/doom1.wad`. The timedemo
requires that WAD and `demo.json` before the runner can hash its inputs.

JaiDOOM uses `examples/jaidoom/scripts/p10_timedemo.jai`, `demo.json` and
`doom1.wad` at 120x78. Each process first executes the entire timedemo as a
warm-up, then invokes its existing `main()` again in the same engine with
`--benchmark-main=1`. Both replays must produce state `1746772898`.
The measured replay must report 3984 tics. The reported integer elapsed
milliseconds divided by 3984 gives ms/tic; the script also prints a rounded
two-decimal ms/tic value, retained in the raw record. Script loading and the
warm-up replay are excluded. The script's replay-loop timer excludes level
loading and final hashing.

`JAISCRIPT_RUNNER_PADDING_CONTROL=ON` builds `jaiscript_padding.exe` with
40 retained volatile statements in its cold help function. The normal and
padding binaries run A B A B in each of three rounds, giving six accepted
measurements per layout and the same paired padding band as GLOOM.

Pass `--jai-workload-root PATH` to use a frozen JaiScript source/workload
copy, including its `examples/jaidoom/demo.json` and `wads/doom1.wad`.
GLOOM then receives an explicit `--scripts` path as well. This keeps Fable's
live script edits out of a measurement window.

Pass `--compare-build PATH` to compare a candidate build in the same command.
For each workload and round the order is normal baseline, normal candidate,
padding baseline, padding candidate, repeated twice. Thus baseline/candidate
and normal/padding both interleave. `differences.json` records candidate
deltas separately for the two layouts. `summary.json` retains each build's
padding band. The band is the minimum and maximum paired padding/normal
percentage difference, not a confidence interval. No baseline/candidate
improvement is established merely by exceeding zero.

For three or four build variants, repeat `--variant-build NAME=PATH` alongside
`--build` and optionally `--compare-build`. Names are simple labels, such as
`raw`, `fused` and `both`. The runner keeps a fixed variant order: all normal
binaries, then all padding binaries, repeated twice per round. Projecting that
sequence onto any two variants preserves the existing ABAB protocol.

Each variant keeps its own padding band. `differences.json` records every pair,
including adjacent increments such as store-only versus store-plus-move, using
`baseline_variant` and `candidate_variant` labels. The existing two-build
baseline/candidate output keeps its prior schema. Busy attempts retry the same
plan slot; an incomplete accepted series fails instead of silently dropping a
pair. Resume checks preserve variant order and the exact accepted prefix.

The runner defaults to a 0.20-core background CPU ceiling. Any override through
`--max-background-cores` is recorded in `metadata.json`. This is a desktop
activity allowance, not a claim that the machine was otherwise idle. Timing
runs require a clear build/test/benchmark process list. Do not build or run
other workloads during the measurement window. A hash failure invalidates a
configuration; it never becomes a timing point. Use a fresh evidence directory
for each run. The runner records executable and workload input SHA256 hashes
and checks them again at the end; an input change invalidates the session.

## Sampled CPU profiles on Windows

Build all normal and padding targets with symbols and markers:

```powershell
powershell -ExecutionPolicy Bypass -File Source/JaiScript/examples/gloom/ports/build_comparison.ps1 -OutputRoot Source/JaiScript/out/astra/gates-profile -ProfileMarkers
```

Run `capture_profile.ps1` in Administrator PowerShell. It calls the same
combined runner with `--engines jai --workloads gloom,timedemo --rounds 3`,
including the gates, warm-ups, CPU pinning and padding controls. Parameters
`-BuildRoot`, `-Rounds`, `-Workloads` and `-MaxBackgroundCores` select
the build and capture. ETL, logs and JSON go under
`out/astra/cpu-captures/TIMESTAMP`. Existing WPR recordings are left alone.
Recorded timings are labelled `WPR` and must not be used as uninstrumented
performance results.

Provider `744b3f90-7d83-4b7b-a861-40637623c592` emits paired begin/end
markers. GLOOM markers delimit its measured loop, excluding loading, warm-up
and summaries. JaiDOOM markers delimit the repeated `main()` call, including
that call's level/demo setup; its printed ms/tic still measures only the replay
loop. Filter samples by PID and marker interval and state which interval was
used. Provider registration belongs to each execution; normal builds compile
out the marker code.


## Recovering an interrupted timing session

Pass `--resume` with the original build, workload, output, round-count,
recording and background-limit arguments. Recovery requires complete conformance
gates. It verifies binary/workload identities, topology, raw logs, screening
verdicts and the exact accepted ABAB prefix before launching anything else.
Already accepted runs and every rejected attempt stay in the original order.
Changes to any measurement input or acceptance limit require a fresh session.

`--max-attempts` sets the busy-retry budget (default 10). Raising that budget
does not change the acceptance threshold. Short LuaJIT JIT processes can produce
coarse process/system CPU-accounting diagnostics and need more retries. They
still receive the same 600-tick workload, warm-up and screening.

Recovery preserves pre-resume metadata/run backups and the new runner source.
A completed timing run prints its accepted/required count. It never publishes a
successful partial timing summary. JSON checkpoints are replaced atomically.

The competitor list is `cl.exe`, `link.exe`, `ninja.exe`,
`msbuild.exe`, `bindstone.exe` and names beginning `jai_gloom`,
`gloom_lua` or `jaiscript`. Other applications and agent processes are
covered only by the background CPU screen and the coordinated quiet window.

CPU trace analysis additionally needs Windows Performance Toolkit (`xperf`)
and matching PDBs. The marked binaries have been built and hash-checked; the
combined administrative CPU-recording wrapper has not yet been run end to end.


## VM function/line cycle profiles

A build with `-DCMAKE_CXX_FLAGS=/DJAISCRIPT_VM_PROFILE` prints diagnostic
opcode-cycle attribution by script function and source line. This instrumentation
is separate from ETW sampling and must not supply ordinary benchmark timings.

For the warmed GLOOM reference, use:

```powershell
jai_gloom.exe --smoke --backend vm --workers 0 --warmup 600 --ticks 600 --seed 666 --profile-reset-tick 0
```

The reset occurs before the named zero-based measured tick. It therefore
excludes loading, warm-up and reboot while retaining all 600 reference frames.
The later state-hash and summary script calls are also present in the dump.

In a profile build, `jaiscript.exe --benchmark-main=1` resets function/opcode
counters after the warm-up returns and before the repeated `main()` begins.
The dump describes that replay, including its level/demo setup; its printed
ms/tic still uses the workload's replay-loop timer. Reset occurs outside active
VM execution. With multiple repetitions, the final dump describes the last.

The initial profiler at `4922f220` double-charges coroutine reentry to the
caller opcode and omits host/coroutine entry/inclusive accounting. Worker
reports also carry the same heading as the parent. Treat that version's rows
as opcode-charge rankings, not disjoint script CPU self time. Other diagnostic
counters may include warm-up because the reset covers function/IP and opcode
totals, not every auxiliary counter.

## Native CPU attribution

`capture_profile.ps1` accepts `-JaiWorkloadRoot` for frozen scripts and
`-OutputRoot` for a new capture directory under `out/astra`. It refuses an
existing output directory before starting WPR. Use a build made with
`-ProfileMarkers`; recording requires an elevated PowerShell session.

The marker build writes one `[profile-cpu]` record per measured span, with
process and calling-thread user/kernel CPU deltas in 100-nanosecond units.
These bracket the existing begin/end markers and include their small overhead.
Windows accounting is quantized, so pool repeated spans and report that
resolution alongside per-capture variation. Keep CPU time separate from elapsed
time and report additional-thread CPU separately. Profiled wall times are
capture diagnostics; speed claims use the uninstrumented paired binaries.

## Lua JaiDOOM

The [JaiDOOM Lua port](../../jaidoom/ports/PORTING.md) adds jai_doom_lua and its padding control to the comparison build. --engines jai-lua selects JaiScript and Lua 5.4.8; --engines jai retains the existing compiler-campaign behavior. With --workloads timedemo, the two languages interleave inside each normal/padding layout, twice per round.

Before accepting a Lua timedemo point, the runner generates or validates a full cold/warm frame certificate. --timedemo-conformance PATH reuses an existing certificate only when the recorded scripts, data and Lua binaries still match. The certificate includes both JaiScript backends and both Lua layouts, with 7968 per-tic state/frame records per configuration. All four traces must agree, including the pinned cold/warm frame hashes. Trace runs are excluded from timing.

The competitor screen now also recognizes jai_doom_* and reference_host processes. For the data, conversion and headless-scope rules, use the port's own documentation.
