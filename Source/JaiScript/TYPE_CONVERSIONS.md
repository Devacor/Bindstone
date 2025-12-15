# JaiScript Type Conversion System

## Overview

JaiScript uses a simple, unified approach to type conversions: **constructors define conversions**. This mirrors C++'s implicit conversion mechanism but in a simplified form appropriate for a scripting language.

## Core Principles

### 1. Constructors ARE Conversions

Single-argument constructors automatically define how values of other types can be converted to your class. No separate conversion registration is needed.

```jai
class TreeNode {
    var value: int = 0;
    var left: TreeNode = null;
    var right: TreeNode = null;

    // This constructor enables: TreeNode t = someCppTreeNode;
    TreeNode(CppTreeNode node) {
        this.value = node.value;
        this.left = node.left ? TreeNode(node.left) : null;
        this.right = node.right ? TreeNode(node.right) : null;
    }

    // This constructor enables: TreeNode t = 42;
    TreeNode(int val) {
        this.value = val;
    }
}
```

### 2. Outbound Conversions via `to_X()` Methods

For converting TO primitive types, classes define `to_X()` methods where X is a built-in JaiScript type:

| Method | Enables | Example |
|--------|---------|---------|
| `to_int()` | `int x = obj;` | `to_int(): int { return this.value; }` |
| `to_float()` | `float x = obj;` | `to_float(): float { return this.value * 1.0; }` |
| `to_string()` | `string x = obj;` | `to_string(): string { return "Node(" + this.value + ")"; }` |
| `to_bool()` | `if (obj)`, `bool x = obj;` | `to_bool(): bool { return this.value != 0; }` |
| `to_char()` | `char x = obj;` | `to_char(): char { return 'A'; }` |

**Note:** `to_array()` and `to_map()` are intentionally excluded - complex container conversions should use explicit methods like `to_list()` or `as_dictionary()`.

### 3. Inheritance = No-Copy Compatibility

When classes have an inheritance relationship, reference and smart pointer assignments don't copy - they just cast:

```jai
class Base { var x: int = 0; }
class Derived : Base { var y: int = 0; }

var d = Derived();
var ref: Base& = d;              // No copy - reference to same object
var ptr: shared_ptr<Base> = d;   // No copy - shared_ptr upcast
```

For value assignment with inheritance, a copy constructor is used if available:

```jai
class Base {
    var x: int = 0;
    Base(Derived d) { this.x = d.x; }  // Copy from derived
}

var d = Derived();
var b: Base = d;  // Calls Base(Derived) constructor - creates copy
```

## C++ Integration via ClassBuilder

### Registering Inbound Conversions

```cpp
class_builder<TreeNode>(engine, "TreeNode")
    .constructor<CppTreeNode>()  // Enables: TreeNode t = cppNode;
    .constructor<int>()          // Enables: TreeNode t = 42;
    .constructor<std::string>()  // Enables: TreeNode t = "label";
    .build();
```

### Registering Outbound Conversions

```cpp
class_builder<TreeNode>(engine, "TreeNode")
    .to_int([](const TreeNode& t) { return t.value; })
    .to_string([](const TreeNode& t) { return "TreeNode(" + std::to_string(t.value) + ")"; })
    .to_bool([](const TreeNode& t) { return t.value != 0; })
    .build();
```

### Inheritance Registration

```cpp
class_builder<DerivedNode>(engine, "DerivedNode")
    .base_class<TreeNode>()  // Enables no-copy compatibility for refs/shared_ptr
    .build();
```

## Conversion Resolution Rules

### Priority Order

When multiple conversions are possible, JaiScript uses this priority:

1. **Exact match** - No conversion needed
2. **Inheritance upcast** - Derived → Base (no copy for refs/ptrs)
3. **Single-arg constructor** - `Target(Source)` constructor exists
4. **`to_X()` method** - Source has `to_Target()` method (primitives only)

### Implicit vs Explicit

All conversions in JaiScript are **implicit** - there is no `explicit` keyword. This keeps the language simple. If you want to prevent implicit conversion, don't provide a single-argument constructor.

### Null Handling

- `null` can be assigned to any nullable type (objects, shared_ptr, references)
- Constructors are NOT called when assigning `null`
- `to_X()` methods are NOT called when the source is `null`

## Implementation Details

### Conversion Lookup Storage

Conversions are stored in `ClassDefinition` for efficient lookup:

```cpp
struct conversion_entry {
    uint64_t source_type_id;     // Symbol ID of source type
    wrapped_function converter;   // The constructor or conversion function
};

class class_definition {
    // Indexed by source type symbol_id for O(1) lookup
    std::unordered_map<uint64_t, conversion_entry> inbound_conversions_;

    // Outbound conversions (to primitives)
    std::optional<wrapped_function> to_int_;
    std::optional<wrapped_function> to_float_;
    std::optional<wrapped_function> to_string_;
    std::optional<wrapped_function> to_bool_;
    std::optional<wrapped_function> to_char_;
};
```

### Hot-Reload Considerations

When a class is redefined during hot-reload:

1. **Inbound conversions** are rebuilt from the new constructor list
2. **Outbound conversions** are rebuilt from the new `to_X()` methods
3. **Existing instances** continue to work - they hold `shared_ptr<class_definition>` which may point to old definition until garbage collected
4. **New instances** use the new conversions

```cpp
// Hot-reload updates the class definition
engine.execute(R"(
    class TreeNode {
        // New constructor - enables new conversion
        TreeNode(string label) { this.value = label.length(); }
    }
)");

// New conversions are immediately available
auto node = engine.execute("TreeNode(\"hello\")");  // value = 5
```

### Performance Considerations

1. **Symbol ID lookup** - All type names are interned to symbol IDs for fast comparison
2. **Cached conversions** - Once looked up, conversion functions are cached per source/target pair
3. **No runtime reflection** - Conversion availability is checked at assignment time, not dynamically

## Error Handling

When no valid conversion exists:

```jai
class A { var x: int = 0; }
class B { var y: int = 0; }

var a: A = B();  // Error: Cannot convert B to A (no suitable constructor)
```

Error message format:
```
Cannot assign B to variable of type A (no conversion from B to A available)
```

## Examples

### Complete Script Example

```jai
class Vector2 {
    var x: float = 0.0;
    var y: float = 0.0;

    // Constructor conversions
    Vector2(float scalar) {
        this.x = scalar;
        this.y = scalar;
    }

    Vector2(int scalar) {
        this.x = scalar * 1.0;
        this.y = scalar * 1.0;
    }

    // Outbound conversions
    to_float(): float { return sqrt(x*x + y*y); }  // Magnitude
    to_string(): string { return "(" + x + ", " + y + ")"; }
    to_bool(): bool { return x != 0.0 || y != 0.0; }
}

// Usage
var v1: Vector2 = 5.0;           // Calls Vector2(float)
var v2: Vector2 = 3;             // Calls Vector2(int)
var length: float = v1;          // Calls to_float()
var desc: string = v2;           // Calls to_string()
if (v1) { print("non-zero"); }   // Calls to_bool()
```

### Complete C++ Example

```cpp
struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float s) : x(s), y(s) {}
    Vector2(int s) : x(static_cast<float>(s)), y(static_cast<float>(s)) {}
};

class_builder<Vector2>(engine, "Vector2")
    .constructor<>()
    .constructor<float>()          // Vector2 v = 5.0f;
    .constructor<int>()            // Vector2 v = 5;
    .property("x", &Vector2::x)
    .property("y", &Vector2::y)
    .to_float([](const Vector2& v) {
        return std::sqrt(v.x*v.x + v.y*v.y);
    })
    .to_string([](const Vector2& v) {
        return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
    })
    .to_bool([](const Vector2& v) {
        return v.x != 0.0f || v.y != 0.0f;
    })
    .build();
```

## Design Rationale

### Why Constructors Instead of Separate Conversion Registration?

1. **Familiarity** - Mirrors C++ semantics developers already know
2. **Single source of truth** - No separate conversion tables to maintain
3. **Natural syntax** - `TreeNode(other)` reads as "construct TreeNode from other"
4. **Hot-reload friendly** - Redefining class automatically updates conversions

### Why No `explicit` Keyword?

JaiScript prioritizes simplicity. If you don't want implicit conversion, don't provide a single-argument constructor. For cases where you need both explicit construction and no implicit conversion, use a static factory method:

```jai
class Wrapper {
    var value: int = 0;

    // No single-arg constructor = no implicit conversion

    // Factory for explicit construction
    static from_int(int x): Wrapper {
        var w = Wrapper();
        w.value = x;
        return w;
    }
}

var w = Wrapper.from_int(42);  // OK - explicit
var w2: Wrapper = 42;          // Error - no conversion
```

### Why Limit `to_X()` to Primitives?

Complex conversions (to containers, other objects) should be explicit methods with descriptive names:

```jai
class TreeNode {
    // Good - clear intent
    to_list(): Array<int> { ... }
    to_dictionary(): Map<string, int> { ... }

    // Avoided - implicit container conversion is confusing
    // to_array(): Array { ... }
}
```
