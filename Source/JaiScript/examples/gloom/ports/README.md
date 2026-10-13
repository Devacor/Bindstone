# GLOOM ports: porting guide and whole-program gates

This page covers porting GLOOM to another embedded scripting language for the
cross-language comparison, then the whole-program gates that time the ports
against JaiScript.

## Porting

**[reference/README.md](../reference/README.md) is the normative game/host
spec** (tick order, RNG contract, STATE_HASH recipe, checkpoint hashes); this
section covers the mechanics: the C++ adapter contract, the CMake wiring, the
measurement protocol, and the feedback rubric each porter fills in.

The JaiScript implementation is the reference: `scripts/*.jai` for the game,
`gloom_adapter_jai.cpp` for the adapter shape (paths in this section are
relative to `examples/gloom/`).

### 1. Architecture: shared host + per-language adapter

```
gloom_host.hpp / gloom_host.cpp     the SHARED host (no scripting runtime deps)
gloom_adapter_jai.cpp               JaiScript adapter + main()   -> jai_gloom
gloom_adapter_chai.cpp              ChaiScript adapter + main()  -> gloom_chai
gloom_adapter_squirrel.cpp          Squirrel adapter + main()    -> gloom_squirrel
gloom_adapter_lua.cpp               Lua/sol2 adapter + main()    -> gloom_lua
scripts/                            the JaiScript game (reference)
ports/<lang>/scripts/               your game scripts (you create this)
```

**The shared host owns** (do not reimplement any of this): the VT console
(alt-screen, truecolor, UTF-8), held-key input (`GetAsyncKeyState`,
foreground-gated) and edge-key events, frame timing/pacing, the `gloom_rng`
xorshift64\* struct, all `--flags` (`--smoke --ticks --workers --dump-frame
--bench --seed --god --pix --w/--h --fps --scripts`), the frame-stream FNV-1a
hash, the smoke parity table, and the bench sim/draw split. One host = one
measurement harness = comparable numbers.

**Your adapter owns**: creating the script runtime, binding the host API
(section 3), loading your scripts, and forwarding six entry-point calls.

**Your scripts own**: everything else — the whole game per reference/README.md
sections 1 and 4 (sim, raycaster, sub-cell renderer, HUD, autopilot). The host
only blits the string your `gloom_frame` returns.

### 2. The adapter contract (`gloom_host.hpp`)

Implement `gloom::script_adapter` (factory + metadata) and
`gloom::script_session` (one booted runtime). `main()` is two lines: construct
your adapter, `return gloom::run_gloom(argc, argv, adapter);`.

`script_adapter`:

| virtual | guarantee |
|---|---|
| `program_name()` | exe name used in messages, e.g. `"gloom_lua"` |
| `language()` | human name for titles, e.g. `"Lua/sol2"` |
| `backends()` | runtimes `--smoke` runs (in order) and `--backend` accepts. Single-runtime ports return one name, e.g. `{"lua"}`. JaiScript returns `{"interpreter","vm"}`. |
| `default_backend()` | default for interactive mode; defaults to `backends().back()` |
| `normalize_backend(b)` | optional alias mapping (jai: `"interp"` → `"interpreter"`) |
| `make_session(backend, services, opt, live_input)` | fresh engine + all host-API bindings; scripts NOT yet loaded. Called once per interactive run, once per backend per smoke run. `live_input` is false in smoke/bench — but `services.key_down` is already gated, so binding it unconditionally is correct. |

`script_session`:

| virtual | when called | must guarantee |
|---|---|---|
| `load_scripts()` | once, right after `make_session` | resolve the scripts dir (use `gloom::locate_scripts_dir(opt.scripts_dir, opt.argv0, GLOOM_SOURCE_SCRIPTS_DIR, "gloom_scripts_<lang>", "main.<ext>")`) and load/parse/execute the top level of your scripts. Throw `std::exception` on failure. |
| `force_autopilot()` | after load, before boot, only for `--bench` | call script `gloom_force_autopilot()` |
| `boot(w, h)` | once after load | call script `gloom_boot(w, h)` (console cols, rows) |
| `frame(dt, key, fps, ms_sim, ms_draw)` | every frame | call script `gloom_frame(...)` and return the FULL frame string (rows joined by `\n`). Smoke passes `key=""` and zeros for the perf floats. This string is hashed byte-for-byte — see reference/README.md section 3.3. |
| `state_hash()` | smoke end | script `gloom_state_hash()` → int64. THE conformance number. |
| `wants_quit()` | per interactive frame | script `gloom_wants_quit()` |
| `summary()` | process/backend end | script `gloom_summary()` (prints via `host_log`) |
| `stack_trace()` | after any of the above throws | script backtrace of the last error, `""` if unavailable |

