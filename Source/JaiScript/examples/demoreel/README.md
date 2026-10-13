# jai_demoreel — a demoscene tech reel written in JaiScript

Sixteen auto-advancing truecolor scenes, five kinds of transition, pixel-font
title cards, and a finale where the word JAISCRIPT ignites out of a wall of
rainbow fire — every effect coded in `scenes/*.jai`, every frame drawn at
**double vertical resolution** with half-block pixels. A ray tracer, a sphere
tracer, a Menger-sponge raymarcher, a textured Wolfenstein raycaster, a
VoxelSpace terrain flight, Verlet cloth, metaballs, a falling-sand toy, a
murmuration, a texture tunnel and the classics. The C++ host (`demoreel_host.cpp`, shared with the ports; see PORTING.md) is a
thin shell: a VT console, a monotonic clock, non-blocking keys, a seeded rng
class, and the frame pump. Everything you see is script.

The reel is also an engine showcase: it keeps **two live engines** warm — one
tree-walking interpreter, one bytecode VM — running the same scripts, and lets you
flip between them mid-run. It doubles as a parity test: in `--smoke` mode both
backends must produce **byte-identical** frame streams. Five scenes trace their
rows across every core with `parallel_transform`, and the streams stay
byte-identical anyway.

## How a frame is drawn

Each scene paints a `w x 2h` grid of palette indices (square pixels, two per
terminal row) and the presenter in `util.jai` packs them into cells through the
upper-half-block glyph `▀`: the **foreground** truecolor escape paints the top
pixel, the **background** escape the bottom one (JaiDoom's `vid_row_half`
technique). Escapes are emitted only when a color changes along the row, and a
cell whose two pixels agree collapses to a space (a background run) or a full
block (a foreground run), so smooth regions cost one byte per cell. Palettes
precompute both escape strings per entry; a transition simply concatenates the
two scenes' tables (the incoming scene's indices shift by an offset) and the
reel's own UI entries ride at the end. `hb_row` is written as a
`parallel_transform` body over captured globals, so the same function serves
the serial loop and, with `HB_WORKERS` set (the standalone runner does on wide
terminals), a parallel presentation.

Text stays crisp on top: the HUD, the scene handle under each title card and
the finale's credits are ordinary glyph rows; the big title letters themselves
are pixels from a 5x7 font (`FONT5`), typed in with a shine sweeping across.

## Run it

From the JaiScript source root, build the runner once:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target jaiscript_runner
```

then:

```bash
build/bin/jaiscript examples/demoreel/demoreel.jai
```

(On Windows the runner is `buildin\jaiscript.exe`.) The native host is the `jai_demoreel` target: `cmake --build build --config Release --target jai_demoreel`, then
`build/bin/jai_demoreel`.

## Build

Part of the JaiScript CMake tree (target `jai_demoreel`):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target jai_demoreel
```

The exe prefers the source `scenes/` dir (so hot reload edits the real files),
falling back to `demoreel_scenes/` copied next to the binary.

### No build: the standalone runner

`demoreel.jai` is a pure-script shim that recreates the C++ host contract (the
`ESC` global, `itrunc`/`ifloor`, and a bit-exact script port of the seeded
xorshift64* `Rng` — checked-int64-safe via 16-bit-limb multiplies) and pumps the
reel on the standalone runner — drag-drop it onto `jaiscript.exe` or:

```
jaiscript demoreel.jai                       # the reel, 30 fps, with its soundtrack (q/esc quits; h/f/n/p/space/digits work)
jaiscript demoreel.jai -- frames 1700        # headless self-test: smoke frames, sizes + stream hash + timing
jaiscript demoreel.jai -- frames 60 dump P   # also write frames 0/N-2/N-1 to P_<i>.txt (+ P_<i>.bmp, the pixel grid)
jaiscript demoreel.jai -- bench 60           # every scene mid-flight: script ms/frame and KB/frame
jaiscript demoreel.jai -- shot 14 at 4 dump P   # one frame of scene 14 at scene time 4 -> P.txt + P.bmp
jaiscript demoreel.jai -- midi song.mid      # write the soundtrack and exit
jaiscript demoreel.jai -- music 0            # the reel, silent
```

