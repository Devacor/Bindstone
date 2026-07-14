# jai_demoreel — a demoscene tech reel written in JaiScript

Thirteen auto-advancing truecolor scenes, transitions, title cards, and a finale where
the word JAISCRIPT ignites out of a wall of rainbow fire — every effect coded in
`scenes/*.jai`. The C++ host (`main.cpp`) is a thin shell: a VT console, a
monotonic clock, non-blocking keys, a seeded rng class, and the frame pump.
Everything you see is script.

The reel is also an engine showcase: it keeps **two live engines** warm — one
tree-walking interpreter, one bytecode VM — running the same scripts, and lets you
flip between them mid-run. It doubles as a parity test: in `--smoke` mode both
backends must produce **byte-identical** frame streams.

## Build

Part of the JaiScript CMake tree (target `jai_demoreel`):

```
cmake --build out/build/x64-Debug --target jai_demoreel
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_demoreel
```

The exe prefers the source `scenes/` dir (so hot reload edits the real files),
falling back to `demoreel_scenes/` copied next to the binary.

### No build: the standalone runner

`demoreel.jai` is a pure-script shim that recreates the C++ host contract (the
`ESC` global, `itrunc`/`ifloor`, and a bit-exact script port of the seeded
xorshift64* `Rng` — checked-int64-safe via 16-bit-limb multiplies) and pumps the
reel on the standalone runner — drag-drop it onto `jaiscript.exe` or:

