# JaiScript

A high-performance embedded scripting language with C++-like syntax designed for game engines.
Two swappable execution backends (tree-walking interpreter and bytecode VM) at full test parity,
seamless C++ integration, hot reload, and C++ standard library naming conventions (snake_case
throughout).

## Why JaiScript?

JaiScript was born from real-world game development frustrations with existing embedded scripting solutions. After extensive use of ChaiScript in production, we identified critical pain points that hindered rapid game development:

- **No natural property access** - Forced to use `self["property"]` instead of `self.property`
- **No hot-reload** - "Already defined" errors require full engine restart *(✅ Solved for script classes - see hot reload feature below)*
- **Explicit self-passing** - Methods need `fun(self, dt)` with manual `self` passing
- **Limited closures** - Manual capture lists `fun[var1, var2]` instead of automatic capture
- **Poor null handling** - Awkward `.is_var_null()` instead of simple `!= null`

See [why_jaiscript.md](why_jaiscript.md) for detailed real-world examples and comparisons.

JaiScript solves these issues while maintaining the performance and C++ integration that game engines require.

## Features

- **Two Backends, Full Parity** - Tree-walking interpreter (default) and a bytecode VM; select
  per engine with `engine->set_backend(jai::backend_type::vm)` before the first execute, or run
  the whole test suite on the VM with `jaiscript_tests.exe --backend=vm`
- **Pre-Parsed Scripts (jaibite)** - Parse once, execute repeatedly; binary save/load of parsed
  scripts (`jaibite()`, `jaibite_load()`), with the VM's compiled bytecode cached inside the bite
- **Opt-In Static Checking** - `off`/`warn`/`strict` modes (`engine->static_checking(...)`); in
  strict mode type errors throw before anything runs — see `static_checking.md`
- **Sandboxing Limits** - Wall-clock execution budgets and a script memory cap; budget overruns
  are TERMINAL (skip every script `catch`, always reach the host)
- **Modern Language Features** - Lambdas with captures, coroutines (functions AND methods) with
  `yield`, template strings (`` `hp: ${x}` ``), `include`/`import`, destructuring, enums,
  namespaces, operator overloading, default arguments
- **Real Reference Semantics** - Lvalue reference arguments (`f(obj.field)`, `f(arr[i])`),
  reference returns/locals, `shared_ptr`/`weak_ptr` script types
- **Typed Fields and Locals** - Typed class fields are enforced like typed locals; the
  `int`/`auto`/`var` ladder gives you exactly as much typing as you want
- **Seamless C++ Integration** - Clean `dynamic_binder` API for exposing C++ types
- **Hot Reload for Script Classes** - Redefine script classes at runtime with automatic instance
  migration
- **Zero Dependencies** - Standalone implementation, no external libraries

## Quick Start

```cpp
#include <jaiscript/core/engine.hpp>

int main() {
    auto engine = jai::engine::make();   // engines are always shared_ptr (ctor is private)

    // Execute script with modern syntax
    auto result = engine->execute(R"(
        // Variables with type inference
        auto x = 10;
        auto y = 20;

        // Lambdas use script-side type names (int, float, string, ...)
        auto add = [](int a, int b) -> int { return a + b; };

        // Use the function
        add(x, y)
    )");

    std::cout << "Result: " << result.as_int() << std::endl; // 30
    return 0;
}
```

Optional: run on the bytecode VM instead of the interpreter (must be set before the first
execute):

```cpp
auto engine = jai::engine::make();
engine->set_backend(jai::backend_type::vm);
```

## Language Overview

### Variables and Types
```javascript
int x = 42;              // 64-bit signed integer
float pi = 3.14159;      // 64-bit double precision
string name = "Game";    // UTF-8 string
bool active = true;      // Boolean
auto count = 0;          // Type inference with locking
var dynamic = 5;         // Dynamic typing (any type allowed)
```

### auto vs var
```javascript
// auto - type inference, locks to inferred type
auto x = 5;              // Locked to int
x = 10;                  // OK
x = 3.9;                 // OK: truncates to 3 (C++-style conversion)
x = "hello";             // ERROR: type mismatch

// var - dynamic typing, any type allowed
var y = 5;               // Any type
y = "hello";             // OK: var allows any type

// Container behavior
auto nums = [1, 2, 3];           // Homogeneous required
auto mixed = [1, "two"];         // ERROR: mixed types not allowed
var flexible = [1, "two", 3.14]; // OK: var allows mixed
```

