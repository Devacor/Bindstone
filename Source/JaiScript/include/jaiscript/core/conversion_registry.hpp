#pragma once

#ifndef __JAISCRIPT_CORE_CONVERSION_REGISTRY_HPP__
#define __JAISCRIPT_CORE_CONVERSION_REGISTRY_HPP__

#include "conversion_registry_fwd.hpp"
#include "types.hpp"
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <algorithm>

namespace jai {

// Forward declaration
class script_value;

// Forward declarations
class engine;

namespace conversions {

// type_id is already defined in conversion_registry_fwd.hpp

// Conversion function signatures
using from_script_func = std::function<void*(const script_value&)>;
using to_script_func = std::function<script_value(const void*)>;

// Registry for custom conversions
class conversion_registry {
private:
    // Raw pointer to the owning engine
    engine* engine_ref_ = nullptr;

public:
    conversion_registry() = default;
    explicit conversion_registry(engine* eng) : engine_ref_(eng) {}
    ~conversion_registry() = default;

    // Non-copyable but moveable
    conversion_registry(const conversion_registry&) = delete;
    conversion_registry& operator=(const conversion_registry&) = delete;
    conversion_registry(conversion_registry&&) = default;
    conversion_registry& operator=(conversion_registry&&) = default;

    // Set engine reference (for registries created before engine)
    void set_engine(engine* eng) { engine_ref_ = eng; }

    // Get engine pointer for conversions
    engine* get_engine() const {
        return engine_ref_;
    }
    
    // === Built-in Type Conversions (from engine's type_conversion_registry) ===
    
    struct BuiltinConversion {
        script_value_type from;
        script_value_type to;
        int cost; // Conversion cost for overload resolution
        std::function<script_value(const script_value&)> converter;
    };
    
    // Register a conversion between JaiScript built-in types
    void register_builtin_conversion(script_value_type from, script_value_type to, int cost, 
                                   std::function<script_value(const script_value&)> converter);
    
    // Check if a built-in conversion exists
    bool can_convert_builtin(script_value_type from, script_value_type to) const;
    
    // Get conversion cost for overload resolution
    int get_builtin_conversion_cost(script_value_type from, script_value_type to) const;
    
    // Perform built-in type conversion
    script_value convert_builtin(const script_value& value, script_value_type targetType) const;
    
    // === Custom Type Conversions ===
    
    // Register a custom conversion for type T
    template<typename T>
    void register_conversion(
        std::function<T(const script_value&)> from_func,
        std::function<script_value(const T&)> to_func
    );
    
    // Check if conversion exists for type T
    template<typename T>
    bool has_conversion() const;
    
    // Convert from script_value to T using registered conversion
    template<typename T>
    T convert_from_script(const script_value& v) const;
    
    // Convert from T to script_value using registered conversion
    template<typename T>
    script_value convert_to_script(const T& value) const;
    
    // Try to convert using custom conversions (type-erased version for internal use)
    bool try_custom_conversion(const script_value& v, const std::type_info& target_type, script_value& out) const;
    
    // === Container Conversions ===
    
    // Register standard vector conversion
    template<typename T>
    void register_vector_conversion();
    
    // Register standard map conversion
    template<typename K, typename V>
    void register_map_conversion();
    
    // Standard library conversions
    void register_standard_conversions();
    
    // === C++ Type Converters (for void* to script_value conversions) ===
    
    // Register a C++ type converter (type_id -> converter function)
    void register_cpp_type_converter(type_id tid, 
                                    std::function<script_value(const void*)> converter);
    
    // Get a C++ type converter by type_id
    std::function<script_value(const void*)> get_cpp_type_converter(type_id tid) const;
    
    // Convert from void* using registered C++ type converter
    script_value convert_cpp_type_from_void(type_id tid, const void* obj) const;
    
    // === Custom Object Extractor (replaces static extractor in script_value) ===
    
    // Type for custom extractor function
    using extractor_func = std::function<std::shared_ptr<void>(const std::string&, std::shared_ptr<void>)>;
    
    // Set custom extractor for unwrapping objects (e.g., extracting C++ objects from class_instance)
    void set_custom_extractor(extractor_func extractor) {
        custom_extractor_ = std::move(extractor);
    }
    
    // Get custom extractor
    const extractor_func& get_custom_extractor() const {
        return custom_extractor_;
    }
    
    // Extract custom object
    std::shared_ptr<void> extract_custom_object(const std::string& type_name, std::shared_ptr<void> data) const {
        if (custom_extractor_) {
            return custom_extractor_(type_name, data);
        }
        return nullptr;
    }
    
private:
    // Helper functions for container conversions
    template<typename T>
    std::vector<T> convert_script_array_to_vector(const script_value& v) const;
    
    template<typename T>
    script_value convert_vector_to_script_array(const std::vector<T>& vec) const;
    
    template<typename K, typename V>
    std::map<K, V> convert_script_map_to_stdmap(const script_value& v) const;
    
    template<typename K, typename V>
    script_value convert_stdmap_to_script_map(const std::map<K, V>& map) const;
    // Built-in type conversions
    std::vector<BuiltinConversion> builtin_conversions_;
    
    // Custom type conversions
    std::map<type_id, from_script_func> from_conversions_;
    std::map<type_id, to_script_func> to_conversions_;
    std::map<type_id, std::function<void(void*)>> destructors_;
    
    // Custom object extractor
    extractor_func custom_extractor_;
    
    // C++ type converters (for void* to script_value conversions)
    std::unordered_map<type_id, std::function<script_value(const void*)>, type_id_hash> cpp_type_converters_;
};

// Forward declarations for helper functions - implementations in conversion_registry_impl.hpp
template<typename T>
std::vector<T> convert_script_array_to_vector(const script_value& v, engine* eng);

template<typename T>
script_value convert_vector_to_script_array(const std::vector<T>& vec);

template<typename K, typename V>
std::map<K, V> convert_script_map_to_stdmap(const script_value& v, engine* eng);

template<typename K, typename V>
script_value convert_stdmap_to_script_map(const std::map<K, V>& stdmap);

} // namespace conversions

// Convenience functions for engine
namespace conversions {

// Add conversion functions to engine
class conversion_manager {
public:
    explicit conversion_manager(std::shared_ptr<conversion_registry> registry, engine* eng) 
        : registry_(std::move(registry)), engine_(eng) {}
    
    // Register standard conversions for common vector types
    template<typename T>
    void add_vector_conversion();
    
    // Register standard conversions for common map types
    template<typename K, typename V>
    void add_map_conversion();
    
    // Register conversions for bound_array types
    template<typename T>
    void add_bound_array_conversion();
    
    // Register conversions for bound_map types
    template<typename K, typename V>
    void add_bound_map_conversion();
    
    // Register custom conversion
    template<typename T>
    void add_custom_conversion(
        std::function<T(const script_value&)> from_func,
        std::function<script_value(const T&)> to_func
    );
    
    // Register built-in type conversion
    void add_builtin_conversion(script_value_type from, script_value_type to, int cost,
                               std::function<script_value(const script_value&)> converter);
    
private:
    std::shared_ptr<conversion_registry> registry_;
    engine* engine_;
};

// Function to register all standard conversions into a registry
void register_all_standard_conversions(std::shared_ptr<conversion_registry> registry);

} // namespace conversions

} // namespace jai

#endif // __JAISCRIPT_CORE_CONVERSION_REGISTRY_HPP__