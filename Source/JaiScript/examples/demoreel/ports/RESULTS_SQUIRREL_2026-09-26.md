# demoreel: JaiScript vs Squirrel 3.2 (2026-09-26)

The Squirrel port (`ports/squirrel/*.nut`, `demoreel_adapter_squirrel.cpp` -> `demoreel_squirrel`)
on the shared demoreel host, against `jai_demoreel` at `02e649a7` + the port commits (the
JaiScript scenes are unchanged).

## Parity

**Full frame-stream parity.** `demoreel_squirrel --smoke` = `1a0337f81a618c10`, the JaiScript
stream hash on both backends (1700 frames, 100x40, seed 20260705, every scene, every
transition kind, every title card). Every per-scene `--perf 30` hash below is identical across
all five configurations. During the port each scene was also checked at perf-at 0.3 (title
card), 3.0 and 9.0 (FINALE and the cloth also at 13.0/14.5): all identical.

Nothing needed an approximation. What it took:

- Squirrel built with `_SQ64` (64-bit ints, self-selected on x64) and `SQUSEDOUBLE` (PUBLIC on
  the runtime target; the adapter `static_assert`s both).
- JaiScript's truncating typed-int stores and `itrunc` -> `.tointeger()` (C truncation);
  `ifloor` -> `floor(x).tointeger()`. Squirrel's int `/`, `%` truncate like C, `>>` is
  arithmetic. `.tofloat()` where a JaiScript `float` slot can receive an int.
