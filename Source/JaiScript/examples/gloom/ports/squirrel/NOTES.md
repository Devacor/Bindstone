# Squirrel port notes

Filled-in PORTING.md section-7 rubric. (Lives here because REFERENCE.md is
frozen while three ports land in parallel — fold into the comparison doc when
the freeze lifts.)

Runtime: in-repo Squirrel **3.2 stable** (`Source/JaiScript/squirrel`), built
as `gloom_squirrel_runtime`. `_SQ64` self-selects on x64; **`SQUSEDOUBLE` does
not** — it was injected at configure time (`CXXFLAGS=/DSQUSEDOUBLE`) and the
adapter carries `static_assert(sizeof(SQFloat) == 8)` so a misconfigured build
cannot silently produce float32 sim math. The CMake option block should gain
`target_compile_definitions(... PUBLIC SQUSEDOUBLE)` on the runtime when the
freeze lifts (CMake was frozen for this pass; nothing else in shared files
needed touching).

## Conformance (all green)

| `--smoke --ticks N` (seed 666, quad) | STATE_HASH | matches |
|---|---|---|
| 300 | 3580805725 | YES |
| 2000 | 319812559 | YES |
| 3000 | 4080154357 | YES |
| 16000 | 3497451110 | YES |

Cross-checks: seeds 7/99/4242 @3000 → 1696980843 / 1855347375 / 2848371116
(all match); `--god --seed 5` @12000 → 576425398 (match). Sim hash identical
across `--pix half/quad/sext`. **Frame-stream hashes also match the JaiScript
reference byte-for-byte** in all three pix modes (quad `7fecc09815bb64e0`,
half `7280e458b8bc45a2`, sext `63114227e60b229e`) — full renderer parity, and
the 16000-tick outcome matches the spec (E1M1 cleared 7/7 kills + 1/1 secret,
zero deaths, deep in E1M2). There was never a hash divergence to bisect: the
first run that compiled produced the correct 300-tick hash.

## Binding experience

