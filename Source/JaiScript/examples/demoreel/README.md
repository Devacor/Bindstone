# jai_demoreel — a demoscene tech reel written in JaiScript

Eleven auto-advancing truecolor scenes, five kinds of transition, pixel-font
title cards, and a finale where an eight-dimensional hypercube unfolds over
Escher's hyperbolic plane, shatters into the word JAISCRIPT and slams it into a
wall of rainbow fire before the credits roll — every effect coded in
`scenes/*.jai`, every frame drawn in **Unicode sextants**, six pixels a cell
(half blocks on request or on very large terminals). A ray tracer, a sphere
tracer, a textured Wolfenstein raycaster, a VoxelSpace terrain flight,
metaballs, a murmuration, a skull-headed spider weaving an orb web in a morphing gyroid
labyrinth, a Navier-Stokes ink tank and the classics. The C++ host (`demoreel_host.cpp`) is a
thin shell: a VT console, a monotonic clock, non-blocking keys, a seeded rng
class, and the frame pump. Everything you see is script.

The reel doubles as a determinism test: `--smoke` hashes the whole frame stream
against a reference. Six scenes trace their rows across every core with
`parallel_transform`, and the stream stays byte-identical anyway.

## How a frame is drawn

Each scene paints a grid of palette indices in square pixels, and the presenter
in `util.jai` packs them into cells one of two ways:

- **Sextants** (the default): a cell is 2x3 pixels drawn with one of the 64
  Unicode 13 sextant glyphs, fitted as JaiDoom fits them (`vid_row_sext`): the
  cell's brightest pixel is the foreground, its darkest the background, and each
  pixel takes the nearer in luminance. A sextant is 2/3 as wide as it is tall, so
  the grid is three pixels a row tall and one and a half a column wide, and each
  of a cell's two glyph columns shows the grid column nearest it.
- **Half blocks**: a cell is 1x2 pixels through the upper-half-block glyph `▀`:
  the **foreground** truecolor escape paints the top pixel, the **background**
  escape the bottom one (JaiDoom's `vid_row_half`).

`pix half|sext|auto` (`--pix` on `jai_demoreel`) picks; auto takes sextants while
the grid stays within 48,000 pixels (about 160x60 cells) and half blocks above,
and the density map always reads half blocks. Escapes are emitted only when a
color changes along the row, and a cell whose pixels agree collapses to a space
(a background run) or a full block (a foreground run), so smooth regions cost one
byte per cell. Palettes
precompute both escape strings per entry; a transition simply concatenates the
two scenes' tables (the incoming scene's indices shift by an offset) and the
reel's own UI entries ride at the end. `hb_row` is written as a
`parallel_transform` body over captured globals, so the same function serves
the serial loop and, with `HB_WORKERS` set (the standalone runner does on wide
terminals), a parallel presentation.

