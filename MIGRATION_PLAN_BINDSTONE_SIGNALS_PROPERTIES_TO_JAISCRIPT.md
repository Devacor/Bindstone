# Migration Plan: Bindstone Signals & Properties → JaiScript

> **DONE as of 2026-07 — see the Bindstone integration audit.** Signals (`jai::signal`), properties
> (`jai::property_owner` across the scene graph), and serialization shipped as JaiScript modules and
> Bindstone runs on them. Historical plan below; details may be stale.

## Overview

Migrate Bindstone's property and signal systems into JaiScript as generic, reusable modules. This completes JaiScript's vision as a composable toolkit for C++ reflection, serialization, and scripting.

**Timeline**: 5-9 weeks (1.5-2 months)

## Architecture Vision

JaiScript becomes a **composable toolkit** where each module is independent but integrates naturally:

```
JaiScript Toolkit (à la carte)
├── Core (required)
│   ├── engine, types, values, binding
│   └── No dependencies
├── Serialization (optional)
│   ├── archives, json, binary
│   └── Depends: core only
├── Properties (optional)
│   ├── property<T>, property_manager, property_owner
│   └── Depends: core + serialization
├── Signals (optional)
│   ├── signal<Signature>, receiver<Signature>
│   └── Depends: core only
└── Standard Library (optional)
    ├── json functions, math, string utils
    └── Depends: core + serialization
```

### Key Principles

- ✅ Each module is opt-in via `#include`
- ✅ No circular dependencies
- ✅ No project-specific concepts
- ✅ Modules integrate naturally when both included
- ✅ Zero external dependencies
- ✅ All lowercase/snake_case naming (JaiScript convention)

## Migration Strategy

### Approach: Clean Break (No Compatibility Layer)

- **Properties/Signals**: Direct migration `MV::Property` → `jai::property` (breaking change)
- **Serialization**: Dual-path during transition
  - Keep Cereal `load()` methods to read existing scene/save files
  - Add JaiScript `save()` methods to write new format
  - Gradual transition over testing period
- **ChaiScript**: Remove entirely, no compatibility needed

### Why No Compatibility Layer?

- Cleaner codebase (no typedef aliases)
- Forces complete migration (no half-done state)
- Easier to find/replace `MV::Property` → `jai::property`
- Properties/signals are internal APIs, not public facing
- ChaiScript removal is already breaking, might as well do it all at once

---

## Phase 1: Migrate Properties to JaiScript (1-2 weeks)

### 1.1 File Structure

```
Source/JaiScript/
  include/jaiscript/
    properties/
      property.hpp              ← property<T> template
      property_manager.hpp      ← Reflection registry
      observable_property.hpp   ← Observable properties (requires signals)
      macros.hpp               ← JAI_PROPERTY, JAI_OBSERVABLE_PROPERTY
```

### 1.2 Core Components

#### `property<T>` Template
- Generic value wrapper with transparent access
- Conversion operators: `operator T&()`, `operator const T&()`
- Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Arithmetic operators: `+=`, `-=`, `*=`, `/=` (for numeric types)
- Container operators: `[]`, iterators (for containers)
- Pointer operators: `->`, `*` (for smart pointers)
- Serialization: `save(archive_writer&)`, `load(archive_reader&)`
- Cloning: Custom clone functions for deep copying
- Type introspection: `std::type_index type()`

#### `property_manager`
- Property registration and lookup
- Reflection: `get(name)`, `get<T>(name)`, `all()`, `visit(fn)`
- Serialization: `save(archive&)`, `load(archive&)`
- Version migration: Load with key order override, key renaming
- Cloning: `clone_to_target(property_manager&)`

#### `property_owner` Base Class
- Contains `property_manager property_mgr`
- Provides `reflection()` accessor
- Base class for all types that want properties

