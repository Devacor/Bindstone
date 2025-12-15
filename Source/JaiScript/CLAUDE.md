# JaiScript Development Guide

High-performance C++-like scripting language with hot-reload support. Snake_case naming throughout.

**Key improvements over ChaiScript:**
- Natural property access: `obj.property` 
- Hot-reload support (✅ implemented for script classes)
- Implicit self in methods
- Automatic lambda captures
- Standard null checks: `!= null`

## Status

**Recent fixes:** Operator overloading, implicit type conversions, variable persistence, exception handling (interpreter only), hot reload for script classes.
**Note:** Variable declarations need semicolons: `auto x = 42;`

### ✅ Completed

**Language:** Lexer/parser/interpreter, all operators (+,-,*,/,%,++,--,<=>), control flow (if/while/for/range-based for/break/continue/try/catch/switch/case/default), functions, lambdas with captures, string concatenation.

**Types:** Primitives (int64, double, char, bool, string), arrays `[1,2,3]`, maps `{"key": value}`, weak_ptr, references, functions. Automatic conversions.

**C++ Integration:** class_builder API, lambda methods, properties, constructor overloading, zero-copy const&, operator overloading.

**Performance:** string_symbolizer, fast arithmetic paths, move semantics.

**VM Backend:** Optional bytecode VM for large scripts (>1000 chars). `engine.set_backend(backend_type::jvm)`

**Standard Library:** `print()`, `to_string()`, `type_of()`, `to_json()`, `from_json()`. Use: `jai::stdlib::register_all(engine)` (supports both `engine&` and `shared_ptr<engine>`)

**Hot Reload:** Full support for script class redefinition with automatic instance migration, field preservation, method updates, and custom migration logic. Optimized with whole-class fingerprinting (20x speedup) and field-unchanged detection (60x speedup).

### ✅ Completed

**Conversion System Refactor (COMPLETED 2025-07-11):** Complete elimination of static state for proper multi-engine support
- ✅ Phase 1: Added engine reference infrastructure to script_value
- ✅ Phase 2: COMPLETE - Eliminated ALL default script_value construction (200+ locations fixed)
- ✅ Phase 3: COMPLETE - Engine context propagated throughout entire system
- ✅ **MAJOR ARCHITECTURAL ACHIEVEMENT**: Every script_value now requires an engine reference
- ✅ **Zero static state**: Full thread safety and multi-engine support achieved
- ✅ **Backward compatibility**: Maintained through serialization_tag constructors

### 🟢 Current Focus

- Performance optimization for hot paths
- Enhanced error messages and debugging experience

### ✅ Completed (2025-07-15)

**Switch/Case Statements (COMPLETED):** Full implementation with safety-first design
- ✅ **Break-by-default**: Each case automatically breaks unless `fallthrough` is used
- ✅ **Universal type support**: Works with any type that supports `operator==`
- ✅ **Context-aware parsing**: `fallthrough` keyword only valid inside switch cases
- ✅ **Explicit fallthrough**: Requires intentional `fallthrough` keyword for clarity
- ✅ **Nested switches**: Full support for nested switch statements

### ✅ Completed (2025-07-15)

**Range-based For Loops (COMPLETED):** Full implementation with C++11-style syntax
- ✅ **Array iteration**: `for (auto x : array)` with value and reference semantics
- ✅ **Map iteration**: `for (auto kv : map)` with key-value pair access via `kv.first`/`kv.second`
- ✅ **Reference support**: `for (auto& x : container)` allows modification of elements
- ✅ **Control flow**: Full `break` and `continue` support in range-based loops
- ✅ **Nested loops**: Complete support for nested range-based for loops

### ❌ Still Missing (Minor)

- Exception handling in VM backend (interpreter only - try/catch/throw works in interpreter)

## Testing

**Test Organization:**
```
tests/language/      # Foundry test suites (control flow, functions, operators, script classes, etc.)
tests/                # Test runner and additional test files
└── main_test_runner.cpp  # Foundry test auto-discovery and execution
```

### Foundry Framework (Multi-Instance Safe)

Tests use the custom Foundry framework for organized, auto-registered test suites.

