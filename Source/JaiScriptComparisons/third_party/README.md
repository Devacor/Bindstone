# third_party

Source trees the comparison builds compile from. They are not committed; `python gloom/ports/prepare_comparison.py`
fetches the pinned, SHA-256-checked archives into `out/deps` and copies each tree here (a folder that already exists is
kept):

| folder | what | used by |
| --- | --- | --- |
| `lua/` | Lua 5.4.8 (the folder holding `src/lua.h`) | `comparison_benchmarks`, `gloom_lua`, `demoreel_lua`, `jai_doom_lua`, `apps_lua` |
| `sol2/` | sol2 3.3.0 with its Lua 5.5 version guards patched (the folder holding `include/sol/sol.hpp`) | `comparison_benchmarks`, the Lua ports |
| `squirrel/` | Squirrel 3.2 at `f77074bd` (the folder holding `include/squirrel.h`) | `comparison_benchmarks`, `gloom_squirrel`, `demoreel_squirrel`, `apps_sq` |
| `chaiscript/` | ChaiScript 6.1.0 with `gloom/ports/chaiscript-6.1.0.patch` (the folder holding `include/chaiscript/chaiscript.hpp`) | `comparison_benchmarks`, `gloom_chai`, `demoreel_chai` |
| `angelscript/` | the AngelScript 2.38.0 SDK (the folder holding `angelscript/include/angelscript.h` and `add_on/`) | `comparison_benchmarks`, `angelscript_run`, `gloom_angelscript` |

Lua 5.5.1 and LuaJIT stay in `out/deps`: `gloom/ports/build_comparison.ps1` passes them to the build, and the kernel
suite's `kernels/host/build_peers.cmd` takes them through `LUA_SRC` and `LUAJIT_SRC`. Each tree keeps its own license
(Lua, sol2, Squirrel, LuaJIT: MIT; ChaiScript: BSD-3-Clause; AngelScript: zlib).
