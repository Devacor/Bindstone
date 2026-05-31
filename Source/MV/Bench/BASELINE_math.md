# MV math micro-benchmark — BASELINE (before any optimization)

Build: MSVC VS2022, `cl /O2 /Ob2 /Oi /Ot /std:c++20 /EHsc /DNDEBUG`, single TU including the real
`matrix.hpp` / `points.h` / `boxaabb.h`. Inputs are runtime-seeded random pools indexed by loop
counter (defeats constant-folding/DCE); results fold into a printed sink. Metric = **min ns/op**
over 15 reps (most stable; median shown for sanity). Machine: Windows 11, this dev box.

| operation | min ns/op (run1) | min ns/op (run2) |
|---|---|---|
| mat4 * mat4 | 10.676 | 10.496 |
| TRS build (identity+pos+rot+scale) | 18.575 | 18.024 |
| node recalc (TRS + parent*local) | 30.765 | 31.299 |
| mat4 * Point<> (3-comp) | 1.592 | 1.577 |
| fullMatrixPointMultiply | 1.698 | 1.708 |
| inverse(mat4) | 12.671 | 12.951 |
| transpose(mat4) | 3.225 | 3.210 |
| TransformMatrix::rotateZ (build+mul) | 18.359 | 18.370 |
| Point += Point | 0.419 | 0.416 |
| Point *= float | 0.522 | 0.511 |
| Point /= Point (guarded) | 1.297 | 1.291 |
| Point.magnitude() | 0.793 | 0.798 |
| Point.normalized() | 1.969 | 1.972 |
| distance(Point,Point) | 0.871 | 0.875 |
| AABB transform (4 corners + expand) | 5.366 | 5.361 |

## Observations that gate which optimizations are worth applying
- **mat4*mat4 (10.5ns) and node recalc (31ns)** are the largest absolute targets. Affine-aware
  4x4 multiply is the prime candidate (parent & local are both affine → bottom row known 0,0,0,1).
- **mat4*Point (1.58ns) and fullMatrixPointMultiply (1.70ns) are already near-optimal** — MSVC /O2
  autovectorizes the column-combine. Be skeptical of "SIMD the matrix*point" proposals; unlikely to win.
- **rotateZ build+mul (18.4ns)** has a huge relative headroom (build identity + full 64-mul multiply
  when only 2 columns change) — but only worth it if actually on a hot path.
- **transpose (3.2ns)** copies-then-overwrites (wasteful), NoFill would trim a little.
- **Point /= Point guarded (1.30ns)** is ~3x a plain add due to per-component zero-guard branches.
