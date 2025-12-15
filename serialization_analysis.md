# JaiScript Serialization Analysis: to_json / from_json

## Current Architecture

### Script API (stdlib functions)

```cpp
// Registered via register_json_functions()
to_json(value)           // Compact JSON
to_json(value, indent)   // Pretty-printed JSON
from_json(json_string)   // Reconstruct objects
```

### C++ API (archive classes)

```cpp
// Writing
serialization::json_archive_writer writer(indent);
writer.write_value(script_value);
std::string json = writer.str();

// Reading
serialization::json_archive_reader reader(json, engine_weak);
script_value value = reader.read_value();
```

## How to_json Works

### Path 1: Script Function → Archive Writer
When you call `to_json(obj)` from script:

1. **Script Call**: `to_json(my_object)`
2. **C++ Handler**: `register_json_functions()` → creates `json_archive_writer`
3. **Writer**: `writer.write_value(script_value)`
4. **Delegation**: For objects, calls `to_json_impl_fallback()` which:
   - **PROBLEM**: Returns `"null"` for objects! ❌
   - Does NOT serialize properties

### Path 2: Original to_json_impl (from stdlib/json.hpp)
The OLD implementation (lines 41-224) properly handles objects:

```cpp
case script_value_type::jai_object_type: {
    auto instance = val.as<std::shared_ptr<class_instance>>();
    if (instance) {
        auto classDef = instance->get_class_definition();

        // Writes _type_ field
        oss << "\"_type_\": \"" << instance->get_class_name() << "\"";

        // Serializes all properties via getters
        for (const auto& propName : classDef->get_property_names()) {
            auto getter = classDef->get_method("_get_" + propName);
            auto result = getter.as_function()({val});
            // ... write property value
        }
    }
}
```

**This version WORKS** ✓ but is NOT used by `json_archive_writer`!

## How from_json Works

### Script Function → Object Reconstruction

When you call `from_json(json)` from script:

1. **Parse JSON**: Uses `json_archive_reader` to parse into maps/arrays
2. **Find _type_**: Checks for `_type_` field in map
3. **Create Instance**: Calls `engine->execute(type_name + "()")`
4. **Set Properties**: For each key (except `_type_`):
   ```cpp
   eng->add_global(tempVar, propValue);
   eng->add_global("_json_obj", instance);
   eng->execute("_json_obj." + propName + " = " + tempVar);
   ```
5. **Call post_deserialize**: If method exists, call with version

This works for **script-defined classes** ✓ and **class_builder classes** ✓

## Issues with class_builder Classes

### Problem: to_json doesn't serialize properties

```cpp
// C++
class_builder<Rectangle>(eng, "Rectangle")
    .property("width", &Rectangle::width)
    .property("height", &Rectangle::height)
    .build();

// Script
var rect = Rectangle();
rect.width = 10;
rect.height = 20;
var json = to_json(rect);
// Result: "null" ❌ Should be: {"_type_":"Rectangle","width":10,"height":20}
```

### Why?

1. `to_json()` uses `json_archive_writer`
2. `json_archive_writer.write_value()` delegates objects to `to_json_impl_fallback()`
3. `to_json_impl_fallback()` returns `"null"` for objects (line 336)

The correct handler exists in `to_json_impl()` (line 138-190) but isn't used!

## Solutions

### Option 1: Fix json_archive_writer.write_value()

Make it use the proper object serialization from `to_json_impl()`:

```cpp
case script_value_type::jai_object_type: {
    // Use the working implementation from stdlib
    auto instance = value.as<std::shared_ptr<class_instance>>();
    if (instance) {
        auto classDef = instance->get_class_definition();

        oss_ << '{';
        oss_ << "\"_type_\": \"" << instance->get_class_name() << "\"";

        // Serialize properties...
        for (const auto& propName : classDef->get_property_names()) {
            auto getter = classDef->get_method("_get_" + propName);
            if (getter.type() == script_value_type::jai_function_type) {
                std::vector<script_value> args = {value};
                auto result = getter.as_function()(args);
                if (result) {
                    oss_ << ",\"" << propName << "\":";
                    write_value(result.value()); // Recursive
                }
            }
        }

        oss_ << '}';
    }
    break;
}
```

