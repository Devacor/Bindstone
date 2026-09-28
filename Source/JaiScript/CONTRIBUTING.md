# Contributing to JaiScript

Thanks for looking. This file is the whole workflow: configure, build, test, and the handful of
rules that keep a two-backend language honest. [`docs/BUILD.md`](docs/BUILD.md) has the
longer build reference — options, generators, where binaries land.

## Configure once per configuration

```bash
cmake -S . -B out/build/x64-Debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Iterate in Debug. It has no link-time code generation, so an incremental link is about five
seconds against several minutes for Release, and on MSVC the debug runtime fills freed memory
with `0xDD`, which turns a dangling reference into a loud crash instead of a quiet wrong
answer. Do a Release pass before you call a change done; Release is the configuration that
ships, and optimizer-dependent bugs only show up there.

(Any directory name works. `out/build/<config>` is what Visual Studio's Open Folder mode uses,
so the two agree if you switch between the IDE and the command line.)

## Build

```bash
cmake --build out/build/x64-Debug --target jaiscript_tests
```

Adding a **new** test `.cpp` needs a reconfigure — the test list is a `file(GLOB_RECURSE)`
evaluated at configure time, so a fresh file is invisible until CMake runs again. Adding a test
to an existing file needs nothing.

## The parity rule

Every change is run twice:

```bash
out/build/x64-Debug/bin/jaiscript_tests
out/build/x64-Debug/bin/jaiscript_tests --backend=vm
```

Both must be green, and "green" means more than a pass count: the tree-walking interpreter and
the bytecode VM must produce the same results, the same printed output, and the same error
text, byte for byte. The interpreter is the VM's oracle and the VM is the interpreter's; that
is what makes it possible to trust either one.

The way that stays affordable is shared kernels. A rule both backends need lives once, in a
header under `include/jaiscript/detail/`, and both backends call it:

| kernel | owns |
|---|---|
| `typed_slot.hpp` | the conversion a typed slot runs — declarations, assignments, parameters, returns, field stores — one table for all of them |
| `return_conv.hpp` | which route a `return` takes through that table |
| `container_enforce.hpp` | the element boundary of `array<T>` / `map<K,V>` |
| `cast_kernel.hpp` | `cast<T>` / `cast<T&>` / `cast<shared_ptr<T>>` |
| `ref_lvalue.hpp` | lvalue reference arguments (`f(obj.field)`, `f(arr[i])`) |
| `char_promotion.hpp` | `char` entering arithmetic and bitwise operators as 0..255 |
| `builtin_methods.hpp` | every builtin container and string method |
| `type_proofs.hpp` | the runtime guards behind the parse-time type proofs |
| `null_member_diagnostic.hpp`, `arity_diagnostic.hpp` | error texts both backends raise |

If you find yourself writing the same rule twice, that is the signal to add a kernel rather
than a second implementation.

## Tests

Suites live under `source/tests/`, one directory per area: `language/`, `semantics/`,
`containers/`, `stdlib/`, `vm/`, `serialization/`, `signals/`, `properties/`, `core/`,
`integration/`, `debug/`, `foundry/` (the framework's own suite), `performance/` (built only
with `JAISCRIPT_ENABLE_BENCHMARKS`), and `fuzz/`.

**Reproduce a bug as a failing test first.** Watch it go red, then fix it. A test written after
the fix proves much less.

The framework is Foundry, which is in the tree:

```cpp
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {
class my_tests : public suite {
public:
    my_tests() : suite("My Suite") {}
    void forge_tests() override {
        test("two_plus_two", [this]() {
            auto e = make_engine();                                // honors --backend=vm
            check_eq((int64_t)4, e->execute("2 + 2").as_int());   // check_eq(expected, actual)
        });
    }
};
}

