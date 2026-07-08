# Python 3 (standalone) port notes

Filled-in PORTING.md section 7 rubric. Unlike the embedded ports (chai /
squirrel / lua) this one is STANDALONE: `python gloom.py` — no C++ adapter, no
CMake, no shared host exe. Rationale: embedding CPython means shipping the
interpreter DLL + stdlib and negotiating the GIL/lifecycle for zero gain here;
standalone is Python's native condition. Instead `gloom.py` reimplements the
thin host from `gloom_host.cpp` faithfully in ~150 of its lines — the
xorshift64* Rng, the VT console (alt-screen + truecolor via ctypes
`SetConsoleMode`, UTF-8 CP), held keys (`GetAsyncKeyState`, foreground-gated
like the host), msvcrt edge keys, frame pacing, all the flags, and the
`--smoke` / `--bench` harnesses printing the same report formats. The game is
genuinely playable in a real terminal, same keys as the other ports.

## Conformance

**All STATE_HASH gates green, first complete run** (CPython 3.12.x and 3.8.2
produce identical hashes):

| `--smoke --ticks N` (seed 666, quad) | STATE_HASH | matches ref |
|---|---|---|
| 300 | 3580805725 | yes |
| 2000 | 319812559 | yes |
| 3000 | 4080154357 | yes |
| 16000 | 3497451110 | yes |

Cross-checks: seeds 7/99/4242 @3000 → 1696980843 / 1855347375 / 2848371116;
`--god --seed 5` @12000 → 576425398; `--pix half`/`sext` @3000 → sim hash
unchanged (4080154357). Gameplay outcome matches the spec expectation: E1M1
cleared 7/7 kills, 1/1 secrets, 0 deaths by tick ~3000; deep in E1M2 at 16000.

**Frame-byte parity**: the frame-stream FNV-1a hashes equal the Lua port's
verified values at every checkpoint both ports share — 300/2000/3000 quad
(`7fecc09815bb64e0`, `cf05be38eb174fbe`, `0ccc249e860ef7f4`) and 3000
half/sext (`4875f91aa68b18dc`, `c741895114fe6f84`). Those streams were
themselves bisect-verified byte-identical to the JaiScript reference through
frame 3590 (see `ports/lua/NOTES.md` for the reference-side render quirk at
3591), so this renderer is byte-exact against the same ground truth. The full
render pipeline runs every tick — DDA walls, texture strips, billboards,
z-clipped particles, automap/hurt overlays, glyph-row ANSI construction —
nothing elided.

Documented deviations (all REFERENCE.md-sanctioned or hash-neutral):

- Flat per-cell render-kind list (`G.mapkind`) instead of the 15-per-int64
  packing (4.2 permits this for serial ports); rebuilt on tile change.
- `gloom_particle` mutates slots in place (same numbers, no allocation).
- Warden summons are appended AND ticked on the spawn tick (live-length
  `while` loop mirrors the reference interpreter's range-for).
- Title tagline stays byte-identical to the reference ("all JaiScript") so
  frame-hash comparability holds.
- The smoke harness computes the frame-stream FNV-1a OUTSIDE the timed window
  and reports its cost separately (~2.9 ms/tick of pure-Python hashing — more
  than the whole game frame; in the C++ hosts the same fold is noise). The
  timed work is identical to what the shared host times: sim + full frame
  string, written to a buffer.

## Binding experience

**N/A (not embedded)** — the rubric column for this port. What replaces it:
reimplementing the host contract (REFERENCE.md section 2) was ~150 lines of
plain Python inside `gloom.py` (365 total with the harnesses/CLI), vs the
530-line shared C++ host + ~124-line adapter the embedded ports lean on. The
Rng is 30 lines with two explicit `& MASK64` sites; ctypes covers
`SetConsoleMode`/`GetAsyncKeyState`/`GetConsoleWindow` without any build step.
One real trap, found by measurement: writing frames through
`sys.stdout.buffer` routes into `_WindowsConsoleIO` (UTF-8 → UTF-16 →
`WriteConsoleW`) and cost **57 ms/frame** in Windows Terminal (8.9 in
conhost); `os.write` on the stdout fd takes the raw `WriteFile` path under
CP 65001 — the C++ host's mechanism — and draws the same bytes in 0.63 ms.

## Porting friction

- **The int64 discipline is two call sites.** Python ints are
  arbitrary-precision, so the 64-bit wraparound the reference relies on is
  opt-in: the Rng step masks after the `<< 25`, and `mix32` masks its lanes.
  Nothing else in the sim can overflow. Compare Lua, where int/float SUBTYPE
  drift was the whole conformance risk: Python has none of that — `int(f)` IS
  C truncation, `//` floors exactly like Lua's (same single `idivt` site for
  the one negative-numerator division), and floats are the same IEEE doubles.
