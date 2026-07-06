# JaiScript Future Design

> **VISION DOCUMENT (2025) — see ROADMAP.md for the live roadmap.** Much of this shipped, often
> under different names: unified serialization (pointer dedup, binary format, versioning) is
> live; the property system shipped as `JAI_PROPERTY` (`properties/`), not `MV_PROPERTY`; memory
> optimizations shipped as the 32-byte thin-value fold + env pooling; the JIT path was obviated
> by the full-parity bytecode VM (+ epoch-keyed lookup caches); async/await was obviated by
> coroutines (functions AND methods). Never happened: header-only / single-header distribution —
> JaiScript is a compiled implementation. Genuinely open items (`??`, generics, pattern matching,
> `extend`, stdlib I/O, LSP/debugger) are tracked in ROADMAP.md. Shipped/obviated items are
> marked ✅/↪ in place below.

## Vision

JaiScript aims to become the definitive scripting solution for high-performance C++ games by unifying scripting, serialization, and hot-reload into a single, elegant system.

## Core Philosophy

- **Zero-friction adoption**: No external dependencies (compiled implementation, not header-only)
- **Performance first**: 25-578x faster than alternatives
- **C++ semantics**: Familiar syntax, zero-copy integration, snake_case naming conventions
- **Unified system**: One solution for scripting AND serialization

## Implemented Features (2025)

- **Deep Copy by Default**: Full C++ value semantics with container deep copying
- **Reference Support**: `&` reference types and weak_ptr support
- **Operator Overloading**: Fast path optimizations respect custom operators
- **Type Conversions**: Seamless int↔float conversions in constructors and methods
- **Strong Type System**: `auto` locks type at inference, `var` allows dynamic typing
- **Container Homogeneity**: `auto` containers enforce homogeneous types, `var` allows heterogeneous
- **Recursive Validation**: Nested containers validated to arbitrary depth
- **Script-Defined Classes**: Full support with constructors, methods, inheritance
- **Hot Reload**: Class redefinition with automatic instance migration
- **Switch Statements**: Break-by-default with explicit `fallthrough` keyword
- **Range-Based For**: `for (auto x : container)` syntax
- **Exception Handling**: try/catch/throw with re-throw support
- **Bitwise Operators**: Full set including `&`, `|`, `^`, `~`, `<<`, `>>`
- **Ternary Operator**: `condition ? true_val : false_val`

## Script-Defined Classes

```javascript
class Character {
    string name = "Hero";
    float health = 100.0;

    Character(string n) {
        name = n;
    }

    void takeDamage(float amount) {
        health -= amount;
        if (health <= 0) {
            onDeath();
        }
    }
}
```

## Planned Features

### 1. Unified Serialization System ✅ SHIPPED (see SERIALIZATION.md)

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

### 2. State-Preserving Hot Reload

Building on existing class hot reload, extend to functions:
- Maintain local variables in functions
- Preserve call stack during reload
- Keep closure captures
- Update function bodies in-place

### 3. Property System Integration ✅ SHIPPED as `JAI_PROPERTY` + `property_owner` CRTP (properties/)

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

### 4. Performance Optimizations

**JIT Compilation Path** ↪ OBVIATED — shipped as the full-parity bytecode VM instead
- Optional JIT backend for hot paths
- Inline caching for property access (shipped as epoch-keyed lookup caches)
- Type specialization for numeric operations

**Memory Optimizations** ✅ SHIPPED — 32-byte `script_value` (thin_value_spec.md), env pooling
- Small object optimization for Value type
- Custom allocators for script objects
- Pool allocation for temporary values

### 5. Language Features

**Still Planned**
- Null coalescing `??`
- Template/generic functions
- ~~Async/await for coroutines~~ ↪ OBVIATED — coroutines shipped (`coroutine`/`yield`, incl. methods)
- Pattern matching

### 6. Tooling and Ecosystem

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

### 7. Distribution Model

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

## Priority Roadmap

1. **Unified Serialization** - Pointer deduplication, circular references, binary format
2. **Property System** - MV_PROPERTY integration, generic property interface
3. **Performance** - JIT compilation, memory optimizations
4. **Tooling** - LSP, syntax highlighting, debugger
5. **Ecosystem** - Standard library expansion, documentation

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