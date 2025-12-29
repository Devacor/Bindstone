# ChaiScript to JaiScript Migration Plan

## Overview

Replace ChaiScript with JaiScript throughout Bindstone, leveraging the property system for automatic bindings where possible.

## Current State

### ChaiScript Hook Files (10 files, ~1200 lines total)
| File | Purpose | Lines |
|------|---------|-------|
| `Source/Game/gameHooks.cxx` | Main entry, includes others. BattleEffect, Creature, Building, Team, Game, GameData, Player | ~550 |
| `Source/Game/standardHooks.cxx` | Type conversions, print(), StandardMessages | ~50 |
| `Source/Game/NetworkLayer/gameNetworkHooks.cxx` | Network-related bindings | TBD |
| `Source/Game/Interface/interfaceHooks.cxx` | UI/Interface bindings | TBD |
| `Source/MV/Render/renderHooks.cxx` | BoxAABB, Color, TexturePoint, Size, Point | ~200 |
| `Source/MV/Render/Scene/sceneHooks.cxx` | Node, Component, Drawable, Text, etc. | ~400 |
| `Source/MV/Utility/utilityHooks.cxx` | General utilities | TBD |
| `Source/MV/Network/networkHooks.cxx` | Network primitives | TBD |
| `Source/MV/ArtificialIntelligence/artificialIntelligenceHooks.cxx` | PathAgent, Grid | TBD |
| `Source/MV/Script/engineHooks.cxx` | Includes other MV hooks | ~6 |

### Script Files (39 files)
- **Creatures**: 24 files (main.script + mainClient.script for 12 creatures)
- **Buildings**: 6 files (3 buildings)
- **BattleEffects**: 2 files
- **Interface**: 8 files (Login, Main, interfaceManager)
- **Prefabs**: 1 file

### Property Systems
1. **MV::PropertyOwner** (`Source/MV/Utility/properties.hpp`)
   - Used by: Scene components (Button, Clickable, Drawable, Text, Spine, etc.)
   - Provides: `PropertyManager`, `MV_PROPERTY()` macro, Cereal serialization

2. **jai::property_owner** (`Source/JaiScript/include/jaiscript/properties/`)
   - Designed to mirror MV::PropertyOwner API
   - Provides: `property_manager`, `JAI_PROPERTY()` macro, JaiScript serialization

3. **Game classes** (Creature, Building, BattleEffect)
   - Currently use Cereal directly, NOT PropertyOwner
   - Have manual `serialize()` methods

---

## Migration Strategy

### Phase 1: Create Auto-Binding Infrastructure

**Goal**: Create utilities that generate JaiScript bindings from MV::PropertyOwner and support method registration.

#### 1A. Property Auto-Binding

```cpp
// New file: Source/MV/Script/jaiscript_bridge.hpp

namespace MV::Script {

// Auto-register all properties from a PropertyOwner to JaiScript
template<typename T>
jai::class_builder<T>& bind_properties(jai::class_builder<T>& builder, T& instance) {
    instance.reflection().visit([&](const std::string& name, PropertyBase* prop) {
        // Properties are registered as getters/setters
        // Type dispatch based on property's underlying type
        if (auto* p = dynamic_cast<Property<int>*>(prop)) {
            builder.property(name, [](T& self) { return self.reflection().get<int>(name)->get(); },
                                   [](T& self, int v) { *self.reflection().get<int>(name) = v; });
        }
        // ... similar for float, string, etc.
    });
    return builder;
}

// Lightweight method binding helper - chainable with class_builder
template<typename T>
class method_binder {
    jai::class_builder<T>& builder_;
public:
    method_binder(jai::class_builder<T>& b) : builder_(b) {}

    template<typename Fn>
    method_binder& method(const std::string& name, Fn&& fn) {
        builder_.method(name, std::forward<Fn>(fn));
        return *this;
    }

    jai::class_builder<T>& done() { return builder_; }
};

} // namespace MV::Script
```

#### 1B. Registrar Pattern for JaiScript

```cpp
// Parallel to existing MV::Script::Registrar<T> for ChaiScript
namespace MV::Script {

template<typename T>
class JaiRegistrar {
public:
    JaiRegistrar(std::function<void(jai::engine&, const MV::Services&)> fn);

    // Called during engine initialization
    static void register_all(jai::engine& eng, const MV::Services& services);
};

} // namespace MV::Script
```

#### 1C. Usage Pattern for Game Classes

```cpp
// Example: Converting Creature hooks
MV::Script::JaiRegistrar<Creature> _jaiCreature([](jai::engine& eng, const MV::Services& services) {
    jai::class_builder<Creature> builder(eng, "Creature");

    builder
        .base_class<MV::Scene::Component>()
        .constructor<>()
        // Auto-bind all PropertyOwner properties (if Creature inherits PropertyOwner)
        // Otherwise, manual property bindings:
        .property("stats", &Creature::statTemplate)
        .property("skin", &Creature::skin)
        // Methods
        .method("game", [](Creature& self) -> GameInstance& { return self.gameInstance; })
        .method("alive", &Creature::alive)
        .method("changeHealth", [](Creature& self, int amount) { return self.changeHealth(amount); })
        .method("team", [](Creature& self) { return self.gameInstance.teamForPlayer(self.player()); })
        .build();

    // Vector type support
    eng.register_vector<std::shared_ptr<Creature>>("VectorCreature");
});
```

### Phase 2: Convert Hook Files to JaiScript

Replace ChaiScript `a_script.add(chaiscript::fun(...))` patterns with JaiScript `class_builder<T>` patterns.

