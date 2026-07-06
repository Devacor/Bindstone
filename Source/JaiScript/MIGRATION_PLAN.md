# ChaiScript to JaiScript Migration Plan

> **DONE as of 2026-07 — see the Bindstone integration audit.** ChaiScript and Cereal are fully
> replaced in live Bindstone code; engines are constructed through `MV::makeScriptEngine()`.
> Historical plan below; details may be stale.

## Overview

Replace ChaiScript with JaiScript throughout Bindstone, leveraging:
- **Auto-registration** via `jai::registrar<T, Context>`
- **Property auto-binding** via `jai::dynamic_binder<T>::auto_bind()`
- **Signal/receiver patterns** for reactive callbacks with `receiver_owner`
- **Observable properties** for change notifications
- **JaiScript serialization** to replace Cereal (binary + JSON archive support)

---

## JaiScript Infrastructure Summary

### Already Implemented in JaiScript

| Feature | Location | Usage |
|---------|----------|-------|
| `jai::registrar<T, Context>` | [registrar.hpp](include/jaiscript/core/registrar.hpp) | Auto-registers bindings at file scope |
| `jai::dynamic_binder<T>` | [dynamic_binder.hpp](include/jaiscript/core/dynamic_binder.hpp) | Fluent API for class bindings |
| `.auto_bind()` | dynamic_binder | Auto-binds all properties from property_owner |
| `jai::property_owner<Derived, Bases...>` | [property_manager.hpp](include/jaiscript/properties/property_manager.hpp) | CRTP base with property_manager + receiver_owner |
| `JAI_PROPERTY(type, name)` | properties | Declares serializable property |
| `jai::signal<T>` | [signal.hpp](include/jaiscript/signals/signal.hpp) | Thread-safe signal/slot |
| `jai::receiver_owner` | [receiver_owner.hpp](include/jaiscript/signals/receiver_owner.hpp) | Auto-disconnect on destruction |
| `jai::observable_property<T>` | [observable_property.hpp](include/jaiscript/properties/observable_property.hpp) | Property with on_change signal |
| `jai::serialization::archive_writer` | [archive.hpp](include/jaiscript/serialization/archive.hpp) | Cereal replacement - write |
| `jai::serialization::archive_reader` | [archive.hpp](include/jaiscript/serialization/archive.hpp) | Cereal replacement - read |
| `jai::serialization::binary_archive_*` | [binary_archive.hpp](include/jaiscript/serialization/binary_archive.hpp) | Binary format (like Cereal binary) |
| `jai::serialization::json_archive_*` | [json_archive.hpp](include/jaiscript/serialization/json_archive.hpp) | JSON format (like Cereal JSON) |

### Key Patterns

#### 1. Auto-Registration (jai::registrar)
```cpp
// File scope - runs automatically before main()
static jai::registrar<Point, MV::Services> _point_registrar([](jai::engine& eng, const MV::Services& services) {
    jai::dynamic_binder<Point>(eng, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
});

// At engine init, trigger all registrars:
jai::bind_registrar<MV::Services>(engine, services);
```

#### 2. Property Auto-Binding
```cpp
// For classes using jai::property_owner
class Entity : public jai::property_owner<Entity> {
    JAI_PROPERTY((int), health);
    JAI_PROPERTY((std::string), name);
};

// Auto-binds all JAI_PROPERTY members:
jai::dynamic_binder<Entity>(eng, "Entity")
    .auto_bind()  // Binds health, name automatically
    .build();
```

#### 3. Signal/Receiver with track()
```cpp
class Player : public jai::property_owner<Player> {
public:
    Player(jai::signal<void(int)>& damage_signal) {
        // track() ties receiver lifetime to this object
        track(damage_signal.connect([this](int dmg) {
            health -= dmg;
        }));
    }
};
```

#### 4. JaiScript Serialization (Cereal Replacement)
```cpp
// Saving with JaiScript (replaces Cereal)
jai::serialization::binary_archive_writer writer(engine);
entity.property_mgr.save(writer);
auto bytes = writer.get_data();

// Loading with JaiScript
jai::serialization::binary_archive_reader reader(engine, bytes);
entity.property_mgr.load(reader);
entity.post_deserialize(reader);  // Hook for initialization
```

