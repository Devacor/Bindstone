#pragma once

#ifndef __JAISCRIPT_CORE_ENGINE_IMPL_HPP__
#define __JAISCRIPT_CORE_ENGINE_IMPL_HPP__

#include "engine.hpp"
#include "conversion_registry.hpp"
#include "class_builder.hpp"

namespace jai {

// Implementation of get_registered_name template
template<typename T>
std::string engine::get_registered_name() const {
    auto class_def = get_class_definition_by_type(std::type_index(typeid(T)));
    if (class_def) {
        return class_def->get_name();
    }
    // Fallback to raw type name
    return typeid(T).name();
}

// Forward declaration helper function for conversion_registry_impl.hpp
// Implementation moved to engine.cpp

// Template implementations that need both engine and conversion_registry

// Helper function for function_binder to convert custom types using engine's conversion registry
template<typename T>
script_value convert_custom_type_with_registry(const T& t, engine* eng) {
    auto registry = eng->get_conversion_registry();
    if (registry && registry->template has_conversion<T>()) {
        return registry->template convert_to_script<T>(t);
    }
    
    // Fallback: no class registered, create a simple object wrapper
    // This should rarely happen since class_builder automatically registers conversions
    auto sharedObj = std::make_shared<T>(t);
    if (!eng) {
        throw runtime_error("Engine reference required for custom type conversion");
    }
    // Use engine to create object with proper type name lookup
    return eng->make_object(sharedObj);
}

// Implementation of engine-aware vector conversion function
template<typename T>
script_value conversions::convert_vector_to_script_array(const std::vector<T>& vec, engine* eng) {
    // Create array with proper element type info for registered types
    type_info_ptr element_type;
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                   std::is_same_v<T, float> || std::is_same_v<T, double> ||
                   std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                   std::is_same_v<T, std::string> || std::is_same_v<T, script_value>) {
        // For basic types, use nullptr (generic array)
        element_type = nullptr;
    } else {
        // For registered types, create persistent type_info
        element_type = eng->get_type_info_for_cpp_type<T>();
    }

    auto script_array = script_value::make_array(element_type, get_engine_weak_ptr(eng));
    auto& arr = const_cast<std::vector<script_value>&>(script_array.as_array());
    
    for (const auto& item : vec) {
        if constexpr (std::is_same_v<T, script_value>) {
            arr.push_back(item);
        } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                           std::is_same_v<T, float> || std::is_same_v<T, double> ||
                           std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                           std::is_same_v<T, std::string>) {
            // For basic types that script_value supports directly
            arr.push_back(script_value(item, get_engine_weak_ptr(eng)));
        } else {
            // For custom types, use the conversion registry if available
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<T>()) {
                    arr.push_back(registry->template convert_to_script<T>(item));
                    continue;
                }
            }
            // Fallback: create raw C++ object with engine reference
            auto sharedObj = std::make_shared<T>(item);
            arr.push_back(eng->make_object(sharedObj));
        }
    }
    
    return script_array;
}

// Engine-aware map conversion implementation
template<typename K, typename V>
script_value conversions::convert_stdmap_to_script_map(const std::map<K, V>& stdmap, engine* eng) {
    // Create map with proper key/value type info for registered types
    type_info_ptr key_type;
    type_info_ptr value_type;

    // Determine key type
    if constexpr (std::is_same_v<K, int> || std::is_same_v<K, int64_t> ||
                   std::is_same_v<K, float> || std::is_same_v<K, double> ||
                   std::is_same_v<K, bool> || std::is_same_v<K, char> ||
                   std::is_same_v<K, std::string> || std::is_same_v<K, script_value>) {
        key_type = nullptr;
    } else {
        key_type = eng->get_type_info_for_cpp_type<K>();
    }

    // Determine value type
    if constexpr (std::is_same_v<V, int> || std::is_same_v<V, int64_t> ||
                   std::is_same_v<V, float> || std::is_same_v<V, double> ||
                   std::is_same_v<V, bool> || std::is_same_v<V, char> ||
                   std::is_same_v<V, std::string> || std::is_same_v<V, script_value>) {
        value_type = nullptr;
    } else {
        value_type = eng->get_type_info_for_cpp_type<V>();
    }

    auto script_map = script_value::make_map(key_type, value_type, get_engine_weak_ptr(eng));
    auto& map = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
    
    for (const auto& [key, value] : stdmap) {
        script_value converted_key = script_value(std::monostate{}, get_engine_weak_ptr(eng));
        script_value converted_value = script_value(std::monostate{}, get_engine_weak_ptr(eng));
        
        // Convert key
        if constexpr (std::is_same_v<K, script_value>) {
            converted_key = key;
        } else if constexpr (std::is_same_v<K, int> || std::is_same_v<K, int64_t> ||
                           std::is_same_v<K, float> || std::is_same_v<K, double> ||
                           std::is_same_v<K, bool> || std::is_same_v<K, char> ||
                           std::is_same_v<K, std::string>) {
            converted_key = script_value(key, get_engine_weak_ptr(eng));
        } else {
            // For custom key types, use conversion registry if available
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<K>()) {
                    converted_key = registry->template convert_to_script<K>(key);
                } else {
                    // Fallback: create raw C++ object with engine reference
                    auto sharedObj = std::make_shared<K>(key);
                    converted_key = eng->make_object(sharedObj);
                }
            } else {
                // Fallback: create raw C++ object with engine reference
                auto sharedObj = std::make_shared<K>(key);
                converted_key = eng->make_object(sharedObj);
            }
        }
        
        // Convert value
        if constexpr (std::is_same_v<V, script_value>) {
            converted_value = value;
        } else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, int64_t> ||
                           std::is_same_v<V, float> || std::is_same_v<V, double> ||
                           std::is_same_v<V, bool> || std::is_same_v<V, char> ||
                           std::is_same_v<V, std::string>) {
            converted_value = script_value(value, get_engine_weak_ptr(eng));
        } else {
            // For custom value types, use conversion registry if available
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<V>()) {
                    converted_value = registry->template convert_to_script<V>(value);
                } else {
                    // Fallback: create raw C++ object with engine reference
                    auto sharedObj = std::make_shared<V>(value);
                    converted_value = eng->make_object(sharedObj);
                }
            } else {
                // Fallback: create raw C++ object with engine reference
                auto sharedObj = std::make_shared<V>(value);
                converted_value = eng->make_object(sharedObj);
            }
        }
        
        map.insert_or_assign(converted_key, converted_value);
    }
    
    return script_map;
}

// Template implementations moved to engine_templates.hpp to avoid circular dependencies

// Implementation of conversion_manager methods are now in conversion_registry_templates.hpp

// Public template implementation for add_custom_conversion
template<typename T>
void engine::add_custom_conversion(
    std::function<T(const script_value&)> from_func,
    std::function<script_value(const T&)> to_func
) {
    conversions::conversion_manager conv_manager = get_conversion_manager();
    conv_manager.add_custom_conversion<T>(from_func, to_func);
}

} // namespace jai

#endif // __JAISCRIPT_CORE_ENGINE_IMPL_HPP__