#### Macros
```cpp
JAI_PROPERTY(type, name, ...)                    // Basic property
JAI_NAMED_PROPERTY(type, prop_name, var_name)    // Different names
JAI_DELETED_PROPERTY(type, name)                 // Backward compatibility
JAI_OBSERVABLE_PROPERTY(type, name, ...)         // With change signals
```

### 1.3 Implementation Details

**No Cereal dependencies** - Properties use JaiScript archives directly:
```cpp
void property<T>::save(jai::archive_writer& ar) const {
    if (serialize_enabled()) {
        ar.write_property_name(name());
        jai::serialize_value(ar, m_value);
    }
}
```

**Transparent access via conversion operators**:
```cpp
jai::property<int> health{mgr, "health", 100};
int x = health;      // Implicit conversion
health = 50;         // Assignment
health += 10;        // Arithmetic
if (health > 75) {}  // Comparison
```

**Custom clone functions for complex types**:
```cpp
JAI_PROPERTY((std::map<int, Texture>), textures,
    [](auto& source, auto& dest) {
        // Deep copy logic
        dest->clear();
        for (auto& [k, v] : source) {
            (*dest)[k] = v.clone();
        }
    });
```

### 1.4 Deliverables

- [ ] Create `property.hpp` with full implementation
- [ ] Create `property_manager.hpp` with serialization support
- [ ] Create `deleted_property<T>` for backward compatibility
- [ ] Create `macros.hpp` with all property macros
- [ ] Add serialization integration with JaiScript archives
- [ ] Unit tests using Foundry (50+ tests)
  - [ ] Basic property access
  - [ ] Transparent conversion
  - [ ] Arithmetic/comparison operators
  - [ ] Container/pointer operations
  - [ ] Property manager reflection
  - [ ] Serialization round-trip
  - [ ] Custom clone functions

---

## Phase 2: Migrate Signals to JaiScript (1-2 weeks)

### 2.1 File Structure

```
Source/JaiScript/
  include/jaiscript/
    signals/
      signal.hpp          ← signal<Signature> template
      receiver.hpp        ← receiver<Signature> base class
      connection.hpp      ← Connection handle for disconnecting
```

### 2.2 Core Components

#### `signal<Signature>` Template
- Generic callback system with type-safe signatures
- Connect receivers: `connect(std::shared_ptr<receiver<Sig>>)`
- Connect functions: `connect(std::function<Sig>)`
- Fire signal: `fire(args...)` or `operator()(args...)`
- Disconnect: `disconnect(receiver)`, `disconnect_all()`
- Query: `empty()`, `size()`

#### `receiver<Signature>` Base Class
- Abstract base for all receivers
- Pure virtual `operator()(Args...)`
- Allows custom receiver implementations

#### `function_receiver<Signature>`
- Wraps `std::function<Signature>`
- Default receiver type for lambdas

#### `connection` Handle
- RAII handle for disconnecting
- Weak reference to receiver
- `connected()`, `disconnect()`

### 2.3 Implementation Details

**Template specialization for void vs non-void returns**:
```cpp
// Void return - fire and forget
signal<void(int)> on_damage;
on_damage.fire(10);

// Non-void return - collect results
signal<bool(Entity&)> on_validate;
auto results = on_validate.fire(entity);  // std::vector<bool>

// Predicate - stop on first true
bool valid = on_validate.fire_until(entity);
```

**Custom receivers for script integration**:
```cpp
template<typename... Args>
class script_receiver : public receiver<void(Args...)> {
    std::shared_ptr<jai::engine> engine;
    jai::script_value function;

    void operator()(Args... args) override {
        // Convert C++ args to script values and call
    }
};
```

**Connection management**:
```cpp
auto conn = signal.connect(my_function);
// ... later ...
conn.disconnect();  // Explicit disconnect

// Or auto-disconnect when connection goes out of scope
{
    auto conn = signal.connect(my_function);
} // Automatically disconnected here
```

### 2.4 Deliverables

