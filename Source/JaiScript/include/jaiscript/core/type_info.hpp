#pragma once

#include "types.hpp"
#include "../jaiscript_fwd.hpp"
#include <vector>
#include <memory>
#include <type_traits>
#include <typeinfo>

namespace jai {

    // Forward declaration
    class type_info;
    using type_info_ptr = std::shared_ptr<type_info>;
    
    // Represents complete type information including generic parameters
    class type_info {
    public:
        value_type base_type;
        std::string type_name;  // For Object<T>, this would be the class name
        
        // Generic type parameters
        std::vector<type_info_ptr> type_params;
        
        // Native type information for serialization
        size_t native_size = 0;     // Size in bytes of the native C++ type
        bool is_signed = true;      // For integral types
        
        // Constructors
        type_info(value_type type) : base_type(type) {}
        type_info(value_type type, const std::string& name) : base_type(type), type_name(name) {}
        
        // Factory methods for common types
        static type_info_ptr make_int() { 
            auto info = std::make_shared<type_info>(value_type::jai_int_type);
            info->type_name = "int";
            info->native_size = sizeof(script_int);
            info->is_signed = true;
            return info;
        }
        static type_info_ptr make_float() { 
            auto info = std::make_shared<type_info>(value_type::jai_float_type);
            info->type_name = "float";
            info->native_size = sizeof(script_float);
            return info;
        }
        static type_info_ptr make_string() { 
            auto info = std::make_shared<type_info>(value_type::jai_string_type);
            info->type_name = "string";
            return info;
        }
        static type_info_ptr make_bool() { 
            auto info = std::make_shared<type_info>(value_type::jai_bool_type);
            info->type_name = "bool";
            return info;
        }
        static type_info_ptr make_char() { 
            auto info = std::make_shared<type_info>(value_type::jai_char_type);
            info->type_name = "char";
            return info;
        }
        
        static type_info_ptr make_void() {
            auto info = std::make_shared<type_info>(value_type::jai_null_type);
            info->type_name = "void";
            return info;
        }
        
        // Factory methods for specific native types
        static type_info_ptr make_int8() {
            auto info = make_int();
            info->type_name = "int8";
            info->native_size = 1;
            return info;
        }
        
        static type_info_ptr make_int16() {
            auto info = make_int();
            info->type_name = "int16";
            info->native_size = 2;
            return info;
        }
        
        static type_info_ptr make_int32() {
            auto info = make_int();
            info->type_name = "int32";
            info->native_size = 4;
            return info;
        }
        
        static type_info_ptr make_uint8() {
            auto info = make_int();
            info->type_name = "uint8";
            info->native_size = 1;
            info->is_signed = false;
            return info;
        }
        
        static type_info_ptr make_uint16() {
            auto info = make_int();
            info->type_name = "uint16";
            info->native_size = 2;
            info->is_signed = false;
            return info;
        }
        
        static type_info_ptr make_uint32() {
            auto info = make_int();
            info->type_name = "uint32";
            info->native_size = 4;
            info->is_signed = false;
            return info;
        }
        
        static type_info_ptr make_uint64() {
            auto info = make_int();
            info->type_name = "uint64";
            info->native_size = 8;
            info->is_signed = false;
            return info;
        }
        
        static type_info_ptr make_float32() {
            auto info = make_float();
            info->type_name = "float32";
            info->native_size = 4;
            return info;
        }
        
        // Array<T>
        static type_info_ptr make_array(type_info_ptr element_type) {
            auto info = std::make_shared<type_info>(value_type::jai_array_type);
            info->type_name = "array";
            info->type_params.push_back(element_type);
            return info;
        }
        
        // Map<K,V>
        static type_info_ptr make_map(type_info_ptr keyType, type_info_ptr valueType) {
            auto info = std::make_shared<type_info>(value_type::jai_map_type);
            info->type_name = "map";
            info->type_params.push_back(keyType);
            info->type_params.push_back(valueType);
            return info;
        }
        
        // Object<T>
        static type_info_ptr make_object(const std::string& className) {
            return std::make_shared<type_info>(value_type::jai_object_type, className);
        }
        
