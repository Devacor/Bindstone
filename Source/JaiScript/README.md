# JaiScript

A modern, lightweight scripting language designed for game development with C++-like syntax, built-in hot-reloading, and state preservation.

## Features

- **C++-like syntax** - Familiar to C++ developers
- **RAII semantics** - Automatic resource management
- **Hot-reload support** - Change scripts while running
- **State preservation** - Maintain state across reloads
- **Type-safe** - Compile-time error checking
- **Easy integration** - Header-only or compiled library
- **No dependencies** - Standalone (serialization provided externally)

## Quick Start

```cpp
#include <jaiscript/jaiscript.hpp>

int main() {
    auto engine = JaiScript::createEngine();
    
    // Add a global function
    engine->addFunction("greet", [](const std::vector<JaiScript::Value>& args) {
        return JaiScript::Value("Hello, " + args[0].asString() + "!");
    });
    
    // Execute script
    engine->eval(R"(
        string message = greet("World");
        print(message);
    )");
    
    return 0;
}
```

## Building

JaiScript supports three build modes:

1. **Header-only** (easiest):
   ```cpp
   #include <jaiscript/jaiscript.hpp>
   ```

2. **Compiled library** (fastest builds):
   ```cmake
   add_library(jaiscript Source/JaiScript/src/jaiscript.cpp)
   ```

3. **Single compilation unit** (your project pattern):
   ```cpp
   // In one .cpp file
   #define JAISCRIPT_IMPLEMENTATION
   #include <jaiscript/jaiscript.hpp>
   ```

## Documentation

See the docs/ directory for:
- Language syntax guide
- C++ integration guide
- Hot-reload documentation
- API reference

## License

TBD - Likely MIT or similar permissive license