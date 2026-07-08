# ChaiScript port notes

GLOOM ported to ChaiScript 6.1.0 (header-only, `External/ChaiScript-6.1.0`,
compiled with `CHAISCRIPT_NO_THREADS`). Scripts in `ports/chai/scripts/*.chai`,
adapter in `gloom_adapter_chai.cpp`. Everything below follows PORTING.md's
rubric template; measurements per section 6's protocol.

## Conformance

Sim conformance (`STATE_HASH`, seed 666, quad):

| ticks | expected | gloom_chai | verdict |
|---|---|---|---|
| 300 | 3580805725 | 3580805725 | MATCH |
| 2000 | 319812559 | 319812559 | MATCH |
| 3000 | 4080154357 | 4080154357 | MATCH |
| 16000 | 3497451110 | 3497451110 | MATCH |

Cross-checks — all exact: seed 7 at 3000 -> 1696980843, seed 99 -> 1855347375,
seed 4242 -> 2848371116, `--god --seed 5` at 12000 -> 576425398. Sim hashes
verified `--pix`-independent (half/quad/sext agree). **The sim is bit-exact at
every checkpoint the spec defines.** Gameplay outcome at 16000 is
field-for-field identical to the reference summary: on E1M2 with 3/10 kills,
episode 7/7 kills + 1/1 secrets, hp 42, ammo 57/8/0, zero deaths; at 3000 the
pilot has cleared E1M1 at 100% kills/secrets with zero deaths, as specified.

Frame-byte conformance: frames are byte-identical to the reference for the
first 163 ticks, then diverge **once** and re-converge — the stream hash
differs while individually sampled frames (60/120/165/180/240/299) are
byte-identical. The one differing frame was chased to ground truth:

- At tick 165 the pilot fires; the muzzle smoke particle spawns almost exactly
  on the camera's center axis. Its screen-x projection computes
  `ttx = inv * (diry*rx - dirx*ry)` — a catastrophic cancellation whose true
  value is ~±4e-16 (2 units in the 2^-52 place).
- Dumped at full mantissa precision: JaiScript computes `ttx = -2×2^-52`,
  ChaiScript computes `+2×2^-52`, and `depth` differs by exactly 1 ulp
  (2338268925666069 vs ...070 ×2^-52). The sign flip moves
  `trunc(50*(1+ttx/depth))` across the 49/50 boundary: the particle renders
  one pixel left in the reference for one frame.
- Everything upstream that the dumps could resolve (particle pool state,
  px/py/pang, cos/sin results) is bit-identical between the two runtimes; the
  last-ulp disagreement appears inside the multiply/subtract chain of the
  projection itself — i.e. codegen-level rounding differences between the two
  interpreters' arithmetic paths, only visible under total cancellation.
- Sim state never sees this (particles are render-only garnish), which is why
  STATE_HASH stays exact while the frame stream hash diverges.