### Option 2: Direct C++ Archive API

For C++ classes that use `property_owner`, you can bypass script functions:

```cpp
// Serialization (works today)
Rectangle rect;
rect.width = 10;
rect.height = 20;

serialization::json_archive_writer writer(2); // pretty-print
writer.begin_object("Rectangle", 1);
rect.property_mgr.save(writer);
writer.end_object();
std::string json = writer.str();

// Deserialization (works today)
Rectangle loaded;
serialization::json_archive_reader reader(json, eng);

std::string type_name;
uint32_t version;
reader.begin_object(type_name, version);
loaded.property_mgr.load(reader);
loaded.post_deserialize(reader); // Call hook
reader.end_object();
```

## Usage Patterns

### For property_owner classes (C++)

```cpp
class MyClass : public property_owner {
    JAI_PROPERTY((int), width, 0);
    JAI_PROPERTY((int), height, 0);

    void post_deserialize(archive_reader& ar) override {
        // Migration logic
    }
};

// Direct C++ serialization
MyClass obj;
json_archive_writer writer(2);
writer.begin_object("MyClass", 1);
obj.property_mgr.save(writer);
writer.end_object();

// Direct C++ deserialization
MyClass loaded;
json_archive_reader reader(json, eng);
std::string type; uint32_t ver;
reader.begin_object(type, ver);
loaded.property_mgr.load(reader);
loaded.post_deserialize(reader);
reader.end_object();
```

### For class_builder classes (C++)

**Currently**: to_json() doesn't work ❌
**from_json() works** ✓ but requires script execution

```cpp
// Registration
class_builder<Rectangle>(eng, "Rectangle")
    .property("width", &Rectangle::width)
    .property("height", &Rectangle::height)
    .post_deserialize_hook([](Rectangle& self, int version) {
        // Migration logic
    })
    .build();

// Serialization (BROKEN - needs fix)
eng.execute("var rect = Rectangle(); rect.width = 10;");
auto json = eng.execute("to_json(rect)"); // Returns "null" ❌

// Deserialization (WORKS)
std::string json = R"({"_type_":"Rectangle","width":15,"height":25})";
eng.add_global("json_str", script_value(json, eng.weak_from_this()));
auto loaded = eng.execute("from_json(json_str)"); // Works! ✓
```

### For script-defined classes

Both work ✓:

```cpp
eng.execute(R"(
    class GameCharacter {
        var health = 100;

        function post_deserialize(version) {
            // Migration logic
        }
    }

    var char = GameCharacter();
    char.health = 80;
    var json = to_json(char);      // Works! ✓
    var loaded = from_json(json);  // Works! ✓
)");
```

## Recommendations

1. **Fix json_archive_writer.write_value()** to properly serialize objects with properties
   - Copy logic from `to_json_impl()` (lines 138-190)
   - Make it handle `class_instance` objects
   - Ensure it writes `_type_` and property values

2. **Add _version_ field** to serialized objects
   - Currently only `_type_` is written
   - Should write both for deserialization hooks

3. **Document C++ API** for direct archive usage
   - property_owner classes can use archives directly
   - class_builder classes must use script functions (until fix #1)

4. **Add tests** verifying:
   - class_builder to_json/from_json round-trip
   - C++ direct archive API for property_owner
   - Version-aware post_deserialize hooks

## Current Test Status

After fixes:
- ✅ 30 tests passing
- ❌ 4 tests failing (hooks not fully tested due to to_json issue)

Tests verify:
- property_owner serialization (C++ archives) ✓
- Script class from_json() with post_deserialize ✓
- class_builder hook registration ✓
- **Missing**: class_builder to_json() / full round-trip ❌