```cpp
#include <jaiscript/testing/foundry.hpp>
using namespace jai::foundry;

namespace jai::foundry::tests {
class my_tests : public suite {
public:
    my_tests() : suite("My Test Suite") {}
    void forge_tests() override {
        test("example_test", [this]() {
            auto js_engine = engine::make();
            check_eq(js_engine->execute("2 + 2").as_int(), 4);
        });
    }
};
}
FOUNDRY_REGISTER(my_tests)  // Auto-registers for discovery
```

**Assertions:**
- `check(condition)` - Assert condition is true
- `check_eq(actual, expected)` - Assert equality
- Test fails on first failed assertion

## Development Workflow

**General Approach:**
1. Build the project in Visual Studio (or via command line)
2. Run tests to identify failures
3. Use Visual Studio debugger to investigate issues
4. Fix code and rebuild
5. Verify tests pass

**Adding Features:**
- Check existing tests first to understand patterns
- Add new tests to appropriate test file in `tests/language/`
- Follow existing Foundry framework patterns

**Fixing Bugs:**
- Reproduce in existing test or add new test case
- Use Visual Studio debugger (F5) to step through code
- Fix issue and verify test passes
- Add regression test to same file

## Architecture

**Engine:** `global_environment_` (all globals), `nonSerializableGlobals` (exclude from serialization), `interpreter` (persistent, reused).

**Values:** Type-erased `std::variant`, custom class_instance extractor, move semantics, thread-local for reference params.

**class_builder:**
```cpp
class_builder<MyClass>(engine, "MyClass")
    .constructor<script_int, script_float>()
    .method("method_name", &MyClass::method_name)
    .method("chainable", [](MyClass& self) -> MyClass& { return self; })
    .property("prop", &MyClass::prop)
    .base_class<BaseClass>()  // Enables inheritance and polymorphism
    .build();
```

**Polymorphism:** When `base_class<Base>()` is called, derived class instances can be passed to functions expecting `shared_ptr<Base>`. Upcasting is automatic via `static_pointer_cast`.

**Performance:** 
- string_symbolizer: name→ID mapping (NOT deterministic, engine-bound, NEVER share between instances or serialize)
- Fast arithmetic paths, move semantics, vector-based value_stack

## Common Patterns

```cpp
// Script execution
engine engine;
script_value result = engine.execute("2 + 2");
script_int val = result.as<script_int>();

// Function registration
engine.add_function("square", [](script_int n) -> script_int { return n * n; });
engine.add_variadic_function("print", [](const std::vector<script_value>& args) { /*...*/ });

// Variable access
engine.add_global("PI", script_value(3.14159));
script_value pi = engine.get_variable("PI");
bool exists = engine.has_variable("PI");
```

## Script Classes (✅ FULLY IMPLEMENTED)

**Status:** Complete native script class system with inheritance, constructors, and hot reload!

**Working syntax:**
```jaiscript
class Cat {
    int a = 0;
    Cat(int val) { a = val; }
}

class Tiger : Cat {
    int b = 5;
    Tiger() : super(5) {}
    
    void roar() {
        print("ROAR! Tiger with a=" + to_string(a));
    }
}

auto tiger = Tiger();
tiger.roar(); // Works!
```

**Switch Statements (✅ COMPLETED 2025-07-15):**
```jaiscript
// Break-by-default safety design
switch (weapon_type) {
    case "sword":
        damage = 10;        // Implicit break
    case "bow":
        damage = 8;         // Implicit break
    case "magic":
        damage = 15;
        fallthrough;        // Explicit fallthrough required
    case "enchanted":
        damage += 5;        // Executes for both magic and enchanted
    default:
        damage = 5;
}
```

**Range-based For Loops (✅ COMPLETED 2025-07-15):**
```jaiscript
// Array iteration
auto numbers = [1, 2, 3, 4, 5];
for (auto x : numbers) {
    print(x);  // Prints each number
}

// Array modification with references
for (auto& x : numbers) {
    x *= 2;    // Doubles each element in-place
}

// Map iteration with key-value pairs (using proper pair type)
auto scores = {"alice": 100, "bob": 85, "charlie": 92};
for (auto kv : scores) {
    print(kv.first + ": " + to_string(kv.second));  // alice: 100, etc.
}

// Map modification with references  
for (auto& kv : scores) {
    kv.second += 10;  // Add 10 bonus points to each score
}
```

