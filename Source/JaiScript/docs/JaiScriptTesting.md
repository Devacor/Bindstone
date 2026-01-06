# JaiScript Testing Guide

## Recent Updates (2025)

- Fixed operator overloading issues where interpreter fast paths bypassed custom operators
- Fixed constructor parameter type conversions to use same mechanism as methods
- Cleaned up all debug output from the engine for cleaner test results
- Tests now pass both individually and when run together

## Test Organization (After Migration)

All tests have been reorganized into a new structure:

```
JaiScript/
├── include/jaiscript/testing/
│   └── foundry.hpp         # Public test framework (jai::foundry)
│
└── tests/
    ├── foundry/            # NEW TESTS GO HERE
    │   ├── core/          # Engine, value, parser, lexer
    │   ├── language/      # Operators, functions, control flow
    │   ├── integration/   # C++ bindings, type conversions
    │   └── performance/   # Benchmarks and stress tests
    │
    └── legacy/            # Existing tests (gradually migrating)
        ├── core/          # 7 files
        ├── language/      # 60 files  
        ├── integration/   # 24 files
        ├── performance/   # 9 files
        ├── dev-tools/     # Debug utilities
        └── jai_test.hpp   # Old test framework

Old Location (NO LONGER EXISTS):
- TestSuite/Tests/  ❌ DO NOT CREATE TESTS HERE
```

## Writing New Tests

### ⚠️ NEVER Add main() to Test Files!

**Problem**: Multiple main() functions cause link errors and prevent test composition.

### Use jai::foundry Framework (Correct Way)

```cpp
// File: tests/foundry/core/test_my_feature.hpp
#pragma once  // Header file, not .cpp!

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class my_feature_tests : public suite {
public:
    my_feature_tests() : suite("My Feature") {}
    
    void forge_tests() override {
        test("basic_functionality", [this]() {
            engine engine;
            auto result = engine.execute("2 + 2");
            check_eq(result.as<int>(), 4);
        });
    }
    
private:
    engine engine_;
};

// Factory function for test runner
std::unique_ptr<suite> create_my_feature_tests() {
    return std::make_unique<my_feature_tests>();
}

} // namespace

// NO main() HERE!
```

## jai::foundry API

- `suite` - Base class for test suites
- `forge_tests()` - Override to define tests (called internally)
- `quench()` - Run all tests and return failure count
- `test(name, lambda)` - Add a test
- `benchmark(name, lambda)` - Add a benchmark
- `pre_test()/post_test()` - Optional setup/teardown
- `check(condition)` - Assert condition is true
- `check_eq(expected, actual)` - Assert equality
- `check_near(expected, actual, tolerance)` - Float comparison
- `check_throws(lambda)` - Assert exception thrown

## Running Tests

### ⚠️ IMPORTANT: Focused Testing for Concurrent Development

When multiple developers/Claude instances work on JaiScript simultaneously, avoid build conflicts:

**DO:** Build only the specific test you need
```bash
# Good - focused compilation
cd tests/legacy/language
g++ -std=c++20 -I../../../include -o test_operators test_operators.cpp -pthread
./test_operators

# Or use a temp directory
mkdir -p /tmp/mytest && cd /tmp/mytest
g++ -std=c++20 -I/path/to/jaiscript/include -o test_temp /path/to/test_file.cpp
./test_temp
```

**DON'T:** Clean and rebuild entire test suite
```bash
# Avoid - causes conflicts with other instances
cd tests/legacy && make clean && make  # ❌ Object files get deleted!
```

### Running Full Test Suite (Single Instance Only)

When you're the only one working:
```bash
cd tests/legacy
make run_tests  # Builds and runs all tests
```

### New Foundry Tests
```bash
cd tests/foundry/core
g++ -std=c++20 -I../../../include -o my_test test_my_feature.cpp
./my_test
```

## Migration Status

- ✅ Test files reorganized into categories
- ✅ jai::foundry framework in public headers
- 🔄 Gradually migrate tests from legacy/ to foundry/
- ❌ Do not create new tests in legacy format

## Quick Compile Commands

For easy copy-paste when testing specific features:

```bash
# Core tests (engine, value, parser, lexer)
g++ -std=c++20 -I../../../include -o test_engine test_engine.cpp -pthread

# Language tests (operators, functions, control flow)
g++ -std=c++20 -I../../../include -o test_operators test_operators.cpp -pthread

# Integration tests (C++ bindings)
g++ -std=c++20 -I../../../include -o test_dynamic_binder test_dynamic_binder.cpp -pthread

# With debug info
g++ -std=c++20 -g -I../../../include -o test_debug test_functions.cpp -pthread
gdb ./test_debug

# Temporary isolated build
mkdir -p /tmp/jai_test_$$ && cd /tmp/jai_test_$$
g++ -std=c++20 -I/path/to/include -o test /path/to/test.cpp -pthread
```

### Isolated Test Development (CONDITIONAL_ISOLATED_TEST)

When developing a single test in isolation:

```bash
# Use the isolated_build.sh script to avoid .o file conflicts
./tests/isolated_build.sh tests/foundry/core/test_my_feature.cpp

# Or manually with isolation flag (builds to temp directory)
g++ -std=c++20 -DJAI_ISOLATED_TEST -I../../../include -o /tmp/test_isolated test_my_feature.cpp
/tmp/test_isolated
```

**How it works:**
- `CONDITIONAL_ISOLATED_TEST(test_class)` macro in your test adds a `main()` only when `-DJAI_ISOLATED_TEST` is defined
- The `isolated_build.sh` script automatically:
  - Creates a temporary build directory
  - Compiles with `-DJAI_ISOLATED_TEST` flag
  - Runs the test
  - Cleans up after itself
- **No .o file conflicts**: Temporary directory ensures isolated .o files don't interfere with main test suite

## Important Notes

1. **All new tests** must use jai::foundry and go in `tests/foundry/`
2. **Legacy tests** remain functional but should be migrated over time
3. **No macros** in foundry - just clean C++ classes
4. **Categories**: Choose the right subdirectory based on what you're testing
5. **Concurrent development**: Always compile specific tests, never `make clean` on shared directories