        // shared_ptr<T>
        static type_info_ptr make_shared_ptr(type_info_ptr pointee_type) {
            auto info = std::make_shared<type_info>(value_type::jai_shared_ptr_type);
            info->type_params.push_back(pointee_type);
            return info;
        }
        
        // weak_ptr<T>
        static type_info_ptr make_weak_ptr(type_info_ptr pointee_type) {
            auto info = std::make_shared<type_info>(value_type::jai_weak_ptr_type);
            info->type_params.push_back(pointee_type);
            return info;
        }
        
        // T&
        static type_info_ptr make_reference(type_info_ptr referenced_type) {
            auto info = std::make_shared<type_info>(value_type::jai_reference_type);
            info->type_params.push_back(referenced_type);
            return info;
        }
        
        // Function<ReturnType(Args...)>
        static type_info_ptr make_function(type_info_ptr return_type, std::vector<type_info_ptr> arg_types) {
            auto info = std::make_shared<type_info>(value_type::jai_function_type);
            info->type_params.push_back(return_type);
            info->type_params.insert(info->type_params.end(), arg_types.begin(), arg_types.end());
            return info;
        }
        
        // Template factory method for C++ types
        template<typename T>
        static type_info_ptr make() {
            if constexpr (std::is_same_v<T, script_int>) {
                return make_int();
            } else if constexpr (std::is_same_v<T, script_float>) {
                return make_float();
            } else if constexpr (std::is_same_v<T, script_string>) {
                return make_string();
            } else if constexpr (std::is_same_v<T, script_bool>) {
                return make_bool();
            } else if constexpr (std::is_same_v<T, script_char>) {
                return make_char();
            } else if constexpr (std::is_same_v<T, void>) {
                return make_void();
            } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                // All integral types (int, long, etc.) map to script_int
                auto info = make_int();
                info->native_size = sizeof(T);
                info->is_signed = std::is_signed_v<T>;
                return info;
            } else if constexpr (std::is_floating_point_v<T>) {
                // All floating point types (float, double) map to script_float
                auto info = make_float();
                info->native_size = sizeof(T);
                return info;
            } else {
                // For custom types, create an object type with the type name
                return make_object(typeid(T).name());
            }
        }
        
        // Type checking
        bool is_array() const { return base_type == value_type::jai_array_type; }
        bool is_map() const { return base_type == value_type::jai_map_type; }
        bool is_object() const { return base_type == value_type::jai_object_type; }
        bool is_function() const { return base_type == value_type::jai_function_type; }
        bool is_reference() const { return base_type == value_type::jai_reference_type; }
        bool is_shared_ptr() const { return base_type == value_type::jai_shared_ptr_type; }
        bool is_weak_ptr() const { return base_type == value_type::jai_weak_ptr_type; }
        
        // Get type parameters
        type_info_ptr get_element_type() const {  // For Array<T>, shared_ptr<T>, etc.
            return type_params.empty() ? nullptr : type_params[0];
        }
        
        type_info_ptr get_key_type() const {  // For Map<K,V>
            return type_params.empty() ? nullptr : type_params[0];
        }
        
        type_info_ptr get_value_type() const {  // For Map<K,V>
            return type_params.size() < 2 ? nullptr : type_params[1];
        }
        
        type_info_ptr get_return_type() const {  // For Function
            return type_params.empty() ? nullptr : type_params[0];
        }
        
        std::vector<type_info_ptr> get_arg_types() const {  // For Function
            if (type_params.size() <= 1) return {};
            return std::vector<type_info_ptr>(type_params.begin() + 1, type_params.end());
        }
        
        // script_string representation for debugging
        std::string to_string() const;
        
        // Equality comparison
        bool equals(const type_info& other) const;
    };
    
    // Example usage in JaiScript syntax:
    // int x = 5;                           // type_info::make_int()
    // array<int> nums = {1, 2, 3};         // type_info::make_array(make_int())
    // map<string, int> scores;             // type_info::make_map(make_string(), make_int())
    // shared_ptr<Creature> creature;        // type_info::make_shared_ptr(make_object("Creature"))
    // Function<int(float, float)> add;     // type_info::make_function(make_int(), {make_float(), make_float()})
    
} // namespace jai