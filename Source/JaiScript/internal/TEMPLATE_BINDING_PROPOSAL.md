# Template Binding System for JaiScript

## Status: Implemented ✓

This document describes the template binding system for JaiScript, which enables binding C++ template types to the scripting engine with minimal boilerplate.

---

## Overview

The template binding system allows you to:
1. Define a binding once for a template class (via `JAI_TEMPLATE_BINDER`)
2. Use the template with any type parameter automatically (via auto-detection)
3. Optionally pre-register specific variants (via `JAI_BIND_TEMPLATE`)

---

## Quick Start

### Minimal Setup (Auto-Detection)

```cpp
// Define your template
template<typename T>
struct Point {
    T x{};
    T y{};
    T length_squared() const { return x * x + y * y; }
};

// Define binding once - that's it!
JAI_TEMPLATE_BINDER(Point, "Point",
    .property("x", &Point<T>::x)
    .property("y", &Point<T>::y)
    .method("length_squared", &Point<T>::length_squared)
);

// Usage in engine - just works without JAI_BIND_TEMPLATE!
auto eng = engine::make();
eng->bind_static_type<Point<int>>();    // Auto-detected from JAI_TEMPLATE_BINDER
eng->bind_static_type<Point<float>>();  // Works for any type parameter

eng->execute("auto p = Point<int32>(); p.x = 3; p.y = 4;");
eng->execute("print(p.length_squared());");  // Prints: 25
```

### When is `bind_static_type` Required?

**For script access**: You need `eng->bind_static_type<Point<int>>()` so scripts can:
- Create instances: `auto p = Point<int32>();`
- Access the type by name in scripts

**For serialization**: NOT required! If a struct has a template property, it serializes automatically:

```cpp
struct Player {
    JAI_PROPERTY(std::string, name);
    JAI_PROPERTY((Point<float>), position);  // Auto-serialized via template_binder_accessor
};

JAI_STATIC_BINDER(Player, "Player");

// Serialization just works - no bind_static_type needed for Point<float>
Player p;
jai_static_type<Player>::save(ar, p);  // Point<float> serialized automatically
```

---

## Macro Reference

### JAI_TEMPLATE_BINDER

Defines a template binding. This is all you need for auto-detection.

```cpp
// Simple form (TemplateClass must be a simple identifier)
JAI_TEMPLATE_BINDER(Point, "Point",
    .property("x", &Point<T>::x)
    .property("y", &Point<T>::y)
);

// Named form (for namespaced types)
JAI_TEMPLATE_BINDER_NAMED(MyPoint, my::ns::Point, "Point",
    .property("x", &my::ns::Point<T>::x)
);

// Two type parameters
JAI_TEMPLATE_BINDER_2(Pair, "Pair",
    .property("first", &Pair<T1, T2>::first)
    .property("second", &Pair<T1, T2>::second)
);

JAI_TEMPLATE_BINDER_2_NAMED(MyPair, my::ns::Pair, "Pair",
    .property("first", &my::ns::Pair<T1, T2>::first)
);
```

### JAI_BIND_TEMPLATE (Optional)

Pre-registers a specific template variant. Use this for:
- Faster startup (avoids runtime detection)
- Types that need `jai_static_type<T>` traits directly

```cpp
// Simple form
JAI_BIND_TEMPLATE(Point, int);
JAI_BIND_TEMPLATE(Point, float);

// Named form (for namespaced types)
JAI_BIND_TEMPLATE_NAMED(MyPoint, my::ns::Point, int);

// Two type parameters
JAI_BIND_TEMPLATE_2(Pair, int, std::string);
JAI_BIND_TEMPLATE_2_NAMED(MyPair, my::ns::Pair, int, std::string);

// Batch registration
JAI_BIND_TEMPLATE_VARIANTS_3(Point, int, float, double);
```

---

## Auto-Detection System

### Detection Traits

The system provides compile-time detection for template binder types:

```cpp
// Check if T has a template binder (1-param templates)
if constexpr (jai::has_template_binder_v<Point<int>>) {
    // Point<int> has JAI_TEMPLATE_BINDER defined
}

// Check if T has a 2-param template binder
if constexpr (jai::has_template_binder_2_v<Pair<int, std::string>>) {
    // ...
}

// Combined check (either 1-param or 2-param)
if constexpr (jai::has_any_template_binder_v<T>) {
    // T has some form of template binder
}
```

### Template Binder Accessor

Access binder functionality from an instantiated type:

```cpp
using Accessor = jai::template_binder_accessor<Point<int>>;

// Get type name
std::string name = Accessor::type_name();  // "Point<int32>"

// Serialize
Accessor::save(ar, point_value);
Accessor::load(ar, point_value);

// Bind to engine
Accessor::bind_to(eng);
```

---

## Type Name Helper

Clean type names for script binding. JaiScript's native `int` is 64-bit:

```cpp
// Native types (what script users expect)
jai::type_name_helper<int64_t>::name()      // "int" (JaiScript's native int)
jai::type_name_helper<uint64_t>::name()     // "uint" (JaiScript's native uint)
jai::type_name_helper<float>::name()        // "float"
jai::type_name_helper<double>::name()       // "double"
jai::type_name_helper<std::string>::name()  // "string"

// Explicit-width integers (C++ int is typically 32-bit)
jai::type_name_helper<int>::name()          // "int32" (C++ int = int32_t)
jai::type_name_helper<int8_t>::name()       // "int8"
jai::type_name_helper<int16_t>::name()      // "int16"
jai::type_name_helper<int32_t>::name()      // "int32"
jai::type_name_helper<uint8_t>::name()      // "uint8"
jai::type_name_helper<uint16_t>::name()     // "uint16"
jai::type_name_helper<uint32_t>::name()     // "uint32"
```

