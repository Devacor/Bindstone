#pragma once

#ifndef __JAISCRIPT_CORE_VALUE_HPP__
#define __JAISCRIPT_CORE_VALUE_HPP__

#include "types.hpp"
#include "type_info.hpp"
#include "conversion_registry.hpp"
#include <jaiscript/jaiscript_fwd.hpp>
#include <variant>
#include <memory>
#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <cmath>
#include <set>

namespace jai {

    // Forward declaration
    class environment;
    class engine;
    
    // Forward declaration for conversion registry helper
    namespace conversions {
        class conversion_registry;
    }
    
    // Forward declarations for helper functions
    std::shared_ptr<conversions::conversion_registry> get_engine_conversion_registry(engine* eng);
    std::weak_ptr<engine> get_engine_weak_ptr(engine* eng);  // Helper to get weak_ptr from engine
    
    // Forward declaration for friend access
    namespace detail {
        template<typename T> struct value_converter;
    }
    
    // Forward declarations for friend classes
    namespace serialization {
        class binary_archive_writer;
        class binary_archive_reader;
        class json_archive_writer;
        class json_archive_reader;
    }

    // Helper trait to detect shared_ptr specializations
    template<typename T, template<typename...> class Template>
    struct is_specialization : std::false_type {};
    
    template<template<typename...> class Template, typename... Args>
    struct is_specialization<Template<Args...>, Template> : std::true_type {};
    
    template<typename T, template<typename...> class Template>
    inline constexpr bool is_specialization_v = is_specialization<T, Template>::value;

    class script_value {
    public:
        script_value(std::nullptr_t) = delete;
        
        // Friend std::map and std::unordered_map so they can use the private default constructor for operator[]
        template<typename K, typename V, typename C, typename A>
        friend class std::map;
        template<typename K, typename V, typename H, typename E, typename A>
        friend class std::unordered_map;
        
        // Friend std::pair and std::tuple for map's internal piecewise construction
        template<typename T1, typename T2>
        friend struct std::pair;
        template<typename... Types>
        friend class std::tuple;
        
    private:
        // Private default constructor - ONLY for std::map operator[] compatibility
        // This creates an invalid script_value that MUST be immediately assigned to
        script_value() : type_info_(nullptr), storage_(std::monostate{}) {}
        
    public:
        
        // ALL constructors require engine context - no exceptions!
        // Special constructor for null/void values
        explicit script_value(std::monostate, std::weak_ptr<engine> eng) : type_info_(nullptr), engine_ref_(eng), storage_(std::monostate{}) {}
        
        // AST literal constructor - ONLY for parser to create placeholder values in AST nodes
        // These values should NEVER be used directly, only as templates for interpreter
        struct ast_literal_tag {};
        script_value(ast_literal_tag, script_int i) : type_info_(type_info::make_int()), storage_(i) {}
        script_value(ast_literal_tag, script_float f) : type_info_(type_info::make_float()), storage_(f) {}
        script_value(ast_literal_tag, const script_string& s) : type_info_(type_info::make_string()), storage_(s) {}
        script_value(ast_literal_tag, script_char c) : type_info_(type_info::make_char()), storage_(c) {}
        script_value(ast_literal_tag, script_bool b) : type_info_(type_info::make_bool()), storage_(b) {}
        script_value(ast_literal_tag, std::monostate) : type_info_(nullptr), storage_(std::monostate{}) {}
        script_value(script_int i, std::weak_ptr<engine> eng) : type_info_(type_info::make_int()), engine_ref_(eng), storage_(i) {}
        script_value(script_float f, std::weak_ptr<engine> eng) : type_info_(type_info::make_float()), engine_ref_(eng), storage_(f) {}
        script_value(const script_string& s, std::weak_ptr<engine> eng) : type_info_(type_info::make_string()), engine_ref_(eng), storage_(s) {}
        script_value(script_string&& s, std::weak_ptr<engine> eng) : type_info_(type_info::make_string()), engine_ref_(eng), storage_(std::move(s)) {}
        script_value(const char* s, std::weak_ptr<engine> eng) : type_info_(type_info::make_string()), engine_ref_(eng), storage_(script_string(s)) {}
        script_value(script_char c, std::weak_ptr<engine> eng) : type_info_(type_info::make_char()), engine_ref_(eng), storage_(c) {}
        script_value(script_bool b, std::weak_ptr<engine> eng) : type_info_(type_info::make_bool()), engine_ref_(eng), storage_(b) {}
        
        // Template constructors for all numeric types - ALL require engine references
        
        // Engine-aware template constructors for integral types (preferred)
        template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
        script_value(T i, std::weak_ptr<engine> eng) : type_info_(type_info::make_int()), engine_ref_(eng), storage_(script_int(i)) {}
        
