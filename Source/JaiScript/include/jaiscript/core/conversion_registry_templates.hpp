#pragma once

#include "conversion_registry.hpp"
#include "value.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "engine_impl.hpp"

namespace jai {
namespace conversions {

// Template implementations that require full script_value definition

template<typename T>
inline void conversion_registry::register_conversion(
    std::function<T(const script_value&)> from_func,
    std::function<script_value(const T&)> to_func
) {
    auto type = type_id::of<T>();
    
    // Wrap the typed functions in type-erased versions
    from_conversions_[type] = [from_func](const script_value& v) -> void* {
        return new T(from_func(v));
    };
    
    to_conversions_[type] = [to_func](const void* ptr) -> script_value {
        return to_func(*static_cast<const T*>(ptr));
    };
    
    // Register destructor for cleanup
    destructors_[type] = [](void* ptr) {
        delete static_cast<T*>(ptr);
    };
}

template<typename T>
inline bool conversion_registry::has_conversion() const {
    auto type = type_id::of<T>();
    return from_conversions_.find(type) != from_conversions_.end();
}

template<typename T>
inline T conversion_registry::convert_from_script(const script_value& v) const {
    auto type = type_id::of<T>();
    auto it = from_conversions_.find(type);
    if (it != from_conversions_.end()) {
        void* result_ptr = it->second(v);
        T result = *static_cast<T*>(result_ptr);
        
        // Clean up temporary
        auto destructor_it = destructors_.find(type);
        if (destructor_it != destructors_.end()) {
            destructor_it->second(result_ptr);
        }
        
        return result;
    }
    
    // No fallback - caller should handle this
    throw std::runtime_error("No conversion registered in convert_from_script for type: " + std::string(typeid(T).name()));
}

template<typename T>
inline script_value conversion_registry::convert_to_script(const T& value) const {
    auto type = type_id::of<T>();
    auto it = to_conversions_.find(type);
    if (it != to_conversions_.end()) {
        return it->second(&value);
    }
    
    // No conversion registered
    throw std::runtime_error("No conversion registered for type: " + std::string(typeid(T).name()));
}

template<typename T>
inline void conversion_registry::register_vector_conversion() {
    register_conversion<std::vector<T>>(
        [this](const script_value& v) -> std::vector<T> {
            return convert_script_array_to_vector<T>(v);
        },
        [this](const std::vector<T>& vec) -> script_value {
            return convert_vector_to_script_array<T>(vec);
        }
    );
}

template<typename K, typename V>
inline void conversion_registry::register_map_conversion() {
    register_conversion<std::map<K, V>>(
        [this](const script_value& v) -> std::map<K, V> {
            return convert_script_map_to_stdmap<K, V>(v);
        },
        [this](const std::map<K, V>& map) -> script_value {
            return convert_stdmap_to_script_map<K, V>(map);
        }
    );
}

// Helper functions for container conversions
template<typename T>
inline std::vector<T> conversion_registry::convert_script_array_to_vector(const script_value& v) const {
    return convert_script_array_to_vector<T>(v);
}

template<typename T>
inline script_value conversion_registry::convert_vector_to_script_array(const std::vector<T>& vec) const {
    return conversions::convert_vector_to_script_array<T>(vec);
}

template<typename K, typename V>
inline std::map<K, V> conversion_registry::convert_script_map_to_stdmap(const script_value& v) const {
    return convert_script_map_to_stdmap<K, V>(v);
}

template<typename K, typename V>
inline script_value conversion_registry::convert_stdmap_to_script_map(const std::map<K, V>& map) const {
    return convert_stdmap_to_script_map<K, V>(map);
}

// Template implementations for conversion_manager methods

template<typename T>
inline void conversion_manager::add_vector_conversion() {
    registry_->register_conversion<std::vector<T>>(
        [engine = engine_](const script_value& v) -> std::vector<T> {
            return convert_script_array_to_vector<T>(v, engine);
        },
        [engine = engine_](const std::vector<T>& vec) -> script_value {
            return convert_vector_to_script_array(vec, engine);
        }
    );
}

template<typename K, typename V>
inline void conversion_manager::add_map_conversion() {
    registry_->register_conversion<std::map<K, V>>(
        [engine = engine_](const script_value& v) -> std::map<K, V> {
            return convert_script_map_to_stdmap<K, V>(v, engine);
        },
        [engine = engine_](const std::map<K, V>& map) -> script_value {
            return convert_stdmap_to_script_map(map, engine);
        }
    );
}

// Implementations for bound_array and bound_map conversions

template<typename T>
inline void conversion_manager::add_bound_array_conversion() {
    // DO NOT register conversions for bound_array in the conversion registry!
    // The function_binder has specialized handling for bound_array that provides
    // proper zero-copy semantics for const& parameters while ensuring safety.
    // Registering conversions here would override those specializations and
    // cause either performance issues (deep copies) or crashes (dangling references).
    
    // The function_binder handles:
    // - bound_array<T> by value: deep copy via script_value constructor
    // - bound_array<T>&: thread_local wrapper with zero-copy
    // - const bound_array<T>&: thread_local wrapper with zero-copy
}

template<typename K, typename V>
inline void conversion_manager::add_bound_map_conversion() {
    // DO NOT register conversions for bound_map in the conversion registry!
    // The function_binder has specialized handling for bound_map that provides
    // proper zero-copy semantics for const& parameters while ensuring safety.
    // Registering conversions here would override those specializations and
    // cause either performance issues (deep copies) or crashes (dangling references).
    
    // The function_binder handles:
    // - bound_map<K,V> by value: deep copy via script_value constructor
    // - bound_map<K,V>&: thread_local wrapper with zero-copy
    // - const bound_map<K,V>&: thread_local wrapper with zero-copy
}

template<typename T>
inline void conversion_manager::add_custom_conversion(
    std::function<T(const script_value&)> from_func,
    std::function<script_value(const T&)> to_func
) {
    registry_->register_conversion<T>(from_func, to_func);
}

} // namespace conversions
} // namespace jai