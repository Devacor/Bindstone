# jai_crawler — GRUBWELL: dungeons of the unwashed

A first-person, step-based dungeon crawler in the spirit of the 1991 shareware
big-hairies (garish VGA corridors, a compass, monsters that get *bigger* as they
walk at you) — written **entirely in JaiScript**. The C++ host (`main.cpp`) is the
same thin shell as `examples/roguelike`: a VT console, a blocking `read_key()`, a
seeded `Rng`, file IO for saves, flags as globals. Everything else — the DDA
raycaster, dungeon gen, combat, AI, items, saves, UI — is script.

Same bones as the roguelike, different eyes: movement is **locked to the four
cardinal facings**. `w` steps a tile, `a`/`d` snap 90 degrees (with a fast 3-frame
heading sweep so the turn reads as motion), and the view is a real one-ray-per-
column raycaster — walls shaded by torch distance, faces darkened by orientation,
materials that change wardrobe every couple of floors, and billboard sprites with
per-column z-buffer occlusion. A distant Grublin is a small green blob at the end
of the hall. It does not stay small.

## Shared logic: copied, not forked (on purpose)

`combat.jai`, `items.jai`, `entities.jai`, `data.jai`, `ai.jai`, `dungeon.jai`,
`util.jai`, `state.jai` are **snapshots of the roguelike's committed versions** —
the game rules are deliberately identical (one key = one turn; the AI plays the
same grid and does not care where the camera points). They are copies for now and
will unify once the upcoming `include` module system lands. Three small deltas,
each marked with a `CRAWLER DELTA` comment at the site:

- `dungeon.jai` — `bfs_step` searches FOUR_DIRS (a facing-locked body cannot step
  diagonally, so the autopilot must not be handed diagonal moves).
- `combat.jai` — class skills target **down the facing** (`facing_target_index`,
  walls block) instead of auto-picking the nearest visible monster.
- `ai.jai` — the boss case mints its coroutine from `G.boss_brain(...)`, a
  **coroutine method** on Game (see language notes below).

New for the crawler: `view.jai` (the raycaster), `ui.jai` (compass panel, minimap,
modals), `game.jai` (facing movement, crawler turn loop, saves, autopilot),
`main.jai`, and the adapted host `main.cpp`.

## Build

Part of the normal JaiScript CMake build (`JAISCRIPT_BUILD_EXAMPLES=ON` by
default). `build.bat` builds both configurations and prints both paths:

```bash
# from Source/JaiScript (VsDevCmd + Ninja, same as the test builds)
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_crawler   # PLAY THIS ONE
cmake --build out/build/x64-Debug --target jai_crawler                  # --dev iteration
```

**Play the Release exe** — the view casts 64 rays plus sprite math per keypress,
which Release does in a few ms and Debug does in a few hundred. The exe prefers
the *source* `scripts/` dir (so `--dev` hot reload edits the files you have open);
a `crawler_scripts/` copy sits next to the binary for distribution.
`--time` prints a key-to-frame latency report on exit.

## Play

```bash
jai_crawler.exe                     # new run, seed 1337, VM backend
jai_crawler.exe --seed 7 --backend interpreter
jai_crawler.exe --load              # continue a saved run
jai_crawler.exe --dev               # 'R' re-imports scripts mid-game (hot reload)
jai_crawler.exe --god               # tourist mode (visit Vexadrach, stay alive)
```

Needs a Windows 10+ console at least 80x28 (256-color VT processing).

| key | action |
|---|---|
| `w` / `up` | step forward (into a monster = attack) |
| `s` / `down` | step back |
| `a` `d` / `left` `right` | turn 90 degrees |
| `q` / `e` | sidestep left / right |
| `.` | wait |
| `m` | minimap toggle (explored tiles + facing arrow) |
| `g` | pick up |
| `i` | inventory — letter to inspect, then `e` use/equip, `d` drop |
| `p` | quaff first potion |
| `E` | equip the best upgrade in the pack |
| `z` | class skill — fires down your facing |
| `>` | descend (on stairs) |
| `c` / `?` | character sheet / help |
| `S` / `Q` | save & quit / quit |
| `R` | hot-reload scripts (with `--dev`) |

Ten floors, permadeath, Vexadrach the Hollow-Flame at the bottom. Same Kin
species, classes, bestiary, affix loot, identify-by-use potions, and timed
effects as JAI'S DESCENT — if you can win that, the only new skill here is
remembering which way you're pointing.

## The view

One DDA ray per column across a ~84 degree FOV; perpendicular distance kills the
fisheye by construction. Wall slices get 8 torch shades by distance, a 0.68x
darkening on N/S vs E/W faces, and a 3-char texture pattern (checker + mortar
rows). Floors and ceilings are per-row gradient bands, brightest at your feet.
Monsters, loot, stairs, statues and murals are billboard sprites scaled by depth,
occluded per column against the wall z-buffer, feet planted on the floor line at
their distance. Torchlight swallows sprites past 14 tiles.

