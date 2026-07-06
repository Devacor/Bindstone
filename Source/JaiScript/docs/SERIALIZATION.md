# JaiScript Serialization System

This document describes the JaiScript serialization architecture, dispatch priority, and usage patterns.

## Overview

JaiScript provides a dual-format serialization system supporting both **binary** and **JSON** formats through a unified archive interface. The system is designed to:

1. **Interoperate with Cereal** - Types can support both JaiScript and Cereal serialization via `if constexpr` branching
2. **Support property-based serialization** - Types using `property_owner<T>` get automatic serialization
3. **Handle complex scenarios** - Non-default-constructible types, shared_ptr deduplication, weak_ptr reconstruction
4. **Remain extensible** - Custom serialization logic can compose with automatic property serialization

> **Scope note:** this doc covers the C++ **value** archive system (`jai_archive`). Serializing
> parsed **scripts** (AST + compiled bytecode) is a separate subsystem — jaibite save/load
> (`engine::jaibite()`, `jaibite::save()`, `engine::jaibite_load()`); see
> `execution_mode_metrics.md`.

## Quick Reference: Dispatch Priority

```
SAVING                              LOADING (smart_ptr<T> without default ctor)
──────                              ──────────────────────────────────────────
1. Native types (containers, ptrs)  1. T::load_and_construct (member)
2. T::save (member)                 2. load_and_construct (free via ADL)
3. T::serialize (member fallback)   Then normal load dispatch...
4. save (free via ADL)
5. serialize (free via ADL)         LOADING (default-constructible)
6. JAI_STATIC_BINDER                ─────────────────────────────────
7. property_owner (auto fallback)   1. Native types (containers, ptrs)
                                    2. T::load (member)
                                    3. T::serialize (member fallback)
                                    4. load (free via ADL)
                                    5. serialize (free via ADL)
                                    6. JAI_STATIC_BINDER
                                    7. property_owner (auto fallback)
```

## Future Work (TODO)

### Top-Level Keyless Serialization

Currently all serialization requires named properties. A future enhancement will support keyless top-level serialization:

```cpp
// Current (requires wrapping)
ar("PlayerData", playerSaveData);

// Future (top-level object without extra nesting)
ar(playerSaveData);  // When saving to player_save_data.json, the filename IS the key
```

This is useful when serializing a single root object to a file where the filename itself provides context.

## Serialization Method Dispatch Priority

When serializing or deserializing a type `T`, JaiScript checks for serialization support in the following priority order. The design follows Cereal's model where you provide **either** `serialize()` **or** a `save()`/`load()` pair - not both.

### For Saving (archive_writer)

```
Priority 1: STL Containers & Smart Pointers (native handling)
            - std::vector, std::map, std::unordered_map, std::array, std::pair
            - std::shared_ptr, std::weak_ptr, std::unique_ptr
            - Direct primitives (int, float, string, bool, enum)

Priority 2: T::save(archive_writer&) const
            - Member function with explicit save logic
            - Can compose with property_mgr.save() internally

Priority 3: T::serialize(archive_writer&)
            - Bidirectional member function (fallback if no save())
            - Same function handles both save and load

Priority 4: save(archive_writer&, const T&) via ADL
            - Free function in T's namespace

Priority 5: serialize(archive_writer&, T&) via ADL
            - Bidirectional free function (fallback if no free save())

Priority 6: JAI_STATIC_BINDER (compile-time properties)
            - Uses jai_static_type<T>::save(ar, obj)

Priority 7: property_owner<T> (automatic fallback)
            - Calls T::property_mgr.save(ar)
            - Serializes all JAI_PROPERTY fields automatically
            - Only used if NO other serialization method exists

If none match: static_assert compile-time error
```

### For Loading (archive_reader)

For types held in `std::shared_ptr<T>` or `std::unique_ptr<T>` that lack default constructors, `load_and_construct` is checked first (see next section).

For default-constructible types or direct values:

```
Priority 1: STL Containers & Smart Pointers (native handling)
            - Same as save, handles deserialization natively

Priority 2: T::load(archive_reader&)
            - Member function with explicit load logic

Priority 3: T::serialize(archive_reader&)
            - Bidirectional member function (fallback if no load())

Priority 4: load(archive_reader&, T&) via ADL
            - Free function in T's namespace

Priority 5: serialize(archive_reader&, T&) via ADL
            - Bidirectional free function (fallback if no free load())

Priority 6: JAI_STATIC_BINDER (compile-time properties)
            - Uses jai_static_type<T>::load(ar, obj)

Priority 7: property_owner<T> (automatic fallback)
            - Calls T::property_mgr.load(ar)
            - Deserializes all JAI_PROPERTY fields automatically
            - Only used if NO other serialization method exists

If none match: static_assert compile-time error
```

