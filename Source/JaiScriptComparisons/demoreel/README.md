# demoreel ports

The demoreel's three heaviest scenes of different kinds, ported to Lua 5.4, Squirrel 3.2, ChaiScript 6.1 and Python 3
for the cross-language comparison: OSSUARY (scene 9, a per-pixel raymarch), MAELSTROM (scene 10, a stable-fluids
simulation drawn as a lit 3D view) and GLOBHUNTER (scene 6, a textured grid raycaster), plus JULIA (scene 2), the
check scene each port's harness was validated with. Every port prints the JaiScript VM's scene hash, byte for byte.
`ports/COMPARISON.md` holds the measurements and the runner; this page is the porting contract.

The JaiScript reel is the reference: JaiScript's `examples/demoreel/scenes/*.jai`, and
`demoreel_adapter_jai.cpp` beside them for the adapter shape. The pattern is GLOOM's (`../gloom/ports/README.md`): one
shared host, a thin adapter per language, the scripts own everything else.

## Architecture

```
demoreel_host.hpp / .cpp        the SHARED host (JaiScript/examples/demoreel; no scripting-runtime deps)
demoreel_adapter_jai.cpp        JaiScript adapter + main()   -> jai_demoreel
demoreel_adapter_<lang>.cpp     a port's adapter + main()    -> demoreel_lua, demoreel_squirrel, demoreel_chai
ports/<lang>/                   the port's scripts (lua, squirrel, chai)
ports/python/                   the Python port and demoreel.py, its standalone driver (no C++ host)
```

**The host owns** (do not reimplement): the VT console, key polling, frame timing, the `demo_rng` xorshift64\*
struct, every `--flag`, and the harnesses; the comparison uses `--perf N --scene I` (ms/frame and an FNV-1a scene hash
over the N frame strings).

**An adapter owns**: creating the runtime, binding the host API (below), loading the scripts, forwarding the entry
points (the `script_session` table in `demoreel_host.hpp`).

**The scripts own** the part of the reel the perf path runs: the presenter, palettes, the score (`music.jai`'s Cue),
the `Scene` base, the `Reel`'s boot, jump and frame, and the scenes. Transitions, title cards, the HUD, keys and the
cold open are not ported: the perf window starts mid-scene and reaches none of them, and a port's `frame` raises an
error rather than skip one silently. Each scene file registers its class under its index; the reel composes a cue only
for the scenes a port has.

## The adapter contract (`demoreel_host.hpp`)

`script_adapter`: `program_name()`, `language()`, `backends()` (one name per port: `{"lua"}`, `{"squirrel"}`,
`{"chai"}`; JaiScript `{"vm"}`), `make_session(backend, services, opt)` (a fresh runtime with every host-API binding,
scripts not loaded yet).

`script_session`:

| virtual | script entry point | guarantee |
|---|---|---|
| `load()` | - | resolve the scenes dir (`demoreel::locate_scenes_dir(opt.scenes_dir, opt.argv0, <compiled-in source dir>, "demoreel_<lang>_scenes", "main.<ext>")`) and load every script; throw on failure |
| `boot(w, h, seed, backend, smoke, hud)` | `demo_boot(...)` | a fresh `Reel`; perf boots again before each measured scene |
| `frame(a)` | `demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) -> string` | the FULL frame, rows joined by `\n`; hashed byte for byte |
| `scene_count()` / `scene_name()` / `scene_index()` / `scene_time()` | `demo_scene_count()` ... | the perf rows (12 scenes) |
| `jump(idx, at_t)` | `demo_jump(idx, at_t)` | start scene idx at at_t, replaying `min(at_t, replay)` seconds of updates |
| `stack_trace()` | - | the script backtrace of the last error |

## Host API the scripts use

- **`Rng`**: the shared `demoreel::demo_rng`, exactly: constructor `(int seed)`, `next(n) roll(lo, hi) chance(p)
  nextf()`. Never the language's own RNG: every scene seeds one and draws in the reference's order. Python, having no
  host, reimplements it bit for bit with 64-bit masking.
- **`host_log(string)`**, **`ESC`**, **`itrunc(float) -> int`** (C truncation) and **`ifloor(float) -> int`**.
- ChaiScript ships no math library: its adapter binds the C functions the scripts call (`sqrt sin cos floor exp log
  log2 fabs atan2 pow`).

## What the scripts must reproduce

- Same algorithms, same resolution, same operation order: IEEE-754 doubles evaluated in source order, the same libm
  calls. Squirrel builds with `_SQ64` and `SQUSEDOUBLE` (the adapter static_asserts both).
- JaiScript `int` is int64 with C semantics: `/` truncates toward zero, `%` takes the dividend's sign, `>>` is
  arithmetic, a typed `int` slot truncates a float stored into it. Lua's `//` and `%` floor and its `>>` is logical;
  Python's `//` and `%` floor; ChaiScript's integer literals are 32-bit (`1ll` where a value can pass 2^31).
- ChaiScript 6.1 parses some decimal literals one ulp off (`parse_num` adds the fraction digit by digit): the chai
  scripts spell those as exact quotients, `(82.0 / 100.0)`, which fold at parse time; `ports/chai/check_literals.py`
  lists them (`--fix` rewrites them).
- A class-typed or array-typed value copies on assignment and by-value parameters (C++ semantics); the ports copy
  explicitly where the original copies and then mutates either side (MAELSTROM's `u = u1`, `MS_FU = u`).
- `parallel_transform` bodies are pure functions of their inputs: the ports run them as serial loops over the same
  per-row functions and keep the row arrays the original builds.

## CMake

`jai_demoreel` always builds. Each port is an option, OFF by default, linking `demoreel_host` and its runtime, never
`jaiscript`; in Release the runtime and the executable build with `/GL` and `/LTCG`, as JaiScript does:

| option | target | runtime |
|---|---|---|
| `-DDEMOREEL_PORT_LUA=ON` | `demoreel_lua` | Lua 5.4 + sol2 at `DEMOREEL_LUA_ROOT` / `DEMOREEL_SOL2_ROOT` (default `third_party/lua`, `third_party/sol2`; the measurements use the pinned Lua 5.4.8 and sol2 3.3.0 that `gloom/ports/prepare_comparison.py` fetches into `out/deps`) |
| `-DDEMOREEL_PORT_SQUIRREL=ON` | `demoreel_squirrel` | Squirrel 3.2 at `DEMOREEL_SQUIRREL_ROOT` (default `third_party/squirrel`) |
| `-DDEMOREEL_PORT_CHAI=ON` | `demoreel_chai` | ChaiScript 6.1.0 at `COMPARISONS_CHAISCRIPT_ROOT` (header-only, `CHAISCRIPT_NO_THREADS`) |

The Python port needs no build: `python ports/python/demoreel.py --perf N --scene I --w 100 --h 40`, standard library
only. Its driver times `demo_frame` as the host does and hashes outside the timed window, since a pure-Python FNV-1a
over a 40 KB frame costs 2 to 3 ms (in the C++ host it is noise).

`DEMOREEL_PADDING_CONTROLS=ON` builds each enabled executable a second time with 40 retained volatile statements in
the host's cold help function, a code-layout control.
