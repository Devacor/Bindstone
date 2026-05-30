# Cereal to JaiScript Serialization Conversion Guide

This guide documents best practices for converting Cereal-only serialization to dual Cereal/JaiScript support, and for migrating fully to JaiScript.

## Overview

JaiScript serialization coexists with Cereal during migration. The architecture supports:
- **Dual-mode serialize methods** using `if constexpr` branching
- **Property-based serialization** via `JAI_PROPERTY` macros
- **Custom save/load methods** that can compose property serialization
- **load_and_construct** for types without default constructors

---

## Pattern 1: Simple Dual-Mode Serialize (Most Common)

For simple data structs with no special construction needs, use `if constexpr`:

```cpp
#include <jaiscript/serialization/archive.hpp>

struct MyData {
    std::string name;
    int value;
    std::vector<float> items;

    // One-parameter forwarder (required for JaiScript detection)
    template<class Archive>
    void serialize(Archive& archive) {
        serialize(archive, 0);
    }

    // Two-parameter implementation with if constexpr branching
    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const /*version*/) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            archive(JAI_NVP(name), JAI_NVP(value), JAI_NVP(items));
        } else {
            archive(CEREAL_NVP(name), CEREAL_NVP(value), CEREAL_NVP(items));
        }
    }
};
```

### Key Points:
- **One-parameter forwarder is required** - JaiScript's property detection calls `serialize(ar)` with one argument
- **JAI_NVP()** is equivalent to `CEREAL_NVP()` for named value pairs
- Use `jai::serialization::make_nvp("name", value)` for explicit naming (like `cereal::make_nvp`)

---

## Pattern 2: Separate Save/Load Methods

When saving and loading require different logic (e.g., versioning, post-load initialization):

```cpp
// One-parameter forwarders REQUIRED for JaiScript method detection
// JaiScript's has_save_method/has_load_method traits check for one-parameter signatures
template<class Archive>
void save(Archive& archive) const { save(archive, 0); }

template<class Archive>
void load(Archive& archive) { load(archive, 0); }

// Two-parameter implementations with if constexpr branching
template<class Archive>
void save(Archive& archive, std::uint32_t const /*version*/) const {
    if constexpr (jai::serialization::jai_archive<Archive>) {
        archive(JAI_NVP(field1), JAI_NVP(field2));
    } else {
        archive(CEREAL_NVP(field1), CEREAL_NVP(field2));
    }
}

template<class Archive>
void load(Archive& archive, std::uint32_t const /*version*/) {
    if constexpr (jai::serialization::jai_archive<Archive>) {
        archive(JAI_NVP(field1), JAI_NVP(field2));
    } else {
        archive(CEREAL_NVP(field1), CEREAL_NVP(field2));
    }
    // Post-load initialization
    initializeFromLoadedData();
}
```

**Important:** JaiScript detects save/load methods using one-parameter trait checks like `has_save_method<T, void_t<decltype(obj.save(archive_writer&))>>`. Without one-parameter forwarders, JaiScript will NOT find your custom save/load methods and will fall back to `property_mgr` directly, skipping any custom serialization logic.

---

## Pattern 3: Base Class Serialization

For derived classes that need to serialize their base:

```cpp
class Derived : public Base {
    template<class Archive>
    void serialize(Archive& archive) {
        serialize(archive, 0);
    }

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const /*version*/) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            archive(JAI_NVP(derivedField));
            Base::serialize(archive, 0);  // Call base explicitly
        } else {
            archive(
                CEREAL_NVP(derivedField),
                cereal::make_nvp("Base", cereal::base_class<Base>(this))
            );
        }
    }
};
```

---

## Pattern 4: Property-Based Serialization (JAI_PROPERTY)

For classes using JaiScript's property system, properties are serialized automatically:

```cpp
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/macros.hpp>

class MyComponent : public jai::property_owner<MyComponent> {
public:
    JAI_PROPERTY(health, int, 100);
    JAI_PROPERTY(name, std::string, "default");
    JAI_PROPERTY(position, MV::Point<float>, {});

    // Properties auto-serialize via property_mgr
};
```

### Manual Property Serialization