### For Smart Pointers to Non-Default-Constructible Types

When deserializing `std::shared_ptr<T>` or `std::unique_ptr<T>` where `T` lacks a default constructor, `load_and_construct` is checked **before** normal load dispatch:

```
Priority 1: T::load_and_construct(archive_reader&, construct<T>&)
            - Static member function (requires friend jai::access)

Priority 2: load_and_construct(archive_reader&, construct<T>&) via ADL
            - Free function in T's namespace

If neither exists: Fall back to default construction + normal load dispatch
                   (will fail at compile time if T has no default constructor)
```

This pattern matches Cereal's approach - `load_and_construct` is specifically for **smart pointers to types without default constructors**, not a general factory pattern for all types.

```cpp
// Member function (with jai::access friendship)
class MyType {
    friend class jai::access;

    // Private constructor - no default
    MyType(int id, std::string name);

    static void load_and_construct(archive_reader& ar, construct<MyType>& c) {
        int id = ar.read_int32();
        std::string name = ar.read_string();
        c(id, name);  // Construct the object
        c->post_init_stuff();  // Access via operator->
    }

    void save(archive_writer& ar) const {
        ar.write_int32(m_id);
        ar.write_string(m_name);
    }
};

// Or as a free function via ADL
void load_and_construct(archive_reader& ar, construct<OtherType>& c);
```

### Limitation: Nested Non-Default-Constructible Types

`load_and_construct` does **not** support value-type members that lack default constructors. If you have:

```cpp
class MyCat {
    JAI_PROPERTY((HairColor), hairColor, ???);  // HairColor has no default ctor!
    MyCat(int age, HairColor hair) : hairColor(std::move(hair)) {}
};
```

The problem is that `JAI_PROPERTY` requires the member to exist when `MyCat` is constructed, but `load_and_construct` can't partially construct an object.

**Solutions:**

1. **Use smart pointers** for non-default-constructible members:
   ```cpp
   JAI_PROPERTY((std::unique_ptr<HairColor>), hairColor, nullptr);
   ```

2. **Add a default constructor** to the nested type (even if it creates an "invalid" state)

3. **Don't use JAI_PROPERTY** for that member - serialize it manually in `load_and_construct`:
   ```cpp
   static void load_and_construct(archive_reader& ar, construct<MyCat>& c) {
       int age = ar.read_int32();
       std::string hairStr = ar.read_string();
       c(age, HairColor(hairStr));  // Construct everything at once
   }
   ```

## Usage Patterns

### Pattern 1: Simple Type with Bidirectional serialize()

Best for types where save and load have identical structure:

```cpp
struct Point {
    float x, y, z;

    template<typename Archive>
    void serialize(Archive& ar) {
        ar.serialize("x", x);
        ar.serialize("y", y);
        ar.serialize("z", z);
    }
};
```

### Pattern 2: Separate save/load (Version Migration, Computed Fields)

When you need different logic for save vs load:

```cpp
class Entity {
    int m_health;
    int m_max_health;
    float m_health_percent;  // Computed, don't serialize

public:
    void save(archive_writer& ar) const {
        ar.serialize("health", m_health);
        ar.serialize("maxHealth", m_max_health);
    }

    void load(archive_reader& ar) {
        ar.serialize("health", m_health);
        ar.serialize("maxHealth", m_max_health);
        m_health_percent = static_cast<float>(m_health) / m_max_health;
    }
};
```

### Pattern 3: property_owner with Automatic Serialization

Simplest pattern - properties serialize automatically:

```cpp
class Player : public jai::property_owner<Player> {
public:
    JAI_PROPERTY((std::string), name, "Unknown");
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((int), level, 1);

    // No serialize/save/load needed - automatic!
};
```

### Pattern 4: property_owner with Custom Logic (Blended)

Combine automatic property serialization with custom fields:

```cpp
class GameState : public jai::property_owner<GameState> {
public:
    JAI_PROPERTY((int), score, 0);
    JAI_PROPERTY((int), lives, 3);

    // Custom fields not using JAI_PROPERTY
    std::vector<Entity> entities;

    void save(archive_writer& ar) const {
        // Serialize JAI_PROPERTY fields automatically
        property_mgr.save(ar);

        // Custom serialization for non-property fields
        ar.serialize("entities", entities);
    }

    void load(archive_reader& ar) {
        property_mgr.load(ar);
        ar.serialize("entities", entities);
    }
};
```

### Pattern 5: Dual Cereal/JaiScript Support

For types that must work with both serialization systems:

