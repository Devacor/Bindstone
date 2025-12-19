# JaiScript Deep Copy Design

## Overview

JaiScript implements C++-style value semantics by default, meaning all assignments and parameter passing create deep copies of values. This document describes the implementation strategy for achieving true deep copy behavior for all types, including containers and polymorphic C++ objects.

## Design Goals

1. **C++ Semantics**: Assignment means copy, just like in C++
2. **Zero User Overhead**: No special registration needed for copyable types
3. **Polymorphic Support**: Correctly copy derived types through base pointers
4. **Type Safety**: Compile-time checking for copyability
5. **No C++ Class Modification**: Work with existing C++ classes as-is

## Implementation Strategy

### 1. Container Deep Copying

Arrays and maps are deep copied recursively:

```cpp
// Deep copy constructor in script_value
case value_type::jai_array_type: {
    auto& other_array = *std::get<std::shared_ptr<std::vector<script_value>>>(other.storage_);
    auto new_array = std::make_shared<std::vector<script_value>>();
    new_array->reserve(other_array.size());
    for (const auto& elem : other_array) {
        new_array->push_back(elem);  // Recursively deep copies each element
    }
    storage_ = new_array;
    break;
}
```

### 2. Automatic Copy Function Registration

When registering a C++ class, `class_builder` automatically captures the copy constructor:

```cpp
template<typename T>
class class_builder {
    class_builder& build() {
        if constexpr (std::is_copy_constructible_v<T>) {
            classDef_->set_copy_function([](const void* src) -> std::shared_ptr<void> {
                const T* typed_src = static_cast<const T*>(src);
                return std::make_shared<T>(*typed_src);
            });
        }
        // ... rest of build
    }
};
```

### 3. Polymorphic Type Support

For polymorphic types, we use RTTI to maintain the correct derived type:

```cpp
class polymorphic_type_registry {
    struct type_copier {
        std::type_index type_id;
        std::function<std::shared_ptr<void>(const void*)> copy_func;
    };
    
    // Maps base type -> all registered derived type copiers
    std::unordered_map<std::type_index, std::vector<type_copier>> base_to_derived_;
    
    template<typename Derived>
    void register_type() {
        type_copier copier{
            typeid(Derived),
            [](const void* obj) -> std::shared_ptr<void> {
                return std::make_shared<Derived>(*static_cast<const Derived*>(obj));
            }
        };
        base_to_derived_[typeid(Derived)].push_back(copier);
    }
    
    std::shared_ptr<void> copy_polymorphic(const void* obj, std::type_index obj_type) {
        const std::type_info& actual_type = typeid(*static_cast<const polymorphic_base*>(obj));
        
        // Find copier for the actual runtime type
        auto it = base_to_derived_.find(obj_type);
        if (it != base_to_derived_.end()) {
            for (const auto& copier : it->second) {
                if (copier.type_id == actual_type) {
                    return copier.copy_func(obj);
                }
            }
        }
        throw runtime_error("No copier registered for polymorphic type");
    }
};
```

### 4. class_builder API Enhancement

The `base_class()` method registers inheritance relationships and polymorphic copy support:

```cpp
template<typename Base>
class_builder& base_class() {
    static_assert(std::is_base_of_v<Base, T>, 
                  "Specified type is not a base class of this class");
    
    // Set up inheritance relationship
    classDef_->set_parent(engine_.get_class_definition(typeid(Base).name()));
    
    // Register polymorphic copy support
    if constexpr (std::is_polymorphic_v<T>) {
        engine_.register_polymorphic_copier<T, Base>();
    }
    
    return *this;
}
```

## Usage Examples

### Simple Types
```cpp
// C++ registration
make_class_builder<Point>(engine, "Point")
    .constructor<double, double>()
    .property("x", &Point::x)
    .property("y", &Point::y)
    .build();  // Copy support automatically registered!

// JaiScript usage
var p1 = Point(3.0, 4.0);
var p2 = p1;        // Deep copy via Point's copy constructor
p2.x = 5.0;         // Only affects p2
print(p1.x);        // Still 3.0
```

### Polymorphic Types
```cpp
// C++ classes (unchanged)
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
};

class Circle : public Shape {
    double radius;
public:
    Circle(double r) : radius(r) {}
    double area() const override { return 3.14159 * radius * radius; }
};

// Registration
make_class_builder<Shape>(engine, "Shape")
    .method("area", &Shape::area)
    .build();

make_class_builder<Circle>(engine, "Circle")
    .base_class<Shape>()  // This enables polymorphic copy!
    .constructor<double>()
    .build();

// JaiScript usage
var shape = Circle(5.0);  // Stored as Shape* internally
var copy = shape;         // Correctly copies as Circle!
```

### Containers
```cpp
// Deep copy by default
var map1 = {"a": 1, "b": 2};
var map2 = map1;          // Deep copy - separate storage
map2["c"] = 3;            // Only affects map2

var arr1 = [Point(1, 2), Point(3, 4)];
var arr2 = arr1;          // Deep copy - including all Points
arr2[0].x = 99;           // Only affects arr2's first Point
```

## Semantics Summary

JaiScript implements:
- Container deep copying (arrays and maps)
- Deep copy as default behavior, matching C++ semantics
- Reference support with `&` syntax for explicit aliasing
- `weak_ptr<T>` support for non-owning references
- `shared_ptr<T>` for ownership semantics

## Migration Notes

### Breaking Changes
1. Container assignment now creates deep copies instead of sharing references
2. Object assignment creates deep copies using copy constructors

### Code that needs updating:
```javascript
// Old behavior (shared reference)
var map1 = {"a": 1};
var map2 = map1;
map2["b"] = 2;  // Would affect map1

// New behavior (deep copy)
var map1 = {"a": 1};
var map2 = map1;
map2["b"] = 2;  // Only affects map2

// To share containers, use explicit wrapping:
var shared = {"map": {"a": 1}};
var ref1 = shared;
var ref2 = shared;
// Now ref1.map and ref2.map refer to the same map
```

## Future Considerations

1. **Move Semantics**: Could add move operations for performance
2. **Copy-on-Write**: Potential optimization for large containers
3. **Shallow Copy Option**: Might add explicit shallow copy function
4. **Reference Wrapper Type**: For explicit reference semantics when needed