```
jaiscript demoreel.jai                  # the reel, 30 fps (q/esc quits; h/f/n/p/space/digits work)
jaiscript demoreel.jai -- frames 1700   # headless self-test: smoke frames, sizes + stream hash + timing
jaiscript demoreel.jai -- frames 60 dump P   # also write frames 0/N-2/N-1 to P_<i>.txt for diffing
```

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
jai_demoreel --capture I         # print scene I as plain text (ANSI stripped)
jai_demoreel --reload-test       # headless hot-reload + backend-swap self-test
jai_demoreel --precompiled       # boot from demoreel.jaibite (saved on first run)
```

Keys while running:

| key | action |
| --- | ------ |
| `TAB` | swap live backend (interpreter <-> VM); scene state re-synced, stateful scenes replay up to 3 sim-seconds |
| `r` | hot-reload every `scenes/*.jai` from disk into BOTH engines |
| `h` | toggle the HUD (scene, ACTIVE BACKEND, frame ms, fps) |
| `f` | freeze the clock (render keeps running) |
| `n` / `p` / space / arrows | next / previous scene |
| `1`..`9`, `0` | jump straight to a scene (`0` = scene 13, the finale) |
| `q` / `ESC` | quit |

The HUD shows the active backend and its frame cost, plus the last measured cost
of the other backend — press TAB and watch the ms number move.

## The scenes

| # | scene | what it is |
| - | ----- | ---------- |
| 1 | PLASMA | the classic summed sine field, 96-color cycling truecolor palette, all integer table lookups per cell |
| 2 | STARFIELD | 3D perspective star streaming with speed-scaled trails (flat parallel arrays on purpose) |
| 3 | DONUT | the rotating torus homage, z-buffered, luminance-shaded ASCII, molten copper |
| 4 | JULIA | morphing Julia set (c orbits the cardioid), escape-count colored, half-res doubled columns |
| 5 | PIPEDREAM | the 3D Pipes screensaver homage: four THICK pipes (block-glyph splats, midpoint-filled tubes) grow one segment per tick through a 16x12x16 voxel volume, glossy elbow balls at every turn, never intersecting. The camera holds still for a whole generation — fill, hold, CRT wipe, reseed from a NEW angle. ~2% of elbows spawn the jackodile's eye (a rainbow-shimmering ball) — canon demanded a teapot |
| 6 | POWDER | falling-sand toy: sand / water / ember / plant / smoke, each element a script class, spawners keep it evolving |
| 7 | BOIDS | murmuration with fading trails and one hungry hawk |
| 8 | KINSTEIN 3D | Wolfenstein-style DDA raycaster over an authored 24x24 keep: one ray per column, perpendicular-distance slices (no fisheye), N/S vs E/W face shading, stone/moss/gold walls + an animated rainbow-fire banner. Billboard Grublins and Orglis brutes garrison the route (wall-occluded per column, distance-scaled, idle wobble); the scripted camera auto-fires on line-of-sight cone lock — muzzle flash, rainbow tracer, hit-flash, gib scatter, kill tally. Enemies respawn each lap. The reel's compute stress scene |
| 9 | CANYONRUN | the Comanche (1992) VoxelSpace algorithm: a 128x128 wrapping heightmap grown by diamond-square (pure integer hashing), a river canyon carved along a wrapping sine meander, height bands + north-light slope shading baked into a packed color/height map. Rendered front-to-back per depth line with a per-column y-buffer, the inner loop pure 16.16 fixed-point integer adds — zero host calls, zero float math. The camera flies the river's own meander formula, banking into turns (per-column horizon roll) and diving on an altitude wave with terrain-clearance lookahead |
| 10 | SPONGEWORKS | a raymarched Menger sponge: per-pixel signed-distance sphere tracing of the KIFS fold (abs / sort / x3 — no sqrt, no mod, no host calls), exact face normals recovered by ONE tracked re-fold (the fold is abs+permutation, so the folded box's gradient axis maps back through the swaps), SDF-probe ambient occlusion, distance fog, six face palettes. Half resolution into a persistent buffer, 4-phase Bayer temporal update, and per-pixel temporal reprojection (last hit distance warm-starts the march). The reel's floating-point stress: the VM showcase |
| 11 | BANNERFALL | Verlet cloth: a 26x11 grid (structural + shear, Jakobsen sqrt-free relaxation, constraint topology as index arithmetic — the hot loop has zero host calls) flying a rainbow weave with the gold JAI sigil from a pole. Real surface-normal lighting (edge cross products, alpha-max magnitude — no normalize). The arc is state-driven physics: calm ripple, rising gale, the hoist seam TEARS thread by thread (weakest at the top, so it peels downward; tears render as holes the moment they happen), the gust dies as the last thread parts, and the free banner tumbles and bursts into ember scraps on a pre-shuffled tear order |
| 12 | FIRE | bottom-seeded convection fire that ignites into RAINBOW fire mid-scene (house signature) |
| 13 | FINALE | a fire wall out of which "JAISCRIPT" ignites, burns in rolling rainbow flame, and remains glowing as the fire dies |

Between scenes: dissolve / wipe / shutter transitions (two grids merged per cell
into a combined palette). Title card with a demoscene handle on every scene.

## Measured numbers

Release (`x64-Release BENCHMARKS`), 100x40 = 4,000 truecolor cells per frame,
seed 20260705, fixed dt 1/30, whole 13-scene reel incl. transitions
(`--smoke`, 1,700 frames); 2026-07-06 VM-perf branch, quiet machine
(~10% ambient load — the earlier 10-scene table was taken while other agents
were compiling on this box and ran ~2x hot; treat absolute ms across README
revisions with suspicion, ratios and hashes are the stable part):

| backend | ms/frame (mean) | fps | boot (parse) |
| ------- | --------------- | --- | ------------ |
| interpreter | 47.1 | ~21 | 18.8 ms |
| **VM** | **36.8** | **~27** | 17.3 ms |

- VM 1.28x over the interpreter for the whole 13-scene reel. The three new
  climax scenes are array-element-bound (terrain maps, cloth positions, pixel
  buffers) where the two backends are closer than in pure control flow —
  PIPEDREAM-style scripted logic remains the widest per-scene gap.
- Parity: frame streams **byte-identical** across backends over 1,700 frames —
  every one of the thirteen scene entries (hash `499577b411b67162`), covering
  CANYONRUN's terrain march, SPONGEWORKS' temporal-reprojected raymarch, and
  BANNERFALL's full physics arc; plus the earlier 2,660-frame full-duration
  run for PIPEDREAM/KINSTEIN. Debug build parity also green historically —
  iterate in Release for this demo (Debug ran ~0.7-0.8 s/frame on the 10-scene
  reel).
- `--reload-test`: hot reload of all scenes into both engines + TAB-style state
  sync, green (13 scenes).

## Console performance (the write is half the frame)

The smoke numbers above are script-only: a live frame also has to get through the
console, and legacy conhost parses VT escapes ~16x slower than Windows Terminal
(measured: the identical ~47 KB plasma stream costs **1.2 ms** to write in Windows
Terminal, **~19 ms** in conhost). The HUD is honest about this now: its headline
frame cost is `sim + draw` (script update + frame string, plus the console write),
split out so you can see which side you're bound on. `--bench N --scene I` runs N
real frames and reports the same split headlessly (`--bench-out FILE` to save it).

What the host does about it (all post-processing in `main.cpp` — scenes and the
`--smoke` frame stream are untouched, so parity hashes don't move):

- **Escape trimming.** Scripts already emit a color escape only when the palette
  index changes; the host additionally drops any SGR group that wouldn't change
  terminal state, and (default on conhost) rewrites 24-bit colors to the xterm-256
  cube — shorter escapes and coarser quanta = longer runs. `--truecolor` keeps
  24-bit with per-channel merge tolerance (`--tol N`, default 8); `--no-filter`
  is the byte-exact legacy stream.
- **Terminal detection.** `WT_SESSION` distinguishes Windows Terminal from legacy
  conhost. Conhost defaults to 256-color mode and shows a one-line tip on the
  title card ("Windows Terminal renders this reel much faster"); everything else
  defaults to truecolor + tolerance merging.
- **One buffered write per frame**, via `WriteConsoleA` on a real console.
- **`--diff`** row-diff redraw: only rows that changed since the last frame are
  rewritten (cursor-move + row). Not the default — plasma/fire change every cell
  every frame and defeat it — but starfield's write drops to ~1 ms.

Measured on conhost (Release, 120 real frames per scene, ~110x29 window):

| scene | bytes/frame raw -> written | write ms raw -> filtered | fps |
| ----- | -------------------------- | ------------------------ | --- |
| plasma | 29.6 KB -> 15.2 KB (2.6x) | 19.1 -> 14.1 | 23.8 -> 25.0 |
| fire | 34.2 KB -> 9.7 KB (3.6x) | 23.4 -> 8.2 | 18.7 -> 27.8 |
| finale | 29.9 KB -> 10.9 KB (3.0x) | 19.4 -> 9.7 | 13.4 -> 14.9 (sim-bound) |
| starfield (`--diff`) | 5.3 KB -> 2.4 KB | 1.0 | 82 uncapped |

The two new scenes, measured headless (`--bench 30 --scene I`, Release, 100x40,
truecolor+tolerance mode, quiet machine — sim ms is the interesting split here):

| scene | backend | sim ms | bytes/frame raw -> written | fps uncapped |
| ----- | ------- | ------ | -------------------------- | ------------ |
| PIPEDREAM (4) | **vm** | **25.6** | 14.8 KB -> 14.4 KB (1.02x) | **38.3** |
| PIPEDREAM (4) | interpreter | 39.7 | 14.9 KB -> 14.5 KB (1.02x) | 24.8 |
| KINSTEIN (7) | **vm** | **28.4** | 9.0 KB -> 9.0 KB (1.00x) | **34.2** |
| KINSTEIN (7) | interpreter | 32.2 | 9.0 KB -> 9.0 KB (1.00x) | 30.7 |

KINSTEIN is the cheapest frame in the reel to *write*: quantized distance shades
over solid-bg slices mean the script's own run-length trick already emits ~9 KB
a frame — the host filter finds nothing left to trim (1.00x). PIPEDREAM shows
the widest per-scene TAB gap (25.6 vs 39.7 ms sim, VM 1.55x) — its incremental
splat pass is pure scripted control flow, exactly where the VM pulls ahead.
Both scenes hold comfortably above 20 fps on both backends in Release.

The technical climax block (`--bench 30 --scene I`, Release, 100x40, truecolor
mode, min of 3 runs at high process priority, quiet machine ~10% ambient —
KINSTEIN re-measured the same session as the in-family reference: vm 14.7 /
interpreter 19.2 ms sim):

| scene | backend | sim ms | bytes/frame raw -> written | fps uncapped |
| ----- | ------- | ------ | -------------------------- | ------------ |
| CANYONRUN (9) | **vm** | **29.3** | 11.4 KB -> 11.4 KB (1.00x) | **~33** |
| CANYONRUN (9) | interpreter | 35.2 | 11.3 KB -> 11.3 KB (1.00x) | ~28 |
| SPONGEWORKS (10) | **vm** | **32.5** | 13.7 KB -> 13.4 KB (1.02x) | **~30** |
| SPONGEWORKS (10) | interpreter | 43.1 | 13.8 KB -> 13.5 KB (1.02x) | ~23 |
| BANNERFALL (11) | **vm** | **39.9** | 10.2 KB -> 10.2 KB (1.00x) | **~24** |
| BANNERFALL (11) | interpreter | 54.6 | 10.0 KB -> 10.1 KB (1.00x) | ~18 |

All three hold 15+ fps on BOTH backends. BANNERFALL shows the widest VM gap of
the three (1.37x — the Verlet constraint solver is exactly the branchy
element-heavy loop the VM chews through), SPONGEWORKS 1.32x (per-pixel float
fold math), CANYONRUN 1.20x (its inner loop is integer adds and grid fills, the
most backend-neutral work in the reel). Honest-tuning ledger: SPONGEWORKS runs
half-res 2x2 blocks with a 4-phase temporal update and 14-step ray budget;
BANNERFALL runs 2 relaxation passes (structural both, shear on the first) on a
26x11 grid — four passes of a 34x13 grid looked marginally better and cost
double; CANYONRUN marches 44 depth lines with a 6-bank haze quantization.

And if it still *feels* like one frame per second: check you're not running the
Debug build (~0.7 s of sim per frame — see above).

## Live coding

1. Run `jai_demoreel`.
2. Open `scenes/plasma.jai` in an editor; change palette constants (e.g. the
   `gfx.rainbow_rgb` phase offsets, `ncolors`, or the field frequencies in
   `render`).
3. Press `r` in the console window.

Both engines re-execute every scene file (hot reload with instance migration) and
the current scene rebuilds — the change appears without restarting. `main.jai` is
deliberately never reloaded: it owns the persistent globals.

## Determinism / parity

`--smoke` runs N frames headless at a fixed dt on the interpreter, then the VM,
FNV-1a-hashing every returned frame; the hashes must match. All scene randomness
goes through the host's seeded xorshift `Rng` class, so a one-cell divergence
between backends fails the run. Env helpers: `JAI_DEMOREEL_SMOKE_FULLDUR=1`
(full interactive scene durations), `JAI_DEMOREEL_DUMP_FRAME=N` (write frame N of
each backend to a file for diffing), `JAI_DEMOREEL_TRACE=1` (per-frame progress
for crash localization). Deliberately NOT part of the main test suite.

## --precompiled

`engine->jaibite()` parses the concatenated scene source once; `--precompiled`
saves the parsed AST as `demoreel.jaibite` on first boot and loads it afterwards.
Measured: parse ~8-12 ms vs .jaibite load ~3.2-3.5 ms (and the .jaibite saved by the
interpreter engine loads into the VM engine — `jaibite_load` re-interns symbols
per engine, so one file serves both backends).

## Text captures

DONUT (`--capture 2`, ANSI stripped — luminance-shaded torus):

```
     ,.--....., . .. . .. :......= ......!  ..   .    .*     ~    !
     ,.,., .... .. . .-.. ............   ..   .   *     . ,   * :*
      . .... ....... . .- ...:..... = ..   .!   .  . .    * ,
      ...... ......... ...-......  ..  ...   .   ..   .  .   ,  ~ :!=
      ..... ..... .............:...  ..;  .   ..   . ! .  .      !
       .... . ... . ........-. ..  ..  ..  ..  =   .       . !,  -  :;=
        ........  ..., ,......-.. ..  .  ;.  .   .   . !.          ! ~:
         .,,,,-,,., .,.-...:.~.  .  :  .  ..  .  =.  .   .  .  !.  -   ;
          .,--.~.:.:= ; .!.!...;. .. .~ .  .   .  .   .  .=  .  .  ,= - ;
```

BOIDS (`--capture 6` — the flock, trails, and the hawk `@`):

```
                                            ^ :  ^.^^
                                         */^: ^^ .^ *
                                      ./ . /:^:*^ ^  ^
                                         /.:/ ^.^ ^  ^
                                  /       /:*^.^. ^  :
                                 .* /./ :/.  : .  .
                  @        :/      :*   .
                          . :/  :/   */
