# demoreel ports

How to port the demoreel to another embedded scripting language for the
cross-language comparison. The JaiScript implementation is the reference:
`scenes/*.jai` for the reel, `demoreel_adapter_jai.cpp` for the adapter shape.
The pattern is GLOOM's (`../gloom/ports/README.md`): one shared host, a thin
adapter per language, the scripts own everything else.

## Architecture

```
demoreel_host.hpp / demoreel_host.cpp   the SHARED host (no scripting-runtime deps)
demoreel_adapter_jai.cpp                JaiScript adapter + main()  -> jai_demoreel
demoreel_adapter_<lang>.cpp             your adapter + main()       -> demoreel_<lang>
scenes/                                 the JaiScript reel (reference)
ports/<lang>/                           your scripts
```

**The host owns** (do not reimplement): the VT console (alt screen, UTF-8, the
SGR output filter and row-diff redraw), key polling, frame timing and pacing,
the `demo_rng` xorshift64\* struct, every `--flag`, the `--smoke` parity table
and its FNV-1a frame-stream hash, `--perf` (per-scene ms/frame and scene hash),
`--bench`, `--capture`, `--reload-test`, and TAB (cycle the live session,
re-synced through `scene_index`/`scene_time`/`jump`).

**Your adapter owns**: creating the runtime, binding the host API (below),
loading your scripts, forwarding the entry points (the `script_session` table).

**Your scripts own**: the reel: `Reel`, the presenter, palettes, transitions,
title cards, HUD, and all 17 scenes. The host only blits the string
`demo_frame` returns.

`main()` is two lines: construct your adapter, `return demoreel::run_demoreel(argc, argv, adapter);`.

## The adapter contract (`demoreel_host.hpp`)

`script_adapter`:

| virtual | guarantee |
|---|---|
| `program_name()` | exe name used in messages, e.g. `"demoreel_lua"` |
| `language()` | human name, e.g. `"Lua 5.4 / sol2"` |
| `backends()` | runtimes `--smoke`/`--perf` run in order and `--backend` accepts. Single-runtime ports return one name (`{"lua"}`); JaiScript returns `{"interpreter","vm"}` |
| `default_backend()` | defaults to `backends().back()` |
| `normalize_backend(b)` | optional alias (jai: `interp` -> `interpreter`) |
| `interactive_backends(opt)` | sessions the interactive loop keeps live (TAB cycles them); defaults to `backends()` |
| `parse_flag(argc, argv, i)` / `usage()` | optional adapter-only flags; return the number of args consumed, 0 = unknown |
| `make_session(backend, services, opt)` | a fresh runtime with every host-API binding in place; scripts NOT loaded yet |

`script_session`:

| virtual | script entry point | when / guarantee |
|---|---|---|
| `load()` | - | resolve the scenes dir (`demoreel::locate_scenes_dir(opt.scenes_dir, opt.argv0, <compiled-in source dir>, "demoreel_<lang>_scenes", "main.<ext>")`) and load every script. Throw on failure. The host times it. |
| `load_kind()` | - | 4-char tag for the smoke table (`prse`, `bite`, `lua`, ...) |
| `boot(w, h, seed, backend, smoke, hud)` | `demo_boot(w, h, seed, backend, smoke, hud)` | builds a fresh `Reel`; called again to restart (perf boots once per scene) |
| `frame(a)` | `demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) -> string` | the FULL frame, rows joined by `\n`; hashed byte for byte. Smoke/perf pass `key=""` and zero perf floats |
| `scene_index()` / `scene_time()` | `demo_scene_index()` / `demo_scene_time()` | TAB state sync |
| `scene_count()` / `scene_name()` | `demo_scene_count()` / `demo_scene_name()` | perf rows |
| `jump(idx, at_t)` | `demo_jump(idx, at_t)` | start scene idx at scene time at_t (stateful scenes replay up to 3 s) |
| `reload()` | re-execute scene files + `demo_reload()` | optional hot reload; return false when unsupported |
| `stack_trace()` | - | script backtrace of the last error, `""` if none |

Errors: script failures surface as `std::exception` throws; the host prints
`<prog>: script error: ...` (smoke/perf add the frame, scene and backend).

## Host API the scripts use

Bind all of these in `make_session`:

- **`Rng`**: the shared `demoreel::demo_rng`, exactly: constructor `(int seed)`,
  methods `next(n) roll(lo, hi) chance(p) nextf()`. Never reimplement it with
  your language's RNG; every scene seeds one (`seed + idx * 7919`), and the
  draws must happen in the reference's order.
- **`host_log(string)`**: forward to `services.log` (stdout, never hashed).
- **`itrunc(float) -> int`** (C cast: truncation toward zero) and
  **`ifloor(float) -> int`**. Plain script-language helpers are fine when the
  language has exact equivalents.
- **`ESC`**: the escape character as a one-character string global.

## What the scripts must reproduce for frame parity

- Same algorithms, same resolution, same operation order: IEEE-754 doubles
  evaluated in source order, the same libm calls (`sin cos sqrt atan2 log2 floor`).
- JaiScript `int` is int64 with C semantics: `/` truncates toward zero, `%`
  takes the dividend's sign, `>>` is arithmetic, a typed `int` slot truncates a
  float stored into it. Lua's `//` and `%` floor and its `>>` is logical, so a
  possibly-negative operand needs the truncating form.
- Ints print as digits; the reel never stringifies a float.
- `parallel_transform` bodies (donut/julia/lava/sponge/chrome/skull rows) are
  pure functions of their inputs: run them as serial loops, writing the grid
  directly (the row arrays exist for the parallel contract, not the picture).
- A class-typed or array-typed value copies on assignment and by-value
  parameters (C++ semantics); Lua/Squirrel tables alias. The reel only relies on
  this in POWDER's Rng parameter (above); everything else is read-only or `var&`.
- The presenter emits a color escape only when the palette index changes along a
  row and collapses uniform cells to a space or a full block (`util.jai hb_row`).

## CMake

`jai_demoreel` always builds. Each port is an option, OFF by default, linking
`demoreel_host` plus its runtime only, never `jaiscript`:

| option | target | runtime |
|---|---|---|
| `-DDEMOREEL_PORT_LUA=ON` | `demoreel_lua` | `lua/` + `sol2/` at the package root (both git-ignored checkouts; `DEMOREEL_LUA_ROOT` / `DEMOREEL_SOL2_ROOT` override) |
| `-DDEMOREEL_PORT_SQUIRREL=ON` | `demoreel_squirrel` | `squirrel/` at the package root (Squirrel 3.2, `_SQ64` + `SQUSEDOUBLE`; `DEMOREEL_SQUIRREL_ROOT` overrides the path) |

## Measurement

Release build, quiet machine, 100x40:

1. `--smoke` (1700 frames, the whole reel on short 3 s scenes): the stream hash
   must equal the JaiScript one; the table's ms/frame is the whole-reel time.
2. `--perf 90`: every scene from t=3.0 for 90 frames on a fresh reel; ms/frame
   and a per-scene frame hash (per-scene parity). JaiScript adds
   `--workers 1` (serial) against the default (parallel sections on workers).