### Functions
```javascript
// Multiple declaration styles supported
int multiply(int a, int b) { return a * b; }
auto divide(float a, float b) -> float { return a / b; }
function greet(string name, string greeting = "Hello") {  // default arguments
    print(greeting + ", " + name);
}

// Lambda expressions with captures
int multiplier = 10;
auto scale = [multiplier](int x) -> int { return x * multiplier; };

// Reference parameters bind lvalue arguments (locals, obj.field, arr[i])
void heal(Creature& c, int amount) { c.health += amount; }
heal(party[0], 25);
```

### Coroutines
```javascript
coroutine function counter(int limit) {
    for (int i = 0; i < limit; i++) { yield i; }
}
for (auto n : counter(3)) { print(n); }   // range-for drives the coroutine

// Coroutine METHODS work too
class Spawner {
    coroutine function waves(int count) {
        for (int i = 0; i < count; i++) { yield i; }
    }
}
```

### Template Strings
```javascript
auto name = "Dragon";
print(`The ${name} has ${hp * 2} hp`);   // backtick + ${expr} interpolation
```

### Include / Import
```javascript
include "setup.jai";     // PHP-template style: always runs, may produce a value
import "creature.jai";   // module style: cached per path, declarations only, never a value
```

### Control Flow
```javascript
if (health <= 0) {
    print("Game Over");
} else {
    print("Health: " + to_string(health));
}

for (int i = 0; i < 10; ++i) {
    sum += i;
    if (sum > 20) {
        break;      // Exit loop
    }
    if (i % 2 == 0) {
        continue;   // Skip to next iteration
    }
}

while (running) {
    update();
}

// Switch statements with break-by-default safety
switch (weapon_type) {
    case "sword":
        damage = 10;
    case "bow":
        damage = 8;
    case "magic":
        damage = 15;
        fallthrough;  // Explicit fallthrough required
    case "enchanted":
        damage += 5;  // Magic weapons get bonus
    default:
        damage = 5;
}
```

### Exception Handling
```javascript
// Try-catch with exception variable — e is the THROWN VALUE with its type preserved
try {
    throw "Something went wrong!";
} catch (e) {
    print("Caught error: " + e);
}

// Catch without variable
try {
    risky_operation();
} catch {
    print("An error occurred");
}

// Re-throw support
try {
    try {
        throw "Inner error";
    } catch (e) {
        print("Inner catch: " + e);
        throw;  // Re-throw the same exception
    }
} catch (e) {
    print("Outer catch: " + e);
}

// Throw any value type — catch (e) receives it typed, not stringified
throw 42;                   // Numbers
throw "Error message";      // Strings
throw {code: 404};         // Objects
```

Both backends support exceptions fully. Terminal errors (execution-budget exhaustion, latched
memory-cap) skip every script `catch` and surface at the host — see `EXCEPTION_DESIGN.md`.

### Operators
- **Arithmetic**: `+`, `-`, `*`, `/`, `%`
- **Compound**: `+=`, `-=`, `*=`, `/=`, `%=`
- **Increment**: `++`, `--` (prefix and postfix)
- **Comparison**: `<`, `>`, `<=`, `>=`, `==`, `!=`
- **Spaceship**: `<=>` (three-way comparison)
- **Logical**: `&&`, `||`, `!`
- **Bitwise**: `&`, `|`, `^`, `~`, `<<`, `>>`
- **Null-safe access**: `?.`
- **Ternary**: `condition ? true_val : false_val`
- **Control Flow**: `break`, `continue`, `switch`/`case`/`default`/`fallthrough` (break-by-default semantics)
- **Custom Operator Overloading**: `= + - * / [] < > <= >= == !=` on script classes

### Arrays and Maps
```javascript
// Array literals and operations
auto numbers = [1, 2, 3, 4, 5];
numbers.push(6);
int first = numbers[0];
int size = numbers.size();

// Map literals — both styles parse
auto scores = {
    {"player1", 100},          // C++ style entries
    {"player2", 85}
};
auto point = {x: 10, y: 20};   // JSON style (bare keys become strings)
scores["player3"] = 90;
int p1Score = scores["player1"];

// Map methods
bool has_player = scores.contains("player1");
auto all_keys = scores.keys();
scores.erase("player2");
```