- `log2`: sqstdmath has none; the adapter binds `std::log2` (same CRT call as JaiScript's).
- **POWDER / `sand.jai`**: every element's `step(..., var r, ...)` takes the scene `Rng` BY
  VALUE, which copies the bound C++ object, so element draws never advance the scene rng (each
  step between two scene draws sees the same next value). The port reproduces it with an
  adapter-local `Rng.copy()` passed per call (Squirrel's `clone` does not copy class user
  data). The host contract is unchanged. This looks unintended in the reference (`var& r`
  was probably meant); changing it changes the stream hash, and the port then passes `rng`.
- `parallel_transform` sections (the presenter's optional path, donut/julia/lava/sponge/chrome/
  skull rows) run as serial loops over the same row functions.
- Squirrel idioms that are not algorithmic changes: containers are references (JaiScript's
  `var&` parameters become plain parameters; the lava blob arrays are shared instead of deep
  copied, read-only), rows are built into a parts array joined with the GLOOM port's pairwise
  `join_arr` (no string builder / array join in 3.2), and a few hot row functions cache
  globals and math functions in locals (`local sqrt = ::sqrt`), the usual Squirrel/Lua idiom.
  `sk_project`'s `float&` out-parameters return an array instead.

## Machine and protocol

Intel Core Ultra 9 285HX (24 logical cores), Windows 11, MSVC 19.51 (VS 18), Release `/O2`,
static CRT, Ninja, `x64-Release BENCHMARKS` build dir. Every timed run held the shared timing
lock; medians of 3 runs. 100x40 terminal = 100x80 pixels.

- `--perf 30`: each scene on a freshly booted reel, jumped to t=3.0 (stateful scenes replay
  3 s), then 30 timed frames of update + render + presentation (no transitions).
- serial = `--workers 1` (every `parallel_transform` runs on the calling thread); parallel =
  the engine default. Squirrel has no parallel sections: it is always serial.

## Per-scene ms/frame (`--perf 30`, median of 3)

| # | scene | jai interp serial | jai interp parallel | jai vm serial | jai vm parallel | Squirrel | Squirrel ÷ vm serial | Squirrel ÷ vm parallel |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | PLASMA | 11.297 | 11.235 | 0.886 | 0.912 | 2.721 | 3.07x | 2.98x |
| 2 | STARFIELD | 8.137 | 8.256 | 1.406 | 1.403 | 1.473 | 1.05x | 1.05x |
| 3 | TUNNEL | 8.930 | 8.941 | 0.649 | 0.652 | 1.732 | 2.67x | 2.66x |
| 4 | DONUT | 61.389 | 15.140 | 20.163 | 4.610 | 5.425 | 0.27x | 1.18x |
| 5 | JULIA | 83.202 | 17.855 | 9.729 | 5.487 | 6.991 | 0.72x | 1.27x |
| 6 | LAVALAMP | 83.746 | 18.309 | 10.275 | 2.424 | 6.059 | 0.59x | 2.50x |
| 7 | PIPEDREAM | 10.435 | 9.126 | 1.579 | 1.618 | 1.729 | 1.09x | 1.07x |
| 8 | POWDER | 29.858 | 24.264 | 4.428 | 4.465 | 2.967 | 0.67x | 0.66x |
| 9 | BOIDS | 16.190 | 14.105 | 3.807 | 3.816 | 1.809 | 0.48x | 0.47x |
| 10 | KINSTEIN 3D | 14.674 | 12.827 | 1.389 | 1.401 | 1.961 | 1.41x | 1.40x |
| 11 | CANYONRUN | 12.298 | 12.027 | 0.895 | 0.861 | 1.439 | 1.61x | 1.67x |
| 12 | SPONGEWORKS | 77.537 | 21.130 | 13.051 | 5.833 | 5.797 | 0.44x | 0.99x |
| 13 | BANNERFALL | 20.782 | 18.145 | 2.988 | 2.983 | 1.935 | 0.65x | 0.65x |
| 14 | CHROME | 89.045 | 17.772 | 18.314 | 3.946 | 8.758 | 0.48x | 2.22x |
| 15 | FIRE | 18.033 | 14.914 | 2.492 | 2.494 | 2.253 | 0.90x | 0.90x |
| 16 | OSSUARY | 7406.430 | 1089.500 | 1771.810 | 339.243 | 512.739 | 0.29x | 1.51x |
| 17 | FINALE | 26.890 | 23.548 | 2.998 | 2.992 | 3.442 | 1.15x | 1.15x |
| | **mean** | **469.346** | **78.653** | **109.815** | **22.655** | **33.484** | 0.30x | 1.48x |

The ratio columns are Squirrel's time over JaiScript's: below 1.00x Squirrel is faster, above it JaiScript is.
Each scene's hash is identical across all five configurations.

Scenes without `parallel_transform` show no serial/parallel difference (noise only).

## Whole reel (`--smoke`, 1700 frames, median of 3)

| configuration | runs (total ms) | median total | ms/frame | stream hash |
|---|---|---:|---:|---|
| jai interp, parallel sections | 150672 / 158063 / 146424 | 150.7 s | 88.63 | 1a0337f81a618c10 |
| jai vm, serial (`--workers 1`) | 200782 / 230778 / 207181 | 207.2 s | 121.87 | 1a0337f81a618c10 |
| jai vm, parallel sections | 44195 / 42318 / 41894 | 42.3 s | 24.89 | 1a0337f81a618c10 |
| **Squirrel 3.2** (serial) | 62132 / 61208 / 61503 | **61.5 s** | **36.18** | 1a0337f81a618c10 |

Boot (load + parse): Squirrel 5 ms (`nut`), JaiScript 25-28 ms (`prse`, the jaibite path).
The interpreter-serial smoke was not run (its perf mean alone is 469 ms/frame). OSSUARY is about
90 of the 1700 frames and dominates every column's total (roughly 70 %, from the perf rows).

## Reading it

- **Serial against serial (the like-for-like language comparison)**: Squirrel runs the whole reel
  **3.37x faster** than the JaiScript vm (61.5 s vs 207.2 s), and the per-scene mean 3.28x.
  That total is OSSUARY's (Squirrel 3.46x faster there). Scene by scene the split is 10 : 7:
  - JaiScript vm faster (7): PLASMA 3.07x, TUNNEL 2.67x, CANYONRUN 1.61x, KINSTEIN 1.41x,
    FINALE 1.15x, PIPEDREAM 1.09x, STARFIELD 1.05x: integer table lookups over typed arrays and
    the typed-loop cell blocks.
  - Squirrel faster (10): DONUT 3.7x, OSSUARY 3.5x, SPONGEWORKS 2.3x, BOIDS 2.1x, CHROME 2.1x,
    LAVALAMP 1.7x, BANNERFALL 1.5x, POWDER 1.5x, JULIA 1.4x, FIRE 1.1x: float-heavy per-pixel
    ray/SDF math in free functions called per row, object-field physics, and the RNG-driven
    automata.
- **With its parallel sections** (24 logical cores) the JaiScript vm runs the reel **1.45x faster
  than serial Squirrel** (42.3 s vs 61.5 s; per-scene mean 1.48x). Squirrel has no in-VM
  parallelism to answer with; per scene it still wins BOIDS, BANNERFALL, POWDER, FIRE and
  SPONGEWORKS (0.99x) outright.
- **Parallel scaling in JaiScript**: vm serial -> parallel is 5.2x on OSSUARY, 4.4x DONUT,
  4.6x CHROME, 4.2x LAVALAMP, 2.2x SPONGEWORKS, 1.8x JULIA; the interpreter scales 3.7-6.8x on the
  same scenes.
- **vm over interpreter** (serial): 10-14x on the integer scenes (PLASMA 12.7x, TUNNEL 13.8x,
  CANYONRUN 13.7x) but only 3-9x on the parallel-row scenes (DONUT 3.0x, OSSUARY 4.2x, CHROME
  4.9x, SPONGEWORKS 5.9x, LAVALAMP 8.2x, JULIA 8.6x). Squirrel wins all six serially. Judgment,
  not measured: the float-heavy free-function row bodies (many small scalar helper calls per
  pixel: `sk_field`, `ch_trace`, the SDFs) look like the vm's weakest shape and the place to
  profile next.

## JaiScript / host issues found

1. **Intermittent segfault in `jai_demoreel` on the vm backend (not the port).** Repro, Release
   `x64-Release BENCHMARKS` at `02e649a7` (the port commits touch no JaiScript or jai-adapter code):
   `for i in 1..50: jai_demoreel.exe --smoke --backend vm --frames 3` -> exit 139 in 3/50 (other
   batches 8/60 default workers, 1/60 and 1/50 with `--workers 1` / `--workers 2`). The
   interpreter gives 0/50, and `--frames 0` (load + boot only) 0/50. With `DEMOREEL_TRACE=1`
   (a flushed stderr line per frame) it is 0/40, so it is timing-sensitive. The process dies
   before stdout flushes (no output at all), so the crash is in the first frames (PLASMA's
   title card, serial, no parallel section) or at teardown. It also hit 2 of 5 full
   `--smoke --backend vm --workers 1` runs within the first second. Not localized further:
   no debugger (cdb) or pdb on this box.
2. **`sand.jai` passes the scene `Rng` by value** (`step(..., var r, ...)`), so element draws
   read a copy and never advance the scene rng. That is legal C++ copy semantics, but it looks
   unintended (see Parity). The Squirrel port mirrors it with the adapter-local `Rng.copy()`.
3. **Host contract: unchanged.** The Squirrel adapter adds only adapter-local surface: the
   `Rng.copy()` method, a `log2` native (sqstdmath lacks it), and the `--only I` porting flag
   (other scenes build as named empty stubs, for per-scene bisection).
