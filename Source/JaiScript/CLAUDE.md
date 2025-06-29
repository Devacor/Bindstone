# JaiScript Development Guide for Claude

## Overview

JaiScript is a modern, high-performance scripting language with C++-like syntax, designed as a replacement for ChaiScript. It features RAII semantics, hot-reloading capabilities, snake_case naming conventions matching the C++ standard library, and exceptional performance (25-578x faster than ChaiScript).

### Why JaiScript Exists

JaiScript addresses real pain points from production use of ChaiScript in game development:
- **Forced array notation**: `self["property"]` instead of natural `self.property`
- **No hot-reload**: "Already defined" errors require full restart
- **Explicit self-passing**: Methods need `fun(self, dt)` pattern
- **Manual closures**: Explicit capture lists `fun[var1, var2]`
- **Awkward null checks**: `.is_var_null()` instead of `!= null`

See `docs/why-jaiscript.md` for detailed examples from actual production code.

## Current Status

### Recent Fixes (2025)
- **Operator Overloading**: Fixed interpreter fast paths bypassing custom operators
- **Type Conversions**: Added implicit conversions (int→double, int→float) in constructors
- **Constructor Parameters**: Fixed to use same `value_converter` mechanism as methods
- **Clean Codebase**: Removed all debug output and temporary test files
- **Variable Persistence**: Fixed issue where variables declared with `auto` weren't persisting between `execute()` calls
  - **IMPORTANT**: Variable declarations require semicolons: `auto x = 42;` (not `auto x = 42`)

### ✅ Completed Features

**Naming Convention Update (2025):**
- Complete migration from PascalCase to snake_case naming throughout the codebase
- All class names now use snake_case: `engine`, `lexer`, `parser`, `token`, etc.
- Type aliases updated: `script_int`, `script_float`, `script_string`, `script_bool`, `script_char`
- Consistent with C++ standard library naming conventions
- See `NAMING_CONVENTIONS.md` for detailed migration guide and examples

**Core Language:**
- Complete lexer, parser, and AST with visitor pattern
- Full tree-walk interpreter with expression evaluation
- All control flow statements (if/else, while, for, break, continue)
- Functions with flexible syntax (C++, TypeScript, scripting styles)
- Lambda expressions with value and reference captures
- All arithmetic operators: `+`, `-`, `*`, `/`, `%`
- Compound assignments: `+=`, `-=`, `*=`, `/=`
- Increment/decrement: `++`, `--` (prefix and postfix)
- Comparison operators: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Spaceship operator: `<=>` (C++20 three-way comparison)
- Bitwise operators: `&`, `|`, `^`, `~`, `<<`, `>>`
- Logical operators: `&&`, `||`, `!`  
- Ternary operator: `condition ? true_val : false_val`
- String concatenation with `+` and `+=`
- Exception handling: `try`/`catch` blocks with re-throw support

**Type System:**
- Fixed-size primitives: `script_int` (64-bit), `script_float` (64-bit double), `script_char`, `script_bool`
- `script_string` (UTF-8 safe)
- Arrays with literals: `[1, 2, 3]` (heterogeneous elements supported)
  - Array methods: `size()`, `push()`, `pop()`, `empty()`, `clear()`, `front()`, `back()`
  - Array subscript operators `arr[i]` with automatic bounds checking
- Maps with both C++ and JSON style literals: `{{"key", value}}` or `{"key": value}`
  - Map subscript operators `map["key"]`
  - Map methods: `size()`, `empty()`, `clear()`, `contains()`, `erase()`, `keys()`, `values()`
- Smart pointers: `shared_ptr<T>`, `weak_ptr<T>`
- Reference types: `T&`
- Function types: `script_function<R(Args...)>`
- Automatic type conversions: int↔double, int→float, with C++-like implicit conversions

**C++ Integration:**
- class_builder API for clean class registration
- Lambda method binding (no static_cast needed!)
- Property registration with automatic getter/setter generation
- Constructor overloading with automatic type conversions
- Zero-copy parameter passing for const references
- Custom operator overloading support (fixed in interpreter fast paths)
- Member access operators (`.` and `->`) work for C++ bound classes

**Performance:**
- StringSymbolizer for fast variable lookups (27% improvement)
- Fast path arithmetic for built-in types (respects custom operator overloads)
- Move semantics throughout (20-40% improvement)
- ValueStack optimization
- Overall: 25-578x faster than ChaiScript