### Reference Types (shared_ptr / weak_ptr)
```javascript
// By default, objects use VALUE semantics (copy on assign)
auto a = Enemy();
auto b = a;           // b is a deep copy
b.health = 50;        // Only b changes

// Use shared_ptr for REFERENCE semantics (shared mutation)
auto a = shared_ptr<Enemy>();
auto b = a;           // b shares with a
b.health = 50;        // Both a and b see the change!

// weak_ptr for non-owning references (breaks cycles)
class Node {
    weak_ptr<Node> parent;  // Won't prevent parent from being deleted
    auto children = [];
}

// Auto-unwrap assignment: shared_ptr<T> delegates to T's operator=
class Counter {
    int value = 0;
    auto operator=(int v) { value = v; }
}
auto ptr = shared_ptr<Counter>();
ptr = 42;             // Calls Counter::operator=(42), not pointer reassign
```

See `TYPE_SYSTEM_DESIGN.md` for complete reference semantics documentation.

## Hot Reload Support (Script Classes)

JaiScript provides comprehensive hot reload support for script-defined classes, allowing you to redefine classes at runtime while preserving existing instances:

```javascript
// Initial class definition
class Enemy {
    auto health = 100;
    auto damage = 10;
    
    void attack(target) {
        target.health -= damage;
    }
}

// Create instances
auto goblin = Enemy();
goblin.health = 50;

// Redefine class at runtime - instances automatically migrated!
class Enemy {
    auto health = 100;    // Preserved: goblin.health still 50
    auto armor = 0;       // New field: gets default value
    auto damage = 15;     // Changed default: existing instances keep old value
    // 'damage' field removed after migration
    
    void attack(target) {
        auto actual_damage = 15 - target.armor;  // New logic
        target.health -= actual_damage;
    }
    
    // Optional: Custom migration logic
    void hot_reload_migrate() {
        // Access old fields before they're removed
        if (this.damage > 12) {
            this.armor = 5;  // Set new field based on old data
        }
    }
}

// After redefinition:
// goblin.health == 50 (preserved)
// goblin.armor == 5 (set by migration)
// goblin.attack() uses new implementation
```

### Hot Reload Features:
- **Automatic Field Migration**: Fields with same names keep their values
- **Method Updates**: All methods replaced with new implementations
- **Custom Migration**: Optional `hot_reload_migrate()` method for complex transformations
- **Instance Tracking**: All instances (including clones) are automatically migrated
- **Performance**: Optimized single-pass migration, typically <10ms for 100 instances

## C++ Integration

### Exposing C++ Classes

JaiScript provides a fluent `dynamic_binder` API for exposing C++ classes to scripts. The API supports two styles: manual registration for full control, or automatic registration using the `property_owner` CRTP pattern.

#### Manual Registration (Full Control)
```cpp
// Clean dynamic_binder API - 60% less code than ChaiScript
jai::dynamic_binder<Vector2>(engine, "Vec2")
    .constructor<script_float, script_float>()
    .constructor<script_int, script_int>()  // Automatic type conversion
    .property("x", &Vector2::x)
    .property("y", &Vector2::y)
    .method("length", &Vector2::length)
    .method("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2{a.x + b.x, a.y + b.y};
    })
    .build();  // Or let destructor call build() automatically

// Automatic type conversions work seamlessly
// In script: auto v = Vec2(3, 4);  // int to double conversion handled
```

#### Inheritance with base_class<>()
```cpp
// Register base class first
jai::dynamic_binder<Entity>(engine, "Entity")
    .constructor<>()
    .property("id", &Entity::id)
    .method("get_name", &Entity::get_name);

// Register derived class with inheritance
jai::dynamic_binder<Player>(engine, "Player")
    .base_class<Entity>()  // Inherits Entity's methods/properties
    .constructor<>()
    .property("health", &Player::health)
    .method("attack", &Player::attack);

// Multiple inheritance - base_class<>() appends, doesn't replace
jai::dynamic_binder<Wizard>(engine, "Wizard")
    .base_class<Player>()      // First base
    .base_class<SpellCaster>() // Second base - methods from both accessible
    .constructor<>()
    .method("cast_spell", &Wizard::cast_spell);

// Note: Diamond inheritance is detected and throws std::runtime_error
```