---

## Current State

### ChaiScript Hook Files (10 files, ~1200 lines total)
| File | Purpose | Lines |
|------|---------|-------|
| [gameHooks.cxx](../../Game/gameHooks.cxx) | BattleEffect, Creature, Building, Team, Game, GameData, Player | ~550 |
| [standardHooks.cxx](../../Game/standardHooks.cxx) | Type conversions, print(), StandardMessages | ~50 |
| [gameNetworkHooks.cxx](../../Game/NetworkLayer/gameNetworkHooks.cxx) | Network-related bindings | TBD |
| [interfaceHooks.cxx](../../Game/Interface/interfaceHooks.cxx) | UI/Interface bindings | TBD |
| [renderHooks.cxx](../../MV/Render/renderHooks.cxx) | BoxAABB, Color, TexturePoint, Size, Point | ~200 |
| [sceneHooks.cxx](../../MV/Render/Scene/sceneHooks.cxx) | Node, Component, Drawable, Text, etc. | ~400 |
| [utilityHooks.cxx](../../MV/Utility/utilityHooks.cxx) | General utilities | TBD |
| [networkHooks.cxx](../../MV/Network/networkHooks.cxx) | Network primitives | TBD |
| [artificialIntelligenceHooks.cxx](../../MV/ArtificialIntelligence/artificialIntelligenceHooks.cxx) | PathAgent, Grid | TBD |
| [engineHooks.cxx](../../MV/Script/engineHooks.cxx) | Includes other MV hooks | ~6 |

### Script Files (39 files)
- **Creatures**: 24 files (main.script + mainClient.script for 12 creatures)
- **Buildings**: 6 files (3 buildings)
- **BattleEffects**: 2 files
- **Interface**: 8 files (Login, Main, interfaceManager)
- **Prefabs**: 1 file

### Serialization Systems Comparison
| Feature | Cereal (MV) | JaiScript Serialization |
|---------|-------------|------------------------|
| Binary format | `cereal::BinaryArchive` | `jai::serialization::binary_archive_*` |
| JSON format | `cereal::JSONArchive` | `jai::serialization::json_archive_*` |
| Named values | `cereal::make_nvp()` | Automatic from property names |
| Shared ptr tracking | Built-in | Built-in (`shared_ptr_ids_`) |
| Post-deserialize hook | Manual | `post_deserialize(archive_reader&)` |
| Depth limit | None | Configurable (`JAI_MAX_SERIALIZATION_DEPTH`) |
| Property keys | Stored in archive | Stored in archive |
| Missing property | Exception | Configurable (skip or error) |

### Property Systems Comparison
| Feature | MV::PropertyOwner | jai::property_owner |
|---------|-------------------|---------------------|
| Macro | `MV_PROPERTY(type, name)` | `JAI_PROPERTY((type), name)` |
| Manager | `PropertyManager` | `property_manager` |
| Access | `reflection().get<T>(name)` | `property_mgr.get<T>(name)` |
| Observable | `ObservableProperty<T>` | `observable_property<T>` |
| Receiver tracking | None | Built-in `receiver_owner` |
| Auto-bind to script | Manual | `.auto_bind()` |
| Inheritance | Manual | CRTP with base tracking |
| Serialization | Cereal | JaiScript archive |

---

## Migration Strategy

### Phase 1: Property Owner Migration (Early - Enables auto_bind)

**Goal**: Migrate MV::PropertyOwner classes to jai::property_owner early to enable `auto_bind()` and get the benefits of the new property system while keeping Cereal for serialization.

#### 1A. The Key Insight: Direct Cereal Compatibility

No adapter files or shims needed. `jai::property<T>` provides direct access to the underlying value via `get()`, which works seamlessly with Cereal's existing serialization:

```cpp
// jai::property<T> already has:
T& get() { return m_value; }
const T& get() const { return m_value; }

// This works directly with Cereal:
ar(cereal::make_nvp("health", health.get()));
```

#### 1B. Migrating a Class: Before and After

**Before (MV::PropertyOwner + Cereal):**
```cpp
class Creature : public MV::PropertyOwner, public MV::Scene::Component {
    MV_PROPERTY((StatTemplate), stats);
    MV_PROPERTY((std::string), skin);

    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("stats", stats));
        ar(cereal::make_nvp("skin", skin));
    }
};
```

