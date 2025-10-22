# JaiScript Future Design

## Vision

JaiScript aims to become the definitive scripting solution for high-performance C++ games by unifying scripting, serialization, and hot-reload into a single, elegant system.

## Core Philosophy

- **Zero-friction adoption**: Header-only, no dependencies
- **Performance first**: 25-578x faster than alternatives
- **C++ semantics**: Familiar syntax, zero-copy integration, snake_case naming conventions
- **Unified system**: One solution for scripting AND serialization

## Recent Accomplishments (2025)

- **Deep Copy by Default**: Full C++ value semantics with container deep copying
- **Reference Support**: Added `&` reference types and weak_ptr support
- **Operator Overloading**: Fixed fast path optimizations to respect custom operators
- **Type Conversions**: Seamless int↔double conversions in constructors and methods
- **Clean Codebase**: Removed all debug output for production-ready deployment

## Planned Features

### 1. Script-Defined Classes (Next Priority)
```javascript
class Character {
    script_string name = "Hero";
    script_float health = 100.0;
    Vec3 position;
    
    Character(script_string n) {
        name = n;
    }
    
    void takeDamage(script_float amount) {
        health -= amount;
        if (health <= 0) {
            onDeath();
        }
    }
}
```

### 2. Unified Serialization System

**Pointer Deduplication**
- Track shared_ptr instances across serialization
- Handle circular references automatically
- Maintain object identity through save/load cycles

**Script State Serialization**
```javascript
function quest_update() {
    auto stage = get_quest_stage();
    auto talked_to_npc = false;
    
    // Save game here preserves local variables!
    // Hot reload here maintains execution state!
    
    if (!talked_to_npc && player.near_npc()) {
        // State perfectly preserved
    }
}
```

**Binary Format**
- Compact binary serialization option
- Versioning support for schema evolution
- Zero-copy deserialization where possible

### 3. Advanced Hot Reload (✅ Core Functionality Implemented)

**Already Implemented:**
- ✅ Class redefinition with automatic instance migration
- ✅ Field preservation (same-named fields keep values)
- ✅ Method replacement (all methods updated)
- ✅ Custom migration via `hot_reload_migrate()`
- ✅ Instance tracking (including clones)
- ✅ Performance optimized (<10ms for 100 instances)

**Still Planned - State-Preserving Reload:**
- Maintain local variables in functions
- Preserve call stack during reload
- Keep closure captures
- Update function bodies in-place

**Development Workflow**
```javascript
// Already works for classes:
class Character {
    auto health = 100;
    void on_damage(amount) {
        health -= amount;
        // Hot reload here updates method instantly!
        if (health < 20) {
            start_limp_animation(); // Add this line
        }
    }
}

// Still planned for standalone functions:
character.on_damage = function(amount) {
    health -= amount;
    if (health < 20) {
        start_limp_animation(); // Add this line
    }
}
// Hot reload - character keeps current health!
```

### 4. Property System Integration

**Automatic Binding**
```cpp
// C++ with MV_PROPERTY
class Weapon {
    MV_PROPERTY((script_int), damage, 10);
    MV_PROPERTY((script_float), fire_rate, 1.0f);
};

// Automatically available in JaiScript:
// weapon.damage = 15;
// weapon.fire_rate *= 1.5;
```

**Generic Property Interface**
```cpp
class property_provider {
    virtual std::vector<property_binding> get_properties(const script_string& class_name) = 0;
};

// Allows integration with any property system
engine.add_property_provider(std::make_unique<mv_property_provider>());
```

### 5. Performance Optimizations

**JIT Compilation Path**
- Optional JIT backend for hot paths
- Inline caching for property access
- Type specialization for numeric operations

**Memory Optimizations**
- Small object optimization for Value type
- Custom allocators for script objects
- Pool allocation for temporary values

### 6. Language Features

**Missing Operators**
- Bitwise OR `|` and XOR `^`
- Ternary operator `? :`
- Null coalescing `??`

**Control Flow**
- ✅ Break and continue statements (completed)
- ✅ Switch/case statements with break-by-default safety (completed 2025-07-15)
- ✅ Range-based for loops (completed 2025-07-15)
- Exception handling (try/catch)

**Advanced Features**
- Template/generic functions
- Async/await for coroutines
- Pattern matching

### 7. Tooling and Ecosystem

**Development Tools**
- Language server protocol (LSP) implementation
- Syntax highlighting (VS Code, Sublime)
- Interactive debugger
- Performance profiler

**Standard Library**
- File I/O
- Networking basics
- Math utilities
- Container algorithms

### 8. Distribution Model

```
jaiscript.hpp              // Core engine (~100KB)
jaiscript_stdlib.hpp       // Standard library (~50KB)
jaiscript_serialization.hpp // Serialization (~50KB)
jaiscript_debug.hpp        // Debug tools (~30KB)
```

Choose only what you need!

## Use Case Scenarios

### Save System
```cpp
// One line to save entire game state
jai::serialize("savegame.jai", game_state);
// Includes: C++ objects, script state, closures, references
```

### Network Replication
```cpp
// Same serialization for networking
auto delta = jai::diff(old_state, new_state);
network.send(delta);
```

### Mod Support
```javascript
// Mods can extend C++ classes
extend Character {
    script_float stamina = 100.0;
    
    void sprint() {
        if (stamina > 0) {
            speed *= 2;
            stamina -= delta_time * 10;
        }
    }
}
```

## Timeline Estimate

**Months 1-3: Core Features**
- Script-defined classes
- Unified serialization with pointer dedup
- State-preserving hot reload

**Months 4-6: Integration**
- Property system bindings
- Binary serialization format
- Initial tooling (syntax highlighting)

**Months 7-9: Optimization**
- Performance improvements
- Memory optimizations
- Platform-specific tuning

**Months 10-12: Ecosystem**
- Documentation
- Tutorial content
- Example projects
- Community building

## Competitive Advantages

1. **vs Lua**: Native C++ semantics, zero-copy bindings, built-in serialization
2. **vs JavaScript/V8**: Lightweight, deterministic, game-focused
3. **vs AngelScript**: Much faster, cleaner API, unified serialization
4. **vs ChaiScript**: 25-578x faster, still easy to embed

## Success Metrics

- **Performance**: Maintain 10x+ advantage over alternatives
- **Adoption**: Target 1000+ GitHub stars in first year
- **Ecosystem**: 10+ community-contributed bindings
- **Production**: Ship in at least one commercial game

## Philosophy

JaiScript isn't trying to be a general-purpose scripting language. It's specifically designed for C++ game engines that need:
- Maximum performance
- Seamless C++ integration with snake_case naming conventions
- Unified serialization
- Hot reload capabilities

By focusing on this specific use case, JaiScript can provide a better solution than any general-purpose alternative.

## The End Goal

Make JaiScript the "SQLite of game scripting" - the obvious choice because it just works, requires no setup, and solves real problems better than anything else.