Error model: script failures surface as `std::exception` throws out of the
session methods; the host prints `<prog>: script error: ...` (smoke wraps with
the failing tick + backend and appends `stack_trace()`).

### 3. The host API your adapter must bind (reference/README.md section 2)

Bind ALL of these before `load_scripts()` returns; the game scripts assume
they exist at parse/boot time.

- **`Rng` class** — bind the shared `gloom::gloom_rng` struct exactly:
  constructor `(int seed)`, methods `next(n) roll(lo,hi) chance(p) nextf()
  state()`. Do NOT reimplement it in script or with your language's RNG; the
  STATE_HASH folds `RNG.state()` every tick, so any deviation diverges
  immediately.
- **`host_log(string)`** — forward to `services.log` (line to stdout, never
  hashed).
- **`key_down(string) -> bool`** — forward to `services.key_down` (already
  smoke/bench-gated; key names in reference/README.md section 2).
- **`itrunc(float) -> int`** (C-cast truncation) and **`ifloor(float) -> int`**
  (floor). Bind even if your language has native equivalents — scripts may use
  either, and truncation-toward-zero semantics must match C.
- **`utf8(int) -> string`** — forward to `gloom::utf8_encode` (block glyphs
  incl. astral U+1FB00 sextants).
- **`ESC`** — the escape character as a 1-char string global.
- **Globals** `HOST_SEED HOST_SMOKE HOST_TICKS HOST_WORKERS HOST_GOD
  HOST_BACKEND HOST_PIX` from `opt` (`HOST_PIX` via
  `gloom::pix_mode_index(opt.pix)`; `HOST_BACKEND` = the backend name passed to
  `make_session`).

### 4. What your scripts must implement

The whole of reference/README.md sections 1 (game spec), 3.2 (STATE_HASH), and 4
(renderer + particle pool), exporting the six entry points of section 2.
Non-negotiables that define conformance:

- RNG consumption order (section 1.3 tick order + call sites) — the hash
  bisects any divergence to the first differing tick.
- IEEE-754 double math evaluated in source order; integer division truncates
  toward zero.