- [ ] Create `signal.hpp` with full implementation
- [ ] Create `receiver.hpp` base class
- [ ] Create `connection.hpp` handle class
- [ ] Create `function_receiver` wrapper
- [ ] Unit tests using Foundry (30+ tests)
  - [ ] Basic signal/receiver
  - [ ] Multiple receivers
  - [ ] Void return signals
  - [ ] Non-void return signals
  - [ ] Predicate signals (fire_until)
  - [ ] Connection management
  - [ ] Disconnect handling

---

## Phase 3: Create Observable Properties (3-4 days)

### 3.1 Implementation

**File**: `Source/JaiScript/include/jaiscript/properties/observable_property.hpp`

Combines properties + signals for reactive programming:

```cpp
template<typename T>
class observable_property : public property<T> {
public:
    signal<void(const T&, const T&, bool)> on_change;

    // Emits signal on assignment
    observable_property& operator=(const T& value) {
        if (this->get() != value) {
            T old = this->get();
            this->get() = value;
            on_change.fire(value, old, m_is_loading);
        }
        return *this;
    }

    // Override load to set is_loading flag
    void load(archive_reader& ar) override {
        m_is_loading = true;
        T old = this->get();
        property<T>::load(ar);
        on_change.fire(this->get(), old, true);
        m_is_loading = false;
    }
};
```

**Change signature**: `void(const T& new_value, const T& old_value, bool is_from_load)`
- Allows distinguishing between programmatic changes and deserialization
- UI can avoid updating during load to prevent flicker

### 3.2 Deliverables

- [ ] Implement `observable_property<T>`
- [ ] Override all mutation operators to emit signals
- [ ] Add `is_loading` flag for deserialization detection
- [ ] Unit tests (15+ tests)
  - [ ] Signal emission on change
  - [ ] Old/new value correctness
  - [ ] Loading flag during deserialization
  - [ ] No signal on no-change assignment

---

## Phase 4: Update Bindstone to Use JaiScript (2-3 weeks)

### 4.1 Global Find/Replace

**Direct namespace change (no compatibility layer)**:

```bash
# Properties
MV::Property → jai::property
MV::ObservableProperty → jai::observable_property
MV::DeletedProperty → jai::deleted_property
MV::PropertyBase → jai::property_base
MV::PropertyManager → jai::property_manager
MV::PropertyOwner → jai::property_owner

# Macros
MV_PROPERTY → JAI_PROPERTY
MV_OBSERVABLE_PROPERTY → JAI_OBSERVABLE_PROPERTY
MV_NAMED_PROPERTY → JAI_NAMED_PROPERTY
MV_DELETED_PROPERTY → JAI_DELETED_PROPERTY
MV_REMOVE_PARENS → JAI_REMOVE_PARENS

# Signals
MV::Signal → jai::signal
MV::Receiver → jai::receiver
MV::Connection → jai::connection

# Member access
propertyManager → property_mgr
```

**Files affected**: ~100+ files
- All component headers (Node, Drawable, Sprite, etc.)
- All game entity headers (Creature, Building, BattleEffect)
- All classes using PropertyOwner
- All classes using signals

### 4.2 Update Include Paths

```cpp
// Old
#include <MV/Utility/properties.hpp>
#include <MV/Utility/signal.hpp>

// New
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/observable_property.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/signals/signal.hpp>
```

### 4.3 Serialization Strategy (Dual-Path)

**Keep Cereal loading for existing files**:
```cpp
class component : public jai::property_owner {
    // NEW: JaiScript serialization (for saving)
    void save(jai::json_archive_writer& ar) const {
        property_mgr.save(ar);
    }

    // OLD: Keep Cereal loading (for existing files)
    template <class Archive>
    void load(Archive& ar, std::uint32_t const version) {
        if (version == 0) {
            reflection().load(ar, {"componentId", "componentOwner"});
        } else {
            reflection().load(ar);
        }
    }
};
```