**Features:**
- Full inheritance with `super()` calls
- Constructor overloading
- Method definitions and calls
- Field initialization with defaults
- Hot reload preserves instances automatically
- Mixed script/C++ inheritance supported

**Performance:** Script classes integrate seamlessly with optimized hot reload system (<10ms for 100 instances).

## Important Reminders

1. **Use Existing Test Infrastructure** - Don't create new test files unnecessarily
2. **Debug First** - Use Visual Studio debugger to investigate issues before adding tests
3. **Organized Tests** - Add tests to appropriate existing files in `tests/language/`
4. **Build System** - Always build via Visual Studio or CMake, executable is in `out/build/x64-Debug/bin/`

## Code Guidelines (CRITICAL)

**Avoid static/thread_local storage - causes test pollution between test runs:**
- Do NOT use `static thread_local` variables
- Do NOT use `thread_local` variables
- Do NOT use non-const `static` variables
- `static const` (non-pointer) values are acceptable

**Instead use:**
- Instance members on classes
- Pass values via engine reference
- Use RAII patterns with engine-bound storage (see `parameter_storage` pattern)

**script_value construction:**
- Always create `script_value` with an engine reference when possible
- Use `script_value(value, engine_weak_ptr)` pattern
- Only use `script_value(std::monostate{}, eng)` for null values

## Bytecode VM

**Status:** Architecture complete, implementation in progress (see `JaiScriptVMToDo.md`)

✅ **Done:** execution_backend abstraction, 85+ opcodes, stack-based design, AST→bytecode compiler framework

**Integration:** Parallel with interpreter, drop-in replacement, auto-switching by script size, full debug support

**Next:** Minimal VM with core opcodes

## Build System

### Primary Build System: Visual Studio 2022 + CMake + Ninja

**Overview:**
- The project uses CMake as the build system with Ninja as the generator
- Visual Studio 2022 is used to open the CMakeLists.txt folder directly
- CMake automatically generates build files in `out/build/x64-Debug/` (or other configurations)
- The test executable is built as `bin/jaiscript_tests.exe`

**Opening the Project:**
1. Open Visual Studio 2022
2. File → Open → Folder
3. Navigate to `D:\git\Bindstone\Source\JaiScript`
4. VS will automatically detect CMakeLists.txt and configure the project

**Building in Visual Studio:**
1. Select build configuration (usually `x64-Debug`) from the dropdown
2. Build → Build All (or Ctrl+Shift+B)
3. To build just tests: Right-click CMakeLists.txt → Build → jaiscript_tests

**Build Output Location:**
- Debug builds: `D:\git\Bindstone\Source\JaiScript\out\build\x64-Debug\bin\jaiscript_tests.exe`
- Release builds: `D:\git\Bindstone\Source\JaiScript\out\build\x64-Release\bin\jaiscript_tests.exe`

### Running Tests

**From Visual Studio:**
- Debug → Start Without Debugging (Ctrl+F5) to run tests
- Or use Test Explorer if configured

### Performance Benchmarks

**IMPORTANT:** Benchmarks are disabled by default. To run performance benchmarks:

1. **In Visual Studio:** Select the `x64-Release BENCHMARKS` configuration from the build configuration dropdown
2. This enables `JAISCRIPT_ENABLE_BENCHMARKS=ON` which includes performance test suites
3. Build and run `jaiscript_tests.exe` - benchmark results will be included in output

**Benchmark categories:**
- Core performance (integer/float/variable/function operations)
- ChaiScript comparison (head-to-head benchmarks)
- Loop performance (for-loop variants, range-for, strong types)
- Container operations (array, map)
- Value construction and type checking

