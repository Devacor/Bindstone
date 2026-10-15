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
twice the text resolution (JaiDoom's half-block method) — procedurally textured
walls, floors and ceilings shaded by torch distance and face orientation, materials
that change wardrobe every couple of floors, and pixel-art billboard sprites with
per-column z-buffer occlusion. A distant Grublin is a few green pixels at the end
of the hall. It does not stay small. And, Moraff's World-style, you look four ways
at once: smaller LEFT, RIGHT and BEHIND views frame the forward one, so the
Grublin sneaking up your flank shows up in 3D before it shows up in the log.

## Shared logic

`combat.jai`, `items.jai`, `entities.jai`, `data.jai`, `ai.jai`, `dungeon.jai`,
`util.jai` and `state.jai` carry the roguelike's game rules unchanged (one key =
one turn; the AI plays the same grid and does not care where the camera points).
They are the canonical versions of those rules, and the crawler's smoke seeds are
their determinism instrument. Three small deltas from the roguelike, each marked
with a `CRAWLER DELTA` comment at the site:

- `dungeon.jai` — `bfs_step` searches FOUR_DIRS (a facing-locked body cannot step
  diagonally, so the autopilot must not be handed diagonal moves).
- `combat.jai` — class skills target **down the facing** (`facing_target_index`,
  walls block) instead of auto-picking the nearest visible monster.
- `ai.jai` — the boss case mints its coroutine from `G.boss_brain(...)`, a
  **coroutine method** on Game (see language notes below).

Crawler-only: `view.jai` (the raycaster), `ui.jai` (log, character, compass, minimap,
modals), `game.jai` (facing movement, crawler turn loop, saves, autopilot),
`main.jai`, and the adapted host `main.cpp`.

## Run it

From the JaiScript source root, build the runner once:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target jaiscript_runner
```

then:

```bash
build/bin/jaiscript examples/crawler/scripts/crawler.jai
```

(On Windows the runner is `build\bin\jaiscript.exe`.) The native host is the `jai_crawler` target: `cmake --build build --config Release --target jai_crawler`, then
`build/bin/jai_crawler`.

## Build

Part of the normal JaiScript CMake build (`JAISCRIPT_BUILD_EXAMPLES=ON` by
default):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target jai_crawler   # PLAY THIS ONE
cmake --build build --config Debug --target jai_crawler     # --dev iteration
```

**Play the Release exe** — the view casts a ray per column and shades every pixel
of four panes per keypress and fills six text boxes (at the default size a
64x40-pixel forward view and three 32x20 ones: about 2 ms on the VM, 13 ms on the
interpreter in Release; the forward view alone is 1 ms and 5.5 ms; Debug is far
slower). The exe prefers
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
which wants a console of about 135x37; `v` switches to the forward view alone, which
fits 80x28.

### No build at all: the standalone-runner shim

`scripts/crawler.jai` replays the whole C++ host in pure JaiScript — drag it onto the
standalone runner (`build/bin/jaiscript`) and GRUBWELL
runs with zero example-specific C++. It captures the runner's non-blocking `read_key`
into a blocking wrapper, re-creates `draw`/`clear_screen`/`chr`/`host_log`,
the flag globals, and a **bit-exact script-side `Rng`** (xorshift64\* in 16/32-bit
limbs under checked int64 — same hashes as the C++ host: seed 99's pilot dies
on floor 4, turn 341, to the same Grublin Slinger). Flags ride after `--`, dashes
optional; the save is `crawler_save.json` beside `scripts/`, whatever the working directory:

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
whenever it fits with a forward view of at least 20 rows (a console of about 135x37),
and its panes grow to fill the console (the forward view 16:5 cells, up to 48 rows);
a smaller console gets the forward view alone, as large as fits, with the panel
beside it and five log lines under it. `v` switches layouts and re-fits. Smoke, dump
and scripted runs show the multi-view around the 64x20 default forward view unless
`view N`, `layout single` or `screen CxR` (size everything as if the console were
C x R) says otherwise. `dump PATH` renders the full frame after every smoke turn
without touching the game (the STATE_HASH is unchanged), writes them all to
`PATH.ans`, prints `FRAME_HASH` (sha256 of that file) and the frame-build ms/frame,
and leaves the last view canvas as `PATH.bmp` plus a sketch `PATH.txt` (pane pixels
as luminance, frames, labels and boxes as their text); `shots N` adds
`PATH_tT.bmp/.txt` every N turns, and `bench N` re-renders the final view N times and
prints ms/frame.

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

