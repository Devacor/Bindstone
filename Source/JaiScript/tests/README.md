# JaiScript Test Suite

## Overview

JaiScript uses two test frameworks:
- **Foundry** (recommended) - Modern, macro-free framework with metallurgy metaphors
- **Legacy** - Older jai_test.hpp framework (being phased out)

## Directory Structure

```
tests/
├── foundry/           # MODERN TESTS (use this for new tests)
│   ├── core/         # Engine, value, parser, lexer, type conversion
│   ├── language/     # Operators, functions, control flow, arrays, maps
│   ├── integration/  # C++ bindings, serialization, shared_ptr
│   ├── vm/          # Virtual machine specific tests
│   └── performance/ # Benchmarks and stress tests
├── legacy/          # Old framework (migrating to foundry)
│   └── dev-tools/   # Debug utilities
└── isolated_build.sh # Build single tests without conflicts
```

## Running Tests

### Foundry Tests (Recommended)

```bash
cd tests/foundry

# DEFAULT: Build individual tests (for concurrent development)
make isolated-core/lexer_tests.cpp        # Single test
make isolated-language/operator_tests.cpp # Another test
make isolated-build-core                  # All core tests as separate executables

# Run isolated test
./isolated_lexer_tests

# Full suite (avoid if others are working)
make clean && make
./quench                 # Run all tests
./quench "operator"      # Filter by name

# Cleanup
make clean               # Clean main build
make clean-isolated      # Clean isolated executables
```

### Legacy Tests
```bash
cd tests/legacy
make run_tests
```

## Best Practices

### 1. **Always Use Isolated Builds for Development**
```bash
# Good - no .o conflicts with other developers
make isolated-core/parser_tests.cpp

# Avoid unless testing everything
make && ./quench
```

### 2. **Writing New Tests**
- Add to `foundry/` directory (NOT legacy)
- Place in appropriate subdirectory
- Use foundry framework:

```cpp
#include <jaiscript/testing/foundry.hpp>
using namespace jai::foundry;

namespace jai::foundry::tests {
class my_tests : public suite {
public:
    my_tests() : suite("My Test Suite") {}
    
    void forge_tests() override {
        test("test_name", [this]() {
            engine engine;
            check_eq(engine.execute("2 + 2").as<int>(), 4);
        });
    }
};
}
FOUNDRY_REGISTER(jai::foundry::tests::my_tests)
```

### 3. **Test Organization**
- **core/** - Low-level functionality (parser, lexer, value system)
- **language/** - Script language features
- **integration/** - C++ binding and interop
- **vm/** - Virtual machine specific behavior
- **performance/** - Benchmarks and stress tests

### 4. **Debugging Tests**
```bash
# Build with debug symbols
make isolated-core/parser_tests.cpp

# Debug with GDB
gdb ./isolated_parser_tests
(gdb) break test_name
(gdb) run
```

### 5. **Auto-Registration**
- Tests auto-register with `FOUNDRY_REGISTER` macro
- No need to manually update test lists
- `JAI_ISOLATED_TEST` flag creates standalone executable

## Migration Status

- 62+ tests migrated to foundry from 120+ legacy files
- New tests should ONLY use foundry framework
- Legacy tests remain for reference during migration
