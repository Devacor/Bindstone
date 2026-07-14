# jai_gloom — GLOOM: a terminal DOOM-like, all JaiScript

A real-time 2.5D raycast shooter in the terminal — one tight four-map episode of
corridors, keycards, secrets, and things that roar before they charge — written
**entirely in JaiScript**. The C++ host (`gloom_host.*` + `gloom_adapter_jai.cpp`)
is the same thin shell as the crawler/demoreel: a VT console, held-key input, a
seeded `Rng`, a monotonic clock, flags as globals — now split into a shared,
runtime-agnostic core and a per-language adapter so the cross-language
comparison ports (ChaiScript/Squirrel/Lua, opt-in CMake targets) reuse the
exact harness. PORTING.md is the porter's guide. Everything else — the raycaster, the sub-cell renderer, enemy
brains, weapons, particles, the episode, the autopilot — is script.

**The five-way comparison report** (JaiScript reference vs the Squirrel / Lua /
ChaiScript / Python ports — perf, conformance, expressiveness, play) is
[docs/GLOOM_COMPARISON.md](../../docs/GLOOM_COMPARISON.md); the per-port
primary records are `ports/<lang>/NOTES.md`.

GLOOM is the crawler's bigger sibling and a deliberate dogfood of this month's
language work: **`parallel_transform`** drives the wall rays and the particle pool,
**coroutine handles living in fields** drive every enemy brain and the demo pilot,
`new`/`shared_ptr` holds the world and its registries, and template strings (with
the new `:spec` formats) build the HUD.

## The look (doom-cli's trick, in script)

The view is a truecolor pixel grid blitted through **sub-cell block glyphs**:

| mode | pixels/cell | glyphs | notes |
|---|---|---|---|
| `--pix half` | 1x2 | `▀` | square pixels, cheapest |
| `--pix quad` | 2x2 | `▘▚▟█…` | **default**; 2 colors/cell by luminosity partition |
| `--pix sext` | 2x3 | `🬀…🬻` | needs a Unicode-13 font (Cascadia Mono in Windows Terminal) |