#### Auto-Binding with property_owner (Recommended for Complex Hierarchies)

For classes using the `property_owner<Derived, Bases...>` CRTP pattern, `auto_bind()` automatically registers base classes:

```cpp
// Define classes using property_owner CRTP
class Entity : public jai::property_owner<Entity> {
public:
    JAI_PROPERTY((int), id, 0);
    std::string get_name() const { return "Entity"; }
};

class Player : public jai::property_owner<Player, Entity> {
public:
    JAI_PROPERTY((int), health, 100);
    void attack() { /* ... */ }
};

class Wizard : public jai::property_owner<Wizard, Player> {
public:
    JAI_PROPERTY((int), mana, 50);
    void cast_spell() { /* ... */ }
};

// Register with auto_bind() - base classes AND default constructor registered automatically!
jai::dynamic_binder<Entity>(engine, "Entity")
    .auto_bind()  // Registers default constructor + to_string/size/empty if present
    .method("get_name", &Entity::get_name);

jai::dynamic_binder<Player>(engine, "Player")
    .auto_bind()  // Auto-registers Entity as base + default constructor
    .method("attack", &Player::attack);

jai::dynamic_binder<Wizard>(engine, "Wizard")
    .auto_bind()  // Auto-registers Player as base -> Entity chain followed
    .method("cast_spell", &Wizard::cast_spell);

// In script:
// auto w = Wizard();
// w.cast_spell();  // Direct method
// w.attack();      // Inherited from Player
// w.get_name();    // Inherited from Entity via Player
// w.health = 50;   // Property from Player
// w.mana = 100;    // Direct property
```

#### auto_bind() Modes

`auto_bind()` takes a `jai::bind_mode` controlling what gets auto-registered
(`all` / `properties` / `hierarchy`):

```cpp
// bind_mode::all (default) - Base classes + properties + auto-detected methods/constructors
// Auto-detects: default constructor, to_string(), size(), empty(), operator==/!=
jai::dynamic_binder<MyClass>(engine, "MyClass")
    .auto_bind(jai::bind_mode::all);  // or just .auto_bind()

// bind_mode::properties - Base classes + properties only (no auto methods/constructors)
jai::dynamic_binder<MyClass>(engine, "MyClass")
    .auto_bind(jai::bind_mode::properties)
    .constructor<>()  // Manual constructor since not auto-detected
    .method("custom_method", &MyClass::custom_method);

// bind_mode::hierarchy - Base classes only (no properties or auto methods)
jai::dynamic_binder<MyClass>(engine, "MyClass")
    .auto_bind(jai::bind_mode::hierarchy)
    .constructor<>()
    .property("hand_picked", &MyClass::hand_picked);
```

#### Multiple Inheritance with auto_bind()

```cpp
// Two independent base classes
class Renderable : public jai::property_owner<Renderable> {
public:
    void draw() { /* ... */ }
};

class Collidable : public jai::property_owner<Collidable> {
public:
    bool check_collision() { return true; }
};

// Multiple inheritance - both bases in the tuple
class Sprite : public jai::property_owner<Sprite, Renderable, Collidable> {
public:
    void update() { /* ... */ }
};

// Registration - auto_bind() registers default constructor automatically
jai::dynamic_binder<Renderable>(engine, "Renderable")
    .auto_bind()
    .method("draw", &Renderable::draw);

jai::dynamic_binder<Collidable>(engine, "Collidable")
    .auto_bind()
    .method("check_collision", &Collidable::check_collision);

jai::dynamic_binder<Sprite>(engine, "Sprite")
    .auto_bind()  // Registers BOTH bases + default constructor
    .method("update", &Sprite::update);

// In script:
// auto s = Sprite();
// s.draw();            // From Renderable
// s.check_collision(); // From Collidable
// s.update();          // Direct method
```

#### When to Use Each Approach

