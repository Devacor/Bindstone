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

Two platform differences are worth knowing:

- **The native-stack guard is Windows-only.** On Windows the engine reads the current thread's
  stack bounds and raises the catchable `Maximum recursion depth exceeded` while 256 KiB of
  headroom remain. Elsewhere only the numeric call-depth caps apply (interpreter 256, VM 250000
  by default; `engine::max_call_depth(backend, depth)`), so a host with a small thread stack
  should lower them.
- **`.jaibite` files are portable.** The format is little-endian on disk whatever the host's
  byte order, with IEEE-754 doubles and 64-bit ints, so a file written on one platform loads on
  any other.

## The short version

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/jaiscript_tests
```

That builds everything: the library, the test suite, the differential fuzzer, the runner, and
the three example games. Binaries land in `build/bin/`, the library in `build/lib/`.

`CMakePresets.json` names the same two trees: `cmake --preset release` / `cmake --build --preset release`
configure and build `build/`, and `debug` does the same in `build-debug/`. Visual Studio's Open Folder lists the
`x64-*` presets, which build under `out/build/<name>/`.

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
build time, not configure time; the binaries land in the same `build/bin/`:

```bash
cmake -S . -B build
cmake --build build --config Release --target jaiscript_tests
build/bin/jaiscript_tests
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
incremental link is seconds rather than minutes. Release links with LTCG (`JAISCRIPT_LTCG`, ON
when JaiScript is the top-level project); `-DJAISCRIPT_LTCG=OFF` gives a fast-linking Release.

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
| `JAISCRIPT_NATIVE_ARCH` | OFF | `-march=native` for Release. Faster; the binary stops being portable |
| `JAISCRIPT_ENABLE_LTO` | OFF | `-flto` for GCC/Clang Release builds |
| `JAISCRIPT_LTCG` | top-level | Whole-program optimization for MSVC Release (`/GL`, `/LTCG`). The measured performance numbers use it. OFF as a subproject: a `/GL` library would turn every consumer's Release link into a whole-program link (about a minute per edit instead of seconds) |
| `JAISCRIPT_EXPERIMENT_VM_NO_GS` | OFF | Measurement only: `/GS-` on `vm_backend.cpp` (MSVC). Never ship with it |
| `JAISCRIPT_RUNNER_PADDING_CONTROL` | OFF | A code-placement control build of the runner, for benchmarking |

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

`JAISCRIPT_ENABLE_BENCHMARKS=ON` adds the performance suites, which time JaiScript alone. The cross-language
comparisons (Lua, Squirrel, ChaiScript and the game ports) are a separate harness, not part of this package;
nothing third-party ships here.

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
Debug and Release share a prefix (the Debug library is `jaiscriptd`). A top-level Release build
compiles the library with `/GL` on MSVC; pass `-DJAISCRIPT_LTCG=OFF` for an install your Release
edits link against quickly:

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