---

## Auto Integer Variant Binding

When you bind a template with an integer type parameter, **both 32-bit and 64-bit variants are automatically bound**:

```cpp
// Bind Point<int> (which is Point<int32_t> in C++)
eng->bind_static_type<Point<int>>();

// Now BOTH are available in script:
eng->execute("auto p32 = Point<int32>();");  // 32-bit variant
eng->execute("auto p64 = Point<int>();");    // 64-bit variant (JaiScript native)
```

This works bidirectionally:
- Binding `Point<int>` or `Point<int32_t>` → also binds `Point<int64_t>`
- Binding `Point<int64_t>` → also binds `Point<int32_t>`
- Same for `uint32_t` ↔ `uint64_t`

This ensures script users can naturally write `Point<int>` (64-bit) while C++ code using `Point<int>` (32-bit) still works.

---

## CRTP Auto-Registration (Optional)

For templates you control, inherit from `auto_bind_template` to trigger registration on instantiation:

```cpp
template<typename T>
struct Point : jai::auto_bind_template<Point, T> {
    T x{};
    T y{};
};

JAI_TEMPLATE_BINDER(Point, "Point",
    .property("x", &Point<T>::x)
    .property("y", &Point<T>::y)
);

// Now instantiation marks the type as registered
Point<int> p;  // Automatically marks Point<int> in static_type_registry
```

---

## Dispatch Priority

When serializing or binding, the system checks in this order:

1. **`has_static_type_v<T>`** - Explicit `JAI_STATIC_BINDER` or `JAI_BIND_TEMPLATE`
2. **`has_any_template_binder_v<T>`** - Auto-detected from `JAI_TEMPLATE_BINDER`
3. **`has_any_save_v<T>` / `has_any_load_v<T>`** - Free functions or member save/load
4. **Static assert** - No serialization support

This means `JAI_BIND_TEMPLATE` takes precedence, but auto-detection works as fallback.

---

## Complete Example

```cpp
// === vec2.hpp ===

namespace game {

template<typename T>
struct Vec2 {
    T x{};
    T y{};

    Vec2() = default;
    Vec2(T x_, T y_) : x(x_), y(y_) {}

    T length_squared() const { return x * x + y * y; }
    Vec2<T> operator+(const Vec2<T>& o) const { return {x + o.x, y + o.y}; }
};

} // namespace game

// Define binding (NAMED variant for namespaced type)
JAI_TEMPLATE_BINDER_NAMED(GameVec2, game::Vec2, "Vec2",
    .property("x", &game::Vec2<T>::x)
    .property("y", &game::Vec2<T>::y)
    .method("length_squared", &game::Vec2<T>::length_squared)
);

// === player.hpp ===

struct Player {
    JAI_PROPERTY(std::string, name);
    JAI_PROPERTY((game::Vec2<float>), position);  // Template type as property
    JAI_PROPERTY(int, health, 100);
};

JAI_STATIC_BINDER(Player, "Player");

// === game.cpp ===

void setup_engine() {
    auto eng = engine::make();

    // Bind Vec2 variants for script access
    eng->bind_static_type<game::Vec2<int>>();
    eng->bind_static_type<game::Vec2<float>>();

    // Bind Player
    eng->bind_static_type<Player>();

    // Script can now use both
    eng->execute(R"(
        auto player = Player();
        player.name = "Hero";
        player.position = Vec2<float>();
        player.position.x = 100.0;
        player.position.y = 200.0;

        auto offset = Vec2<int>();
        offset.x = 10;
        offset.y = 20;
        print(offset.length_squared());  // 500
    )");
}

void serialize_player(const Player& p) {
    std::vector<uint8_t> buffer;
    serialization::binary_archive_writer ar(buffer);

    // This just works! Vec2<float> serialized via auto-detection
    // No need to call bind_static_type for serialization
    jai_static_type<Player>::save(ar, p);
}
```

---

## Summary

| Feature | Requires |
|---------|----------|
| Define binding for template | `JAI_TEMPLATE_BINDER` |
| Use in script | `eng->bind_static_type<T>()` |
| Serialize as nested property | Nothing extra (auto-detected) |
| Pre-register for faster startup | `JAI_BIND_TEMPLATE` (optional) |
| CRTP auto-registration | Inherit `auto_bind_template` (optional) |
| Namespaced templates | Use `*_NAMED` variants |
| Get both `int` and `int32` variants | Automatic (bind either, get both) |

The key insight is that **`JAI_TEMPLATE_BINDER` alone is sufficient** for most use cases. You only need:
- `bind_static_type<T>()` when scripts need to create/access the type
- `JAI_BIND_TEMPLATE` for performance or when you need `jai_static_type<T>` traits

**Integer Type Mapping:**
- JaiScript `int` = C++ `int64_t` (64-bit native)
- JaiScript `int32` = C++ `int` / `int32_t` (32-bit explicit)
- Binding either variant automatically binds the other
