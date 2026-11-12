# The demoreel's heaviest scenes: JaiScript against Lua, Squirrel, ChaiScript and Python

Three scenes of the JaiScript demoreel, each the heaviest of its kind, ported whole to Lua 5.4, Squirrel 3.2,
ChaiScript 6.1 and Python 3, each port printing the JaiScript VM's scene hash byte for byte. `run_comparison.py`
checks the hashes and times every scene in every runtime; `../README.md` is the porting contract.

## The scenes

Every scene of the reel on the JaiScript VM, serial, `jai_demoreel --perf 60 --perf-warmup 30 --scene I --workers 1
--w 100 --h 40` pinned to one P core, median of three interleaved rounds (MSVC x64 Release with LTCG, Core Ultra 9
285HX):

| `--scene` | scene | what it is | VM ms/frame |
|---|---|---|---|
| 9 | OSSUARY | per-pixel raymarch of a skull-spider, its web and a moth | 120.8 |
| 3 | LAVALAMP | raytraced lava-lamp forest, a 2D DDA over the lamp grid | 39.2 |
| 8 | CHROME | ray tracer: chrome and glass spheres over a floor | 36.7 |
| 10 | MAELSTROM | stable-fluids simulation, ink on a finer grid, a lit 3D view | 25.2 |
| 0 | EIGHT | the opening music video: hypercube, hyperbolic tiling, fire, slides | 14.3 |
| 1 | DONUT | a torus met by a closed-form quartic, reflected in a table | 13.5 |
| 4 | PIPEDREAM | pipes grown in a voxel volume, drawn as capsule impostors | 12.4 |
| 5 | BOIDS | a 1,500-bird murmuration over a uniform grid | 10.9 |
| 6 | GLOBHUNTER | textured DDA grid raycaster with floor and ceiling casting | 6.8 |
| 2 | JULIA | the Julia set, a distance estimate per pixel | 5.4 |
| 7 | CANYONRUN | VoxelSpace height-field flight | 3.5 |
| 11 | FINALE | fireworks over a skyline, credits | 2.1 |

The set is the heaviest scene of each kind of work:

- **OSSUARY**, float ray work: the heaviest scene by three times. LAVALAMP and CHROME, next by cost, are ray tracers
  too, so they would measure the same kind of work again.
- **MAELSTROM**, simulation: the heaviest simulation (BOIDS is the other), array-heavy stencil passes over float grids.
- **GLOBHUNTER**, a grid renderer: the heaviest grid or height-field renderer (CANYONRUN is the other), integer DDA
  and texture lookups through class fields, and the only one of the three with no parallel loop.

JULIA is ported too: each harness (presenter, palettes, score, reel) was checked against the VM on it before the
scenes were ported. It is hash-checked, not timed.

## Protocol

- The workload is the shared host's `--perf`: boot the reel (seed 20260705, 100x40 cells, 150x120 sextant pixels),
  jump to the scene at t = 3.0 (replaying three seconds of updates), then time 60 frames at dt 1/30: update, render,
  presentation and the frame string, as the scene runs from t = 3.0 to 5.0. Each frame is hashed (FNV-1a 64); the
  Python driver hashes outside its timed window, since pure-Python hashing would add 2 to 3 ms a frame.
- **Full protocol** (JaiScript VM serial and parallel, Lua, Squirrel, Python): 60 timed frames after 30 warm-up frames
  in the same process, which then reboots the reel and jumps again, so caches and allocator state are warm and the
  measured frames are the unwarmed reference's. Five rounds; each round runs every scene in every runtime, one process
  at a time, the runtime order rotating between rounds, four seconds between processes and twenty between rounds.
  The median of the five is reported, with the range.
- **ChaiScript** runs a frame of OSSUARY in about six seconds, too slow for the full protocol. It times three slices of
  four frames spread across the same 60-frame window (frames 0-3, 20-23 and 40-43, from t = 3.0, 3.67 and 4.33), each
  after one warm-up frame; three rounds, the median of each slice, and the mean of the three medians as its ms/frame
  (12 sampled frames). The window total is projected from that mean (x 60).
- The VM serial row is `--workers 1`; the parallel row the VM's default worker count, the eight P cores. Serial
  processes are pinned to logical CPU 10 (`start /affinity 400`), the parallel row to the eight P cores (`C03C03`).
- Every process holds the machine's timing lock (JaiScript's `tools/ci/timing_lock.sh`) while it runs, so no build or
  other benchmark overlaps it. Builds: MSVC x64 Release, `/O2`, `/GL` and `/LTCG` for JaiScript and for every port's
  runtime and executable; the static CRT; Lua 5.4.8 and sol2 3.3.0 pinned by `gloom/ports/prepare_comparison.py`;
  Squirrel 3.2 with `_SQ64` and `SQUSEDOUBLE`; ChaiScript 6.1.0 with `CHAISCRIPT_NO_THREADS`; CPython 3.11.0
  (python.org build).
