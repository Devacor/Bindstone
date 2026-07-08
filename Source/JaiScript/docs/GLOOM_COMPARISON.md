# GLOOM × 5 — the five-way comparison report

**2026-07, VM-perf branch.** One game — GLOOM, a real-time 2.5D raycast shooter in the
terminal (`examples/gloom/`) — implemented five times: the JaiScript reference (both
backends), then ports to **Squirrel 3.2**, **Lua 5.4 (sol2)**, **ChaiScript 6.1**, and
**Python 3.12** (standalone). Same spec, same shared C++ host, same seeded RNG, same
conformance hashes, same measurement protocol, same machine.

The question this document answers, in Dev's words:

> how does code written properly in each engine that technically functions to enable fully
> playable games result in perf and play and style

That framing is load-bearing: every implementation is **realistic idiomatic code for its
language** — written the way a competent user of that language would write a game — not
machined line-by-line against the reference. Conformance is operational (`STATE_HASH` at
fixed checkpoints, REFERENCE.md §3), so "idiomatic" and "provably the same game" coexist.

Sources (each is the primary record for its column):
[REFERENCE.md](../examples/gloom/REFERENCE.md) (the spec + JaiScript language-feel §5),
[PORTING.md](../examples/gloom/PORTING.md) (adapter contract, protocol, rubric),
[squirrel/NOTES.md](../examples/gloom/ports/squirrel/NOTES.md),
[lua/NOTES.md](../examples/gloom/ports/lua/NOTES.md),
[chai/NOTES.md](../examples/gloom/ports/chai/NOTES.md),
[python/NOTES.md](../examples/gloom/ports/python/NOTES.md),
the [gloom README](../examples/gloom/README.md) (parallel story incl. captured reads),
and [PERFORMANCE.md](PERFORMANCE.md) (the microbenchmark context).

---

## 1. Executive summary

| | Lua 5.4 (sol2) | Python 3.12 | Squirrel 3.2 | JaiScript | ChaiScript 6.1 |
|---|---|---|---|---|---|
| **quad ms/tick** (headless, 300t) | **1.4** | 2.5 | 2.9 | VM 33–34 serial · **33.0 W=8**; interp 40–42 | 273 |
| vs JaiScript VM | 24× faster | 13× faster | 11× faster | 1× (the reference) | 8× slower |
| sim conformance (STATE_HASH) | bit-exact, first run | bit-exact, first run | bit-exact, first run | the reference (2 backends, any W) | bit-exact, first run |
| frame bytes | exact to frame 3590¹ | exact to frame 3590¹ | **exact, all 3 pix modes** | ground truth | exact minus 1-ulp knife edges² |
| enemy brains | coroutines, 1:1 | generators, 1:1 | generators, 1:1 | coroutine methods (native) | hand-lowered state machines (+44%) |
| script LOC | 3206 | 3351 | 3454 | 3561 | 3627 |
| C++ adapter LOC | 124 | none (standalone) | 404 | 140 | 155 |

Four of the five implementations are **bit-exact sim reproductions of the fifth** (the
JaiScript reference) at every checkpoint the spec defines — seeds 666/7/99/4242, god-mode
seed 5, 300 through 16000 ticks — and all five play out the identical game to the identical
outcome. LOC lands in the same ~3200–3630 band everywhere: the game is the same size in
every language; only the speed and the shape of the code differ.

¹ Lua/Python frame streams are byte-identical to the reference through frame 3590; the
divergence at 3591 was bisected to a *reference-side* render quirk (§2, §4). ² ChaiScript
diverges only at float-cancellation knife edges (~1 frame in 300, one particle, one pixel).
Frame parity is documented **as of each port's landing**; a reference-side fix for the
tick-3591 quirk is in flight and may change frame-stream hashes after this report.
STATE_HASH checkpoints are unaffected and remain the stable contract.

**One-sentence verdicts:**

- **Lua 5.4 / sol2** — the low-drama winner: 1.4 ms/tick, one compile error and zero script
  fixes on the way to first-run bit-exact conformance, coroutine brains 1:1; its only real
  tax was the int/float-subtype audit and 1-based-index vigilance.
