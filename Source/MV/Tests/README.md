# MV (Bindstone) regression tests

`mv_tests.exe` — a Bindstone test runner built on **JaiScript's Foundry** framework (header-only)
plus Foundry's generic `main_test_runner.cpp`, linked against the built engine (`mutedvision.lib`).
It guards the behavior the rendering/transform optimizations depend on.

## Build & run

```bat
:: builds mv_tests.exe against the optimized engine lib and runs it (returns non-zero on failure)
Source\MV\Tests\build_tests.bat
:: or run directly:
Source\MV\Tests\mv_tests.exe --verbose
Source\MV\Tests\mv_tests.exe "MV Scene Transform"      :: additive: runs the whole suite
Source\MV\Tests\mv_tests.exe "MV Matrix.rotate"        :: dotted: suite AND test
```

Prereq: a Release engine lib at `Builds\Windows\x64\Release\mutedvision.lib` (build
`MutedVision_Windows.vcxproj`). `build_tests.bat <libdir>` can point at a different lib.

## Suites

- **MV Matrix** — `affineMultiply` is bit-identical to the generic 4×4 multiply for affine inputs;
  in-place `rotateX/Y/Z` are numerically identical to the old build-identity-then-multiply form;
  rotate preserves the translation column; `rotateZ(+90°)` maps +X→+Y.
- **MV Scene Transform** — a `Clipped` override-parent draw does not pollute persistent world
  transforms; dynamic transform changes apply and survive override draws; `worldTransform` equals
  the manual ancestor-chain product; and a **PERF GATE**:
  `recalculateMatrixCalls == 0` over 500 idle frames of a static clipped subtree. That gate is a
  *deterministic* guard (a count, not a timing) against re-introducing the per-frame matrix
  re-dirty that the Clipped dirty-flag fix removed.

## Benchmarks (measurement, not assertions)

See `../Bench/` (`bench_math`, `bench_scene`, `bench_frame` + `RESULTS_math.md`) for the
before/after performance numbers. Benchmarks are informational; the deterministic correctness +
recalc-count gates live here.

## Adding a Bindstone test

```cpp
#include <jaiscript/testing/foundry.hpp>
// ... MV headers ...
namespace mv_tests {
class my_suite : public jai::foundry::suite {
public:
    my_suite() : suite("My Suite") {}
    void forge_tests() override {
        test("does the thing", []{ jai::foundry::check(cond, "msg"); });
    }
};
}
using my_suite = mv_tests::my_suite;   // FOUNDRY_REGISTER needs a global-scope name
FOUNDRY_REGISTER(my_suite)
```
Add the `.cpp` to `build_tests.bat`'s source list. Build flags must include
`/MD /DSDL_MAIN_HANDLED` and link gl3w.c + the SDL/Win libs (see the bat).
