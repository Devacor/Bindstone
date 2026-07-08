# GLOOM — porter's guide

How to port GLOOM to another embedded scripting language for the 4-language
comparison. **REFERENCE.md is the normative game/host spec** (tick order, RNG
contract, STATE_HASH recipe, checkpoint hashes); this document covers the
mechanics: the C++ adapter contract, the CMake wiring, the measurement
protocol, and the feedback rubric each porter fills in.

The JaiScript implementation is the reference: `scripts/*.jai` for the game,
`gloom_adapter_jai.cpp` for the adapter shape.

---

## 1. Architecture: shared host + per-language adapter

```
gloom_host.hpp / gloom_host.cpp     the SHARED host (no scripting runtime deps)
gloom_adapter_jai.cpp               JaiScript adapter + main()   -> jai_gloom
gloom_adapter_chai.cpp              ChaiScript adapter + main()  -> gloom_chai      (stub)
gloom_adapter_squirrel.cpp          Squirrel adapter + main()    -> gloom_squirrel  (stub)
gloom_adapter_lua.cpp               Lua/sol2 adapter + main()    -> gloom_lua       (stub)
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

**Your scripts own**: everything else — the whole game per REFERENCE.md
sections 1 and 4 (sim, raycaster, sub-cell renderer, HUD, autopilot). The host
only blits the string your `gloom_frame` returns.

## 2. The adapter contract (`gloom_host.hpp`)

Implement `gloom::script_adapter` (factory + metadata) and
`gloom::script_session` (one booted runtime). `main()` is two lines: construct
your adapter, `return gloom::run_gloom(argc, argv, adapter);`.

### script_adapter

| virtual | guarantee |
|---|---|
| `program_name()` | exe name used in messages, e.g. `"gloom_lua"` |
| `language()` | human name for titles, e.g. `"Lua/sol2"` |
| `backends()` | runtimes `--smoke` runs (in order) and `--backend` accepts. Single-runtime ports return one name, e.g. `{"lua"}`. JaiScript returns `{"interpreter","vm"}`. |
| `default_backend()` | default for interactive mode; defaults to `backends().back()` |
| `normalize_backend(b)` | optional alias mapping (jai: `"interp"` → `"interpreter"`) |
| `make_session(backend, services, opt, live_input)` | fresh engine + all host-API bindings; scripts NOT yet loaded. Called once per interactive run, once per backend per smoke run. `live_input` is false in smoke/bench — but `services.key_down` is already gated, so binding it unconditionally is correct. |

### script_session

| virtual | when called | must guarantee |
|---|---|---|
| `load_scripts()` | once, right after `make_session` | resolve the scripts dir (use `gloom::locate_scripts_dir(opt.scripts_dir, opt.argv0, GLOOM_SOURCE_SCRIPTS_DIR, "gloom_scripts_<lang>", "main.<ext>")`) and load/parse/execute the top level of your scripts. Throw `std::exception` on failure. |
| `force_autopilot()` | after load, before boot, only for `--bench` | call script `gloom_force_autopilot()` |
| `boot(w, h)` | once after load | call script `gloom_boot(w, h)` (console cols, rows) |
| `frame(dt, key, fps, ms_sim, ms_draw)` | every frame | call script `gloom_frame(...)` and return the FULL frame string (rows joined by `\n`). Smoke passes `key=""` and zeros for the perf floats. This string is hashed byte-for-byte — see REFERENCE.md section 3.3. |
| `state_hash()` | smoke end | script `gloom_state_hash()` → int64. THE conformance number. |
| `wants_quit()` | per interactive frame | script `gloom_wants_quit()` |
| `summary()` | process/backend end | script `gloom_summary()` (prints via `host_log`) |
| `stack_trace()` | after any of the above throws | script backtrace of the last error, `""` if unavailable |

Error model: script failures surface as `std::exception` throws out of the
session methods; the host prints `<prog>: script error: ...` (smoke wraps with
the failing tick + backend and appends `stack_trace()`).

## 3. The host API your adapter must bind (REFERENCE.md section 2)

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
  smoke/bench-gated; key names in REFERENCE.md section 2).
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

## 4. What your scripts must implement

The whole of REFERENCE.md sections 1 (game spec), 3.2 (STATE_HASH), and 4
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

## 5. CMake wiring

`jai_gloom` always builds; each port is an option, OFF by default, and its
runtime need not exist when OFF:

| option | target | runtime location (checked at configure) |
|---|---|---|
| `-DGLOOM_PORT_CHAI=ON` | `gloom_chai` | `External/ChaiScript-6.1.0` (header-only) |
| `-DGLOOM_PORT_SQUIRREL=ON` | `gloom_squirrel` | `Source/JaiScript/squirrel` (built as `gloom_squirrel_runtime`) |
| `-DGLOOM_PORT_LUA=ON` | `gloom_lua` | `Source/JaiScript/lua` + `Source/JaiScript/sol2` (Lua built as `gloom_lua_runtime`) |

Port targets link `gloom_host` + their runtime only — NOT `jaiscript`. Keep it
that way: the shared core must never gain a scripting-runtime dependency, and
no `#ifdef` in `gloom_host.*` may reference any runtime.

When your scripts exist, add (in the option's block) a
`GLOOM_SOURCE_SCRIPTS_DIR` pointing at `ports/<lang>/scripts` (already done in
the stubs) and a POST_BUILD `copy_directory` to
`$<TARGET_FILE_DIR:...>/gloom_scripts_<lang>` mirroring `jai_gloom`'s.

Replace the stub session in `gloom_adapter_<lang>.cpp`; the stub already
compiles and links its runtime, so binding work starts immediately.

## 6. Measurement protocol (identical for every port)

Same machine, quiet, Release/optimized build, Windows Terminal, 100x40.
Numbers reported per REFERENCE.md section 3.4:

1. **Conformance**: `--smoke --ticks {300,2000,3000,16000}` hash table above,
   plus the seed cross-checks. Exit code 0 required (single-backend ports get
   parity trivially; the exit still gates on the run completing).
2. **Headless perf**: `--smoke --ticks 300` ms/tick for `--pix half`, `quad`,
  `sext` (3 rows). Single 300-tick runs; treat ±10% as noise, rerun if the
  machine was loaded.
3. **Interactive split**: `--bench 300` sim/draw ms per frame (quad).
4. Optional ablation for stage-level parity with the JaiScript numbers:
   sim-only / rays / paint / rows, measured by early-returning the render
   pipeline (see README.md).

Report alongside the JaiScript rows in README.md's tables.

## 7. Feedback rubric (fill in per port)

Append your filled-in copy to REFERENCE.md section 5's sibling — one section
per language, same headings, so the comparison writes itself:

```markdown
## <Language> port notes

- Binding experience: (dynamic_binder-equivalent ergonomics; Rng/class binding,
  free functions, globals; lines of C++ adapter code; anything that fought you)
- Porting friction: (language features that mapped poorly — coroutine brains,
  typed truncation, value vs reference semantics, string building; workarounds
  chosen and why)
- LOC: (script total + per-file table like REFERENCE.md section 5's)
- Time-to-first-running: (wall hours to: skeleton boots / first clean smoke 300
  / full 16000 conformance)
- Debugging story: (what a hash divergence bisect felt like; error messages,
  stack traces, tooling)
- Perf: (the section 6 tables)
- Net: (one paragraph — where the language earned or lost its keep)
```

Honesty rules: engine-bug detours excluded from the hours, language friction
included; note anything you had to change in shared files (should be nothing).