Every two floors the Well changes wardrobe (Moraff would approve of the palette):
Mossrot Warrens, Bloodbrick Vaults, Gilded Fungus Halls, Screaming Copper Vein,
The Ashen Throat, and — floor 10 — The Hollow Gallery, where the color has been
swallowed like everything else.

Captures below are plaintext (ANSI colors stripped). A corridor with a Grublin
blob at the end of it:

```
 GRUBWELL -- dungeons of the unwashed --  Floor 1/10  Turn 10  ~ Mossrot Warrens ~
%%%%%%%%%######%%%#                                             | Wanderer
#########%%%%%%###%%#                                           | Jackodile-kin
%%%%%%%%%######%%%##%%                                        %%| HP 28/28
=======================                                   ======| ############
%%%%%%%%%######%%%##%%#%#                             #%%%####%%|
#########%%%%%%###%%##%#%                            %%###%%%%##| Fo 14/14
%%%%%%%%%######%%%##%%#%#                            ##%%%####%%| ############
=========================                            ===========| Lv 1  XP 0/15
%%%%%%%%%######%%%##%%#%#%                    ####%####%%%####%%| Atk 6  Def 1
#########%%%%%%###%%##%#%###%#ggggg#%#%##%#%%#%%%%#%%%%###%%%%##| Eva 10  Crt 5
%%%%%%%%%######%%%##%%#%#%%%#%ggggg%#%#%%#%##%####%####%%%####%%| Gold 0
=========================     ggggg                =============|
%%%%%%%%%######%%%##%%#%#.....ggggg..................##%%%####%%|       N
#########%%%%%%###%%##%#%                            %%###%%%%##|     W + E
%%%%%%%%%######%%%##%%#%#............................##%%%####%%|       S
========================                                ========| Face WEST
%%%%%%%%%######%%%##%%#.....................................##%%| Wpn -
#########%%%%%%###%%#                                           | Arm -
%%%%%%%%%######%%%##............................................| Trk -
```

Two steps later it is very much no longer small (bump it to attack):

```
 GRUBWELL -- dungeons of the unwashed --  Floor 1/10  Turn 12  ~ Mossrot Warrens ~
                                                                | Wanderer
                                                                | Jackodile-kin
%%                                                              | HP 25/28
======                ggggggggggggggggggggg                     | ##########--
%%####%%%#            ggggggggggggggggggggg                     | -3 HP (Grublin)
##%%%%###%%##%        ggggggggggggggggggggg                     | Fo 14/14
%%####%%%##%%##%#%    ggggggggggggggggggggg                     | ############
======================ggggggggggggggggggggg             ========| Lv 1  XP 0/15
%%####%%%##%%##%#%#%%#ggggggggggggggggggggg#%#####%##%#%##%##%%#| Atk 6  Def 1
##%%%%###%%##%%#%#%##%ggggggggggggggggggggg%#%%%%%#%%#%#%%#%%##%| Eva 10  Crt 5
%%####%%%##%%##%#%#%%#ggggggggggggggggggggg#%#####%##%#%##%##%%#| Gold 0
======================ggggggggggggggggggggg        =============|
%%####%%%##%%##%#%#%..ggggggggggggggggggggg.................#%%#|       N
##%%%%###%%##%%#      ggggggggggggggggggggg                     |     W + E
%%####%%%##%..........ggggggggggggggggggggg.....................|       S
========              ggggggggggggggggggggg                     | Face WEST
%%##..................ggggggggggggggggggggg.....................| Wpn -
                      ggggggggggggggggggggg                     | Arm -
......................ggggggggggggggggggggg.....................| Trk -
                      ggggggggggggggggggggg                     |
 The Grublin claws you for 3 (HP 25/28).
```

And the `m` minimap — explored tiles only, facing arrow, loot in sight:

```
 GRUBWELL -- dungeons of the unwashed --  Floor 1/10  Turn 0  ~ Mossrot Warrens ~
                                                                | Wanderer
                 ##############.                                | Fo 14/14
                 ...............                                | ############
                 #............#                                 | Lv 1  XP 0/15
                 #............#                                 | Atk 6  Def 1
                 #......^...!.#                                 | Eva 10  Crt 5
                 #............#                                 | Gold 0
                 #..............                                |
                 ...............                                |       N
                 ...............                                |     W + E
                  #############                                 |       S
                                                                | Face NORTH
```

## Determinism smoke test

`--smoke` runs the same headless autopilot idea as the roguelike, translated into
a body that must turn before it can walk: BFS to the goal, rotate until the nose
lines up, step, bump-attack what blocks, skill down corridors, sip when scared.
The FNV-fold `STATE_HASH` (now also folding the facing) must match across
backends for a given seed:

```bash
jai_crawler.exe --smoke --seed 99 --turns 1500 --backend vm
jai_crawler.exe --smoke --seed 99 --turns 1500 --backend interpreter
# STATE_HASH must match between the two runs
```

Verified at the time of writing: seeds 99 / 7 / 1337 / 4242 hash-identical across
backends (up to 1500 turns; seed 99's pilot dies honestly on floor 4 at turn 341,
same corpse both backends), and `--god --smoke --turns 3000 --seed 5` reaches
floor 10 and kills Vexadrach on both (`victory: true`, same hash, turn 1016).
Scripted interactive input works like the roguelike: `--input "11^dwwwww" --quiet`
(`;` = enter, `^` = esc), and a `save -> load` round trip restores turn, facing,
position, inventory and effects exactly.

## Files

```
main.cpp            thin host: console, keys, Rng, file IO, flags (adapted from roguelike)
scripts/state.jai   persistent globals (G, RNG, PILOT_CO)            [boss handle moved into G]
scripts/util.jai    colors, hashing, direction tables                [copied verbatim]
scripts/data.jai    species/classes/bestiary/items/affixes/flavor    [copied verbatim]
scripts/entities.jai Entity/Monster/Player/Item + timed effects      [copied verbatim]
scripts/items.jai   loot gen, inventory, potions/scrolls             [copied verbatim]
scripts/dungeon.jai rooms+corridors gen, FOV, BFS                    [copied, 4-dir BFS delta]
scripts/combat.jai  attack resolution, XP, class skills              [copied, facing-target delta]
scripts/ai.jai      monster behaviors                                [copied, boss-handle delta]
scripts/view.jai    NEW: DDA raycaster, themes, sprites, compass math
scripts/ui.jai      NEW: frame chrome, compass panel, minimap, modals
scripts/game.jai    NEW: Game class (+boss coroutine method), facing movement, saves, pilot
scripts/main.jai    NEW: imports + entry + hot reload list
```

## Language notes (first project after typed fields + coroutine methods)

This is the first example written *after* typed class fields enforce (67d248ef)
and coroutine methods landed (3766da09). Both got used naturally:

- **Typed fields feel right.** `Game` declares `int facing`, `bool show_map`, and
  friends; the compiler-enforced types caught two pilot bugs at the write site
  during development instead of three screens later. Typed *local* declarations
  converting like assignment is quietly the raycaster's best friend — `int lineh =
  WALL_SCALE / pdist;` is the truncation you want, everywhere, with no `itrunc`
  helper needed.
- **Coroutine methods are the right shape for the boss.** `boss_brain` is a
  `coroutine void` method on `Game`: the handle pins the receiver, the phase
  state (enraged, mid-telegraph) lives in the coroutine frame, and the body reads
  `monsters[bi]` / `player` through implicit `this` — including passing
  `monsters[bi]` to `var&` helpers like `attack()` and `step_toward()`, which
  resolves through the new lvalue-reference argument path and mutates the real
  monster. The roguelike's free-function + global-chain version is strictly
  clumsier.
- **FIXED (2026-07): the handle-in-a-field gripe.** This README used to say
  "handles still refuse to live in fields/containers, so `BOSS_CO` keeps its
  named global slot". Handles are reference-semantic on copy now — copies share
  the one live coroutine — so the boss handle moved into a `Game` field where it
  always belonged (`G.boss_co`, see game.jai/ai.jai) and the `BOSS_CO` global is
  gone. Field stores, container elements, and by-value params all just work.
- **Found one live regression** (reported; worked around at one site in
  `ui.jai`): a **bare container-element read used directly as a condition is
  always truthy** on BOTH backends — `if (arr[i])` and `if (!arr[i])` see the
  reference wrapper, not the bool. `var v = arr[i]; if (v)`, typed/`var` function
  returns, comparisons (`==`, `<`) and bool-typed locals all deref correctly, so
  game logic built on `is_visible()` was never affected — but it silently
  defeated the fog of war in any direct `if (G.visible[i])` render path (the
  roguelike's map render at HEAD has the same shape). Minimal repro:
  `var a = [false]; if (a[0]) { /* taken! */ }`.
- Inherited workarounds from the roguelike copies are still honored (save
  padding for `from_json` engine-pointer holes, hoisted unary-minus on element
  reads); nothing new tripped in ~1,100 lines of fresh script, including heavy
  float math, 1,280-cell frame builds, and nested-array sprite records.

Known scope cuts: monsters do not peek around corners any better than the
roguelike's greedy-with-slide pathing; the turn sweep interpolates the camera
only (sprites do not motion-blur, which is probably for the best).
