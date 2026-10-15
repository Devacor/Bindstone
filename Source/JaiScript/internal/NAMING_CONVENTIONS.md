# JaiScript Naming Conventions

## Overview

JaiScript follows C++ standard library naming conventions throughout the codebase, using snake_case for consistency and familiarity to C++ developers.

## Naming Rules

### Classes and Types
```cpp
// Classes use snake_case
class engine { };
class lexer { };
class parser { };
class script_value { };

// Type aliases use script_ prefix for clarity
using script_int = int64_t;      // 64-bit signed integer
using script_float = double;     // 64-bit double precision
using script_string = std::string;
using script_char = char;
using script_bool = bool;
```

### Functions and Methods
```cpp
// Functions use snake_case
void add_function(const script_string& name);
script_value get_variable(const script_string& name);
bool has_variable(const script_string& name);

// Method names follow the same pattern
void set_value(script_int val);
script_int get_value() const;
```

### Variables and Members
```cpp
// All variables use snake_case
script_int line_number;
script_string file_name;
std::vector<token> token_list;

// Data members of a plain struct carry no suffix (detail/lexer.hpp)
struct token {
    token_type type;
    std::string_view lexeme;
    uint64_t symbol_id = 0;
    source_location location;
};
```

### Template Parameters and Containers
```cpp
// Template types maintain snake_case
template<typename T>
class shared_ptr { };

template<typename T>
class weak_ptr { };

// Container types
std::vector<script_value> arguments;
std::map<script_string, script_value> variables;
```

## Type Aliases

JaiScript uses `script_` prefixed type aliases to:
1. Provide cross-platform fixed-size types
2. Avoid confusion with standard library types
3. Maintain consistency in the `jai::` namespace

```cpp
namespace jai {
    using script_int = int64_t;        // Always 64-bit signed
    using script_float = double;       // Always 64-bit double precision
    using script_string = std::string; // UTF-8 string
    using script_char = char;          // 8-bit character
    using script_bool = bool;          // 1 byte boolean
}
```

## API Example

```cpp
auto engine = jai::engine::make();   // ctor is private; engines are always shared_ptr
jai::script_value result = engine->execute("2 + 2");
script_int val = result.as<script_int>();

jai::dynamic_binder<MyClass>(engine, "MyClass")
    .constructor<script_int, script_float>()
    .method("set_value", &MyClass::set_value)
    .build();
```

## Benefits

1. **Consistency**: Matches C++ standard library conventions
2. **Familiarity**: Natural for C++ developers
3. **Clarity**: `script_` prefix clearly identifies JaiScript types
4. **Namespace Harmony**: `jai::script_int` vs `jai::jai_int` (redundant)
