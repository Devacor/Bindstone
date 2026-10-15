# JaiScript Serialization System

This document describes the JaiScript serialization architecture, dispatch priority, and usage patterns.

## Overview

JaiScript provides a dual-format serialization system supporting both **binary** and **JSON** formats through a unified archive interface. The system is designed to:

1. **Interoperate with other archive libraries** - A type can serve another library too by branching on the `jai::serialization::jai_archive<Archive>` concept
2. **Support property-based serialization** - Types using `property_owner<T>` get automatic serialization
3. **Handle complex scenarios** - Non-default-constructible types, shared_ptr deduplication, weak_ptr reconstruction
4. **Remain extensible** - Custom serialization logic can compose with automatic property serialization

> **Scope note:** this doc covers the C++ **value** archive system (`jai_archive`). Serializing
> parsed **scripts** (AST + compiled bytecode) is a separate subsystem — jaibite save/load
> (`engine::jaibite()`, `jaibite::save()`, `engine::jaibite_load()`); see
> `docs/site/embedding/04-jaibite.html`.

## Quick Reference: Dispatch Priority

```
SAVING (archive_writer_impl)          LOADING (archive_reader_impl)
────────────────────────────          ─────────────────────────────
1. T::save (member)                   1. T::load (member)
2. T::serialize (member)              2. T::serialize (member)
3. property_owner (whole chain)       3. property_owner (whole chain)
4. primitives                         4. primitives
5. smart pointers, std containers     5. smart pointers, std containers
6. jai::property                      6. jai::property
7. serialize (free via ADL)           7. serialize (free via ADL)
8. save (free via ADL)                8. load (free via ADL)
otherwise: run-time serialization_error
```

A top-level `ar(obj)` with no key works: at array or root context the writer wraps the object in
`begin_object`/`end_object` itself.

## Serialization Method Dispatch Priority

When serializing or deserializing a type `T`, JaiScript checks for serialization support in the following priority order. You provide **either** `serialize()` **or** a `save()`/`load()` pair - not both.

The archives are `archive_writer_impl<Derived>` and `archive_reader_impl<Derived>` (`archive_impl.hpp`), instantiated as
`json_archive_writer`/`json_archive_reader` and `binary_archive_writer`/`binary_archive_reader`. The examples below write
`Archive&` for any of them.

### For Saving

```
Priority 1: T::save(Archive&, std::uint32_t) const or T::save(Archive&) const
            - Member function with explicit save logic (protected is fine with friend jai::access)
            - Can compose with property_mgr.save() internally

Priority 2: T::serialize(Archive&, std::uint32_t) or T::serialize(Archive&)
            - Bidirectional member function (used when there is no save())

Priority 3: property_owner<T>
            - save_owned_properties walks every level of the inheritance chain
            - Serializes all JAI_PROPERTY fields automatically

Priority 4: Primitives (arithmetic, enum, std::string)

Priority 5: Smart pointers and std containers
            - std::shared_ptr, std::weak_ptr, std::unique_ptr
            - std::pair, std::optional, std::variant, std::tuple, std::array
            - map-like, set-like and sequence containers

Priority 6: jai::property<T> (writes the wrapped value)

Priority 7: serialize(Archive&, T&) via ADL

Priority 8: save(Archive&, const T&) via ADL

If none match: a run-time jai::serialization_error naming the type
```

### For Loading

```
Priority 1: T::load(Archive&, std::uint32_t) or T::load(Archive&)
Priority 2: T::serialize(Archive&, std::uint32_t) or T::serialize(Archive&)
Priority 3: property_owner<T> (load_owned_properties, whole chain)
Priority 4: Primitives
Priority 5: Smart pointers and std containers
Priority 6: jai::property<T>
Priority 7: serialize(Archive&, T&) via ADL
Priority 8: load(Archive&, T&) via ADL

If none match: a run-time jai::serialization_error naming the type
```

`JAI_STATIC_BINDER` is not part of this dispatch: it feeds the type-erased property path
(`property_serialization.hpp`, `static_binder_impl.hpp`).

### Shared Pointers and load_and_construct

When deserializing a `std::shared_ptr<T>` record, a static member `T::load_and_construct(Archive&, construct<T>&)`
(reached through `friend jai::access`) is used whenever it exists, before default construction. Without it, `T` is
default-constructed and loaded through the normal dispatch; a type with neither raises `serialization_error`.
`std::unique_ptr<T>` always default-constructs and raises `serialization_error` for a type without a default
constructor.

