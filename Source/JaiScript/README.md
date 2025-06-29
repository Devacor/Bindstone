# JaiScript

A high-performance scripting language with C++-like syntax designed for game engines. Features 25-578x faster execution than ChaiScript, seamless C++ integration, modern language features, and follows C++ standard library naming conventions with snake_case throughout.

## Why JaiScript?

JaiScript was born from real-world game development frustrations with existing embedded scripting solutions. After extensive use of ChaiScript in production, we identified critical pain points that hindered rapid game development:

- **No natural property access** - Forced to use `self["property"]` instead of `self.property`
- **No hot-reload** - "Already defined" errors require full engine restart
- **Explicit self-passing** - Methods need `fun(self, dt)` with manual `self` passing
- **Limited closures** - Manual capture lists `fun[var1, var2]` instead of automatic capture
- **Poor null handling** - Awkward `.is_var_null()` instead of simple `!= null`

See [docs/why-jaiscript.md](docs/why-jaiscript.md) for detailed real-world examples and comparisons.

JaiScript solves these issues while maintaining the performance and C++ integration that game engines require.

## Features

- **Exceptional Performance** - 25-578x faster than ChaiScript
- **C++-like Syntax** - Familiar to C++ developers  
- **Modern Language Features** - Lambdas with captures, flexible function syntax, operator overloading
- **Seamless C++ Integration** - Clean class_builder API for exposing C++ types
- **Standard Naming Conventions** - Uses snake_case throughout to match C++ standard library
- **Zero Dependencies** - Standalone implementation
- **Flexible Build Options** - Header-only, compiled library, or single compilation unit

## Quick Start

```cpp
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine engine;
    
    // Execute script with modern syntax
    auto result = engine.execute(R"(
        // Variables with type inference
        auto x = 10;
        auto y = 20;
        
        // Functions with multiple syntax styles
        auto add = [](script_int a, script_int b) -> script_int { return a + b; };
        
        // Use the function
        add(x, y)
    )");
    
    std::cout << "Result: " << result.as<script_int>() << std::endl; // 30
    return 0;
}
```

## Language Overview

### Variables and Types
```javascript
script_int x = 42;           // 64-bit signed integer
script_float pi = 3.14159;   // 64-bit double precision  
script_string name = "Game"; // UTF-8 string
script_bool active = true;   // Boolean
auto count = 0;              // Type inference
```

### Functions
```javascript
// Multiple declaration styles supported
script_int multiply(script_int a, script_int b) { return a * b; }
auto divide(script_float a, script_float b) -> script_float { return a / b; }
function greet(script_string name) { print("Hello, " + name); }

// Lambda expressions with captures
script_int multiplier = 10;
auto scale = [multiplier](script_int x) -> script_int { return x * multiplier; };
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
```

### Exception Handling
```javascript
// Try-catch with exception variable
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

// Throw any value type
throw 42;                   // Numbers
throw "Error message";      // Strings
throw {code: 404};         // Objects
```

### Operators
- **Arithmetic**: `+`, `-`, `*`, `/`, `%`
- **Compound**: `+=`, `-=`, `*=`, `/=`
- **Increment**: `++`, `--` (prefix and postfix)
- **Comparison**: `<`, `>`, `<=`, `>=`, `==`, `!=`
- **Spaceship**: `<=>` (three-way comparison)
- **Logical**: `&&`, `||`, `!`
- **Bitwise**: `&`, `|`, `^`, `~`, `<<`, `>>`
- **Ternary**: `condition ? true_val : false_val`
- **Control Flow**: `break`, `continue` (C++ semantics)
- **Custom Operator Overloading**: Full support for custom operators on user-defined types

### Arrays and Maps
```javascript
// Array literals and operations
auto numbers = [1, 2, 3, 4, 5];
numbers.push(6);
int first = numbers[0];
int size = numbers.size();

// Map literals and operations  
auto scores = {
    {"player1", 100},
    {"player2", 85}
};
scores["player3"] = 90;
int p1Score = scores["player1"];

// Map methods
bool has_player = scores.contains("player1");
auto all_keys = scores.keys();
scores.erase("player2");
```

## C++ Integration