**After (jai::property_owner + Cereal via get()):**
```cpp
class Creature : public jai::property_owner<Creature, MV::Scene::Component> {
    JAI_PROPERTY((StatTemplate), stats);
    JAI_PROPERTY((std::string), skin);

    // Cereal compatibility - direct access to property values
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("stats", stats.get()));
        ar(cereal::make_nvp("skin", skin.get()));
    }
};

// Now auto_bind() works!
static jai::registrar<Creature, JaiContext> _creature([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<Creature>(eng, "Creature")
        .base_class<MV::Scene::Component>()
        .auto_bind()  // Binds stats, skin automatically!
        .method("alive", &Creature::alive)
        .build();
});
```

#### 1C. Optional: Bulk Serialization Helper

For classes with many properties, add a helper method to `jai::property_owner`:

```cpp
// In jai::property_owner (already in JaiScript)
template<class Archive, typename... Props>
void serialize_props(Archive& ar, Props&... props) {
    (ar(props.get()), ...);
}

// Usage:
template<class Archive>
void serialize(Archive& ar) {
    serialize_props(ar, stats, skin, position, velocity);  // Fold expression
}
```

Or for named serialization with Cereal:
```cpp
// Standalone helper function (can add to property_owner or use free function)
template<class Archive, typename T>
void serialize_named(Archive& ar, const char* name, jai::property<T>& prop) {
    ar(cereal::make_nvp(name, prop.get()));
}

// Usage:
template<class Archive>
void serialize(Archive& ar) {
    serialize_named(ar, "stats", stats);
    serialize_named(ar, "skin", skin);
}
```

#### 1D. Inheritance Pattern: Sprite → Drawable → Component

**Key Insight**: Each class uses `jai::property_owner<Self, Base>` CRTP. This is NOT diamond inheritance - it's a linear chain where each level has its own `property_mgr` and the `_jai_base_types` tuple links them for traversal.

**Difference from MV::PropertyOwner:**
- MV: ONE `PropertyManager` per instance, all properties register into it
- JaiScript: Type-level schema + per-level `property_mgr`, chain linked via `_jai_base_types`

```cpp
// Base class
class Component : public std::enable_shared_from_this<Component>,
                  public jai::property_owner<Component> {
    JAI_PROPERTY((std::string), componentId);  // → Component's property_mgr

    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("componentId", componentId.get()));
    }
};

// Derived - uses property_owner<Drawable, Component>
class Drawable : public jai::property_owner<Drawable, Component> {
    JAI_PROPERTY((bool), shouldDraw);           // → Drawable's property_mgr
    JAI_PROPERTY((std::string), shaderProgramId);
    // _jai_base_types = tuple<Component>

    template<class Archive>
    void serialize(Archive& ar) {
        Component::serialize(ar);  // Call base chain
        ar(cereal::make_nvp("shouldDraw", shouldDraw.get()));
        ar(cereal::make_nvp("shaderProgramId", shaderProgramId.get()));
    }
};

// Further derived - uses property_owner<Sprite, Drawable>
class Sprite : public jai::property_owner<Sprite, Drawable> {
    JAI_PROPERTY((std::string), texturePath);  // → Sprite's property_mgr
    // _jai_base_types = tuple<Drawable>

    template<class Archive>
    void serialize(Archive& ar) {
        Drawable::serialize(ar);  // Call base chain
        ar(cereal::make_nvp("texturePath", texturePath.get()));
    }
};
```

**Why this works (not diamond inheritance):**
- Linear chain: `Sprite` → `property_owner<Sprite, Drawable>` → `Drawable` → `property_owner<Drawable, Component>` → `Component` → `property_owner<Component>`
- Each level has its OWN `property_mgr` for its OWN properties
- `_jai_base_types` tuple links the chain for traversal
- `auto_bind()` walks `_jai_base_types` recursively to collect ALL properties
- Serialization walks the chain via base class `serialize()` calls
- `receivers_` and `track()` work at each level independently

