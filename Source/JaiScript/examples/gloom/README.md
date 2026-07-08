# jai_gloom — GLOOM: a terminal DOOM-like, all JaiScript

A real-time 2.5D raycast shooter in the terminal — one tight four-map episode of
corridors, keycards, secrets, and things that roar before they charge — written
**entirely in JaiScript**. The C++ host (`main.cpp`) is the same thin shell as the
crawler/demoreel: a VT console, held-key input, a seeded `Rng`, a monotonic clock,
flags as globals. Everything else — the raycaster, the sub-cell renderer, enemy
brains, weapons, particles, the episode, the autopilot — is script.

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

Two systems run through `parallel_transform` every tick, both bodies in
`pure.jai` (the admission contract: element + locals only, math whitelist):

- **Wall rays** — the pure-value contract means shared inputs ride inside every
  element, so elements are column *chunks* (16 of them), each carrying the map
  snapshot packed 15-tiles-per-int64; the DDA inner loop is pure shift/mask.
  One ray per cell column; sprites and particles keep full sub-cell resolution.
- **Particles** — the 288-slot pool is a flat value array stepped by one pure
  function (fixed timestep baked in as a literal, since the body can't read
  enclosing state).

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

The honest headline: the *parallel* parts are cheap and scale (the ray stage
alone runs 2.2x at W=4), but they are a small slice — the serial consumption
of the pixel grid is the wall. An element read through the VM costs ~0.5 µs,
and the row builder must read all 13.6k sub-pixels per frame; that one stage
costs more than rays, paint, and sim combined, so total ms/tick barely moves
with worker count (Amdahl, working as advertised). A `parallel_for` with
per-thread pads over row bands would dissolve it; the builtin's value-only
contract can't, because the pixel grid would have to ride inside the elements.
One curiosity: `--pix half` emits MORE bytes per frame than quad (35.6 KB vs
21.5 KB) because flat quad cells are a 1-byte space on background while every
half-mode cell is a 3-byte half-block glyph.

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
main.cpp            thin host: console, held keys, Rng, clock, flags, smoke/bench harness
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