**Pattern Mapping:**
| ChaiScript | JaiScript |
|------------|-----------|
| `chaiscript::user_type<T>()` | `class_builder<T>(eng, "Name")` |
| `chaiscript::constructor<T()>()` | `.constructor<>()` |
| `chaiscript::fun(&T::method)` | `.method("name", &T::method)` |
| `chaiscript::fun([](T& self){ ... })` | `.method("name", [](T& self){ ... })` |
| `chaiscript::base_class<Base, Derived>()` | `.base_class<Base>()` |
| `a_script.add_global(var, "name")` | `eng.add_global("name", value)` |
| `chaiscript::type_conversion<A,B>()` | `eng.add_conversion<A,B>([](A& a){ return B(a); })` |

**Order of conversion** (dependency order):
1. utilityHooks.cxx (no dependencies)
2. renderHooks.cxx (Point, Size, Color, BoxAABB)
3. networkHooks.cxx
4. sceneHooks.cxx (depends on render)
5. artificialIntelligenceHooks.cxx
6. standardHooks.cxx
7. interfaceHooks.cxx
8. gameNetworkHooks.cxx
9. gameHooks.cxx (depends on all above)

### Phase 3: Convert Script Files

**Syntax Conversion:**
| ChaiScript | JaiScript |
|------------|-----------|
| `def func(args)` | `function func(args) -> auto` |
| `var x = val` | `auto x = val` |
| `var x := val` | `auto x = val` (same behavior) |
| `fun(args){ body }` | `[](args){ body }` or keep as `function` |
| `global x = val` | `var x = val` at module scope |
| `self["key"]` | `self["key"]` (same) |
| `.is_var_null()` | `== null` |
| `.is_var_undef()` | Not supported (use null) |

**Script file conversion order:**
1. Start with simplest: `initialize.script`, `deselectText.script`
2. Then: Building scripts (simpler logic)
3. Then: Creature scripts (more complex)
4. Finally: Interface manager and login

### Phase 4: Integration & Testing

1. Create parallel engine initialization (run both during transition)
2. Test each converted hook file in isolation
3. Test each converted script file
4. Remove ChaiScript dependency

---

## Decisions Made

1. **Auto-Binding Approach**: Bridge MV::PropertyOwner → JaiScript
   - Auto-bind properties from PropertyManager
   - Also need lightweight function binding support for methods

2. **Migration Style**: Big-bang
   - Convert everything, then switch over
   - Keep existing Cereal serialization during transition for safety/validation

3. **Serialization**: Keep Cereal alongside JaiScript serialization
   - Allows A/B comparison during migration
   - Can validate JaiScript serialization against known-good Cereal output

---

## Implementation Order

### Step 1: JaiScript Bridge Infrastructure
1. Create `Source/MV/Script/jaiscript_bridge.hpp`
   - `JaiRegistrar<T>` template (parallel to ChaiScript `Registrar<T>`)
   - Property auto-binding helpers
   - Method binding helpers

2. Create `Source/MV/Script/jaiscript_init.cpp`
   - Engine initialization
   - Call all JaiRegistrar registrations

### Step 2: Convert Hook Files (in dependency order)
1. **utilityHooks.cxx** → `jaiUtilityHooks.cxx` (no deps)
2. **renderHooks.cxx** → `jaiRenderHooks.cxx` (Point, Size, Color, BoxAABB)
3. **networkHooks.cxx** → `jaiNetworkHooks.cxx`
4. **sceneHooks.cxx** → `jaiSceneHooks.cxx` (depends on render)
5. **artificialIntelligenceHooks.cxx** → `jaiAIHooks.cxx`
6. **standardHooks.cxx** → `jaiStandardHooks.cxx` (type conversions, print)
7. **interfaceHooks.cxx** → `jaiInterfaceHooks.cxx`
8. **gameNetworkHooks.cxx** → `jaiGameNetworkHooks.cxx`
9. **gameHooks.cxx** → `jaiGameHooks.cxx` (Creature, Building, BattleEffect, etc.)

### Step 3: Convert Script Files
1. Create syntax conversion tool/script (ChaiScript → JaiScript)
2. Convert in batches:
   - Interface scripts (simplest, 8 files)
   - Building scripts (6 files)
   - BattleEffect scripts (2 files)
   - Creature scripts (24 files, most complex)
   - Prefabs (1 file)

### Step 4: Integration & Validation
1. Wire up JaiScript engine in `Game` class
2. Run both engines in parallel (optional validation mode)
3. Compare serialization output Cereal vs JaiScript
4. Full test pass
5. Remove ChaiScript code

---

## Files Summary

### New Files to Create
| File | Purpose |
|------|---------|
| `Source/MV/Script/jaiscript_bridge.hpp` | Bridge utilities, JaiRegistrar |
| `Source/MV/Script/jaiscript_init.cpp` | Engine init, registration |
| `Source/MV/Script/jaiUtilityHooks.cxx` | Utility bindings |
| `Source/MV/Render/jaiRenderHooks.cxx` | Render type bindings |
| `Source/MV/Render/Scene/jaiSceneHooks.cxx` | Scene component bindings |
| (etc. for each hook file) | |

### Files to Modify
| File | Change |
|------|--------|
| `Source/Game/game.cpp` | Add JaiScript engine init |
| `Source/SolutionSpecific/*/main.cpp` | Bootstrap changes |
| All 39 `.script` files | Syntax conversion |

### Files to Remove (after migration complete)
- All `*Hooks.cxx` ChaiScript binding files
- `External/ChaiScript-6.1.0/` directory