**Registration with auto_bind():**
```cpp
// Component registration
static jai::registrar<Component, JaiContext> _component([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<Component>(eng, "Component")
        .auto_bind()  // Binds componentId from Component's property_mgr
        .build();
});

// Drawable registration - auto_bind walks _jai_base_types to include Component
static jai::registrar<Drawable, JaiContext> _drawable([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<Drawable>(eng, "Drawable")
        .base_class<Component>()
        .auto_bind()  // Binds shouldDraw, shaderProgramId + walks to Component
        .method("hide", &Drawable::hide)
        .method("show", &Drawable::show)
        .build();
});

// Sprite registration
static jai::registrar<Sprite, JaiContext> _sprite([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<Sprite>(eng, "Sprite")
        .base_class<Drawable>()
        .auto_bind()  // Binds texturePath + walks to Drawable + Component
        .build();
});
```

#### 1E. Benefits of Early Migration

| Benefit | Description |
|---------|-------------|
| `auto_bind()` works immediately | Walks `_jai_base_types` chain to collect all properties |
| `receiver_owner` at each level | Each class has its own `receivers_` for signal cleanup |
| Type-level schema | Properties registered globally per-type, not per-instance |
| Linear CRTP chain | `property_owner<Self, Base>` pattern - no diamond inheritance |
| Cereal compatibility | Existing save files still work via `get()` |
| No adapter files | Direct integration, no shims or temporary inheritance |
| Incremental | Can migrate one class at a time |

### Phase 2: Bridge Infrastructure

**Goal**: Create the JaiScript bridge with context type for registrar.

#### 2A. JaiScript Bridge Header

```cpp
// New file: Source/MV/Script/jaiscript_bridge.hpp
#pragma once

#include <jaiscript/jaiscript.hpp>
#include <MV/Utility/jai_cereal_adapter.hpp>

namespace MV::Script {

// Context type for registrar - passed to all registration functions
using JaiContext = MV::Services;

} // namespace MV::Script
```

#### 2B. Registration Pattern

Each hook file becomes a set of `jai::registrar` instances:

```cpp
// Source/MV/Render/jaiRenderHooks.cxx
#include <MV/Script/jaiscript_bridge.hpp>

using namespace MV::Script;

// Point registration - runs at file scope
static jai::registrar<MV::Point<>, JaiContext> _point([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<MV::Point<>>(eng, "Point")
        .constructor<>()
        .constructor<float, float>()
        .property("x", &MV::Point<>::x)
        .property("y", &MV::Point<>::y)
        .method("distance", &MV::Point<>::distance)
        .method("normalize", &MV::Point<>::normalize)
        .build();
});

// Size registration
static jai::registrar<MV::Size<>, JaiContext> _size([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<MV::Size<>>(eng, "Size")
        .constructor<>()
        .constructor<float, float>()
        .property("width", &MV::Size<>::width)
        .property("height", &MV::Size<>::height)
        .build();
});

// Color registration
static jai::registrar<MV::Color, JaiContext> _color([](jai::engine& eng, const JaiContext&) {
    jai::dynamic_binder<MV::Color>(eng, "Color")
        .constructor<>()
        .constructor<float, float, float, float>()
        .property("r", &MV::Color::R)
        .property("g", &MV::Color::G)
        .property("b", &MV::Color::B)
        .property("a", &MV::Color::A)
        .build();
});
```

#### 2C. Engine Initialization

```cpp
// Source/MV/Script/jaiscript_init.cpp
#include <jaiscript/jaiscript.hpp>
#include <MV/Script/jaiscript_bridge.hpp>

namespace MV::Script {

std::shared_ptr<jai::engine> create_jaiscript_engine(const MV::Services& services) {
    auto eng = std::make_shared<jai::engine>();

    // Trigger all file-scope registrars
    jai::bind_registrar<JaiContext>(*eng, services);

    return eng;
}

} // namespace MV::Script
```

### Phase 3: Convert Hook Files

**Pattern Mapping:**
| ChaiScript | JaiScript |
|------------|-----------|
| `chaiscript::user_type<T>()` | `jai::dynamic_binder<T>(eng, "Name")` |
| `chaiscript::constructor<T()>()` | `.constructor<>()` |
| `chaiscript::fun(&T::method)` | `.method("name", &T::method)` |
| `chaiscript::fun([](T& self){ ... })` | `.method("name", [](T& self){ ... })` |
| `chaiscript::base_class<Base, Derived>()` | `.base_class<Base>()` |
| `a_script.add_global(var, "name")` | `eng.add_global("name", value)` |
| `chaiscript::type_conversion<A,B>()` | `eng.add_conversion<A,B>([](A& a){ return B(a); })` |