        // Engine-aware template constructors for floating point types (preferred)
        template<typename T>
        requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
        script_value(T f, std::weak_ptr<engine> eng) : type_info_(type_info::make_float()), engine_ref_(eng), storage_(script_float(f)) {}

        // Delete pointer constructors - use add_global_ref instead
        template<typename T>
        script_value(T* ptr, std::weak_ptr<engine> eng) = delete;

        script_value(script_value&& other) noexcept 
            : type_info_(std::move(other.type_info_)), 
              engine_ref_(std::move(other.engine_ref_)), 
              storage_(std::move(other.storage_)),
              cpp_bound_ptr_(other.cpp_bound_ptr_) {
            other.type_info_ = nullptr;
            other.engine_ref_.reset();
            other.storage_ = std::monostate{};
            other.cpp_bound_ptr_ = nullptr;
        }
        
        script_value& operator=(script_value&& other) noexcept {
            if (this != &other) {
                type_info_ = std::move(other.type_info_);
                engine_ref_ = std::move(other.engine_ref_);
                storage_ = std::move(other.storage_);
                cpp_bound_ptr_ = other.cpp_bound_ptr_;
                other.type_info_ = nullptr;
                other.engine_ref_.reset();
                other.storage_ = std::monostate{};
                other.cpp_bound_ptr_ = nullptr;
            }
            return *this;
        }
        
        // Copy constructor (shallow copy for reference semantics)
        script_value(const script_value& other);
        script_value& operator=(const script_value& other);
        
        // Explicit deep copy method
        script_value clone() const;
        
