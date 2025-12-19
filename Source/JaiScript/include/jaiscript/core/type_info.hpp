#pragma once

#ifndef __JAISCRIPT_CORE_TYPE_INFO_HPP__
#define __JAISCRIPT_CORE_TYPE_INFO_HPP__

#include "types.hpp"
#include <jaiscript/jaiscript_fwd.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <vector>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <string_view>

namespace jai {

    // Forward declarations
    class type_info;

    // Wrapper for type_info* to enable operator overloading and prevent pointer comparison bugs
    struct type_info_ptr {
        type_info* ptr = nullptr;

        type_info_ptr() = default;
        type_info_ptr(type_info* p) : ptr(p) {}
        type_info_ptr(std::nullptr_t) : ptr(nullptr) {}

        type_info* operator->() const { return ptr; }
        type_info& operator*() const { return *ptr; }
        operator type_info*() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }

        // Get the raw pointer
        type_info* get() const { return ptr; }
    };

    // Represents complete type information including generic parameters
    class type_info {
    public:
        script_value_type base_type;
        std::string type_name;  // For Object<T>, this would be the class name
        uint64_t id = 0;        // Interned canonical name for O(1) type comparison

        // Generic type parameters
        std::vector<type_info_ptr> type_params;

        // Native type information for serialization
        size_t native_size = 0;     // Size in bytes of the native C++ type
        bool is_signed = true;      // For integral types

        // Constructors
        type_info(script_value_type type) : base_type(type) {}
        type_info(script_value_type type, std::string_view name) : base_type(type), type_name(name) {}

        // Type checking
        bool is_array() const { return base_type == script_value_type::jai_array_type; }
        bool is_map() const { return base_type == script_value_type::jai_map_type; }
        bool is_object() const { return base_type == script_value_type::jai_object_type; }
        bool is_function() const { return base_type == script_value_type::jai_function_type; }
        bool is_reference() const { return base_type == script_value_type::jai_reference_type; }
        bool is_weak_ptr() const { return base_type == script_value_type::jai_weak_ptr_type; }

        // Get type parameters
        type_info_ptr element_type() const {  // For Array<T>, weak_ptr<T>, etc.
            return type_params.empty() ? nullptr : type_params[0];
        }

        type_info_ptr key_type() const {  // For Map<K,V>
            return type_params.empty() ? nullptr : type_params[0];
        }

        type_info_ptr value_type() const {  // For Map<K,V>
            return type_params.size() < 2 ? nullptr : type_params[1];
        }

        type_info_ptr return_type() const {  // For Function
            return type_params.empty() ? nullptr : type_params[0];
        }

        std::vector<type_info_ptr> arg_types() const {  // For Function
            if (type_params.size() <= 1) return {};

            return std::vector<type_info_ptr>(type_params.begin() + 1, type_params.end());
        }

        // script_string representation for debugging
        std::string to_string() const;

        // Canonical name for type interning (unique identifier for this type structure)
        // Examples: "int", "array<int>", "array<>", "map<string,int>", "map<,>"
        std::string canonical_name() const {
            switch (base_type) {
                case script_value_type::jai_int_type: return "int";
                case script_value_type::jai_float_type: return "float";
                case script_value_type::jai_string_type: return "string";
                case script_value_type::jai_bool_type: return "bool";
                case script_value_type::jai_char_type: return "char";
                case script_value_type::jai_null_type: return "void";
                case script_value_type::jai_any_type: return "any";
                case script_value_type::jai_array_type: {
                    std::string elem = element_type() ? element_type()->canonical_name() : "";
                    return "array<" + elem + ">";
                }
                case script_value_type::jai_map_type: {
                    std::string k = key_type() ? key_type()->canonical_name() : "";
                    std::string v = value_type() ? value_type()->canonical_name() : "";
                    return "map<" + k + "," + v + ">";
                }
                case script_value_type::jai_object_type:
                case script_value_type::jai_shared_ptr_type:
                    return type_name;  // Use registered class name
                case script_value_type::jai_weak_ptr_type: {
                    std::string elem = element_type() ? element_type()->canonical_name() : "";
                    return "weak_ptr<" + elem + ">";
                }
                case script_value_type::jai_function_type: {
                    std::string ret = return_type() ? return_type()->canonical_name() : "void";
                    std::string args_str = "(";
                    auto args = arg_types();
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (i > 0) args_str += ",";
                        args_str += args[i] ? args[i]->canonical_name() : "";
                    }
                    args_str += ")";
                    return "function<" + ret + args_str + ">";
                }
                default:
                    return type_name.empty() ? "unknown" : type_name;
            }
        }

        // Equality comparison
        bool equals(const type_info& other) const;

    private:
        friend class engine;  // Engine is responsible for creating and interning type_info objects
        friend class parser;  // Parser needs to create type_info during parsing
        friend class class_definition;  // Class definitions need to create type_info for their types

        // Factory methods for common types - private, only accessible by engine/parser/class_definition
        // These create type_info objects with interned canonical names
        static type_info make_int(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_int_type);
            info.type_name = "int";
            info.native_size = sizeof(script_int);
            info.is_signed = true;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_float(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_float_type);
            info.type_name = "float";
            info.native_size = sizeof(script_float);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_string(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_string_type);
            info.type_name = "string";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_bool(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_bool_type);
            info.type_name = "bool";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_char(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_char_type);
            info.type_name = "char";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_void(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_null_type);
            info.type_name = "void";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // any type (var keyword) - dynamic typing, any value allowed
        static type_info make_any(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_any_type);
            info.type_name = "any";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_invalid(string_symbolizer& symbolizer) {
            type_info info(script_value_type::jai_invalid_type);
            info.type_name = "invalid";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Factory methods for specific native types
        static type_info make_int8(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "int8";
            info.native_size = 1;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_int16(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "int16";
            info.native_size = 2;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_int32(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "int32";
            info.native_size = 4;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_uint8(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "uint8";
            info.native_size = 1;
            info.is_signed = false;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_uint16(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "uint16";
            info.native_size = 2;
            info.is_signed = false;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_uint32(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "uint32";
            info.native_size = 4;
            info.is_signed = false;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_uint64(string_symbolizer& symbolizer) {
            auto info = make_int(symbolizer);
            info.type_name = "uint64";
            info.native_size = 8;
            info.is_signed = false;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        static type_info make_float32(string_symbolizer& symbolizer) {
            auto info = make_float(symbolizer);
            info.type_name = "float32";
            info.native_size = 4;
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Array<T>
        static type_info make_array(string_symbolizer& symbolizer, type_info_ptr element_type) {
            type_info info(script_value_type::jai_array_type);
            info.type_name = "array";
            info.type_params.push_back(element_type);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Map<K,V>
        static type_info make_map(string_symbolizer& symbolizer, type_info_ptr keyType, type_info_ptr valueType) {
            type_info info(script_value_type::jai_map_type);
            info.type_name = "map";
            info.type_params.push_back(keyType);
            info.type_params.push_back(valueType);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Object<T>
        static type_info make_object(string_symbolizer& symbolizer, std::string_view className) {
            type_info info(script_value_type::jai_object_type, className);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // weak_ptr<T>
        static type_info make_weak_ptr(string_symbolizer& symbolizer, type_info_ptr pointee_type) {
            type_info info(script_value_type::jai_weak_ptr_type);
            info.type_params.push_back(pointee_type);
            info.type_name = "weak_ptr";  // Keep generic name for lookups
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // shared_ptr<T>
        static type_info make_shared_ptr(string_symbolizer& symbolizer, type_info_ptr pointee_type) {
            type_info info(script_value_type::jai_shared_ptr_type);
            info.type_params.push_back(pointee_type);
            // Use inner type's name for canonical name (matches parser behavior)
            info.type_name = pointee_type ? pointee_type->type_name : "shared_ptr";
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // T&
        static type_info make_reference(string_symbolizer& symbolizer, type_info_ptr referenced_type) {
            type_info info(script_value_type::jai_reference_type);
            info.type_params.push_back(referenced_type);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // C++ bound value
        static type_info make_cpp_bound(string_symbolizer& symbolizer, type_info_ptr bound_type) {
            type_info info(script_value_type::jai_cpp_bound_type);
            info.type_params.push_back(bound_type);
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Function<ReturnType(Args...)>
        static type_info make_function(string_symbolizer& symbolizer, type_info_ptr return_type, std::vector<type_info_ptr> arg_types) {
            type_info info(script_value_type::jai_function_type);
            info.type_params.push_back(return_type);
            info.type_params.insert(info.type_params.end(), arg_types.begin(), arg_types.end());
            info.id = symbolizer.intern(info.canonical_name());
            return info;
        }

        // Template factory method for C++ types
        template<typename T>
        static type_info make(string_symbolizer& symbolizer) {
            if constexpr (std::is_same_v<T, script_int>) {
                return make_int(symbolizer);
            } else if constexpr (std::is_same_v<T, script_float>) {
                return make_float(symbolizer);
            } else if constexpr (std::is_same_v<T, script_string>) {
                return make_string(symbolizer);
            } else if constexpr (std::is_same_v<T, script_bool>) {
                return make_bool(symbolizer);
            } else if constexpr (std::is_same_v<T, script_char>) {
                return make_char(symbolizer);
            } else if constexpr (std::is_same_v<T, void>) {
                return make_void(symbolizer);
            } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                // All integral types (int, long, etc.) map to script_int
                auto info = make_int(symbolizer);
                info.native_size = sizeof(T);
                info.is_signed = std::is_signed_v<T>;
                info.id = symbolizer.intern(info.canonical_name());
                return info;
            } else if constexpr (std::is_floating_point_v<T>) {
                // All floating point types (float, double) map to script_float
                auto info = make_float(symbolizer);
                info.native_size = sizeof(T);
                info.id = symbolizer.intern(info.canonical_name());
                return info;
            } else {
                // For custom types, create an object type with the type name
                return make_object(symbolizer, typeid(T).name());
            }
        }
    };

    // Equality operators for type_info_ptr - compares content, not addresses
    inline bool operator==(type_info_ptr lhs, type_info_ptr rhs) {
        if (!lhs.ptr && !rhs.ptr) return true;
        if (!lhs.ptr || !rhs.ptr) return false;
        return lhs.ptr->equals(*rhs.ptr);
    }

    inline bool operator!=(type_info_ptr lhs, type_info_ptr rhs) {
        return !(lhs == rhs);
    }

    // Example usage in JaiScript syntax:
    // int x = 5;                           // type_info::make_int()
    // array<int> nums = {1, 2, 3};         // type_info::make_array(make_int())
    // map<string, int> scores;             // type_info::make_map(make_string(), make_int())
    // weak_ptr<Creature> creature;         // type_info::make_weak_ptr(make_object("Creature"))
    // Function<int(float, float)> add;     // type_info::make_function(make_int(), {make_float(), make_float()})
    
} // namespace jai

#endif // __JAISCRIPT_CORE_TYPE_INFO_HPP__