| Approach | Use When |
|----------|----------|
| Manual `base_class<>()` | Simple classes, no property system, full control needed |
| `auto_bind()` (default) | Classes using `property_owner`, want default constructor + common methods |
| `auto_bind(bind_mode::properties)` | Need base classes + properties but manual constructor/method control |
| `auto_bind(bind_mode::hierarchy)` | Need only the base-class chain wired up |

**What auto_bind(bind_mode::all) auto-detects:**
- Default constructor (if `std::is_default_constructible_v<T>`)
- `to_string()` method (if present)
- `size()` method (if present)
- `empty()` method (if present)
- `operator==` and `operator!=` (if `operator==(const T&, const T&)` is defined)
- Base classes from `_jai_base_types` (if using `property_owner<T, Bases...>`)
- A `static T::jai_auto_bind(binder)` hook (if present) for registering private members

### Adding Functions
```cpp
// Simple function binding
engine->add_function("sqrt", [](script_float x) -> script_float {
    return std::sqrt(x);
});

// Variadic functions
engine->add_variadic_function("print", [](const std::vector<script_value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.to_string();
    }
    std::cout << std::endl;
    return script_value();
});
```

### Zero-Copy Parameters
```cpp
// Const references avoid copies
engine->add_function("processString", [](const script_string& str) {
    // Direct access to script string - no copy
});

// Custom types work the same way
engine->add_function("distance", [](const Vec2& a, const Vec2& b) -> script_float {
    // No copies made of Vec2 objects
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
});
```

## Building

JaiScript is a compiled implementation (headers in `include/jaiscript/`, sources in
`source/implementation/`) built with CMake. Include the umbrella header
`#include <jaiscript/jaiscript.hpp>` (or individual headers like `core/engine.hpp`) and link
the library.

- Standalone build, options, and test instructions: see [BUILD.md](BUILD.md)
- Day-to-day development workflow (Debug/Release configs, test filtering, `--backend=vm`,
  building against MutedVision/Bindstone): see `Source/JaiScript/CLAUDE.md`
- Test framework details: see [JaiScriptTesting.md](JaiScriptTesting.md) and
  [JaiScriptVMTesting.md](JaiScriptVMTesting.md)

## API Reference

### engine Class
```cpp
class engine {
    // Construction — the constructor is private; engines are always shared_ptr
    static std::shared_ptr<engine> make();

    // Script execution
    script_value execute(const std::string& code);
    script_value execute_file(const std::string& path);
    script_value execute(const std::string& code, const instance_variables& locals);

    // Pre-parsed scripts (parse once, run many; binary save/load via jaibite::save)
    jai::jaibite jaibite(const std::string& code);
    script_value execute(jai::jaibite& bite);
    jai::jaibite jaibite_load(const std::string& path);

    // Backend selection (before the first execute)
    void set_backend(backend_type type);   // interpreter (default) or vm

    // Static checking (off/warn/strict) — see static_checking.md
    void static_checking(check_mode mode);
    check_report check(const std::string& code);

    // Sandboxing
    void execution_budget(double seconds);  // terminal on overrun; default 1.0
    void memory_cap(size_t bytes);          // 0 = unlimited (default)

    // Variable management
    void add_global(const std::string& name, script_value value);
    template<typename T> void add_global_ref(const std::string& name, T& value);
    script_value get_variable(const std::string& name) const;
    bool has_variable(const std::string& name) const;

    // Function registration
    template<typename Func> void add_function(const std::string& name, Func&& func);
    void add_variadic_function(const std::string& name, script_function func);

    // Type registration — normally done through jai::dynamic_binder<T>(engine, "Name"),
    // which calls this under the hood:
    void add_class(const std::string& name, std::shared_ptr<class_definition> classDef);
};
```

### script_value Class
```cpp
class script_value {
    // Type checking
    bool is_null() const;
    bool is_int() const;
    bool is_float() const;
    bool is_string() const;
    bool is_bool() const;
    bool is_array() const;
    bool is_map() const;
    bool is_function() const;
    
    // Value extraction
    template<typename T> T as() const;
    script_int as_int() const;
    script_float as_float() const;  
    const script_string& as_string() const;
    script_bool as_bool() const;
    
    // Conversions
    script_string to_string() const;
};
```

## Standard Library

JaiScript includes a growing standard library with essential functions. To use the standard library:

```cpp
#include <jaiscript/stdlib/stdlib.hpp>

// Register all stdlib functions
jai::stdlib::register_all(engine);

// Or register individual modules
jai::stdlib::register_io_functions(engine);
jai::stdlib::register_json_functions(engine);
```

### I/O Functions

#### `print(...)`
Flexible output function that supports both formatted output and sequential concatenation.

**Usage:**
- `print()` - Prints just a newline
- `print(value)` - Prints value followed by newline
- `print(format, args...)` - If format contains `{}`, uses format-style
- `print(arg1, arg2, ...)` - If no `{}` in first arg, prints all args sequentially
- `print(..., skip_newline)` - Skips the newline at end
- `print(..., skip_flush)` - Skips both newline and flush

**Format String Mode (activated when first arg contains `{}` or `{n}` where n is a number):**
- `{}` - Sequential placeholder, replaced with next argument
- `{n}` - Positional placeholder, replaced with nth argument (0-based, digits only)
- `{{` - Escaped left brace, prints literal `{`
- `}}` - Escaped right brace, prints literal `}`

**Note:** Format mode is only activated by valid placeholders. Patterns like `{abc}`, `{ }`, or `{1a}` are treated as literal text and will trigger concatenation mode.

**Examples:**
```javascript
// Format mode - first arg has {}
print("Hello, {}!", "JaiScript");          // Hello, JaiScript!
print("{} + {} = {}", 2, 3, 5);            // 2 + 3 = 5
print("{1} comes after {0}", "A", "B");   // B comes after A

// Concatenation mode - no {} in first arg
print("The answer is ", 42, "!");          // The answer is 42!
print("Hello", " ", "World");              // Hello World

// Escape sequences
print("Use {{}} for placeholders");        // Use {} for placeholders
print("Closing brace: }}");                // Closing brace: }

// Control output behavior
print("Progress: 50%", skip_newline);      // Progress: 50% (no newline)
print(" [######### ]");                    // <continued from previous line>
print("Debug: ", value, skip_flush);       // Debug: <value> (no newline/flush)
```

#### `format(...)`
Builds formatted strings with identical behavior to `print` but returns a string instead of printing.

**Usage:**
- `format(value)` - Converts value to string
- `format(format_str, args...)` - If format_str contains `{}`, uses format-style
- `format(arg1, arg2, ...)` - If no `{}` in first arg, concatenates all args

**Note:** Control types (`skip_newline`, `skip_flush`) are ignored in format function.

**Examples:**
```javascript
// Format mode
auto greeting = format("Hello, {}!", "World");     // "Hello, World!"
auto equation = format("{} + {} = {}", 2, 3, 5);   // "2 + 3 = 5"

// Concatenation mode
auto message = format("The answer is ", 42);       // "The answer is 42"
auto path = format("/home/", username, "/docs");   // "/home/alice/docs"

// Escape sequences work the same as print
auto literal = format("{{name}}: {{value}}");      // "{name}: {value}"

// Template strings are often cleaner than either mode:
auto json = `{"${key}": "${value}"}`;
```

#### `to_string(value)`
Converts any value to its string representation.

#### `type_of(value)`
Returns the type name of a value as a string ("int", "float", "string", etc.).

### JSON Functions

#### `to_json(value)` / `to_json(value, indent)`
Converts a JaiScript value to JSON string. Optional indent parameter for pretty printing.

#### `from_json(json_string)`
Parses a JSON string and returns the corresponding JaiScript value.

## Performance

The definitive report is [PERFORMANCE.md](PERFORMANCE.md) (2026-07 capstone: both backends
head-to-head against Lua 5.4/sol2, Squirrel, and ChaiScript by workload class, plus per-op
and idiom ladders, aliasing, parallel scaling, debugger cost, and jaibite cache numbers).
Committed baselines live in [thin_value_rebaseline.md](thin_value_rebaseline.md) (§6,
interpreter and VM side by side) and [execution_mode_metrics.md](execution_mode_metrics.md)
(execution-mode and jaibite-cache numbers). What passing/holding strategy to use for objects
(value vs `T&` vs `shared_ptr<T>`) is measured and decided in [aliasing_costs.md](aliasing_costs.md).
`script_value` is 32 bytes (static_assert-gated),
heavy types ride in non-atomic `strong_ptr` for O(1) copies, and the benchmark suite includes
sol2/Lua, Squirrel, and ChaiScript comparisons (`x64-Release BENCHMARKS` config).