**Conversion Order** (dependency order):
1. **utilityHooks.cxx** → `jaiUtilityHooks.cxx` (no deps)
2. **renderHooks.cxx** → `jaiRenderHooks.cxx` (Point, Size, Color, BoxAABB)
3. **networkHooks.cxx** → `jaiNetworkHooks.cxx`
4. **sceneHooks.cxx** → `jaiSceneHooks.cxx` (depends on render)
5. **artificialIntelligenceHooks.cxx** → `jaiAIHooks.cxx`
6. **standardHooks.cxx** → `jaiStandardHooks.cxx` (type conversions, print)
7. **interfaceHooks.cxx** → `jaiInterfaceHooks.cxx`
8. **gameNetworkHooks.cxx** → `jaiGameNetworkHooks.cxx`
9. **gameHooks.cxx** → `jaiGameHooks.cxx` (Creature, Building, BattleEffect)

### Phase 4: Script File Conversion

**Syntax Conversion:**
| ChaiScript | JaiScript |
|------------|-----------|
| `def func(args)` | `function func(args) -> auto` |
| `var x = val` | `auto x = val` |
| `var x := val` | `auto x = val` |
| `fun(args){ body }` | `[](args){ body }` |
| `global x = val` | `var x = val` (at module scope) |
| `self["key"]` | `self["key"]` (same) |
| `.is_var_null()` | `== null` |
| `.is_var_undef()` | Use `null` instead |

**File Extension**: Keep `.script` (familiar to team, no tooling changes needed)

**Conversion Order:**
1. Interface scripts (simplest, 8 files)
2. Building scripts (6 files)
3. BattleEffect scripts (2 files)
4. Creature scripts (24 files, most complex)
5. Prefabs (1 file)

### Phase 5: Cereal Replacement

**Goal**: Replace Cereal serialization with JaiScript's native archive system.

#### 5A. Understanding the Difference

**Cereal Pattern (MV::PropertyManager):**
```cpp
template <class Archive>
void save(Archive& ar) const {
    std::vector<std::string> keys;
    for (const auto& [name, prop] : properties) {
        if (prop->allowSave()) keys.push_back(name);
    }
    ar(cereal::make_nvp("PropertyKeys", keys));
    for (const auto& name : keys) {
        properties.at(name)->save(ar);  // Each property saves itself
    }
}
```

**JaiScript Pattern (jai::property_manager):**
```cpp
void save(serialization::archive_writer& ar) const {
    // Similar pattern but using JaiScript's type-safe primitives
    ar.write_uint32(static_cast<uint32_t>(m_properties.size()));
    for (const auto& [name, prop] : m_properties) {
        ar.write_string(name);
        prop->save(ar);  // Uses property_serialization.hpp
    }
}
```

#### 5B. Conversion Strategy for Existing Classes

**For classes using MV::PropertyOwner:**
1. Add parallel `jai::property_owner` CRTP base
2. Add `JAI_PROPERTY` alongside `MV_PROPERTY` temporarily
3. Implement save/load using JaiScript archives
4. Validate output matches Cereal output
5. Remove MV::PropertyOwner and Cereal

**For classes with manual `serialize()` methods:**
```cpp
// Before (Cereal)
template<class Archive>
void serialize(Archive& ar) {
    ar(cereal::make_nvp("health", health_));
    ar(cereal::make_nvp("position", position_));
}

// After (JaiScript) - Convert to properties or explicit save/load
class Entity : public jai::property_owner<Entity> {
    JAI_PROPERTY((int), health);
    JAI_PROPERTY((Point), position);
};
// Serialization is automatic via property_mgr.save()/load()
```

#### 5C. Archive Format Conversion Tool

