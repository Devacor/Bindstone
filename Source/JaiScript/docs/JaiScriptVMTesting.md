# JaiScript VM Testing and Backend Comparison

## Overview

JaiScript supports two execution backends that can be used interchangeably:
- **Interpreter**: Default tree-walk interpreter (stable, full feature support)
- **JVM**: Bytecode virtual machine (experimental, performance-focused)

## Backend Selection

### API Selection
```cpp
engine eng;
eng.set_backend(backend_type::interpreter);  // Use interpreter
eng.set_backend(backend_type::jvm);         // Use JVM
eng.set_backend(backend_type::auto_select); // Automatic selection (default)
```

### Environment Variable
```bash
JAISCRIPT_BACKEND=interpreter ./my_program
JAISCRIPT_BACKEND=jvm ./my_program
JAISCRIPT_BACKEND=auto ./my_program
```

### Automatic Selection Heuristics
When `auto_select` is enabled:
- Scripts > 1000 characters → JVM (better for larger scripts)
- Scripts ≤ 1000 characters → Interpreter (lower compilation overhead)

## Testing Infrastructure

### Backend Comparison Tool
```bash
# Compare all tests between backends
make compare-backends

# Compare specific test
make compare-test TEST=test_operators

# Example output:
Test Name                               Interpreter     JVM            Interp Time    JVM Time       Speedup
--------------------------------------------------------------------------------------------------------
test_arithmetic                         ✓ PASS         ✓ PASS         45.32 ms       8.21 ms        5.52x
test_functions                          ✓ PASS         ✓ PASS         67.89 ms       12.34 ms       5.50x
test_closures                          ✓ PASS         ✗ FAIL         34.56 ms       N/A            N/A
```

### Running Tests with Specific Backend
```bash
# Run all tests with JVM
JAISCRIPT_BACKEND=jvm make test

# Run VM-specific tests
make test-vm

# Run single test with backend
JAISCRIPT_BACKEND=jvm ./bin/test_operators
```

## VM Test Suite

### Core VM Tests
- `test_vm.cpp` - Direct bytecode execution tests
- `test_vm_script_features.cpp` - Script compilation to bytecode
- `test_vm_debug.cpp` - VM debugging features

### Comprehensive Feature Tests
- `test_vm_operators.cpp` - All operators (arithmetic, bitwise, logical)
- `test_vm_strings.cpp` - String operations and concatenation
- `test_vm_error_handling.cpp` - Runtime/compilation error handling
- `test_vm_closures.cpp` - Closure and upvalue implementation
- `test_vm_cpp_integration.cpp` - C++ class binding compatibility

### Backend Comparison Test
`test_backend_comparison.cpp` - Runs representative test cases on both backends and reports:
- Pass/fail status for each backend
- Execution time comparison
- Average speedup metrics

## Performance Expectations

### Typical Speedup Ranges
- Arithmetic operations: 5-10x faster
- Function calls: 3-5x faster
- Loop-heavy code: 10-20x faster
- String operations: 2-3x faster
- First execution: May be slower due to compilation overhead

### When to Use Each Backend

**Use Interpreter When:**
- Scripts are small (<1000 chars)
- One-shot execution (no loops)
- Maximum compatibility needed
- Debugging/development

**Use JVM When:**
- Scripts are large or complex
- Performance is critical
- Lots of loops or function calls
- Production deployment

## Known Limitations

### JVM Backend Current Status
- ✅ All operators implemented
- ✅ Functions and closures
- ✅ Control flow (if/else, loops)
- ✅ Arrays and basic operations
- ✅ String operations
- ✅ C++ class integration
- ⚠️  Map methods not fully implemented
- ⚠️  Some edge cases in error handling
- ⚠️  Debug information (line numbers) incomplete

### Compatibility Notes
- Both backends share global environment
- Variables persist across backend switches
- Function definitions are shared
- C++ bindings work identically

## Development Workflow

### Adding VM Support for New Features
1. Implement interpreter support first (stable baseline)
2. Add bytecode opcodes in `bytecode.hpp`
3. Update compiler in `compiler.cpp`
4. Implement VM execution in `virtual_machine.cpp`
5. Add tests comparing both backends
6. Run `make compare-backends` to verify compatibility

### Debugging Backend Issues
```bash
# Run specific test with verbose output
JAISCRIPT_BACKEND=jvm ./bin/test_name --verbose

# Use GDB with specific backend
JAISCRIPT_BACKEND=jvm gdb ./bin/test_name

# Compare single script between backends
./bin/tool_backend_comparison ./bin/test_specific
```

## Future Improvements
- JIT compilation for hot paths
- More sophisticated heuristics for auto-selection
- Profiling-guided optimization
- Persistent bytecode cache
- Advanced debugging support