```

PIPEDREAM (brightness map of the raw frame near fill — thick interlocking tubes;
the brightest cells are the glossy elbow balls; every generation reseeds from a
new static camera angle):

```
                  ####@@%%@@%%@@@@@@@#################@@@@@@@@@@@@@@**@@%%%****### %%%
             ********#%%%%%%%@@@@#########@@########@@@#@@@@@@@@@@@@**@@%%% ***  ###%%
             *+****   %%%%%%%@########################@#####@@@@@@@@@#@@%%  ***    ##
             ++++**++ %@@%%###############@@########@@@#####@@@@@@@@@@@*%%  ****   #%%%
            **++++   +%@%%%###@#########@@############@#######@@@@@@@##***%% *** %##%%%
            ****++   %%%%%#@#########@@##@############@########@@@@@@@@@**** *** ##%
            ######   %%%%%########@**%%%#@@@########@@@########@@@@@@@@@##**%%%%**#
           %%%#### **%%%%**@#####****%%##@####@@@@@###@@@@@######@@@@@@@##***********
           %%%****** %%%@##@#####**%%%%@@@@@##@@@@@#@@@#@@@@@####@@@@@***************  ###
           ** %%%    %%%@#@@@####*********@@##@@@@@#@@@##########@@@@@@@@%%  @@@@*****++##
          ***       %%%@@@@@@######@@@@@##@@@@@###@@@@@#####@@@###@@@@@@@@@@@@@######@+##
          **       @@@@@@@@@@@#####@@@@@##########@@@@@###@@@@@###%%% @@@@@@@####@###@###+