        // Factory methods for complex types
        static script_value make_array(type_info_ptr element_type);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType);
        static script_value make_object(const std::string& type_name, std::shared_ptr<void> data);
        // Internal factory method for raw C++ objects - use make_object for general use
        static script_value make_cpp_object(const std::string& type_name, std::shared_ptr<void> data);
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env);

        // Engine-aware factory methods (preferred - ALWAYS use these)
        static script_value make_array(type_info_ptr element_type, std::weak_ptr<engine> eng);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType, std::weak_ptr<engine> eng);
        static script_value make_object(const std::string& type_name, std::shared_ptr<void> data, std::weak_ptr<engine> eng);
        // Optimized version with cached type_id
        static script_value make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, std::weak_ptr<engine> eng, bool is_cpp_class = true);
        // Internal factory method for raw C++ objects - use make_object for general use
        static script_value make_cpp_object(const std::string& type_name, std::shared_ptr<void> data, std::weak_ptr<engine> eng);
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env, std::weak_ptr<engine> eng);
        static script_value make_function(const script_function& func, std::weak_ptr<engine> eng);
        
        // Factory method for C++ bound values
        template<typename T>
        static script_value make_cpp_bound(T* target, std::weak_ptr<engine> eng) {
            script_value val(std::monostate{}, eng);

            // Map C++ types to script types
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                // Integer types (int, long, int64_t, etc.) -> script_int
                val.type_info_ = type_info::make<script_int>();
                val.storage_ = script_int{0};
            } else if constexpr (std::is_floating_point_v<T>) {
                // Floating point types (float, double) -> script_float
                val.type_info_ = type_info::make<script_float>();
                val.storage_ = script_float{0.0};
            } else if constexpr (std::is_same_v<T, bool>) {
                // bool -> script_bool
                val.type_info_ = type_info::make<script_bool>();
                val.storage_ = script_bool{false};
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, script_string>) {
                // string types -> script_string
                val.type_info_ = type_info::make<script_string>();
                val.storage_ = script_string{};
            } else if constexpr (std::is_same_v<T, char>) {
                // char -> script_char
                val.type_info_ = type_info::make<script_char>();
                val.storage_ = script_char{'\0'};
            } else {
                // For complex types, we'll need special handling
                val.type_info_ = type_info::make<T>();
                val.storage_ = std::monostate{};
            }

            val.cpp_bound_ptr_ = static_cast<void*>(target);
            return val;
        }
        
        // Template factory methods removed - use engine->make_object instead
        // This ensures proper type name registration
        
    private:
        template<typename T>
        static script_value make_cpp_object(std::shared_ptr<T> data) {
            return make_cpp_object(typeid(T).name(), std::static_pointer_cast<void>(data));
        }
    public:
        
        // Engine-aware template factory methods removed - use engine->make_object instead
        // This ensures proper type name registration and avoids circular dependencies
        
    private:
        template<typename T>
        static script_value make_cpp_object(std::shared_ptr<T> data, std::weak_ptr<engine> eng) {
            return make_cpp_object(typeid(T).name(), std::static_pointer_cast<void>(data), eng);
        }
    public:
        
        // Engine-aware object creation through registered class system
        template<typename T, typename... Args>
        static script_value make_registered_object(class engine* eng, Args&&... args);
        
        // Type information
        type_info_ptr get_type_info() const { return type_info_; }
        script_value_type type() const { 
            return type_info_ ? type_info_->base_type : script_value_type::jai_null_type; 
        }
        bool is_null() const { return deref().type() == script_value_type::jai_null_type; }
        bool is_invalid() const { return deref().type() == script_value_type::jai_invalid_type; }
        bool is_int() const { return deref().type() == script_value_type::jai_int_type; }
        bool is_float() const { return deref().type() == script_value_type::jai_float_type; }
        bool is_string() const { return deref().type() == script_value_type::jai_string_type; }
        bool is_char() const { return deref().type() == script_value_type::jai_char_type; }
        bool is_bool() const { return deref().type() == script_value_type::jai_bool_type; }
        bool is_array() const { return deref().type() == script_value_type::jai_array_type; }
        bool is_map() const { return deref().type() == script_value_type::jai_map_type; }
        bool is_object() const {
            auto t = deref().type();
            return t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type;
        }
        bool is_function() const { return deref().type() == script_value_type::jai_function_type; }
        bool is_reference() const { return type() == script_value_type::jai_reference_type; }  // Don't deref for this check!
        bool is_cpp_bound() const { return cpp_bound_ptr_ != nullptr; }
        bool is_weak_ptr() const { return deref().type() == script_value_type::jai_weak_ptr_type; }

        // Type marker helpers - check if value has shared_ptr<T> or weak_ptr<T> type info
        bool is_shared_ptr_type() const {
            return type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type;
        }

        bool is_weak_ptr_type() const {
            return type_info_ && type_info_->base_type == script_value_type::jai_weak_ptr_type;
        }

        // Check if this type has reference semantics (shared ownership by default)
        bool has_reference_semantics() const {
            const auto t = deref().type();
            return t == script_value_type::jai_object_type ||
                   t == script_value_type::jai_shared_ptr_type ||
                   t == script_value_type::jai_array_type ||
                   t == script_value_type::jai_map_type ||
                   t == script_value_type::jai_function_type;
        }

        // script_value extraction (inlined for performance)
        inline script_int as_int() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                if (val.cpp_bound_ptr_) {
                    // cpp_bound_ptr might point to various integer types, not just script_int
                    // For now, we assume it's the same size or we handle common cases
                    // TODO: Store type metadata with cpp_bound values for proper casting
                    return static_cast<script_int>(*static_cast<const int*>(val.cpp_bound_ptr_));
                }
                return std::get<script_int>(val.storage_);
            } else if (val.type() == script_value_type::jai_float_type) {
                // Float to int conversion with truncation (like C++)
                if (val.cpp_bound_ptr_) {
                    return static_cast<script_int>(*static_cast<const float*>(val.cpp_bound_ptr_));
                }
                return static_cast<script_int>(std::get<script_float>(val.storage_));
            } else {
                throw runtime_error("script_value is not an integer or float");
            }
        }
        
        inline script_float as_float() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_float_type) {
                throw runtime_error("script_value is not a float");
            }
            if (val.cpp_bound_ptr_) {
                return *static_cast<const script_float*>(val.cpp_bound_ptr_);
            }
            return std::get<script_float>(val.storage_);
        }
        
        inline const script_string& as_string() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_string_type) {
                throw runtime_error("script_value is not a string");
            }
            if (val.cpp_bound_ptr_) {
                return *static_cast<const script_string*>(val.cpp_bound_ptr_);
            }
            return std::get<script_string>(val.storage_);
        }
        
        inline script_bool as_bool() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_bool_type) {
                throw runtime_error("script_value is not a boolean");
            }
            if (val.cpp_bound_ptr_) {
                return *static_cast<const script_bool*>(val.cpp_bound_ptr_);
            }
            return std::get<script_bool>(val.storage_);
        }
        
        inline script_char as_char() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_char_type) {
                throw runtime_error("script_value is not a character");
            }
            if (val.cpp_bound_ptr_) {
                return *static_cast<const script_char*>(val.cpp_bound_ptr_);
            }
            return std::get<script_char>(val.storage_);
        }
        
        inline const std::vector<script_value>& as_array() const {
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
        }
        
        inline std::vector<script_value>& as_array() {
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
        }
        
        inline const std::map<script_value, script_value>& as_map() const {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return *std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
        }
        
        inline std::map<script_value, script_value>& as_map() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return *std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
        }

        const script_function& as_function() const;

        // Safe mutable reference accessors for zero-copy parameter binding
        // These encapsulate direct storage access and handle deref() properly
        inline script_int& as_int_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_int_type) {
                throw runtime_error("script_value is not an integer");
            }
            return std::get<script_int>(val.storage_);
        }

        inline script_float& as_float_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_float_type) {
                throw runtime_error("script_value is not a float");
            }
            return std::get<script_float>(val.storage_);
        }

        inline script_bool& as_bool_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_bool_type) {
                throw runtime_error("script_value is not a boolean");
            }
            return std::get<script_bool>(val.storage_);
        }

        inline script_char& as_char_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_char_type) {
                throw runtime_error("script_value is not a character");
            }
            return std::get<script_char>(val.storage_);
        }

        inline script_string& as_string_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_string_type) {
                throw runtime_error("script_value is not a string");
            }
            return std::get<script_string>(val.storage_);
        }
        
        // Generic extraction with type checking
        // HOT PATH OPTIMIZATION: Specialize common types to avoid template overhead
        template<typename T>
        T as() const {
            // FAST PATH: Direct type specializations for hot types (avoids constexpr cascade)
            // These will be resolved at compile-time to direct function calls
            if constexpr (std::is_same_v<T, script_int>) {
                // Inline the deref + variant access to avoid function call overhead
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return static_cast<script_int>(*static_cast<const int*>(val.cpp_bound_ptr_));
                }
                // Direct variant access with single type check
                if (val.type() == script_value_type::jai_int_type) {
                    return std::get<script_int>(val.storage_);
                } else if (val.type() == script_value_type::jai_float_type) {
                    // Float to int conversion
                    return static_cast<script_int>(std::get<script_float>(val.storage_));
                }
                throw runtime_error("script_value is not an integer or float");
            }
            else if constexpr (std::is_same_v<T, script_float> || std::is_same_v<T, double>) {
                // Inline hot path for float access
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return *static_cast<const script_float*>(val.cpp_bound_ptr_);
                }
                // Direct variant access with type conversion
                if (val.type() == script_value_type::jai_float_type) {
                    return std::get<script_float>(val.storage_);
                } else if (val.type() == script_value_type::jai_int_type) {
                    return static_cast<script_float>(std::get<script_int>(val.storage_));
                }
                throw runtime_error("script_value is not a float or integer");
            }
            else if constexpr (std::is_same_v<T, script_bool>) {
                // Inline hot path for bool access
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return *static_cast<const script_bool*>(val.cpp_bound_ptr_);
                }
                if (val.type() == script_value_type::jai_bool_type) {
                    return std::get<script_bool>(val.storage_);
                }
                throw runtime_error("script_value is not a boolean");
            }
            else if constexpr (std::is_same_v<T, script_char>) {
                // Inline hot path for char access
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return *static_cast<const script_char*>(val.cpp_bound_ptr_);
                }
                if (val.type() == script_value_type::jai_char_type) {
                    return std::get<script_char>(val.storage_);
                }
                throw runtime_error("script_value is not a character");
            }
            // MEDIUM PATH: Common integral conversions (inlined to avoid cascading checks)
            else if constexpr (std::is_same_v<T, int>) {
                script_int val = as<script_int>();  // Uses fast path above
                if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
                    throw runtime_error("Integer value out of range for int");
                }
                return static_cast<int>(val);
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                return static_cast<int64_t>(as<script_int>());  // Uses fast path, no bounds check
            }
            else if constexpr (std::is_same_v<T, float>) {
                // Inline float conversion to avoid double dispatch
                const script_value& val = deref();
                if (val.type() == script_value_type::jai_float_type) {
                    if (val.cpp_bound_ptr_) {
                        return static_cast<float>(*static_cast<const script_float*>(val.cpp_bound_ptr_));
                    }
                    return static_cast<float>(std::get<script_float>(val.storage_));
                } else if (val.type() == script_value_type::jai_int_type) {
                    if (val.cpp_bound_ptr_) {
                        return static_cast<float>(*static_cast<const int*>(val.cpp_bound_ptr_));
                    }
                    return static_cast<float>(std::get<script_int>(val.storage_));
                }
                throw runtime_error("Cannot convert script_value to float");
            }
            // SLOWER PATH: Everything else (original implementation)
            // Handle reference types
            else if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;

                // For const references, we can return references to our internal data
                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        if (type() != script_value_type::jai_int_type) {
                            throw runtime_error("script_value is not an integer");
                        }
                        return std::get<script_int>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_float>) {
                        if (type() != script_value_type::jai_float_type) {
                            throw runtime_error("script_value is not a float");
                        }
                        return std::get<script_float>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        if (type() != script_value_type::jai_bool_type) {
                            throw runtime_error("script_value is not a boolean");
                        }
                        return std::get<script_bool>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        if (type() != script_value_type::jai_char_type) {
                            throw runtime_error("script_value is not a character");
                        }
                        return std::get<script_char>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        return as_string();  // as_string() already returns const script_string&
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        return as_array();   // as_array() already returns const std::vector<script_value>&
                    } else if constexpr (std::is_same_v<base_type, std::map<script_value, script_value>>) {
                        return as_map();     // as_map() already returns const std::map<script_value, script_value>&
                    } else {
                        // For user-defined types stored as objects - use the same logic as by-value extraction
                        auto t = type();
                        if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                            // First try to get shared_ptr<base_type> and dereference it
                            try {
                                auto ptr = as<std::shared_ptr<base_type>>();
                                return *ptr;
                            } catch (const std::exception&) {
                                // If shared_ptr extraction fails, this might not be the right type
                                throw runtime_error("Cannot extract const reference to custom type - type mismatch");
                            }
                        }
                        throw runtime_error("Unsupported type for const reference extraction");
                    }
                } else {
                    // For non-const references, we can't return references to our internal data
                    // as it would break const-correctness, so we throw an error
                    throw runtime_error("Cannot extract non-const reference from const script_value");
                }
            }
            else if constexpr (std::is_same_v<T, script_string>) {
                return as_string();
            } else if constexpr (std::is_same_v<T, int8_t>) {
                script_int val = as<script_int>();
                if (val < std::numeric_limits<int8_t>::min() || val > std::numeric_limits<int8_t>::max()) {
                    throw runtime_error("Integer value out of range for int8_t");
                }
                return static_cast<int8_t>(val);
            } else if constexpr (std::is_same_v<T, int16_t>) {
                script_int val = as<script_int>();
                if (val < std::numeric_limits<int16_t>::min() || val > std::numeric_limits<int16_t>::max()) {
                    throw runtime_error("Integer value out of range for int16_t");
                }
                return static_cast<int16_t>(val);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                script_int val = as<script_int>();
                if (val < std::numeric_limits<int32_t>::min() || val > std::numeric_limits<int32_t>::max()) {
                    throw runtime_error("Integer value out of range for int32_t");
                }
                return static_cast<int32_t>(val);
            }
            // Unsigned integer types with bounds checking
            else if constexpr (std::is_same_v<T, uint8_t>) {
                script_int val = as<script_int>();
                if (val < 0 || val > std::numeric_limits<uint8_t>::max()) {
                    throw runtime_error("Integer value out of range for uint8_t (must be 0-255)");
                }
                return static_cast<uint8_t>(val);
            } else if constexpr (std::is_same_v<T, uint16_t>) {
                script_int val = as<script_int>();
                if (val < 0 || val > std::numeric_limits<uint16_t>::max()) {
                    throw runtime_error("Integer value out of range for uint16_t (must be non-negative)");
                }
                return static_cast<uint16_t>(val);
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                script_int val = as<script_int>();
                if (val < 0 || val > std::numeric_limits<uint32_t>::max()) {
                    throw runtime_error("Integer value out of range for uint32_t (must be non-negative)");
                }
                return static_cast<uint32_t>(val);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                script_int val = as<script_int>();
                if (val < 0) {
                    throw runtime_error("Integer value must be non-negative for uint64_t");
                }
                return static_cast<uint64_t>(val);
            } else if constexpr (std::is_same_v<T, size_t>) {
                script_int val = as<script_int>();
                if (val < 0) {
                    throw runtime_error("Integer value must be non-negative for size_t");
                }
                return static_cast<size_t>(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return as_string();  // script_string is already std::string
            }
            // Check custom converter FIRST for vector types
            else if constexpr (is_specialization_v<T, std::vector>) {
                // Use engine's conversion registry first
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        return registry->template convert_from_script<T>(*this);
                    }
                }
                
                // Built-in vector handling
                if (type() != script_value_type::jai_array_type) {
                    throw runtime_error("Cannot convert non-array to vector");
                }
                
                using element_type = typename T::value_type;
                T result;
                const auto& arr = as_array();
                result.reserve(arr.size());
                
                for (const auto& elem : arr) {
                    if constexpr (std::is_same_v<element_type, script_value>) {
                        result.push_back(elem);
                    } else {
                        result.push_back(elem.as<element_type>());
                    }
                }
                return result;
            }
            // Check custom converter FIRST for map types
            else if constexpr (is_specialization_v<T, std::map>) {
                // Use engine's conversion registry first
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        return registry->template convert_from_script<T>(*this);
                    }
                }
                
                // Built-in map handling
                if (type() != script_value_type::jai_map_type) {
                    throw runtime_error("Cannot convert non-map to std::map");
                }
                
                using key_type = typename T::key_type;
                using mapped_type = typename T::mapped_type;
                T result;
                const auto& m = as_map();
                
                for (const auto& [k, v] : m) {
                    if constexpr (std::is_same_v<key_type, script_value> && std::is_same_v<mapped_type, script_value>) {
                        result.emplace(k, v);
                    } else if constexpr (std::is_same_v<key_type, script_value>) {
                        result.emplace(k, v.as<mapped_type>());
                    } else if constexpr (std::is_same_v<mapped_type, script_value>) {
                        result.emplace(k.as<key_type>(), v);
                    } else {
                        result.emplace(k.as<key_type>(), v.as<mapped_type>());
                    }
                }
                return result;
            }
            // Support for shared_ptr extraction from objects
            else if constexpr (std::is_same_v<T, std::shared_ptr<jai::class_instance>>) {
                auto t = type();
                if (t != script_value_type::jai_object_type && t != script_value_type::jai_shared_ptr_type) {
                    throw runtime_error("script_value is not an object (type=" + std::to_string(static_cast<int>(type())) + ")");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                if (!objHolder->is_cpp_class_instance) {
                    throw runtime_error("Object is not a class_instance (type_name=" + objHolder->type_name + ")");
                }
                return std::static_pointer_cast<class_instance>(objHolder->data);
            }
            // Check custom converter for other class types
            else if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string>) {
                // Special handling for shared_ptr types that aren't class_instance
                if constexpr (is_specialization_v<T, std::shared_ptr>) {
                    // For shared_ptr types, try to extract as object
                    auto t = type();
                    if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                        auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                        // Try to cast the void* to the requested type
                        using element_type = typename T::element_type;
                        // If this is a C++ object stored in the holder, return it
                        if (!objHolder->is_cpp_class_instance) {
                            return std::static_pointer_cast<element_type>(objHolder->data);
                        } else {
                            // This is a class_instance wrapper
                            // We can't directly access class_instance methods here due to circular dependencies
                            // Instead, use the custom extractor if available
                            if (auto eng = engine_ref_.lock()) {
                                auto registry = get_engine_conversion_registry(eng.get());
                                if (registry) {
                                    auto extracted = registry->extract_custom_object(objHolder->type_name, objHolder->data);
                                    if (extracted) {
                                        return std::static_pointer_cast<element_type>(extracted);
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Use engine's conversion registry
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        return registry->template convert_from_script<T>(*this);
                    }
                }
                // No conversion available
                throw runtime_error("No conversion available for type " + std::string(typeid(T).name()));
            }
            // Support for shared_ptr<void> extraction
            else if constexpr (std::is_same_v<T, std::shared_ptr<void>>) {
                if (type() != script_value_type::jai_object_type) {
                    throw runtime_error("script_value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                return objHolder->data;
            }
            // Support for shared_ptr<user_type> extraction from objects
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                // First check if there's a registered conversion for this shared_ptr type
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        return registry->template convert_from_script<T>(*this);
                    }
                }
                
                // Fall back to default shared_ptr extraction
                if (type() != script_value_type::jai_object_type) {
                    throw runtime_error("script_value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                
                // Check if we need to use the custom extractor (for class_instance wrapping)
                // Only use extractor if this is a class_instance wrapper
                if (objHolder->is_cpp_class_instance) {
                    if (auto eng = engine_ref_.lock()) {
                        auto registry = get_engine_conversion_registry(eng.get());
                        if (registry) {
                            auto extracted = registry->extract_custom_object(objHolder->type_name, objHolder->data);
                            if (extracted) {
                                return std::static_pointer_cast<typename T::element_type>(extracted);
                            }
                        }
                    }
                }
                
                // Otherwise use static cast (for objects created directly)
                return std::static_pointer_cast<typename T::element_type>(objHolder->data);
            }
            // Support for extracting custom objects by value (dereference shared_ptr)
            else if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                // Custom converter already checked above

                // For custom classes, try to extract shared_ptr and dereference
                auto t = type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                    auto ptr = as<std::shared_ptr<T>>();
                    return *ptr;
                }
                throw runtime_error("Cannot extract custom type by value from non-object");
            }
            else {
                throw runtime_error("Unsupported type conversion");
            }
            
            // This should be unreachable, but some compilers need it
            throw runtime_error("Internal error: fell through all type conversions");
        }
        
        // Non-const version of as() for extracting non-const references
        template<typename T>
        T as() {
            // Handle reference types
            if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;
                
                // For const references, delegate to const version
                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    return const_cast<const script_value*>(this)->as<T>();
                } else {
                    // For non-const references - use safe accessors that handle deref()
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        return as_int_ref();
                    } else if constexpr (std::is_same_v<base_type, script_float> || std::is_same_v<base_type, double>) {
                        return as_float_ref();
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        return as_bool_ref();
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        return as_char_ref();
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        return as_string_ref();
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        return as_array();
                    } else if constexpr (std::is_same_v<base_type, std::map<script_value, script_value>>) {
                        return as_map();
                    } else if constexpr (std::is_same_v<base_type, script_value>) {
                        // For script_value&, just return a reference to the dereferenced value
                        return deref();
                    } else {
                        // For user-defined types stored as objects
                        if (type_info_ && type_info_->is_object()) {
                            auto holder = std::get<std::shared_ptr<object_holder>>(storage_);
                            // Get the void* from the holder
                            auto objectPtr = holder->data;
                            // Try to cast to the requested type
                            if (auto typedPtr = std::static_pointer_cast<base_type>(objectPtr)) {
                                return *typedPtr;
                            }
                            throw runtime_error("Object type mismatch");
                        }
                        throw runtime_error("Unsupported type for non-const reference extraction");
                    }
                }
            }
            // For non-reference types, delegate to const version
            else {
                return const_cast<const script_value*>(this)->as<T>();
            }
        }
        
        // Conversion to string for debugging
        std::string to_string() const;
        
        // Dereference method - returns this for non-references, dereferences for references
        const script_value& deref() const;
        script_value& deref();
        
        // Assignment through reference - assigns to target if this is a reference
        void assign_through(const script_value& value);
        void assign_through(script_value&& value);
        
        // Operators
        bool operator==(const script_value& other) const;
        bool operator!=(const script_value& other) const { return !(*this == other); }
        
        // C++20 spaceship operator for complete ordering
        std::strong_ordering operator<=>(const script_value& other) const;
        
        // Custom conversion checker function type
        using conversion_func = std::function<script_value(const script_value&, const std::type_info&)>;
        
        // Get engine reference for creating new script_values
        std::weak_ptr<engine> get_engine_ref() const { return engine_ref_; }
        
        
        // Implicit conversion operators for common C++ types
        // These enable natural overload resolution in C++ function calls
        operator int() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                auto int_val = std::get<script_int>(val.storage_);
                if (int_val < std::numeric_limits<int>::min() || 
                    int_val > std::numeric_limits<int>::max()) {
                    throw runtime_error("script_int value out of range for int");
                }
                return static_cast<int>(int_val);
            }
            throw runtime_error("script_value is not an integer");
        }
        
        operator int64_t() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                return std::get<script_int>(val.storage_);
            }
            throw runtime_error("script_value is not an integer");
        }
        
        operator double() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                return std::get<script_float>(val.storage_);
            } else if (val.type() == script_value_type::jai_int_type) {
                return static_cast<double>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        operator float() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                return static_cast<float>(std::get<script_float>(val.storage_));
            } else if (val.type() == script_value_type::jai_int_type) {
                return static_cast<float>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        
    private:
        type_info_ptr type_info_;  // Complete type information
        std::weak_ptr<engine> engine_ref_;  // Reference to creating engine for conversions
        
        // For object storage - external serialization will handle this
        struct object_holder {
            std::string type_name;           // Type identification for serialization
            uint64_t type_id = UINT64_MAX;  // Interned type name ID for fast comparison (UINT64_MAX = not set)
            std::shared_ptr<void> data;     // The actual object
            bool is_cpp_class_instance = false;  // True if data is a class_instance wrapping a C++ object

            // Note: Serialization functions will be managed externally
            // by ISerializer implementations to keep JaiScript dependency-free
        };
        
        // Reference wrapper for reference types
        struct reference_holder {
            script_value* target = nullptr;  // Points to the referenced value
            std::weak_ptr<environment> sourceEnv;  // environment that owns the target
        };
        
        
        // Tag type for invalid values
        struct invalid_tag {};
        
        // Type-erased storage using variant for efficiency
        using storage = std::variant<
            std::monostate,                               // Null
            script_int,                                           // script_int
            script_float,                                         // script_float
            script_string,                                        // script_string
            script_char,                                          // script_char
            script_bool,                                          // script_bool
            std::shared_ptr<std::vector<script_value>>,          // Array<T>
            std::shared_ptr<std::map<script_value, script_value>>,      // Map<K,V>
            std::shared_ptr<object_holder>,                // Object<T>
            script_function,                                // Function
            std::shared_ptr<reference_holder>,             // T&
            std::shared_ptr<script_value>,                       // shared_ptr<T>
            std::weak_ptr<object_holder>,                         // weak_ptr<T>
            invalid_tag                                           // Invalid value marker
        >;
        
        storage storage_;
        void* cpp_bound_ptr_ = nullptr;  // If non-null, this value is bound to a C++ variable
        
    public:
        // Method to set engine reference after construction
        void set_engine_ref(std::weak_ptr<engine> eng) { engine_ref_ = eng; }
        
        // Extract object_holder for class_instance operations
        // Returns nullptr if not an object type
        std::shared_ptr<object_holder> get_object_holder() {
            auto t = type();
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<std::shared_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }
        
        const std::shared_ptr<object_holder> get_object_holder() const {
            auto t = type();
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<std::shared_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }

        // Set object holder directly (used when restoring from weak_ptr.lock())
        void set_object_holder(std::shared_ptr<object_holder> holder) {
            storage_ = holder;
        }

        // Get raw array storage for interpreter operations
        std::shared_ptr<std::vector<script_value>>& get_array_storage() {
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
        }
        
        // Get raw map storage for interpreter operations
        std::shared_ptr<std::map<script_value, script_value>>& get_map_storage() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
        }
        
        // Get raw weak_ptr storage for interpreter operations
        std::weak_ptr<object_holder>& get_weak_ptr_storage() {
            if (type() != script_value_type::jai_weak_ptr_type) {
                throw runtime_error("script_value is not a weak_ptr");
            }
            return std::get<std::weak_ptr<object_holder>>(storage_);
        }
        
        // ===== Safe Public APIs for Interpreter Operations =====
        
        // Factory method for null values with engine reference
        static script_value make_null(std::weak_ptr<engine> eng) {
            return script_value(std::monostate{}, eng);
        }
        
        // Factory method for invalid values (used as sentinel for non-existent fields/methods)
        static script_value make_invalid(std::weak_ptr<engine> eng) {
            script_value val(std::monostate{}, eng);  // Start with null
            val.storage_ = invalid_tag{};  // Change to invalid
            val.type_info_ = type_info::make_invalid();  // Set proper type info
            return val;
        }
        
        // Check if this value has a valid engine reference
        bool has_valid_engine_ref() const {
            return !engine_ref_.expired();
        }
        
        
        // Safe access to reference holder for reference types
        reference_holder* get_reference_holder() {
            if (type() == script_value_type::jai_reference_type) {
                return std::get<std::shared_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }
        
        const reference_holder* get_reference_holder() const {
            if (type() == script_value_type::jai_reference_type) {
                return std::get<std::shared_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }
        
        
        // Check if array/map has unique ownership (for COW optimization)
        bool is_unique_reference() const {
            if (type() == script_value_type::jai_array_type) {
                const auto& ptr = std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
                return ptr.use_count() == 1;
            } else if (type() == script_value_type::jai_map_type) {
                const auto& ptr = std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
                return ptr.use_count() == 1;
            }
            return false;
        }
        
        // Safe weak_ptr access
        std::weak_ptr<object_holder> get_weak_ptr() const {
            if (type() == script_value_type::jai_weak_ptr_type) {
                return std::get<std::weak_ptr<object_holder>>(storage_);
            }
            return std::weak_ptr<object_holder>();
        }
        
        // Safe class_instance extraction from object_holder
        std::shared_ptr<class_instance> get_class_instance() {
            auto holder = get_object_holder();
            if (holder && holder->is_cpp_class_instance) {
                return std::static_pointer_cast<class_instance>(holder->data);
            }
            return nullptr;
        }
        
        
        // Direct weak_ptr assignment for interpreter use
        void set_weak_ptr(const std::weak_ptr<object_holder>& weak) {
            if (type() == script_value_type::jai_weak_ptr_type) {
                storage_ = weak;
            } else {
                throw runtime_error("Cannot set weak_ptr on non-weak_ptr script_value");
            }
        }

        // Direct shared_ptr assignment for interpreter use
        void set_shared_ptr(const std::shared_ptr<script_value>& shared) {
            if (type() == script_value_type::jai_shared_ptr_type) {
                storage_ = shared;
            } else {
                throw runtime_error("Cannot set shared_ptr on non-shared_ptr script_value");
            }
        }

        // Get the wrapped value from a shared_ptr
        script_value get_shared_ptr_value() const {
            if (type() == script_value_type::jai_shared_ptr_type) {
                auto wrapped = std::get<std::shared_ptr<script_value>>(storage_);
                if (wrapped) {
                    return *wrapped;
                }
            }
            throw runtime_error("Cannot get wrapped value from non-shared_ptr script_value");
        }
        
        // Create an empty weak_ptr value
        static script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type, std::weak_ptr<engine> eng) {
            script_value v(std::monostate{}, eng);
            v.type_info_ = weak_ptr_type ? weak_ptr_type : type_info::make_weak_ptr(nullptr);
            v.storage_ = std::weak_ptr<object_holder>();
            return v;
        }
        
        // Create a weak_ptr from an object
        // Create a weak_ptr from an object (implemented in value.cpp)
        static script_value make_weak_ptr(const script_value& obj, std::weak_ptr<engine> eng);
        
        // Set type info for special cases (like weak_ptr creation)
        void set_type_info(type_info_ptr type) {
            type_info_ = type;
        }
        
    private:
        // Friends only for essential access patterns
        template<typename T> friend class class_builder;  // For make_cpp_object
        friend class serialization::binary_archive_writer;  // For storage_ access
        friend class serialization::binary_archive_reader;  // For storage_ access
        friend class serialization::json_archive_writer;    // For storage_ access
        friend class serialization::json_archive_reader;    // For storage_ access
        
        // STL container friends for direct assignment support
        template<typename K, typename V, typename C, typename A> friend class std::map;
        template<typename T1, typename T2> friend struct std::pair;
        template<typename... Types> friend class std::tuple;
        template<typename K, typename C, typename A> friend class std::set;
    };
    
} // namespace jai

#endif // __JAISCRIPT_CORE_VALUE_HPP__