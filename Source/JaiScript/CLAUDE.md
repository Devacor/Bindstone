# JaiScript Development Guide

High-performance C++-like scripting language with hot-reload support. Snake_case naming throughout.

**Key improvements over ChaiScript:**
- Natural property access: `obj.property` 
- Hot-reload support (planned)
- Implicit self in methods
- Automatic lambda captures
- Standard null checks: `!= null`

## Status

**Recent fixes:** Operator overloading, implicit type conversions, variable persistence, exception handling (interpreter only).
**Note:** Variable declarations need semicolons: `auto x = 42;`

### ✅ Completed

**Language:** Lexer/parser/interpreter, all operators (+,-,*,/,%,++,--,<=>), control flow (if/while/for/break/continue/try/catch), functions, lambdas with captures, string concatenation.

**Types:** Primitives (int64, double, char, bool, string), arrays `[1,2,3]`, maps `{"key": value}`, shared_ptr, references, functions. Automatic conversions.

**C++ Integration:** class_builder API, lambda methods, properties, constructor overloading, zero-copy const&, operator overloading.

**Performance:** string_symbolizer, fast arithmetic paths, move semantics.

**VM Backend:** Optional bytecode VM for large scripts (>1000 chars). `engine.set_backend(backend_type::jvm)`

**Standard Library:** `print()`, `to_string()`, `type_of()`, `to_json()`, `from_json()`. Use: `jai::stdlib::register_all(engine)`

### ❌ Missing

- Script-defined classes (`class` keyword)
- Switch/case statements  
- Range-based for loops
- Exception handling in VM (interpreter only)

## Testing

**CRITICAL:** Use GDB for debugging instead of creating minimal tests. Only create new tests in `tests/foundry/`.

```
tests/foundry/     # NEW TESTS HERE (use #include <jaiscript/testing/foundry.hpp>)
├── core/          # Engine, value, parser, lexer, type conversion, deep copy
├── language/      # Operators, functions, control flow, arrays, maps
├── integration/   # C++ bindings, serialization, shared_ptr
├── vm/            # Virtual machine specific tests
└── performance/   # Benchmarks and stress tests
tests/legacy/      # Old tests (migrating to foundry)
└── dev-tools/     # Debug utilities
```

### Foundry Framework (Multi-Instance Safe)

```cpp
#include <jaiscript/testing/foundry.hpp>
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
}
FOUNDRY_REGISTER(jai::foundry::tests::my_tests)  // Auto-registers or creates main()
```

**Building (ALWAYS USE ISOLATED BUILDS FOR CONCURRENT WORK):**
```bash
cd tests/foundry

# DEFAULT: Build single test (no .o conflicts between instances)
make isolated-core/value_semantics_tests.cpp
make isolated-language/operator_tests.cpp
make isolated-build-core     # All core tests as isolated executables
./isolated_lexer_tests

# ONLY when testing everything (avoid if others working):
make clean && make
./quench

make clean-isolated          # Cleanup isolated builds
```

**Auto-Registration:** FOUNDRY_REGISTER creates global registry entry OR main() function based on build mode. No manual registration needed.

**Concurrent Work Examples:**
```bash
# Instance 1: Working on parser
make isolated-core/parser_tests.cpp
gdb ./isolated_parser_tests

# Instance 2: Working on values (no conflicts!)
make isolated-core/value_semantics_tests.cpp

# Instance 3: Full suite (only when needed)
./quench "operator"  # Filter by name
```

## Development Workflow

**ALWAYS:** Build only your specific test. Use GDB. Don't create minimal repros.

**Adding Features:** Check existing tests first. Add to appropriate foundry category.

**Fixing Bugs:** Reproduce in existing test, use GDB, fix, add regression test to same file.

## Architecture

**Engine:** `globalEnvironment` (all globals), `nonSerializableGlobals` (exclude from serialization), `interpreter` (persistent, reused).

**Values:** Type-erased `std::variant`, custom class_instance extractor, move semantics, thread-local for reference params.

**class_builder:**
```cpp
make_class_builder<MyClass>(engine, "MyClass")
    .constructor<script_int, script_float>()
    .method("method_name", &MyClass::method_name)
    .method("chainable", [](MyClass& self) -> MyClass& { return self; })
    .property("prop", &MyClass::prop)
    .inherits<base_class>()
    .build();
```

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

## Script-Side Classes (In Progress)

**Design:** Unified `class_definition` for C++/script classes. Hot-reload always enabled (~10KB overhead). AST stored for reload.

**Status:** ✅ class_definition extended, is_script_class() helpers added. 🚧 Need: parser support, visit_class_decl, constructor/method compilation.

**Target syntax:**
```jaiscript
class Enemy : Creature {
    damage = 10.0;
    Enemy(script_string name) : super(name) { }
    update(script_float dt) override { super::update(dt); }
}
```

## Important Reminders

1. **NO TEST BLOAT** - Use existing test infrastructure
2. **GDB FIRST** - Debug with GDB before creating minimal tests  
3. **CLEAN UP** - Delete temporary test files immediately
4. **ORGANIZED TESTS** - Put tests in appropriate existing files

## Bytecode VM

**Status:** Architecture complete, implementation in progress (see `JaiScriptVMToDo.md`)

✅ **Done:** execution_backend abstraction, 85+ opcodes, stack-based design, AST→bytecode compiler framework

**Integration:** Parallel with interpreter, drop-in replacement, auto-switching by script size, full debug support

**Next:** Minimal VM with core opcodes

## Build Commands

```bash
cd Source/JaiScript/tests/foundry

# DEFAULT: Isolated builds (for concurrent work)
make isolated-core/lexer_tests.cpp
make isolated-language/operator_tests.cpp
make isolated-build-core            # All core tests isolated
./isolated_lexer_tests              # Executables have 'isolated_' prefix

# Full suite (avoid if others working)
make clean && make && ./quench
./quench "operator"                  # Filter by name

# Debug
gdb ./isolated_parser_tests

# Cleanup
make clean          # Main build
make clean-isolated # Isolated builds

# Legacy (if needed)
cd ../legacy && make && ./test_engine
```