# third_party

Source checkouts the comparison builds compile from. They are not committed (the repository ignores them); place them here:

| folder | what | used by |
| --- | --- | --- |
| `lua/` | Lua 5.4 sources (the folder holding `src/lua.h`) | `comparison_benchmarks`, `gloom_lua`, `demoreel_lua`, `jai_doom_lua` |
| `sol2/` | sol2 3.x (the folder holding `include/sol/sol.hpp`) | `comparison_benchmarks`, the Lua ports |
| `squirrel/` | Squirrel 3.2 (the folder holding `include/squirrel.h`) | `comparison_benchmarks`, `gloom_squirrel`, `demoreel_squirrel` |

ChaiScript 6.1.0 stays in `Bindstone/External/ChaiScript-6.1.0` (`COMPARISONS_CHAISCRIPT_ROOT` points elsewhere). The pinned
Lua 5.4.8, Lua 5.5.1, LuaJIT and sol2 archives of the whole-program comparison are fetched by
`gloom/ports/prepare_comparison.py` instead (see `gloom/ports/README.md`).
