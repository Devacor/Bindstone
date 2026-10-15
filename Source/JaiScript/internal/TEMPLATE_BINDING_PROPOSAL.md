# Template Binding System for JaiScript

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

**For serialization**: not required. A `JAI_STATIC_BINDER` serializes exactly the members its `.property()` list
names (a binder with no list serializes nothing); a listed member of a template type serializes through its
`JAI_TEMPLATE_BINDER`:

```cpp
struct Player {
    std::string name;
    Point<float> position;
};

JAI_STATIC_BINDER(Player, "Player",
    .property("name", &Player::name)
    .property("position", &Player::position)  // serialized via template_binder_accessor
);

// No bind_static_type needed for Point<float>
Player p;
jai_static_type<Player>::save(ar, p);
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

Pre-registers a specific template variant: it specializes `jai_static_type<T>` for that instantiation, for code that
needs the static-type traits directly.

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

The script spelling of a C++ type as a template argument. JaiScript's native `int` and `float` are 64-bit, so a
narrower width carries its size:

```cpp
// Native types (what script users expect)
jai::type_name_helper<int64_t>::name()      // "int" (JaiScript's native int)
jai::type_name_helper<uint64_t>::name()     // "uint" (JaiScript's native uint)
jai::type_name_helper<double>::name()       // "float" (JaiScript's native float)
jai::type_name_helper<float>::name()        // "float32"
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

Math-adapted types, `std::vector`, `std::map` and smart-pointer handles spell through their contents; a type with no
script spelling is a compile error unless `jai::type_name_helper<T>` is specialized for it.

---

## Native-Width Sibling Binding

When you bind a template with a 32- or 64-bit integer or floating type parameter, **both widths are bound**
(`native_width_sibling`, `width_variant_binder`):

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
- Same for `uint32_t` ↔ `uint64_t` and `float` ↔ `double` (script `Point<float32>` ↔ `Point<float>`)

This ensures script users can naturally write `Point<int>` (64-bit) while C++ code using `Point<int>` (32-bit) still works.

---

## CRTP Auto-Registration (Optional)

For templates you control, inherit from `auto_bind_template` to declare every instantiation for binding:

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

// A compile-time fact, no state:
static_assert(jai::static_type_registry::is_registered<Point<int>>());
```

What an engine has bound is engine state: `static_type_registry::is_registered<T>(eng)`.

---

## Dispatch Priority

`engine::bind_static_type<T>()` checks, in order (`static_binder_impl.hpp`):

1. **`has_static_type_v<T>`** - Explicit `JAI_STATIC_BINDER` or `JAI_BIND_TEMPLATE`
2. **`has_any_template_binder_v<T>`** - Auto-detected from `JAI_TEMPLATE_BINDER` (then binds the native-width sibling)
3. **Static assert** - Not registered

A listed property (`static_property::save`/`load`) serializes in this order:

1. Primitives (`is_direct_serializable_v`)
2. `std::vector`, then `std::map`
3. `has_static_type_v<P>`
4. `has_any_template_binder_v<P>`
5. Otherwise the archive's own `ar(value)` dispatch (member or ADL `save`/`load`/`serialize`)

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
    std::string name;
    game::Vec2<float> position;  // Template type as property
    int health = 100;
};

JAI_STATIC_BINDER(Player, "Player",
    .property("name", &Player::name)
    .property("position", &Player::position)
    .property("health", &Player::health)
);

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
        player.position = Vec2<float32>();
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
| Pre-register a `jai_static_type<T>` | `JAI_BIND_TEMPLATE` (optional) |
| CRTP auto-registration | Inherit `auto_bind_template` (optional) |
| Namespaced templates | Use `*_NAMED` variants |
| Get both `int` and `int32` variants | Automatic (bind either, get both) |

The key insight is that **`JAI_TEMPLATE_BINDER` alone is sufficient** for most use cases. You only need:
- `bind_static_type<T>()` when scripts need to create/access the type
- `JAI_BIND_TEMPLATE` when you need `jai_static_type<T>` traits

**Integer Type Mapping:**
- JaiScript `int` = C++ `int64_t` (64-bit native)
- JaiScript `int32` = C++ `int` / `int32_t` (32-bit explicit)
- JaiScript `float` = C++ `double`; JaiScript `float32` = C++ `float`
- Binding either width automatically binds the other