Every corner around the panes is a text box in the same frames. The LOG fills the
column above LEFT with as many messages as fit, wrapped, newest at the bottom: what
happened since your last move keeps its colors, older lines are dimmed. Beside BEHIND,
the MAP shows the explored map around you (north up) and the COMPASS your heading and
floor. CHARACTER fills the column above RIGHT: vitals, stats, skill, effects and gear,
and in a tall box your kin's trait and class skill spelled out. Under LEFT, IN SIGHT
lists the monsters you can see, nearest first, each with the pane it is in and how
far (`LEFT 3`); under RIGHT, LOOT does the same for loot (a mimic lists as a chest
until it bites). The forward view alone keeps the classic panel beside it and the
last five log lines under it.

JaiDoom's terminal video method: the caster draws into a palette-indexed pixel canvas
holding every pane (`VIEW_W x 2*VIEW_H` pixels for the forward view), and each text
cell is an upper-half block `▀` whose truecolor foreground is the top pixel and
background the bottom one. A row emits an escape only when its color changes
(run-length), from `VPAL_FG`/`VPAL_BG` escape tables built once per color; frames and
labels are prebuilt text runs and the box rows are written every frame, both spliced
between the pixel runs. Wide canvases (160+ columns) build their rows through
`parallel_transform`.

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
luminance, two pixels averaged; frames, labels and boxes as their text), here the
default 64x20 forward view on floor 2, facing east down a mossy hall with a Snarlbat
swooping in from the north: it is in the LEFT pane and on the MAP, IN SIGHT reports
it at `LEFT 1`, and it is not in the forward view yet (torch hand bottom left of
AHEAD):

```
┌─ LOG ──────────────────────────┐┌─ MAP ────────┐┌─ BEHIND · W ───────────────────┐┌─ COMPASS ────┐┌─ CHARACTER ────────────────────┐
│ You pocket 1 gold.             ││    .......#  ││:::::::::..:::.::.--:..:::::::-:││              ││ Testpilot                      │
│ You see a scroll labeled "GNIK ││#   #......##.││....:..:.......::......::.......││      N       ││ Drask-kin Emberwright          │
│  SISI" at your feet. (g to     ││    #...>...  ││...................-------------││      |       ││ HP 26/26    ################## │
│  grab)                         ││ #  #......#  ││.... . ..-:. ..  .:*==*=*-*=-*=*││   W -+- E    ││ Fo 20/20    ################## │
│ You pick up a scroll labeled   ││ ####..b...#  ││::::::::----.:.  .:--:==+=-====+││      |       ││                                │
│  "GNIK SISI".                  ││.......>......││:::::...:-- ::.  ...::::::.:::::││      S       ││ Lv 1  XP 7/15                  │
│ Stairs spiral down into the    ││ ##.&....#    ││.:......----::.....:::--+:::---+││              ││ Atk 5  Def 0  Eva 8  Crt 5     │
│  dark. Press > to descend.     ││ ##......#    ││::::::.::::..::::-..::-:=:----:=││     EAST     ││ Gold 18  Kills 1               │
│ You descend to floor 2:        ││# #......#    ││==-=-=----=-*=-+-*:-==-=:=:=--==││  floor 2/10  ││ Skill Emberbolt (4 focus, z)   │
│  Mossrot Warrens.              ││  #......#    ││-=---=---:+=-#-%-#*#*--=-=-=:=-=││              ││                                │
│ You see Fang Blade at your     │└──────────────┘└────────────────────────────────┘└──────────────┘│ Wpn Fang Blade (+2 atk)        │
│  feet. (g to grab)             │┌─ AHEAD · E ────────────────────────────────────────────────────┐│ Arm -                          │
│ You pick up Fang Blade (+2     ││::.:.:::..--:---::..----:::::..::..:-----:::.:-:-:::::..:::.:.--││ Trk -                          │
│  atk).                         ││::-:-.:.::::.--:---:..::---::.:.::.:---:--.:++++**+++**+**++++++││                                │
│ You equip Fang Blade (+2 atk). ││.:.:::::::..:..:::.::..::.:.:.:.::.::.:.::-=@##@@@@@@@@@@@@@@@@@││                                │
└────────────────────────────────┘│.:::.:.:.:.:..::.:.:::..::.::::::..:::::.+++@**@@@@*@@@@@@@@@@@@│└────────────────────────────────┘
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
┌─ IN SIGHT ─────────────────────┐│----:-+%==%+-:-:--=---:=-------:.---=--=::::-===%#=****%#+*+%:==│┌─ LOOT ─────────────────────────┐
│ b Snarlbat              LEFT 1 ││:--=--%%%%%%--:-:::--:-=--=====-.-:--::-=-::==+###-+++++++++%:-=││ no loot in sight               │
│                                ││---=-:+%%%%%---+-=-=:=---=-=-=--.=-----=-=-:###==:------=-------││                                │
│                                ││-----:::::::+======:*+----------:*========+=:::::--------:-+====││                                │
└────────────────────────────────┘└────────────────────────────────────────────────────────────────┘└────────────────────────────────┘
```

