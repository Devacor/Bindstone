# JaiScript Serializer API Documentation

## Overview

The JaiScript serializer provides a simple, builder-style API for serializing and deserializing C++ objects registered with `class_builder`. The API automatically looks up type names and versions from the engine's class registry, eliminating the need to manually specify metadata.

## Key Features

- **Automatic Type/Version Lookup**: When you provide an engine reference, type names and versions are automatically retrieved from `class_builder` registrations
- **Builder-Style API**: Intuitive chaining syntax: `writer w(engine); w(obj1); w(obj2); auto data = w.save();`
- **Multiple Format Support**: JSON (human-readable) and Binary (compact) formats
- **Single and Multiple Objects**: Automatically wraps multiple objects in an array
- **Convenience Functions**: Simple one-liners for common use cases

## Basic Usage

### Automatic Type/Version Lookup (Recommended)

When you register a class with `class_builder`, the serializer can automatically determine the type name and version:

```cpp
// 1. Register your class
auto eng = std::make_shared<engine>();

class_builder<MyClass>(*eng, "MyClass")
    .version(2)
    .property("value", &MyClass::value)
    .property("name", &MyClass::name)
    .build();

// 2. Serialize (automatic lookup)
MyClass obj;
obj.value = 42;
obj.name = "test";

writer w(*eng);
w(obj);  // No type name or version needed!
auto json = w.save();
// Result: {"_type_":"MyClass","_version_":2,"value":42,"name":"test"}

// 3. Deserialize
MyClass loaded;
from_json(loaded, json, eng->weak_from_this());
```

### Manual Type/Version Specification

If you don't have an engine reference or want to override the registered metadata:

```cpp
MyClass obj;
obj.value = 42;

// Without engine
writer w;
w(obj, "MyClass", 1);  // Explicit type and version
auto json = w.save();

// Or using convenience function
auto json = to_json(obj, "MyClass", 1);
```

## API Reference

### Writer Class

The `writer` class accumulates objects and serializes them when `save()` is called.

#### Constructors

```cpp
// With engine (enables automatic lookup)
writer(engine& eng, bool use_binary = false, int indent = 2)
writer(std::shared_ptr<engine> eng, bool use_binary = false, int indent = 2)
writer(std::weak_ptr<engine> eng, bool use_binary = false, int indent = 2)

// Without engine (requires manual type/version)
writer(std::weak_ptr<engine> eng = {}, bool use_binary = false, int indent = 2)
```

#### Methods

```cpp
// Add object with automatic lookup (requires engine)
template<typename T>
writer& operator()(const T& obj);

// Add object with explicit type/version
template<typename T>
writer& operator()(const T& obj, const std::string& type_name, uint32_t version = 1);

// Save and return serialized data
std::string save();
```

### Reader Class

The `reader` class deserializes data into objects.

#### Constructor

```cpp
reader(std::weak_ptr<engine> engine_weak, const std::string& data, bool is_binary = false)
reader(std::shared_ptr<engine> engine_ptr, const std::string& data, bool is_binary = false)
```

#### Methods

```cpp
// Read into property_owner object
template<typename T>
reader& operator()(T& obj);

// Set user context for custom deserialization
template<typename T>
void set_user_context(T* context);
```

### Convenience Functions

#### JSON Serialization

```cpp
// With automatic lookup
std::string to_json(engine& eng, const T& obj, int indent = 2);
std::string to_json(std::shared_ptr<engine> eng, const T& obj, int indent = 2);

// With manual specification
std::string to_json(const T& obj, const std::string& type_name, uint32_t version = 1, int indent = 2);
```

#### Binary Serialization

```cpp
// With automatic lookup
std::string to_binary(engine& eng, const T& obj);
std::string to_binary(std::shared_ptr<engine> eng, const T& obj);

// With manual specification
std::string to_binary(const T& obj, const std::string& type_name, uint32_t version = 1);
```

#### Deserialization

```cpp
// Basic deserialization
void from_json(engine& eng, T& obj, const std::string& json);
void from_binary(engine& eng, T& obj, const std::string& data);

// With context object for non-default constructors (similar to Cereal's UserDataAdapter)
template<typename T, typename Context>
void from_json(engine& eng, T& obj, const std::string& json, Context& context);
template<typename T, typename Context>
void from_binary(engine& eng, T& obj, const std::string& data, Context& context);

// Property owner convenience (gets engine from object)
void from_json(T& obj, const std::string& json);  // Requires obj.bind_to_engine() first
void from_binary(T& obj, const std::string& data);
template<typename Context>
void from_json(T& obj, const std::string& json, Context& context);
template<typename Context>
void from_binary(T& obj, const std::string& data, Context& context);
```

## Examples

### Single Object Serialization

```cpp
auto eng = std::make_shared<engine>();

// Register class
class_builder<GameCharacter>(*eng, "GameCharacter")
    .version(1)
    .property("health", &GameCharacter::health)
    .property("position", &GameCharacter::position)
    .build();

// Serialize
GameCharacter player;
player.health = 100;
player.position = Vector3(10, 20, 30);

auto json = to_json(*eng, player);
// Output: {"_type_":"GameCharacter","_version_":1,"health":100,"position":{...}}

// Deserialize
GameCharacter loaded;
from_json(loaded, json, eng->weak_from_this());
```