**Eventually remove Cereal** once all content is re-saved:
- Save all scenes with JaiScript format
- Save all prefabs with JaiScript format
- Remove Cereal `load()` methods
- Remove Cereal dependency from CMakeLists.txt

### 4.4 Remove ChaiScript Integration

**Files to delete**:
- `Source/MV/Script/script.cxx` (ChaiScript implementation)
- `Source/MV/Script/*Hooks.cxx` (10 hook files with ~1,085 bindings)
- Remove ChaiScript from CMakeLists.txt

**Files to update**:
- `Source/MV/Utility/signal.hpp`: Remove ChaiScript receiver factory
- `Source/Game/standardScriptMethods.h`: Remove ChaiScript loading
- All entity classes: Remove ChaiScript script registration

### 4.5 Deliverables

- [ ] Global find/replace for namespaces
- [ ] Update all include paths
- [ ] Add JaiScript `save()` methods to all components
- [ ] Keep Cereal `load()` methods temporarily
- [ ] Remove all ChaiScript code
- [ ] Compile Bindstone successfully
- [ ] Run all existing tests (ensure nothing broke)

---

## Phase 5: Bindstone JaiScript Integration (2-3 weeks)

Now implement the JaiScript scripting integration (replacement for ChaiScript).

### 5.1 Property Auto-Binding

**File**: `Source/MV/JaiScriptIntegration/PropertyBinding.hpp`

```cpp
namespace MV::JaiScriptIntegration {

// Auto-bind all properties from property_mgr to JaiScript
template<typename ClassType>
void auto_bind_properties(
    jai::class_builder<ClassType>& builder,
    const ClassType& exemplar
) {
    for (const auto& [prop_name, prop_base] : exemplar.property_mgr.all()) {
        bind_property_by_type<ClassType>(builder, prop_name, prop_base);
    }
}

// Type dispatch - try to bind as known types
template<typename ClassType>
void bind_property_by_type(...) {
    if (try_bind<ClassType, int>(builder, name, prop)) return;
    if (try_bind<ClassType, float>(builder, name, prop)) return;
    if (try_bind<ClassType, std::string>(builder, name, prop)) return;
    // ... all common types
}

template<typename ClassType, typename PropType>
bool try_bind(jai::class_builder<ClassType>& builder, ...) {
    if (auto* typed = dynamic_cast<jai::property<PropType>*>(prop)) {
        builder.property(name,
            [name](const ClassType& self) -> const PropType& {
                return self.property_mgr.get<PropType>(name)->get();
            },
            [name](ClassType& self, const PropType& value) {
                self.property_mgr.get<PropType>(name)->get() = value;
            }
        );
        return true;
    }
    return false;
}

}
```

### 5.2 Signal Script Integration

**File**: `Source/MV/JaiScriptIntegration/SignalBinding.hpp`

```cpp
namespace MV::JaiScriptIntegration {

// Script function as signal receiver
template<typename... Args>
class script_function_receiver : public jai::receiver<void(Args...)> {
    std::shared_ptr<jai::engine> m_engine;
    jai::script_value m_function;

public:
    void operator()(Args... args) override {
        std::vector<jai::script_value> script_args;
        (script_args.push_back(jai::to_script_value(m_engine.get(), args)), ...);
        m_engine->call_function(m_function, script_args);
    }
};

// Factory
template<typename... Args>
std::shared_ptr<jai::receiver<void(Args...)>>
make_script_receiver(std::shared_ptr<jai::engine> eng, jai::script_value fn) {
    return std::make_shared<script_function_receiver<Args...>>(
        std::move(eng), std::move(fn)
    );
}

}
```

### 5.3 Type Bindings

Create new JaiScript bindings for all Bindstone types (replacing ChaiScript bindings):

**Files**: `Source/MV/JaiScriptIntegration/TypeBindings/*.hpp` (7 files)