## Documentation

### Language & Type System
- [grammar.md](grammar.md) - Complete language grammar reference
- [why_jaiscript.md](why_jaiscript.md) - Motivation with real-world comparisons
- [STRONG_TYPES.md](STRONG_TYPES.md) - The runtime type ladder (int/auto/var)
- [static_checking.md](static_checking.md) - Opt-in static type checking (off/warn/strict)
- [TYPE_SYSTEM_DESIGN.md](TYPE_SYSTEM_DESIGN.md) - Type system, shared_ptr/weak_ptr, auto-unwrap semantics
- [EXCEPTION_DESIGN.md](EXCEPTION_DESIGN.md) - Exception handling (+ terminal-error tier)

### Engine & Integration
- [BUILD.md](BUILD.md) - Build instructions and options
- [SERIALIZATION.md](SERIALIZATION.md) - The C++ value-archive system (jai_archive)
- [TEMPLATE_BINDING_PROPOSAL.md](TEMPLATE_BINDING_PROPOSAL.md) - Binding C++ template classes (implemented)
- [LOCAL_VARIABLES_BEHAVIOR.md](LOCAL_VARIABLES_BEHAVIOR.md) - `instance_variables` overlay scopes
- [NAMING_CONVENTIONS.md](NAMING_CONVENTIONS.md) - Code naming conventions
- [portability.md](portability.md) - Platform/compiler portability notes
- [invariants.md](invariants.md) - Load-bearing internal invariants (read before touching value layout/VM)

### Performance & Testing
- [execution_mode_metrics.md](execution_mode_metrics.md) - Execution modes, jaibite, cache metrics
- [aliasing_costs.md](aliasing_costs.md) - Measured cost of value vs `T&` vs `shared_ptr<T>` + decision guidance
- [thin_value_spec.md](thin_value_spec.md) / [thin_value_rebaseline.md](thin_value_rebaseline.md) - 32-byte value fold (as-built spec + benchmark baseline)
- [PERFORMANCE.md](PERFORMANCE.md) - The definitive 2026-07 performance report (head-to-head vs Lua/Squirrel/ChaiScript, ladders, scaling)
- [JaiScriptTesting.md](JaiScriptTesting.md) / [JaiScriptVMTesting.md](JaiScriptVMTesting.md) - Testing guides

### Roadmap & Design History
- [ROADMAP.md](ROADMAP.md) - Feature roadmap and priorities
- [parallel_design.md](parallel_design.md) / [parallel_prove_or_serial.md](parallel_prove_or_serial.md) - Parallelism design + decision record
- [JaiScript_FutureDesign.md](JaiScript_FutureDesign.md) - VISION (2025) document
- [JaiScript_DeepCopyDesign.md](JaiScript_DeepCopyDesign.md) - Deep copy semantics
- [REFERENCE_SEMANTICS_PLAN.md](REFERENCE_SEMANTICS_PLAN.md) - HISTORICAL reference-semantics plan
- [PERSISTENT_INTERPRETER_DESIGN.md](PERSISTENT_INTERPRETER_DESIGN.md) - HISTORICAL persistent-interpreter design
- [MIGRATION_PLAN.md](MIGRATION_PLAN.md) - HISTORICAL ChaiScript→JaiScript migration plan
- [JAI_ARCHIVE_DEVIRTUALIZATION.md](JAI_ARCHIVE_DEVIRTUALIZATION.md) - HISTORICAL archive devirtualization
- [JaiScript_BinarySerialization.md](JaiScript_BinarySerialization.md) - HISTORICAL binary-serialization vision

## Type Conversion Behavior

JaiScript follows C++ semantics for numeric conversions:
- `int` to `float`: automatic widening conversion
- `float` to `int`: automatic truncation (no warning, matches C++ behavior)

Want stricter? Enable static checking (`engine->static_checking(jai::check_mode::strict)`).

## License

MIT License - See LICENSE file for details

## Credits

JaiScript was designed and created by Devacor (mike@m2tm.net) in honor of his love, Jai.