```cpp
class MyComponent {
    int m_value;

    template<class Archive>
    void save(Archive& ar, std::uint32_t version) const {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            // JaiScript path
            ar.serialize("value", m_value);
        } else {
            // Cereal path
            ar(cereal::make_nvp("value", m_value));
        }
    }

    template<class Archive>
    void load(Archive& ar, std::uint32_t version) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            ar.serialize("value", m_value);
        } else {
            ar(cereal::make_nvp("value", m_value));
        }
    }
};
```

Cereal-style versioned signatures — `save(Archive&, std::uint32_t)` / `load(Archive&,
std::uint32_t)` — are **detected natively and preferred**: the detection traits
(`has_versioned_save_method` / `has_simple_save_method`, combined in `has_save_method` in
`archive_impl.hpp`, via the `jai::access` wrappers) probe both arities and dispatch the
versioned overload with the archive's version when it exists. One-parameter forwarders
(`save(Archive& ar) const { save(ar, 0); }`) are **no longer required** — they're optional
legacy shims and can be deleted.

### Pattern 6: Free Functions via ADL

For types you can't modify (third-party libraries):

```cpp
namespace third_party {
    struct Vector3 { float x, y, z; };
}

// In third_party namespace for ADL to find:
namespace third_party {
    void save(jai::serialization::archive_writer& ar, const Vector3& v) {
        ar.serialize("x", v.x);
        ar.serialize("y", v.y);
        ar.serialize("z", v.z);
    }

    void load(jai::serialization::archive_reader& ar, Vector3& v) {
        ar.serialize("x", v.x);
        ar.serialize("y", v.y);
        ar.serialize("z", v.z);
    }
}
```

## Archive API Reference

### archive_writer Methods

```cpp
// Named property serialization (preferred)
ar.serialize("name", value);      // Writes name and value
ar("name", value);                // Same as serialize()
ar(nvp);                          // Using make_nvp("name", value)
ar(JAI_NVP(field));               // Uses variable name as key

// Variadic NVP
ar(JAI_NVP(x), JAI_NVP(y), JAI_NVP(z));

// Direct type serialization (for types with save())
ar(myObject);                     // Calls myObject.save(ar)

// Array elements (inside begin_array/end_array)
ar.write_element(value);          // No name, used for array items

// Low-level primitives
ar.write_int32(value);
ar.write_string(value);
ar.write_bool(value);
// ... etc

// Structure markers
ar.begin_object(type_name, version);
ar.end_object();
ar.begin_array(size);
ar.end_array();
ar.begin_map(size);
ar.end_map();
```

### archive_reader Methods

```cpp
// Named property deserialization
ar.serialize("name", value);      // Seeks to "name", reads into value
ar("name", value);                // Same as serialize()
ar(nvp);                          // Using make_nvp

// Direct type deserialization
ar(myObject);                     // Calls myObject.load(ar)

// Array elements (inside begin_array/end_array)
ar.read_element(value);           // No name, reads next array item

// Property existence checking
ar.has_property("name");          // Returns true if property exists
ar.seek_property("name");         // Positions reader at property

// Low-level primitives
value = ar.read_int32();
value = ar.read_string();
// ... etc

// Structure markers
ar.begin_object(type_name, version);  // Returns false if not object
ar.end_object();
size = ar.begin_array();              // Returns array size
ar.end_array();
```

## Binary vs JSON Format

Both formats share the same archive interface but differ in implementation:

| Feature | Binary | JSON |
|---------|--------|------|
| Property names in file | Yes (in preamble) | Yes (inline) |
| Human readable | No | Yes |
| File size | Smaller | Larger |
| Parse speed | Faster | Slower |
| `needs_property_keys()` | true | false |
| `is_text_format` | false | true |

The `is_text_format` static constexpr is used by smart pointer serialization to choose between formats at compile time (no virtual dispatch):
- **Binary**: Compact format - just ID + object data
- **JSON**: Verbose format - `{"_type_": "ptr", "$id": id, "$val": {...}}` for readability

```cpp
// Compile-time format detection (like Cereal)
template<typename Archive, typename T>
void write_shared_ptr(Archive& ar, const std::shared_ptr<T>& ptr) {
    constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;
    if constexpr (text_format) {
        // JSON verbose format
    } else {
        // Binary compact format
    }
}
```

### Binary Object Format

Binary uses a buffered preamble format for objects:
1. Object marker + type name + version
2. Property count
3. All property names (array)
4. All property sizes (array)
5. All property values (raw bytes)
6. End marker

This enables:
- O(1) random access to properties by name (via pre-built index)
- Graceful handling of missing/extra properties (versioning)
- Offset-based seeking without parsing values

## Shared Pointer Handling