Create a tool to convert existing save files:
```cpp
// Tool: cereal_to_jaiscript_converter
void convert_save_file(const std::string& cereal_path, const std::string& jai_path) {
    // 1. Load with Cereal
    std::ifstream is(cereal_path, std::ios::binary);
    cereal::BinaryInputArchive cereal_ar(is);
    GameState state;
    cereal_ar(state);

    // 2. Save with JaiScript
    jai::serialization::binary_archive_writer writer(engine);
    state.property_mgr.save(writer);

    std::ofstream os(jai_path, std::ios::binary);
    auto data = writer.get_data();
    os.write(reinterpret_cast<const char*>(data.data()), data.size());
}
```

#### 5D. Validation Strategy

Run both serialization systems in parallel during migration:
```cpp
void save_with_validation(Entity& entity, const std::string& path) {
    // Save with Cereal
    std::ostringstream cereal_stream;
    {
        cereal::BinaryOutputArchive ar(cereal_stream);
        entity.legacy_serialize(ar);
    }

    // Save with JaiScript
    jai::serialization::binary_archive_writer writer(engine);
    entity.property_mgr.save(writer);

    // Compare sizes (rough validation)
    if (cereal_stream.str().size() != writer.get_data().size()) {
        log_warning("Serialization size mismatch for {}", typeid(Entity).name());
    }

    // Use JaiScript output
    write_file(path, writer.get_data());
}
```

#### 5E. Migration Steps for Cereal Replacement

1. **Audit all Cereal usage**: Find all `serialize()` methods and Cereal includes
   ```bash
   grep -r "cereal::" Source/ --include="*.hpp" --include="*.cpp"
   grep -r "serialize(" Source/ --include="*.hpp" --include="*.cpp"
   ```

2. **Categorize classes**:
   - Uses MV::PropertyOwner → Migrate to jai::property_owner
   - Manual serialize() → Convert to JAI_PROPERTY or manual save/load
   - STL containers only → Use JaiScript container serialization

3. **Create shim layer**: Allow reading old Cereal format, writing new JaiScript format

4. **Version save files**: Add format version header to detect old vs new
   ```cpp
   constexpr uint32_t SAVE_FORMAT_CEREAL = 1;
   constexpr uint32_t SAVE_FORMAT_JAISCRIPT = 2;
   ```

5. **Migrate incrementally**: One class at a time with validation

6. **Remove Cereal**: After all classes converted and validated

---

## Best Practices

### 1. Use track() for Signal Connections

**Problem**: Dangling callbacks when object is destroyed.

**Solution**: Use `track()` in `property_owner` classes:
```cpp
class GameObserver : public jai::property_owner<GameObserver> {
public:
    GameObserver(GameInstance& game) {
        // Auto-disconnects when GameObserver is destroyed
        track(game.on_creature_spawned.connect([this](Creature& c) {
            handle_spawn(c);
        }));
        track(game.on_turn_ended.connect([this]() {
            handle_turn_end();
        }));
    }
};
```

For non-property_owner classes, use `receiver_owner` directly:
```cpp
class MyClass {
    jai::receiver_owner receivers_;
public:
    void setup(jai::signal<void(int)>& sig) {
        receivers_.track(sig.connect([this](int x) { ... }));
    }
};
```

### 2. Observable Properties for UI Binding

Use `observable_property<T>` when UI needs to react to changes:
```cpp
class Player : public jai::property_owner<Player> {
public:
    jai::observable_property<int> health{property_mgr, "health", 100};
    jai::observable_property<int> gold{property_mgr, "gold", 0};

    Player() {
        // UI can subscribe to changes
        track(health.on_change.connect([](const int& old_val, const int& new_val) {
            // Update health bar
        }));
    }
};
```

### 3. Signals vs Observable Properties

| Use Case | Recommendation |
|----------|----------------|
| Value changed notification | `observable_property<T>` |
| Event with multiple parameters | `signal<void(A, B, C)>` |
| Property needs serialization | `observable_property<T>` |
| One-shot event | `signal<void(Args...)>` |
| Multiple observers | Both work equally well |

### 4. Property Owner Inheritance

Leverage CRTP base tracking for inheritance hierarchies:
```cpp
class Entity : public jai::property_owner<Entity> {
    JAI_PROPERTY((int), id);
};

class Creature : public jai::property_owner<Creature, Entity> {
    JAI_PROPERTY((int), health);  // Also has id from Entity
};

// all_property_names() includes inherited properties
auto names = Creature::all_property_names();  // ["id", "health"]
```