### Multiple Object Serialization

```cpp
// Create multiple objects
GameCharacter player1, player2, enemy;

// Serialize all at once
writer w(*eng);
w(player1);
w(player2);
w(enemy);
auto json = w.save();
// Output: [{"_type_":"GameCharacter",...}, {"_type_":"GameCharacter",...}, {"_type_":"GameCharacter",...}]

// Deserialize
GameCharacter loaded1, loaded2, loaded3;
reader r(eng, json);
r(loaded1);
r(loaded2);
r(loaded3);
```

### Version Migration with post_deserialize

```cpp
// Register class with version 2
class_builder<SaveGame>(*eng, "SaveGame")
    .version(2)
    .property("player_name", &SaveGame::player_name)
    .property("level", &SaveGame::level)
    .property("new_field", &SaveGame::new_field)  // Added in v2
    .post_deserialize_hook([](SaveGame& self, int version) {
        if (version < 2) {
            // Migrate from v1 to v2
            self.new_field = "default_value";
        }
    })
    .build();

// Load old save file (version 1)
SaveGame save;
from_json(save, old_v1_json, eng->weak_from_this());
// post_deserialize hook runs, migrates data
```

### Compact JSON Output

```cpp
// Set indent=0 for compact JSON
writer w(*eng, false, 0);
w(obj);
auto compact = w.save();
// Output: {"_type_":"MyClass","_version_":1,"value":42}  (no newlines)
```

### Binary Format

```cpp
// Binary format is more compact than JSON
auto binary = to_binary(*eng, obj);
std::cout << "JSON size: " << to_json(*eng, obj).size() << std::endl;
std::cout << "Binary size: " << binary.size() << std::endl;

// Deserialize from binary
MyClass loaded;
from_binary(*eng, loaded, binary);
```

### Nested Object Serialization

```cpp
// Nested objects are automatically handled via script_value properties
class Character : public property_owner {
public:
    JAI_PROPERTY((std::string), name, "");
    JAI_PROPERTY((int), health, 100);
    // In script code, you can add: property inventory: Inventory;
};

class Inventory : public property_owner {
public:
    JAI_PROPERTY((int), gold, 0);
    JAI_PROPERTY((std::vector<std::string>), items, {});
};

// Register both classes
class_builder<Character>(*eng, "Character")
    .version(1)
    .property("name", &Character::name)
    .property("health", &Character::health)
    .build();

class_builder<Inventory>(*eng, "Inventory")
    .version(1)
    .property("gold", &Inventory::gold)
    .property("items", &Inventory::items)
    .build();

// The serialization system automatically handles nested objects through
// write_value() recursion - when a property contains another object,
// it's serialized recursively with full type information
```

### Context Object Support for Non-Default Constructors

```cpp
// Define a context object with external dependencies
struct GameContext {
    AssetManager* assets;
    ServiceLocator* services;
    int server_id;
};

// Register a custom factory that uses the context
class_builder<GameObject>(*eng, "GameObject")
    .version(1)
    .custom_factory([](archive_reader& ar, uint32_t version) -> script_value {
        // Retrieve the context object
        auto* context = ar.get_user_context<GameContext>();
        if (!context) {
            throw std::runtime_error("GameContext required for deserialization");
        }

        // Use context to construct object with dependencies
        auto obj = std::make_shared<GameObject>(context->assets, context->services);

        // Deserialize properties normally
        // (property loading happens after factory returns)

        return script_value::make_shared(obj, ar.get_engine());
    })
    .property("position", &GameObject::position)
    .property("rotation", &GameObject::rotation)
    .build();

// Deserialize with context (similar to Cereal's UserDataAdapter)
GameContext context{&asset_mgr, &service_locator, server_id};
GameObject loaded_obj;
from_json(*eng, loaded_obj, json_data, context);

// Or with binary format
from_binary(*eng, loaded_obj, binary_data, context);
```

## Design Philosophy

### Why Automatic Lookup?

The old API required manually specifying type names and versions:

```cpp
// Old way - error-prone!
writer.begin_object("MyClass", 1);
obj.property_mgr.save(writer);
writer.end_object();
auto json = writer.str();
```

The new API eliminates this duplication:

```cpp
// New way - cleaner and safer!
writer w(engine);
w(obj);
auto json = w.save();
```

### Why Builder Style?

The builder pattern makes multi-object serialization intuitive:

```cpp
// Single object - returns object directly
writer w(engine);
w(obj);
auto json = w.save();  // {"_type_":"MyClass",...}

// Multiple objects - automatically wraps in array
writer w(engine);
w(obj1);
w(obj2);
auto json = w.save();  // [{"_type_":"MyClass",...}, {"_type_":"MyClass",...}]
```

## Requirements

- Your class must inherit from `property_owner`
- Properties must be registered with `JAI_PROPERTY` macro and `property_mgr.register_property()`
- Class must be registered with `class_builder` for automatic lookup
- Version must be set with `.version()` (defaults to 1)

## Limitations

- **Multiple Object Binary Serialization**: Currently limited. Use JSON for multiple objects or the archive API directly.
- **Script Values**: Serialization of raw `script_value` objects not yet fully implemented
- **Custom Factories**: Complex deserialization factories need archive API

For advanced use cases, use the archive API directly: `json_archive_writer`, `binary_archive_writer`, etc.