**Bytecode VM (Experimental):**
- Optional JVM backend for improved performance on larger scripts
- Automatic backend selection based on script size (>1000 chars → JVM)
- Manual selection: `engine.set_backend(backend_type::jvm)`
- Environment variable: `JAISCRIPT_BACKEND=jvm`
- Shared global state between backends allows seamless switching
- See `JaiScriptVMTesting.md` for backend comparison methodology

**Standard Library:**
- I/O Functions (`stdlib/io.hpp`):
  - `print(...)` - Formatted output with `{}` and `{n}` placeholders (like std::format)
  - `to_string(value)` - Convert any value to string representation
  - `type_of(value)` - Get type name as string ("int", "float", etc.)
- JSON Functions (`stdlib/json.hpp`):
  - `to_json(value)` / `to_json(value, indent)` - Serialize to JSON
  - `from_json(json_string)` - Parse JSON to JaiScript values
- Usage: `jai::stdlib::register_all(engine)` or register individual modules

### ❌ Not Yet Implemented

**Script-Side Classes:**
- No `class` keyword parsing in scripts
- No script-defined classes, constructors, or destructors
- No inheritance or polymorphism in scripts
- No `this` keyword support for script classes
- No `new` operator for script classes

**Control Flow Statements:**
- ✅ Break and continue statements (completed with C++ semantics)
- ✅ Try-catch exception handling (completed with re-throw support)

**Other Missing Features:**
- Switch/case statements
- Range-based for loops
- Exception handling in VM backend (interpreter only for now)

## Testing Guidelines

### CRITICAL: Test Development Rules

1. **Default to using GDB to debug issues instead of creating new minimal tests**, unless the new minimal test is critical for simplicity of the test case.

2. **Use the correct test location**:
   - **NEW TESTS**: Create in `tests/foundry/` using `#include <jaiscript/testing/foundry.hpp>`
   - **LEGACY TESTS**: Located in `tests/legacy/` using old framework
   - **NEVER CREATE TESTS IN**: Old `TestSuite/` directory (no longer exists)

### Test Organization

```
tests/
├── foundry/        # NEW TESTS GO HERE (using jai::foundry framework)
│   ├── core/       # Engine, value, parser, lexer tests
│   ├── language/   # Operators, functions, control flow tests  
│   ├── integration/# C++ bindings, type conversion tests
│   └── performance/# Benchmarks and stress tests
└── legacy/         # Old tests (gradually migrating to foundry)
    └── [same subdirectories]
```

### Foundry Test Framework

**IMPORTANT FOR MULTI-INSTANCE DEVELOPMENT**: The Foundry test framework now supports auto-registration and isolated test building to prevent conflicts between multiple Claude instances.

#### Writing Tests

For NEW tests, use the `jai::foundry` framework with auto-registration:

```cpp
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class my_tests : public suite {
public:
    my_tests() : suite("My Test Suite") {}
    
    void forge_tests() override {
        test("example_test", [this]() {
            engine engine;
            check_eq(engine.execute("2 + 2").as<int>(), 4);
        });
    }
};

} // namespace jai::foundry::tests

// This macro handles both auto-registration and isolated test support
FOUNDRY_REGISTER(jai::foundry::tests::my_tests)
```

#### Building and Running Tests

**For Multiple Claude Instances Working Simultaneously:**

```bash
# Navigate to foundry tests directory
cd Source/JaiScript/tests/foundry

# Option 1: Build and run ALL tests (avoid if other instances are working)
make clean && make
./quench

# Option 2: Build and run a SINGLE test in isolation (RECOMMENDED)
# This creates a separate executable without .o file conflicts
make isolated-core/value_semantics_tests.cpp
make isolated-language/operator_tests.cpp
make isolated-integration/class_builder_tests.hpp

# Option 3: Build all tests in a directory as isolated executables
make isolated-build-core       # Creates isolated_lexer_tests, isolated_parser_tests, etc.
make isolated-build-language   # Creates isolated_operator_tests, isolated_function_tests, etc.

# Run a specific isolated executable
./isolated_lexer_tests

# Clean up isolated executables
make clean-isolated
```

#### How Auto-Registration Works

1. **FOUNDRY_REGISTER Macro**: 
   - When building the full suite: Registers the test with the global registry
   - When building isolated (`-DJAI_ISOLATED_TEST`): Creates a `main()` function instead
   - Prevents .o file conflicts between different build modes

