# jai_crawler — GRUBWELL: dungeons of the unwashed

A first-person, step-based dungeon crawler in the spirit of the 1991 shareware
big-hairies (garish VGA corridors, a compass, monsters that get *bigger* as they
walk at you) — written **entirely in JaiScript**. The C++ host (`main.cpp`) is a
deliberately thin shell: a VT console, a blocking `read_key()`, a
seeded `Rng`, file IO for saves, flags as globals. Everything else — the DDA
raycaster, dungeon gen, combat, AI, items, saves, UI — is script.

Same bones as JAI'S DESCENT (the retired ASCII roguelike this grew out of),
different eyes: movement is **locked to the four
cardinal facings**. `w` steps a tile, `a`/`d` snap 90 degrees (with a fast 3-frame
heading sweep so the turn reads as motion) — but on the `m` overhead map the
movement keys go **absolute**: `wasd`/`hjkl`/arrows step by compass direction,
roguelike-style, with your facing following the step (so dropping back to first
person points where you were walking; turn-then-walk is FPS-view-only). The view
is a real one-ray-per-column raycaster drawn into a 24-bit pixel framebuffer at
twice the text resolution (JaiDOOM's half-block method) — procedurally textured
walls, floors and ceilings shaded by torch distance and face orientation, materials
that change wardrobe every couple of floors, and pixel-art billboard sprites with
per-column z-buffer occlusion. A distant Grublin is a few green pixels at the end
of the hall. It does not stay small. And, Moraff's World-style, you look four ways
at once: smaller LEFT, RIGHT and BEHIND views frame the forward one, so the
Grublin sneaking up your flank shows up in 3D before it shows up in the log.

## Shared logic: copied from the roguelike (now the canonical copy)

`combat.jai`, `items.jai`, `entities.jai`, `data.jai`, `ai.jai`, `dungeon.jai`,
`util.jai`, `state.jai` were **snapshots of the roguelike's committed versions** —
the game rules are deliberately identical (one key = one turn; the AI plays the
same grid and does not care where the camera points). The roguelike is retired
(2026-07) and these copies are now the canonical versions, along with its
smoke-seed determinism-instrument role. Three small deltas from the originals,
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

**Play the Release exe** — the view casts a ray per column and shades every pixel
of four panes per keypress (at the default size a 64x40-pixel forward view and three
32x20 ones: about 1.6 ms on the VM, 10 ms on the interpreter in Release; the forward
view alone is 1 ms and 5.5 ms; Debug is far slower). The exe prefers
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

Needs a Windows 10+ console with truecolor VT processing and UTF-8 output (Windows
Terminal is ideal); the view is drawn with `▀` half blocks and framed with box
lines. `jai_crawler.exe` keeps a 64x20-cell forward view and opens in the multi-view,
which wants a console of about 150x42; `v` switches to the forward view alone, which
fits 80x28.

### No build at all: the standalone-runner shim

`scripts/crawler.jai` replays the whole C++ host in pure JaiScript — drag it onto the
standalone runner (`out/build/x64-Release BENCHMARKS/bin/jaiscript.exe`) and GRUBWELL
runs with zero example-specific C++. It captures the runner's non-blocking `read_key`
into a blocking wrapper, re-creates `draw`/`clear_screen`/`chr`/`host_log`/`sleep_ms`,
the flag globals, and a **bit-exact script-side `Rng`** (xorshift64\* in 16/32-bit
limbs under checked int64 — same hashes as the C++ host: seed 99's pilot still dies
on floor 4, turn 341, to the same Grublin Slinger). Flags ride after `--`, dashes
optional; the save default is `crawler_save.json` in the cwd:

```bash
jaiscript.exe crawler.jai                                  # interactive, seed 1337
jaiscript.exe crawler.jai -- seed 7 god
jaiscript.exe crawler.jai -- smoke turns 250 seed 99       # headless; prints STATE_HASH
jaiscript.exe crawler.jai -- input "11^dwwwww" quiet       # scripted keys (';'=enter '^'=esc)
jaiscript.exe crawler.jai -- view 36                       # a 115x36-cell forward view (default: fit the console)
jaiscript.exe crawler.jai -- layout single                 # the forward view alone (default: multi-view if it fits)
jaiscript.exe crawler.jai -- smoke turns 250 seed 99 dump out/s99   # + every frame, FRAME_HASH
```

Interactively the shim sizes the view to the console. The multi-view is picked
whenever it fits with a forward view of at least 20 rows (a console of about 150x42),
and its forward view grows to fill the console (16:5 cells, up to 48 rows, the panel
beside it and five log lines under it); a smaller console gets the forward view alone,
as large as fits. `v` switches layouts and re-fits. Smoke, dump and scripted runs show
the multi-view around the 64x20 default forward view unless `view N`, `layout single`
or `screen CxR` (size everything as if the console were C x R) says otherwise.
`dump PATH` renders the full frame after every smoke turn without touching the game
(the STATE_HASH is unchanged), writes them all to `PATH.ans`, prints `FRAME_HASH`
(sha256 of that file) and the frame-build ms/frame, and leaves the last view canvas
as `PATH.bmp` plus a sketch `PATH.txt` (pane pixels as luminance, frames and labels
as their text); `shots N` adds `PATH_tT.bmp/.txt` every N turns, and `bench N`
re-renders the final view N times and prints ms/frame.

Runner-keyboard deltas (its console pump reports lowercase letter edges and no
punctuation on Windows): uppercase commands = shift+letter, `>` descend = **TAB**,
`.` wait = **SPACE**, and the `?` help modal is scripted-input-only.

| key | action |
|---|---|
| `w` / `up` | step forward (into a monster = attack) |
| `s` / `down` | step back |
| `a` `d` / `left` `right` | turn 90 degrees |
| `q` / `e` | sidestep left / right |
| `.` | wait |
| `m` | minimap toggle (explored tiles + facing arrow) |
| `v` | views: the forward view alone, or with LEFT, RIGHT and BEHIND around it |
| *(on the map)* `wasd` / `hjkl` / arrows | step by **compass direction** — no turning; facing follows the step |
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
effects as JAI'S DESCENT had — its whole feature set lives on here; the only
skill it never asked of you is remembering which way you're pointing.

## The view

The view is a cockpit of four panes. The forward view (AHEAD) sits in the middle,
LEFT and RIGHT at half scale beside it with their horizon on its horizon, and BEHIND
above it like a rear-view mirror. Each pane is the same caster turned a further
quarter turn (facing +3, +1 and +2 on the four-way grid), so walls, sconces, fog,
torchlight, loot and monsters show up in all of them exactly as in the main view;
only the forward view carries your hands and the damage rim. With the ~84 degree FOV
the four panes cover the whole circle but four 6 degree slivers, and LEFT, AHEAD and
RIGHT read as one panorama. Every pane is boxed in the floor's wall tone and labeled
in its glow color with the pane and the compass heading it shows (`LEFT · W` while
you face north). `v` drops to the forward view alone, which the console sizing also
picks when the multi-view will not fit.

JaiDOOM's terminal video method: the caster draws into a palette-indexed pixel canvas
holding every pane (`VIEW_W x 2*VIEW_H` pixels for the forward view), and each text
cell is an upper-half block `▀` whose truecolor foreground is the top pixel and
background the bottom one. A row emits an escape only when its color changes
(run-length), from `VPAL_FG`/`VPAL_BG` escape tables built once per color; frames,
labels and the gaps between panes are prebuilt text runs spliced between the pixel
runs. Wide canvases (160+ columns) build their rows through `parallel_transform`.

One DDA ray per column across a ~84 degree FOV; perpendicular distance kills the
fisheye by construction. Every color is a 16-step **ramp** from the floor's fog color
(dark) to torch-lit (warm), so distance shading is one table lookup, with a 4x4 Bayer
dither between steps for the VGA grain; N/S faces sit at 0.72x the light of E/W
faces, the torchlight flickers a little turn to turn, and glowing things (flames,
embers, fungus, eyes, runes) use flat ramps that ignore the dark.

- **Walls** are 32x32 procedural textures, one per wardrobe: mossy brick with moss
  dripping from the top, blood-streaked brick, violet blocks sprouting glowing gold
  mushrooms, teal riveted panels weeping orange, ash rock split by ember cracks,
  veined gray marble with moldings. Wall tiles pick variants from a coordinate hash
  (never the game RNG): iron torch sconces with a two-frame flame and a halo of
  light, and skull niches.
- **Floor and ceiling** are cast per pixel from the same rays: flagstones with
  bevels, plus hashed puddles, bone scatter, moss, blood and a cyan rune circle
  under the stairs; the ceiling is stone slabs crossed by timber beams with roots.
  Both fade into the fog with distance. Monsters that die here leave blood on the
  flagstones for the rest of the floor.
- **Sprites** are pixel art written as strings in `view.jai` (a role letter per
  texel: body, shade, highlight, outline, eyes, teeth, gold, steel, flame...), tinted
  by each monster's or item's identity color: every bestiary member, a mimic chest
  that grows teeth, loot by kind, the stairs arch, the statue and the mural. They
  scale by depth, stand on the floor at their distance (bats, moths and wisps hover
  and bob), sort far to near, and clip per column against the wall z-buffer.
  Torchlight swallows them past 14 tiles. A monster flashes white on the turn it
  takes a hit, and a wounded one carries a health bar.
- **You**: a torch in your off hand (flame flickers turn to turn), your blade in the
  other once you wield one, and a ragged red rim around the view on a turn you took
  damage.

Every two floors the Well changes wardrobe (Moraff would approve of the palette):
Mossrot Warrens, Bloodbrick Vaults, Gilded Fungus Halls, Screaming Copper Vein,
The Ashen Throat, and — floor 10 — The Hollow Gallery, where the color has been
swallowed like everything else.

The sketch `dump` writes beside the BMP (one character per cell: pane pixels as
luminance, two pixels averaged; frames and labels as their text), here the default
64x20 forward view on floor 2, facing east down a mossy hall with a Snarlbat
swooping in from the north: it is in the LEFT pane, not yet in the forward view
(torch hand bottom left of AHEAD):

```
                                                  ┌─ BEHIND · W ───────────────────┐
                                                  │:::::::::..:::.::.--:..:::::::-:│
                                                  │....:..:.......::......::.......│
                                                  │...................-------------│
                                                  │.... . ..-:. ..  .:*==*=*-*=-*=*│
                                                  │::::::::----.:.  .:--:==+=-====+│
                                                  │:::::...:-- ::.  ...::::::.:::::│
                                                  │.:......----::.....:::--+:::---+│
                                                  │::::::.::::..::::-..::-:=:----:=│
                                                  │==-=-=----=-*=-+-*:-==-=:=:=--==│
                                                  │-=---=---:+=-#-%-#*#*--=-=-=:=-=│
                                                  └────────────────────────────────┘
                                  ┌─ AHEAD · E ────────────────────────────────────────────────────┐
                                  │::.:.:::..--:---::..----:::::..::..:-----:::.:-:-:::::..:::.:.--│
                                  │::-:-.:.::::.--:---:..::---::.:.::.:---:--.:++++**+++**+**++++++│
                                  │.:.:::::::..:..:::.::..::.:.:.:.::.::.:.::-=@##@@@@@@@@@@@@@@@@@│
                                  │.:::.:.:.:.:..::.:.:::..::.::::::..:::::.+++@**@@@@*@@@@@@@@@@@@│
┌─ LEFT · N ─────────────────────┐│:..::........::....:.... ...:.....::...:==++@**###+***@@@###@@@@│┌─ RIGHT · S ────────────────────┐
│........:=:.:.: = ..:::.........││::...........:....................... :-==+-@---------#@@---@#@@││...::................:.......:..│
│..::.:::.===:.=%#%=::==:::.::-:.││   . . ::::::::::::::::::::. . ...  .:--=.+=@####:*#*##@@####:+@││**:.:.:::::::..:::.:::..::::.:::│
│. .......=:===#@#@#===: ........││::::...===-=====:===-=====::.   .  .:.-:-:+-****+@@*+**@@*++%:-@││%*##=:.. .......... ...:...... .│
│++=.::. . . :==#*#==: ........::││   :...=:.:-=::=:-:.:-=::-:..      . :.:--+=***+*:=****##***%:=@││*---*-+=.:.......... .....  ....│
│.  :-+..........=...........:-..││   ....:::::-:.=::::::-:.=:..      . ...::+:-----:----------+:-@││+*++==-=:::.................+.::│
│ - :.:........ ......::.....:. :││ . .....:.::..:-:.:::..::-:.       . ::.::=-+++++++++++:+*+++++#││+==-+:=-:::... ................:│
│ - -.:..............-+@.........││:.:.....::.:.:::-.::.:.:::-..      .:.::---+=+*********:*%***+**││===-+=--::................. ....│
│.:-:--:::.:-:::::::+:**:*:-::::-││.. ....=@=::-:......::-:.............:..-:-+-****+******+***+*+*││--*+*:-::::::::::.:.::::-:-:::::│
│:-:--*+::*:-:-::::-::=:***:--::-││......#@@@#:::.......:::.:.::...::.....::.-+=++=++++**+:=++=%+++││-=+:::::-:---:-:::::---:-:::::--│
│---=----+-:----=+:--++:--+-=--+-││-::.::.+#+:-.-:-:.:.::-:::::::::-:::::..--:+=====:=*%+++++++%:==││**:--=:--:----:---.---:----:---:│
└────────────────────────────────┘│:.::--::==:-::::::---::-.-:::-::=:::-:::-:::==-***+*+**%%***%:-=│└────────────────────────────────┘
                                  │----:-+%==%+-:-:--=---:=-------:.---=--=::::-===%#=****%#+*+%:==│
                                  │:--=--%%%%%%--:-:::--:-=--=====-.-:--::-=-::==+###-+++++++++%:-=│
                                  │---=-:+%%%%%---+-=-=:=---=-=-=--.=-----=-=-:###==:------=-------│
                                  │-----:::::::+======:*+----------:*========+=:::::--------:-+====│
                                  └────────────────────────────────────────────────────────────────┘
```

The `m` minimap stays text — explored tiles only, facing arrow, loot in sight —
and takes the panes' place, centered in it (with the forward view alone at 64x20
it fills the view exactly):

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
(The pilot never opens the map, so map-mode absolute movement (2026-07) is not on
this path — all hashes below are unchanged from before that control change.)
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
Scripted interactive input is also available: `--input "11^dwwwww" --quiet`
(`;` = enter, `^` = esc), and a `save -> load` round trip restores turn, facing,
position, inventory and effects exactly.

