# Building JaiScript

JaiScript uses CMake for cross-platform builds with excellent Visual Studio integration.
(Day-to-day dev workflow with the exact CLI commands, Debug-vs-Release advice, and the
Bindstone/MutedVision integration build lives in `Source/JaiScript/CLAUDE.md`.)

## Prerequisites

- **C++20 Compiler**
  - Visual Studio 18 (MSVC)
  - GCC 10+ or Clang 12+ (Linux/Mac)
- **CMake 3.20+** (+ Ninja for the Open-Folder configs)

## Visual Studio (Recommended for Windows)

### Open Folder

1. Open Visual Studio
2. **File → Open → Folder**
3. Select the `JaiScript` directory
4. VS will automatically detect `CMakeLists.txt` and configure the project (Ninja generator;
   build dirs land under `out/build/<config>/`, e.g. `out/build/x64-Debug/`,
   `out/build/x64-Release BENCHMARKS/`)
5. Build using **Build → Build All** (Ctrl+Shift+B), or build just the test target
   (`jaiscript_tests`)
6. Run tests: `out/build/<config>/bin/jaiscript_tests.exe`

## Command Line Build

```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --target jaiscript_tests

# Run tests
cd build
ctest -C Release --output-on-failure
# Or run directly:
./bin/jaiscript_tests.exe                 # tree-walking interpreter (default)
./bin/jaiscript_tests.exe --backend=vm    # whole suite on the bytecode VM
```

## CMake Options

- `JAISCRIPT_BUILD_TESTS` - Build test suite (default: ON)
- `JAISCRIPT_ENABLE_PROFILING` - Profiling instrumentation (default: OFF)
- `JAISCRIPT_ENABLE_BENCHMARKS` - Performance benchmark suites, incl. sol2/Lua and Squirrel
  comparisons (default: OFF; the "x64-Release BENCHMARKS" config turns it on)
- `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` - Env/closure cycle debugging (default: OFF; the Debug
  config defines it)
- `JAISCRIPT_NATIVE_ARCH` - Tune Release for this machine's CPU (`-march=native`; non-portable
  binaries) (default: OFF)
- `JAISCRIPT_ENABLE_LTO` - Link-time optimization for GCC/Clang Release (default: OFF)

Example:
```bash
cmake -B build -DJAISCRIPT_BUILD_TESTS=OFF
```

## Running Tests

The Foundry test suite auto-discovers all test suites. You can filter tests by name:

```bash
# Run all tests
jaiscript_tests.exe

# Bare pattern (ADDITIVE): matching suites (all their tests) OR matching test names
jaiscript_tests.exe "static"

# Dot form: suite filter AND test filter
jaiscript_tests.exe "Language.operators"
```

Adding a NEW test `.cpp` requires a CMake reconfigure (the test list is a `file(GLOB_RECURSE)`
evaluated at configure time).

## Project Structure

```
JaiScript/
├── CMakeLists.txt          # Root CMake configuration
├── include/jaiscript/      # Public headers (core/ detail/ vm/ stdlib/ signals/ properties/ serialization/ testing/)
├── source/
│   ├── implementation/     # Library .cpp files (lexer, parser, interpreter, engine, value, vm/)
│   └── tests/              # Foundry test suites
│       ├── main_test_runner.cpp
│       ├── containers/  core/       foundry/     fuzz/       integration/
│       ├── language/    performance/ properties/ scripts/    semantics/
│       └── serialization/ signals/  stdlib/      vm/
├── examples/               # Standalone example programs
├── bench/                  # Micro-benchmarks
└── sol2/ lua/ squirrel/    # NOT built into the engine — benchmark-comparison sources only
```

## Incremental Builds

CMake automatically tracks dependencies. Just build and it will only recompile changed files.
(The legacy `build_*.bat` scripts still exist for a couple of standalone tools, but the CMake
targets are the supported path.)
