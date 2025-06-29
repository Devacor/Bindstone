# JaiScript Foundry Test Framework

A clean, macro-free test framework with metallurgy-inspired naming.

## Metaphor

- **Foundry**: The test framework where code is forged
- **forge_tests()**: Sets up the tests (like preparing molds)
- **quench()**: Runs the tests (like tempering metal)
- **pre_test/post_test**: Heating and cooling cycles

## Example

```cpp
#pragma once
#include <jaiscript/testing/foundry.hpp>

namespace jai::foundry::tests {

class my_tests : public suite {
public:
    my_tests() : suite("My Tests") {}
    
    // Override to define your tests
    void forge_tests() override {
        test("example", [this]() {
            check_eq(2 + 2, 4);
        });
    }
};

} // namespace

// In main test runner:
auto tests = std::make_unique<my_tests>();
int failures = tests->quench();  // Run the tests
```

## Running Tests

```bash
# Compile and run all tests
./quench

# Run specific test suite
./quench "Engine"

# Run isolated test
./isolated_build.sh tests/foundry/core/my_test.cpp
```