```

KINSTEIN 3D (brightness map of the raw frame — a one-point-perspective corridor,
full-height near slices at the edges stepping down to the vanishing point; the
bright center column is a rainbow tracer mid-kill, the dots around it gib
scatter where the Grublin stood, and the `-+-` at the bottom is the gun):

```
%%%%%%%%%%%%%%%%%#####..............................................#####%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++===---::       :--====++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++===---:. .. .. :--====++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++===---:....@...:--====++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++===--......%.....-====++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++===-.......+......====++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++==.........=........==++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++++...........+..........++++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******++.............#............++******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######******...............@..............******#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######****.................%................****#######%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%#######:::::::::::::::::::::+::::::::::::::::::::#######%%%%%%%%%%%%%%%%%%%%%%
```

FINALE afterglow (brightness map of the raw frame as the fire dies — the word
stays lit in rainbow flame):

```
               ....#..#####..###..########....#####..###..#####. .#.
               ....#..#####..###..#####.##....###.#..##...###.#  .#.
               ...##..#####..###...####.##....####...###..####   .#.
               ..###..#. .#..###...####.##....#.#....###..#.     .#.
                #..# .#. .#..###...#..# #.##..#. #...###..#.     .#.
                  ##   #   # ##### ####   #### #   # ##### #       #
