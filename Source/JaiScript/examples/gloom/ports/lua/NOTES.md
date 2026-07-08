# Lua 5.4 / sol2 port notes

Filled-in PORTING.md section 7 rubric. (REFERENCE.md is the shared spec file;
this copy lives with the port so the comparison section can absorb it.)

## Conformance

**All STATE_HASH gates green** (the PORTING.md conformance contract):

| `--smoke --ticks N` (seed 666, quad) | STATE_HASH | matches ref |
|---|---|---|
| 300 | 3580805725 | yes |
| 2000 | 319812559 | yes |
| 3000 | 4080154357 | yes |
| 16000 | 3497451110 | yes |

Cross-checks: seeds 7/99/4242 @3000 → 1696980843 / 1855347375 / 2848371116;
`--god --seed 5` @12000 → 576425398; `--pix half`/`sext` @3000 → sim hash
unchanged (4080154357); `--workers 0/8` → unchanged (ports are always serial;
flag is display-only). Gameplay outcome @3000 matches the spec expectation:
E1M1, 7/7 kills, 1/1 secrets, 0 deaths. **Every sim checkpoint passed on the
first run after first successful build.**

**Frame-byte parity (optional gate): holds for the first 3590 frames**, then
diverges transiently. Verified byte-identical streams: 300 / 2000 / 3000 /
3100 / 3200 / 3400 quad, 3000 half, 3000 sext (hashes 7fecc09815bb64e0,
cf05be38eb174fbe, 0ccc249e860ef7f4, 4875f91aa68b18dc, c741895114fe6f84, ...),
plus individual frame dumps at 3500 / 3580 / 3590 — all identical. First
divergent frame: **3591** (seed 666 quad). Bisected to ground truth:

- Sim state is **bit-identical** at the divergence (instrumented a scratch
  copy of the reference scripts via `--scripts`: px/py/pang scaled by 2^30
  print equal integers on both engines; muzzle/gun_kick/cooldown equal;
  STATE_HASH stream equal through and past the window).
- My frame 3591 is exactly the render of the post-tick-3592 camera
  (pang 0.18) — confirmed by an independent Python re-implementation of the
  spec DDA fed the dumped state; frame 3590 (byte-identical between ports)
  is exactly the post-tick-3591 render (pang 0.27), so the tick↔frame
  binding matches up to that point.
- The reference's frame 3591 corresponds to **no single tick state**: its
  best-fit camera angle is ~0.30 rad, BETWEEN its own tick-3590 (0.36) and
  tick-3591 (0.27) poses, while the state it logged at that render was the
  tick-3592 pose. Reproduced identically on reference interpreter AND vm, at
  `--workers -1` AND `0` — deterministic reference behavior that a
  straightforward transliteration of the reference source does not produce.
- Divergence is transient/recurring (3591-3593 differ, 3594/3598 byte-equal
  again; cumulative stream hashes differ from 3595 on), sim unaffected.