// FOUNDRY_REGISTER is at global scope, so it needs a using-alias first:
using my_tests = jai::foundry::tests::my_tests;
FOUNDRY_REGISTER(my_tests)
```

Assertions: `check`, `check_eq`, `check_ne`, `check_near`, `check_throws`, `check_true`,
`check_false`, `check_null`, `check_not_null`, `check_gt`, `check_lt`, `check_ge`, `check_le`.

Create engines through `foundry::make_engine()`, not `engine::make()` — it applies the
runner's `--backend=` choice, and that is how `--backend=vm` reaches a suite. A test that
calls `engine::make()` directly only ever runs on the interpreter.

One thing to know about parse errors: a recoverable one makes the parser synchronize, so
`execute()` may return null rather than throw. That is why "deeply nested input doesn't crash"
is tested by "execute returns and the engine is still usable", not by `check_throws`.

Filtering:

```bash
jaiscript_tests "Script Class"        # additive: matching SUITES (all their tests) OR matching test names
jaiscript_tests "Suite.Test"          # the dot form is the strict AND
jaiscript_tests --verbose             # per-test names and timings; use this to find a hang
```

## The differential fuzzer

`jaiscript_fuzz` generates programs, runs each on both backends, and reports any disagreement.

```bash
jaiscript_fuzz                         # seeds 0..999
jaiscript_fuzz --iterations 5000       # more programs
jaiscript_fuzz --seed 41337            # one specific program
jaiscript_fuzz --dump 41337            # print that program and stop
jaiscript_fuzz --seed 41337 --minimize # shrink a divergence to its smallest form
jaiscript_fuzz --isolate               # run each program in a child process (survives a crash)
jaiscript_fuzz --no-proofs             # with parse-time type proofs disabled
jaiscript_fuzz --no-suppressions       # include already-triaged shapes
```

Exit code 0 means no *new* divergence. The already-triaged shapes are written up in
`source/tests/fuzz/known_divergences.md`; if you hit one of those, the entry explains why it is
allowed. A fixed set of seeds also runs inside `jaiscript_tests` as the "Differential Fuzz
Smoke" suite, so a regression shows up in the ordinary test run.

## Code style

Match the surrounding code. Beyond that:

- **snake_case throughout** — types, functions, members. It reads like the standard library on
  purpose.
- **Tabs in `interpreter.cpp` and `parser.cpp`**; elsewhere, follow the file.
- **No non-const `static` or `thread_local` storage.** State that outlives a call belongs to an
  engine instance. This is what makes multiple engines on multiple threads safe, and one static
  cache silently breaks it. `static const` non-pointer data is fine.
- **Every `script_value` carries an engine reference** — `script_value(v, eng)`, or
  `script_value(std::monostate{}, eng)` for null.
- **Integer arithmetic goes through `jai::ints::try_add` / `try_sub` / `try_mul` / `try_div` /
  `mod`**, never raw `a + b` on a `script_int`. The overflow policy is a build-time choice and
  those helpers are where it lives.
- **`operator==` and `operator<=>` must stay consistent** and form a strict weak ordering —
  `script_value` is used as a `std::map` key.
- **Never read a `script_value` after moving from it** — the move constructor nulls the source.
- **Comments explain a non-obvious *why*** — a gotcha, an invariant, a deliberate deviation —
  in one line. Not what the code does, and not what it used to do.
- **Keep heavy headers out of core headers.** `<intrin.h>`, `<format>` and the like do not
  belong on the `value.hpp` path; consumers pay for every one.

Two catalogues move with the code:

- **Error texts** are listed in [`docs/diagnostics.md`](docs/diagnostics.md) and must stay
  byte-identical across backends. Change a text, update the catalog, and check the site for
  output blocks that quote it.
- **The AST format.** A change to what the serializer writes bumps `k_jaibite_version` in
  `include/jaiscript/detail/ast_serializer.hpp`, so stale `.jaibite` caches are
  rejected instead of misread.

Before you touch value layout, `strong_ptr`, raw-index dispatch or the VM run loop, read
[`docs/invariants.md`](docs/invariants.md).

## Documentation

Prose lives in two places, and they have a precedence order: `docs/*.md` are the contract, and
the site in `docs/site/` explains it. When they disagree, the `.md` files win and the site is
the bug.

Every JaiScript sample on the site must run on both backends under strict checking. If you
change a sample, run it:

```bash
jaiscript --check=strict --backend=vm     sample.jai
jaiscript --check=strict --backend=interp sample.jai
```

Where a page shows output, paste the real output.

## Commits and pull requests

One-line commit messages — a title, no body.

Before you open a PR:

- [ ] `jaiscript_tests` and `jaiscript_tests --backend=vm` green in Debug **and** Release
- [ ] `jaiscript_fuzz` green (or the divergence triaged in `known_divergences.md`)
- [ ] `docs/diagnostics.md` updated if any error text changed
- [ ] docs updated, and any sample you touched re-run on both backends
- [ ] no new `static` / `thread_local` mutable state

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push and pull request.
It builds Debug and Release with three compilers — MSVC on Windows, GCC 14 and Clang on
Ubuntu 24.04 — and each of the six builds runs:

- `jaiscript_tests` and `jaiscript_tests --backend=vm`
- a 300-program differential-fuzz campaign (`jaiscript_fuzz --isolate`)
- the example hash gates: GLOOM and GRUBWELL (and, in Release, demoreel and Gator Quest) must
  reproduce their reference hashes on both backends

A separate job runs a cheap `-fsyntax-only` sweep of every implementation file, which catches
MSVC-isms and missing standard includes without waiting for the matrix. The gates live in
[`tools/ci/gates.sh`](tools/ci/gates.sh), so any of them runs locally the way CI runs it:

```bash
BIN=build/bin bash tools/ci/gates.sh tests-vm gloom
```

CI is a safety net, not a substitute for the checklist above.