### Exposing C++ Classes
```cpp
// Clean class_builder API - 60% less code than ChaiScript
jai::make_class_builder<Vector2>(engine, "Vec2")
    .constructor<script_float, script_float>()
    .constructor<script_int, script_int>()  // Automatic type conversion support
    .field("x", &Vector2::x)
    .field("y", &Vector2::y)
    .method("length", &Vector2::length)
    .method("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2{a.x + b.x, a.y + b.y};
    })
    .build();

// Automatic type conversions work seamlessly
// In script: auto v = Vec2(3, 4);  // int to double conversion handled automatically
```

### Adding Functions
```cpp
// Simple function binding
engine.add_function("sqrt", [](script_float x) -> script_float {
    return std::sqrt(x);
});

// Variadic functions  
engine.add_variadic_function("print", [](const std::vector<script_value>& args) {
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
engine.add_function("processString", [](const script_string& str) {
    // Direct access to script string - no copy
});

// Custom types work the same way
engine.add_function("distance", [](const Vec2& a, const Vec2& b) -> script_float {
    // No copies made of Vec2 objects
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
});
```

## Building

### Option 1: Header-Only (Simplest)
```cpp
#include <jaiscript/jaiscript.hpp>
```

### Option 2: Compiled Library  
```cmake
add_library(jaiscript 
    Source/JaiScript/src/implementation/engine.cpp
    Source/JaiScript/src/implementation/interpreter.cpp
    Source/JaiScript/src/implementation/lexer.cpp
    Source/JaiScript/src/implementation/parser.cpp
    Source/JaiScript/src/implementation/value.cpp
)
target_include_directories(jaiscript PUBLIC Source/JaiScript/include)
```

### Option 3: Single Compilation Unit
```cpp
// In one .cpp file in your project
#define JAISCRIPT_IMPLEMENTATION
#include <jaiscript/jaiscript.hpp>
```

### Running Tests
```bash
cd Source/JaiScript/TestSuite
make clean && make
make run_tests
```

## API Reference

### engine Class
```cpp
class engine {
    // Script execution
    script_value execute(const script_string& code);
    script_value execute_file(const script_string& path);
    
    // Variable management
    void add_global(const script_string& name, script_value value);
    script_value get_variable(const script_string& name);
    bool has_variable(const script_string& name);
    
    // Function registration
    void add_function(const script_string& name, script_function func);
    void add_variadic_function(const script_string& name, script_function func);
    
    // Type registration (see class_builder)
    template<typename T>
    class_builder<T> addClass(const script_string& name);
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
Formatted output to stdout with support for format strings.

**Usage:**
- `print()` - Prints just a newline
- `print(value)` - Prints value followed by newline  
- `print(format, args...)` - Printf-style formatting with placeholders

**Format placeholders:**
- `{}` - Sequential placeholder, replaced with next argument
- `{n}` - Positional placeholder, replaced with nth argument (0-based)
- `{{` - Escaped left brace, prints literal '{'
- `}}` - Escaped right brace, prints literal '}'

**Examples:**
```javascript
print("Hello, World!");                    // Hello, World!
print("Hello, {}!", "JaiScript");          // Hello, JaiScript!
print("{} + {} = {}", 2, 3, 5);            // 2 + 3 = 5
print("{1} comes after {0}", "A", "B");   // B comes after A
print("Use {{}} for placeholders");        // Use {} for placeholders
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

Benchmark results vs ChaiScript:
- Simple arithmetic: **578x faster**
- Variable operations: **293x faster**
- Function calls: **173x faster**
- Loops: **25.8x faster**
- Overall: **25-578x performance improvement**

See `docs/performance.md` for detailed benchmarks.

## Documentation

- `docs/why-jaiscript.md` - **Motivation and real-world pain points that led to JaiScript**
- `docs/syntax.md` - Complete language syntax reference
- `docs/cpp-integration.md` - Detailed C++ binding guide
- `docs/architecture.md` - Internal architecture and design
- `docs/performance.md` - Performance analysis and benchmarks
- `JaiScript_FutureDesign.md` - **Roadmap and vision for JaiScript's future**

## Current Limitations

- No script-defined classes yet (C++ classes can be used)
- No switch/case statements
- Hot-reload system not yet implemented
- Exception handling not yet supported in VM backend (interpreter only)

See `JaiScript_FutureDesign.md` for planned features including unified serialization, state-preserving hot reload, and property system integration.

## License

MIT License - See LICENSE file for details

## Credits

JaiScript was designed and created by Devacor (mike@m2tm.net) in honor of his love, Jai.