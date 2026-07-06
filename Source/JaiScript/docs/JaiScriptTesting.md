# JaiScript Testing — Pointer Doc

The authoritative, always-current testing instructions live in **`Source/JaiScript/CLAUDE.md`**
(Build & Test + Foundry sections). VM-specific testing notes are in
**[JaiScriptVMTesting.md](JaiScriptVMTesting.md)**. This page is just the map.

## Where tests live

Foundry suites are `.cpp` files under `source/tests/`:

```
source/tests/
  containers/ core/ foundry/ fuzz/ integration/ language/ performance/
  properties/ scripts/ semantics/ serialization/ signals/ stdlib/ vm/
```

## Build & run

CMake + Ninja + MSVC (opened as a folder in Visual Studio, or via the CLI commands in
CLAUDE.md). Build the `jaiscript_tests` target; the exe lands in
`out/build/<config>/bin/jaiscript_tests.exe`.

```
jaiscript_tests.exe                     # whole suite (tree-walking interpreter)
jaiscript_tests.exe --backend=vm        # whole suite on the bytecode VM (both must stay green)
jaiscript_tests.exe --verbose           # per-test names + timings (use to find a hang)
```

### Filtering

```
jaiscript_tests.exe "Script Class"      # bare pattern (ADDITIVE): suites whose NAME matches
                                        #   (all their tests) OR any test whose name matches
jaiscript_tests.exe "Suite.Test"        # dot = explicit AND: suite filter . test filter
jaiscript_tests.exe --gtest_filter="Property.*"    # also --jaitest_filter=, --filter=
```

## Writing a test

Suites are classes registered with the `FOUNDRY_REGISTER` macro
(`include/jaiscript/testing/auto_register.hpp`):

```cpp
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {
class my_tests : public suite {
public:
    my_tests() : suite("My Suite") {}
    void forge_tests() override {
        test("two_plus_two", [this]() {
            auto e = engine::make();                              // always shared_ptr
            check_eq((int64_t)4, e->execute("2 + 2").as_int());   // check_eq(expected, actual)
        });
    }
};
}
using my_tests = jai::foundry::tests::my_tests;   // FOUNDRY_REGISTER is at global scope
FOUNDRY_REGISTER(my_tests)
```

Assertions: `check`, `check_eq(expected, actual)`, `check_ne`, `check_near`, `check_throws`,
`check_true/false`, `check_null/not_null`, `check_gt/lt/ge/le`.

## Gotchas

- **New test file ⇒ CMake reconfigure.** The test list is a `file(GLOB_RECURSE)` evaluated at
  configure time — adding a `.cpp` requires re-running the configure step (adding tests to an
  existing file does not). Prefer extending an existing suite file.
- **Iterate in Debug** (links in ~1s vs ~115s Release LTCG, and catches env/closure cycles +
  use-after-free that Release can mask); do a final Release pass.
- Recoverable parse errors make `execute()` return null rather than throw (the parser
  synchronizes) — test "bad input doesn't crash" via "returns and the engine stays usable",
  not `check_throws`.