1. `UtilityTypes.hpp` (85 bindings) - Task, Signal types
2. `RenderTypes.hpp` (268 bindings) - Point, Color, BoxAABB, Texture
3. `SceneTypes.hpp` (377 bindings) - Component, Drawable, Sprite, etc.
4. `NetworkTypes.hpp` (58 bindings) - NetworkObject, DynamicVariable
5. `AITypes.hpp` (21 bindings) - Pathfinding
6. `GameTypes.hpp` (207 bindings) - Creature, Building, BattleEffect
7. `InterfaceTypes.hpp` (21 bindings) - UI types

**Pattern**:
```cpp
void register_scene_types(std::shared_ptr<jai::engine> engine, const MV::Services& svcs) {
    auto builder = jai::class_builder<Scene::Component>(engine.get(), "Component");

    // Auto-bind all properties
    auto temp = std::make_shared<Scene::Component>(nullptr);
    auto_bind_properties(builder, *temp);

    // Manually bind methods
    builder.method("id",
        static_cast<std::string(Component::*)() const>(&Component::id));
    builder.method("bounds", &Component::bounds);
    builder.method("initialize", &Component::initialize);
}
```

### 5.4 Engine Wrapper

**File**: `Source/MV/JaiScriptIntegration/JaiScriptEngine.hpp`

```cpp
namespace MV {

class JaiScriptEngine {
    std::shared_ptr<jai::engine> m_engine;
    Services& m_services;

public:
    JaiScriptEngine(Services& svcs) : m_services(svcs) {
        m_engine = std::make_shared<jai::engine>();

        // Register all Bindstone types
        JaiScriptIntegration::register_utility_types(m_engine, svcs);
        JaiScriptIntegration::register_render_types(m_engine, svcs);
        JaiScriptIntegration::register_scene_types(m_engine, svcs);
        JaiScriptIntegration::register_network_types(m_engine, svcs);
        JaiScriptIntegration::register_ai_types(m_engine, svcs);
        JaiScriptIntegration::register_game_types(m_engine, svcs);
        JaiScriptIntegration::register_interface_types(m_engine, svcs);
    }

    template<typename T>
    std::optional<T> eval(const std::string& script_id,
                          const std::string& script_contents,
                          const std::map<std::string, jai::script_value>& locals = {}) {
        for (const auto& [name, value] : locals) {
            m_engine->set_global(name, value);
        }

        try {
            auto result = m_engine->evaluate(script_contents, script_id);
            if constexpr (std::is_same_v<T, void>) {
                return std::nullopt;
            } else {
                return jai::from_script_value<T>(result);
            }
        } catch (const jai::runtime_error& e) {
            std::cerr << "Script error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    jai::engine* get() { return m_engine.get(); }
    std::shared_ptr<jai::engine> get_shared() { return m_engine; }
};

}
```

### 5.5 Script File Migration

**Current**: 39 `.script` files using ChaiScript syntax
**Target**: Convert to JaiScript syntax (if different)

Check syntax compatibility:
- Function definition: `fun(args)` vs `function(args)`
- Property access: Same
- Method calls: Same
- Callbacks: `self.spawn = function(self) { }`

If syntax differs significantly, create conversion tool.

### 5.6 Deliverables

- [ ] Create PropertyBinding.hpp with auto-bind
- [ ] Create SignalBinding.hpp for script callbacks
- [ ] Create 7 TypeBinding files (~1,085 bindings)
- [ ] Create JaiScriptEngine wrapper
- [ ] Test with creature scripts (spawn/update/death callbacks)
- [ ] Test with UI scripts (button callbacks)
- [ ] Test signal integration (onDeath, onHealthChange, etc.)
- [ ] Convert/verify 39 .script files

---

## Phase 6: Testing & Validation (2-3 weeks)

### 6.1 JaiScript Unit Tests (Foundry)

**Properties**:
- [ ] Basic property access (50+ tests)
- [ ] Transparent conversion operators
- [ ] Arithmetic/comparison operators
- [ ] Container operations ([], iterators)
- [ ] Pointer operations (->, *)
- [ ] Property manager reflection
- [ ] Serialization round-trip (JSON and binary)
- [ ] Custom clone functions
- [ ] Observable property change signals