Stopped there per the relaxed bar ("realistic code, not machined to the
frame reference"): the port renders the pure function of the sim state that
the reference source specifies; the residual delta looks like a reference-
side render quirk around this camera pose and is worth a look from the
reference side rather than contorting the port.

Documented deviations (all REFERENCE.md-sanctioned or hash-neutral):

- No 15-per-int64 map packing: the ray DDA reads a flat per-cell render-kind
  array (`G.mapkind`), rebuilt on tile change (REFERENCE.md 4.2 allows this —
  the packing exists for JaiScript's parallel value contract, not the game).
- `gloom_ray` takes plain args and returns 4 values (no packed record);
  the renderer inlines the identical DDA per column. Float stream identical.
- `gloom_particle` mutates slots in place instead of returning fresh records
  (same numbers, no allocation).
- Shot glow-trail phase uses `(tick + i - 1) % 2` to preserve the reference's
  0-based shot index rule.
- Warden summons are appended AND ticked on the spawn tick: the reference
  interpreter's range-for re-checks live size per iteration (verified in
  `interpreter.cpp:visit_range_for_stmt`); the port mirrors it with a live
  `#enemies` while-loop.
- Title tagline stays byte-identical to the reference ("all JaiScript") so
  frame-hash comparability holds; swap it once frame parity stops mattering.
- CMake is frozen for porters: the `gloom_scripts_lua` POST_BUILD
  `copy_directory` is still missing from the GLOOM_PORT_LUA block (one-liner,
  mirrors jai_gloom's). Builds run fine anyway via the compiled-in
  `GLOOM_SOURCE_SCRIPTS_DIR`; only a relocated exe needs the copy.

## Binding experience

124 lines of C++ adapter, ~40 of which are the actual bindings:

- `new_usertype<gloom_rng>` with `sol::constructors<gloom_rng(int64_t)>` +
  5 member-function pointers: worked first try. Lua 5.4's `lua_Integer` is
  64-bit, and sol2 maps `int64_t` returns onto it directly — `RNG:state()`
  folds into the hash every tick and matched at tick 300 immediately, which
  proves there is no double round-trip anywhere on the RNG path.
- Free functions are `lua_["name"] = lambda;` — as low-ceremony as binding
  gets. Globals (HOST_*) are plain assignments.
- The one API fight: `protected_function::error_handler` (the member most
  docs/examples show) does not exist in sol2 3.2.3 — the compile error was a
  C2039 inside a template stack, sol2's error-message reputation making its
  scheduled appearance. Fix: `sol::protected_function::set_default_handler(
  sol::object(lua_["debug"]["traceback"]))` once after load. That was the
  only compile iteration in the whole port.
- `sol::protected_function` + the traceback default handler surfaces full Lua
  stack traces (with script line numbers) through the host's `stack_trace()`.
- sol2 is heavy to compile (~10 s for the one TU, wants `/bigobj`), free at
  runtime for this call pattern (6 entry points/frame; per-call overhead is
  noise next to the frame itself).

## Porting friction

- **Integer/float subtype discipline is where all the conformance risk
  lived.** JaiScript's typed locals (`int x = float_expr` truncates) had to be
  re-derived at every site: Lua `/` always yields float, `//` FLOORS rather
  than truncates, and `math.floor != C truncation` for negatives (billboard /
  particle screen-x can be negative). Every int-division site was classified
  (positive → `//`, possibly-negative → an `idivt` helper — exactly one site:
  wall-slice `y0 = (vh - lineh)/2`), and every typed-assignment site became a
  `trunc()` helper call. Also `tostring(1.0) == "1.0"` would silently poison
  frame bytes, so HUD arithmetic must stay integer-subtyped end to end.
  Systematic audit, zero bugs shipped — but this is the tax a dynamically
  numeric language charges against a hash that quantizes doubles.
- **1-based indexing**: the port keeps the reference's 0-based tile coords and
  kind VALUES and pays `+1` at every flat subscript (`tiles[y*mw+x+1]`); hot
  loops fold the +1 into a biased row base (`itop = ry*2*vw + 1`). Mechanical
  rule, applied everywhere, no off-by-ones hit — but it is 100% vigilance,
  0% compiler help.
- **Coroutine brains mapped 1:1** — `coroutine.create` on the method,
  `resume(co, self)` per tick, `status(co) == "dead"` for done, pain-interrupt
  = drop the handle. The pilot yielding an input table per tick is literally
  `coroutine.yield(tbl)`. The single best fit of the port; Lua is at least as
  at home here as the reference.
- **Metatable OO, no class library**: three classes (Game, Enemy,
  ParticlePool) as plain `Class.__index = Class` + `Class.new` + colon
  methods. At this scale a class helper would cost more than it saves;
  that's the idiomatic call, not a workaround.
- **No `continue`**: brains/loops restructured into else-chains (RNG order
  preserved); never needed `goto`.
- Multiple return values replaced the reference's flat ray records — cleaner
  and faster than the original's own idiom.
- Data as code: sprite art / maps / weapon tables were machine-converted from
  the reference (`data.lua`, generated) rather than hand-retyped — the
  reference literals are JSON-compatible, and hand-transcribing 600 lines of
  pixel art invites silent frame drift.

## LOC

Script total **3206** (of which 262 generated data) vs reference 3561 + 124
C++ adapter (reference jai adapter: 141):

| file | LOC | subsystem |
|---|---|---|
| render.lua | 715 | raycast view, strips, billboards, row builders |
| game.lua | 589 | Game class, tick, autopilot, hash |
| enemies.lua | 348 | Enemy class + 4 coroutine brains |
| data.lua | 262 | GENERATED: weapons/bestiary/items/art/maps |
| combat.lua | 255 | LOS/hitscan/projectiles/damage |
| hud.lua | 251 | HUD + full-screen states |
| maps.lua | 181 | map parser + boot validator |
| sim.lua | 166 | movement/use/pickups |
| particles.lua | 143 | pool + burst kit |
| pure.lua | 95 | serial ray + particle step |
| util.lua | 82 | math/mix32/palette intern |
| main.lua | 74 | entry points |
| defs.lua | 40 | art parser |
| state.lua | 5 | globals |

## Time-to-first-running

- Skeleton boots / first clean smoke 300: **~1h45m** from cold start — and
  they were the same moment: the first successful `--smoke 300` run matched
  the checkpoint hash AND the reference frame hash. (~45 min of that was
  reading REFERENCE.md + all 3.5k lines of reference script before writing
  any Lua; the up-front int/float audit is why nothing needed debugging.)
- Full 16000 sim conformance + seed/god/pix cross-checks: +15 min (run time).
- One compile fix total (sol2 error-handler API), zero script fixes.
- Separately: ~3h wall on the frame-parity investigation (section above),
  ~80% of it waiting on reference-side probe runs; no port changes came out
  of it — counted apart from the port hours since the sim gate never moved.

## Debugging story

Two very different halves. **Sim conformance: nothing to debug** — all
STATE_HASH checkpoints passed on the first run, so the up-front int/float
audit paid for itself. **Frame-byte parity: a real bisect**, and the tooling
held up well. The workflow that localized frame 3591 out of 16000:
stream-hash probes are monotone (first divergence only), so binary-search
`--ticks N` runs bracketed the tick; `--dump-frame N` byte-diffs identified
the divergent element (a wall-slice run at specific columns); a Python
re-implementation of the spec DDA fed with exact dumped state (floats
printed scaled by 2^30) adjudicated WHOSE render matched the state; and
`--scripts` pointing at an instrumented scratch COPY of the reference
scripts made the reference itself printable without touching frozen files.
The asymmetric run cost dominated wall time: each gloom_lua probe was
seconds, each reference probe 5-8 minutes (both backends run per smoke).
Lua-side ergonomics were good throughout — errors carry file:line, the
traceback handler gives full script backtraces through the host, and
monkey-patching `show_msg`/`render_frame` from an appended chunk made
tick-stamped event traces trivial (a genuinely nice dynamic-language perk:
instrumentation without editing shipped files).

## Perf

Same machine and session for all rows, quiet (build lock held), Release
`/O2 /GL` MT for both exes, 100x40.

Headless `--smoke --ticks 300`, ms/tick (lua min-of-5; jai min-of-3):

| pixels | Lua/sol2 | JaiScript VM | JaiScript interp | Lua vs VM |
|---|---|---|---|---|
| half (100x68) | 1.008 | 21.911 | 27.881 | 21.7x |
| quad (200x68, default) | **1.407** | 33.996 | 40.482 | **24.2x** |
| sext (200x102) | 1.968 | 43.091 | 53.356 | 21.9x |

(16000-tick sustained average: 1.50 ms/tick — no GC cliff over long runs.
Stage ablation not run: at 1.4 ms/frame total there is no stage worth
carving; the whole Lua frame costs less than the reference's sim step.)

Interactive `--bench 300` (quad):

| | sim ms/frame | draw ms/frame | total | fps uncapped |
|---|---|---|---|---|
| gloom_lua | 1.43 | 0.02 | 1.44 | 692 |
| jai_gloom (vm) | 32.39 | 0.01 | 32.40 | 31 |

## Net

Lua came in as the presumptive perf winner and won by more than the
microbenches suggested: **22-24x faster than the JaiScript VM on the whole
game**, putting sim + raycast + sub-cell renderer + row building at 1.4
ms/tick where the reference spends its entire 33 ms budget. The stage that
dominates the reference (serial glyph-row building over 13.6k sub-pixels) is
simply not a problem at Lua's per-op cost, no parallelism needed. What Lua
charged for it: the int/float subtype audit (the only real conformance
hazard, handled by two helpers and discipline), 1-based-index vigilance with
zero compiler backup, and OO/typing you assemble yourself. The coroutine
brain architecture ported 1:1 and is the part of the reference design that
feels most native here. For a game this shape, Lua 5.4 + sol2 is the
low-drama option: one compile error, zero script fixes, first-run sim
conformance, and byte-exact frames for the first 3590 of them.