### 5. Use Registrar for Modular Bindings

Keep registrations in the same file as the class definition when possible:
```cpp
// creature.hpp
class Creature { ... };

// creature.cpp
#include <MV/Script/jaiscript_bridge.hpp>

static jai::registrar<Creature, MV::Script::JaiContext> _creature_binding(
    [](jai::engine& eng, const MV::Script::JaiContext&) {
        jai::dynamic_binder<Creature>(eng, "Creature")
            // ...
            .build();
    });
```

### 6. Post-Deserialize Hooks

Always implement `post_deserialize()` for classes that need reinitialization:
```cpp
class NetworkedEntity : public jai::property_owner<NetworkedEntity> {
    JAI_PROPERTY((int), network_id);

    NetworkConnection* connection_ = nullptr;  // Not serialized

    void post_deserialize(jai::serialization::archive_reader& ar) override {
        jai::property_owner<NetworkedEntity>::post_deserialize(ar);
        // Reconnect to network
        connection_ = NetworkManager::instance().get_connection(network_id);
    }
};
```

### 7. Hot Reload Support

JaiScript supports hot-reloading class definitions. Leverage this during development:
```cpp
// dynamic_binder registers type info that survives hot reload
jai::dynamic_binder<Creature>(eng, "Creature")
    .enable_hot_reload()  // Preserves instances across reloads
    .auto_bind()
    .build();
```

---

## Decisions

1. **Auto-Registration**: Use existing `jai::registrar<T, Context>` (not custom JaiRegistrar)
2. **Property Owner Migration First**: Migrate game entities to `jai::property_owner` EARLY (Phase 1)
   - Enables `auto_bind()` for all subsequent hook conversions
   - Keep Cereal via `get()` - no adapter files or shims needed
   - `jai::property_manager` becomes the source of truth
3. **Migration Style**: Incremental for property_owner, then big-bang for scripting
4. **Serialization**:
   - Phase 1: Use `prop.get()` with existing Cereal `serialize()` methods
   - Phase 5: Replace Cereal with JaiScript archives after ChaiScript removed
   - Add format versioning for backwards compatibility
5. **Thread Safety**: Signals are thread-safe by default (opt-out via `JAI_SIGNALS_NO_THREADSAFE`)
6. **File Extension**: Keep `.script` (familiar to team, no tooling changes needed)

---

## Files Summary

### New Files to Create
| File | Purpose |
|------|---------|
| `Source/MV/Script/jaiscript_bridge.hpp` | Bridge utilities, context type |
| `Source/MV/Script/jaiscript_init.cpp` | Engine creation, registrar trigger |
| `Source/MV/Utility/jaiUtilityHooks.cxx` | Utility bindings |
| `Source/MV/Render/jaiRenderHooks.cxx` | Point, Size, Color, BoxAABB |
| `Source/MV/Render/Scene/jaiSceneHooks.cxx` | Node, Component, Drawable |
| `Source/MV/Network/jaiNetworkHooks.cxx` | Network primitives |
| `Source/MV/ArtificialIntelligence/jaiAIHooks.cxx` | PathAgent, Grid |
| `Source/Game/jaiStandardHooks.cxx` | Type conversions, print |
| `Source/Game/Interface/jaiInterfaceHooks.cxx` | UI bindings |
| `Source/Game/NetworkLayer/jaiGameNetworkHooks.cxx` | Game network |
| `Source/Game/jaiGameHooks.cxx` | Creature, Building, BattleEffect |
| `Tools/cereal_to_jai_converter.cpp` | Save file format converter |

### Files to Modify
| File | Change |
|------|--------|
| `Source/Game/game.cpp` | Add JaiScript engine init |
| `Source/SolutionSpecific/*/main.cpp` | Bootstrap changes |
| All 39 `.script` files | Syntax conversion |
| All classes with `serialize()` | Convert to JAI_PROPERTY |
| All classes with MV::PropertyOwner | Migrate to jai::property_owner |

### Files to Remove (after migration)
- All `*Hooks.cxx` ChaiScript binding files
- `External/ChaiScript-6.1.0/` directory
- Cereal dependency (after serialization migration complete)

---

## Implementation Order