**Signals**:
- [ ] Basic signal/receiver (30+ tests)
- [ ] Multiple receivers
- [ ] Void return signals
- [ ] Non-void return signals (result collection)
- [ ] Predicate signals (fire_until)
- [ ] Connection management
- [ ] Disconnect handling
- [ ] Script function receivers

### 6.2 Bindstone Integration Tests

**Serialization**:
- [ ] Save scene with JaiScript archives
- [ ] Load scene with Cereal (existing files)
- [ ] Round-trip: Cereal load → JaiScript save → JaiScript load
- [ ] Verify all component types serialize correctly
- [ ] Test polymorphic deserialization

**Scripting**:
- [ ] Creature spawn/update/death callbacks work
- [ ] UI button callbacks work
- [ ] Signal callbacks from scripts work
- [ ] Property access from scripts works
- [ ] Network synchronization with scripts

**Property System**:
- [ ] All ~2000+ properties accessible
- [ ] Observable properties emit signals
- [ ] Property cloning works (scene duplication)
- [ ] Reflection via property_mgr works

### 6.3 Performance Benchmarks

**Serialization**:
- [ ] Compare JaiScript JSON vs Cereal JSON (speed & size)
- [ ] Compare JaiScript binary vs Cereal binary (speed & size)
- [ ] Large scene (1000+ nodes) save/load times

**Property Access**:
- [ ] Direct C++ property access
- [ ] Property access via property_mgr (reflection)
- [ ] Property access from scripts
- [ ] Observable property overhead

**Signals**:
- [ ] Signal fire with 0/1/10/100 receivers
- [ ] Script callback overhead vs C++ callback

### 6.4 Deliverables

- [ ] 100+ unit tests for properties (Foundry)
- [ ] 50+ unit tests for signals (Foundry)
- [ ] Integration tests for Bindstone
- [ ] Performance benchmarks documented
- [ ] No memory leaks (Valgrind/ASAN)
- [ ] All existing Bindstone tests still pass

---

## Phase 7: Cleanup & Documentation (1 week)

### 7.1 Remove Cereal (Once Content Migrated)

After all scenes/prefabs are re-saved with JaiScript format:
- [ ] Remove all Cereal `load()` methods
- [ ] Remove Cereal dependency from CMakeLists.txt
- [ ] Update documentation

### 7.2 Documentation

**JaiScript docs**:
- [ ] Properties module documentation
- [ ] Signals module documentation
- [ ] Observable properties guide
- [ ] Serialization integration guide
- [ ] Examples for each module

**Bindstone docs**:
- [ ] Migration guide for users
- [ ] New property/signal API documentation
- [ ] JaiScript scripting guide
- [ ] Breaking changes list

### 7.3 Deliverables

- [ ] Remove Cereal dependency
- [ ] Update JaiScript README
- [ ] Create module documentation
- [ ] Create migration guide
- [ ] Update example projects

---

## Success Criteria

### Phase Completion

**Phase 1 (Properties)**: ✅ Complete when:
- [ ] All property types implemented and tested
- [ ] Serialization works with JaiScript archives
- [ ] 50+ unit tests pass
- [ ] Zero dependencies on Bindstone

**Phase 2 (Signals)**: ✅ Complete when:
- [ ] All signal functionality implemented
- [ ] 30+ unit tests pass
- [ ] Script receiver support works
- [ ] Zero dependencies on Bindstone

**Phase 3 (Observable)**: ✅ Complete when:
- [ ] Observable properties emit change signals
- [ ] Load detection works
- [ ] 15+ unit tests pass

**Phase 4 (Bindstone Update)**: ✅ Complete when:
- [ ] All Bindstone code uses jai:: namespace
- [ ] Compiles without errors
- [ ] All existing tests pass
- [ ] ChaiScript code removed