Per the project ruling mid-port ("playable + outcome-reasonable, don't machine
the code to force byte parity"), the chase stopped there. The finding stands:
ChaiScript reproduces the reference sim bit-exactly at every checkpoint tried,
and reproduces frame bytes except at float-cancellation knife edges (~1 frame
in 300, one particle, one pixel).

## Binding experience

Good — the best part of ChaiScript. The whole host API bound in ~30 lines of
the obvious `chai.add(fun(...), "name")` calls: `gloom_rng` methods bind
directly off the struct (`fun(&gloom_rng::next)`), the constructor via
`constructor<gloom_rng(int64_t)>()`, host closures as lambdas, globals via
`add_global_const(const_var(...))`. Numeric argument conversion (script int →
`int64_t` param) just works. Entry points come back as `std::function` via
`chai.eval<std::function<...>>("gloom_frame")` — also painless.

Two adapter-level gotchas:

- ChaiScript core has **no math library**. `sqrt/cos/sin/atan2/floor` had to
  be bound in the adapter (they live in the separate ChaiScript_Extras repo,
  not checked in here). Fair to count as language friction: the runtime you
  check out cannot compute a square root.
- Error surfacing is good: `eval_error::pretty_print()` gives a real script
  stack trace with file/line, which fed `stack_trace()` naturally.

Compile time: the legendary ChaiScript compile cost was real but bounded —
~50 s for the adapter TU (MSVC, /O2, `/bigobj` required), vs ~5 s for a
typical TU. One TU in this design, so it only hurt when the adapter changed.

Adapter LOC: 155 (reference jai adapter: 140).

## Porting friction

**1. Coroutine brains → hand-lowered state machines (the big one).**
The reference's enemy brains are coroutine methods — phase state lives in the
coroutine frame, `for (i < 7) yield` IS the telegraph, and pain-interruption
is "discard the handle". ChaiScript has no coroutines, so each brain became an
explicit state machine: a `bphase` resume-point field, a `bt` wait counter,
and every coroutine local (`zig`, `orbit`, `burst`, `volley`, `enraged`,
`summoned`) promoted to an object field that must be re-initialized when the
brain re-mints. The lowering needed a discipline to keep RNG call sites on the
same ticks as the reference:

- "enter an N-yield wait" = set `bt = N-1` and return (the entry tick is the
  first yield);
- coroutine code that runs between yields (`continue` back to the loop head,
  same-tick chained transitions) = phase fall-through inside a `while(true)`
  dispatch loop.

The warden was the worst case: dormant / seen-pause / loop-head (enrage +
summon checks) / volley telegraph / between-volleys gap / walk / melee windup
/ melee recover — 8 phases plus two "coroutine local" flags whose reset on
re-mint is itself gameplay (pain re-runs the enrage/summon logic — the
reference gets that for free from coroutine frame lifetime; here it's a
comment and two lines in `tick()` that a maintainer must not delete).
It works — and it hit every checkpoint hash — but it's the difference between
*describing behavior* and *implementing a scheduler by hand*. Roughly 2× the
code, and the control flow reads inside-out: you see phases, not behavior.
The autopilot was the easy case: one yield per loop iteration means it lowers
to a plain function with persistent fields.

Representative excerpt (grunt, reference vs port):

```jaiscript
// JaiScript reference — the telegraph IS the loop
state = ENEMY_STATE_HUNT; anim = 0;
for (int i = 0; i < 7; ++i) { yield; }   // the roar (it commits)
int zig = RNG.next(2) == 0 ? 1 : -1;
while (true) { ... }
```

```chaiscript
// ChaiScript port — the telegraph is a resume-point + counter
if (this.bphase == 0) {
    if (this.state != ENEMY_STATE_DORMANT || (this.sees_player() && ...)) {
        this.state = ENEMY_STATE_HUNT;
        this.anim = 0;
        this.bphase = 1;
        this.bt = 6;             // the roar: 7 yields including this one
    }
    return;
}
if (this.bphase == 1) {
    if (this.bt > 0) { this.bt -= 1; return; }
    this.zig = RNG.next(2) == 0 ? 1 : -1;
    this.bphase = 2;
    continue;                    // first chase step runs this tick
}
```

**2. Vector assignment copies element HANDLES (the sharpest edge).**
Probed behavior: `a = b` on two existing Vectors makes `a`'s elements share
storage with `b`'s — a later `a[0] = 5` writes through to `b[0]`. Even
`var d = c` ("clone" on declaration) shares element storage. Only
assignments into an *undefined* lhs deep-clone. This killed the reference's
single biggest render optimization: `pix = bg` (restore the background with
one whole-buffer copy) silently turned the background into an alias of the
frame buffer, so every frame started from the previous frame's image — a
one-pixel-wrong smoke slice at the slice edges, hours of forensics. The fix
is idiomatic but costly: the column loop paints ceiling/floor per pixel
(there is simply no bulk value-copy in the language). Related consequence:
a slot in a pre-sized vector cannot change type via `=` (STRIPS slots are
pre-seeded as empty vectors so strips can be assigned in later).

**3. 32-bit ints by default; unsigned `size()`.**
Integer literals are C++ `int`. `v >> 32` on a 32-bit value is UB-shaped
(masked shift), so the hash lanes canonicalize through `0ll + v` before
shifting — `mix32` works but reads like a workaround. `.size()` returns
`size_t`, and `G.w - left.size() - 2` silently goes unsigned-huge; every size
used in signed arithmetic goes through `slen/vlen` helpers (`itrunc(0.0 +
v.size())`). These are the classic dynamic-language-on-C++-semantics traps:
nothing warns, the numbers are just wrong.

**4. Numeric assignment preserves the lhs type.**
`var q = 5; q = 2.7` leaves `q == 2` — assignment converts to the *variable's*
type like C++, so a field initialized `0` can never hold a fraction. Fine once
known (initialize `0.0`), invisible until then.

**5. No statement continuation across newlines.**
A multi-line ternary is a parse error ("Incomplete block") unless inside
brackets. Minor, but the error message pointed at the wrong conceptual thing.

**6. No typed-local truncation.**
The reference leans on `int lineh = 1.0 * vh / dist;` for C-style truncation;
ChaiScript needs explicit `itrunc(...)` at ~20 call sites. Mechanical, not
painful, but each missed site is a determinism bug.

**7. Small stuff.** No string `repeat`/`join` (5-line helpers); maps are
string-keyed only (palette intern keys become `"${r}:${g}:${b}"`); map
iteration is sorted (deterministic — fine here); `${}` interpolation is
genuinely pleasant and got used everywhere the reference used template
strings, though escaped quotes inside `${...}` are risky enough that ternaries
with string literals were pre-computed into locals.

**What felt good, honestly:** the OO layer (`class`/`var`/`def` with `this.`)
mapped 1:1 to the reference's classes; reference semantics for objects in
containers (`push_back` shares constructor temporaries, range-for shares
elements, chained `G.enemies[best].hurt(...)` mutates in place) made the
registry-of-enemies pattern *simpler* than JaiScript's value-semantics rules —
no `auto&` discipline to remember; `${}` interpolation; and the binding API.
The language's failure mode is never expressiveness — it's arithmetic
semantics and speed.

## LOC

Script total **3627** (reference: 3561) + 155 adapter (reference: 140).
Near-parity overall; the brains pay the coroutine tax, offset slightly by
serial-only rendering (no chunking machinery).

| file | chai LOC | jai LOC | subsystem |
|---|---|---|---|
| render.chai | 670 | 694 | raycast view, strips, billboards, row builders |
| defs.chai | 616 | 618 | data + all pixel art |
| game.chai | 564 | 529 | Game class, tick, autopilot, hash |
| enemies.chai | 476 | 331 | Enemy class + 4 brains (**+44% — the coroutine tax**) |
| maps.chai | 309 | 347 | maps + parser + validator |
| hud.chai | 248 | 233 | HUD + full-screen states |
| combat.chai | 226 | 229 | LOS/hitscan/projectiles/damage |
| particles.chai | 174 | 130 | pool + burst kit |
| sim.chai | 148 | 153 | movement/use/pickups |
| util.chai | 84 | 65 | helpers (+slen/vlen/str_rep/join_all) |
| pure.chai | 47 | 164 | the ray DDA (serial-only, no chunk plumbing) |
| main.chai | 65 | 68 | entry points |

## Time-to-first-running

Engine-bug detours: none (the runtime behaved as shipped). Approximate wall
clock, including the semantics probing that ChaiScript's underdocumented
sharing rules made necessary:

- skeleton boots (adapter + CMake + empty entry points + semantics probe
  script): ~1.5 h (one ~50 s compile; everything after was script-only
  iteration — that part of the loop was excellent)
- full game written, first boot attempt: ~4 h in (one parse error — the
  multi-line ternary — then ran)
- **first clean smoke 300 (STATE_HASH match): first full run** — the
  state-machine lowering discipline paid off; the sim hash matched on the
  first 300-tick attempt
- frame-byte parity chase (background aliasing bug + the one-ulp particle):
  ~3 h of forensics before the "playable is the bar" ruling landed
- 2000/3000/16000 checkpoint runs: dominated by ChaiScript's own runtime
  speed (~280 ms/tick means a 16000-tick smoke is an 75+ minute experiment)

## Debugging story

Mixed. `pretty_print()` stack traces with file/line are genuinely useful, and
script-only iteration (no rebuild) made the edit-run loop fast in wall-clock
terms — except that the *runs* themselves are slow, so every hash bisection
step cost minutes, not seconds. The two real bugs were both semantics, not
logic: (1) the vector-handle aliasing (found by pixel-diffing frame dumps
against the reference and then writing a five-line probe script — the
language gives you no tool to see that two vectors share element storage);
(2) nothing else — every other divergence was the float knife-edge above.
The hash-checkpoint architecture of the spec did all the heavy lifting: a
divergence bisects to a tick, a frame dump bisects to a pixel, and scaled
`itrunc(x * 2^52)` prints recover exact mantissas through a text log.

## Perf

(Section 6 protocol: Release, min of 5; a co-resident agent's load spiked
through some runs — the min rows below are from visibly clean runs, and the
polluted samples ran 2-2.5× the min, so treat ±10% as the noise floor.
Reference rows: gloom README table, same machine.)

| smoke --ticks 300, ms/tick | half | quad | sext |
|---|---|---|---|
| gloom_chai | 249.8 | 273.4 | 281.2 |
| jai_gloom vm (serial, W=0) | — | 34.1 | — |
| jai_gloom interp (serial, W=0) | — | 42.0 | — |
| jai_gloom vm (parallel, W=4) | 22.2 | 31.3 | 42.3 |

`--bench 300` (quad): sim **295.0 ms/frame**, draw 0.01 ms/frame (the frame
string is built in-script; the console write is nothing).

Headline: **~273 ms/tick quad — 8.0× slower than the reference's own serial
VM row (34.1), and ~195× slower than the Lua port (1.4)**. A 30 Hz game runs
at ~3.5 fps. The second-order finding is the shape of the table: half mode
touches 6.8k sub-pixels, sext touches 20.4k — 3× the pixels for 13% more
time. Per-PIXEL work is not the wall; per-CELL and per-STATEMENT dispatch is
(ray count and glyph-cell count are identical across modes at 100×34). Every
`pix[gi] = c` is a multimethod dispatch through Boxed_Value, and the row
builders + column paint execute ~100-200k of those per frame. There is no
parallelism to reach for (the port is serial by spec, and ChaiScript was
compiled CHAISCRIPT_NO_THREADS for speed).

## Net

ChaiScript's ergonomics are real: the binding API is the nicest of any C++
embedded scripting library I have used, object reference semantics made the
entity registry trivially correct, and the class syntax let the game keep the
reference's shape almost line-for-line. But the running game pays for it
everywhere: no coroutines (the enemy brains — the reference's best language
fit — become hand-written schedulers), C++ arithmetic semantics leaking
through a dynamically-typed surface (32-bit literals, unsigned size_t,
type-preserving assignment, handle-sharing vector copies — every one a silent
wrong-number bug), no math library in core, and interpretive speed that turns
a 30 Hz game into a ~3 Hz slideshow. It reproduced the sim bit-for-bit, which
says the *semantics* can be driven to spec — you just have to know exactly
which of its C++ bones poke through the dynamic skin, and accept that the
renderer runs an order of magnitude behind a purpose-built script VM.