- **0-based indexing means the port DELETES the Lua port's entire `+1` tax.**
  Tiles, art pixels, palette indices, hash lanes: the reference's 0-based
  values map straight onto Python lists. The glow-trail phase rule is
  literally the reference's `(tick + i) % 2` with `enumerate`.
- **Generator brains are a 1:1 mapping and the best part.** A coroutine brain
  becomes a generator method; `for _ in range(9): yield` IS the telegraph;
  pain-interruption = drop the handle; "is it dead" = `gen.gi_frame is None`.
  The autopilot yields an `Input` NamedTuple per tick from the same pattern.
- **Shared mutable globals vs the module system** was the one structural
  fight: `G`/`RNG` live as attributes of `state.py`, and the import cycles the
  game's call graph wants (enemies→hud→render→enemies) demand plain
  `import module` + call-time attribute access — a from-import anywhere on the
  cycle breaks at boot. Mechanical rule, applied everywhere.
- **Fast CPython is a dialect.** The renderer is written like the Lua port's:
  hoisted locals for every hot list/function (`ap = parts.append`), flat index
  arithmetic, manual min/max chains. Comprehensions and slice-assign live at
  the edges (`pix[:] = bg` is `table.move`; `[t2k[t] for t in tiles]` is the
  snapshot rebuild) but the per-pixel cores are 1985-style loops, because
  that's what the interpreter rewards. It reads fine; it just isn't the
  Python of tutorials.
- Data as code: `data.py` is machine-converted (~60-line one-shot Lua-table
  parser) from the Lua port's generated `data.lua`, keeping art/maps
  byte-exact through a second generation rather than trusting hand-retyping.

## LOC

Script total **3351** (of which 359 generated data; 2986 game + host):

| file | LOC | subsystem |
|---|---|---|
| render.py | 692 | raycast view, strips, billboards, row builders |
| game.py | 528 | Game class, tick, autopilot, hash |
| gloom.py | 365 | entry points + the reimplemented host (Rng, console, smoke/bench) |
| data.py | 359 | GENERATED: weapons/bestiary/items/art/maps |
| enemies.py | 314 | Enemy class + 4 generator brains |
| hud.py | 227 | HUD + full-screen states |
| combat.py | 215 | LOS/hitscan/projectiles/damage |
| sim.py | 176 | movement/use/pickups |
| maps.py | 119 | map parser + boot validator |
| pure.py | 115 | serial ray + particle step |
| particles.py | 100 | pool + burst kit |
| util.py | 69 | math/mix32/palette intern |
| defs.py | 31 | art parser |
| world.py | 23 | tile queries + raycast snapshot |
| state.py | 18 | globals |

(vs Lua 3206 + 124 C++ adapter; reference 3561 + 530 host. Python needs no
adapter and its host substitute is counted in the total above.)

## Time-to-first-running

- Skeleton boots / first clean smoke 300: **~3h** from cold start, and — same
  as the Lua port — they were the same moment: the first complete
  `--smoke 300` matched the checkpoint hash AND the reference frame-stream
  hash. (~1h of that was reading REFERENCE.md + the entire Lua port before
  writing anything; the Lua NOTES' int/float audit largely dissolves in
  Python, which is why nothing needed debugging.)
- Full 16000 conformance + seed/god/pix cross-checks: +15 min (run time).
- Zero script fixes for conformance. Post-conformance iterations were
  harness-side only: compacting the generated data emitter (LOC parity;
  hashes unchanged) and switching draws off `_WindowsConsoleIO`.

## Debugging story

Nothing to bisect — every sim checkpoint and every frame-stream checkpoint
passed on the first complete run, so the tooling claims are untested in anger
here. What the language offers if it had gone wrong: exceptions with real
tracebacks (file:line through the whole call stack) at zero setup,
`python -i gloom.py` drops into a REPL with the entire world poke-able
(`state.G.enemies[3].brain.gi_frame.f_lineno` tells you which line of its
brain an enemy is suspended on — the Lua port needed a monkey-patch chunk for
less), and the same monotone stream-hash + `--dump-frame` bisect workflow the
Lua investigation used. The genuinely dangerous corner is silent float→int
context: Python has no typed locals, so every truncation site is a visible
`int()`/`//` decision, same discipline as Lua but with one fewer trap
(no float subtype to poison string formatting).

