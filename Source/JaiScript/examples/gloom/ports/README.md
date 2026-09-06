# GLOOM runtime comparisons

The ports execute the shared host's deterministic 100x40, quad-pixel workload.
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
directory under `Source/JaiScript/out`. Pass its `release` child to the runner's
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
retained in the raw JSON but excluded from summary statistics.

Each reported median carries the padding control median, the range of paired
padding deltas and the observed run range. The padding band includes normal
run-to-run variation, ASLR and runtime hash-seed variation; it is not a pure
measurement of one compiler mechanism.

`metadata.json` records source and executable hashes. `runs.json` records every
command, output filename, hash verdict, CPU affinity and background CPU reading.
`summary.json` contains only accepted runs. A port that fails conformance has
no timing result.
