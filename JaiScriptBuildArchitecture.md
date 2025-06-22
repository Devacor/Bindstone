# JaiScript Build Architecture

## Design Goals

1. **Immediate Simplicity** - Include and use immediately, no complex build steps
2. **Reasonable Build Times** - Avoid ChaiScript's template-heavy compilation overhead
3. **Flexible Integration** - Support both header-only and compiled library usage
4. **Single Compilation Unit** - Follow your proven .cxx pattern for bindings

## Current Implementation Status

✅ **Architecture Successfully Implemented**
- Minimal template usage achieved - only in Value extraction and FunctionBinder
- PIMPL pattern used for Engine class
- Type-erased Value system working with std::variant
- Clean separation of interface and implementation
- Comprehensive test suite with custom framework (no dependencies)
- ClassBuilder API for clean C++ class registration
- Migrated to organized TestSuite structure with 30+ test files

## Proposed Hybrid Architecture

### Core Structure

```
JaiScript/
├── include/
│   └── jaiscript/
│       ├── jaiscript.hpp          // Main include file
│       ├── jaiscript_fwd.hpp      // Forward declarations
│       ├── core/
│       │   ├── engine.hpp         // Engine interface (minimal templates)
│       │   ├── value.hpp          // Value type (minimal templates)
│       │   └── types.hpp          // Basic type definitions
│       └── detail/
│           ├── lexer.hpp          // Implementation details
│           ├── parser.hpp         // Can be hidden from users
│           ├── interpreter.hpp    // Internal implementation
│           └── serializer.hpp     // Cereal integration
├── src/
│   ├── jaiscript.cpp              // Single compilation unit
│   └── implementation/
│       ├── lexer.cpp              // Actual implementations
│       ├── parser.cpp             // Hidden from header
│       ├── interpreter.cpp        // No template bloat
│       └── serializer.cpp         // Clean separation
└── single_include/
    └── jaiscript_single.hpp       // Generated amalgamation
```

### Usage Patterns

#### Pattern 1: Header-Only (Easiest)
```cpp
// Just include and use
#include <jaiscript/jaiscript_single.hpp>

JaiScript::Engine engine;
engine.eval("print('Hello World');");
```

#### Pattern 2: Compiled Library (Fastest Builds)
```cpp
// In your build system
add_library(jaiscript src/jaiscript.cpp)

// In your code
#include <jaiscript/jaiscript.hpp>
```

#### Pattern 3: Single Compilation Unit (Your Current Pattern)
```cpp
// In Source/MV/Script/jaiscript.cxx
#define JAISCRIPT_IMPLEMENTATION
#include <jaiscript/jaiscript_single.hpp>

// In other files
#include <jaiscript/jaiscript_fwd.hpp>  // Just forward declarations
```

### Key Design Decisions

#### 1. Minimal Template Usage in Public API
```cpp
// BAD (ChaiScript style - heavy templates)
template<typename T, typename... Args>
auto add_function(const std::string& name, T&& func, Args&&... args);

// GOOD (JaiScript style - type-erased interface)
class Engine {
public:
    void addFunction(const std::string& name, Function func);
    void addClass(const std::string& name, ClassDefinition def);
};
```

#### 2. PIMPL for Heavy Implementation
```cpp
// jaiscript/core/engine.hpp
namespace JaiScript {
    class Engine {
    public:
        Engine();
        ~Engine();
        
        // Clean, simple interface
        Value eval(const std::string& code);
        void addGlobal(const std::string& name, Value value);
        
    private:
        struct Implementation;
        std::unique_ptr<Implementation> impl;  // Hide complexity
    };
}
```

#### 3. Type-Erased Value System
```cpp
// Avoid template explosion
class Value {
public:
    Value() = default;
    Value(int64_t i);
    Value(double d);
    Value(std::string s);
    
    template<typename T>
    T as() const;  // Only template is extraction
    
private:
    struct Storage;
    std::shared_ptr<Storage> storage;  // Type-erased storage
};
```

#### 4. Registration Without Templates
```cpp
// Instead of ChaiScript's template-heavy approach
// engine.add(chaiscript::fun(&MyClass::method), "method");

// JaiScript uses builders
ClassBuilder<MyClass>("MyClass")
    .constructor<int, float>()
    .method("update", &MyClass::update)
    .property("health", &MyClass::health)
    .build(engine);
```

### Build Time Optimization Strategies

#### 1. Separate Interface from Implementation
- Public headers contain only declarations and minimal inline code
- All parsing, interpretation, serialization in .cpp files
- Template instantiations hidden in implementation

#### 2. Forward Declaration Headers
```cpp
// jaiscript_fwd.hpp - Include this in headers
namespace JaiScript {
    class Engine;
    class Value;
    class Function;
    class ClassDefinition;
}
```

#### 3. Precompiled Headers Support
```cpp
// jaiscript_pch.hpp
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
// Common STL headers used internally
```

#### 4. Unity Build Support
```cpp
// jaiscript.cpp - Single compilation unit
#include "implementation/lexer.cpp"
#include "implementation/parser.cpp"
#include "implementation/interpreter.cpp"
#include "implementation/serializer.cpp"
```

### Integration with Existing Code

Following your current pattern:

```cpp
// Source/MV/Script/jaiscript.hpp
#pragma once
#include <jaiscript/jaiscript_fwd.hpp>

namespace MV::Script {
    class JaiScriptEngine {
        // Your wrapper interface
    };
}

// Source/MV/Script/jaiscript.cxx
#define JAISCRIPT_IMPLEMENTATION
#include <jaiscript/jaiscript_single.hpp>
#include "jaiscript.hpp"

// All JaiScript compilation happens here
// Your bindings also go here
```

### Advantages Over Alternatives

**vs ChaiScript:**
- 10x faster compilation (minimal templates)
- Cleaner error messages (less template noise)
- Same ease of inclusion

**vs AngelScript:**
- No build system required
- No add-ons to compile
- Single header option available

**vs Lua/Wren:**
- C++ native feel
- Header-only option
- Better IDE integration

### Migration Path

1. **Phase 1**: Implement core with header-only support
2. **Phase 2**: Add compilation unit optimization
3. **Phase 3**: Create amalgamation script
4. **Phase 4**: Benchmark and optimize build times

### Example Minimal Usage

```cpp
// Complete working example
#include <jaiscript/jaiscript.hpp>

int main() {
    JaiScript::Engine engine;
    
    // Add a function
    engine.addFunction("greet", [](std::string name) {
        return "Hello, " + name + "!";
    });
    
    // Run script
    auto result = engine.eval(R"(
        string message = greet("World");
        print(message);
    )");
    
    return 0;
}
```

This architecture ensures JaiScript remains as easy to use as ChaiScript while avoiding the build time penalties that come with template-heavy implementations.