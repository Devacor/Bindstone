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

    // Construct entries in place: operator[] would require V to be default-constructible AND
    // copy-assignable, and the assignability trait lies for types holding vector<unique_ptr<...>>
    // members (hard error inside vector::operator=). Copy construction is all we require.
    auto registry = eng ? get_engine_conversion_registry(eng) : nullptr;
    auto convertedKey = [&](const script_value& k) -> K {
        if constexpr (std::is_same_v<K, script_value>) {
            return k;
        } else {
            if (registry && registry->template has_conversion<K>()) {
                return registry->template convert_from_script<K>(k);
            }
            return k.as<K>();
        }
    };
    auto convertedValue = [&](const script_value& val) -> V {
        if constexpr (std::is_same_v<V, script_value>) {
            return val;
        } else {
            if (registry && registry->template has_conversion<V>()) {
                return registry->template convert_from_script<V>(val);
            }
            return val.as<V>();
        }
    };
    for (const auto& [k, val] : m) {
        K key = convertedKey(k);
        result.erase(key); // last-wins if two script keys convert to the same K
        result.emplace(std::move(key), convertedValue(val));
    }
    return result;
}

// Engine-aware version that uses the conversion registry for custom types
// Implementation moved to engine_impl.hpp to avoid circular dependencies
template<typename K, typename V>
script_value convert_stdmap_to_script_map(const std::map<K, V>& stdmap, engine* eng);

// Implementation of conversion_manager methods moved to engine_impl.hpp
// to avoid circular dependencies

} // namespace conversions
} // namespace jai