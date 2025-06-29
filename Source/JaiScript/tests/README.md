# JaiScript Test Suite

## Structure

- `legacy/` - Existing tests using the old jai_test.hpp framework
  - `core/` - Core functionality tests (engine, value, parser, lexer)
  - `language/` - Language feature tests (operators, functions, control flow)
  - `integration/` - C++ integration tests (bindings, type conversions)
  - `performance/` - Performance and stress tests
  - `dev-tools/` - Development and debugging tools

- `foundry/` - New tests using the jai::foundry framework
  - Same subcategories as legacy

## Running Tests

### Legacy Tests
```bash
cd tests/legacy
make run_tests
```

### Foundry Tests
```bash
cd tests/foundry
# Build instructions here
```

## Migration Status

Tests are being gradually migrated from `legacy/` to `foundry/`.
