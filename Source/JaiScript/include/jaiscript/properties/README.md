# JaiScript Properties System

A generic, reflection-enabled property system with built-in serialization support, migrated from Bindstone's MV::Property system.

## Features

- **Transparent value access** - Properties behave like their underlying type
- **Automatic reflection** - All properties auto-register with `property_manager`
- **Serialization support** - JSON and binary archives built-in
- **Version migration** - Add/remove/rename fields with backward compatibility
- **Selective serialization** - Enable/disable per-property
- **Clone support** - Deep copy with custom clone functions
- **Container support** - Works with `std::vector`, `std::map`, etc.

## Quick Start

```cpp
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/properties/property_serialization.hpp>

class Player : public jai::property_owner {
public:
    // Note: Types MUST be wrapped in parentheses
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((float), speed, 5.0f);
    JAI_PROPERTY((std::string), name, "Player");
    JAI_PROPERTY((bool), active, true);

    // Complex types need parentheses too
    JAI_PROPERTY((std::vector<int>), scores);
    JAI_PROPERTY((std::map<std::string, int>), inventory);
};

// Usage
Player p;
p.health = 50;                    // Direct assignment
int h = p.health;                 // Implicit conversion
p.health += 10;                   // Arithmetic operators
if (p.health > 0) { /*...*/ }    // Comparison operators

// Reflection
for (auto& [name, prop] : p.property_mgr.all()) {
    std::cout << name << std::endl;
}

// Serialization
#include <jaiscript/serialization/json_archive.hpp>

jai::serialization::json_archive_writer writer;
writer.begin_object("Player", 1);
p.property_mgr.save(writer);
writer.end_object();

std::string json = writer.str();
```

## Macros

### JAI_PROPERTY(type, name, default_value...)
Declares a property with optional default value.

**Important**: Types **must** be wrapped in parentheses to handle commas in template arguments.

```cpp
JAI_PROPERTY((int), health, 100);
JAI_PROPERTY((std::string), name, "Default");
JAI_PROPERTY((std::vector<int>), numbers);  // No default
JAI_PROPERTY((std::map<std::string, float>), data);
```

### JAI_DELETED_PROPERTY(type, name)
Marks a property as deleted for version migration. Allows old serialized data to be skipped.

```cpp
JAI_DELETED_PROPERTY((int), old_field);
```

### JAI_NAMED_PROPERTY(type, prop_name, var_name, default_value...)
Creates a property with different property name vs variable name.

```cpp
JAI_NAMED_PROPERTY((int), "healthPoints", health, 100);
// Property name in serialization: "healthPoints"
// Variable name in code: health
```

## Property Features

### Transparent Access
Properties support all operators of their underlying type:

```cpp
JAI_PROPERTY((int), count, 0);

count = 10;           // Assignment
count += 5;           // Compound assignment
++count;              // Increment
int x = count * 2;    // Arithmetic
if (count > 5) {}     // Comparison
```

### Container Support
Properties work seamlessly with STL containers:

```cpp
JAI_PROPERTY((std::vector<int>), numbers);

numbers.get().push_back(1);
for (int n : numbers) { /*...*/ }  // Range-based for
numbers[0] = 10;                   // Subscript operator
```

### Pointer Support
Properties handle smart pointers and raw pointers:

```cpp
JAI_PROPERTY((std::shared_ptr<Node>), node);

node->method();        // Arrow operator
if (node) { /*...*/ }  // Boolean conversion
```

### Serialization Control

```cpp
JAI_PROPERTY((int), secret_value, 42);

// Disable serialization for specific properties
secret_value.serialize_enabled(false);

// Check status
bool enabled = secret_value.serialize_enabled();
```

## Serialization

### Basic Serialization

```cpp
#include <jaiscript/serialization/json_archive.hpp>

// Write
jai::serialization::json_archive_writer writer;
writer.begin_object("MyClass", 1);  // version = 1
obj.property_mgr.save(writer);
writer.end_object();

std::string json = writer.str();

// Read
jai::serialization::json_archive_reader reader(json);
std::string type_name;
uint32_t version;
reader.begin_object(type_name, version);
obj.property_mgr.load(reader);
reader.end_object();
```

### Binary Serialization

```cpp
#include <jaiscript/serialization/binary_archive.hpp>

std::vector<uint8_t> buffer;
jai::serialization::binary_archive_writer writer(buffer);
writer.begin_object("MyClass", 1);
obj.property_mgr.save(writer);
writer.end_object();

// Read
jai::serialization::binary_archive_reader reader(buffer);
// ... same as JSON
```

## Version Migration

### Adding New Fields

```cpp
// Version 1
class Player_v1 : public jai::property_owner {
    JAI_PROPERTY((int), x);
    JAI_PROPERTY((int), y);
};

// Version 2 (added z)
class Player_v2 : public jai::property_owner {
    JAI_PROPERTY((int), x);
    JAI_PROPERTY((int), y);
    JAI_PROPERTY((int), z, 0);  // New field with default
};

// Load v1 data into v2
player_v2.property_mgr.load(reader, {"x", "y"});  // Specify old field order
```

### Renaming Fields

```cpp
// Version 2 (field named "label")
class Object_v2 : public jai::property_owner {
    JAI_PROPERTY((std::string), label);
};

// Version 3 (renamed to "description")
class Object_v3 : public jai::property_owner {
    JAI_PROPERTY((std::string), description);
    JAI_DELETED_PROPERTY((std::string), label);
};

// Load v2 data into v3
std::unordered_map<std::string, std::string> renames = {
    {"label", "description"}
};
obj_v3.property_mgr.load(reader, {"label"}, renames);
```

### Removing Fields

```cpp
// Version 3
class Player_v3 : public jai::property_owner {
    JAI_PROPERTY((int), x);
    JAI_PROPERTY((int), y);
    JAI_DELETED_PROPERTY((int), old_field);  // Removed field
};

// Old serialized data will skip "old_field"
```

## Clone Support

```cpp
// Custom deep copy
JAI_PROPERTY((std::shared_ptr<Node>), node, nullptr,
    [](auto& src, auto& dst) {
        if (src.get()) {
            dst = std::make_shared<Node>(*src.get());
        }
    });

// Clone to another object
source_obj.property_mgr.clone_to_target(dest_obj.property_mgr);
```

## Architecture

- **property_base**: Abstract base class for type-erased property access
- **property<T>**: Template class implementing typed property
- **deleted_property<T>**: Placeholder for removed fields (versioning)
- **property_manager**: Reflection registry, owns property references
- **property_owner**: Base class providing `property_mgr` member

## Migration from MV::Property

The JaiScript property system is designed for easy migration from Bindstone:

```cpp
// Before (Bindstone)
class MyClass : public MV::PropertyOwner {
    MV_PROPERTY((int), health, 100);
    MV_PROPERTY((std::string), name);
};

// After (JaiScript)
class MyClass : public jai::property_owner {
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((std::string), name);
};
```

Key differences:
- `property_owner` instead of `PropertyOwner` (snake_case)
- `property_mgr` instead of `propertyManager`
- All methods use snake_case: `serialize_enabled()`, `allow_save()`
- Uses JaiScript archives instead of Cereal

## Naming Conventions

All JaiScript code uses **snake_case**:
- Classes: `property_manager`, `property_owner`, `property_base`
- Methods: `serialize_enabled()`, `allow_save()`, `clone_to_target()`
- Members: `property_mgr`, `m_value`, `m_allow_serialization`

This differs from Bindstone's PascalCase but follows JaiScript conventions.
