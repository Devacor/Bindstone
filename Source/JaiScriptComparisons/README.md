# JaiScriptComparisons

JaiScript against ChaiScript, Lua, LuaJIT, Luau and Squirrel. Everything comparison-related lives here, so the
JaiScript build carries none of it: this project adds `../JaiScript` as a subdirectory, and nothing in JaiScript refers
back here.

## Path map (from the JaiScript tree)

| was (under `Source/JaiScript/`) | now (under `Source/JaiScriptComparisons/`) |
| --- | --- |
| `lua/`, `sol2/`, `squirrel/` (local checkouts) | `third_party/lua/`, `third_party/sol2/`, `third_party/squirrel/` (move yours) |
| `source/tests/performance/{chaiscript,sol2,squirrel}_comparison.cpp` | `benchmarks/` (target `comparison_benchmarks`) |
| `examples/gloom/ports/`, `gloom_adapter_{chai,lua,squirrel}.cpp` | `gloom/ports/`, `gloom/` |
| `examples/demoreel/ports/`, `demoreel_adapter_{lua,squirrel}.cpp`, the README's Porting section | `demoreel/ports/`, `demoreel/`, `demoreel/README.md` |
| `internal/examples/demoreel/ports/COMPARISON.md`, `internal/examples/jaidoom/ports/PORTING.md` | `demoreel/ports/`, `jaidoom/ports/` |
| `examples/jaidoom/ports/` | `jaidoom/ports/` |
| `examples/gatorquest/lua/` | `gatorquest/lua/` |
| `bench/kernels/lua/`, `run.py` | `kernels/` |
| `bench/kernels/host/lua_kernel_host.c`, `addOne.c` | `kernels/host/` |
| `bench/kernels/host/build.cmd` (peers + JaiScript host) | `kernels/host/build_peers.cmd` (Lua, LuaJIT) and `JaiScript/bench/kernels/host/build.cmd` (the JaiScript host) |
| `bench/kernels/bin/{lua_kernel_host.exe,luajit.exe,addOne.dll}` | `kernels/bin/` (move yours; `jai_kernel_host.exe` stays in `JaiScript/bench/kernels/bin`) |
| `out/astra/` (the comparison scripts' builds, dependencies and evidence) | `out/` |
| `-DGLOOM_PORT_*`, `-DDEMOREEL_PORT_*`, `-DDEMOREEL_PADDING_CONTROLS`, `-DGLOOM_COMPARISON_PADDING_CONTROLS`, `-DJAIDOOM_PORT_*` on the JaiScript build | the same options on this project (`-S Source/JaiScriptComparisons`) |

Unchanged: the games (`JaiScript/examples/*`), `JaiScript/bench/kernels/jai/` and `JaiScript/bench/kernels/host/jai_kernel_host.cpp`.

## Layout

| folder | what |
| --- | --- |
| `benchmarks/` | `comparison_benchmarks`: Foundry suites timing JaiScript against ChaiScript, Lua (sol2) and Squirrel in one process |
| `gloom/` | the GLOOM ports (ChaiScript, Squirrel, Lua 5.4/5.5, LuaJIT, Python) and their runners; `gloom/ports/README.md` holds the adapter contract |
| `demoreel/` | the demoreel ports (Lua, Squirrel), their adapter contract (`README.md`) and the OSSUARY comparison runner |
| `jaidoom/` | the headless Lua JaiDoom port, its transpiler and conformance checker |
| `gatorquest/` | the Lua, LuaJIT and Luau twins of the Gator Quest smoke session |
| `kernels/` | the kernel suite's Lua side and `run.py`, the cross-language runner |
| `third_party/` | where the Lua, sol2 and Squirrel checkouts go |

## Dependencies

None of the compared languages ship here. Supply the sources yourself; the configure step looks for each one, prints
what it found, and skips a benchmark comparison whose sources are missing (all four libraries are MIT-licensed):

| comparison | expected location | variable |
|---|---|---|
| Lua 5.4 + sol2 | `third_party/lua/src/lua.h` and `third_party/sol2/include/sol/sol.hpp` | `COMPARISONS_THIRD_PARTY` |
| Squirrel 3 | `third_party/squirrel/include/squirrel.h` | `COMPARISONS_THIRD_PARTY` |
| ChaiScript 6.1.0 | `include/chaiscript/chaiscript.hpp` under the given folder | `COMPARISONS_CHAISCRIPT_ROOT` |

Both variables are cache paths, so `-DCOMPARISONS_THIRD_PARTY=<dir>` or `-DCOMPARISONS_CHAISCRIPT_ROOT=<dir>` points
elsewhere. The GLOOM and demoreel ports read the same locations behind their own options (`GLOOM_PORT_LUA`,
`GLOOM_PORT_SQUIRREL`, `GLOOM_PORT_CHAI`, `DEMOREEL_PORT_LUA`, `DEMOREEL_PORT_SQUIRREL`, all OFF); unlike the
benchmarks, a port option that is ON with its sources missing is a configure error rather than a skip.

## Building

```
cmake -S Source/JaiScriptComparisons -B Source/JaiScriptComparisons/out/release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DGLOOM_PORT_LUA=ON -DGLOOM_PORT_SQUIRREL=ON -DDEMOREEL_PORT_LUA=ON -DDEMOREEL_PORT_SQUIRREL=ON
cmake --build Source/JaiScriptComparisons/out/release --target comparison_benchmarks gloom_lua demoreel_lua
```

JaiScript builds inside it with its in-tree layout (static runtime, whole-program Release); every program lands in the
build's `bin/` next to `jai_gloom`, `jai_demoreel` and `jaiscript`. The whole-program GLOOM/JaiDoom comparison has its
own pinned build (`gloom/ports/build_comparison.ps1`, see `gloom/ports/README.md`); every script keeps its builds, dependencies and evidence under `out/`; the OSSUARY runner is described in
`demoreel/ports/COMPARISON.md`, the kernel suite in `kernels/README.md`.