**From Command Line:**
```powershell
# Navigate to build output directory
cd D:\git\Bindstone\Source\JaiScript\out\build\x64-Debug\bin

# Run all tests
.\jaiscript_tests.exe

# Filter by suite name (substring match)
.\jaiscript_tests.exe "Script Class"

# Filter by test name (substring match)
.\jaiscript_tests.exe "destructor"

# gtest-style filter (for AI/automation compatibility)
.\jaiscript_tests.exe --gtest_filter="Property.*"

# jaitest-style filter (synonym for gtest)
.\jaiscript_tests.exe --jaitest_filter="Hot Reload.*"

# Explicit filter parameter
.\jaiscript_tests.exe --filter="*.post_deserialize*"

# Suite.Test pattern (filter both suite and test)
.\jaiscript_tests.exe "Property.post_deserialize"
```

**Filter Syntax:**
- `pattern` - Matches both suite names AND test names (substring match)
- `Suite.Test` - Dot separates suite filter from test filter
- `*` - Wildcard matches any sequence of characters
- `?` - Wildcard matches single character
- No wildcards = substring match

**Supported Filter Arguments:**
- `--gtest_filter=pattern` - Google Test compatible
- `--jaitest_filter=pattern` - JaiScript native (synonym)
- `--filter=pattern` - Explicit filter
- `pattern` - Positional argument

**Test Output:**
- Tests auto-discover and run all registered Foundry test suites
- Filter applies to both suite names and test names
- Skipped tests are counted and reported
- Returns exit code 0 on success, non-zero on failure

### Command Line Build (Claude Code Compatible)

These commands work from PowerShell and properly set up the VS environment:

**Release build:**
```powershell
powershell.exe -Command "& cmd /c '`"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript && cmake --build out/build/x64-Release --config Release 2>&1'"
```

**Debug build:**
```powershell
powershell.exe -Command "& cmd /c '`"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript && cmake --build out/build/x64-Debug --config Debug 2>&1'"
```

**Run all tests (Release):**
```powershell
powershell.exe -Command "& cmd /c '`"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript\out\build\x64-Release && bin\jaiscript_tests.exe 2>&1'"
```

**Run specific test case:**
```powershell
powershell.exe -Command "& cmd /c '`"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript\out\build\x64-Release && bin\jaiscript_tests.exe --test-case=`"test_name`" 2>&1'"
```

**Important:** Do NOT use plain `cmake` commands without the VsDevCmd.bat environment - standard includes like `<iostream>` won't be found.

### Alternative: Batch File Build

```batch
REM Set up Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

REM Navigate to build directory
cd /d D:\git\Bindstone\Source\JaiScript\out\build\x64-Debug

REM Build everything
cmake --build .

REM Or build just tests
cmake --build . --target jaiscript_tests