The soundtrack is composed by the script at boot: a Standard MIDI File (64 bars
at 124 bpm in A minor over i-VI-III-VII — pumping synth bass, warm pad, a saw
arpeggiating sixteenths, a square lead improvising on the pentatonic from the
reel's seed, a four-on-the-floor kit) written to the app cache dir and looped
through the runner's `play_midi`.

Verified: the 1700-frame smoke reel produces the same stream hash and
byte-identical dumped frames under `--backend=vm` and `--backend=interp`.
Not ported from the C++ host: the VT escape filter / xterm-256 rewrite /
row-diff (frames present whole), TAB dual-engine swap, and `r` hot reload.

## Run

```
jai_demoreel                     # the reel, VM backend active, 30 fps pacing
jai_demoreel --backend interp    # start on the interpreter instead
jai_demoreel --smoke             # headless: both backends, frame-hash parity + perf
jai_demoreel --smoke --frames N  # smoke frame budget (default 1700)
jai_demoreel --capture I         # print scene I as plain text (ANSI stripped: half blocks and spaces)
jai_demoreel --reload-test       # headless hot-reload + backend-swap self-test
jai_demoreel --precompiled       # boot from demoreel.jaibite (saved on first run)
```

Keys while running:

| key | action |
| --- | ------ |
| `TAB` | swap live backend (interpreter <-> VM); scene state re-synced, stateful scenes replay up to 3 sim-seconds |
| `r` | hot-reload every `scenes/*.jai` from disk into BOTH engines |
| `h` | toggle the HUD (scene, ACTIVE BACKEND, frame ms, fps, pixel size) |
| `f` | freeze the clock (render keeps running) |
| `n` / `p` / space / arrows | next / previous scene |
| `1`..`9`, `0` | jump straight to a scene (`0` = scene 16, the finale) |
| `q` / `ESC` | quit |

The HUD shows the active backend and its frame cost, plus the last measured cost
of the other backend — press TAB and watch the ms number move.

## The scenes

| # | scene | what it is |
| - | ----- | ---------- |
| 1 | PLASMA | four summed sine fields over a 256-entry cyclic palette, integer table lookups per pixel; the radial term reads a double-size distance table through a sliding window, so the ripple's center orbits at zero per-pixel cost |
| 2 | STARFIELD | a warp run: 700 stars in four color temperatures drawn as motion streaks (tail dim, head bright, near stars bloom) over a periodic value-noise nebula; the palette is ADDITIVE (nebula level x star light), the view rolls, and the ship spools up, jumps to warp and drops out in a flash |
| 3 | TUNNEL | the table-driven texture tunnel: atan2 and 1/r baked per pixel of a double-size table, a frame is four integer lookups per pixel; the vanishing point wanders (sliding window), the walls twist with depth, bevelled riveted plates with racing neon rings dissolve into the glow at the tunnel's end |
| 4 | DONUT | the torus homage grown up: sphere-traced per pixel on the torus distance field in the donut's own frame, exact normals, diffuse + Blinn specular, pink icing with a wavy drip edge and sprinkles on a hashed (phi, theta) grid; rows in parallel |
| 5 | JULIA | morphing Julia set (c orbits the cardioid) at full resolution, continuous escape-time coloring (log-log fractional count, no bands) into a cycling 256-entry palette, orbit-trap shading inside the set; rows in parallel |
| 6 | LAVALAMP | eight metaballs: one loop sums the inverse-square field, its gradient and a contribution-weighted hue, so merging blobs blend colors; the wax is lit from a normal built out of the gradient (the height of a blob under the field), with a tinted glow outside; rows in parallel |
| 7 | PIPEDREAM | the 3D Pipes screensaver homage: four pipes grow one segment per tick through a 16x12x16 voxel volume, never intersecting; every tube is a chain of lit sphere impostors a quarter voxel apart (per-pixel normal, specular, fog, depth nudge so crossings intersect), glossy elbow balls at every turn, a static camera per generation, CRT wipe, reseed from a new angle; ~2% of elbows spawn the jackodile's rainbow eye |
| 8 | POWDER | falling-sand toy at full pixel resolution, two substeps a frame: sand / water / ember / plant / smoke, each element a script class; water levels out (up to four cells of sideways flow a step), surfaces catch the light, water darkens with depth, embers glow onto the air around them |
| 9 | BOIDS | a starling murmuration at sunset: 80 boids (script class instances) and a flapping hawk as silhouettes against a baked sky (gradient, sun glow, a lit skyline); every palette entry is a sky tone darkened by a level, so birds and their fading wakes shade whatever sky they cross |
| 10 | KINSTEIN 3D | Wolfenstein-style DDA raycaster over an authored 24x24 keep, TEXTURED: coursed stone, mossy stone, fluted gold and a banner of animated rainbow fire (16x16 textures grown in script), floor and ceiling cast per pixel, distance fog; billboard Grublins and Orglis brutes lit as spheres per pixel, with eyes and grins, wall-occluded per column; the camera auto-fires on line-of-sight cone lock — recoil, muzzle flash, rainbow tracer, hit-flash, gib scatter, a pixel-font kill counter |
| 11 | CANYONRUN | the Comanche (1992) VoxelSpace algorithm: a 128x128 wrapping heightmap by diamond-square, a river canyon carved along a sine meander, four levels of sun-side slope light plus hashed grain baked into a packed color/height map; 72 geometric depth steps of 16.16 fixed-point marching with a per-column y-buffer, ten haze banks, a sun that tracks the heading, banking into turns |
| 12 | SPONGEWORKS | a raymarched Menger sponge: one ray per pixel, KIFS-fold sphere tracing (no sqrt, no mod), the exact face normal from ONE tracked re-fold, SDF-probe AO, fog, six 16-level face ramps, a 4-phase Bayer temporal update with per-pixel reprojection; rows in parallel |
| 13 | BANNERFALL | Verlet cloth: a 26x11 grid (structural + shear, Jakobsen sqrt-free relaxation, topology as index arithmetic) flying a rainbow weave with the gold JAI sigil; each quad rasterized as two triangles by edge functions at pixel centers from subpixel positions, lit by edge cross products; the hoist seam tears thread by thread, the banner tumbles and bursts into ember scraps |
| 14 | CHROME | the Amiga Juggler's grandchild: a ray tracer, one ray per pixel across every core — three tinted chrome spheres bounce on a checkerboard running to a hazy horizon, one reflection bounce, hard shadows, a sun glint; the ray returns WHAT IT SAW as a class and each sphere looks that up in its own tinted copy of the class table |
| 15 | FIRE | convection fire with the Doom fire's random sideways jitter at full resolution, 73 heat levels, sparks breaking off and drifting up; ignites into RAINBOW fire mid-scene (house signature) |
| 16 | FINALE | the fire scene's subclass: a wall of fire out of which "JAISCRIPT" ignites in double-size pixel letters (a heat-source mask), burns in rolling rainbow flame, and remains white-hot as the fire dies while the credits come up |

Between scenes: dissolve (hashed per pixel), slant wipe and iris (both with a
glowing seam), blinds, and the Doom screen melt — two grids merged per pixel
into the combined palette. A title card with a demoscene handle opens on every
scene.

## Measured numbers

Standalone runner (Release `jaiscript`), 100x40 cells =
100x80 = 8,000 pixels per frame, seed 20260705, fixed dt 1/30, the whole
16-scene reel incl. transitions (`-- frames 1700`, smoke durations of 3 s per
scene), two runs each. "render" is `demo_frame` alone (update + render +
presentation); "incl. hashing" adds the shim's per-byte script hash of every
frame:

| backend | render ms/frame | incl. hashing | bytes/frame (mean) |
| ------- | --------------- | ------------- | ------------------ |
| **VM** | **3.5 - 3.8** | 8.9 - 9.1 | 52 KB |
| interpreter | 20.1 - 20.7 | 29.9 - 30.6 | 52 KB |

The earlier cell renderer (one pixel per cell, 4,000 per frame, thirteen scenes)
measured 1.8 / 11.8 ms render on the same runner: the reel now draws twice the
pixels with far heavier effects for about twice the script cost, and the VM
spends about a ninth of the 33 ms frame budget. The five `parallel_transform`
scenes are what keep it there — CHROME traced serially cost 30.6 ms/frame on
the VM, 4.0 ms across the cores.

Per scene, mid-flight (`-- bench 60`, no transitions, same size):

| scene | VM ms | interp ms | KB/frame |
| ----- | ----- | --------- | -------- |
| PLASMA | 0.8 | 11.3 | 139 |
| STARFIELD | 1.5 | 8.3 | 31 |
| TUNNEL | 0.7 | 8.9 | 63 |
| DONUT | 4.7 | 14.6 | 30 |
| JULIA | 5.0 | 16.9 | 123 |
| LAVALAMP | 2.3 | 18.1 | 51 |
| PIPEDREAM | 1.1 | 10.9 | 56 |
| POWDER | 4.9 | 25.3 | 33 |
| BOIDS | 4.0 | 14.2 | 23 |
| KINSTEIN 3D | 1.2 | 12.6 | 33 |
| CANYONRUN | 0.8 | 12.2 | 22 |
| SPONGEWORKS | 5.8 | 21.2 | 28 |
| BANNERFALL | 2.9 | 18.4 | 21 |
| CHROME | 4.0 | 17.5 | 27 |
| FIRE | 2.7 | 16.1 | 54 |
| FINALE | 3.1 | 24.5 | 96 |

Every scene holds 30 fps on the VM with room to spare (mean 2.8 ms, worst
5.8 ms); on the interpreter every scene stays under 26 ms (mean 15.7 ms).

- Parity: frame streams **byte-identical** across backends over 1,700 frames,
  every one of the sixteen scene entries plus the wrap back to PLASMA: shim
  stream hash `3352695962` on both. The C++ host's `--smoke` hash is FNV-1a 64
  over the same frames: `23505cc87911c46d` on both backends (the host's HUD
  is off in smoke, so its frames are the shim's bytes).
- `--reload-test` re-executes every scene file into both engines. The
  presenter's globals live in `util.jai`, so a reload resets them and the reel
  re-binds them on the next frame; a reload mid-transition cancels the
  transition (the live scene rebuilds), so no scene reads a reset buffer.

### Scoreboard

The [comparison runner](ports/COMPARISON.md) times natural/shaped JaiScript, Lua and Squirrel with warm-up, interleaving and padding controls.

`scenes/skull_natural.jai` is OSSUARY written the way a user would write it:
the same algorithms and the same frames as `skull.jai` (scene hash
`1aba3d7537895828`), without the spellings chosen to fit the VM's fast paths
(one `sk_world`, the warp's amplitude read left of its sine, ternary folds,
one `sk_crown` holding its flames, the row pushing `sk_trace`'s result). The
ratio of the two files' serial OSSUARY rows, natural over shaped, measures how
general the VM's fast path is: at 1.0 plain code runs as fast as code written
for the VM. The host loads `skull.jai` only, so run the natural file from a
copy of the scenes directory:

```
cp -r scenes /tmp/natural_scenes
cp scenes/skull_natural.jai /tmp/natural_scenes/skull.jai
jai_demoreel --perf 30 --backend vm --workers 1                               # shaped
jai_demoreel --perf 30 --backend vm --workers 1 --scenes /tmp/natural_scenes  # natural
```

## Console performance

Half-block frames carry two colors per cell, so a frame is bigger than the old
one-pixel-per-cell stream (52 KB on average, 139 KB for PLASMA, whose 256-entry
palette changes nearly every pixel; KINSTEIN and CANYONRUN stay near 20-30 KB).
Legacy conhost parses VT ~16x slower than Windows Terminal, which is what the
host's post-processing in `demoreel_host.cpp` is for (scenes and the `--smoke` stream
are untouched by it, so parity hashes don't move):

- **Escape trimming.** Scripts already emit a color escape only when a palette
  index changes; the host additionally drops any SGR group that wouldn't change
  terminal state, and (default on conhost) rewrites 24-bit colors to the xterm-256
  cube — shorter escapes and coarser quanta = longer runs. `--truecolor` keeps
  24-bit with per-channel merge tolerance (`--tol N`, default 8); `--no-filter`
  is the byte-exact legacy stream. The filter tracks foreground and background
  separately, which is exactly the half-block stream's shape.
- **Terminal detection.** `WT_SESSION` distinguishes Windows Terminal from legacy
  conhost. Conhost defaults to 256-color mode and shows a one-line tip on the
  title card ("Windows Terminal renders this reel much faster"); everything else
  defaults to truecolor + tolerance merging.
- **One buffered write per frame**, via `WriteConsoleA` on a real console.
- **`--diff`** row-diff redraw: only rows that changed since the last frame are
  rewritten (cursor-move + row). Not the default — most scenes change every
  pixel every frame.

The HUD's headline frame cost is `sim + draw` (script update + frame string,
plus the console write), split out so you can see which side you're bound on.
`--bench N --scene I` runs N real frames and reports the same split headlessly
(`--bench-out FILE` to save it).

## Live coding

1. Run `jai_demoreel`.
2. Open `scenes/plasma.jai` in an editor; change the palette stops (the
   `pal.gradient` call), or the field frequencies in `render`.
3. Press `r` in the console window.

Both engines re-execute every scene file (hot reload with instance migration) and
the current scene rebuilds — the change appears without restarting. `main.jai` is
deliberately never reloaded: it owns the persistent globals.

## Determinism / parity

`--smoke` runs N frames headless at a fixed dt on the interpreter, then the VM,
FNV-1a-hashing every returned frame; the hashes must match. All scene randomness
goes through the host's seeded xorshift `Rng` class (the presentation-only hashes
are integer arithmetic on pixel coordinates), and `parallel_transform` returns
rows in order, so a one-pixel divergence between backends fails the run. Env
helpers: `DEMOREEL_SMOKE_FULLDUR=1` (full interactive scene durations),
`DEMOREEL_DUMP_FRAME=N` (write frame N of each backend to a file for
diffing), `DEMOREEL_TRACE=1` (per-frame progress for crash localization).
Deliberately NOT part of the main test suite.

## --precompiled

`engine->jaibite()` parses the concatenated scene source once; `--precompiled`
saves the parsed AST as `demoreel.jaibite` on first boot and loads it afterwards
(the .jaibite saved by the interpreter engine loads into the VM engine —
`jaibite_load` re-interns symbols per engine, so one file serves both backends).

## Captures

Stripped of color a half-block frame is only `▀`, `█` and spaces, so these are
brightness maps of the pixel grid (one character per terminal cell, the two
pixels averaged; `-- shot I at T dump P` writes the full-color `P.bmp`).

CHROME (`shot 14 at 4` — three chrome spheres over the checkerboard; the `@@`
are sun glints, the reflections carry the sky and the floor):

```
##########********************************************************************************##########
########################++=====++####**************************#####################################
####################++:::::::::::::=+###############################################################
##################+-:::::::::::::::::-+#############################################################
#################*--:::::+++::::::::::--*###########################################################
################*=-----::@@@*::::::::---=*##########################################################
################+=======--------------====##########################################################
%%%%%%%%%%%%%%%%+++++++++++++++++++++++++++%%%%%%%+-:::::-+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%*********++**+++*++*+****++%%%%%*--:::+:::--*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%*+***--=-==*===-*-===-.-*=+%%%%#+=::-**---===#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%+===:===*==*======:=:-=*-%%%%%***..=++-+++***%%*++*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%-:*=*::*=::=***::*=.=:=#%%%%%#+-+*=+==:-+==-:......:-+%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%%+*:====**=::::***:=-+%%%%%%%%+=:::*=.:*:-:..........:-*%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%###%######%%%%#%%##==::=======.:.=%%%%#######*==:=*:=::.-::..@@..::::-*##%%%%%#%%%%######%##%%%%
%%###**%%%#########****##*++####=+=##%%####****#****##+%%=..-------------==***%%%%%##%%%%%******####
##*##%######*####+#########+####%%+######%%***++**%****++:-==-=--=-======-=+#######******#######*%%%
###+#++++###**********%%%%%%+********%*++++++*%******===#--:--=-:=::-=::=:=+++********++++*%%%######
+*%%%***********%%%%%+++++****%*****====*********==*===+++-==-:-===----=--*++****%%%%%%*+++****%****
++++++****+*****=====%%%%%%%%%**-=====++++==-----====%%****-=:---------++%**==****%%%****+++++++***%
%%%%%%%%%%==*********%========----=============*%%%%%%%%%**====:==***+**=========***%%%%%****=***%%%
****=====%%%%%%%%%%%%%=********%***+------======**%******==----=======**%%***==*****%%%****=========
========********=======%%%%%%%%%%%%%%*=******%%%%%%**======--------===---**=====**%%%%%%%%%%%%***===
*%%%%%%*===============%%%%******======%%%%%%%%%%%%%%%*=====*+++-====-+-=============**%%%%%%***====
%%%%%%%=======******%%%%================*%%%%%%%%%*****==*%%%%%%%%%%%%%%%%**=========****%***=======
%%******%%%%%%%%%%%%%%%%====------------**%**---------------*%%%%%%%%%%%%%%%%%==**%%%%%%%%%%%%%***==
=====*%%%%%%%%%%%%%%%%%%%-----*****%%%%%%%%%*------------------*%%%%%%%***--------**%%%%%%%%%%%%%%%%
```

KINSTEIN 3D (`shot 10 at 3.1` — the kill counter top left, oak planks overhead,
coursed stone walls converging on a Grublin down the corridor, flagstones
below, the pistol at the bottom):

```
--======:.....                                         :::::::::.        .::::.       .....:======--
--%:=**:*=%== % ....% ....===== .::::.=====.====+::::::.........::::.....................:======----
--%=*..===% :.% ..::% ::::+=++::::::::% ++% :.:++ :::::::::::::::::::::::::::::.   ....:=====-------
--* ==---=* ==* ....* ::::::..*.::::::*-..* :==..:::::::::::::::::::::::::::::::.....:=====---------
--= --=.=----:+---- -----.----         ---  -----                       ....  .......===------------
--------------=..+-..................   ...............................::::......-+..=--------------
---------------..++++-...............    ......................:::......::....-++++..---------------
..::-----------..=++++=-.............................   ....................-=++++=..-----------::..
......::-------..====+===-................................................-===+====..-------::......
..........::---..=========.:.....................................  .....:.=========..---::..........
*---.........::..=========.--:.. .................................  ..:--.=========..::.........---*
****...--........::===---=.----:.. ........  .................... ..:----.=---===::........--...****
****...====---.......::---.--:::=:..         ..                 ..:=:::--.---::.......---====...****
+++*...========+---......:.--:::--=.......................... ...=--:::--.:......---+========...*+++
++++...========++++++..-....:.::---.::: .                  . :::.---::.:....-..++++++========...++++
++++...========++++++..+++---...::-.::- . ................ . -::.-::...---+++..++++++========...++++
++++...==============..++++++.-:::....: .:.              .:. :....:::-.++++++..==============...++++
====...--------======..======.---==-::: ......         ..... :::-==---.======..======--------...====
====...--------======..======.:---=--:-:::... ..--=-:  ...:::-:--=---:.======..======--------...====
====...--------======..======.::-----:::::.:  ++#+*=#-: :::::::-----::.======..======--------...====
....................................... :....=+**#=*++:..:.: .......................................
........................::.--::=---.--: :::..:----%:::...::: :--.---=::--.::........................
.........------..=========.++===---.--: ...-=.......... ::.. :--.---===++.=========..------.........
-**************..=====----.====-:::.::..::.......:........::..::.:::-====.----=====..**************-
**************+..=====----.==---....::::....:...:...::.:::..::::....---==.----=====..+**************
*******++++++++..====-----.=::...:=--::.:..:....:.::::.:::::.::--=:...::=.-----====..++++++++*******
+++++++++++++++..--------:....:-----::.:.::.:::::.::::-..::--.::-----:....:--------..+++++++++++++++
++++++++++++++=..----::....-=.-:-:-..--.::............::.......---:-:-.=-....::----..=++++++++++++++
+++++++++======..::.....-====.:::.::::::::::::-=:.:----::-::---=:..:::.====-.....::..======+++++++++
++++=========--........======.:-.----==::-------:::::--==:::------::-:.======........--=========++++
==========--......-==..====--::::::-==..::::::..==**..:-==..:---------:--====..==-......--==========
======--.......-=====..==----:::::-==..:------..==--..--==-..:-------------==..=====-.......--======
==--........:========..---:------.....::------..==--..-----...-----------:---..========:........--==
.........:===========..--::::---------------....==--....-------------===-..:-..===========:.........
.......======--===---..:---------::::---::::..==**----..--------:::----===-....---===--======.......
...-...====----=----::--------------------....==------....----===========+++-..:----=----====...-...
-+++...=----------::----:::---------------..==**--------..================+++-...:----------=...+++-
++++...---------:-++++++++++++++++++++++##..==----------..##+++++++++++++++++++-...:---------...++++
++++...-------:.......................####..------------..####.......................:-------...++++
++==...---------------=++++-....----########..--------..########-....==================------...==++
```

DONUT (`shot 4 at 4` — the frosted torus, specular highlight on the icing):

```
....................................................................................................
....................................................................................................
...............................................=------*******+*****-=---............................
......................................-=--+****##%%%%%%##*#******+****%#**=--.......................
.................................--******++++##%%@@@%%#%*#**+**#***********+++=:....................
............................++#**+++==+=+=====**+*****++++++**+************+++++=:..................
.........................==***+++=+===---==-----============++:+*****+****++++++==-.................
.......................=+**++=+===---::::::::::::==-------=+=+********+*++++++++==--................
.....................***%**+**+===--::...............--=-*##***********++*+++++==+--:...............
...................=*********+*++++=------------##***********++******+++++**++====+-:...............
..................**************%%*%%***********%%**********+******+++++++++++===---:...............
.................*************%%********++**+++**@*********#**+++=+++++++++===+=---::...............
................-******######%#############################%###**+++++++==-=-=----::. ..............
................++***#####%%%%%%%%%%%%%%##%@@@@%%%%%%%%%#######**+++++======-----::.. ..............
................++***####%%%%@@@@@@@@@@@@@@@@@@@@@@%%%%%%##*##*++++=======---:::::.. ...............
................=+++**@%##%%%@@@@@@@@@@@@@@@@@@@@%%%%%#%#***+**=========--:-:::::.  ................
................-++++++*####%%%%%%@@@@@@@@@@@@@%%%###***#*+==========---:--::::..  .................
.................==*+*++++**#######%%%%%%%#%###*****++===+=======-------:::::..  ...................
..................-==**+++++++***++*******++++++=========++==-------::::::...   ....................
...................--============--====-================-==------::::::...    ......................
....................:----=========-====::========------------:::::::...      .......................
.......................::----::------------------------:--:::::.....      ..........................
...........................:::::::::------::::::::::::::::.....        .............................
................................::::::::::::::..:........          .................................
```

FINALE afterglow (`shot 16 at 12` — the fire has died, the word stays white-hot):

```
:::::::::::::::::::::-::::::::::::::::::::::::::::::::::::-:::::::::::::::::::::::::::::::::::::::::
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:::::::::::::::::::::::::::::::--::::::::::::::::::-::-::::::-::::::::::::::::::::::::::::::::::::::
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
.*++=*++=*+.:.++=*++.:.+=*++=*++=.:.=*++=*++.:.+=*++=*+.=*++=*++.:.+=*++=*++=.++=*++=*.. *++=*++=*+
::::::.+=...=*.::::.=*.:::.+=.:::.++.::=::::.*+.:::::::.=*.::::.=*.:::.+=.:::.++.::::.++ . ..*+
::::::.+= :.=*.::::.=*.:::.+=.:::.++.:::::-:.*+.:::::::.=*.::::.=*.:::.+=.:::.++.::::.++     *+ .
::::::.+= :.=*++=*++=*.:::.+=.:::::.=*++=*.:.*+.:::::::.=*++=*++.:::::.+-.:::.+==*++=*.    . *+ .
.*+.::.+= :.=*.    .=*.:::.+=.:::::::::::.++.*+.:::::::.=*  =*.:::::::.+=.:::.++ .   :     . *+
 *+ ::.+= . =*     .=*.:::.+=.:::::::::::.++ *+.:::::::.=*    ++.:::::.+=.:::.++.            *+
   +=*+     =*      =* +=*+-=*++= ++=*++=*     +=*++=*+ =*      =* +=*++=*++= ++             *+
                                                                                              .
                            -
                                       -                               -        .
```

## Language feedback (dogfooding notes — recorded honestly)

Everything below was hit while writing this demo.
Minimal repros were extracted for each; the workarounds ship in the scene
comments. The single biggest lesson first:

**JaiScript is value-semantic everywhere.** Assignment, method/function/lambda
parameters, returns, and even array-element reads deep-copy — for arrays AND
script-class objects. (`docs` say "copy = shallow", which describes the C++
`script_value` handle, not language-level assignment.) Mutation flows through
`var&` reference parameters / reference declarations, or through the owning
object's own fields. The first draft of this reel silently rendered
palette-entry-0 washes for every scene because `render(var grid)` mutated a
copy — and `--smoke` stayed green because both backends produced identical
blanks. Parity testing cannot catch "consistently wrong"; capture your frames.

**Bugs worked around (each with a small repro):**

> **Triage update:** every finding below was
> reproduced (or bisected) at HEAD and resolved; pinning regression tests live in
> `source/tests/language/demoreel_regression_tests.cpp`. The reel of that day
> exercised the fixed paths: the graveyard was gone (scenes are destroyed at
> every transition), `render_rows` declared `prev` in the loop body again, and
> plasma passed raw `c[0], c[1], c[2]` element args into typed params.

1. **FIXED** (frame exit clears only the frame's OWN method env).
   ~~Free-function / lambda calls inside class methods poison implicit-self~~
   (both backends). After `g = mk();` where `mk` is a free script function,
   every later field read/write in that method throws `Undefined variable`, and
   `this` stops being an object. Method calls, constructor calls, builtins, and
   host calls are safe.
   ```jaiscript
   function mk() -> var { return [1]; }
   class R { var g = null; R() { g = mk(); } }   // Undefined variable 'g'
   ```
   Workaround: shared helpers live on a `Gfx` class (global instance `gfx`);
   scene factories are an if-chain method instead of an array of lambdas.

2. **FIXED** (method frames reserved 0 slots, so a mid-loop DECL_VAR
   reallocated the locals vector and dangled the counted-for cached pointers).
   ~~VM: loop-body locals assigned from a nested scope corrupt the enclosing
   loop~~ (method context only). An inner `for` assigning an outer-loop body
   local makes the outer loop run once — or forever. A plain `if` (no `else`)
   assigning a loop-body local does the same; `if/else`, `while`, and range-for
   are fine. Method-level locals are immune.
   ```jaiscript
   class G { int t(int w, int h) {
       int rows = 0;
       for (int y = 0; y < h; ++y) {
           int prev = -1;
           for (int x = 0; x < w; ++x) { prev = x; }
           rows = rows + 1;
       }
       return rows;   // VM: 1, expected h
   } }
   ```
   Workaround: every local assigned from a deeper scope is hoisted to method
   level; single-branch clamps became same-level ternaries. Declaring the same
   local name in sibling if/else branches also aliased slots (nondeterministic
   across process runs) — hoisted too.

3. **FIXED** (interp ++/-- resolves
   slot-based locals). ~~Interpreter: `++x` on an enclosing-scope local inside a
   for body throws `Undefined variable 'x'`~~ (VM fine; `x = x + 1` fine on both).

4. **FIXED** (typed declarations, slot stores and typed fields convert
   like assignment — `int d = 4.7` truncates to 4, and a declared field type is
   enforced on every store, on both backends). `cast<int>(x)` is the explicit
   float→int conversion; the host's `itrunc()`/`ifloor()` remain for the scenes
   that use them.

5. **FIXED** (overload matching derefs lvalue reference arguments).
   ~~Raw element / nested-field arguments misresolve against typed parameters~~ —
   `p.add_bg(c[0], c[1], c[2])` resolves and converts; host candidates too.

6. **NO LONGER REPRODUCES at HEAD** (likely lived in the uncommitted engine state
   this reel was built against). ~~`var` fields class-lock on script objects —
   interpreter only.~~ `var` fields stay dynamic on both backends; pinned by
   `var_field_retypes_across_script_classes` (member and unqualified in-method
   shapes). The `cur = null;` resets were removed from the reel.

7. **NO LONGER REPRODUCES at HEAD** (verified with the graveyard removed over a
   2,200-frame Release run on both backends, byte-identical; also re-verified
   against the pre-fix engine — the crash lived in the uncommitted engine state
   this reel was built against). ~~Destroying a script-class instance mid-run
   segfaults the interpreter~~ — the graveyard is gone; scenes are destroyed at
   every transition now.

8. **BELIEVED FIXED** (same root as finding 2: method frames
   reserved 0 slots, so many-block-scope methods reallocated their locals vector
   mid-frame — any held pointer/reference into it dangled with exactly this
   delayed-detonation signature). ~~Progressive corruption with delayed
   detonation~~ — not reproducible at HEAD (the original pre-workaround scene
   shapes no longer exist to retest byte-for-byte); the split methods stay.

9. **Host-callable poisoning NO LONGER REPRODUCES at HEAD** (fixed by
   reentrant execute isolation — saved call/value stacks, no mid-run
   pool reset); pinned by `coroutine_in_host_callable_no_state_leak`. Coroutine
   *methods* work (`coroutine` prefixes a method or static method like a free
   function); the finale drives its sequence off scene time.

10. **NEW (interpreter only).** Plain
    *assignment* of a bare array-element read into an int-typed local throws
    `Cannot assign ... to 'x' (declared int)`; the *declaration* shape is fine, and the VM
    accepts both. Hit in KINSTEIN's DDA loop; likely the new element-ref
    (ref_lvalue) surface.
    ```jaiscript
    var a = [7];
    int x = 0;
    x = a[0];        // interpreter: Cannot assign ... to 'x' (declared int)
    int y = a[0];    // fine on both backends
    ```
    Workaround (ships in kinstein.jai): read into a declared local, then assign.

11. **FIXED** (a bail inside a statement flushes the values the statement
    defined before it; an inlined call's fallback keeps the caller's statement).
    ~~Found while rewriting DONUT (its splat
    version): inside a large render method of an IMPORTED unit (the shim's
    `import "scenes/donut.jai"`), the fused int expression
    `mt[mrow + i] + itrunc(d * 15.99) * 4 + itrunc(s16 * s8 * 3.99)` (`itrunc`
    a script function defined by the importing file) evaluated to a wrong value
    on the VM — 17,908 wrong evaluations a frame against 0 on the interpreter.
    The same program pasted into one file is correct, and splitting the
    expression into three typed locals is correct. The sphere-traced DONUT no
    longer has the shape; the three-file reproduction (a host stub, `util.jai`,
    the reduced scene) was handed to the engine side.~~ Pinned by
    `a_bail_inside_a_statement_flushes_its_spills_and_graft_values`.

**Paper cuts:** error text reaching the host keeps `{0}` placeholders and drops
script context; `override` is required to redefine a base method (good check,
surprising the first time).

**Perf lesson (PIPEDREAM):** a script *method call* costs on the
order of tens of microseconds — an empty-bodied helper invoked ~1,500x per frame
cost ~60 ms before its body ran a single op (measured by gutting the method).
Fine-grained helpers in per-cell loops are a trap; either inline the work or
restructure so the call count collapses. PIPEDREAM's fix was architectural: the
canon-accurate static camera makes the screen image persistent, so the scene
splats only the ~dozen voxels grown per frame into a kept buffer instead of
re-projecting the whole volume (160+ ms -> fire-scene cost). KINSTEIN keeps its
per-blob helper but does one sqrt per sprite ROW instead of float math per cell.

**Perf lessons (the technical climax block — all A/B-measured with
throwaway micro-scenes in a scratch copy of the reel):**

- **Array assignment deep-copies — `var&` reference declarations are the hot-loop
  idiom.** `var alias = field_array;` copies the whole array (value semantics,
  exactly as the notes above warn), so writes through it silently vanish:
  SPONGEWORKS' persistent pixel buffer stopped persisting and BANNERFALL's
  physics froze, while every parity hash stayed green (consistently wrong again).
  `var& alias = field_array;` binds a true reference — writes land, and element
  access through it costs about HALF a field access (see next point). All three
  new scenes bind every hot array this way at the top of update/render.
- **Field access resolves through the environment chain — ~2x a local slot.**
  20k field-array reads cost ~1.8 us each vs ~0.9 us through a `var&` local
  (Release, VM). Hot scalars (`w`, `h`, masks, camera vectors) are worth copying
  to locals too; SPONGEWORKS' ray loop reads 11 camera fields per ray otherwise.
- **A declaration costs ~0.3-0.7 us — hoist or inline in tight loops.** The
  cloth solver dropped from ~13 locals per constraint to 5 (element reads inlined
  into expressions, corrections written as compound stores) for a measurable win
  across its ~2.2k solves x 2 passes per frame.
- **Compound element stores (`arr[i] += v`) are ~2x cheaper than read+write
  pairs** (~0.7 us vs ~1.4 us) — one lvalue resolve instead of two. The Verlet
  relaxation writes all six endpoint corrections that way.
- **Temporal reprojection works in script.** SPONGEWORKS caches each pixel's
  last hit distance and warm-starts the next march 15% short of it — most
  surface rays converge in a few steps. Free real speedup, but it broke
  step-count AO (step counts stop correlating with occlusion, adjacent pixels
  speckle) — the fix was one extra SDF probe along the normal instead.
- **Shift operators exist and floor correctly for non-negative operands** —
  CANYONRUN's fixed-point inner loop keeps its coordinates offset positive so
  `>> 16` is an exact floor with no host `ifloor` calls (the whole depth-march
  inner loop runs without a single host call or float op).

**What carried the demo:** parity discipline is real — thousands of frames
byte-identical across two completely different execution engines, in Debug and
Release. Hot reload with instance migration works exactly as advertised. jaibite
save/load worked on the first try, including cross-engine symbol relocation.
`var&` reference parameters, once discovered, are exactly the right tool —
`f(obj.field)` and `f(arr[i])` lvalue refs included. Template literals,
switch-on-string, and the builtin container methods (`push`, `join`, `clear`,
`repeat`, `substr`) are pleasant, and `dynamic_binder` made the seeded rng a
one-liner per method. A full scripted render pipeline — simulation, palette
lookup, half-block run-length escape assembly, string join — moves 8,000
truecolor pixels in about 3.5 ms a frame on the VM, and `parallel_transform`
spreads the five heaviest scenes across the cores without costing a byte of
parity.