- Conformance: the VM's hash for each workload (the 60-frame window and each ChaiScript slice) is the reference, and
  every gate and timed run must print it, or the session fails. The VM's scene hash is the same serial, parallel, and
  with or without warm-up. The ports also matched at `--perf 1` and `--perf 10` while being written, and at later
  scene times (`--perf-at` through each scene's story) where their porters checked them.

## Results

Measured 2026-11-12 (`run_comparison.py`, evidence under `out/demoreel/run1`). ms/frame, median of five (range):

| scene | shape | Jai VM | Jai VM, 8 workers | Lua 5.4.8 | Squirrel 3.2 | ChaiScript 6.1 (12 sampled frames) | Python 3.11 |
|---|---|---|---|---|---|---|---|
| OSSUARY | per-pixel float raymarch: small distance functions over float arguments and global floats, called tens of thousands of times a frame | 117.19 (116.72-117.71) | 18.47 (18.26-18.88) | 45.73 (45.63-46.30) | 97.57 (96.64-98.15) | 5,964 (slices 5,670 / 6,123 / 6,099; runs 5,667-6,147) | 143.00 (141.20-146.29) |
| MAELSTROM | fluid grid: array-heavy stencil passes (ripples, advection, pressure) over float arrays, then a ray per column | 24.51 (24.37-24.57) | 9.52 (9.44-9.82) | 7.08 (7.06-7.16) | 13.68 (13.50-13.82) | 768 (slices 761 / 766 / 777; runs 758-837) | 23.79 (23.27-23.96) |
| GLOBHUNTER | grid raycaster: integer DDA and per-pixel floor and ceiling texture lookups through class fields; no parallel loop | 6.53 (6.49-6.56) | 6.51 (6.50-6.56) | 2.20 (2.19-2.22) | 4.40 (4.37-4.42) | 152 (slices 140 / 161 / 155; runs 139-161) | 5.87 (5.83-5.96) |

Every run printed the VM's hash: `511aa753e2412a15` (OSSUARY), `ad264527f90a5529` (MAELSTROM) and `4a7f519c6d5d5dc3`
(GLOBHUNTER) for the 60-frame window, and the VM's hash of each ChaiScript slice. ChaiScript's 60-frame window,
projected from its sampled mean: OSSUARY 358 s, MAELSTROM 46 s, GLOBHUNTER 9.1 s. GLOBHUNTER has no parallel loop, so
its eight-worker row is its serial row again.

## Ratios

Time over Lua 5.4's (lower is faster for the left-hand runtime):

| scene | VM 8 workers / Lua | VM / Lua | Squirrel / Lua | ChaiScript / Lua | Python / Lua | VM / Python |
|---|---|---|---|---|---|---|
| OSSUARY | 0.40 | 2.56 | 2.13 | 130 | 3.13 | 0.82 |
| MAELSTROM | 1.34 | 3.46 | 1.93 | 108 | 3.36 | 1.03 |
| GLOBHUNTER | (no parallel loop) | 2.97 | 2.00 | 69 | 2.67 | 1.11 |

On one core the VM takes 2.6 to 3.5 times Lua's time on these scenes, and runs level with CPython 3.11. Squirrel is
about twice Lua everywhere. The VM's eight workers bring OSSUARY (its rows are independent) to 6.3 times its serial
speed and 0.40 of Lua's time; MAELSTROM's parallel stages are part of its frame, so it gains 2.6 times. The old reel's
OSSUARY row (VM 0.76 of Lua's time) measured an earlier scene, a skull on a gyroid; this OSSUARY is call-heavy (the
Python port's profile counts about 37,000 calls of `sk_spider` a frame, each calling smaller distance functions), and
calls are the VM's slowest ground, as Richards and DeltaBlue show.

## Fairness notes

- Each port is a whole-file transliteration of the scene: same algorithm, same float expressions in the same order,
  the same libm calls, the same RNG draws. Nothing is cached that the original recomputes, and the original's waste is
  kept (fresh arrays per row, repeated cue queries, per-call tables).
- Each port is written the way its language is written for speed in ordinary style: Lua keeps a scene's state in file
  locals (upvalues) and aliases hot globals and math functions into locals; Squirrel hoists globals and natives into
  locals; ChaiScript reads globals and fields into locals and aliases containers with `auto&`; Python aliases globals
  and `math` functions into locals. The presenter's rows are built in a buffer and joined in Lua, Squirrel and Python
  (each language's string-building idiom); JaiScript and ChaiScript append to a string.
- `parallel_transform` bodies run as serial loops over the same per-row functions in the ports; the VM's serial row
  runs the scene as written, its `parallel_transform` calls on one worker.
- Where JaiScript copies a value (MAELSTROM's `u = u1`, `MS_FU = u`) the ports copy; where it passes `float&` or
  `int&` (OSSUARY's span helpers) Lua and Python return the values and Squirrel passes a two-element array.
- ChaiScript misparses some decimal literals by an ulp; its scripts spell those as exact quotients that fold at parse
  time (`chai/check_literals.py`), which costs nothing at run time.

## Running it

From a VsDevCmd x64 prompt at the comparison project's root, fetch the runtimes into `third_party/` and configure with
the ports on (see `../README.md` for the roots):

```
python gloom/ports/prepare_comparison.py
cmake -S . -B out/release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DDEMOREEL_PORT_LUA=ON -DDEMOREEL_PORT_SQUIRREL=ON -DDEMOREEL_PORT_CHAI=ON
python demoreel/ports/run_comparison.py --build --output out/demoreel/<fresh folder>
```

`--runtimes` and `--scenes` narrow the set, `--conformance-only` checks the hashes without timing, `--rounds` (at least
five) and `--chai-rounds` (at least three) lengthen the session. The folder receives `metadata.json`, `runs.json`
(every run: command, log, hash verdict, ms/frame), `summary.json` and `table.md`, and every process log. The unit tests
are `python -m unittest discover -s demoreel/ports/tests`.