Every color the game shows is interned once into a truecolor palette (pixel grids
store small indices; the row builder emits prebuilt `38;2;r;g;b` escapes only when
a cell's colors change). Walls get 16 distance shades x 2 face shades x 3 texture
tones per kind; sprites are hand-drawn pixel-art bitmaps (14-16 px enemies, an
18-px boss) shaded per distance band through cached LUTs, with damage flashes and
multi-frame deaths; particles are z-buffered pixel confetti — muzzle flash,
impacts, blood, gibs, smoke, hexmotes.

## Build & run

Part of the normal JaiScript CMake build (`JAISCRIPT_BUILD_EXAMPLES=ON`).

```bash
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_gloom   # PLAY THIS ONE
cmake --build out/build/x64-Debug --target jai_gloom                  # debug iteration
```

Play the Release exe in **Windows Terminal** (truecolor + fast VT). The exe
prefers the source `scripts/` dir; a `gloom_scripts/` copy sits next to the
binary.

```bash
jai_gloom.exe                        # seed 666, VM backend, quad pixels
jai_gloom.exe --pix sext             # the full doom-cli look (font permitting)
jai_gloom.exe --backend interpreter --seed 7
jai_gloom.exe --workers 0            # serial script loop instead of parallel_transform
jai_gloom.exe --bench 300            # autopilot plays while you read ms/frame numbers
```

| key | action |
|---|---|
| `w` / `s` | move forward / back |
| `a` / `d` | strafe |
| `left` / `right` | turn |
| `space` / `ctrl` | fire |
| `e` | use — doors, suspicious walls, the exit switch |
| `shift` | run |
| `1` `2` `3` | pistol / scattergun / hexcaster |
| `m` | automap overlay |
| `esc` | quit |

Movement keys are honest held-key state (`GetAsyncKeyState`), so strafe-running
while turning and firing works like it should.

### No C++ host: the standalone runner

`gloom.jai` is a drag-drop shim for the standalone `jaiscript.exe` runner: it
supplies the host API of REFERENCE.md section 2 in pure script (a bit-exact
xorshift64\* `Rng` under checked overflow, `host_log`/`itrunc`/`ifloor`/`utf8`/
`ESC`, the `HOST_*` globals) on top of the runner's console IO, then loads
`scripts/` and runs the host pump. Flags go after `--`:

```bash
jaiscript.exe gloom.jai                          # play (esc quits)
jaiscript.exe gloom.jai -- --smoke --ticks 300   # headless parity self-test vs section 3.5
jaiscript.exe gloom.jai -- --bench 300           # autopilot + sim/draw split
```

`--smoke` exits 0 only when `STATE_HASH` matches the reference checkpoint
table. The idiomatic rewrite has the same shim:
`../gloom_idiomatic/gloom_idiomatic.jai`.

## The game

Four maps: **DIMLIT ANTECHAMBER** (grunts, and a wall that isn't), **COOLANT
WARRENS** (spitters behind coolant vats, a red lock), **THE HEX FOUNDRY** (the
hexcaster lives here, behind the blue lock the turrets watch), **GLOOM'S THRONE**
(him). Kills / secrets / par-time tally after each floor; health, armor, three
ammo pools, keycards; a face that grins when you pick up a new gun.

Enemies (each brain is a `coroutine` **method** whose handle lives in a field on
the enemy itself — phase state is coroutine-frame locals, pain interrupts by
discarding the handle mid-phase):

- **GRUNT** — dozes until it sees or hears you, roars (it commits), then zigzag
  charges into a lunge with a real windup.
- **SPITTER** — holds a range band, orbits sideways, telegraphs a green gob.
- **TURRET** — dormant metal until it has line of sight; wakes, 3-round bursts.
  Break line of sight and it goes back to wary idle — its coroutine never forgets.
- **THE WARDEN** — stalks and lobs hollow fire; under half health he goes
  double-volley and calls the family. Exactly once.

Weapons: pistol (accurate, honest), **SCATTERGUN** (6 pellets, crowd opinions),
**HEXCASTER** (a projectile that detonates into a particle bloom and area damage —
including yours, standing in your own spell is a choice).

## parallel_transform, honestly

Three systems run through `parallel_transform` every tick — two pure bodies in
`pure.jai` (element + locals only, math whitelist) and one captured-reads body
in `render.jai`:

- **Wall rays** — written pre-capture: shared inputs ride inside every element,
  so elements are column *chunks* (16 of them), each carrying the map snapshot
  packed 15-tiles-per-int64; the DDA inner loop is pure shift/mask. One ray per
  cell column; sprites and particles keep full sub-cell resolution. (This is
  the pair idiom — with captured reads it could read a global snapshot instead;
  kept as-is as the hand-chunking reference shape.)
- **Particles** — the 288-slot pool is a flat value array stepped by one pure
  function (fixed timestep baked in as a literal).
- **Glyph rows (quad)** — `gloom_row_quad(ry)` reads the CAPTURED pixel grid
  (borrow tier: zero-copy) and palette tables (per-worker snapshots); see the
  v0.5 section below.

`--workers 0` runs the same pure functions in a plain serial loop — determinism
across worker counts is checked by construction (`--smoke` hashes are identical
at any `--workers`).

Measured (i7-6920HQ 4C/8T, Release, 100x40 console, seed 666, quiet machine,
`--smoke` headless ms/tick, 300-tick runs; every row hash-identical):

| pixels \ backend | VM | interpreter |
|---|---|---|
| half (100x68) | 22.2 | 27.4 |
| **quad (200x68, default)** | **31.3** | 39.5 |
| sext (200x102) | 42.3 | 51.7 |

| workers (quad) | VM ms/tick | interp ms/tick |
|---|---|---|
| 0 (serial script loops) | 34.1 | 42.0 |
| 1 | 33.6 | 39.0 |
| 2 | 37.4 | 41.6 |
| 4 | 31.3 | 39.5 |
| 8 | 34.1 | 43.2 |

Single 300-tick runs; treat ±10% as machine noise (a thermal-loaded rerun of
this table once showed serial interp at 64 ms — measure quiet or measure twice).

Stage ablation (VM, quad, W=4, measured by early-returning the pipeline):
sim ~2 ms, ray chunk build ~1 ms, ray transform ~1.5 ms (2.2x over its serial
cost at W=4), wall paint ~6 ms, sprites+particles ~3 ms, **glyph-row building
~16 ms**.

The honest headline (v0, historical): the *parallel* parts were cheap and
scaled (the ray stage alone ran 2.2x at W=4), but they were a small slice — the
serial consumption of the pixel grid was the wall. An element read through the
VM costs ~0.5 µs, and the row builder must read all 13.6k sub-pixels per frame;
that one stage cost more than rays, paint, and sim combined, so total ms/tick
barely moved with worker count (Amdahl, working as advertised). The v0
value-only contract couldn't touch it, because the pixel grid would have had to
ride inside the elements. One curiosity: `--pix half` emits MORE bytes per
frame than quad (35.6 KB vs 21.5 KB) because flat quad cells are a 1-byte space
on background while every half-mode cell is a 3-byte half-block glyph.

### Captured reads dissolve the glyph wall (v0.5, 2026-07-08)

`parallel_transform` captured reads (docs/parallel_design.md §13) let a body
READ enclosing globals, so the quad glyph-row builder is now a per-row function
over `ROWIDX` reading the pixel grid as a captured **borrow** (`PIX` is a flat
all-primitive int array touched only by subscript — zero copies, raw element
reads; the escape-string palettes `PAL_FG`/`PAL_BG` snapshot per worker per
frame). `--workers 0` runs the same row function in a serial loop; frame bytes
are **byte-identical** to the old serial builder at every worker count and
STATE_HASH is untouched (all reference checkpoints re-verified, incl. 3000 →
4080154357 and seed 7 → 1696980843).

Before/after on the same binary (min-of-3 interleaved runs, quad, seed 666,
300 ticks, loaded dev machine — absolute numbers differ from the quiet-machine
table above; the DELTA is the claim):

| workers (quad) | VM before | VM after | interp before | interp after |
|---|---|---|---|---|
| 0 (serial) | 46.6 | 45.3 | 53.3 | 58.3 |
| 1 | 39.1 | 37.3 | 47.5 | 45.4 |
| 4 | 46.3 | **35.3** | 50.6 | 40.5 |
| 8 | 41.1 | **33.0** | 51.9 | 37.3 |

Reading it honestly: at W=4 the vm drops ~11 ms/tick — the ~16-23 ms glyph
stage roughly halves (Amdahl residue: the per-frame palette snapshots, the
barrier's all-primitive scan of the 13.6k-int grid, and the serial paint stages
that still dominate). Serial (`--workers 0`) is a wash on the vm and a few ms
slower on the interpreter — the row function allocates its parts array per row
and reads globals instead of `var&`-cached aliases (captured names may not be
aliased), the small price of one implementation serving both paths. Residual
restructuring cost beyond the capture itself: moving the grid from a `Game`
field to the global `PIX` (captures resolve global names), plus `VW`/`VH`/
`ROWIDX` globals — a ~20-line diff, no algorithm changes.

## Determinism

`--smoke` runs the seeded autopilot headless for N ticks on BOTH backends and
compares a per-tick FNV `STATE_HASH` (player, enemies, particles, rng state) AND
a hash of every rendered frame string — sim determinism and frame *byte* parity,
interpreter vs VM, at any worker count:

```bash
jai_gloom.exe --smoke --ticks 6000            # both backends, parity table
jai_gloom.exe --smoke --ticks 6000 --workers 8
```

The autopilot is itself a coroutine handle in a Game field: it fights what it
sees, loots what it can absorb, hunts the secrets it "remembers", opens what its
keycards allow, pulls the exit switch, and — on the throne floor — goes and has
words with the landlord.

## Files

```
gloom_host.hpp/.cpp thin SHARED host: console, held keys, Rng, clock, flags,
                    smoke/bench harness; language-agnostic (PORTING.md has the
                    adapter contract for the cross-language comparison ports)
gloom_adapter_jai.cpp  the JaiScript adapter + main() (this exe)
gloom_adapter_{chai,squirrel,lua}.cpp  port scaffolds (opt-in CMake targets)
scripts/state.jai   RNG global
scripts/util.jai    math helpers, mix32, the truecolor palette intern
scripts/defs.jai    weapons, bestiary, pickups, ALL sprite art, weapon HUD art
scripts/maps.jai    the four maps + parser + boot-time validator + packed snapshot
scripts/pure.jai    the parallel_transform bodies: ray chunks, particle step
scripts/particles.jai  pool class (access labels), burst kit, color ramps
scripts/enemies.jai the Enemy class: coroutine brains in fields, pain, deaths
scripts/combat.jai  LOS, hitscan, projectiles, explosions, player damage
scripts/sim.jai     movement + sliding, use (doors/secrets/exit), pickups
scripts/render.jai  raycast view: chunked rays, strip cache, billboards,
                    particles, automap, half/quad/sext row builders
scripts/hud.jai     status strip, vitals, face, weapon art, title/tally/death
scripts/game.jai    Game class (typed shared_ptr global), fixed-timestep tick,
                    the autopilot coroutine, STATE_HASH
scripts/main.jai    imports + host entry points
```