```cpp
// Member function (with jai::access friendship)
class MyType {
    friend class jai::access;

    // Private constructor - no default
    MyType(int id, std::string name);

    template<class Archive>
    static void load_and_construct(Archive& ar, construct<MyType>& c) {
        int id = ar.read_int32();
        std::string name = ar.read_string();
        c(id, name);  // Construct the object
        c->post_init_stuff();  // Access via operator->
    }

    template<class Archive>
    void save(Archive& ar) const {
        ar.write_int32(m_id);
        ar.write_string(m_name);
    }
};
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
   template<class Archive>
   static void load_and_construct(Archive& ar, construct<MyCat>& c) {
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
    template<class Archive>
    void save(Archive& ar) const {
        ar.serialize("health", m_health);
        ar.serialize("maxHealth", m_max_health);
    }

    template<class Archive>
    void load(Archive& ar) {
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

    template<class Archive>
    void save(Archive& ar) const {
        // Serialize JAI_PROPERTY fields automatically
        property_mgr.save(ar);

        // Custom serialization for non-property fields
        ar.serialize("entities", entities);
    }

    template<class Archive>
    void load(Archive& ar) {
        property_mgr.load(ar);
        ar.serialize("entities", entities);
    }
};
```

### Pattern 5: Serving Another Archive Library

A type that another archive library also serializes branches on the `jai_archive` concept:

```cpp
class MyComponent {
    int m_value;

    template<class Archive>
    void save(Archive& ar, std::uint32_t version) const {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            ar.serialize("value", m_value);
        } else {
            // the other library's form
        }
    }
};
```

Versioned signatures — `save(Archive&, std::uint32_t)` / `load(Archive&, std::uint32_t)` — are detected natively and
preferred: the detection traits (`has_versioned_save_method` / `has_simple_save_method`, combined in `has_save_method`
in `archive_impl.hpp`, via the `jai::access` wrappers) probe both arities and call the versioned overload with version
0 when it exists. A one-parameter forwarder is not needed.

### Pattern 6: Free Functions via ADL

For types you can't modify (third-party libraries):

```cpp
namespace third_party {
    struct Vector3 { float x, y, z; };
}

// In third_party namespace for ADL to find:
namespace third_party {
    template<class Archive>
    void save(Archive& ar, const Vector3& v) {
        ar.serialize("x", v.x);
        ar.serialize("y", v.y);
        ar.serialize("z", v.z);
    }

    template<class Archive>
    void load(Archive& ar, Vector3& v) {
        ar.serialize("x", v.x);
        ar.serialize("y", v.y);
        ar.serialize("z", v.z);
    }
}
```

## Archive API Reference

### Writer Methods

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

### Reader Methods

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

`is_text_format` is a static constexpr trait of each archive; pointer records do not depend on it (both formats write
the same record shapes, see Shared Pointer Handling).

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

### Pointer records (both formats)

Binary and JSON write the same record shapes, keyed by the archive's per-save object ids (the binary writer encodes
them as ordinary records and properties):

| Value | Written as |
|---|---|
| `shared_ptr`, first time its object is written | `{"$id": N, "$val": {...}}`; through a base pointer holding a registered derived class, `{"$id": N, "$type": "Derived", "$val": {...}}` |
| `shared_ptr` to an object already written | `{"$id": N}`, re-aliased on load |
| `weak_ptr` | `{"$ref": N}`, with the object's `$val` (and `$type`) inline when the weak pointer is its first reference in the save; an expired one is null |
| `unique_ptr` | `{"$val": {...}}` by the pointer's static type, no `$type` (a derived object writes only the base's fields) |
| null | null |

The public statement of these shapes is the site's serialization chapter ("Wire formats"); keep the two in step.

### Archive user context

`set_user_context<T>(T*)` attaches a pointer the code inside save/load reads back with
`ar.template get_user_context<T>()` (null when none of that type was set); the convenience loads that take a context
argument (`from_json(eng, text, ctx)` and its binary twins, `convenience.hpp`) set it.

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
template<class Archive>
void load(Archive& ar) {
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

3. **Use load_and_construct** for types without default constructors held by `shared_ptr`

4. **Test both formats** - serialization bugs often manifest differently in binary vs JSON

5. **Consider serialize_mode::transient** for computed/cached properties that shouldn't be serialized

## Design Notes

1. **serialize() as fallback**: If you have `save()` but no `load()`, the system uses `serialize()` for load (and vice versa).

2. **Member before free**: Member functions take priority over free functions, so class-specific logic overrides ADL-provided serialization.

3. **Explicit methods before property_owner**: If you inherit from `property_owner<T>` but also provide `save()`/`load()` or `serialize()`, your explicit methods take priority.
