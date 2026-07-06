# ChaiScript to JaiScript Migration Plan

> **DONE as of 2026-07 — see the Bindstone integration audit.** All phases shipped: hooks, scripts,
> and serialization are on JaiScript; ChaiScript/Cereal are gone from live code. Historical plan
> below; details may be stale.

## Overview

Replace ChaiScript with JaiScript throughout Bindstone, leveraging the property system for automatic bindings where possible.

## Phase 1: Infrastructure (COMPLETED ✅)

- [x] `dynamic_binder` with fluent API
- [x] `auto_bind()` with CRTP inheritance detection
- [x] `property_owner<Derived, Bases...>` CRTP pattern
- [x] Operator== auto-detection
- [x] `registrar` and `bind_registrar` for file-scope registration
- [x] Diamond inheritance detection

## Phase 2: Convert Hook Files

Convert ChaiScript hook files to JaiScript using `dynamic_binder` and `registrar`.

### Hook Files (10 files, dependency order)

| Priority | File | Lines | Dependencies |
|----------|------|-------|--------------|
| 1 | `utilityHooks.cxx` | ~50 | None |
| 2 | `renderHooks.cxx` | ~200 | Point, Size, Color, BoxAABB |
| 3 | `networkHooks.cxx` | ~50 | Network primitives |
| 4 | `sceneHooks.cxx` | ~400 | Node, Component, Drawable (depends on render) |
| 5 | `artificialIntelligenceHooks.cxx` | ~50 | PathAgent, Grid |
| 6 | `standardHooks.cxx` | ~50 | Type conversions, print() |
| 7 | `interfaceHooks.cxx` | ~100 | UI bindings |
| 8 | `gameNetworkHooks.cxx` | ~100 | Game network bindings |
| 9 | `gameHooks.cxx` | ~550 | Creature, Building, BattleEffect, Game |
| 10 | `engineHooks.cxx` | ~6 | Includes all MV hooks |

### Conversion Pattern

**Before (ChaiScript):**
```cpp
MV::Script::Registrar<BattleEffect> _hookBattleEffect([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
    a_script.add(chaiscript::user_type<BattleEffect>(), "BattleEffect");
    a_script.add(chaiscript::base_class<MV::Scene::Component, BattleEffect>());
    a_script.add(chaiscript::fun(&BattleEffect::alive), "alive");
    a_script.add(chaiscript::fun([](BattleEffect& self) -> GameInstance& { return self.game(); }), "game");
});
```

**After (JaiScript):**
```cpp
jai::registrar<BattleEffect, MV::Services> _hookBattleEffect([](jai::engine& eng, const MV::Services& services) {
    jai::dynamic_binder<BattleEffect>(eng, "BattleEffect")
        .base_class<MV::Scene::Component>()
        .method("alive", &BattleEffect::alive)
        .method("game", [](BattleEffect& self) -> GameInstance& { return self.game(); });
});
```

### ChaiScript → JaiScript API Mapping

| ChaiScript | JaiScript |
|------------|-----------|
| `chaiscript::user_type<T>()` | `dynamic_binder<T>(eng, "Name")` |
| `chaiscript::constructor<T()>()` | `.constructor<>()` |
| `chaiscript::fun(&T::method)` | `.method("name", &T::method)` |
| `chaiscript::fun([](T& self){...})` | `.method("name", [](T& self){...})` |
| `chaiscript::base_class<Base, Derived>()` | `.base_class<Base>()` |
| `a_script.add_global(var, "name")` | `eng.add_global("name", value)` |
| `chaiscript::type_conversion<A,B>()` | `eng.add_conversion<A,B>([](A& a){ return B(a); })` |
| `MV::Script::Registrar<T>` | `jai::registrar<T, Context>` |

## Phase 3: Convert Script Files

Convert 39 .script files from ChaiScript to JaiScript syntax.

### Script File Categories

| Category | Files | Complexity |
|----------|-------|------------|
| Interface | 8 | Low |
| Buildings | 6 | Medium |
| BattleEffects | 2 | Medium |
| Creatures | 24 | High |
| Prefabs | 1 | Low |

### Syntax Conversion

| ChaiScript | JaiScript |
|------------|-----------|
| `def func(args)` | `function func(args) -> auto` or `auto func(args) -> auto` |
| `var x = val` | `auto x = val` |
| `var x := val` | `auto x = val` |
| `fun(args){ body }` | `[](args){ body }` |
| `global x = val` | `var x = val` at module scope |
| `.is_var_null()` | `== null` |
| `.is_var_undef()` | `== null` (not supported - use null) |
| `self["key"]` | `self["key"]` (same) |

## Phase 4: Serialization Strategy

### Option A: Keep Cereal (Recommended for initial migration)
- Keep existing Cereal serialization for game data
- JaiScript property system works alongside Cereal
- Lower risk, can validate both systems produce same results

### Option B: Full JaiScript Archives (Future)
- Convert to JaiScript JSON/Binary archives
- Requires save file migration or version bump
- Cleaner long-term but higher initial risk

### Recommended Approach
1. Phase 2-3: Convert bindings and scripts, keep Cereal serialization
2. Validate: Run both engines in parallel, compare behavior
3. Phase 5 (future): Evaluate JaiScript serialization migration

## Phase 5: Integration & Testing

1. **Parallel Engine Mode**: Run both ChaiScript and JaiScript during transition
2. **Behavior Comparison**: Verify identical script execution results
3. **Performance Validation**: Confirm JaiScript performance gains
4. **Full Cutover**: Remove ChaiScript dependency

## Files to Create

| File | Purpose |
|------|---------|
| `Source/MV/Utility/jaiUtilityHooks.cxx` | JaiScript utility bindings |
| `Source/MV/Render/jaiRenderHooks.cxx` | JaiScript render bindings |
| `Source/MV/Render/Scene/jaiSceneHooks.cxx` | JaiScript scene bindings |
| `Source/MV/Network/jaiNetworkHooks.cxx` | JaiScript network bindings |
| `Source/MV/ArtificialIntelligence/jaiAIHooks.cxx` | JaiScript AI bindings |
| `Source/Game/jaiStandardHooks.cxx` | JaiScript standard bindings |
| `Source/Game/Interface/jaiInterfaceHooks.cxx` | JaiScript interface bindings |
| `Source/Game/NetworkLayer/jaiGameNetworkHooks.cxx` | JaiScript game network bindings |
| `Source/Game/jaiGameHooks.cxx` | JaiScript game bindings |

## Files to Modify

| File | Change |
|------|--------|
| `Source/Game/game.cpp` | Add JaiScript engine initialization |
| `Source/SolutionSpecific/*/main.cpp` | Bootstrap JaiScript engine |

## Files to Remove (after migration complete)

- All `*Hooks.cxx` ChaiScript binding files
- `External/ChaiScript-6.1.0/` directory

## ObservableProperty TODO

The JaiScript property system is missing `ObservableProperty<T>` with signal support. This should be added before migrating classes that use observable properties:

```cpp
// Future addition to JaiScript
template<typename T>
class observable_property : public property<T> {
    signal<void(const T&, const T&)> on_change;  // old_value, new_value
};
```

## Recommended Starting Point

Start with `utilityHooks.cxx` as it has no dependencies and is the simplest. This will validate the conversion pattern before tackling more complex files.