Text stays crisp on top: the HUD, the scene handle under each title card and
the finale's credits are ordinary glyph rows, and a scene can lay characters on
any cell through the text layer (`txt_put`, `txt_glyph`: a codepoint and a color
per cell, drawn over the cell's darkest pixel). The big title letters themselves
are pixels from a 5x7 font (`FONT5`), typed in with a shine sweeping across, at
the largest scale that fits.

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

(On Windows the runner is `build\bin\jaiscript.exe`.) The native host is the `jai_demoreel` target: `cmake --build build --config Release --target jai_demoreel`, then
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
jaiscript demoreel.jai -- color 256          # the reel quantized to the xterm-256 cube (color 24 forces 24-bit)
jaiscript demoreel.jai -- ascii 1            # one density character per cell (no block glyphs)
jaiscript demoreel.jai -- pix half           # half blocks instead of sextants (for a font without them)
jaiscript demoreel.jai -- frames 1700        # headless self-test: smoke frames, sizes + stream hash + timing
jaiscript demoreel.jai -- frames 60 dump P   # also write frames 0/N-2/N-1 to P_<i>.txt (+ P_<i>.bmp, the pixel grid)
jaiscript demoreel.jai -- bench 60           # every scene mid-flight: script ms/frame and KB/frame
jaiscript demoreel.jai -- shot 8 at 4 dump P    # one frame of scene 8 at scene time 4 -> P.txt (as presented) + P.bmp
jaiscript demoreel.jai -- midi cues          # write the soundtrack's cues (one MIDI file a scene) and exit
jaiscript demoreel.jai -- video frames every 2   # the whole reel's frames as 24-bit text, with the cues, for a video
jaiscript demoreel.jai -- music 0            # the reel, silent
```

The soundtrack is procedural and the scenes hear it (`scenes/music.jai`). At boot
the script composes one cue per scene at 124 bpm: each scene has a mood (a scale
and chord progression, a drum style, General MIDI instruments for bass, pad,
arpeggio and lead, a level of energy), the energy builds into each of the
scene's declared hits and drops on it, the lead sings a two-bar motif from the
reel's seed over each chord, the toms fill into every fourth bar, and each hit
lands a crash, a low boom and a brass stab with a snare riser into the big ones.
The same score is data the scenes query every frame (`cue.kick_env(t)`,
`snare_env`, `hat_env`, `bass_env`, `chord`, `energy`, `hit`, `riser`), so the
pictures move with exactly the music being played, deterministically and with no
audio analysis. Every scene is a whole number of bars and starts on a bar line,
so the cues play back to back: the runner preloads them as MIDI files and
switches the frame a scene's transition starts.

Verified: the 1700-frame smoke reel produces the same stream hash and
byte-identical dumped frames under `--backend=vm` and `--backend=interp`.
The shim picks its color depth as `jai_demoreel` does, through the runner's
`env_var`: 24-bit under `COLORTERM=truecolor` (or `24bit`) and in Windows Terminal
(`WT_SESSION`), the xterm-256 cube elsewhere; `color 24`, `color 256` and `ascii 1`
override it. Not ported
from the C++ host: its escape trimming and row-diff redraw (frames present
whole) and `r` hot reload.

On Windows the runner sets the console to UTF-8 and VT output. q, ESC and
Ctrl+C all end the reel with the cursor, the colors and the main screen
restored, on every platform, as `jai_demoreel` does.

## Run

```
jai_demoreel                     # the reel, 30 fps pacing
jai_demoreel --ascii             # one density character per cell, for terminals without block glyphs
jai_demoreel --pix half          # half blocks instead of sextants (--pix sext forces sextants at any size)
jai_demoreel --256color          # force the xterm-256 cube (--truecolor forces 24-bit)
jai_demoreel --smoke             # headless: frame-stream hash + perf
jai_demoreel --smoke --frames N  # smoke frame budget (default 1700)
jai_demoreel --capture I         # print scene I as plain text (ANSI stripped: block glyphs and spaces)
jai_demoreel --capture I --ascii # the same frame as a density map
jai_demoreel --reload-test       # headless hot-reload self-test
jai_demoreel --precompiled       # boot from demoreel.jaibite (saved on first run)
```

Keys while running:

| key | action |
| --- | ------ |
| `r` | hot-reload every `scenes/*.jai` from disk; a band under the HUD names the files that changed, or the file that failed to load with its error |
| `h` | step the HUD: two quiet chips on the bottom row (the scene; the VM's ms per frame, cores and fps), then the full status bar (backend, frame ms split into sim and draw, fps, pixel size) and a key bar that fills as the scene plays, then off |
| `f` | freeze the clock (render keeps running) |
| `n` / `p` / space / arrows | next / previous scene |
| `1`..`9`, `0` | jump straight to a scene (`0` = scene 11, the finale) |
| `q` / `ESC` | quit |

`jai_demoreel` runs one engine, on the VM: it links the library without the
interpreter, so the two backends are compared on the standalone runner
(`--backend=interp`, above). The HUD shows the backend and its frame cost. Ctrl+C, Ctrl+Break and SIGTERM
end the reel through the same exit as `q`: the cursor, the colors, the
main screen and (on Linux and macOS) the terminal's input mode come back.

## The scenes

| # | scene | what it is |
| - | ----- | ---------- |
| 1 | DONUT | a product shot that drops in on the beat: the donut sphere-traced per pixel as a distance field in its own frame (the dough torus, an icing layer with real thickness, a beaded lip, a wavy edge and drips, and sprinkles raised off it as capsules on a hashed grid in the torus's coordinates, catching the light and shadowing the icing), over a glossy walnut table that reflects it and takes its soft shadow, before a cyclorama with drifting bokeh; scored to its cue: the title bar is dark with a glint tracing the silhouette over the riser, on the next downbeat the donut drops in and lands as the key light slams on with a flash and a shake, then it hops on every kick with its sprinkles popping off the icing, the light pumps, the camera swings round on the bar lines and pushes in on the sprinkles before the spin-out; it renders within a 26,000-pixel budget and scales up |
| 2 | JULIA | the Julia set as a landscape flown low: every frame a 200x200 height field of a morphing c (orbiting the cardioid) is iterated across all cores, the smooth escape count raising ridges along the set's boundary and the inside a low lake the orbit trap ripples; each screen column is cast voxelspace-style across it (bilinear heights, slope light, fog to the horizon, a sky) from a camera that keeps clear of the ground; scored to driving sixteenths: kicks punch the terrain up and widen the lens, snares flash the sky, the chord turns the palette, the energy sets the speed; the riser climbs high over the set and the drop plunges into a canyon run between crystal ridges as c jumps to a new set, and the camera pulls up into the sky at the end |
| 3 | LAVALAMP | metaballs in a lamp: the shot opens in macro on the wax and pulls back to show a rounded glass vessel on a steel base under a steel cap in a dark room; seven blobs rise off the heater and sink as they cool over a pool that never leaves the base, stretched into taffy by their speed; one loop per pixel sums the inverse-square field, its gradient and a contribution-weighted hue, so merging blobs blend their colours into a bridge, and the wax is lit from a normal built out of the gradient (diffuse + a sharp specular); between blobs the violet liquid glows with the wax's light, the glass carries a fresnel edge and a highlight stripe, the steel a studio reflection, the wall and the table a halo of the lamp's colour; on the beat the light pulses and the wax burns through the rainbow, painting the room, before the camera pushes back into the glass; rows run in parallel |
| 4 | PIPEDREAM | the 3D Pipes screensaver homage on a synthwave set: six chrome pipes grow one segment per tick through a 16x12x16 voxel volume, never intersecting; every tube is a chain of lit sphere impostors a quarter voxel apart (per-pixel normal reflecting the set, so each tube carries the horizon's bright band; specular, fog, depth nudge so crossings intersect), glossy elbow balls at every turn, over a striped sun and a floor grid rushing in; the persistent splat image is read through a slow push-in and drift, a glint sweeps every pipe and the frame flashes when a generation fills, then a CRT beam wipes back to the set and the next generation grows from a new angle; ~2% of elbows spawn the jackodile's rainbow eye Scored to its cue: the first generation fills on the downbeat of bar 3 and the second on bar 6 (both hits), the wipe takes two beats, the kick punches the camera and lights the floor grid, the snare flares the chrome, each chord recolours the sun and the energy sets the floor's speed. |
| 5 | BOIDS | a starling murmuration over a lake at sunset: a thousand birds fly in 3D, keeping their distance from and matching the heading of their nearest neighbours (a uniform grid hashed every frame, steering in parallel chunks), drawn to places on a sheet that swings, stretches and folds; a peregrine falcon stoops through them, and panic passes bird to bird as a scare wave that splits the cloud; projected from the shore into a density buffer, so the cloud darkens where it is thick and pales where the sheet faces you, mirrored in the lake under the sun's glitter path; cut like a film: god rays fan from the sun and the grade sinks toward dusk, the first stoop plays in slow motion on the flock's own clock as the camera pushes in on the strike (the flock squeezes into a ball, a flash and a lens flare land on impact), the second shakes the frame at full speed, and the murmuration ends as a long ribbon slanting down across the sun Scored to its cue: the slow-motion strike lands on bar 4 (the falcon's dive aimed through the flock's centre) and the second stoop on beat 3 of bar 5, the kick swells the rays and darkens the flock, the snare sparks the glitter, the bass breathes through the sky, and the bar before the strike draws the light in. |
| 6 | GLOBHUNTER | Wolfenstein-style DDA raycaster over an authored 24x24 keep, TEXTURED: coursed stone, mossy stone, fluted gold and a banner of animated rainbow fire (16x16 textures grown in script), floor and ceiling cast per pixel (a floor row and its ceiling row share one walk); LIT by flickering torches through a light map (each cell's steady torch sum, flickering with the torch that rules it, plus the muzzle on each shot), warm in the pools and cold blue in the gloom, under a vignette; billboard Grublins and Orglis brutes lit as spheres per pixel with a torchlit rim, eyes that glow in the dark, grins, wall-occluded per column; the camera hunts: it swings onto a mark in clear sight, bobs as it walks, and fires on cone lock — recoil kicking the view, muzzle flash lighting the hall, a rainbow tracer, a hit-flash and hit-stop, goo that bursts in 3D and bounces off the flagstones and walls, callouts (DOUBLE KILL, MULTI KILL), a pixel-font kill counter; the HUD scales with the screen It plays to its cue: the gun fires only on a beat (on the eighths through the bar after the big hit), footfalls land on the beat, the torches flare with the kick, the big hit on bar five's downbeat floods the hall with light and calls FRENZY, and the last bar slows the walk and holds fire. |
| 7 | CANYONRUN | the Comanche (1992) VoxelSpace algorithm flown as a golden-hour drone shot: a 512x512 wrapping heightmap by diamond-square, a U-shaped gorge carved along a wrapping meander, the land terraced into mesas and stepped cliffs; the colormap is baked with light in parallel rows (a low sun's shadows marched across the height field, diffuse from the normals, ambient occlusion from the height field against its blur) and materials (river, wet sand, a green floor, scrub, mesa tops, pale peaks, ten strata colours); 120 geometric depth steps of 16.16 fixed-point marching with a per-column y-buffer and a sub-texel jitter per column and step, cliff faces painted per pixel with the rock layer at the height each row looks at (integer inverse projection), grain on rock and ground, twelve haze banks; the river mirrors the sky about each column's horizon with ripples and the sun's glitter, the sky is a perspective cloud deck lit by the sunset; the camera skims the river, banks through the bends, climbs out over the rim for the reveal and dives back in It plays to its cue: the climb out runs through bar three to the reveal on bar four's downbeat, where the sun flares with the hit, the dive back in starts on bar six, and the camera nods on the kick and leans on the snare. |
| 8 | CHROME | the Amiga Juggler's grandchild: a ray tracer, one ray per pixel across every core — three tinted chrome spheres bounce on a checkerboard running to a hazy horizon, one reflection bounce, hard shadows, a sun glint; the ray returns WHAT IT SAW as a class and each sphere looks that up in its own tinted copy of the class table; the middle sphere is glass (Snell refraction in and out at index 1.5, the world upside down through it, a mirror rim at grazing angles), the dark tiles are polished and show the spheres standing over them, the camera cranes up from a floor-level dolly, whips around at the middle and pulls back high as the bounces fall into step, and star sparkles bloom on the brightest glints It plays to its cue: every bounce is a parabola landing on a beat (one sphere on each beat, one every other, one on the offbeats), the camera rides the riser in and whips around on bar three's downbeat, and in bar four the spheres fall into step until they land as one on bar five; the glints flare on the hits and twinkle on the snare. |
| 9 | OSSUARY | a story in seven acts over 21 bars (40 s): a horned, three-eyed skull on a spider's body sleeps on a raymarched field that morphs between three triply periodic surfaces (gyroid, Schwarz P, Schwarz D) under a flowing domain warp, its walls dark teal and quiet; it wakes as its crown of flame catches and explores the sheet on eight three-segment IK legs in a tetrapod gait, the sheet melts away into a chamber, it climbs its dragline to the hub, casts the spokes one by one, lays the capture spiral from the rim inward and waits head-down while dew glints on the pale silk; a moth with eyespotted wings strikes on bar 15's downbeat (the cue's big hit), the strike rings out through every thread, the spider runs it down, turns and wraps it in a spinning cocoon, and the camera draws back on the web; the walls breathe with the music (veins lit on the kick, a ring of light leaving the creature on every kick, the wall's hue on the chord, its light on the cue's energy); the silk is drawn over the trace and depth-tested, the moth and cocoon are distance fields in the same march, rows trace in parallel at most about 9,000 rays and scale up to the grid |
| 10 | MAELSTROM | ink in water, simulated: Jos Stam's stable fluids. The flow runs on a half-size grid (vorticity confinement hands back the curl the grid smears away, 20 sweeps of warm-started SOR project it divergence-free, semi-Lagrangian self-advection), and the ink moves per pixel through its bilinear upsampling, so the filaments are finer than the grid; the dye is a density plus a premultiplied hue vector, so inks blend into the hue between them and opposites wash out to pearl, lit as a height field (diffuse + Blinn specular from the density gradient) with motes riding the current. Two jets cross, a ring of nozzles winds the tank up, the tank drains, and eight inks bloom into a rainbow wheel that a whirlpool twists; ink and light rows in parallel; graded for the screen: caustic light (two crossing waves through a cosine table) ripples over the bare water, the jets pump their ink on the 124 bpm beat, a starburst of twelve jets fires a rainbow wheel out of the middle of the tank, a vignette darkens the corners, and the camera pushes in on the whirlpool at the end |
| 11 | FINALE | JaiScript in eight dimensions, a 94-second music video on the soundtrack's 124 bpm: after a title sequence and a four-beat count-in it counts the language in, ONE (scripting), TWO (serialization), THREE (reflection), FOUR (hot reload), FIVE (parallel), SIX (signals), SEVEN (debugging), EIGHT (embedding), four bars a slide (`scenes/show.jai`): the counted word slams in on a sweep of light, a code card slides in and types a real snippet in syntax colours (every script one runs on the runner), a live demonstration plays beside it (Spark objects, an archive round trip, a reflect:: inspector, a file edited mid-loop, eight cores on one frame, a signal fanning out, a paused debugger, C++ and script trading calls), captions run along the lower third and a checklist lights; every slide opens one more axis of an 8-cube (256 vertices, 1024 edges) turning in a window above the demonstration as a world turns in Fez, in eased quarter-turns snapping on the beat, on Escher's hyperbolic plane: the {8,3} tiling in the Poincaré disk, folded per pixel by its three mirrors, three-coloured, opening ring by ring with the dimensions and mirrored through the circle at infinity to fill the screen; with all eight shown the cube takes the screen, the snaps double up every half beat and the whole scene turns, swirls and ripples as one per-pixel map while the plane warps; the cube shatters into a vortex of sparks that wind onto JAISCRIPT, which slams in with a flash, shockwaves and a shake as the fire wall erupts (the fire scene in `fire.jai`), the flames pump in rainbow on the beat with the plane behind them, then the letters cool to neon and the credits roll |

Between scenes, two beats from the bar line, each scene hands over its own way:
DONUT dives through its hole into JULIA (zoom through), JULIA swirls into the
lamps, the wax ripples into the chrome pipes, the pipes iris onto the flock, the
flock melts like Doom into GLOBHUNTER, the hunt zooms out over the canyon, the
canyon ripples into CHROME, the glass shatters into OSSUARY, the web swirls into
the ink, the ink dives into FINALE and FINALE irises back to the start. The
motion transitions read the departing frame through a moving map (a turning
zoom, tiles spinning away, a ring wave, a vortex); the wipes merge two grids per
pixel into the combined palette. A title card with a demoscene handle opens
every scene for one bar, in one of three styles taking turns (a band opening
from the middle, a shutter closing from both sides, a glitch slipping on the
hats), pumping on the kick. A scene can set `pixel_budget`: past it the scene
renders smaller and the reel scales it up, so the heaviest scenes hold their
frame rate on large terminals.

## Measured numbers

The whole reel on the standalone runner (100x40 cells = 100x80 = 8,000 pixels
per frame, seed 20260705, fixed dt 1/30, transitions included, smoke durations
of 3 s per scene) reports its own timing: `jaiscript demoreel.jai -- frames 1700`
prints the render cost per frame (`demo_frame` alone: update, render and
presentation), the cost including the shim's per-byte script hash of every
frame, and the mean frame size (45 KB). Measure it on a quiet Release build.

Per scene, mid-flight (`-- bench 60`, no transitions, same size):

| scene | VM ms | interp ms | KB/frame |
| ----- | ----- | --------- | -------- |
| DONUT | 7.5 | 121.5 | 50 |
| JULIA | 1.5 | 59.0 | 73 |
| LAVALAMP | 0.9 | 22.2 | 33 |
| PIPEDREAM | 1.1 | 10.9 | 56 |
| GLOBHUNTER | 1.8 | 28.9 | 35 |
| CANYONRUN | 1.6 | 32.3 | 90 |
| CHROME | 4.0 | 17.5 | 27 |

The table covers the seven scenes it lists; BOIDS, OSSUARY, MAELSTROM and FINALE are not in it.
Every scene in it holds 30 fps on the VM with room to spare (mean 2.7 ms, worst 5.0 ms); on
the interpreter each stays under 19 ms (mean 14.7 ms).

- Parity: frame streams **byte-identical** across backends over 1,700 frames,
  every one of the eleven scene entries: shim stream hash `367563470` on
  both. The C++ host's `--smoke` hash is FNV-1a 64 over the same frames:
  `7ebf414b506dd9d9` (the host's HUD
  is off in smoke, so its frames are the shim's bytes).
- `--reload-test` re-executes every scene file into the engine. The
  presenter's globals live in `util.jai`, so a reload resets them and the reel
  re-binds them on the next frame; a reload mid-transition cancels the
  transition (the live scene rebuilds), so no scene reads a reset buffer.

### Scoreboard

`ossuary_bench/` keeps the original OSSUARY (the skull-spider on the morphing
gyroid, before it spun its web) as a frozen pair: `skull_shaped.jai`, tuned for
the VM, and `skull_natural.jai`, the same algorithms and the same frames written
the way a user would write them (scene hash `afd81a7d28930a94` from both at
`--pix half --workers 1`), without the spellings chosen to fit the VM's fast
paths (one `sk_world`, the warp's amplitude read left of its sine, ternary folds,
one `sk_crown` holding its flames, the row pushing `sk_trace`'s result). The
ratio of their serial OSSUARY rows, natural over shaped, measures how general
the VM's fast path is: at 1.0 plain code runs as fast as code written for the
VM. Drop either one into a copy of the scenes directory as `skull.jai`:

```
cp -r scenes /tmp/shaped_scenes && cp ossuary_bench/skull_shaped.jai /tmp/shaped_scenes/skull.jai
cp -r scenes /tmp/natural_scenes && cp ossuary_bench/skull_natural.jai /tmp/natural_scenes/skull.jai
jai_demoreel --perf 30 --scene 8 --backend vm --workers 1 --pix half --scenes /tmp/shaped_scenes   # shaped
jai_demoreel --perf 30 --scene 8 --backend vm --workers 1 --pix half --scenes /tmp/natural_scenes  # natural
```

## Console performance

Frames carry two colors per cell. At 100x40 in sextants they average about 53 KB,
from 34 KB for CHROME to 97 KB for CANYONRUN, whose shading changes nearly every
cell; FINALE runs from 7 KB in its slides to 115 KB under the dancing 8-cube.
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
- **Terminal detection.** `COLORTERM=truecolor` (or `24bit`) and Windows
  Terminal (`WT_SESSION`) get 24-bit color with tolerance merging; every other
  terminal (macOS Terminal.app, the Linux console, legacy conhost) gets the
  xterm-256 cube. Conhost also shows a one-line tip on the title card ("Windows
  Terminal renders this reel much faster"). `--truecolor` and `--256color`
  override the choice, and `--ascii` replaces the block glyphs with one density
  character per cell (its brightness the cell's, its color the cell's hue at
  full value) for terminals without the block glyphs; rows carrying text keep
  their characters.
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
2. Open `scenes/lava.jai` in an editor; change the wax colors (the
   `gfx.rainbow_rgb` phase in `build`), or the blob speeds in `update`.
3. Press `r` in the console window.

The engine re-executes every scene file (hot reload with instance migration) and
the current scene rebuilds — the change appears without restarting. `main.jai` is
deliberately never reloaded: it owns the persistent globals.

For three seconds a green band under the HUD names the files whose text changed
and the scene that was rebuilt (`RELOADED  lava.jai  LAVALAMP rebuilt at 4.2 s`).
An edit that does not parse or check loads nothing: the file keeps running its
previous code, every other file still reloads, and a red band shows the first
error with its source line and caret for eight seconds. Fix it and press `r`
again.

## Determinism / parity

`--smoke` runs N frames headless at a fixed dt, FNV-1a-hashing every returned
frame; `--expect H` fails the run unless the stream hash is H. All scene randomness
goes through the host's seeded xorshift `Rng` class (the presentation-only hashes
are integer arithmetic on pixel coordinates), and `parallel_transform` returns
rows in order, so the hash holds at every worker count. The backends' parity runs
on the standalone runner (`--backend=vm` and `--backend=interp` above). Env
helpers: `DEMOREEL_SMOKE_FULLDUR=1` (full interactive scene durations),
`DEMOREEL_DUMP_FRAME=N` (write frame N to a file for
diffing), `DEMOREEL_TRACE=1` (per-frame progress for crash localization).
Deliberately NOT part of the main test suite.

## --precompiled

`engine->jaibite()` parses the concatenated scene source once; `--precompiled`
saves the parsed AST as `demoreel.jaibite` on first boot and loads it afterwards
(`jaibite_load` re-interns symbols per engine, so one file serves every engine).

## Captures

Stripped of color a half-block frame is only `▀`, `█` and spaces, so these are
brightness maps of the pixel grid (one character per terminal cell, the two
pixels averaged; `-- shot I at T dump P` writes the full-color `P.bmp`).

CHROME (`shot 8 at 4` — three chrome spheres over the checkerboard; the `@@`
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

GLOBHUNTER (`shot 6 at 3.1` — the kill counter top left, a Grublin with glowing
red eyes in the crosshair down a cold corridor while the goo of the last one
bursts around it, the pistol at the bottom):

```
::-====--:::::::::::::::::::::::.....::::::::::::::::::::::::::::::::::::::::::::::::::::::--====-::
::@-=##:#*@**:@:::::@:::::*****::....:*****:*****::::::::::::::::::::::::::::::::::::::::----====-::
::@*#-----@.-:@.::::@.::::****::::....@.**@.:.:**.:::::::::::::::::::::::::::::::::::::-==---====-::
::%.**=---%.==%.::::%.::::::::%.::::::%+.:%.:++.:::::::::::::::::::::::::::::::::::::-====---====-::
::*.==*:*++++-*++++:+++++:++++:::::::::+++::+++++::::::::::::::::::::::::::::::::::-======---====-::
::----=---========-::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::-========---=----::
::--------=========::-::::::::::::::::::::::::::::::::::::::::::::::::::::::::-::=========--------::
:::---------=======::---:::::::::::::::::::::::::::::::::::::::::::::..:::::---::=======---------:::
::::::-:-------====::-----::::::::::::::::::::::::::::::::-:::::::::::::::-----::====-------:-::::::
::::::::::--------=::-------::::::--::::::::::::::::::::::::::::::::::::-------::=--------::::::::::
----:::::::::------::-------:-:::::::::::...:::::::::::::::--:::::-:::-:-------::------:::::::::---=
---===---::::::::--::-------:==-:::::::::::::::::::::.*==:::::::::::-==:-------::--::::::::---===--=
---====---=::::::::::-------:====:::::::::=:::::::::::+++:::::.::::====:-------::::::::::=---====--=
---====---=:::==---::::::---:-===:--:::::.......#--.....::...:::--:===-:---::::::---==:::=---====--=
---====---=:::=======---:::::----:--:-#++:::+=::+++:::::::::::-:--:----:::::---=======:::=---====--=
---====---=:::==========:=---:::-:--:-===::::::::::::::::::--:-:--:-:::---=:==========:::=---====--=
---====---=:::==========:======:--:::::-==+-----=-::::::-===-:::::--:======:==========:::=---====--=
----------=:::==========:---===:=----:-:--+:-@....::::.-=+--:-:----=:===---:==========:::=----------
-----------:::==========:---===:=----:-===-=:#=:--%-:.::=-==--:----=:===---:==========:::-----------
-----------:::==========:------:=----:--=+-=-:+=+-#-+-:-=-+=--:----=:------:==========:::-----------
:::::::::::::::::::::::::::::::::::::::-===:::###==-***#===--:::::::::::::::::::::::::::::::::::::::
::::::::::::::::::::::::----:--==:--:-:==++:=:-*--%---:++++=-:-:--:==--:----::::::::::::::::::::::::
::::::::::------===::=======:====:--:-:=---=::::-:*:-:::=-+--:-:--:====:=======::===------::::::::::
::-====---=========::=======:----:--:::-=+-++=-++++**-++=-+=::::--:----:=======::=========---====-::
::-====---========-::=======:----:::::-----==-=====*==-====---:::::----:=======::-========---====-::
::-====---=--------::=======:-:::----:---#=#=#++#++--==-===:--:----:::-:=======::--------=---====-::
::-----------------::====--:::-:=----++:-+++++=====-----------:----=:-:::--====::-----------------::
::-----------------::--::::-===:=---:==--:::::::::::::--::::::::---=:===-::::--::-----------------::
::-----------------::::::---=--:------:--:---------------::---------:--=---::::::-----------------::
::-------------::::::-==:------:--=====-------==::=====---------==-::------:==-::::::-------------::
::----------::::::-=====:----------------------**##--------------==-::-----:=====-::::::----------::
::-----::::::::-========:------------::--------**++--------:::-------------:========-::::::::-----::
:::-::::::::::==========:-------===-::---------**++-----===-::-==========--:==========::::::::::-:::
::::::::::=:::========---------===-::----------**++------===:::-===========---========:::=::::::::::
::::::----=:::=====---=-------===-:::--------**##++++----====:::=======----===---=====:::=----::::::
:::-===---=:::===------------:::::::---------**++++++--------::::------------------===:::=---===-:::
:--====----:::=----::::--------------------**##++++++++-----------------------:::----=:::----====---
---====----:::---:---------------------%%--**++++++++++--%%-----------------===-=::---:::----====--=
---=-------:::-:---------------------%%%%--++++++++++++--%%%%---------------------:::-:::-------=--=
-----------:::-------------:::::---%%%%%%%%--++++++++--%%%%%%%%--------------------::::::----------=
```

DONUT (`shot 1 at 4` — the product shot: the iced torus hopping over the walnut table, its reflection below):

```
:::::::::::::::::::::::::--------------------------------------------------:::::::::::::::::::::::::
::::::::::::::------------------------------------------------------------------------::::::::::::::
------------------------------------------------------------=---------------------------------------
-------------------==---------------------=====----------+****+-------------------------------------
----------------=*******+----------------++++++*=-------*******=------------------------------------
----------------*****####+---------------+++++++*-------+******-------------------------------------
----------------##########---------------++++++++--------+++++--------------------------------------
----------------+#########----------------=+++++----------------------------------------------------
------------------+++++++------------------------#-#------------------------------------------------
----------------------------------------=+*-**#:####+#**#+**----------------------------------------
-----------------------------------===**#**##**%###*#******%*=--=-----------------------------------
-----------------------------------=+###%#+##**###%**#*#*+**#***=*----------------------------------
----------------------------------***#%%***+*+=+******=#####******#*--------------------------------
--------------------------------*###%***++-==+==++++**+**#%%#*+**+*+*=------------------------------
::::::::::::...:.:..........:.*:**%%%#*=++=+======++++***+*#*%#*******:........:........::::::::::::
...............:.:...........:+*#%#***==++===-----===++****%#*@#+***+=+-.:.:.:.:...........:...:....
....:.............:....:..:.:+##%%#*==+++==---::..:.:-+******%@%***+**+=.......:.........:.:..:.....
..:.....:..:..:....::.::.:::-**#%@#*+*++==--:::::::::.::*###+#%%*#****=*=...:::....:.:..:..:..:..:..
::.:..:::.:::::::::::.:::::::+++%@#*-*+++===::::::::::::+###*%#%++*=**++=:::.::::::.::::.::.::.::...
::::::::::::::::::::::::::::%%##%#%*+++++++=:::::::-::::%##*#%@#***+*=+++:::::::::::::::::::::::::::
.:::::::::::::::::::::::::::%####@%+******++=----------*####%*%*+****==++:::::::::::::::::::::::::::
::::::::::::::::::-:----:---***#+%%*#=*******++-----**###+#=@%******++=--::---:::--:::-:::::::::::::
:::::::-:-:-:---:----:------**+*#+#%%%*=+++****##########%%%%+***+++++-=--------------:---::::::::::
:::---:-:--------------------*#++*%##%*%******+*##+####*###***+*=+++===----------------------:-:--::
-----------------------====-==*+****+*++#####*#####%*##%#*+***+++=====-::::-::----------------------
---------------------====-======%*#+*+***+#**#####%#*+****+=*+=+=====----:::::::::-=----------------
-------------====-============++=*%+****+**+***:+**-*****+++++=-====------::::-:::::-=--------------
----------=============+====++++++++=*=*+*+*****++*+*++=-+=++=====---:-----::::-::::--===-----------
-------==============+++++++++++++++++**++++++=++=++++=+======--------:-----::::------======---=----
----===============+++++=++++++++++******++++===+-=+==-=====-----------:-----::----===========---==-
-================+++++++++++++++++********===-========--------------------------================---=
================+++++=+++++++++++**********+===--:-------------------------==++++=================--
==============+++++=++++++++++++*+**++********++++===----=-==-----====++++++++++++==================
===============++==+++++++++++++++*+++****+======*****++++++*++++++++++++++++++++++=================
==================++++++++++++++++++==-:::::::::::::::::===***++++++++++++++++++++++================
=================+++++++++++++++++-:::::::-::::::::::::::::::-=+++++++++++++++++++++================
=================++++++++++++++++:=----:---::-:-::-::::-:::::::::=++++++++++++++++++================
================++++++==++++++++:-----------------::::-:::::::::---=++++++++++++=++++===============
==============+++++++==+++++++++=:-==-=-----------------::::::::::--=+=+++++++++==++++==============
=============++++=====++++++++++=====-=-=-==----------------::-::----+++++++++=====+++==============
```

FINALE afterglow (`shot 11 at 84.8 ascii 1`: the fire is out, the word glows in neon and the credits roll):

```


                                                                                   .
      ********    +++++   ********    ######    %%%%%%  #######   ********  +++++++   ********
           **   ++     +      *     ##        %%        ##     #      *     ++     +      *
           **   ++     +      *     ##        %%        ##     #      *     ++     +      *
           **   ++++++++      *       #####   %%        #######       *     +++++++       *
      **   **   ++     +      *            #  %%        ##  #         *     ++            *
      **   **   ++     +      *            #  %%        ##   ##       *     ++            *
        ***     ++     +  ********  #######     %%%%%%  ##     #  ********  ++            *






                                       THE JAISCRIPT DEMOREEL

                                  eleven scenes . five transitions
                              every pixel computed by the jaiscript vm

                                              ONE  scripting
                                              TWO  serialization
                                            THREE  reflection
                                             FOUR  hot reload
                                             FIVE  parallel
                                              SIX  signals
                                            SEVEN  debugging
                                            EIGHT  embedding
```

## Idioms

- **`var&` for hot arrays and the frame grid.** Assignment copies arrays and objects, so
  `var alias = field_array;` is a copy and writes through it never reach the field.
  `var& alias = field_array;` binds a reference; the scenes bind their hot arrays this way at
  the top of `update` and `render`, and a scene paints through `render(var& grid)`.
- **Compound element stores.** `arr[i] += v` resolves the element once, where a read followed
  by a write resolves it twice; MAELSTROM's jets, whirlpool and pressure solve write the flow
  that way.
- **Two resolutions.** MAELSTROM's pressure solve is most of a fluid frame, so the flow runs on
  a grid of half the pixel resolution (a quarter of the cells) and only the ink, which carries
  the detail, moves per pixel through the bilinearly upsampled flow.
- **A scene's `replay`.** A jump (`--perf`, `--capture`, a shot) replays a scene's updates so it
  arrives mid-flight; three seconds rebuild most scenes, while MAELSTROM's water is its whole
  history, so it sets `replay` to its full length and a shot at second 13 shows second 13.