The view has its own gate on the runner shim: `dump` renders every smoke turn's
frame and prints a `FRAME_HASH` that must also match across backends (the renderer
reads the game but never the RNG, so the STATE_HASH is the same with or without it):

```bash
jaiscript.exe --backend=vm crawler.jai -- smoke turns 250 seed 99 dump f99
jaiscript.exe --backend=interp crawler.jai -- smoke turns 250 seed 99 dump f99
# STATE_HASH: 252356464 and the same FRAME_HASH on both
```

## Files

```
main.cpp            thin host: console, keys, Rng, file IO, flags
scripts/state.jai   persistent globals (G, RNG, PILOT_CO)            [boss handle moved into G]
scripts/util.jai    colors, hashing, direction tables                [copied verbatim]
scripts/data.jai    species/classes/bestiary/items/affixes/flavor    [copied verbatim]
scripts/entities.jai Entity/Monster/Player/Item + timed effects      [copied verbatim]
scripts/items.jai   loot gen, inventory, potions/scrolls             [copied verbatim]
scripts/dungeon.jai rooms+corridors gen, FOV, BFS                    [copied, 4-dir BFS delta]
scripts/combat.jai  attack resolution, XP, class skills              [copied, facing-target delta]
scripts/ai.jai      monster behaviors                                [copied, boss-handle delta]
scripts/view.jai    NEW: pixel raycaster, textures, themes, sprite art, panes + frames, half-block blit
scripts/ui.jai      NEW: frame chrome, compass panel, minimap, modals, console-fit layout
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
  retired roguelike's map render had the same shape). Minimal repro:
  `var a = [false]; if (a[0]) { /* taken! */ }`.
- Inherited workarounds from the roguelike copies are still honored (save
  padding for `from_json` engine-pointer holes, hoisted unary-minus on element
  reads); nothing new tripped in ~1,100 lines of fresh script, including heavy
  float math, 1,280-cell frame builds, and nested-array sprite records.

Known scope cuts: monsters do not peek around corners any better than the
roguelike's greedy-with-slide pathing; the turn sweep interpolates the camera
only (sprites do not motion-blur, which is probably for the best).