REM Run tests
.\bin\jaiscript_tests.exe
```

### Legacy: Linux/WSL Build (No Longer Primary)

**Note:** The project previously used WSL for development but now primarily uses Windows native builds.

If needed, Linux builds can still be done using Make:
```bash
cd Source/JaiScript/tests/foundry
make isolated-core/lexer_tests.cpp
./isolated_lexer_tests
```

---

# JaiScript LLM Context Summary

## What JaiScript Is
- High-performance C++-like scripting language for game engines
- 4-72x faster than ChaiScript across all benchmarks  
- Complete object-oriented programming with native script classes
- Production-ready hot reload system with advanced optimizations
- Seamless C++ integration via sophisticated class_builder API

## Fully Working Features (2024)
**Core Language:** Complete lexer/parser/interpreter with all C++ operators including ternary (`?:`), bitwise (`|`, `^`, `&`), arithmetic (`+`, `-`, `*`, `/`, `%`, `++`, `--`), comparison (`<`, `>`, `<=`, `>=`, `==`, `!=`, `<=>`), logical (`&&`, `||`, `!`), assignment (`=`, `+=`, `-=`, etc.).

**Type System:** Primitives (int64, double, string, char, bool), collections (arrays `[1,2,3]`, maps `{"key": value}`), smart pointers (weak_ptr), references, functions. Automatic type conversions.

**Control Flow:** if/else, while loops, for loops, range-based for loops, break/continue, try/catch/throw exception handling, switch/case/default statements with break-by-default semantics.

**Functions & Lambdas:** Function definitions, lambda expressions with captures `[=]() -> auto {}`, variadic functions.

**Script Classes (COMPLETE):** Native class syntax with inheritance:
```jaiscript
class Cat { 
    int a = 0; 
    Cat(int val) { a = val; } 
}
class Tiger : Cat { 
    Tiger() : super(5) {} 
    void roar() { print("ROAR!"); }
}
```

**C++ Integration:** Full class_builder API for exposing C++ classes with constructors, methods, properties, inheritance. Zero-copy bindings.

**Hot Reload:** Production-grade system with:
- Automatic instance migration (preserves field values)
- Method redefinition (updates all implementations)  
- Advanced optimizations: 20x speedup (fingerprinting), 60x speedup (field-unchanged detection)
- Custom migration via `hot_reload_migrate()` method
- Performance: <10ms for 100 instances, <100μs for identical classes

**VM Backend:** Optional bytecode virtual machine for large scripts (>1000 chars), auto-switching.

**Standard Library:** print(), to_string(), type_of(), JSON support (to_json/from_json).

## Performance Benchmarks vs ChaiScript
- Engine creation: 3.1x faster (1,608μs vs 4,955μs)
- Simple arithmetic: 4.2x faster (interpreter), 7.4x faster (VM)  
- Variable assignment: 15.5x faster (interpreter), 54.6x faster (VM)
- Function calls: 21.7x faster (interpreter), 63.5x faster (VM)
- Class operations: 4.9x faster

## Current Limitations (Minor)
- Exception handling in VM backend (works fully in interpreter)
- Coroutines/generators (not planned)
- JIT compilation (bytecode VM only)

## Recent Major Achievements (2025-07-11)
**Conversion System Refactor - COMPLETED:** Complete elimination of static state for proper multi-engine support
- ✅ **Architectural Revolution**: Eliminated ALL default script_value construction (200+ locations)
- ✅ **Engine Reference Infrastructure**: Every script_value now has mandatory engine context
- ✅ **Multi-Engine Support**: Complete thread safety and isolation achieved
- ✅ **Zero Static State**: No global or static conversion state remaining
- ✅ **Helper Functions**: Created `get_engine_weak_ptr()` for template compatibility
- ✅ **Template Safety**: Fixed incomplete type issues in template code

## Recent Work (2025)
**Completed Major Systems:**
- ✅ Zero static state architecture - All tests passing
- ✅ Switch/case statements with break-by-default semantics
- ✅ Range-based for loops with C++11 style syntax
- ✅ Performance optimizations (string symbolizer, type check optimization)

## File Structure
- `include/jaiscript/` - Headers (core/, detail/, jvm/, stdlib/, testing/)
- `source/implementation/` - Core implementation (engine.cpp, interpreter.cpp, parser.cpp, etc.)
- `tests/language/` - Foundry test suites (script classes, control flow, functions, operators, etc.)
- `tests/main_test_runner.cpp` - Test auto-discovery and runner

## Testing Approach
- Tests are in `tests/language/` using Foundry framework
- Build in Visual Studio 2022 (opens CMakeLists.txt folder)
- Test executable: `out/build/x64-Debug/bin/jaiscript_tests.exe`
- Run all: `.\jaiscript_tests.exe` or run with filter: `.\jaiscript_tests.exe "Script Class"`
- Debug with Visual Studio debugger (F5)
- Avoid creating new test files - use existing test infrastructure

## Key APIs
```cpp
// Basic usage
jai::engine engine;
auto result = engine.execute("2 + 2");
int value = result.as<int>();

// C++ class binding
class_builder<MyClass>(engine, "MyClass")
    .constructor<int, float>()
    .method("method", &MyClass::method)
    .property("field", &MyClass::field)
    .build();

// Script class usage (works natively!)
engine.execute(R"(
    class GameEntity {
        float health = 100.0;
        GameEntity(float h) { health = h; }
        void takeDamage(float damage) { health -= damage; }
    }
    auto player = GameEntity(150.0);
    player.takeDamage(25.0);
)");
```

## Current State
JaiScript has achieved production-ready status with complete script class support, exceptional performance, and industry-leading hot reload capabilities. The language is feature-complete for game scripting with comprehensive test coverage and clean, modern C++20 architecture. All major systems are implemented and tested, with ongoing focus on performance optimization and enhanced developer experience.