The `m` minimap stays text — explored tiles only, facing arrow, loot in sight —
and takes the forward view's place, centered in it, while the other panes and the
boxes stay live (with the forward view alone at 64x20 it fills the view exactly):

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
(The pilot never opens the map, so map-mode absolute movement is not on
this path.) The FNV-fold `STATE_HASH`, which folds the facing too, must match
across backends for a given seed:

```bash
jai_crawler.exe --smoke --seed 99 --turns 1500 --backend vm
jai_crawler.exe --smoke --seed 99 --turns 1500 --backend interpreter
# STATE_HASH must match between the two runs
```

Verified: seeds 99 / 7 / 1337 / 4242 hash-identical across
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
scripts/state.jai   persistent globals (G, RNG, PILOT_CO)            [shared]
scripts/util.jai    colors, hashing, direction tables                [shared]
scripts/data.jai    species/classes/bestiary/items/affixes/flavor    [shared]
scripts/entities.jai Entity/Monster/Player/Item + timed effects      [shared]
scripts/items.jai   loot gen, inventory, potions/scrolls             [shared]
scripts/dungeon.jai rooms+corridors gen, FOV, BFS                    [shared, 4-dir BFS delta]
scripts/combat.jai  attack resolution, XP, class skills              [shared, facing-target delta]
scripts/ai.jai      monster behaviors                                [shared, boss-method delta]
scripts/view.jai    pixel raycaster, textures, themes, sprite art, panes + boxes + frames, half-block blit
scripts/ui.jai      box contents, panel, minimap, modals, console-fit layout
scripts/game.jai    Game class (+boss coroutine method), facing movement, saves, pilot
scripts/main.jai    imports + entry + hot reload list
scripts/crawler.jai the standalone-runner shim (the host contract in script)
```

## Language notes

- **Typed fields.** `Game` declares `int facing`, `bool show_map` and friends, and
  a store of the wrong type fails at the write site. Typed local declarations
  convert like assignment, so `int lineh = kpx / pdist;` in the raycaster is the
  truncation it wants, with no `itrunc` helper.
- **Coroutine methods.** `boss_brain` is a `coroutine void` method on `Game`: the
  handle pins the receiver, the phase state (enraged, mid-telegraph) lives in the
  coroutine frame, and the body reads `monsters[bi]` and `player` through implicit
  `this`, passing `monsters[bi]` to `var&` helpers such as `attack()` and
  `step_toward()`, which mutate the real monster.
- **Coroutine handles in fields.** A copied handle shares the one live coroutine,
  so the boss handle lives in a `Game` field (`G.boss_co`, see game.jai and
  ai.jai); field stores, container elements and by-value parameters all hold it.

Known scope cuts: monsters do not peek around corners any better than the
roguelike's greedy-with-slide pathing; the turn sweep interpolates the camera
only (sprites do not motion-blur, which is probably for the best).
