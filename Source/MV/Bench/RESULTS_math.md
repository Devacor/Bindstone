# MV math optimization — before/after results

Benchmark: `bench_math.cpp` (MSVC `/O2 /Ob2 /Oi /Ot /std:c++20 /DNDEBUG`, includes the real
headers). Metric = **min ns/op** over 21 reps (most stable; run-to-run shown as A / B).
Changed ops are measured **old-vs-new in the same binary** (verbatim pre-change impls kept in the
bench) so there is no cross-build noise. Unchanged "anchor" ops (mat4×Point, magnitude, …) match
the baseline, confirming the two columns are comparable.

## Correctness (self-check over 1024 random affine inputs, runs every launch)
- `affineMultiply` vs generic `operator*`: **fully bit-identical** — 0 numeric diffs, 0 signed-zero diffs.
- in-place `rotateX/Y/Z` vs old build-identity-then-multiply: **numerically identical** — 0 numeric
  diffs; 1607 benign signed-zero diffs (`-0.0f` vs `+0.0f`) confined to the always-zero projective
  row of an affine matrix. `-0.0f == +0.0f` and behaves identically in every downstream op
  (matrix×point sums it as 0); nothing in the engine divides by or bit-inspects that row.
- `transpose` NoFill: pure element copy → bit-identical.

## Timings (min ns/op)

| operation | before | after | speedup |
|---|---|---|---|
| **mat4 × mat4** (world×local composition) | 11.5 / 12.0 | **6.0 / 6.1** (`affineMultiply`) | **~1.9×** |
| **node recalc** (TRS build + parent×local) | 34.0 / 33.7 | **29.3 / 29.3** | ~1.15× (−14%) |
| **rotateZ right-multiply** | 17.1 / 16.9 | **4.7 / 4.7** (in-place) | **~3.6×** |
| transpose(mat4) | 3.6 / 3.6 | 3.5 / 3.5 | ~1.0× (no win) |
| Point /= float (cerr branch) | 1.27 | — | n/a (cerr costs nothing) |

Anchors (unchanged, confirm comparability): mat4×Point 1.6, fullMatrixPointMultiply 1.7,
magnitude 0.79, normalized 1.96, distance 0.87, Point+=Point 0.42.

## Applied (measured wins, behavior-safe)
1. **`affineMultiply` + wire into `node.cpp` Node::recalculateMatrix** — the world = parent×local
   composition (runs per dirty node per frame). Affine-only 4×4 multiply skips the known `[0 0 0 1]`
   4th row (~44% fewer flops). Bit-identical for affine inputs. **1.9× on the multiply; −14% on the
   full per-node recalc** (the rest is the TRS build, dominated by 6 trig calls — inherent).
2. **In-place single-axis `rotateX/Y/Z`** — replaced "build identity rotation + full 4×4 multiply"
   with a direct 2-column update. **~3.6× faster**, numerically identical, and now correct for
   non-affine inputs too (preserves untouched columns/rows exactly).
3. **Clipped override-draw dirty-flag fix** (`node.cpp` Node::draw(overrideParent) + new
   `recalculateLocalMatrix`). The override draw (used by `Clipped` to render its subtree to a clip
   texture) now recalcs the **local** matrix only and never touches `worldMatrixTransform`, so it
   cannot pollute it with the clip-space temporary — removing the need for the per-frame
   `SCOPE_EXIT` re-dirty of both flags. Measured with `bench_scene.cpp` (headless `Draw2D` +
   300-node clip subtree, 2000 static refresh frames):

   | metric | before | after |
   |---|---|---|
   | `recalculateMatrixCalls` / frame | **300** (every node) | **0** |
   | refresh-only ms/frame | 0.0345 | **0.0062** (~5.6×) |
   | refresh + world queries ms/frame | 0.0366 | **0.0073** (~5×) |

   Correctness (in-bench, empirical): world transforms byte-identical before/after an override draw
   (0/300 polluted; static-query `sink` unchanged) + dynamic check passes (move-applied,
   move-survives-draw). Composition now also uses `affineMultiply`. The win is on **static** nodes
   under a `Clipped`; animated nodes recompute correctly as before (markMatrixDirty path unchanged),
   so a mixed scene benefits proportional to its static fraction.

## End-to-end frame implication (full engine, pristine vs optimized)

`bench_frame.cpp` links the real engine lib and runs a realistic frame: a 1500-node "world" subtree
drawn the normal way (worldTransform per node) + a 500-node "UI" subtree refreshed through the
`Clipped` override path, with a configurable fraction of nodes animated/frame. BEFORE = engine built
from a pristine `git worktree` at HEAD (no optimizations); AFTER = optimized tree. Same `bench_frame`
binary source linked against each lib. The world-node `sink` is **byte-identical** between the two
(behavior-preserving confirmed at the whole-engine level).

| scene (2000 nodes) | BEFORE ms/frame | AFTER ms/frame | recalc/frame before→after | gain |
|---|---|---|---|---|
| 8% animated | 0.383 | 0.287 | 1275 → 775 | **−25%** |
| 2% animated (mostly static UI) | 0.209 | 0.107 | 1034 → 534 | **−49% (~1.96×)** |

Both optimizations compound: `affineMultiply` speeds up *every* world recompute that still happens
(the 775/534), and the dirty-flag fix *eliminates* ~500 redundant clipped recomputes/frame. The win
grows as the clipped UI is more static (the common case), and shrinks toward the affine-multiply-only
gain as more of the scene animates.

Reproduce the pristine "BEFORE" lib (uncommitted changes => HEAD is original):
`git worktree add --detach D:/git/bindstone-baseline HEAD` → build `MutedVision_Windows.vcxproj`
there (`/p:SolutionDir=D:\git\bindstone-baseline\`) → `build_frame.bat
"D:\git\bindstone-baseline\Builds\Windows\x64\Release" bench_frame_base.exe` → run vs
`bench_frame_opt.exe` → `git worktree remove D:/git/bindstone-baseline --force`.

## Tested, NOT applied (benchmark showed no win)
- **`transpose` NoFill**: no measurable change — MSVC `/O2` already eliminates the wasted copy.
  (Kept as a 1-line clarity change; trivially revertible.)
- **`cerr`-out-of-line in `Point/Size::operator/=(scalar)`**: `Point /= float` (1.27 ns) ≈
  `Point /= Point` (1.28 ns), so the cold `cerr` branch costs nothing on the hot path. Not applied.

## Deferred — need an integration/scene benchmark (not micro-benchmarkable here)
- **projection-cached-composed-matrix** (render.cpp): hoist `ProjectionDetails` out of per-point/
  per-corner loops and cache `cameraProjection*modelview` (+ its inverse) per batch, so `unProject`
  stops re-running the full 4×4 inverse for every point. Needs a 1000-point projection bench.
- **affine inverse** for the (orthographic ⇒ affine) projection product — ~3–4× on `inverse`, but the
  only caller is mouse-move hit-testing, not per-frame.

## Out of scope — correctness bugs (behavior-changing, NOT touched)
- **`BoxAABB::centerPoint()`** (boxaabb.h:63): returns `minPoint + ((minPoint+maxPoint)/2)` instead
  of `(minPoint+maxPoint)/2` — wrong unless minPoint==0. Appears unused; needs a consumer audit.
- **`BoxAABB::collides(..., useDepth=true)`** (boxaabb.h:381): depth uses OR, so Z-separated boxes
  still report collision. No caller passes `useDepth=true` today. Needs a gameplay decision + test.