2. **No Manual Registration**: 
   - Tests automatically register themselves
   - No need to update `run_tests.cpp` when adding new tests
   - Just add `FOUNDRY_REGISTER(namespace::test_class)` at the end of your test file

3. **Isolated Test Benefits**:
   - Each test compiles to its own executable
   - No shared .o files that could conflict
   - Can debug individual tests with GDB
   - Multiple Claude instances can work on different tests simultaneously

#### Example Workflows

**Claude Instance 1 - Working on Parser:**
```bash
cd Source/JaiScript/tests/foundry
# Edit parser_tests.cpp
make isolated-core/parser_tests.cpp
# Debug if needed
gdb ./test_isolated_core/parser_tests.cpp
```

**Claude Instance 2 - Working on Value Semantics:**
```bash
cd Source/JaiScript/tests/foundry
# Edit value_semantics_tests.cpp
make isolated-core/value_semantics_tests.cpp
# No conflicts with Instance 1!
```

**Claude Instance 3 - Running Full Suite:**
```bash
cd Source/JaiScript/tests/foundry
make clean && make
./quench
# Or run with filter
./quench "operator"  # Only runs tests with "operator" in the name
```

Legacy tests still use the old framework in `tests/legacy/jai_test.hpp`.

## Development Workflow

### When Testing Changes

**IMPORTANT: Focused Testing for Multi-Instance Development**

Since multiple Claude instances may be working on JaiScript simultaneously:

1. **Build only what you need** - Don't run `make clean && make` on the entire test suite
2. **Compile specific test files** - Focus on the test that covers your feature:
   ```bash
   # Good - compile only the test you need
   cd tests/legacy/core
   g++ -std=c++20 -I../../../include -o test_engine test_engine.cpp -pthread
   ./test_engine
   
   # Avoid - this can conflict with other instances
   cd tests/legacy && make clean && make
   ```
3. **Use isolated builds** - Create temporary build directories if needed:
   ```bash
   mkdir -p /tmp/my_test_build
   g++ -std=c++20 -I../include -o /tmp/my_test_build/test_specific test_specific.cpp
   ```

### When Adding Features

1. **Check existing tests first** - Don't create new test files unnecessarily
2. **Use GDB for debugging** - Attach to existing tests rather than creating minimal repros
3. **Add tests to appropriate category** - tests/foundry/{core,language,integration,performance}
4. **Build incrementally** - Only compile the tests you're working on

### When Fixing Bugs

1. **Reproduce in existing test infrastructure** - Modify an existing test temporarily
2. **Use GDB to diagnose** - `gdb ./test_engine` etc.
3. **Fix the issue**
4. **Add regression test to appropriate existing test file**
5. **Clean up any temporary test files**

## Architecture Notes

### Engine Design

The Engine maintains:
- `globalEnvironment`: All globals stored here (single source of truth)
- `nonSerializableGlobals`: Set of globals to exclude from serialization
- `interpreter`: Persistent interpreter instance (reused between executions)

### Value System

- Type-erased storage using `std::variant`
- Custom extractor for unwrapping ClassInstance objects
- Move semantics throughout for performance
- Thread-local storage for reference parameter passing

### class_builder Pattern

Clean API for C++ class registration:
```cpp
make_class_builder<MyClass>(engine, "MyClass")
    .constructor<script_int, script_float>()
    .method("method_name", &MyClass::method_name)
    .method("chainable", [](MyClass& self) -> MyClass& { 
        // Lambda for chainable methods
        return self; 
    })
    .property("prop", &MyClass::prop)
    .inherits<base_class>()
    .build();
```

### Performance Optimizations