- The autopilot (section 1.12) — `--smoke`/`--bench` drive the game through it.
- Frame bytes per section 4.2 if you want frame-hash comparability (strongly
  recommended; it catches renderer drift the sim hash can't see).
- `--workers` is a JaiScript `parallel_transform` detail; **ports run the
  serial-equivalent loops always** (outputs are defined identical). Bind
  `HOST_WORKERS` anyway (the HUD may display it).

Conformance gate — all four, on your single backend:

| `--smoke --ticks N` (seed 666, quad) | STATE_HASH |
|---|---|
| 300 | 3580805725 |
| 2000 | 319812559 |
| 3000 | 4080154357 |
| 16000 | 3497451110 |

Cross-checks: seeds 7/99/4242 at 3000 → 1696980843 / 1855347375 / 2848371116;
`--god --seed 5` at 12000 → 576425398. Sim hashes are `--pix`-independent
(run half/quad/sext to prove your renderer doesn't touch sim state or RNG).

### 5. CMake wiring

`jai_gloom` always builds; each port is an option, OFF by default, and its
runtime need not exist when OFF:

| option | target | runtime location (checked at configure) |
|---|---|---|
| `-DGLOOM_PORT_CHAI=ON` | `gloom_chai` | `External/ChaiScript-6.1.0` two levels above the package root (header-only) |
| `-DGLOOM_PORT_SQUIRREL=ON` | `gloom_squirrel` | `squirrel/` at the package root (built as `gloom_squirrel_runtime`) |
| `-DGLOOM_PORT_LUA=ON` | `gloom_lua` | `lua/` + `sol2/` at the package root (Lua built as `gloom_lua_runtime`) |

Port targets link `gloom_host` + their runtime only — NOT `jaiscript`. Keep it
that way: the shared core must never gain a scripting-runtime dependency, and
no `#ifdef` in `gloom_host.*` may reference any runtime.

A new port's option block sets `GLOOM_SOURCE_SCRIPTS_DIR` to
`ports/<lang>/scripts` and adds a POST_BUILD `copy_directory` to
`$<TARGET_FILE_DIR:...>/gloom_scripts_<lang>` mirroring `jai_gloom`'s.

### 6. Measurement protocol (identical for every port)

Same machine, quiet, Release/optimized build, Windows Terminal, 100x40.
Numbers reported per reference/README.md section 3.4:

1. **Conformance**: `--smoke --ticks {300,2000,3000,16000}` hash table above,
   plus the seed cross-checks. Exit code 0 required (single-backend ports get
   parity trivially; the exit still gates on the run completing).
2. **Headless perf**: `--smoke --ticks 300` ms/tick for `--pix half`, `quad`,
   `sext` (3 rows). Single 300-tick runs; treat ±10% as noise, rerun if the
   machine was loaded.
3. **Interactive split**: `--bench 300` sim/draw ms per frame (quad).
4. Optional ablation for stage-level parity with the JaiScript numbers:
   sim-only / rays / paint / rows, measured by early-returning the render
   pipeline (see [the GLOOM README](../README.md)).

The whole-program gates below are the timed comparison.

### 7. Feedback rubric (fill in per port)

One section per language, same headings, so the comparison writes itself:

```markdown
## <Language> port notes

- Binding experience: (dynamic_binder-equivalent ergonomics; Rng/class binding,
  free functions, globals; lines of C++ adapter code; anything that fought you)
- Porting friction: (language features that mapped poorly — coroutine brains,
  typed truncation, value vs reference semantics, string building; workarounds
  chosen and why)
- LOC: (script total + per-file table like reference/README.md section 5's)
- Time-to-first-running: (wall hours to: skeleton boots / first clean smoke 300
  / full 16000 conformance)
- Debugging story: (what a hash divergence bisect felt like; error messages,
  stack traces, tooling)
- Perf: (the section 6 tables)
- Net: (one paragraph — where the language earned or lost its keep)
```

Honesty rules: engine-bug detours excluded from the hours, language friction
included; note anything you had to change in shared files (should be nothing).

## Whole-program gates

The ports execute the shared host's deterministic 100x40, quad-pixel workload.
Squirrel 3.2 uses the bundled runtime with 64-bit integers and `SQUSEDOUBLE`,
and the existing scripts in `squirrel/scripts`. Its normal and padding builds
run the same conformance gates and timing protocol as the Lua configurations.
Lua 5.4.8 and Lua 5.5.1 use the same Lua scripts and the same sol2 adapter.
LuaJIT 2.1 JIT uses the same adapter with the overrides in `luajit/scripts`;
unmodified modules come from `lua/scripts`.

LuaJIT JIT is a runtime-code-generation ceiling, not a shipping target for
platforms that forbid runtime code generation (iOS).

### Build on Windows

Use Python 3.11 or newer, CMake, Ninja, and an x64 Visual Studio Native Tools
environment. From the JaiScript package root:

```powershell
powershell -ExecutionPolicy Bypass -File examples/gloom/ports/build_comparison.ps1
python examples/gloom/ports/run_comparison.py
```

All downloaded sources, object files, binaries and evidence stay under the
scripts' git-ignored output root inside the package's `out/` directory (their
default). `build_comparison.ps1 -OutputRoot` accepts another directory inside
that root. Pass its `release` child to the runner's `--build` option and an
evidence directory inside the same root to `--output`.

`prepare_comparison.py` pins archive SHA256 hashes for Lua 5.4.8, Lua 5.5.1,
sol2 3.3.0, and LuaJIT JIT branch `v2.1` at
`24c20c94e7db195b640854619577441f9b4bc6be`. The script patches only its isolated sol2 dependency copy: it admits
Lua version 505, supplies the removed error-code fallback, and passes Lua 5.5's
seed argument in the custom-allocator state constructor. The actual Lua 5.5
header and version number remain visible. The adapter uses `luaL_newstate`
through sol2's ordinary default constructor.

### Workload and validation

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

### Placement controls and evidence

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

### JaiDOOM timedemo

The default command runs both whole programs. JaiDOOM includes the Lua 5.4.8 port as well as JaiScript. For JaiScript increments, omit
the rival configurations with:

```powershell
python examples/gloom/ports/run_comparison.py --engines jai --workloads gloom,timedemo --rounds 3 --build <output root>/release --output <output root>/<evidence directory>
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
GLOOM then receives an explicit `--scripts` path as well. This keeps live
script edits out of a measurement window.

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

### Sampled CPU profiles on Windows

Build all normal and padding targets with symbols and markers:

```powershell
powershell -ExecutionPolicy Bypass -File examples/gloom/ports/build_comparison.ps1 -OutputRoot <output root>/gates-profile -ProfileMarkers
```

Run `capture_profile.ps1` in Administrator PowerShell. It calls the same
combined runner with `--engines jai --workloads gloom,timedemo --rounds 3`,
including the gates, warm-ups, CPU pinning and padding controls. Parameters
`-BuildRoot`, `-Rounds`, `-Workloads` and `-MaxBackgroundCores` select
the build and capture. ETL, logs and JSON go under
`<output root>/cpu-captures/TIMESTAMP`. Existing WPR recordings are left alone.
Recorded timings are labelled `WPR` and must not be used as uninstrumented
performance results.

Provider `744b3f90-7d83-4b7b-a861-40637623c592` emits paired begin/end
markers. GLOOM markers delimit its measured loop, excluding loading, warm-up
and summaries. JaiDOOM markers delimit the repeated `main()` call, including
that call's level/demo setup; its printed ms/tic still measures only the replay
loop. Filter samples by PID and marker interval and state which interval was
used. Provider registration belongs to each execution; normal builds compile
out the marker code.

### Recovering an interrupted timing session

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
`gloom_lua` or `jaiscript`. Other processes are covered only by the
background CPU screen and the coordinated quiet window.

CPU trace analysis additionally needs Windows Performance Toolkit (`xperf`)
and matching PDBs.

### VM function/line cycle profiles

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

### Native CPU attribution

`capture_profile.ps1` accepts `-JaiWorkloadRoot` for frozen scripts and
`-OutputRoot` for a new capture directory inside the output root. It refuses an
existing output directory before starting WPR. Use a build made with
`-ProfileMarkers`; recording requires an elevated PowerShell session.

The marker build writes one `[profile-cpu]` record per measured span, with
process and calling-thread user/kernel CPU deltas in 100-nanosecond units.
These bracket the existing begin/end markers and include their small overhead.
Windows accounting is quantized, so pool repeated spans and report that
resolution alongside per-capture variation. Keep CPU time separate from elapsed
time and report additional-thread CPU separately. Profiled wall times are
capture diagnostics; speed claims use the uninstrumented paired binaries.

### Lua JaiDOOM

The [JaiDOOM Lua port](../../jaidoom/ports/README.md) adds jai_doom_lua and its padding control to the comparison build. --engines jai-lua selects JaiScript and Lua 5.4.8; --engines jai runs JaiScript alone. With --workloads timedemo, the two languages interleave inside each normal/padding layout, twice per round.

Before accepting a Lua timedemo point, the runner generates or validates a full cold/warm frame certificate. --timedemo-conformance PATH reuses an existing certificate only when the recorded scripts, data and Lua binaries still match. The certificate includes both JaiScript backends and both Lua layouts, with 7968 per-tic state/frame records per configuration. All four traces must agree, including the pinned cold/warm frame hashes. Trace runs are excluded from timing.

The competitor screen also recognizes jai_doom_* and reference_host processes. For the data, conversion and headless-scope rules, use the port's own documentation.
