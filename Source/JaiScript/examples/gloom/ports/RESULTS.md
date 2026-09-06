# GLOOM comparison snapshot: 2026-09-06

Source increment: `b79fbc60cf235f6e676a7c3499699676ba92e1f7`.
Intel Core Ultra 9 285HX, 24 physical/logical processors, Windows x64,
High Performance power plan. Serial affinity is performance CPU 0; eight-worker
affinity is CPUs 0, 1, 10, 11, 12, 13, 22 and 23. The region worker count includes
the calling thread. Dimensions: 100x40, quad pixels; seed: 666.

## Warmed results

| Runtime | P cores | Normal median ms/tick | Padding median ms/tick | Paired padding band | Normal observed range ms/tick |
|---|---:|---:|---:|---|---|
| JaiScript VM serial | 1 | 2.388277 | 2.379183 | -1.11% to +0.62% | 2.384786 to 2.397669 |
| JaiScript VM 8 workers | 8 | 2.024679 | 2.022994 | -1.77% to +0.13% | 2.019064 to 2.048779 |
| Lua 5.4.8 | 1 | 0.384455 | 0.387616 | -4.18% to +1.02% | 0.382099 to 0.405084 |
| Lua 5.5.1 | 1 | 0.395992 | 0.392296 | -4.11% to -0.01% | 0.395717 to 0.404249 |
| LuaJIT 2.1 JIT | 1 | 0.101952 | 0.092693 | -18.83% to +4.60% | 0.090147 to 0.118107 |

Each median has six accepted runs per binary: three interleaved A B A B
rounds per configuration. A is normal; B has 40 retained volatile statements
in the cold help function, which is never executed by smoke. The paired band
is the range of `100 * (B/A - 1)` across neighboring accepted pairs.
It includes code placement and run variation. The executable code sections
differ; their sizes and SHA256 are in `text-sections.json`.

All configurations run 600 measured ticks after 600 untimed warm-up ticks
and a reboot of game state in the same runtime. Loading, boot, warm-up and
final summaries are excluded. Script execution, frame-string production and
the shared host's frame hash are included.

The machine had resident desktop activity. Zero-background measurements were
not obtained. The runner paused for detected build, test and other GLOOM
processes. Background CPU was screened at 1.0 core-equivalent across 24 cores;
28 timed attempts were excluded. The initial 0.20-core attempt did not
obtain a complete measurement window. This runner never overlapped its
benchmark processes.

Lua 5.4.8 and Lua 5.5.1 have overlapping observed ranges. These data do not
establish a speed ranking between them. LuaJIT JIT's wider normal-build range
and padding band do not support attributing its padding delta to a runtime
change. This snapshot is a desktop reference for the typed-regions campaign.

## Versions and compiler flags

| Component | Version / source |
|---|---|
| Lua 5.4 | 5.4.8, official lua.org archive |
| Lua 5.5 | 5.5.1, official lua.org archive |
| LuaJIT JIT | 2.1.1788460057, v2.1 at `24c20c94e7db195b640854619577441f9b4bc6be`; branch tip resolved 2026-09-06 |
| Binding | sol2 3.3.0, isolated Lua 5.5 compatibility patch from `prepare_comparison.py` |
| Compiler | MSVC 19.51.36252 x64; VS toolset directory 14.51.36231 |
| Tools | CMake 4.3.2, Ninja 1.13.2, Python 3.11.0 |

JaiScript, the shared host, adapters and stock Lua runtimes: Release,
`/O2 /Ob2 /Oi /Ot /GL /Zi /DNDEBUG /MT`. C++ uses C++20,
`/EHsc /permissive-`. Linking uses
`/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`.

The LuaJIT JIT native runtime uses the upstream
`msvcbuild.bat lua52compat static` recipe: `/O2 /Zi`, x64 GC64,
FFI and JIT enabled, Lua 5.2 compatibility, static library. Its C++ adapter
and final executable use the same Release flags as the other configurations.
JIT settings are runtime defaults. The native runtime itself is not built
with LTCG.

LuaJIT JIT is a runtime-code-generation ceiling. Bindstone's iOS
configuration cannot use runtime code generation; it is not a shipping target.

## Conformance and validation

| Ticks | Seed | Frame-stream hash | State hash |
|---:|---:|---|---:|
| 300 | 666 | `7fecc09815bb64e0` | 3580805725 |
| 600 | 666 | `dfd969ed5a336dca` | 1503537018 |

Every configuration and both binary variants passed cold 300, cold 600 and
warmed 600 checks. The interpreter independently passed the 300-tick
reference. All 60 accepted timed runs matched both 600-tick hashes. There
are no nonconforming ports in this snapshot.

Full Debug tests passed on both backends: 116 suites, 2739 tests each.
The isolated Windows build recipe was executed successfully. Lua 5.5 uses
the unchanged Lua 5.4 game scripts. LuaJIT JIT overrides preserve floor
division, bit operations, two-argument angle calculation and the FNV hash.
RNG state is transferred as exact uint64 cdata.

## Evidence and reproduction

Paths are relative to `Source/JaiScript`.

- `out/astra/comparison-desktop/metadata.json`: source/binary SHA256,
  processor topology, affinity and methodology.
- `out/astra/comparison-desktop/runs.json`: every command, output log,
  hash verdict, CPU reading and acceptance decision.
- `out/astra/comparison-desktop/summary.json`: medians, ranges and bands.
- `out/astra/comparison-desktop/text-sections.json`: executable code hashes.
- `out/astra/final-conformance/`: independent post-build hash gates.
- `out/astra/deps/sources.json`: archive SHA256, URLs and patches.
- `out/astra/reproduction-build.log`: executed build recipe.
- `out/astra/debug-interpreter.log` and `out/astra/debug-vm.log`: full tests.
- `out/astra/comparison/` and `out/astra/comparison-clean-runner.log`:
  rejected/unfinished measurement attempts.

From an x64 Visual Studio Native Tools environment, at the repository root:

```powershell
powershell -File Source/JaiScript/examples/gloom/ports/build_comparison.ps1
python Source/JaiScript/examples/gloom/ports/run_comparison.py --output Source/JaiScript/out/astra/comparison-desktop --max-background-cores 1.0
```

Choose a new output directory for another snapshot. The runner defaults to
0.20 background core-equivalents; this desktop snapshot explicitly used 1.0.
Compiler invocations are retained in the build logs and Ninja files.
