# Building JaiScript

JaiScript builds with CMake and has no dependencies. The whole thing is a static library plus
headers, and everything else in this repository — the test suite, the runner, the example games
— is a target you can turn off.

## Requirements

- **A C++20 compiler.** Verified — the full suite on both backends — with MSVC 19.51 (Visual
  Studio 18, toolset 14.51), Clang 20 and GCC 16 (MinGW-w64); the CI workflow in
  `.github/workflows/ci.yml` targets MSVC, GCC 14 and Clang on Ubuntu 24.04. Older compilers
  may work; they are untested. The library uses `std::to_chars` for doubles and
  `std::strong_order`, so a standard library without those will not get far.
- **CMake 3.20+.**
- **Ninja**, optional but recommended. It gives single-configuration build directories, which
  makes the output paths predictable.

## The short version

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/jaiscript_tests
```

That builds everything: the library, the test suite, the differential fuzzer, the runner, and
the three example games. Binaries land in `build/bin/`, the library in `build/lib/`.

## Picking targets

Building everything takes a while; usually you want one or two things.

```bash
cmake --build build --target jaiscript          # the library alone
cmake --build build --target jaiscript_tests    # the Foundry test suite
cmake --build build --target jaiscript_runner   # the runner/REPL — the binary is `jaiscript`
cmake --build build --target jaiscript_fuzz     # the differential fuzzer
cmake --build build --target jai_gloom          # GLOOM, the raycast shooter
cmake --build build --target jai_crawler        # GRUBWELL, the dungeon crawler
cmake --build build --target jai_demoreel       # the demoscene reel
```

Note that the runner's CMake target is `jaiscript_runner` (the library already owns the name
`jaiscript`), while the executable it produces is `jaiscript`.

## Where the binaries end up

With a single-configuration generator (Ninja, Make) they are in `build/bin/`:

```bash
build/bin/jaiscript_tests
build/bin/jaiscript hello.jai
build/bin/jai_gloom
```

With a multi-configuration generator (Visual Studio, Xcode) the configuration is chosen at
build time, not configure time, and the binaries sit one level deeper:

```bash
cmake -S . -B build
cmake --build build --config Release --target jaiscript_tests
build/bin/Release/jaiscript_tests
```

The example games copy their scripts next to the binary (`gloom_scripts/`, `crawler_scripts/`,
`demoreel_scenes/`) but prefer the source directory in the tree (`examples/gloom/scripts/`,
`examples/crawler/scripts/`, `examples/demoreel/scenes/`) when they can find it, so editing a
script in the tree is what hot reload picks up. Build the games in Release: they render every
frame in script, and a Debug frame takes tens of times longer. Each game's `README.md` has the
flags.

## Visual Studio: Open Folder

Visual Studio can open this directory directly (**File → Open → Folder**). It detects
`CMakeLists.txt`, configures with Ninja, and puts build directories under
`out/build/<config>/`, so the test executable is `out/build/x64-Debug/bin/jaiscript_tests.exe`.
The configurations it offers come from `CMakeSettings.json`.

Debug is where you want to live day to day: it has no link-time code generation, so an
incremental link is seconds rather than minutes.

## Options

The build targets (tests, runner, language server, examples) and the install rules default to
ON when JaiScript is the top-level project and OFF when it is added to another project with
`add_subdirectory`, so a consumer builds the library alone unless it asks for more.

| Option | Default | What it does |
|---|---|---|
| `JAISCRIPT_BUILD_TESTS` | top-level | The Foundry test suite (`jaiscript_tests`) and the fuzzer (`jaiscript_fuzz`) |
| `JAISCRIPT_BUILD_RUNNER` | top-level | The standalone runner / REPL (`jaiscript_runner`, binary `jaiscript`) |
| `JAISCRIPT_BUILD_LSP` | top-level | The language server `jaiscript_lsp` the VS Code extension launches |
| `JAISCRIPT_BUILD_EXAMPLES` | top-level | `jai_gloom`, `jai_crawler`, `jai_demoreel` |
| `JAISCRIPT_INSTALL` | top-level | The install rules and the `JaiScript` CMake package (`find_package(JaiScript)`) |
| `JAISCRIPT_ENABLE_DEBUGGER` | ON | Compiles the DAP-over-TCP debug listener. Turn it OFF for shipping or mobile builds and no socket code is compiled in at all |
| `JAISCRIPT_ENABLE_BENCHMARKS` | OFF | Adds the performance suites to `jaiscript_tests` (see below) |
| `JAISCRIPT_ENABLE_PROFILING` | OFF | Profiler-friendly codegen and link flags |
| `JAISCRIPT_NATIVE_ARCH` | OFF | `-march=native` for Release. Faster; the binary stops being portable |
| `JAISCRIPT_ENABLE_LTO` | OFF | `-flto` for GCC/Clang Release builds |
| `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` | OFF | Inert; nothing in the runtime reads it |
| `JAISCRIPT_EXPERIMENT_VM_NO_GS` | OFF | Measurement only: `/GS-` on `vm_backend.cpp` (MSVC). Never ship with it |
| `JAISCRIPT_RUNNER_PADDING_CONTROL` | OFF | A code-placement control build of the runner, for benchmarking |
| `GLOOM_PORT_LUA`, `GLOOM_PORT_SQUIRREL`, `GLOOM_PORT_CHAI`, `GLOOM_PORT_LUA55`, `GLOOM_PORT_LUAJIT`, `JAIDOOM_PORT_LUA`, `DEMOREEL_PORT_LUA`, `DEMOREEL_PORT_SQUIRREL` | OFF | The comparison ports of the examples (see Comparison benchmarks) |
| `GLOOM_COMPARISON_PADDING_CONTROLS`, `DEMOREEL_PADDING_CONTROLS`, `JAIDOOM_PADDING_CONTROLS`, `JAIDOOM_PORT_TOOLS` | OFF (`JAIDOOM_PADDING_CONTROLS` ON) | Placement controls and tools for the comparison ports |

```bash
cmake -S . -B build -DJAISCRIPT_BUILD_EXAMPLES=OFF -DJAISCRIPT_ENABLE_DEBUGGER=OFF
```

### Integer overflow policy

By default, an overflowing `+`, `-`, `*`, unary `-`, or `INT64_MIN / -1` raises a catchable
script error. Define `JAISCRIPT_WRAP_ON_OVERFLOW` for the whole build to opt into silent
two's-complement wraparound instead:

```bash
cmake -S . -B build -DCMAKE_CXX_FLAGS=-DJAISCRIPT_WRAP_ON_OVERFLOW
```

The unused policy's code is eliminated at compile time, so neither choice costs anything at run
time. Arithmetic is never undefined behavior either way.

## Running the tests

```bash
build/bin/jaiscript_tests                 # tree-walking interpreter
build/bin/jaiscript_tests --backend=vm    # bytecode VM
```

**Both must pass.** The two backends are held to identical results, output, and error text; a
change that only satisfies one of them is not done. See
[../CONTRIBUTING.md](../CONTRIBUTING.md).

Filtering:

```bash
jaiscript_tests "Coroutine"          # ADDITIVE: matching suites (all their tests) OR matching test names
jaiscript_tests "Language.operators" # the dot form is the strict AND: suite filter AND test filter
jaiscript_tests --verbose            # per-test names and timings
```

A grouped CTest view is registered too, in the tests subdirectory:

```bash
ctest --test-dir build/source/tests --output-on-failure
```

Adding a **new** test `.cpp` requires re-running CMake — the test list is a
`file(GLOB_RECURSE)` evaluated at configure time.

## Comparison benchmarks

`JAISCRIPT_ENABLE_BENCHMARKS=ON` adds the performance suites. Some of them compare JaiScript
against other embedded languages, and **those sources are not distributed here** — nothing
third-party ships in this repository. The configure step looks for each one and prints what it
found:

| comparison | expected location |
|---|---|
| Lua 5.4 + sol2 | `lua/src/lua.h` and `sol2/include/sol/sol.hpp` at the repository root |
| Squirrel 3 | `squirrel/include/squirrel.h` at the repository root |
| ChaiScript 6.1.0 | `include/chaiscript/chaiscript.hpp` under `CHAISCRIPT_ROOT` (see below) |

Missing ones are skipped with a configure message, and the rest of the benchmark suites build
and run normally. All four are MIT-licensed and easy to drop in if you want the comparison
numbers; you are just supplying them yourself. The paths are fixed in
`source/tests/CMakeLists.txt` (`CHAISCRIPT_ROOT`, `SQUIRREL_ROOT`, `LUA_ROOT`, `SOL2_ROOT`);
there is no cache variable to point elsewhere. ChaiScript's default points outside the
repository, so edit that one line to where your copy lives.

The GLOOM example has ports in the same languages behind their own options —
`GLOOM_PORT_LUA`, `GLOOM_PORT_SQUIRREL`, `GLOOM_PORT_CHAI` (all OFF) — reading the same
locations. Unlike the benchmarks, a port option that is ON with its sources missing is a
configure error rather than a skip.

## Layout

```
JaiScript/
├── CMakeLists.txt
├── include/jaiscript/      Public headers
│   ├── core/                 value, engine, dynamic_binder, class_definition, ...
│   ├── detail/               lexer, parser, AST, and the kernels both backends share
│   ├── vm/                   opcodes, compiler, bytecode backend, disassembler
│   ├── stdlib/  signals/  properties/  serialization/  testing/
├── source/
│   ├── implementation/     Library .cpp files
│   └── tests/              Foundry suites, and the differential fuzzer under fuzz/
├── tools/
│   ├── jaiscript/          The runner / REPL
│   └── vscode-jaiscript/   The VS Code extension (syntax, debugging)
├── examples/               GLOOM, GRUBWELL, the demoreel
├── bench/                  Micro-benchmarks
└── docs/                   This directory, and the documentation site under docs/site/
```

## Consuming it from another project

The library target is `jaiscript`, also spelled `JaiScript::jaiscript`; link that name and the
include path, the C++20 requirement and the platform libraries (`ws2_32` on Windows, for the
debugger transport) come with it. There are two routes.

**From source, as a subproject.** Nothing but the library builds by default:

```cmake
add_subdirectory(JaiScript)
target_link_libraries(my_game PRIVATE JaiScript::jaiscript)
```

`FetchContent` works the same way. Options set before the `add_subdirectory` (or with `-D`)
turn the tools back on if you want them.

**Installed, through `find_package`.** Build and install the library once per configuration;
Debug and Release share a prefix (the Debug library is `jaiscriptd`):

```bash
cmake -S JaiScript -B build-jai -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/jaiscript       -DJAISCRIPT_BUILD_TESTS=OFF -DJAISCRIPT_BUILD_EXAMPLES=OFF -DJAISCRIPT_BUILD_RUNNER=OFF -DJAISCRIPT_BUILD_LSP=OFF
cmake --build build-jai
cmake --install build-jai
```

```cmake
find_package(JaiScript 0.1 CONFIG REQUIRED)          # -DCMAKE_PREFIX_PATH=/opt/jaiscript
target_link_libraries(my_game PRIVATE JaiScript::jaiscript)
```

On MSVC the library follows the runtime of the project that builds it: as a subproject it takes
the parent's `CMAKE_MSVC_RUNTIME_LIBRARY` (CMake's default is the DLL runtime), and the in-tree
build defaults to the static runtime (`/MT`, `/MTd`) unless you pass `CMAKE_MSVC_RUNTIME_LIBRARY`
yourself. An installed package records the runtime its library was built with in
`JaiScript_MSVC_RUNTIME_LIBRARY` (empty for CMake's default), so a consumer matches it with one
line:

```cmake
set_property(TARGET my_game PROPERTY MSVC_RUNTIME_LIBRARY "${JaiScript_MSVC_RUNTIME_LIBRARY}")
```

The target carries its own interface flags: `/bigobj` on MSVC (the binder headers overflow the
default section limit in a Debug translation unit) and `-Wa,-mbig-obj` on MinGW GCC. Warning
flags stay private to the library.

`examples/embed_quickstart/` is a complete consumer: `add_subdirectory` by default,
`-DEMBED_QUICKSTART_FIND_PACKAGE=ON -DCMAKE_PREFIX_PATH=<prefix>` for the installed package.
