#pragma once

#ifndef __JAISCRIPT_CORE_VALUE_HPP__
#define __JAISCRIPT_CORE_VALUE_HPP__

#include "types.hpp"
#include "type_info.hpp"
#include "conversion_registry.hpp"
#include "runtime_errors.hpp"
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
        // type_info_ is set to nullptr since these are temporary placeholders without engine context
        struct ast_literal_tag {};
        script_value(ast_literal_tag, script_int i) : type_info_(nullptr), storage_(i) {}
        script_value(ast_literal_tag, script_float f) : type_info_(nullptr), storage_(f) {}
        script_value(ast_literal_tag, const script_string& s) : type_info_(nullptr), storage_(s) {}
        script_value(ast_literal_tag, script_char c) : type_info_(nullptr), storage_(c) {}
        script_value(ast_literal_tag, script_bool b) : type_info_(nullptr), storage_(b) {}
        script_value(ast_literal_tag, std::monostate) : type_info_(nullptr), storage_(std::monostate{}) {}
        script_value(script_int i, std::weak_ptr<engine> eng);
        script_value(script_float f, std::weak_ptr<engine> eng);
        script_value(const script_string& s, std::weak_ptr<engine> eng);
        script_value(script_string&& s, std::weak_ptr<engine> eng);
        script_value(const char* s, std::weak_ptr<engine> eng);
        script_value(script_char c, std::weak_ptr<engine> eng);
        script_value(script_bool b, std::weak_ptr<engine> eng);
        
        // Template constructors for all numeric types - ALL require engine references
        // NOTE: Implementations are in value_impl.hpp (include after engine.hpp)

        // Engine-aware template constructors for integral types (preferred)
        template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
        script_value(T i, std::weak_ptr<engine> eng);

        // Engine-aware template constructors for floating point types (preferred)
        template<typename T>
        requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
        script_value(T f, std::weak_ptr<engine> eng);

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
        
        // Factory methods for complex types (deprecated - use engine-aware versions below)
        static script_value make_array(type_info_ptr element_type);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType);
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env);

        // Engine-aware factory methods (preferred - ALWAYS use these)
        static script_value make_array(type_info_ptr element_type, std::weak_ptr<engine> eng);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType, std::weak_ptr<engine> eng);
        static script_value make_object(const std::string& type_name, std::shared_ptr<void> data, std::weak_ptr<engine> eng);
        // Optimized version with cached type_id
        static script_value make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, std::weak_ptr<engine> eng, bool is_cpp_class = true);
        // Internal factory method for raw C++ objects - always requires type_id to avoid re-interning
        static script_value make_cpp_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, std::weak_ptr<engine> eng);
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env, std::weak_ptr<engine> eng);
        static script_value make_function(const script_function& func, std::weak_ptr<engine> eng);
        
        // Factory method for C++ bound values
        // NOTE: Implementation is in value_impl.hpp (include after engine.hpp)
        template<typename T>
        static script_value make_cpp_bound(T* target, std::weak_ptr<engine> eng);
        
        // Template factory methods removed - use engine->make_object instead
        // This ensures proper type name registration
    public:
        
        // Engine-aware object creation through registered class system
        template<typename T, typename... Args>
        static script_value make_registered_object(class engine* eng, Args&&... args);
        
        // Type information
        type_info_ptr get_type_info() const { return type_info_; }
        script_value_type type() const {
            return type_info_ ? type_info_->base_type : script_value_type::jai_null_type;
        }

        // Get type from storage variant - useful for AST literals that have nullptr type_info
        script_value_type storage_type() const {
            switch (storage_.index()) {
                case 0: return script_value_type::jai_null_type;        // std::monostate
                case 1: return script_value_type::jai_int_type;         // script_int
                case 2: return script_value_type::jai_float_type;       // script_float
                case 3: return script_value_type::jai_string_type;      // script_string
                case 4: return script_value_type::jai_char_type;        // script_char
                case 5: return script_value_type::jai_bool_type;        // script_bool
                case 6: return script_value_type::jai_array_type;       // Array
                case 7: return script_value_type::jai_map_type;         // Map
                case 8: return script_value_type::jai_object_type;      // Object
                case 9: return script_value_type::jai_function_type;    // Function
                case 10: return script_value_type::jai_reference_type;  // Reference
                case 11: return script_value_type::jai_shared_ptr_type; // shared_ptr
                case 12: return script_value_type::jai_weak_ptr_type;   // weak_ptr
                case 13: return script_value_type::jai_invalid_type;    // invalid_tag
                default: return script_value_type::jai_invalid_type;
            }
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
            auto result = checked_as_string();
            if (!result) {
                throw runtime_error(result.message());
            }
            return *result.value();
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

        // ============================================================================
        // UNCHECKED ACCESSORS - Ultra-fast direct access without type checking
        // ONLY use these when you've already verified the type (e.g., via type() switch)
        // These provide zero-overhead access with no branching or error checking
        // Uses std::get_if which returns a pointer without throwing (faster than std::get)
        // ============================================================================

        inline script_bool unchecked_as_bool() const noexcept {
            return *std::get_if<static_cast<size_t>(storage_index::jai_bool)>(&storage_);
        }

        inline script_int unchecked_as_int() const noexcept {
            return *std::get_if<static_cast<size_t>(storage_index::jai_int)>(&storage_);
        }

        inline script_float unchecked_as_float() const noexcept {
            return *std::get_if<static_cast<size_t>(storage_index::jai_float)>(&storage_);
        }

        inline const script_string& unchecked_as_string() const noexcept {
            return *std::get_if<static_cast<size_t>(storage_index::jai_string)>(&storage_);
        }

        inline const std::vector<script_value>& as_array() const {
            auto result = checked_as_array();
            if (!result) {
                throw runtime_error(result.message());
            }
            return *result.value();
        }
        
        inline std::vector<script_value>& as_array() {
            // For non-const, we can't use checked_as_array() which returns const&
            // So we check type and return mutable reference directly
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
        }
        
        inline const std::map<script_value, script_value>& as_map() const {
            auto result = checked_as_map();
            if (!result) {
                throw runtime_error(result.message());
            }
            return *result.value();
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
        // Thin wrapper around checked_as<T>() that throws on error
        template<typename T>
        T as() const {
            auto result = checked_as<T>();
            if (!result) {
                throw runtime_error(result.message().empty() ? "Type conversion failed" : result.message());
            }
            return std::move(result.value());
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

        // ============================================================================
        // CHECKED HELPER METHODS - Return checked_result instead of throwing
        // Using pointers to avoid reference_wrapper complexity and try/catch overhead
        // ============================================================================

        inline checked_result<const script_string*> checked_as_string() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_string_type) {
                return checked_result<const script_string*>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a string"
                );
            }
            if (val.cpp_bound_ptr_) {
                return checked_result<const script_string*>(static_cast<const script_string*>(val.cpp_bound_ptr_));
            }
            return checked_result<const script_string*>(&std::get<script_string>(val.storage_));
        }

        inline checked_result<const std::vector<script_value>*> checked_as_array() const {
            if (type() != script_value_type::jai_array_type) {
                return checked_result<const std::vector<script_value>*>(
                    make_error_code(runtime_error_code::not_an_array),
                    "script_value is not an array"
                );
            }
            return checked_result<const std::vector<script_value>*>(std::get<std::shared_ptr<std::vector<script_value>>>(storage_).get());
        }

        inline checked_result<const std::map<script_value, script_value>*> checked_as_map() const {
            if (type() != script_value_type::jai_map_type) {
                return checked_result<const std::map<script_value, script_value>*>(
                    make_error_code(runtime_error_code::not_a_map),
                    "script_value is not a map"
                );
            }
            return checked_result<const std::map<script_value, script_value>*>(std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_).get());
        }

        // ============================================================================
        // COMPREHENSIVE checked_as<T>() - Returns checked_result instead of throwing
        // ============================================================================
        template<typename T>
        checked_result<T> checked_as() const {
            // FAST PATH: Direct type specializations for hot types
            if constexpr (std::is_same_v<T, script_int>) {
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return checked_result<T>(static_cast<script_int>(*static_cast<const int*>(val.cpp_bound_ptr_)));
                }
                if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(std::get<script_int>(val.storage_));
                } else if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(static_cast<script_int>(std::get<script_float>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not an integer or float. Actual type: " +
                    std::to_string(static_cast<int>(val.type()))
                );
            }
            else if constexpr (std::is_same_v<T, script_float> || std::is_same_v<T, double>) {
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return checked_result<T>(*static_cast<const script_float*>(val.cpp_bound_ptr_));
                }
                if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(std::get<script_float>(val.storage_));
                } else if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(static_cast<script_float>(std::get<script_int>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a float or integer"
                );
            }
            else if constexpr (std::is_same_v<T, script_bool>) {
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return checked_result<T>(*static_cast<const script_bool*>(val.cpp_bound_ptr_));
                }
                if (val.type() == script_value_type::jai_bool_type) {
                    return checked_result<T>(std::get<script_bool>(val.storage_));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a boolean"
                );
            }
            else if constexpr (std::is_same_v<T, script_char>) {
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return checked_result<T>(*static_cast<const script_char*>(val.cpp_bound_ptr_));
                }
                if (val.type() == script_value_type::jai_char_type) {
                    return checked_result<T>(std::get<script_char>(val.storage_));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a character"
                );
            }
            // MEDIUM PATH: Common integral conversions
            else if constexpr (std::is_same_v<T, int>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int"
                    );
                }
                return checked_result<T>(static_cast<int>(val));
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                return checked_result<T>(static_cast<int64_t>(val_result.value()));
            }
            else if constexpr (std::is_same_v<T, float>) {
                const script_value& val = deref();
                if (val.type() == script_value_type::jai_float_type) {
                    if (val.cpp_bound_ptr_) {
                        return checked_result<T>(static_cast<float>(*static_cast<const script_float*>(val.cpp_bound_ptr_)));
                    }
                    return checked_result<T>(static_cast<float>(std::get<script_float>(val.storage_)));
                } else if (val.type() == script_value_type::jai_int_type) {
                    if (val.cpp_bound_ptr_) {
                        return checked_result<T>(static_cast<float>(*static_cast<const int*>(val.cpp_bound_ptr_)));
                    }
                    return checked_result<T>(static_cast<float>(std::get<script_int>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Cannot convert script_value to float"
                );
            }
            // String types
            else if constexpr (std::is_same_v<T, script_string> || std::is_same_v<T, std::string>) {
                auto str_result = checked_as_string();
                if (!str_result) {
                    return checked_result<T>(str_result.error(), str_result.message());
                }
                return checked_result<T>(*str_result.value());
            }
            // Handle reference types (now supported via checked_result<T&> specialization)
            else if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;

                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        if (type() != script_value_type::jai_int_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not an integer"
                            );
                        }
                        return checked_result<T>(std::get<script_int>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_float>) {
                        if (type() != script_value_type::jai_float_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a float"
                            );
                        }
                        return checked_result<T>(std::get<script_float>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        if (type() != script_value_type::jai_bool_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a boolean"
                            );
                        }
                        return checked_result<T>(std::get<script_bool>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        if (type() != script_value_type::jai_char_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a character"
                            );
                        }
                        return checked_result<T>(std::get<script_char>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        auto str_result = checked_as_string();
                        if (!str_result) {
                            return checked_result<T>(str_result.error(), str_result.message());
                        }
                        return checked_result<T>(*str_result.value());
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        auto arr_result = checked_as_array();
                        if (!arr_result) {
                            return checked_result<T>(arr_result.error(), arr_result.message());
                        }
                        return checked_result<T>(*arr_result.value());
                    } else if constexpr (std::is_same_v<base_type, std::map<script_value, script_value>>) {
                        auto map_result = checked_as_map();
                        if (!map_result) {
                            return checked_result<T>(map_result.error(), map_result.message());
                        }
                        return checked_result<T>(*map_result.value());
                    } else {
                        auto t = type();
                        if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                            auto ptr_result = checked_as<std::shared_ptr<base_type>>();
                            if (!ptr_result) {
                                return checked_result<T>(ptr_result.error(), ptr_result.message());
                            }
                            return checked_result<T>(*ptr_result.value());
                        }
                        return checked_result<T>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Unsupported type for const reference extraction"
                        );
                    }
                } else {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot extract non-const reference from const script_value"
                    );
                }
            }
            // Signed integer types with bounds checking
            else if constexpr (std::is_same_v<T, int8_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int8_t>::min() || val > std::numeric_limits<int8_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int8_t"
                    );
                }
                return checked_result<T>(static_cast<int8_t>(val));
            }
            else if constexpr (std::is_same_v<T, int16_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int16_t>::min() || val > std::numeric_limits<int16_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int16_t"
                    );
                }
                return checked_result<T>(static_cast<int16_t>(val));
            }
            else if constexpr (std::is_same_v<T, int32_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int32_t>::min() || val > std::numeric_limits<int32_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int32_t"
                    );
                }
                return checked_result<T>(static_cast<int32_t>(val));
            }
            // Unsigned integer types with bounds checking
            else if constexpr (std::is_same_v<T, uint8_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint8_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint8_t (must be 0-255)"
                    );
                }
                return checked_result<T>(static_cast<uint8_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint16_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint16_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint16_t (must be non-negative)"
                    );
                }
                return checked_result<T>(static_cast<uint16_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint32_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint32_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint32_t (must be non-negative)"
                    );
                }
                return checked_result<T>(static_cast<uint32_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint64_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < 0) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value must be non-negative for uint64_t"
                    );
                }
                return checked_result<T>(static_cast<uint64_t>(val));
            }
            else if constexpr (std::is_same_v<T, size_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return checked_result<T>(val_result.error(), val_result.message());
                }
                script_int val = val_result.value();
                if (val < 0) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value must be non-negative for size_t"
                    );
                }
                return checked_result<T>(static_cast<size_t>(val));
            }
            // Vector types
            else if constexpr (is_specialization_v<T, std::vector>) {
                // Use engine's conversion registry first
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Built-in vector handling
                if (type() != script_value_type::jai_array_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_array),
                        "Cannot convert non-array to vector"
                    );
                }

                using element_type = typename T::value_type;
                T result;
                auto arr_result = checked_as_array();
                if (!arr_result) {
                    return checked_result<T>(arr_result.error(), arr_result.message());
                }
                const auto& arr = *arr_result.value();
                result.reserve(arr.size());

                for (const auto& elem : arr) {
                    if constexpr (std::is_same_v<element_type, script_value>) {
                        result.push_back(elem);
                    } else {
                        auto elem_result = elem.checked_as<element_type>();
                        if (!elem_result) {
                            return checked_result<T>(elem_result.error(),
                                "Failed to convert array element: " + elem_result.message());
                        }
                        result.push_back(elem_result.value());
                    }
                }
                return checked_result<T>(std::move(result));
            }
            // Map types
            else if constexpr (is_specialization_v<T, std::map>) {
                // Use engine's conversion registry first
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Built-in map handling
                if (type() != script_value_type::jai_map_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_a_map),
                        "Cannot convert non-map to std::map"
                    );
                }

                using key_type = typename T::key_type;
                using mapped_type = typename T::mapped_type;
                T result;
                auto map_result = checked_as_map();
                if (!map_result) {
                    return checked_result<T>(map_result.error(), map_result.message());
                }
                const auto& m = *map_result.value();

                for (const auto& [k, v] : m) {
                    if constexpr (std::is_same_v<key_type, script_value> && std::is_same_v<mapped_type, script_value>) {
                        result.emplace(k, v);
                    } else if constexpr (std::is_same_v<key_type, script_value>) {
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return checked_result<T>(val_result.error(),
                                "Failed to convert map value: " + val_result.message());
                        }
                        result.emplace(k, val_result.value());
                    } else if constexpr (std::is_same_v<mapped_type, script_value>) {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return checked_result<T>(key_result.error(),
                                "Failed to convert map key: " + key_result.message());
                        }
                        result.emplace(key_result.value(), v);
                    } else {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return checked_result<T>(key_result.error(),
                                "Failed to convert map key: " + key_result.message());
                        }
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return checked_result<T>(val_result.error(),
                                "Failed to convert map value: " + val_result.message());
                        }
                        result.emplace(key_result.value(), val_result.value());
                    }
                }
                return checked_result<T>(std::move(result));
            }
            // Support for shared_ptr<class_instance> extraction from objects
            else if constexpr (std::is_same_v<T, std::shared_ptr<jai::class_instance>>) {
                auto t = type();
                if (t != script_value_type::jai_object_type && t != script_value_type::jai_shared_ptr_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_object),
                        "script_value is not an object (type=" + std::to_string(static_cast<int>(type())) + ")"
                    );
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                if (!objHolder->is_class_instance_wrapper) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_a_class),
                        "Object is not a class_instance (type_name=" + objHolder->type_name + ")"
                    );
                }
                return checked_result<T>(std::static_pointer_cast<class_instance>(objHolder->data));
            }
            // Support for shared_ptr<void> extraction
            else if constexpr (std::is_same_v<T, std::shared_ptr<void>>) {
                if (type() != script_value_type::jai_object_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_object),
                        "script_value is not an object"
                    );
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                return checked_result<T>(objHolder->data);
            }
            // Support for other shared_ptr types
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                // First check if there's a registered conversion
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Fall back to default shared_ptr extraction
                auto t = type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                    auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);

                    // Check if we need to use the custom extractor
                    if (objHolder->is_class_instance_wrapper) {
                        if (auto eng = engine_ref_.lock()) {
                            auto registry = get_engine_conversion_registry(eng.get());
                            if (registry) {
                                auto extracted = registry->extract_custom_object(objHolder->type_name, objHolder->data);
                                if (extracted) {
                                    return checked_result<T>(std::static_pointer_cast<typename T::element_type>(extracted));
                                }
                            }
                        }
                    }

                    // Otherwise use static cast
                    return checked_result<T>(std::static_pointer_cast<typename T::element_type>(objHolder->data));
                }

                return checked_result<T>(
                    make_error_code(runtime_error_code::not_an_object),
                    "script_value is not an object"
                );
            }
            // Support for extracting custom objects by value (dereference shared_ptr)
            else if constexpr (std::is_class_v<T> &&
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                // Check custom converter first
                if (auto eng = engine_ref_.lock()) {
                    auto registry = get_engine_conversion_registry(eng.get());
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // For custom classes, try to extract shared_ptr and dereference
                auto t = type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                    auto ptr_result = checked_as<std::shared_ptr<T>>();
                    if (!ptr_result) {
                        return checked_result<T>(ptr_result.error(), ptr_result.message());
                    }
                    return checked_result<T>(*ptr_result.value());
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Cannot extract custom type by value from non-object"
                );
            }
            else {
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Unsupported type conversion"
                );
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
            bool is_class_instance_wrapper = false;  // True if data is a class_instance object (both C++ and script classes), false for raw data

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

        // Variant indices for ultra-fast unchecked access
        // IMPORTANT: Keep this enum in sync with the storage variant below!
        enum class storage_index : size_t {
            jai_null = 0,
            jai_int = 1,
            jai_float = 2,
            jai_string = 3,
            jai_char = 4,
            jai_bool = 5,
            jai_array = 6,
            jai_map = 7,
            jai_object = 8,
            jai_function = 9,
            jai_reference = 10,
            jai_shared_ptr = 11,
            jai_weak_ptr = 12,
            jai_invalid = 13
        };

        // Type-erased storage using variant for efficiency
        using storage = std::variant<
            std::monostate,                               // 0 - Null
            script_int,                                   // 1 - script_int
            script_float,                                 // 2 - script_float
            script_string,                                // 3 - script_string
            script_char,                                  // 4 - script_char
            script_bool,                                  // 5 - script_bool
            std::shared_ptr<std::vector<script_value>>,   // 6 - Array<T>
            std::shared_ptr<std::map<script_value, script_value>>, // 7 - Map<K,V>
            std::shared_ptr<object_holder>,               // 8 - Object<T>
            script_function,                              // 9 - Function
            std::shared_ptr<reference_holder>,            // 10 - T&
            std::shared_ptr<script_value>,                // 11 - shared_ptr<T>
            std::weak_ptr<object_holder>,                 // 12 - weak_ptr<T>
            invalid_tag                                   // 13 - Invalid value marker
        >;
        
        storage storage_;
        void* cpp_bound_ptr_ = nullptr;  // If non-null, this value is bound to a C++ variable
        
    public:
        // Method to set engine reference after construction
        void set_engine_ref(std::weak_ptr<engine> eng) { engine_ref_ = eng; }

        // Access raw storage for AST literals (bypasses type checking)
        const storage& get_storage() const { return storage_; }
        
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
        static script_value make_invalid(std::weak_ptr<engine> eng);
        
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
            if (holder && holder->is_class_instance_wrapper) {
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
        static script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type, std::weak_ptr<engine> eng);
        
        // Create a weak_ptr from an object
        // Create a weak_ptr from an object (implemented in value.cpp)
        // Returns checked_result to avoid throwing exceptions
        static checked_result<script_value> make_weak_ptr(const script_value& obj, std::weak_ptr<engine> eng);
        
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