```

SPONGEWORKS (brightness map of the raw frame — the raymarched Menger sponge
mid-orbit: the perforated top face catches the light, the recesses fall into
SDF-probe shadow, and the darkest cells are rays that spent their step budget
deep in a crevice):

```
        ..                                ==========
        ..                                ==========
                ..        ********....**************  ****......**************
                ..        ********....**************  ****......**************
        **********..  ..######  ####..........  ..  ..........################**********....
        **********..  ..######  ####..........  ..  ..........################**********....
      ........################....  ....##########......  ######........##########........
      ........################....  ....##########......  ######........##########........
      ....................####################  ############################..............
      ....................####################  ############################..............
        ................  ............######################  ######................  ..
        ................  ............######################  ######................  ..
        ....--##........  ........................@@@@@@  @@@@..........................
        ....--##........  ........................@@@@@@  @@@@..........................
          ................  ....................................::..........##..........
          ................  ....................................::..........##..........
```

BANNERFALL (brightness map of the raw frame just after the hoist seam starts
to tear — the banner streams right of the bare pole segment it has already
peeled from; in color it is a rainbow weave carrying the gold JAI sigil):

```

              %
              :
              =                                             --
              :                                           ==---
              =                                          -++***
              :                                         .--++++
..............=.......................................-==-==---.....................................
..............:...................................==---==*==***.....................................
..............=............:++++++...........::--.=+===**--=***.....................................
..............:........::::==+***===......::-::..::+==++==+****.....................................
..............=......====+++++++***=.==:::::-:-----**=++==***+++....................................
..............:....=========--.==----%%%--::**---%%***++******+++...................................
..............=***@==--++%%%-++**----**:--::%%--%%%--==+******+++...................................
..............:****.++++++%++==**----**:::::%%-------++++*****+++...................................
..............=*******++==-------------:::--------===++++*****......................................
..............:***********++++-------------:------===+++++..........................................
```

(PLASMA / JULIA / FIRE / POWDER / CANYONRUN are solid background-color scenes —
stripping the ANSI leaves only spaces, and CANYONRUN's drama is entirely in its
truecolor terrain bands and distance haze, so they don't caption well in text.
Run the reel.)

## Language feedback (dogfooding notes — recorded honestly)

Everything below was hit while writing this demo on the VM-perf branch
(2026-07-05 tree, with another agent's uncommitted engine changes linked in).
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

> **Triage update (2026-07-05, VM-perf `b5c118de..`):** every finding below was
> reproduced (or bisected) at HEAD and resolved; pinning regression tests live in
> `source/tests/language/demoreel_regression_tests.cpp`. The reel itself now
> exercises the fixed paths: the graveyard is gone (scenes are destroyed at every
> transition), `render_rows` declares `prev` in the loop body again, and plasma
> passes raw `c[0], c[1], c[2]` element args into typed params.

1. **FIXED** (`53526626`: frame exit clears only the frame's OWN method env).
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

2. **FIXED** (`b5c118de`: method frames reserved 0 slots, so a mid-loop DECL_VAR
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

3. **FIXED** (was already fixed at HEAD by `b80f12fd`: interp ++/-- resolves
   slot-based locals). ~~Interpreter: `++x` on an enclosing-scope local inside a
   for body throws `Undefined variable 'x'`~~ (VM fine; `x = x + 1` fine on both).

4. **FIXED for locals** (`9e5fea2f`: typed declarations and slot stores convert
   like assignment — `int d = 4.7` truncates to 4). Typed *fields* remain dynamic
   (declared field types are discarded at runtime; both backends agree) — pending
   a Dev ruling, pinned by `typed_field_assignment_stays_dynamic_parity`. The
   host still exposes `itrunc()`/`ifloor()`; script has no explicit float→int
   cast expression.

5. **FIXED** (`5ef9c0be`: overload matching derefs lvalue reference arguments).
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

8. **BELIEVED FIXED by `b5c118de`** (same root as finding 2: method frames
   reserved 0 slots, so many-block-scope methods reallocated their locals vector
   mid-frame — any held pointer/reference into it dangled with exactly this
   delayed-detonation signature). ~~Progressive corruption with delayed
   detonation~~ — not reproducible at HEAD (the original pre-workaround scene
   shapes no longer exist to retest byte-for-byte); the split methods stay.

9. **Host-callable poisoning NO LONGER REPRODUCES at HEAD** (fixed by
   `74b438d8`: reentrant execute isolation — saved call/value stacks, no mid-run
   pool reset); pinned by `coroutine_in_host_callable_no_state_leak`. Coroutine
   *methods* remain a parse error — that is a missing feature (the class-member
   parser never accepts `coroutine`), not a bug; needs a Dev decision to build.
   The finale still drives its sequence off scene time (works fine).

10. **NEW (2026-07-06, post-rulings HEAD; interpreter only).** Plain
    *assignment* of a bare array-element read into an int-typed local throws
    `Type mismatch in assignment`; the *declaration* shape is fine, and the VM
    accepts both. Hit in KINSTEIN's DDA loop; likely the new element-ref
    (ref_lvalue) surface.
    ```jaiscript
    var a = [7];
    int x = 0;
    x = a[0];        // interpreter: Type mismatch in assignment
    int y = a[0];    // fine on both backends
    ```
    Workaround (ships in kinstein.jai): read into a declared local, then assign.

**Paper cuts:** error text reaching the host keeps `{0}` placeholders and drops
script context; `override` is required to redefine a base method (good check,
surprising the first time); no script-side float→int cast.

**Perf lesson (2026-07-06, PIPEDREAM):** a script *method call* costs on the
order of tens of microseconds — an empty-bodied helper invoked ~1,500x per frame
cost ~60 ms before its body ran a single op (measured by gutting the method).
Fine-grained helpers in per-cell loops are a trap; either inline the work or
restructure so the call count collapses. PIPEDREAM's fix was architectural: the
canon-accurate static camera makes the screen image persistent, so the scene
splats only the ~dozen voxels grown per frame into a kept buffer instead of
re-projecting the whole volume (160+ ms -> fire-scene cost). KINSTEIN keeps its
per-blob helper but does one sqrt per sprite ROW instead of float math per cell.

**Perf lessons (2026-07-06, the technical climax block — all A/B-measured with
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
lookup, run-length escape assembly, string join — moves 4,000 truecolor cells at
~25 fps on the VM.
