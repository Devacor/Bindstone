# Building JaiScript

JaiScript uses CMake for cross-platform builds with excellent Visual Studio integration.

## Prerequisites

- **C++20 Compiler**
  - Visual Studio 2022 (MSVC 19.30+)
  - GCC 10+ or Clang 12+ (Linux/Mac)
- **CMake 3.20+**

## Visual Studio 2022 (Recommended for Windows)

### Method 1: Open Folder (Easiest)

1. Open Visual Studio 2022
2. **File → Open → Folder**
3. Select the `JaiScript` directory
4. VS will automatically detect `CMakeLists.txt` and configure the project
5. Build using **Build → Build All** (Ctrl+Shift+B)
6. Run tests using **Test Explorer** or run `jaiscript_tests.exe`

### Method 2: Generate Solution File

```powershell
# From JaiScript directory
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
# Open JaiScript.sln in Visual Studio
```

## Command Line Build

```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run tests
cd build
ctest -C Release --output-on-failure
# Or run directly:
./bin/Release/jaiscript_tests.exe
```

## CMake Options

- `JAISCRIPT_BUILD_TESTS` - Build test suite (default: ON)

Example:
```bash
cmake -B build -DJAISCRIPT_BUILD_TESTS=OFF
```

## Running Tests

The Foundry test suite auto-discovers all test suites. You can filter tests by name:

```bash
# Run all tests
jaiscript_tests.exe

# Run only static method tests
jaiscript_tests.exe "static"

# Run only language tests
jaiscript_tests.exe "Language"
```

## Project Structure

```
JaiScript/
├── CMakeLists.txt          # Root CMake configuration
├── include/jaiscript/      # Public headers
├── source/                 # Library implementation
│   ├── CMakeLists.txt
│   └── implementation/     # .cpp files
└── tests/                  # Foundry test suites
    ├── CMakeLists.txt
    ├── main_test_runner.cpp
    ├── containers/
    ├── language/
    ├── semantics/
    ├── integration/
    ├── performance/
    └── vm/
```

## Incremental Builds

CMake automatically tracks dependencies. Just build and it will only recompile changed files!

No more batch file madness. 🎉