**Phase 5 (Integration)**: ✅ Complete when:
- [ ] All 1,085 bindings migrated to JaiScript
- [ ] Property auto-binding works
- [ ] Signal script callbacks work
- [ ] Creature/UI scripts execute correctly

**Phase 6 (Testing)**: ✅ Complete when:
- [ ] 150+ unit tests pass
- [ ] All integration tests pass
- [ ] Performance benchmarks documented
- [ ] No regressions detected

**Phase 7 (Cleanup)**: ✅ Complete when:
- [ ] Cereal removed (optional, can keep longer)
- [ ] Documentation complete
- [ ] Migration guide published

---

## Risk Assessment

### High-Risk Areas

**Risk 1: Breaking All Bindstone Code**
- **Impact**: ~100+ files need namespace changes
- **Mitigation**: Automated find/replace, comprehensive testing
- **Contingency**: Git branch for rollback

**Risk 2: Serialization Format Incompatibility**
- **Impact**: Existing scenes/saves can't be loaded
- **Mitigation**: Keep Cereal `load()` during transition
- **Contingency**: Conversion tool to re-save all content

**Risk 3: Script Syntax Differences**
- **Impact**: 39 .script files may not work
- **Mitigation**: Verify syntax compatibility early
- **Contingency**: Create automated conversion tool

### Medium-Risk Areas

**Risk 4: Performance Regression**
- **Impact**: Property/signal overhead affects runtime
- **Mitigation**: Benchmark early, optimize hot paths
- **Contingency**: Add fast-path for direct access

**Risk 5: Missing Property Types**
- **Impact**: Some complex properties can't auto-bind
- **Mitigation**: Support all common types, allow manual binding
- **Contingency**: Document unsupported patterns

---

## Timeline Summary

| Phase | Duration | Key Deliverables |
|-------|----------|------------------|
| 1. Properties → JaiScript | 1-2 weeks | property<T>, property_manager, tests |
| 2. Signals → JaiScript | 1-2 weeks | signal<T>, receiver<T>, tests |
| 3. Observable Properties | 3-4 days | observable_property<T>, tests |
| 4. Bindstone Update | 2-3 weeks | Namespace migration, ChaiScript removal |
| 5. Integration Layer | 2-3 weeks | Type bindings, auto-bind, script engine |
| 6. Testing & Validation | 2-3 weeks | Unit tests, integration tests, benchmarks |
| 7. Cleanup & Docs | 1 week | Remove Cereal (optional), documentation |
| **TOTAL** | **5-9 weeks** | **Complete JaiScript toolkit** |

---

## Next Steps

1. **Immediate**: Implement Phase 1 (Properties)
   - Create file structure
   - Implement property<T>, property_manager
   - Add JaiScript serialization
   - Write Foundry tests

2. **After Phase 1**: Implement Phase 2 (Signals)
   - Create signal<T>, receiver<T>
   - Add connection management
   - Write Foundry tests

3. **After Phase 2**: Combine into Observable Properties (Phase 3)

4. **Then**: Begin Bindstone migration (Phase 4)

---

## Benefits of This Approach

### For JaiScript
- ✅ Generic, reusable modules (properties, signals)
- ✅ Completes the "reflection + serialization + scripting" toolkit
- ✅ Zero dependencies on Bindstone
- ✅ Useful for any C++ project

### For Bindstone
- ✅ Single reflection/serialization/scripting system (not three)
- ✅ Properties automatically accessible from scripts
- ✅ Cleaner codebase (remove ChaiScript, eventually Cereal)
- ✅ Full control over the stack

### For Development
- ✅ Clean architecture (no circular dependencies)
- ✅ Easier to test (modules are independent)
- ✅ Faster iteration (JaiScript changes don't require Bindstone rebuild)
- ✅ Better separation of concerns

---

**Document Version**: 1.0
**Created**: 2025-01-04
**Status**: Ready to begin Phase 1