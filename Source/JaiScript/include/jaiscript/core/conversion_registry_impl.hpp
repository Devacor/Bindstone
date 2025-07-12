#pragma once

#include "conversion_registry.hpp"
#include "value.hpp"

namespace jai {
    // Forward declaration
    class engine;
    
    // Helper function for engine conversion registry access
    // Must be defined after engine.hpp is included
    std::shared_ptr<conversions::conversion_registry> get_engine_conversion_registry(engine* eng);

namespace conversions {

// Implementation of template functions that require full script_value definition

template<typename T>
std::vector<T> convert_script_array_to_vector(const script_value& v, engine* eng) {
    if (v.type() != script_value_type::jai_array_type) {
        throw runtime_error("Cannot convert non-array to vector");
    }
    
    std::vector<T> result;
    const auto& arr = v.as_array();
    result.reserve(arr.size());
    
    for (const auto& elem : arr) {
        if constexpr (std::is_same_v<T, script_value>) {
            result.push_back(elem);
        } else {
            // For custom types, try conversion registry first, then fallback to as<T>()
            // This allows proper custom type conversion while avoiding infinite recursion
            if (eng) {
                auto registry = get_engine_conversion_registry(eng);
                if (registry && registry->template has_conversion<T>()) {
                    try {
                        result.push_back(registry->template convert_from_script<T>(elem));
                        continue;
                    } catch (...) {
                        // Fall through to direct conversion on any error
                    }
                }
            }
            // Fallback to direct conversion
            result.push_back(elem.as<T>());
        }
    }
    return result;
}

// This template function should not be used directly - use the engine-aware version instead
// Keeping it only for backward compatibility
template<typename T>
[[deprecated("Use engine-aware version convert_vector_to_script_array(vec, engine) instead")]]
script_value convert_vector_to_script_array(const std::vector<T>& vec) {
    throw std::runtime_error("Non-engine-aware vector conversion is no longer supported. Use convert_vector_to_script_array(vec, engine) instead.");
}

// Engine-aware version that uses the conversion registry for custom types
// Implementation moved to engine_impl.hpp to avoid circular dependencies
template<typename T>
script_value convert_vector_to_script_array(const std::vector<T>& vec, engine* eng);

template<typename K, typename V>
std::map<K, V> convert_script_map_to_stdmap(const script_value& v, engine* eng) {
    if (v.type() != script_value_type::jai_map_type) {
        throw runtime_error("Cannot convert non-map to std::map");
    }
    
    std::map<K, V> result;
    const auto& m = v.as_map();
    
    for (const auto& [k, val] : m) {
        if constexpr (std::is_same_v<K, script_value> && std::is_same_v<V, script_value>) {
            result[k] = val;
        } else if constexpr (std::is_same_v<K, script_value>) {
            // Try engine-aware conversion for V
            if (eng) {
                auto registry = get_engine_conversion_registry(eng);
                if (registry && registry->template has_conversion<V>()) {
                    result[k] = registry->template convert_from_script<V>(val);
                } else {
                    result[k] = val.as<V>();
                }
            } else {
                result[k] = val.as<V>();
            }
        } else if constexpr (std::is_same_v<V, script_value>) {
            result[k.as<K>()] = val;
        } else {
            // Try engine-aware conversion for both K and V
            K converted_key;
            V converted_value;
            
            if (eng) {
                auto registry = get_engine_conversion_registry(eng);
                
                // Convert key
                if (registry && registry->template has_conversion<K>()) {
                    converted_key = registry->template convert_from_script<K>(k);
                } else {
                    converted_key = k.as<K>();
                }
                
                // Convert value
                if (registry && registry->template has_conversion<V>()) {
                    converted_value = registry->template convert_from_script<V>(val);
                } else {
                    converted_value = val.as<V>();
                }
            } else {
                converted_key = k.as<K>();
                converted_value = val.as<V>();
            }
            
            result[converted_key] = converted_value;
        }
    }
    return result;
}

// This template function should not be used directly - use the engine-aware version instead
// Keeping it only for backward compatibility
template<typename K, typename V>
[[deprecated("Use engine-aware version convert_stdmap_to_script_map(map, engine) instead")]]
script_value convert_stdmap_to_script_map(const std::map<K, V>& stdmap) {
    throw std::runtime_error("Non-engine-aware map conversion is no longer supported. Use convert_stdmap_to_script_map(map, engine) instead.");
}

// Engine-aware version that uses the conversion registry for custom types
// Implementation moved to engine_impl.hpp to avoid circular dependencies
template<typename K, typename V>
script_value convert_stdmap_to_script_map(const std::map<K, V>& stdmap, engine* eng);

// Implementation of conversion_manager methods moved to engine_impl.hpp
// to avoid circular dependencies

} // namespace conversions
} // namespace jai