Raw C API, stack discipline, no binder layer. The adapter is **404 lines**
(vs 140 for JaiScript's `dynamic_binder` version) and roughly half of it is
bookkeeping a binder would hide: push root / push name / `sq_newclosure` /
`sq_setparamscheck` / `sq_newslot` per function, explicit stack pops after
every call, and an error path (`sq_getlasterror` + `sq_tostring`) you must
write yourself. Nothing *fought* — the API is small, orthogonal, and
documented by its own header — but everything is manual and every mistake is
a silent stack imbalance rather than a compile error. Two genuinely nice
pieces: `sq_setclassudsize` embeds the shared `gloom_rng` POD inside the
class instance (placement-new in the constructor closure; no heap, no release
hook), and `sq_setforeignptr` gives natives their session without any global
state. One real gotcha: `sqstd_seterrorhandlers` silently replaces a
previously installed compiler error handler — install yours after it.
Squirrel has no module system, so the adapter also supplies an `include()`
native (13 lines) to mirror the reference's per-file import structure.
Per-frame call overhead (root lookup + push/call/pop) is unmeasurable next to
the frame itself.

## Porting friction

- **The brains ported as brains.** Squirrel generators are the reference's
  coroutine methods almost token-for-token: generator *methods* mint handles
  stored in instance fields, `resume` = one tick, pain-interruption = null the
  field, `getstatus() == "dead"` = re-mint. The autopilot is the same shape.
  This is the headline: Squirrel CAN express the reference architecture
  (ChaiScript cannot), and that section of the port took no design work at
  all. Limitation that didn't bite: Squirrel generators cannot yield through
  a nested call — GLOOM's brains only yield at their own top level.
- **Reference semantics simplified everything JaiScript had to fight.**
  Registries of instances, `G.items[i][3] = 1` through an alias, chained
  mutation — all just work; the whole `auto&`/deep-copy vigilance tax from
  the JaiScript notes has no Squirrel equivalent. The one inversion: where
  JaiScript's `pix = G.bg` deep-copies by default, Squirrel needs an explicit
  `clone G.bg` — one call site.
- **Typed truncation had to become explicit.** JaiScript's `int x = expr;`
  truncation is `(expr).tointeger()` here (C-cast semantics match exactly,
  including toward-zero on negatives). ~40 call sites, all mechanical; int/int
  division truncating like C meant the damage/geometry math moved unchanged.
- **Reserved words bit twice**: `base` (a keyword referencing the parent
  class) collided with two reference variable names, and arrays have no
  `.clone()` *method* — `clone` is a prefix operator. Both were one-line
  fixes; these were the ONLY two compile errors in ~3450 ported lines.
- **No string builder, no join.** Squirrel 3.2 strings are immutable, concat
  copies both sides, and there is no array-join. The row builder uses a
  10-line pairwise-merge `join_arr` (O(bytes·log n)); with it, row building
  is merely the biggest slice (see ablation) instead of a wall.
- **No string.repeat, `s[i]` yields char codes** (int), so map parsing
  compares `'#'` literals — arguably nicer than the reference's 1-char
  substrings.
- **Globals want `::`**. Bare identifiers resolve through `this`, and inside
  a method `this` is the instance, not the root table — so every global
  access in shared functions is `::`-qualified. Mechanical, slightly noisy,
  and a real bug source if forgotten (it fails at *runtime*, at the call
  site, as "the index 'X' does not exist").
- **Class-body defaults are shared** across instances for reference types —
  every array/table field is initialized in the constructor instead. Known
  idiom, easy to respect, silent aliasing if you don't.
- **The bit-packed map snapshot was deleted, not ported**: REFERENCE 4.2
  allows serial ports to read the tile array directly; TILE_TO_KIND is
  applied at ray time. int64 bit math (the STATE_HASH mix, RNG passthrough)
  worked unchanged thanks to `_SQ64`.

## LOC

Scripts **3454** total (reference: ~3560 .jai) + **404** adapter:

| file | LOC | subsystem |
|---|---|---|
| render.nut | 682 | raycast view (inlined DDA), strips, billboards, rows |
| defs.nut | 616 | data + all pixel art |
| game.nut | 523 | Game class, tick, autopilot generator, hash |
| enemies.nut | 331 | Enemy class + 4 generator brains |
| maps.nut | 305 | maps + parser + validator |
| hud.nut | 245 | HUD + full-screen states |
| combat.nut | 227 | LOS/hitscan/projectiles/damage |
| sim.nut | 152 | movement/use/pickups |
| particles.nut | 120 | pool + burst kit |
| pure.nut | 92 | serial ray + particle step |
| util/main/state | 161 | glue + join_arr/rep (stdlib gaps) |

## Time-to-first-running

Single working session: skeleton (adapter compiled + bound) ~1h; full script
translation ~2.5h; **first clean smoke 300 ~15 minutes after the first run**
(two reserved-word compile errors were the only failures — the first run that
compiled matched the checkpoint hash); full 16000 conformance + frame parity
+ all cross-seeds passed on the first attempt with no further edits.
Effectively zero debugging time — credit shared between Squirrel's C-like
semantics (int division, C truncation, IEEE doubles, left-to-right
evaluation) and the reference spec's precision.

## Debugging story

Nearly untested, because nothing diverged. What was exercised: compile errors
report file:line:column with a clear message ("expected 'IDENTIFIER'") once
the compiler error handler is wired — but the *default* aux handler routes
diagnostics through the print/error callbacks, and until the adapter captured
those, a failure surfaced as a bare "compile failed: <file>". Runtime errors
(provoked deliberately) give typed messages ("the index 'foo' does not
exist") plus a real call stack with function names, file:line, and local
variable values through `sqstd_seterrorhandlers` — better than expected, and
strictly better than ChaiScript's reputation. The hash-divergence bisect
workflow was never needed; the sim-only ablation (renderer early-return)
reproduced identical STATE_HASHes, which doubles as proof the renderer
touches no sim state.

## Perf (section 6 protocol: Release /O2, /MT, quiet machine, min of 5)

Headless `--smoke --ticks 300`, ms/tick (seed 666):

| pixels | Squirrel 3.2 | jai VM | jai interp |
|---|---|---|---|
| half (100x68) | 2.61 | 22.2 | 27.4 |
| **quad (200x68, default)** | **2.90** | **31.3** | 39.5 |
| sext (200x102) | 3.67 | 42.3 | 51.7 |

(jai rows from README.md, same machine class; a same-session spot check of
`jai_gloom --smoke --ticks 300` quad gave 34.4 (vm) / 41.2 (interp) under
concurrent-agent load, consistent with the table.)

Interactive `--bench 300` (quad): sim **3.32 ms/frame**, draw 0.01 ms/frame,
~300 fps uncapped (jai reference: ~34 ms/frame total at quad).

Stage ablation (quad, early-returning the pipeline, min of 3):

| stage | ms/tick | delta |
|---|---|---|
| sim only | 0.15 | 0.15 |
| + rays + wall paint | 0.77 | 0.62 |
| + billboards + particles + overlays | 1.05 | 0.28 |
| + glyph rows + HUD + join (full) | 2.90 | 1.85 |

Same shape as JaiScript (glyph-row building dominates; sim is a rounding
error) at roughly **10x less cost per stage**. Squirrel's per-element array
read is ~50 ns where JaiScript's is ~0.5 µs, and 60 ticks/sec of full game
plus renderer fits in ~18% of one core.

## Net

Squirrel earned its keep everywhere the reference leaned on its two big
ideas: generator-shaped brains ported as-is (the only candidate language
where that's true), and reference-semantic containers deleted a whole
category of the reference implementation's friction. The costs were an
afternoon of `::` prefixes and `.tointeger()` casts, two reserved-word
collisions, a hand-rolled join/repeat for its threadbare 3.2 stdlib, and a
raw binding layer that's 3x the C++ of dynamic_binder for the same surface
(fine at 15 bindings; painful at 500). The 10x perf margin over the JaiScript
VM is the loudest number in the table, but it buys no *architecture* —
everything it runs fast, JaiScript also expresses; what it can't buy back is
dynamic_binder's C++ ergonomics or JaiScript's typed-field bug-catching at
the write site. As a pure embedded game-scripting runtime for THIS workload,
Squirrel is the efficiency benchmark the other ports have to explain
themselves against.
