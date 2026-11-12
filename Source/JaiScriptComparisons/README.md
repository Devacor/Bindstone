# JaiScript benchmarks

JaiScript against Lua 5.4 and 5.5, LuaJIT, Luau, Squirrel 3.2, ChaiScript 6.1, AngelScript 2.38 and CPython: the same
programs ported to each runtime, every port held to the JaiScript original's output hash. The results are on the
JaiScript site's performance page; its headline table is at
<https://jaiscript.com/embedding/11-performance.html#measured-results>.

The published numbers were measured on Windows with MSVC (x64, Release, whole-program optimization) on JaiScript
v0.5.4, the release the `jaiscript/` submodule pins. The runners pin processes with Windows affinity masks, so timing
runs are Windows-only; the ports themselves are portable C++ and script.

## What is compared

| folder | programs | runtimes |
| --- | --- | --- |
| `demoreel/` | the demoreel's three heaviest scenes (OSSUARY, MAELSTROM, GLOBHUNTER), one shared terminal host | JaiScript VM (serial, 8 workers), Lua 5.4 + sol2, Squirrel, ChaiScript, Python |
| `gloom/` | GLOOM, a terminal raycaster: the whole game's simulation and renderer on one shared host | JaiScript VM (serial, 8 workers), Lua 5.4 and 5.5 + sol2, LuaJIT, Squirrel, AngelScript, ChaiScript, Python |
| `jaidoom/` | JaiDOOM's 3984-tic recorded timedemo, headless, frame-checked tic by tic | JaiScript VM and interpreter, Lua 5.4 |
| `gatorquest/` | the Gator Quest smoke simulation (world generation, mobiles, combat, bots) | JaiScript VM, Lua 5.4/5.5, LuaJIT, Luau |
| `apps/` | Richards, DeltaBlue, a bytecode VM and a spatial-hash simulation | JaiScript VM and interpreter, Lua 5.4, Squirrel, AngelScript, CPython |
| `kernels/` | the sixteen interpreter kernels of daScript's dasProfile, typed and dynamic | JaiScript VM and interpreter, Lua 5.5, LuaJIT, AngelScript |
| `benchmarks/` | `comparison_benchmarks`: microbenchmark suites in one process | JaiScript, Lua (sol2), Squirrel, ChaiScript, AngelScript |

The folders' READMEs state each protocol: what one sample is, warm-up, interleaving, pinning, the hash gate.

## Getting the sources

```
git clone --recursive https://github.com/Devacor/jaiscript_benchmarks.git
cd jaiscript_benchmarks
python gloom/ports/prepare_comparison.py
```

JaiScript is found by one rule, in the build and in every runner (`comparison_paths.cmake`, `comparison_paths.py`):
the `JAISCRIPT_DIR` environment variable if set, else the `jaiscript/` submodule, else a JaiScript tree beside this
folder (`../JaiScript`). `-DJAISCRIPT_DIR=<dir>` overrides it for the build.

None of the compared runtimes ship here. `prepare_comparison.py` downloads each pinned archive into `out/deps`, checks
its SHA-256, applies the two source patches the measurements used (sol2's Lua 5.5 version guards;
`gloom/ports/chaiscript-6.1.0.patch`, a `null` literal and a file loader hook for ChaiScript) and copies the trees the
build reads by default into `third_party/` (see `third_party/README.md`):

| runtime | pin |
| --- | --- |
| Lua 5.4.8, Lua 5.5.1 | lua.org release tarballs |
| LuaJIT 2.1 | `24c20c94e7db195b640854619577441f9b4bc6be` |
| sol2 | v3.3.0 |
| Squirrel 3.2 | `f77074bdd6152d230609146a3d424c6f49e3770f` |
| ChaiScript | v6.1.0 |
| AngelScript | the 2.38.0 SDK |

The configure step looks for each runtime and prints what it found. A microbenchmark suite whose runtime is missing
compiles to a skip; a port option that is ON with its sources missing is a configure error. Each root is a cache path:

| comparison | default location | variable |
|---|---|---|
| Lua 5.4 + sol2 | `third_party/lua`, `third_party/sol2` | `COMPARISONS_THIRD_PARTY` |
| Squirrel | `third_party/squirrel` | `COMPARISONS_THIRD_PARTY` |
| ChaiScript | `third_party/chaiscript` | `COMPARISONS_CHAISCRIPT_ROOT` |
| AngelScript | `third_party/angelscript` | `COMPARISONS_ANGELSCRIPT_ROOT` |

## Building

From an x64 Visual Studio developer prompt (`VsDevCmd.bat -arch=x64`), with CMake and Ninja:

```
cmake -S . -B out/release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DDEMOREEL_PORT_LUA=ON -DDEMOREEL_PORT_SQUIRREL=ON -DDEMOREEL_PORT_CHAI=ON ^
      -DGLOOM_PORT_LUA=ON -DGLOOM_PORT_SQUIRREL=ON -DGLOOM_PORT_CHAI=ON -DGLOOM_PORT_ANGELSCRIPT=ON ^
      -DAPPS_PEERS=ON -DANGELSCRIPT_PEERS=ON
cmake --build out/release
```

JaiScript builds inside it as a subproject (static runtime, whole-program Release), and every program lands in
`out/release/bin` next to `jai_gloom`, `jai_demoreel` and `jaiscript`. The whole-program GLOOM and JaiDOOM gates have
their own pinned build with the Lua 5.5, LuaJIT, JaiDOOM and cold-function padding targets:
`gloom/ports/build_comparison.ps1` (see `gloom/ports/README.md`). Every script keeps its builds, dependencies and
evidence under `out/`.

## Running

| comparison | timed run | hash check only |
| --- | --- | --- |
| demoreel | `python demoreel/ports/run_comparison.py --output out/demoreel/<fresh folder>` | add `--conformance-only --no-lock` |
| GLOOM, JaiDOOM | `python gloom/ports/run_comparison.py --build out/release --output out/whole-program/<fresh folder>` | add `--conformance-only` |
| JaiDOOM frames | | `python jaidoom/ports/verify_conformance.py --build out/release --output out/<fresh folder>` |
| Gator Quest | `python gatorquest/lua/run_comparison.py --jai out/release/bin/jaiscript.exe --lua54 <lua.exe> --output <folder>` | `python gatorquest/lua/parity.py --jai out/release/bin/jaiscript.exe --lua <lua.exe>` |
| apps | `python apps/run.py` | `python apps/run.py --rounds 1 --no-lock` (every sample is hash-checked) |
| kernels | `python kernels/run.py` | `python kernels/run.py --runs 1 --no-lock` (the report lists every checksum) |
| microbenchmarks | `out/release/bin/comparison_benchmarks.exe` | |

The timing runners hold the machine-wide timing lock (JaiScript's `tools/ci/timing_lock.sh`, run through Git's bash)
unless told otherwise, so two timing runs never overlap. JaiDOOM needs the shareware `doom1.wad`, which JaiScript's
`examples/jaidoom/bootstrap.bat` fetches into the submodule. Gator Quest takes its interpreters as arguments
(`out/release/bin/apps_lua.exe` is Lua 5.4.8's own `lua.c`). The runner unit tests are
`python -m unittest discover -s demoreel/ports/tests` and `python -m unittest discover -s gloom/ports/tests`.

## Layout

| folder | what |
| --- | --- |
| `benchmarks/` | `comparison_benchmarks`: Foundry suites timing JaiScript against AngelScript, ChaiScript, Lua (sol2) and Squirrel in one process |
| `gloom/` | the GLOOM ports (AngelScript, ChaiScript, Squirrel, Lua 5.4/5.5, LuaJIT, Python) and their runners; `gloom/ports/README.md` holds the adapter contract |
| `demoreel/` | the demoreel's three heaviest scenes ported to Lua, Squirrel, ChaiScript and Python, their adapter contract (`README.md`) and the scene-set runner (`ports/COMPARISON.md`) |
| `jaidoom/` | the headless Lua JaiDOOM port, its transpiler and conformance checker |
| `gatorquest/` | the Lua, LuaJIT and Luau twins of the Gator Quest smoke simulation |
| `kernels/` | the kernel suite in JaiScript, Lua and AngelScript, its hosts and `run.py`, the cross-language runner |
| `apps/` | the benchmark apps in JaiScript, Lua, Squirrel, AngelScript and Python, the hash-checked runner and the `apps_lua`/`apps_sq` interpreters (`APPS_PEERS`); see `apps/README.md` |
| `angelscript/` | `angelscript_run`, the stand-alone AngelScript host of the apps and kernels (`ANGELSCRIPT_PEERS`) |
| `third_party/` | where the fetched runtime sources go |

## License

The benchmarks and ports are MIT-licensed (`LICENSE`). The runtimes they are compared with, fetched into `out/deps` and
`third_party/`, keep their own licenses: Lua, LuaJIT, sol2 and Squirrel are MIT, ChaiScript BSD-3-Clause, AngelScript
zlib. The kernel suite's Lua scripts (`kernels/lua`) are daScript's dasProfile scripts and keep dasProfile's license.