- **Python** — kept the expressiveness crown at a shockingly low price (2.5 ms/tick, at or
  under Squirrel), with generator brains + REPL introspection the best debugging surface in
  the set — provided you run the right interpreter (5× cliff on 3.8-32bit).
- **Squirrel 3.2** — the efficiency benchmark among the embedded runtimes: brains ported
  token-for-token as generator methods, full frame-byte parity in all three pix modes, 2.9
  ms/tick — paid for with a threadbare stdlib and a 404-line raw-C-API adapter.
- **JaiScript** — expressed every part of the design natively (coroutine-method brains,
  typed fields, template strings, and the set's **only in-language parallelism**), held
  two-backend + any-worker-count determinism through five reimplementations — and lost
  10–24× on wall clock to a diagnosed cause: per-element read cost / value traffic (§2.4).
- **ChaiScript** — the nicest binding API and bit-exact sim conformance prove the semantics
  can be driven to spec, but no coroutines (brains become hand-written schedulers, +44%
  code) and ~273 ms/tick (a ~3.5 fps slideshow) make it the cautionary tale.

---

## 2. Head-to-head performance

Protocol: PORTING.md §6 — same machine (i7-6920HQ 4C/8T, Windows 10 19045), Windows
Terminal, 100×40, Release/optimized builds, headless `--smoke --ticks 300` seed 666,
min-of-5 for ports (min-of-3 interleaved for the JaiScript captured-reads delta), ±10%
treated as machine noise. JaiScript rows are the gloom README quiet-machine table (W=4);
the Lua session re-measured JaiScript quad in-session at 34.0 (VM) / 40.5 (interp) —
consistent.

### 2.1 The full table (ms/tick, three pixel modes)

| pixels | Lua/sol2 | Python 3.12 | Squirrel | jai VM (W=4) | jai interp (W=4) | ChaiScript |
|---|---|---|---|---|---|---|
| half (100×68) | 1.01 | 1.67 | 2.61 | 22.2 | 27.4 | 249.8 |
| **quad (200×68, default)** | **1.41** | **2.53** | **2.90** | **31.3** | **39.5** | **273.4** |
| sext (200×102) | 1.97 | 4.06 | 3.67 | 42.3 | 51.7 | 281.2 |

JaiScript serial (`--workers 0`): 34.1 VM / 42.0 interp at quad. After the captured-reads
work (§2.5) the parallel rows improved to **35.3 @W4 / 33.0 @W8** (VM, loaded-machine
min-of-3 interleaved; the delta, not the absolute, is the claim).

### 2.2 Interactive `--bench 300` split (quad)

| | sim ms/frame | draw ms/frame | fps uncapped |
|---|---|---|---|
| gloom_lua | 1.43 | 0.02 | 692 |
| gloom.py (Windows Terminal, `os.write` path) | 2.99 | 0.63 | 276 |
| gloom_squirrel | 3.32 | 0.01 | ~300 |
| jai_gloom (VM) | 32.39 | 0.01 | 31 |
| gloom_chai | 295.0 | 0.01 | ~3.4 |

Draw is free everywhere the C++ host writes the bytes; Python pays 0.63 ms doing its own
console write (and 57 ms if you let it route through `_WindowsConsoleIO` — see its NOTES).

### 2.3 Where each language's time goes (the ablation insight)

Stage ablations were run by early-returning the render pipeline (REFERENCE.md §3.4):

- **JaiScript** (VM, quad, W=4): sim ~2 ms · ray chunk build ~1 · ray transform ~1.5 (2.2×
  scaled) · wall paint ~6 · sprites+particles ~3 · **glyph-row building ~16 ms**. The wall
  was the serial consumption of the 13.6k-sub-pixel grid — more than rays, paint, and sim
  combined (pre-captured-reads).
- **Squirrel** (quad): sim 0.15 · rays+paint +0.62 · billboards +0.28 · rows+HUD+join +1.85
  = 2.90. **The same shape as JaiScript at ~10× less cost per stage** — glyph rows dominate,
  sim is a rounding error. No walls; its per-element array read is ~50 ns.
- **Lua**: no ablation worth running — at 1.4 ms/tick the whole frame costs less than the
  reference's sim step alone. 16000-tick sustained 1.50 ms/tick, no GC cliff.
- **Python**: no stage wall either (16000-tick sustained 2.81, flat), but a **version
  cliff**: the identical code on the machine's default Python 3.8 (32-bit) runs 13.3
  ms/tick — 5× slower than 3.12-64bit, bit-identical hashes.
- **ChaiScript**: the second-order finding is the *shape* of its row — half mode touches
  6.8k sub-pixels, sext 20.4k, and 3× the pixels costs only 13% more time. **Per-pixel work
  is not its wall; per-STATEMENT dispatch is** (ray count and glyph-cell count are constant
  across modes) — every `pix[gi] = c` is a multimethod dispatch through `Boxed_Value`, and
  the frame executes ~100–200k of them.

### 2.4 The microbench inversion (the report's most useful finding)

[PERFORMANCE.md](PERFORMANCE.md) has the JaiScript VM **beating Squirrel 13W/6L** across 19
head-to-head microbenchmark rows (statements 4–5×, containers 2–4×, methods 2×, strings
1.4×), and splitting honestly with Lua (7W/10L/2T). GLOOM inverts that: the same VM loses
the *whole game* to Squirrel by ~11× and to Lua by ~24×.

The diagnosis is specific, not hand-waved: **per-element read cost — value traffic**. The
micro suite's won rows are statement/container/method-shaped at counts where JaiScript's
fast paths win; the game frame is dominated by bulk element traffic — tens of thousands of
`pix[gi]`/`strip[t]` reads and stores per frame — where the idiom ladder prices a VM array
element read at ~370–575 ns against Squirrel's ~50 ns and Lua's less. Each scripted element
op moves a 32-byte tagged `script_value` (engine ref + type_info — the "value traffic" of
`flatstack_design.md`) where Lua moves a TValue and Squirrel an SQObject. Multiply a ~7–10×
per-element gap by a workload that is almost entirely elements and you get exactly the
observed game-level gap. This is the **identified next campaign**: element-read /
value-traffic cost, the successor to the flat-stack and method-cost campaigns.

### 2.5 The JaiScript parallel story (the only in-language parallelism in the set)

Every port runs serial by spec (`--workers` is display-only for them); JaiScript is the one
implementation whose *language* offers parallelism, and GLOOM dogfooded it twice:

1. **v0 — pure `parallel_transform`** (wall rays as 16 hand-built column chunks carrying
   the bit-packed map snapshot, plus the 288-slot particle pool): the parallel stages were
   cheap and scaled (rays 2.2× at W=4) but were a small slice; total ms/tick barely moved
   with worker count. Amdahl, working as advertised — the serial glyph-row stage was the
   wall, and the v0 value-only contract couldn't touch it.
2. **v0.5 — captured reads** (`parallel_design.md` §13, landed 3201d4dc): a body may READ
   enclosing globals, so the quad row builder became `gloom_row_quad(ry)` reading the pixel
   grid as a zero-copy **borrow**. Frame bytes stayed byte-identical at every worker count
   and backend; the before/after on the same binary (min-of-3 interleaved, quad):

| workers | VM before | VM after | interp before | interp after |
|---|---|---|---|---|
| 0 (serial) | 46.6 | 45.3 | 53.3 | 58.3 |
| 1 | 39.1 | 37.3 | 47.5 | 45.4 |
| 4 | 46.3 | **35.3** | 50.6 | 40.5 |
| 8 | 41.1 | **33.0** | 51.9 | 37.3 |

The ~16–23 ms glyph wall roughly halved at W=4–8; the honest residue is the per-frame
palette snapshots, the barrier's scan of the grid, and the still-serial paint stages. Two
things are true at once: the parallel machinery works, is deterministic by construction
(identical hashes at any W), and materially moved the frame time — and no amount of it
buys back a 7–10× per-element cost against runtimes that don't need workers at all.

---

## 3. Expressiveness — the same brain, five ways

Dev's ordering puts this dimension right after perf, and GLOOM's enemy brains are the
sharpest probe: the reference expresses each monster as a **coroutine method whose handle
lives in a field** — phase state is coroutine-frame locals, one yield = one tick, and pain
interrupts by discarding the handle (`tick()` re-mints it). Here is the TURRET — "dormant
metal until it has line of sight; then 3-round bursts" — in all five, from the shipped
scripts.

**JaiScript** (`scripts/enemies.jai`, 23 lines):

```jaiscript
coroutine void brain_turret() {
    while (true) {
        while (!(sees_player() && player_dist() < 11.0)) {
            state = ENEMY_STATE_DORMANT;
            yield;
        }
        state = ENEMY_STATE_HUNT;     // waking whir
        anim = 0;
        for (int i = 0; i < 8; ++i) { yield; }
        while (sees_player() && player_dist() < 12.0) {
            state = ENEMY_STATE_ATTACK;
            anim = 0;
            for (int burst = 0; burst < 3; ++burst) {
                turret_shot(x, y);
                yield;
                yield;
            }
            state = ENEMY_STATE_HUNT;
            anim = 0;
            for (int i = 0; i < 13; ++i) { yield; }
        }
    }
}
```

**Squirrel** (`ports/squirrel/scripts/enemies.nut`) is the same function **almost
token-for-token** — `function brain_turret()` with `local` for `int` and `::`-qualified
globals; generator methods mint handles stored in instance fields, `resume` is a tick,
`getstatus() == "dead"` re-mints. **Lua** (`enemies.lua`) is the same 23 lines with
`coroutine.yield` and `self:`; **Python** (`enemies.py`) is a generator method where
`for _ in range(8): yield` IS the wake-up and `gen.gi_frame is None` is "dead":

```python
def brain_turret(self):
    while True:
        while not (self.sees_player() and self.player_dist() < 11.0):
            self.state = STATE_DORMANT
            yield
        self.state = STATE_HUNT     # waking whir
        self.anim = 0
        for _ in range(8):
            yield
        while self.sees_player() and self.player_dist() < 12.0:
            self.state = STATE_ATTACK
            self.anim = 0
            for _ in range(3):
                combat.turret_shot(self.x, self.y)
                yield
                yield
            self.state = STATE_HUNT
            self.anim = 0
            for _ in range(13):
                yield
```

**ChaiScript** has no coroutines, so the same brain is a hand-lowered state machine —
50 lines instead of 23, a `bphase` resume-point field, a `bt` wait counter, and every
coroutine local promoted to an object field that must be re-initialized on re-mint
(`ports/chai/scripts/enemies.chai`, excerpt):

```chaiscript
// phases: 0 dormant, 1 wake, 2 between-shots gap, 3 burst cooldown, 4 check
def brain_turret() {
    while (true) {
        if (this.bphase == 0) {
            if (this.sees_player() && this.player_dist() < 11.0) {
                this.state = ENEMY_STATE_HUNT;     // waking whir
                this.anim = 0;
                this.bphase = 1;
                this.bt = 7;             // wake: 8 yields including this one
                return;
            }
            this.state = ENEMY_STATE_DORMANT;
            return;
        }
        if (this.bphase == 1) {
            if (this.bt > 0) { this.bt -= 1; return; }
            this.bphase = 4;
            continue;
        }
        if (this.bphase == 2) {
            if (this.bt > 0) { this.bt -= 1; return; }
            this.burst += 1;
            if (this.burst < 3) {
                turret_shot(this.x, this.y);
                this.bt = 1;             // two yields between shots
                return;
            }
            // ... burst cooldown, attack-loop re-check, LOS-broken reset ...
```

It hits every checkpoint hash — the lowering discipline in its NOTES is genuinely clever —
but the control flow reads inside-out: you see phases, not behavior. The warden was the
worst case (8 phases + two flags whose reset-on-pain is itself gameplay, protected only by
a comment). That is the difference between *describing behavior* and *implementing a
scheduler by hand*.

### One HUD line, five ways

```jaiscript
string left = ` ${G.map_name}  KILLS ${G.kills}/${G.kill_total}  SCRT ${G.secrets}/${G.secret_total}`;
```
```chaiscript
var left = " ${G.map_name}  KILLS ${G.kills}/${G.kill_total}  SCRT ${G.secrets}/${G.secret_total}";
```
```python
left = f" {g.map_name}  KILLS {g.kills}/{g.kill_total}  SCRT {g.secrets}/{g.secret_total}"
```
```squirrel
local left = " " + g.map_name + "  KILLS " + g.kills + "/" + g.kill_total + "  SCRT " + g.secrets + "/" + g.secret_total;
```
```lua
local left = " " .. g.map_name .. "  KILLS " .. g.kills .. "/" .. g.kill_total .. "  SCRT " .. g.secrets .. "/" .. g.secret_total
```

Interpolation (jai `${}` with format specs, chai `${}`, python f-strings) reads the way the
HUD looks; Squirrel and Lua concatenate. Small thing, ~40 HUD/message sites, adds up.

### The render inner loop is the same everywhere

The wall-slice paint (fixed-point texture walk) is nearly identical in all five — this is
the code whose *cost*, not shape, separates the columns:

```jaiscript
int tacc = (cy0 - y0) * tstep;             // jai: typed locals, truncation built in
while (y < cy1) { pix[gi] = strip[tacc >> 11]; tacc += tstep; gi += vw; y = y + 1; }
```
```lua
local tacc = (cy0 - y0) * tstep            -- lua: the +1 tax on every subscript
for y = cy0, cy1 - 1 do pix[gi] = strip[(tacc >> 11) + 1]; tacc = tacc + tstep; gi = gi + vw end
```
```python
tacc = (cy0 - y0) * tstep                  # python: 0-based, reference values map straight in
for _ in range(cy0, cy1): pix[gi] = strip[tacc >> 11]; tacc += tstep; gi += vw
```

Squirrel is the jai loop with `local`; ChaiScript is the same loop **plus** per-pixel
ceiling/floor painting above and below it, because its vector-assignment aliasing made the
reference's one-line `pix = bg` background restore impossible (§4). Everyone writes 1985
code in the middle of the renderer; the languages differ in what that costs and what they
let you write at the edges (Python's comprehensions for strips/palettes, jai's
`parallel_transform` for rows).

### Per-subsystem LOC

| subsystem | jai | squirrel | lua | chai | python |
|---|---|---|---|---|---|
| render | 694 | 682 | 715 | 670 | 692 |
| data + art (defs/data) | 618 | 616 | 302* | 616 | 390* |
| game/tick/pilot/hash | 529 | 523 | 589 | 564 | 528 |
| **enemies (brains)** | **331** | **331** | **348** | **476** | **314** |
| maps + validator | 347 | 305 | 181 | 309 | 119 |
| hud | 233 | 245 | 251 | 248 | 227 |
| combat | 229 | 227 | 255 | 226 | 215 |
| sim | 153 | 152 | 166 | 148 | 176 |
| particles | 130 | 120 | 143 | 174 | 100 |
| pure (ray/particle step) | 164 | 92 | 95 | 47 | 115 |
| glue (util/main/state) | 133 | 161 | 161 | 149 | 110 |
| **script total** | **3561** | **3454** | **3206** | **3627** | **3351** |

\* Lua and Python machine-converted the art/map literals into generated data files rather
than hand-retyping 600 lines of pixel art (a correctness call, and an LOC discount).

**The say-it-directly verdict:** JaiScript, Squirrel, Lua, and Python all express the
game's central architecture — resumable brains with state in the suspension frame —
*natively*, and their enemies files land within ±10% of each other. ChaiScript must encode
it manually and pays +44% on exactly that file, concentrated in the one subsystem where the
design lives. The enemies row is the whole expressiveness section in one line of numbers.

---

## 4. Ease of implementation — rubric synthesis

### Binding experience (C++ ↔ script)

| | adapter LOC | character |
|---|---|---|
| JaiScript `dynamic_binder` | 140 | fluent builder; Rng class + free functions + globals, no fights |
| Lua / sol2 | 124 (~40 actual bindings) | lowest ceremony (`lua_["f"] = lambda`); one API fight (the 3.2.3 `error_handler` that doesn't exist — fixed via `set_default_handler`); ~10 s TU compile, `/bigobj` |
| ChaiScript | 155 (~30 of real bindings) | **the nicest API in the set** (`chai.add(fun(...))`, everything just works) — but core ships **no math library** (the adapter must bind `sqrt`/`cos`/…), and the TU costs ~50 s |
| Squirrel (raw C API) | 404 | no binder: push/closure/newslot per function, hand-rolled error path, every mistake a silent stack imbalance; two redeeming tricks (`sq_setclassudsize` embeds the Rng POD in-instance, `sq_setforeignptr` kills global state) |
| Python | n/a (standalone) | no embedding at all; ~150 lines of plain Python reimplement the host contract (ctypes console/keys, the Rng in 30 lines); the one trap was Windows console IO routing (57 → 0.63 ms/frame) |

Roughly: sol2 and dynamic_binder are peers at ~10× less code than raw Squirrel; ChaiScript
is friendlier than both until you need a square root.

### Time-to-first-running (port hours; reference wrote the game from scratch)

| | skeleton | first clean smoke-300 | notes |
|---|---|---|---|
| JaiScript (reference) | ~2 h | ~7 h in (first clean smoke incl. map fixes) | plus ~2 h perf campaign + ~1.5 h pilot/balance; it also wrote the spec |
| Squirrel | ~1 h | **+15 min after translation (~3.5 h)** | two reserved-word compile errors were the only failures; first compiled run matched |
| Lua | — | **~1 h 45 m, same moment as boot** | first successful run matched sim AND frame hash; ~45 min was reading spec+reference first |
| Python | — | **~3 h, same moment as boot** | zero script fixes; ~1 h reading spec + the Lua port first |
| ChaiScript | ~1.5 h | first full run (~4 h in) | the state-machine lowering discipline paid off; then ~3 h frame-byte forensics |

### Conformance difficulty (what the hash gate cost each language)

- **Squirrel / Lua / Python: first-run exact.** All three matched every STATE_HASH
  checkpoint on their first complete run. Their pre-paid taxes differed: Squirrel needed
  `.tointeger()` at ~40 typed-truncation sites; Lua needed the full int/float-subtype audit
  (`//` floors, one `idivt` site, `tostring(1.0)` poisoning) — "the tax a dynamically
  numeric language charges against a hash that quantizes doubles"; Python's int64 contract
  shrank to **two masked call sites** and its 0-based indexing deleted Lua's entire `+1` tax.
- **ChaiScript needed discipline, not luck**: the coroutine→state-machine lowering had to
  keep every RNG call site on the same tick as the reference (the "enter an N-yield wait =
  set `bt = N−1` and return" rule) — and it, too, then matched on the first full run.
- **The float knife edges** are where byte-parity work stopped, by ruling (§7): ChaiScript's
  tick-165 particle (`ttx` true value ±4e-16; the two runtimes' multiply-subtract chains
  round to opposite signs, moving one pixel for one frame), and the Lua/Python frame-3591
  finding — bisected to a *reference-side* render whose camera pose corresponds to no
  single tick state, reproduced on both reference backends at every worker count. The
  bisect tooling (monotone stream hashes + `--dump-frame` + scaled-mantissa prints)
  localized both to ground truth; that workflow is a spec deliverable in its own right.

### Debugging stories, compressed

Squirrel: nothing diverged, so mostly untested; wired-up error handlers give typed messages
with call stacks and locals — better than its reputation. Lua: the frame-3591 bisect was
the tooling's stress test and it held; monkey-patching instrumentation from an appended
chunk (no file edits) was the standout dynamic-language perk. ChaiScript: `pretty_print()`
traces are good, but 273 ms/tick means every bisect step costs minutes, and the
vector-aliasing bug required writing a probe script because the language offers no way to
see that two vectors share element storage. Python: `python -i` drops into a REPL where
`state.G.enemies[3].brain.gi_frame.f_lineno` tells you which line of its brain an enemy is
suspended on — the best introspection in the set, at zero setup. JaiScript: the two-backend
parity harness and the boot-time map validator (throwing template strings) caught its bugs
before they were mysteries; it also has the only actual step-debugger (DAP, breakpoints on
the interpreter backend).

---

## 5. Play

All five run the same game with the same keys (host-supplied held-key input for the C++
ports; Python reimplements `GetAsyncKeyState` via ctypes, foreground-gated the same way).
At a real 100×40 Windows Terminal, quad pixels:

- **Lua** and **Squirrel** are indistinguishable at the pad: hundreds of uncapped fps,
  paced to 30, input crisp, zero hitches over long sessions (no GC cliff at 16000 ticks).
- **Python** holds a comfortable 30 fps (276 uncapped) *once frames go out through
  `os.write`*; on the naive stdout path it would be a 17 fps stutter in Windows Terminal.
  Strafe-running while turning and firing feels identical to the C++ hosts.
- **JaiScript** sits at ~31 fps uncapped — i.e. it *makes* its 30 fps budget at quad with
  workers on, with nothing to spare; sext drops below target on this machine. It plays
  correctly and smoothly at the default; it is the only implementation where you can feel
  the renderer working.
- **ChaiScript is ~3.5 fps.** The game is "technically fully playable" — every mechanic
  works, the pilot clears the episode — and it is genuinely unplayable by a human at real
  time: a keypress-to-photon latency near 300 ms turns a shooter into a slideshow. This is
  what an 8× gap below the *reference's* budget looks like in the hand.

---

## 6. Honest JaiScript self-assessment

What five implementations of its own game proved, and what they exposed. (Per the standing
ruling, the reference's language-feel is reported happy-path — engine-bug detours excluded
from hours — but the bugs the exercise *found and fixed* are listed below as dogfooding
value, because that is half of why the exercise existed.)

**Proved:**

- **Spec-first determinism works.** REFERENCE.md's operational contract (tick order, RNG
  consumption order, the quantizing STATE_HASH) was strong enough that three of four ports
  matched bit-exactly on their first complete run and the fourth on its first full attempt
  — across four runtimes with different number models, index bases, and object semantics.
  The checkpoint-hash + frame-dump bisect workflow localized every divergence that did
  occur, down to single ulps.
- **Two-backend parity survived contact.** Interpreter and VM produced identical hashes and
  identical frame bytes at every worker count through the entire exercise — including the
  captured-reads renderer rework. Five reimplementations of the semantics found zero
  backend divergences.
- **The language said everything directly.** Coroutine-method brains, typed fields
  catching transposed-argument bugs at the write site, template strings, `shared_ptr`
  registries, a throwing boot-time validator — and the set's only in-language parallelism,
  which went from "Amdahl wall" to "halved the wall stage" when captured reads landed
  (§2.5). Nothing in GLOOM had to be encoded around a missing feature; ChaiScript's +44%
  enemies file shows what that costs when it isn't true.
- **Dogfooding paid in fixed engines, not just findings.** The GLOOM feedback bundle
  landed as real fixes with pinned tests (`gloom_feedback_tests.cpp`, commits
  0cda8e9f/4610a684/bf5983d9/381c93fd/888228b3): the one store-copy kernel
  (`clone_for_assignment` — assignment-shares-handles held at ~20 sites on both backends),
  decl-path reference normalization (silent live aliases in typed decls), read-only string
  subscript `s[i]`, host-boundary propagation of uncaught script throws, and a pinned
  misdiagnosis (`var name() {}` methods work). Captured reads itself (v0.5) is a language
  feature GLOOM's glyph wall motivated, designed, and validated.

**Exposed:**

- **The 10–24× real-game gap** (§2.1) between the VM and Lua/Python/Squirrel on
  grid-consumption workloads — while *winning* the microbenchmark suite against Squirrel.
  The gap has a name and an address: **element-read / value-traffic cost** (~370–575 ns per
  scripted element read; 32-byte tagged values on every load), and it is the next perf
  campaign. Until it lands, the honest guidance stands: JaiScript holds 60 ticks/sec of
  full game *sim* in ~2 ms and earns its price in gameplay code; per-pixel software
  rendering is where it pays list price.
- **Value-semantics vigilance at scale** (REFERENCE.md §5): every rule is individually
  defensible; the asymmetries between them (`var e = arr[i]` deep-copies, `var& x =
  G.field` doesn't bind, ternary-of-element mis-typing) are a real tax at 3.5k lines — the
  feedback bundle removed the outright traps, and the remaining tax is documented rather
  than dissolved.
- **Squirrel's NOTES said it cleanly**: the 10× margin "buys no architecture — everything
  it runs fast, JaiScript also expresses"; the inverse is also true — everything JaiScript
  expresses, three other runtimes ran 10–24× faster. Expressiveness parity is table stakes
  among the healthy runtimes here; the differentiators are perf (Lua), introspection
  (Python), C++ ergonomics (jai/sol2/chai), and typed-write-site safety (jai alone).

---

## 7. Methodology appendix

- **Machine**: i7-6920HQ (4C/8T), Windows 10 19045, Windows Terminal, 100×40 console.
  All cross-language rows same machine; quiet unless noted (the captured-reads delta and
  parts of the chai runs are flagged loaded — min-of-N interleaved runs, delta-not-absolute
  claims).
- **Builds**: Release `/O2` (`/GL` where the port notes say so) for every C++ host;
  CPython 3.12 x64 for Python (3.8-32bit measured separately as the version-cliff row).
  Squirrel with `_SQ64` + `SQUSEDOUBLE` (static_assert-gated); ChaiScript 6.1.0
  `CHAISCRIPT_NO_THREADS`.
- **Protocol** (PORTING.md §6): headless `--smoke --ticks 300` ms/tick per pix mode
  (min-of-5 ports, min-of-3 jai), `--bench 300` sim/draw split, optional early-return
  ablation; ±10% is noise.
- **Conformance gates**: STATE_HASH at 300/2000/3000/16000 (seed 666) = 3580805725 /
  319812559 / 4080154357 / 3497451110; cross-seeds 7/99/4242 @3000; `--god --seed 5`
  @12000; pix-independence of sim hashes. Frame-stream FNV-1a as the optional byte-parity
  gate (quad ground truth `7fecc09815bb64e0` at 300). All five implementations pass every
  sim gate; frame parity per the table in §1, as of each port's landing (the tick-3591
  reference-side quirk is under separate investigation and does not touch sim hashes).
- **The relaxed bar (Dev ruling, mid-exercise)**: ports are "playable + outcome-reasonable,
  written as realistic idiomatic code" — byte-parity forensics stop at ground truth rather
  than contorting a port to force the last ulp. Applied twice (chai tick-165, lua/python
  frame 3591), both documented with exact mantissas in the respective NOTES.
- **Primary records**: [REFERENCE.md](../examples/gloom/REFERENCE.md) ·
  [PORTING.md](../examples/gloom/PORTING.md) ·
  [gloom README](../examples/gloom/README.md) ·
  [squirrel](../examples/gloom/ports/squirrel/NOTES.md) /
  [lua](../examples/gloom/ports/lua/NOTES.md) /
  [chai](../examples/gloom/ports/chai/NOTES.md) /
  [python](../examples/gloom/ports/python/NOTES.md) NOTES ·
  [PERFORMANCE.md](PERFORMANCE.md) (microbench context) ·
  `docs/parallel_design.md` §13 (captured reads).