1. **StringSymbolizer**: Maps variable names to integer IDs (like Unreal's FName)
   - WARNING: IDs are NOT deterministic - never serialize them
   
2. **Fast Path Arithmetic**: Skips operator overload checks for built-in types

3. **Move Semantics**: Value class has explicit move constructor/assignment

4. **ValueStack**: Uses vector with reserved capacity instead of std::stack

## Common Patterns

### Script Execution
```cpp
engine engine;
script_value result = engine.execute("2 + 2");
script_int val = result.as<script_int>();
```

### Function Registration
```cpp
// Simple function
engine.add_function("square", [](script_int n) -> script_int {
    return n * n;
});

// Variadic function
engine.add_variadic_function("print", [](const std::vector<script_value>& args) {
    // Handle any number of arguments
});
```

### Variable Access
```cpp
engine.add_global("PI", script_value(3.14159));
script_value pi = engine.get_variable("PI");
bool exists = engine.has_variable("PI");
```

## Script-Side Classes (In Progress)

### Design Decisions Made

1. **Unified class_definition**: Extended existing `class_definition` to support both C++ and script classes
   - `class_type` enum distinguishes between `cpp_class` and `script_class`
   - Script methods compile to same `script_value` functions as C++ methods
   - Reuses existing method dispatch and field storage

2. **Hot-Reload Philosophy**: Always enabled for consistency
   - Identical structure reloads are essentially free (just pointer updates)
   - Only breaking change: inheritance modification (throws error)
   - Deleted classes: instances become orphaned but remain valid
   - ~10KB + 100ns overhead is negligible for dev/prod consistency

3. **Constructor/Method Storage**:
   - AST stored in `script_constructors_`, `script_methods_` for hot-reload
   - Compiled functions stored in existing `methods_` map
   - Constructors register as overloaded functions like C++ constructors

### Implementation Status

✅ **Completed:**
- Extended `class_definition` with script class support
- Added `class_instance::is_script_class()` helper methods
- Verified no existing tests broken

🚧 **Next Steps:**
1. Parser support for `class` keyword and class body
2. Interpreter `visit_class_decl` implementation
3. Constructor compilation and registration
4. Method compilation and binding

### Example Target Syntax

```jaiscript
class Enemy : Creature {
    damage = 10.0;
    
    // Constructor with base class delegation
    Enemy(script_string name) : super(name) {
        // Constructor body
    }
    
    // Method with override enforcement
    update(script_float dt) override {
        super::update(dt);
        // Enemy-specific logic
    }
}
```

## Important Reminders

1. **NO TEST BLOAT** - Use existing test infrastructure
2. **GDB FIRST** - Debug with GDB before creating minimal tests
3. **CLEAN UP** - Delete temporary test files immediately
4. **STANDARD FRAMEWORK** - Always use jai_test.hpp
5. **ORGANIZED TESTS** - Put tests in appropriate existing files

## Performance Benchmarks

Current performance vs ChaiScript:
- Engine creation: 12.2x faster
- Simple arithmetic: 578x faster
- Variable assignment: 293x faster
- Function definition: 173x faster
- Loops: 25.8x faster
- Nested functions: 57x faster

## Bytecode Virtual Machine Implementation

**Status**: Architecture complete, implementation in progress  
**Detailed Progress**: See `JaiScriptVMToDo.md` for comprehensive VM implementation roadmap

### VM Architecture ✅
- **execution_backend abstraction** - Clean interface allowing interpreter/VM backends
- **Bytecode instruction set** - 85+ opcodes covering all JaiScript features
- **Stack-based VM design** - Optimized for expression-heavy JaiScript syntax
- **Modular compilation** - AST-to-bytecode compiler with optimization passes

### Integration Strategy
- **Parallel development** - VM and interpreter coexist, allowing easy A/B testing
- **Drop-in replacement** - Engine API unchanged, existing code works without modification
- **Performance-based switching** - Automatic backend selection based on script complexity
- **Debugging support** - Both backends preserve line numbers and stack traces

### Next Phase: Implementation
Focus on minimal viable VM with core opcodes and basic compilation pipeline.

## Build Commands

### Foundry Tests (Recommended)

```bash
# Navigate to foundry tests
cd Source/JaiScript/tests/foundry

# Build and run all tests
make clean && make
./quench

# Build and run specific test in isolation (for multi-instance development)
make isolated-core/lexer_tests.cpp
make isolated-language/operator_tests.cpp
make isolated-integration/class_builder_tests.hpp

# Build all tests in a directory as isolated executables
make isolated-build-core
make isolated-build-language

# Debug with GDB
gdb ./test_isolated_core/parser_tests.cpp
(gdb) break test_name
(gdb) run

# Clean up
make clean          # Clean main build
make clean-isolated # Clean isolated executables
```

### Legacy Tests

```bash
# Build and run legacy tests (if needed)
cd Source/JaiScript/tests/legacy
make clean && make
./test_engine
./test_operators
```