#pragma once

#ifndef __JAISCRIPT_CORE_PROPERTY_TYPE_CONVERTER_HPP__
#define __JAISCRIPT_CORE_PROPERTY_TYPE_CONVERTER_HPP__

#include <jaiscript/jaiscript_fwd.hpp>
#include <jaiscript/core/types.hpp>
#include <typeindex>
#include <functional>
#include <unordered_map>
#include <string>
#include <memory>

namespace jai {

// Forward declarations
class script_value;
class engine;

// ============================================================================
// Property Type Converter
// ============================================================================
//
// Type-erased converter for property values. This allows complex types like
// MV::Point<int, int> to be bound as property values without modifying the
// core dynamic_binder code.
//
// Usage:
//   // At file scope (static initialization):
//   static jai::property_type_converter_registrar<MV::Point<int, int>> _point_converter(
//       "Point",
//       [](const MV::Point<int, int>& p, engine* eng) {
//           // Create a script object representation
//           return script_value::make_object("Point", create_point_instance(p, eng), eng);
//       },
//       [](const script_value& sv) {
//           // Extract C++ value from script object
//           return MV::Point<int, int>(sv["x"].as_int(), sv["y"].as_int());
//       }
//   );

struct property_type_converter {
    std::string type_name;
    std::type_index type_id{typeid(void)};  // Default to void type

    // Type-erased converter: void* (C++ value) -> script_value
    std::function<script_value(const void*, engine*)> to_script;

    // Type-erased converter: script_value -> void (stores in provided void*)
    std::function<void(const script_value&, void*)> from_script;

    // Size of the C++ type (for allocation)
    size_t type_size = 0;

    property_type_converter() = default;
    property_type_converter(property_type_converter&&) = default;
    property_type_converter& operator=(property_type_converter&&) = default;
    property_type_converter(const property_type_converter&) = default;
    property_type_converter& operator=(const property_type_converter&) = default;
};

// ============================================================================
// Global Property Type Converter Registry
// ============================================================================
//
// Singleton registry that maps type_index to conversion functions.
// Populated at static initialization time via property_type_converter_registrar.

class property_type_converter_registry {
public:
    static property_type_converter_registry& instance() {
        static property_type_converter_registry registry;
        return registry;
    }

    // Register a converter (called at static initialization)
    void register_converter(property_type_converter converter) {
        converters_[converter.type_id] = std::move(converter);
    }

    // Check if a type has a registered converter
    bool has_converter(std::type_index type_id) const {
        return converters_.find(type_id) != converters_.end();
    }

    // Get converter for a type (returns nullptr if not found)
    const property_type_converter* get_converter(std::type_index type_id) const {
        auto it = converters_.find(type_id);
        return it != converters_.end() ? &it->second : nullptr;
    }

    // Template convenience methods
    template<typename T>
    bool has_converter() const {
        return has_converter(std::type_index(typeid(T)));
    }

    template<typename T>
    const property_type_converter* get_converter() const {
        return get_converter(std::type_index(typeid(T)));
    }

private:
    property_type_converter_registry() = default;
    std::unordered_map<std::type_index, property_type_converter> converters_;
};

// ============================================================================
// Static Registrar Helper
// ============================================================================
//
// Use at file scope to register a type converter at program start.
//
// Example:
//   static jai::property_type_converter_registrar<MV::Point<int, int>> _point_reg(
//       "Point",
//       [](const MV::Point<int, int>& p, engine* eng) { ... },
//       [](const script_value& sv) -> MV::Point<int, int> { ... }
//   );

template<typename T>
struct property_type_converter_registrar {
    property_type_converter_registrar(
        const char* type_name,
        std::function<script_value(const T&, engine*)> to_script,
        std::function<T(const script_value&)> from_script
    ) {
        // Create type-erased wrappers
        property_type_converter converter;
        converter.type_name = type_name;
        converter.type_id = std::type_index(typeid(T));
        converter.type_size = sizeof(T);

        // Type-erased to_script: takes void* pointing to T
        converter.to_script = [to_script_fn = std::move(to_script)](const void* ptr, engine* eng) -> script_value {
            const T* typed_ptr = static_cast<const T*>(ptr);
            return to_script_fn(*typed_ptr, eng);
        };

        // Type-erased from_script: stores result in void* pointing to T
        converter.from_script = [from_script_fn = std::move(from_script)](const script_value& sv, void* ptr) {
            T* typed_ptr = static_cast<T*>(ptr);
            *typed_ptr = from_script_fn(sv);
        };

        property_type_converter_registry::instance().register_converter(std::move(converter));
    }
};

// ============================================================================
// Built-in Type Registration
// ============================================================================
//
// Note: Primitive types (int, float, bool, string, etc.) are handled directly
// in dynamic_binder without needing registration. Only complex types need to
// be registered via property_type_converter_registrar.

} // namespace jai

#endif // __JAISCRIPT_CORE_PROPERTY_TYPE_CONVERTER_HPP__