JaiScript tracks shared_ptr instances to:
1. **Avoid duplicate serialization** - Same object serialized once, referenced by ID
2. **Reconstruct weak_ptr** - weak_ptr stores ID reference, resolved after load

### Binary Format (Compact)

```cpp
// shared_ptr: [id:uint32][object data if new]
// weak_ptr: [id:uint32]

// Writing shared_ptr:
auto [id, is_new] = ar.get_or_assign_shared_id(ptr.get());
ar.write_uint32(id);
if (is_new && ptr) {
    ptr->save(ar);  // Serialize object directly
}

// Writing weak_ptr:
uint32_t id = ar.lookup_shared_id(shared.get());
ar.write_uint32(id);
```

### JSON Format (Verbose)

```cpp
// shared_ptr: {"_type_": "ptr", "$id": id, "$val": {...}}
// weak_ptr: {"_type_": "weak_ptr", "$id": id}

ar.begin_object("ptr", 0);
ar(make_nvp("$id", id));
if (is_new && ptr) {
    ar.write_property_name("$val");
    // ... serialize object
}
ar.end_object();
```

## Version Handling

JaiScript uses a self-describing binary format where:
- Property names are embedded in the data
- Missing properties retain default values
- Extra properties (from newer versions) are skipped gracefully

```cpp
class Entity : public jai::property_owner<Entity> {
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((int), level, 1);
    JAI_PROPERTY((int), experience, 0);  // Added in v2

    // No version migration needed - format handles it automatically
};
```

For complex migrations, use explicit save/load with version checking:

```cpp
void load(archive_reader& ar) {
    uint32_t version = ar.get_version();

    ar.serialize("health", m_health);
    ar.serialize("level", m_level);

    if (version >= 2 && ar.has_property("experience")) {
        ar.serialize("experience", m_experience);
    } else {
        // v1 data - compute experience from level
        m_experience = m_level * 100;
    }
}
```

## Error Handling

Serialization errors throw `jai::serialization_error`:

```cpp
try {
    auto reader = binary_archive_reader(data, &engine);
    myObject.load(reader);
} catch (const jai::serialization_error& e) {
    std::cerr << "Serialization failed: " << e.what() << std::endl;
}
```

## Best Practices

1. **Prefer property_owner<T>** for simple data classes - automatic serialization with no boilerplate

2. **Use save/load over serialize** when you need:
   - Different logic for save vs load
   - Version migration
   - Computed/cached fields

3. **Add one-parameter forwarders** when supporting both Cereal and JaiScript

4. **Use load_and_construct** for types without default constructors

5. **Test both formats** - serialization bugs often manifest differently in binary vs JSON

6. **Consider serialize_mode::transient** for computed/cached properties that shouldn't be serialized

## Implementation Status

### Priority Order Alignment ✓

The implementation in `property_serialization.hpp` now matches the documented priority order:

| Priority | Dispatch Order |
|----------|----------------|
| 1 | Native types (containers, smart ptrs, primitives) ✓ |
| 2 | T::save/load (member) ✓ |
| 3 | T::serialize (member fallback) ✓ |
| 4 | save/load (free via ADL) ✓ |
| 5 | serialize (free via ADL fallback) ✓ |
| 6 | JAI_STATIC_BINDER ✓ |
| 7 | property_owner (last resort) ✓ |

### Design Notes

1. **serialize() as fallback**: If you have `save()` but no `load()`, the system will use `serialize()` for load (and vice versa). This allows incremental migration from bidirectional to split functions.

2. **Member before free**: Member functions always take priority over free functions, allowing you to override ADL-provided serialization with class-specific logic.

3. **property_owner is truly last resort**: Only used if NO other serialization method exists. If you inherit from `property_owner<T>` but also provide `save()`/`load()`, your explicit methods take priority.

## Comparison with Cereal

| Feature | JaiScript | Cereal |
|---------|-----------|--------|
| serialize() | Supported | Supported |
| save/load split | Supported | Supported |
| load_and_construct | Supported | Supported |
| Minimal save/load | Not supported | Supported |
| Property-based auto | Yes (property_owner) | No |
| Self-describing binary | Yes | No (fixed layout) |
| Archive detection | `jai_archive<Archive>` concept | Archive traits |
| Versioning | In-format + explicit | Explicit only |

## Related Documentation

- [JaiScript_BinarySerialization.md](JaiScript_BinarySerialization.md) - Detailed binary format specification
- [MIGRATION_PLAN.md](MIGRATION_PLAN.md) - Migrating from Cereal to JaiScript serialization

## Sources

- [Cereal Serialization Functions](https://uscilab.github.io/cereal/serialization_functions.html)
- [Cereal C++ Library](https://github.com/USCiLab/cereal)