You can manually invoke property serialization in custom save/load:

```cpp
// JaiScript: directly call property_mgr
void save(jai::serialization::archive_writer& ar) const {
    property_mgr.save(ar);  // Serialize all JAI_PROPERTY fields
    // Add any extra fields not in properties
    ar("computedField", computeValue());
}

void load(jai::serialization::archive_reader& ar) {
    property_mgr.load(ar);  // Deserialize all JAI_PROPERTY fields
    // Handle any extra fields
    ar("computedField", cachedComputed);
}
```

---

## Pattern 4b: Property Owner with Base Class Inheritance

JaiScript's `property_owner` supports CRTP with an optional base class parameter. This creates an inheritance chain where both properties and base class data are serialized.

### Declaration Pattern

```cpp
// Base class (also a property_owner)
class Component : public jai::property_owner<Component> {
public:
    JAI_PROPERTY(id, std::string, "");
    JAI_PROPERTY(enabled, bool, true);
};

// Derived class inherits via second template parameter
class Drawable : public jai::property_owner<Drawable, Component> {
public:
    JAI_PROPERTY(visible, bool, true);
    JAI_PROPERTY(zOrder, int, 0);
};

// Further derivation continues the chain
class Sprite : public jai::property_owner<Sprite, Drawable> {
public:
    JAI_PROPERTY(subdivisions, uint16_t, 0);
};
```

### Dual-Mode Serialization with Base Class

When using custom save/load, serialize both properties and base class:

```cpp
class Sprite : public jai::property_owner<Sprite, Drawable> {
    friend cereal::access;

public:
    JAI_PROPERTY(subdivisions, uint16_t, 0);

protected:
    template<class Archive>
    void save(Archive& archive, std::uint32_t const /*version*/) const {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            // Option 1: Serialize properties then call base
            property_mgr.save(archive);
            Drawable::save(archive, 0);  // Call base class save
        } else {
            // Cereal: serialize property values and base class
            archive(cereal::make_nvp("subdivisions", subdivisions.get()));
            archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
        }
    }

    template<class Archive>
    void load(Archive& archive, std::uint32_t const version) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            property_mgr.load(archive);
            Drawable::load(archive, 0);  // Call base class load
        } else {
            archive(cereal::make_nvp("subdivisions", subdivisions.get()));
            archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
        }
    }
};
```

### Important Notes

1. **No property duplication**: Each `property_owner<Derived, Base>` has its own `property_mgr` that only manages properties declared in that specific class. When you call `property_mgr.save()` then `Base::save()`, each level saves only its own properties - no duplication occurs.

2. **Constructor chaining**: Pass required parameters to base class constructor:
   ```cpp
   Sprite(const std::weak_ptr<Node>& a_owner)
       : jai::property_owner<Sprite, Drawable>(a_owner) {}
   ```

3. **Property access with `.get()`**: When serializing JAI_PROPERTY fields with Cereal's `make_nvp`, use `.get()` to access the underlying value:
   ```cpp
   archive(cereal::make_nvp("subdivisions", subdivisions.get()));
   ```

4. **Base class serialization order**: In JaiScript, call base class save/load explicitly. The order matters for JSON compatibility.

5. **load_and_construct with property_owner base**: When constructing types with required parameters, call property_mgr.load() after construction:
   ```cpp
   static void load_and_construct(Archive& ar, cereal::construct<Sprite>& c, std::uint32_t const v) {
       c(std::shared_ptr<Node>());  // Construct with required param
       c->load(ar, v);              // Load calls property_mgr.load() and base
       c->initialize();             // Post-load setup
   }
   ```

---

## Pattern 5: Blending Properties with Custom Logic

When you need both automatic property serialization AND custom fields/logic:

```cpp
class ComplexComponent : public jai::property_owner<ComplexComponent> {
public:
    JAI_PROPERTY(health, int, 100);
    JAI_PROPERTY(name, std::string, "");

    // Non-property fields that need manual handling
    std::weak_ptr<OtherComponent> parentRef;
    mutable int cachedValue;

    // For Cereal compatibility with if constexpr
    template<class Archive>
    void save(Archive& archive, std::uint32_t const /*version*/) const {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            // Serialize properties first
            property_mgr.save(archive);
            // Then custom fields
            archive(jai::serialization::make_nvp("parentRef", parentRef));
        } else {
            // Cereal: serialize everything explicitly
            archive(
                CEREAL_NVP(health),
                CEREAL_NVP(name),
                CEREAL_NVP(parentRef)
            );
        }
    }

    template<class Archive>
    void load(Archive& archive, std::uint32_t const /*version*/) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            property_mgr.load(archive);
            archive(jai::serialization::make_nvp("parentRef", parentRef));
        } else {
            archive(
                CEREAL_NVP(health),
                CEREAL_NVP(name),
                CEREAL_NVP(parentRef)
            );
        }
        // Post-load logic
        cachedValue = computeFromHealth();
    }
};
```

---

## Pattern 6: load_and_construct (No Default Constructor)

For types that cannot be default-constructed, both Cereal and JaiScript support `load_and_construct`:

### JaiScript Version:

```cpp
#include <jaiscript/serialization/construct.hpp>

class NoDefaultCtor {
    friend class jai::access;  // Allow JaiScript to call private methods

public:
    NoDefaultCtor(int id, std::string name) : m_id(id), m_name(std::move(name)) {}

    void save(jai::serialization::archive_writer& ar) const {
        ar("id", m_id);
        ar("name", m_name);
    }

private:
    // Private: only accessible via jai::access friendship
    static void load_and_construct(jai::serialization::archive_reader& ar,
                                   jai::serialization::construct<NoDefaultCtor>& c) {
        int id;
        std::string name;
        ar("id", id);
        ar("name", name);
        c(id, std::move(name));  // Construct the object
        // Access constructed object via c-> if needed
    }

    int m_id;
    std::string m_name;
};
```

### Cereal Version (for comparison):

```cpp
class NoDefaultCtor {
    friend class cereal::access;

public:
    NoDefaultCtor(int id, std::string name) : m_id(id), m_name(std::move(name)) {}

    template<class Archive>
    void save(Archive& ar, std::uint32_t const) const {
        ar(CEREAL_NVP(m_id), CEREAL_NVP(m_name));
    }

private:
    template<class Archive>
    static void load_and_construct(Archive& ar, cereal::construct<NoDefaultCtor>& c,
                                   std::uint32_t const) {
        int id;
        std::string name;
        ar(cereal::make_nvp("m_id", id), cereal::make_nvp("m_name", name));
        c(id, std::move(name));
    }

    int m_id;
    std::string m_name;
};
```

### Dual-Mode load_and_construct:

```cpp
class NoDefaultCtor {
    friend class cereal::access;
    friend class jai::access;

public:
    NoDefaultCtor(int id, std::string name) : m_id(id), m_name(std::move(name)) {}

    // JaiScript save (non-template for trait detection)
    void save(jai::serialization::archive_writer& ar) const {
        ar("id", m_id);
        ar("name", m_name);
    }

    // Cereal save
    template<class Archive>
    void save(Archive& ar, std::uint32_t const) const
        requires jai::serialization::not_jai_archive<Archive> {
        ar(CEREAL_NVP(m_id), CEREAL_NVP(m_name));
    }

private:
    // JaiScript load_and_construct
    static void load_and_construct(jai::serialization::archive_reader& ar,
                                   jai::serialization::construct<NoDefaultCtor>& c) {
        int id;
        std::string name;
        ar("id", id);
        ar("name", name);
        c(id, std::move(name));
    }

    // Cereal load_and_construct
    template<class Archive>
    static void load_and_construct(Archive& ar, cereal::construct<NoDefaultCtor>& c,
                                   std::uint32_t const)
        requires jai::serialization::not_jai_archive<Archive> {
        int id;
        std::string name;
        ar(cereal::make_nvp("m_id", id), cereal::make_nvp("m_name", name));
        c(id, std::move(name));
    }

    int m_id;
    std::string m_name;
};
```

---

## Pattern 7: Blending load_and_construct with Properties

For property_owner types that also need load_and_construct:

```cpp
class ConfiguredComponent : public jai::property_owner<ConfiguredComponent> {
    friend class jai::access;

public:
    JAI_PROPERTY(enabled, bool, true);
    JAI_PROPERTY(priority, int, 0);

    ConfiguredComponent(GameInstance& game) : m_game(game) {}

    void save(jai::serialization::archive_writer& ar) const {
        property_mgr.save(ar);  // All JAI_PROPERTY fields
        // Note: m_game is NOT serialized - it's passed via constructor
    }

private:
    static void load_and_construct(jai::serialization::archive_reader& ar,
                                   jai::serialization::construct<ConfiguredComponent>& c) {
        // Reconstruct with required dependency (injected externally)
        // The caller must provide the GameInstance somehow
        c(*ar.user_context<GameInstance>());
        c->property_mgr.load(ar);  // Load properties into constructed object
    }

    GameInstance& m_game;
};
```

---

## Quick Reference

### Include Headers

```cpp
#include <jaiscript/serialization/archive.hpp>      // For jai_archive concept, JAI_NVP
#include <jaiscript/serialization/construct.hpp>    // For load_and_construct support
#include <jaiscript/properties/macros.hpp>          // For JAI_PROPERTY
```

### Archive Detection

```cpp
if constexpr (jai::serialization::jai_archive<Archive>) {
    // JaiScript path
} else {
    // Cereal path
}
```

### Cereal-Only Constraint (for overloads)

```cpp
template<class Archive>
void save(Archive& ar) const requires jai::serialization::not_jai_archive<Archive> {
    // Only matches Cereal archives
}
```

### Macros

| Cereal | JaiScript | Notes |
|--------|-----------|-------|
| `CEREAL_NVP(x)` | `JAI_NVP(x)` | Named value pair with automatic naming |
| `cereal::make_nvp("name", x)` | `jai::serialization::make_nvp("name", x)` | Explicit naming |
| `cereal::base_class<Base>(this)` | `Base::serialize(archive, 0)` | Call base explicitly |

### Priority Order for JaiScript Serialization

JaiScript checks serialization support in this order:
1. **Member save()/load()** - Custom methods (highest priority)
2. **Free save()/load()** - ADL-found free functions
3. **JAI_STATIC_BINDER** - Compile-time property bindings
4. **property_owner<T>** - Auto-serialize via property_mgr
5. **Member serialize()** - Bidirectional method
6. **Free serialize()** - ADL-found bidirectional function

---

## Migration Checklist

- [ ] Add `#include <jaiscript/serialization/archive.hpp>`
- [ ] Add `if constexpr (jai::serialization::jai_archive<Archive>)` branching
- [ ] Replace `CEREAL_NVP` with `JAI_NVP` in JaiScript branches
- [ ] For base classes, call `Base::serialize(archive, 0)` instead of `cereal::base_class`
- [ ] For load_and_construct types, add `friend class jai::access;` and JaiScript version
- [ ] Test with both Cereal and JaiScript archives

---

## Known Issues and Considerations

### Conditional Serialization with property_mgr

When using `property_mgr.save()` in the JaiScript path, ALL registered JAI_PROPERTY fields are serialized unconditionally. If you have conditional serialization logic in the Cereal path (like `serializePoints()` in Drawable), be aware that:

1. `property_mgr.save()` will serialize all properties regardless of conditions
2. Do NOT also explicitly archive those same fields in the JaiScript path, or you'll get double serialization

**Example issue (avoid this):**
```cpp
if constexpr (jai::serialization::jai_archive<Archive>) {
    property_mgr.save(archive);  // Serializes all JAI_PROPERTY fields
    if (serializePoints()) {
        archive(JAI_NVP(points));  // BUG: points already serialized above!
    }
}
```

**Correct approach:** If certain JAI_PROPERTY fields need conditional serialization, either:
1. Don't use `property_mgr.save()` - serialize all fields explicitly with conditions
2. Accept that properties are always serialized in JaiScript format
3. Mark fields as non-serializable using property attributes (if supported)

### Version Handling

Version handling only applies to Cereal backward compatibility. The JaiScript path typically ignores the version parameter since it's a fresh format. Old data loaded via Cereal can be re-saved via JaiScript for migration.