### Step 1: Property Owner Migration (Phase 1)
1. Ensure `jai::property<T>::get()` is available (already in JaiScript)
2. Identify key game entities to migrate first (Creature, Building, BattleEffect)
3. Migrate one class at a time: replace `MV::PropertyOwner` → `jai::property_owner`
4. Update `serialize()` methods: change `ar(prop)` → `ar(prop.get())`
5. Verify Cereal serialization still works (existing save files load correctly)

### Step 2: Bridge Infrastructure (Phase 2)
1. Create `jaiscript_bridge.hpp` with context type (`JaiContext = MV::Services`)
2. Create `jaiscript_init.cpp` with engine factory
3. Verify `jai::bind_registrar<JaiContext>` works

### Step 3: Convert Hooks (Phase 3 - dependency order)
1. utilityHooks → jaiUtilityHooks (no deps)
2. renderHooks → jaiRenderHooks (Point, Size, Color, BoxAABB)
3. networkHooks → jaiNetworkHooks
4. sceneHooks → jaiSceneHooks (depends on render)
5. artificialIntelligenceHooks → jaiAIHooks
6. standardHooks → jaiStandardHooks (type conversions, print)
7. interfaceHooks → jaiInterfaceHooks
8. gameNetworkHooks → jaiGameNetworkHooks
9. gameHooks → jaiGameHooks (uses `auto_bind()` for migrated property_owner classes!)

### Step 4: Convert Scripts (Phase 4)
1. Create conversion script/tool (ChaiScript → JaiScript syntax)
2. Convert by complexity: Interface → Buildings → BattleEffects → Creatures → Prefabs

### Step 5: Integration & ChaiScript Removal
1. Wire up JaiScript engine in Game class
2. Optional: Run both engines in parallel for validation
3. Full test pass
4. Remove ChaiScript dependency

### Step 6: Cereal Replacement (Phase 5)
1. Classes already use `jai::property_owner` (from Step 1)
2. Add format versioning to save files
3. Switch `save_properties_cereal` → `property_mgr.save(jai_archive)`
4. Create save file converter tool for existing files
5. Validate all serialization with A/B testing
6. Remove Cereal dependency

---

## Error Handling

### Script Errors
JaiScript provides detailed error messages with line numbers:
```
[JaiScript Error] main.script:42:15
  Undefined variable 'heatlh' (did you mean 'health'?)
```

### Serialization Errors
JaiScript throws `serialization_error` with context:
```cpp
try {
    entity.property_mgr.load(reader);
} catch (const jai::serialization_error& e) {
    // e.what() includes property name and class info
    log_error("Failed to load entity: {}", e.what());
}
```

---

## Signal/Receiver Pattern Reference

```
┌─────────────────┐         ┌──────────────────┐
│    Signal<T>    │◄────────│   Emitter Code   │
│  (thread-safe)  │  emit() │                  │
└────────┬────────┘         └──────────────────┘
         │
         │ connect() → shared_ptr<receiver<T>>
         │
         ▼
┌─────────────────┐         ┌──────────────────┐
│   receiver<T>   │◄────────│  receiver_owner  │
│  (weak in sig)  │  track()│  (stores shared) │
└────────┬────────┘         └────────┬─────────┘
         │                           │
         │ invoke callback           │ ~receiver_owner()
         ▼                           ▼
┌─────────────────┐         ┌──────────────────┐
│  User Callback  │         │  Auto-disconnect │
│  (lambda/fn)    │         │  (ref count → 0) │
└─────────────────┘         └──────────────────┘
```

**Thread Safety**: All signal operations (connect, disconnect, emit) are protected by `std::recursive_mutex` by default.

---

## Testing Strategy

### Unit Tests
- Each converted hook file gets corresponding JaiScript tests
- Property serialization round-trip tests
- Signal/receiver lifecycle tests

### Integration Tests
- Script execution tests (convert existing ChaiScript tests)
- Serialization A/B comparison (Cereal vs JaiScript output)
- Full game save/load cycle

### Validation Checklist
- [ ] All 39 script files convert without syntax errors
- [ ] All hook files register types successfully
- [ ] Serialization produces equivalent output to Cereal
- [ ] Hot reload works for script changes
- [ ] Signal connections don't leak memory
- [ ] Observable properties trigger UI updates correctly