## Expressiveness excerpts

The turret brain, whole (`enemies.py`) — the phase machine IS the control
flow; a burst is three shots two yields apart, and pain-interrupt anywhere in
here resumes from a fresh generator next tick:

```python
def brain_turret(self):
    """TURRET: dormant metal until it has line of sight; then 3-round bursts."""
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

Comprehension-shaped render bits — the raycast snapshot rebuild, a lazy
texture strip, and the whole particle palette (`world.py` / `render.py` /
`particles.py`):

```python
g.mapkind = [t2k[t] for t in g.tiles]                              # tile -> render-kind snapshot

s = [wallt[tbase + _tex_tone(kind, tx, ty)] for ty in range(32)]   # one 33-tall wall strip
s.append(s[31])                                                    # fixed-point overshoot guard

PART_COLS[:] = [rgb_idx(r, g, b) for ramp in _RAMPS for (r, g, b) in ramp]
```

And the honest counterweight: the per-pixel cores (wall-slice paint, the
glyph-row builders) are hoisted-locals `while`/`for` loops indistinguishable
in shape from the Lua port's — at 27k sub-pixels a frame, CPython pays you to
write it flat, so the comprehension-Python lives at the edges and the
1985-Python owns the middle.

## Perf

CPython 3.12 (64-bit), same machine/session, background desktop apps at ~14%
load (not lab-quiet; min-of-5 used throughout, spread noted). Release-grade
comparisons: the C++ hosts' numbers from their NOTES/README runs.

Headless `--smoke --ticks 300`, ms/tick (python min-of-5):

| pixels | Python 3.12 | Lua/sol2 | Squirrel | JaiScript VM | Python vs VM |
|---|---|---|---|---|---|
| half (100x68) | 1.67 | 1.008 | — | 21.911 | 13.1x faster |
| quad (200x68, default) | **2.53** | 1.407 | ~2.9 | 33.996 | **13.4x faster** |
| sext (200x102) | 4.06 | 1.968 | — | 43.091 | 10.6x faster |

Run-to-run spread on this machine was 2.5–4.1 quad (best single run 2.39);
mins are stable across batches. Version matters enormously: the machine's
default `python` (3.8.2, 32-bit) runs the identical code, bit-identical
hashes, at **13.3 ms/tick quad** — 5x slower than 3.12. No PyPy on the
machine (`py -0`), so no JIT row; expect it to change the story.

16000-tick sustained: **2.81 ms/tick** average (2000-tick: 2.78, 3000: 2.81)
— flat, no GC/allocation cliff; the late game simply has more live entities
than the 300-tick window. The harness reports the pure-Python frame-hash cost
it excluded (~2.9 ms/tick) on every smoke run for transparency.

Interactive `--bench 300 --w 100 --h 40` (quad, autopilot, real console
writes):

| | sim ms/frame | draw ms/frame | total | fps uncapped |
|---|---|---|---|---|
| gloom.py (Windows Terminal) | 2.99 | 0.63 | 3.62 | 276 |
| gloom_lua (WT, from its NOTES) | 1.43 | 0.02 | 1.44 | 692 |
| jai_gloom vm (WT) | 32.39 | 0.01 | 32.40 | 31 |

The with-console-write number differs from the buffer-timed smoke number by
that 0.63 ms/frame of real terminal write (drawn with `os.write`; through
Python's default console stream it was 57 ms in WT / 8.9 in conhost — see
Binding experience). The game holds a comfortable interactive 30 fps either
way; playable with the same keys as the other ports.

## Net

Python came in as the presumptive expressiveness king and mostly kept the
crown at a surprisingly low price: **~2.5 ms/tick quad on CPython 3.12 — about
1.8x Lua, at or under Squirrel, and ~13x faster than the JaiScript VM** whose
entire 34 ms budget this port spends 7% of. Conformance was the cheapest of
any dynamic-language port so far: the int64 contract shrank to two masked call
sites, 0-based indexing made the reference's values native instead of taxed,
and generator brains + an Input NamedTuple pilot are a strictly nicer fit than
Lua coroutines (same 1:1 mapping, better introspection). What it charged:
the fast-CPython dialect in the renderer (hoisted-locals loops, not
comprehensions — the expressive Python lives at the edges), a 5x cliff if
someone runs the wrong interpreter version, module-vs-globals ceremony for a
game built on one world object, and a draw path where C++ hands bytes to the
console for free while Python pays milliseconds. For a game this shape, as a
thing you `python gloom.py` and play: first-run sim conformance, byte-exact
frames against the same ground truth as the Lua port, zero build.
