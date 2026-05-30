#pragma once

#ifndef __JAISCRIPT_CORE_VALUE_HPP__
#define __JAISCRIPT_CORE_VALUE_HPP__

#include "types.hpp"
#include "type_info.hpp"
#include "conversion_registry.hpp"
#include "runtime_errors.hpp"
#include "strong_ptr.hpp"
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

    // Convert a double to script_int (int64) with DEFINED behavior for NaN, +/-inf,
    // and out-of-range values. A plain static_cast of those to a 64-bit integer is
    // undefined behavior in C++ (commonly yielding INT64_MIN or a trap). NaN -> 0,
    // and out-of-range saturates to INT64_MIN/MAX.
    inline script_int float_to_script_int(script_float f) noexcept {
        if (std::isnan(f)) return 0;
        if (f >= 9223372036854775808.0) return (std::numeric_limits<script_int>::max)();   // >= 2^63
        if (f <= -9223372036854775808.0) return (std::numeric_limits<script_int>::min)();  // <= -2^63
        return static_cast<script_int>(f);
    }

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
        explicit script_value(std::monostate, engine* eng) : type_info_(nullptr), engine_(eng), storage_(std::monostate{}) {}
        
        // AST literal constructor - ONLY for parser to create placeholder values in AST nodes
        // These values should NEVER be used directly, only as templates for interpreter
        // type_info_ is set to nullptr since these are temporary placeholders without engine context
        struct ast_literal_tag {};
        script_value(ast_literal_tag, script_int i) : type_info_(nullptr), storage_(i) {}
        script_value(ast_literal_tag, script_float f) : type_info_(nullptr), storage_(f) {}
        script_value(ast_literal_tag, const script_string& s) : type_info_(nullptr), storage_(make_strong<script_string>(s)) {}
        script_value(ast_literal_tag, script_char c) : type_info_(nullptr), storage_(c) {}
        script_value(ast_literal_tag, script_bool b) : type_info_(nullptr), storage_(b) {}
        script_value(ast_literal_tag, std::monostate) : type_info_(nullptr), storage_(std::monostate{}) {}
        script_value(script_int i, engine* eng);
        script_value(script_float f, engine* eng);
        script_value(const script_string& s, engine* eng);
        script_value(script_string&& s, engine* eng);
        script_value(const char* s, engine* eng);
        script_value(script_char c, engine* eng);
        script_value(script_bool b, engine* eng);
        
        // Template constructors for all numeric types - ALL require engine references
        // NOTE: Implementations are in value_impl.hpp (include after engine.hpp)

        // Engine-aware template constructors for integral types (preferred)
        template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
        script_value(T i, engine* eng);

        // Engine-aware template constructors for floating point types (preferred)
        template<typename T>
        requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
        script_value(T f, engine* eng);

        // Delete pointer constructors - use add_global_ref instead
        template<typename T>
        script_value(T* ptr, engine* eng) = delete;

        script_value(script_value&& other) noexcept
            : type_info_(std::move(other.type_info_)),
              engine_(other.engine_),
              storage_(std::move(other.storage_)),
              cpp_bound_ptr_(other.cpp_bound_ptr_),
              cpp_bound_type_size_(other.cpp_bound_type_size_) {
            other.type_info_ = nullptr;
            other.engine_ = nullptr;
            other.storage_ = std::monostate{};
            other.cpp_bound_ptr_ = nullptr;
            other.cpp_bound_type_size_ = 0;
        }

        script_value& operator=(script_value&& other) noexcept {
            if (this != &other) {
                type_info_ = std::move(other.type_info_);
                engine_ = other.engine_;
                storage_ = std::move(other.storage_);
                cpp_bound_ptr_ = other.cpp_bound_ptr_;
                cpp_bound_type_size_ = other.cpp_bound_type_size_;
                other.type_info_ = nullptr;
                other.engine_ = nullptr;
                other.storage_ = std::monostate{};
                other.cpp_bound_ptr_ = nullptr;
                other.cpp_bound_type_size_ = 0;
            }
            return *this;
        }
        
        // Copy constructor (shallow copy for reference semantics)
        script_value(const script_value& other);
        script_value& operator=(const script_value& other);
        
        // Explicit deep copy method
        script_value clone() const;
        
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env);

        // Engine-aware factory methods (preferred - ALWAYS use these)
        static script_value make_array(type_info_ptr element_type, engine* eng);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType, engine* eng);
        static script_value make_object(const std::string& type_name, std::shared_ptr<void> data, engine* eng);
        // Optimized version with cached type_id
        static script_value make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng, bool is_cpp_class = true);
        // Internal factory method for raw C++ objects - always requires type_id to avoid re-interning
        static script_value make_cpp_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng);
    public:
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env, engine* eng);
        // Overload for container subscript references with element type constraint
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env, engine* eng, type_info_ptr container_element_type);
        // Reallocation-safe reference to a vector element (range-for auto&, arr[i] lvalue):
        // holds the owning container + index so deref/assign-through recompute the element
        // address each time and bounds-check it (see reference_holder::container).
        static script_value make_element_reference(const strong_ptr<std::vector<script_value>>& container, size_t index,
                                                   const std::shared_ptr<environment>& env, engine* eng, type_info_ptr element_type);
        static script_value make_function(const script_function& func, engine* eng);
        
        // Factory method for C++ bound values
        // NOTE: Implementation is in value_impl.hpp (include after engine.hpp)
        template<typename T>
        static script_value make_cpp_bound(T* target, engine* eng);
        
        // Template factory methods removed - use engine->make_object instead
        // This ensures proper type name registration
    public:
        
        // Engine-aware object creation through registered class system
        template<typename T, typename... Args>
        static script_value make_registered_object(class engine* eng, Args&&... args);
        
        // Type information
        type_info_ptr get_type_info() const { return type_info_; }

        // Get interned type ID for error messages (falls back to enum value if type_info not set)
        uint64_t type_id() const {
            return type_info_ ? type_info_->id : static_cast<uint64_t>(current_type());
        }

        // current_type() - What type is ACTUALLY stored in the variant right now
        // Use this for runtime type checks (is_null, is_object, etc.)
        script_value_type current_type() const {
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

        // defined_type() - What type the variable was DECLARED as (from type_info)
        // For any_type (var), returns current_type() since it's dynamically typed
        // Use this for type enforcement, assignment compatibility checks
        script_value_type defined_type() const {
            if (type_info_) {
                if (type_info_->base_type == script_value_type::jai_any_type) {
                    return current_type();  // Dynamic typing - use actual value type
                }
                return type_info_->base_type;
            }
            return current_type();
        }

        // type() - Legacy alias, returns defined_type() for backward compatibility
        // Prefer using current_type() or defined_type() for clarity
        script_value_type type() const { return defined_type(); }

        // storage_type() - Legacy alias for current_type()
        script_value_type storage_type() const { return current_type(); }

        // Ultra-fast raw storage index - no pointer chasing, single integer read
        // Use this in hot paths like is_truthy() to avoid type_info_ pointer dereference
        inline size_t raw_storage_index() const noexcept { return storage_.index(); }
        // Type checking methods use raw_storage_index() for fastest possible type checks
        // Direct integer comparison - no switch statement, no enum mapping
        // This ensures a typed variable holding null returns is_null()=true, is_object()=false
        bool is_null() const { return deref().raw_storage_index() == TYPEID_NULL; }
        bool is_invalid() const { return deref().raw_storage_index() == TYPEID_INVALID; }
        bool is_int() const { return deref().raw_storage_index() == TYPEID_INT; }
        bool is_float() const { return deref().raw_storage_index() == TYPEID_FLOAT; }
        bool is_string() const { return deref().raw_storage_index() == TYPEID_STRING; }
        bool is_char() const { return deref().raw_storage_index() == TYPEID_CHAR; }
        bool is_bool() const { return deref().raw_storage_index() == TYPEID_BOOL; }
        bool is_array() const { return deref().raw_storage_index() == TYPEID_ARRAY; }
        bool is_map() const { return deref().raw_storage_index() == TYPEID_MAP; }
        bool is_object() const {
            auto idx = deref().raw_storage_index();
            return idx == TYPEID_OBJECT || idx == TYPEID_SHARED_PTR;
        }
        bool is_function() const { return deref().raw_storage_index() == TYPEID_FUNCTION; }
        bool is_reference() const { return raw_storage_index() == TYPEID_REFERENCE; }  // Don't deref for this check!
        bool is_cpp_bound() const { return cpp_bound_ptr_ != nullptr; }

        // Check if this is a non-owning C++ object reference
        // Returns true when: cpp_bound_ptr_ is set AND object_holder has no owning data
        bool is_non_owning_object() const {
            if (!cpp_bound_ptr_) return false;
            auto idx = raw_storage_index();
            if (idx != TYPEID_OBJECT && idx != TYPEID_SHARED_PTR) return false;
            auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
            return objHolder && !objHolder->data;
        }

        // Get raw pointer to cpp_bound object (for non-owning method calls)
        // Returns nullptr if not cpp_bound. Caller is responsible for type safety.
        void* get_cpp_bound_ptr() const { return cpp_bound_ptr_; }

        // Type-safe extraction of cpp_bound pointer
        // Returns nullptr if not cpp_bound or type doesn't match
        template<typename T>
        T* get_cpp_bound_as() const {
            if (!cpp_bound_ptr_) return nullptr;
            return static_cast<T*>(cpp_bound_ptr_);
        }

        bool is_weak_ptr() const { return deref().raw_storage_index() == TYPEID_WEAK_PTR; }

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
        // These throwing versions delegate to checked_* versions for consistency
        inline script_int as_int() const {
            auto result = checked_as_int();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline script_float as_float() const {
            auto result = checked_as_float();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline const script_string& as_string() const {
            auto result = checked_as_string();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return *result.value();
        }

        inline script_bool as_bool() const {
            auto result = checked_as_bool();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline script_char as_char() const {
            auto result = checked_as_char();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        // ============================================================================
        // UNCHECKED ACCESSORS - Fast direct access without type checking
        // ONLY use these when you've already verified the type (e.g., via type() switch)
        // These check for cpp_bound values and fall back to storage_ otherwise
        // Uses std::get_if which returns a pointer without throwing (faster than std::get)
        // ============================================================================

        inline script_bool unchecked_as_bool() const noexcept {
            if (cpp_bound_ptr_) {
                return *static_cast<const bool*>(cpp_bound_ptr_);
            }
            return *std::get_if<TYPEID_BOOL>(&storage_);
        }

        inline script_int unchecked_as_int() const noexcept {
            if (cpp_bound_ptr_) {
                const uint8_t size = cpp_bound_type_size_ & 0x7F;
                if (cpp_bound_type_size_ & 0x80) {
                    switch (size) {
                        case 1: return static_cast<script_int>(*static_cast<const uint8_t*>(cpp_bound_ptr_));
                        case 2: return static_cast<script_int>(*static_cast<const uint16_t*>(cpp_bound_ptr_));
                        case 4: return static_cast<script_int>(*static_cast<const uint32_t*>(cpp_bound_ptr_));
                        default: return static_cast<script_int>(*static_cast<const uint64_t*>(cpp_bound_ptr_));
                    }
                }
                switch (size) {
                    case 1: return static_cast<script_int>(*static_cast<const int8_t*>(cpp_bound_ptr_));
                    case 2: return static_cast<script_int>(*static_cast<const int16_t*>(cpp_bound_ptr_));
                    case 4: return static_cast<script_int>(*static_cast<const int32_t*>(cpp_bound_ptr_));
                    default: return *static_cast<const script_int*>(cpp_bound_ptr_);
                }
            }
            return *std::get_if<TYPEID_INT>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead in loops)
        inline script_int& unchecked_as_int_ref() noexcept {
            return *std::get_if<TYPEID_INT>(&storage_);
        }

        inline script_float unchecked_as_float() const noexcept {
            if (cpp_bound_ptr_) {
                if ((cpp_bound_type_size_ & 0x7F) == sizeof(script_float))
                    return *static_cast<const script_float*>(cpp_bound_ptr_);
                return static_cast<script_float>(*static_cast<const float*>(cpp_bound_ptr_));
            }
            return *std::get_if<TYPEID_FLOAT>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead in loops)
        inline script_float& unchecked_as_float_ref() noexcept {
            return *std::get_if<TYPEID_FLOAT>(&storage_);
        }

        inline const script_string& unchecked_as_string() const noexcept {
            return **std::get_if<TYPEID_STRING>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead)
        inline script_string& unchecked_as_string_ref() noexcept {
            return **std::get_if<TYPEID_STRING>(&storage_);
        }

        inline script_char unchecked_as_char() const noexcept {
            if (cpp_bound_ptr_) {
                return *static_cast<const script_char*>(cpp_bound_ptr_);
            }
            return *std::get_if<TYPEID_CHAR>(&storage_);
        }

        // Unchecked array accessor - caller must verify is_array() first
        inline const std::vector<script_value>& unchecked_as_array() const noexcept {
            return **std::get_if<TYPEID_ARRAY>(&storage_);
        }

        // Unchecked mutable array storage accessor - caller must verify is_array() first
        inline strong_ptr<std::vector<script_value>>& unchecked_get_array_storage() noexcept {
            return *std::get_if<TYPEID_ARRAY>(&storage_);
        }

        // Unchecked function accessor - caller must verify is_function() first
        inline const script_function& unchecked_as_function() const noexcept {
            return **std::get_if<TYPEID_FUNCTION>(&storage_);
        }

        // Unchecked map accessor - caller must verify is_map() first
        inline const std::map<script_value, script_value>& unchecked_as_map() const noexcept {
            return **std::get_if<TYPEID_MAP>(&storage_);
        }

        // Unchecked mutable map storage accessor - caller must verify is_map() first
        inline strong_ptr<std::map<script_value, script_value>>& unchecked_get_map_storage() noexcept {
            return *std::get_if<TYPEID_MAP>(&storage_);
        }

        inline const std::vector<script_value>& as_array() const {
            auto result = checked_as_array();
            if (!result) {
                throw runtime_error(std::string(result.message()));
            }
            return *result.value();
        }
        
        inline std::vector<script_value>& as_array() {
            // For non-const, we can't use checked_as_array() which returns const&
            // So we check type and return mutable reference directly
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return *std::get<strong_ptr<std::vector<script_value>>>(storage_);
        }
        
        inline const std::map<script_value, script_value>& as_map() const {
            auto result = checked_as_map();
            if (!result) {
                throw runtime_error(std::string(result.message()));
            }
            return *result.value();
        }
        
        inline std::map<script_value, script_value>& as_map() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return *std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
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
            return *std::get<strong_ptr<script_string>>(val.storage_);
        }
        
        // Generic extraction with type checking
        // Thin wrapper around checked_as<T>() that throws on error
        // NOTE: This throws for compatibility with C++ bindings (dynamic_binder, function_binder)
        // Internal interpreter code should use checked_as<T>() directly
        template<typename T>
        T as() const {
            auto result = checked_as<T>();
            if (!result) {
                if (result.message().empty()) {
                    throw runtime_error("Type conversion failed");
                }
                // Format the error message with symbol IDs
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
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
                            auto holder = std::get<strong_ptr<object_holder>>(storage_);
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
            return checked_result<const script_string*>(std::get<strong_ptr<script_string>>(val.storage_).get());
        }

        inline checked_result<const std::vector<script_value>*> checked_as_array() const {
            if (type() != script_value_type::jai_array_type) {
                return checked_result<const std::vector<script_value>*>(
                    make_error_code(runtime_error_code::not_an_array),
                    "script_value is not an array"
                );
            }
            return checked_result<const std::vector<script_value>*>(std::get<strong_ptr<std::vector<script_value>>>(storage_).get());
        }

        inline checked_result<const std::map<script_value, script_value>*> checked_as_map() const {
            if (type() != script_value_type::jai_map_type) {
                return checked_result<const std::map<script_value, script_value>*>(
                    make_error_code(runtime_error_code::not_a_map),
                    "script_value is not a map"
                );
            }
            return checked_result<const std::map<script_value, script_value>*>(std::get<strong_ptr<std::map<script_value, script_value>>>(storage_).get());
        }

        inline checked_result<script_int> checked_as_int() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                if (val.cpp_bound_ptr_) {
                    return checked_result<script_int>(val.unchecked_as_int());
                }
                return checked_result<script_int>(std::get<script_int>(val.storage_));
            } else if (val.type() == script_value_type::jai_float_type) {
                if (val.cpp_bound_ptr_) {
                    return checked_result<script_int>(float_to_script_int(val.unchecked_as_float()));
                }
                return checked_result<script_int>(float_to_script_int(std::get<script_float>(val.storage_)));
            }
            return checked_result<script_int>(
                make_error_code(runtime_error_code::type_mismatch),
                "script_value is not an integer or float"
            );
        }

        inline checked_result<script_float> checked_as_float() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                // unchecked_as_float() decodes the bound size (4-byte float vs
                // 8-byte double). Reading a bound float as a raw script_float
                // (double) reinterpreted 8 bytes from a 4-byte object -> garbage + OOB.
                if (val.cpp_bound_ptr_) {
                    return checked_result<script_float>(val.unchecked_as_float());
                }
                return checked_result<script_float>(std::get<script_float>(val.storage_));
            } else if (val.type() == script_value_type::jai_int_type) {
                // Int to float conversion. unchecked_as_int() decodes the bound
                // integer's true size/signedness; the old `const int*` cast read a
                // fixed 4-byte signed value, truncating int64 and misreading other widths.
                if (val.cpp_bound_ptr_) {
                    return checked_result<script_float>(static_cast<script_float>(val.unchecked_as_int()));
                }
                return checked_result<script_float>(static_cast<script_float>(std::get<script_int>(val.storage_)));
            }
            return checked_result<script_float>(
                make_error_code(runtime_error_code::type_mismatch),
                "script_value is not a float or integer"
            );
        }

        inline checked_result<script_bool> checked_as_bool() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_bool_type) {
                return checked_result<script_bool>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a boolean"
                );
            }
            if (val.cpp_bound_ptr_) {
                return checked_result<script_bool>(*static_cast<const script_bool*>(val.cpp_bound_ptr_));
            }
            return checked_result<script_bool>(std::get<script_bool>(val.storage_));
        }

        inline checked_result<script_char> checked_as_char() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_char_type) {
                return checked_result<script_char>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a character"
                );
            }
            if (val.cpp_bound_ptr_) {
                return checked_result<script_char>(*static_cast<const script_char*>(val.cpp_bound_ptr_));
            }
            return checked_result<script_char>(std::get<script_char>(val.storage_));
        }

        // ============================================================================
        // COMPREHENSIVE checked_as<T>() - Returns checked_result instead of throwing
        // ============================================================================
        template<typename T>
        checked_result<T> checked_as() const {
            // Try to unwrap transparent wrappers for basic types (int, float, bool, string, char)
            // This allows observable_property_ref<int> to be used as int in C++ code
            if constexpr (std::is_same_v<T, script_int> || std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, script_float> || std::is_same_v<T, double> || std::is_same_v<T, float> ||
                          std::is_same_v<T, script_bool> || std::is_same_v<T, script_char> ||
                          std::is_same_v<T, script_string> || std::is_same_v<T, std::string>) {
                const script_value& val = deref();
                if (val.is_object()) {
                    script_value unwrapped = val.try_unwrap_transparent_wrapper();
                    // If unwrapped to a different value (not an object anymore), use that
                    if (!unwrapped.is_object()) {
                        return unwrapped.checked_as<T>();
                    }
                }
            }

            // FAST PATH: Direct type specializations for hot types
            if constexpr (std::is_same_v<T, script_int>) {
                const script_value& val = deref();
                if (val.cpp_bound_ptr_) {
                    return checked_result<T>(val.unchecked_as_int());
                }
                if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(std::get<script_int>(val.storage_));
                } else if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(float_to_script_int(std::get<script_float>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not an integer or float. Actual type: {0}",
                    val.type_id()
                );
            }
            else if constexpr (std::is_same_v<T, script_float> || std::is_same_v<T, double>) {
                const script_value& val = deref();
                // Decode bound values via the size/sign-aware accessors. The old
                // blanket `*static_cast<const script_float*>(cpp_bound_ptr_)` read 8
                // bytes for ANY bound value, reinterpreting bound ints and 4-byte
                // floats as doubles (wrong value, and an out-of-bounds read for float).
                if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(val.cpp_bound_ptr_ ? val.unchecked_as_float()
                                                                : std::get<script_float>(val.storage_));
                } else if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(static_cast<script_float>(
                        val.cpp_bound_ptr_ ? val.unchecked_as_int()
                                           : std::get<script_int>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a float or integer. Actual type: {0}",
                    val.type_id()
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
                    "script_value is not a boolean. Actual type: {0}",
                    val.type_id()
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
                    "script_value is not a character. Actual type: {0}",
                    val.type_id()
                );
            }
            // MEDIUM PATH: Common integral conversions
            else if constexpr (std::is_same_v<T, int>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                return checked_result<T>(static_cast<int64_t>(val_result.value()));
            }
            else if constexpr (std::is_same_v<T, float>) {
                const script_value& val = deref();
                if (val.type() == script_value_type::jai_float_type) {
                    if (val.cpp_bound_ptr_) {
                        return checked_result<T>(static_cast<float>(val.unchecked_as_float()));
                    }
                    return checked_result<T>(static_cast<float>(std::get<script_float>(val.storage_)));
                } else if (val.type() == script_value_type::jai_int_type) {
                    if (val.cpp_bound_ptr_) {
                        return checked_result<T>(static_cast<float>(val.unchecked_as_int()));
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
                    return str_result.error_value();  // Preserves symbol IDs for formatting
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
                            return str_result.error_value();  // Preserves symbol IDs for formatting
                        }
                        return checked_result<T>(*str_result.value());
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        auto arr_result = checked_as_array();
                        if (!arr_result) {
                            return arr_result.error_value();  // Preserves symbol IDs for formatting
                        }
                        return checked_result<T>(*arr_result.value());
                    } else if constexpr (std::is_same_v<base_type, std::map<script_value, script_value>>) {
                        auto map_result = checked_as_map();
                        if (!map_result) {
                            return map_result.error_value();
                        }
                        return checked_result<T>(*map_result.value());
                    } else {
                        auto t = type();
                        if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                            auto ptr_result = checked_as<std::shared_ptr<base_type>>();
                            if (!ptr_result) {
                                return ptr_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                    return val_result.error_value();  // Preserves symbol IDs for formatting
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
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
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
                    return arr_result.error_value();  // Preserves symbol IDs for formatting
                }
                const auto& arr = *arr_result.value();
                result.reserve(arr.size());

                for (const auto& elem : arr) {
                    if constexpr (std::is_same_v<element_type, script_value>) {
                        result.push_back(elem);
                    } else {
                        auto elem_result = elem.checked_as<element_type>();
                        if (!elem_result) {
                            return elem_result.error_value();
                        }
                        result.push_back(elem_result.value());
                    }
                }
                return checked_result<T>(std::move(result));
            }
            // Map types
            else if constexpr (is_specialization_v<T, std::map>) {
                // Use engine's conversion registry first
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
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
                    return map_result.error_value();
                }
                const auto& m = *map_result.value();

                for (const auto& [k, v] : m) {
                    if constexpr (std::is_same_v<key_type, script_value> && std::is_same_v<mapped_type, script_value>) {
                        result.emplace(k, v);
                    } else if constexpr (std::is_same_v<key_type, script_value>) {
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return val_result.error_value();
                        }
                        result.emplace(k, val_result.value());
                    } else if constexpr (std::is_same_v<mapped_type, script_value>) {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return key_result.error_value();
                        }
                        result.emplace(key_result.value(), v);
                    } else {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return key_result.error_value();
                        }
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return val_result.error_value();
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
                        "script_value is not an object. Actual type: {0}",
                        type_id()
                    );
                }
                auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
                if (!objHolder->is_class_instance_wrapper) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_a_class),
                        "Object is not a class_instance. Actual type: {0}",
                        type_id()
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
                auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
                return checked_result<T>(objHolder->data);
            }
            // Support for other shared_ptr types
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                // First check if there's a registered conversion
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
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
                    auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

                    // Check for non-owning C++ reference (cpp_bound_ptr_ set, data null)
                    // Cannot safely return shared_ptr from non-owning reference
                    if (cpp_bound_ptr_ && !objHolder->data) {
                        return checked_result<T>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Cannot extract shared_ptr from non-owning C++ reference. "
                            "The object is bound by reference, not owned. Use T& or T* extraction."
                        );
                    }

                    // Check if we need to use the custom extractor
                    if (objHolder->is_class_instance_wrapper) {
                        if (engine_) {
                            auto registry = get_engine_conversion_registry(engine_);
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
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
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
                    auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

                    // Handle non-owning C++ reference via cpp_bound_ptr_
                    if (cpp_bound_ptr_ && !objHolder->data) {
                        // Copy from non-owning pointer (only if copyable)
                        if constexpr (std::is_copy_constructible_v<T>) {
                            return checked_result<T>(*static_cast<T*>(cpp_bound_ptr_));
                        } else {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Cannot copy non-copyable type from non-owning C++ reference"
                            );
                        }
                    }

                    // Owning path: extract shared_ptr and dereference
                    auto ptr_result = checked_as<std::shared_ptr<T>>();
                    if (!ptr_result) {
                        return ptr_result.error_value();  // Preserves symbol IDs for formatting
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

        // Try to unwrap a transparent wrapper type (like observable_property_ref)
        // Returns the unwrapped value if this is a transparent wrapper, otherwise returns copy of this
        // Used by checked_as<T>() to automatically unwrap wrapper types
        script_value try_unwrap_transparent_wrapper() const;

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
        
        // Get engine pointer for creating new script_values
        engine* get_engine() const { return engine_; }

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
        engine* engine_ = nullptr;  // Raw pointer to engine (no atomic ops on copy)
        
        // For object storage - external serialization will handle this
        struct object_holder {
            std::string type_name;           // Type identification for serialization
            uint64_t type_id = UINT64_MAX;  // Interned type name ID for fast comparison (UINT64_MAX = not set)
            std::shared_ptr<void> data;     // The actual object
            bool is_class_instance_wrapper = false;  // True if data is a class_instance object (both C++ and script classes), false for raw data

            // Lifetime anchor for NON-OWNING references (data == nullptr, cpp_bound_ptr_ set).
            // When a C++ method returns a reference into its receiver (e.g. `return *this`
            // for chaining), the resulting non-owning value must keep the receiver's
            // underlying object alive even if the receiver was a temporary. This pins
            // that owner WITHOUT participating in extraction (cpp_bound_ptr_ remains the
            // authoritative target). Empty for owning objects.
            std::shared_ptr<void> keep_alive;

            // Note: Serialization functions will be managed externally
            // by ISerializer implementations to keep JaiScript dependency-free
        };
        
        // Reference wrapper for reference types
        struct reference_holder {
            script_value* target = nullptr;  // Points to the referenced value (when container is null)
            std::weak_ptr<environment> sourceEnv;  // environment that owns the target
            type_info_ptr container_element_type = nullptr;  // For array/map subscript refs: the element type constraint

            // For references into a std::vector element (range-for `auto&`, `arr[i]`
            // lvalue): hold the OWNING container + index instead of a raw element
            // pointer. A raw pointer into a vector dangles when the vector reallocates
            // (e.g. a push inside the loop body) -> heap corruption on write-through.
            // When `container` is set, deref()/assign_through() recompute the element
            // address from container+index each time (surviving reallocation) and
            // bounds-check the index (a shrink throws instead of reading freed memory).
            // The strong_ptr also keeps the vector alive for the reference's lifetime.
            strong_ptr<std::vector<script_value>> container;
            size_t container_index = SIZE_MAX;
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

    public:
        // Constexpr type index constants for fast type checking in hot paths
        // Use with raw_storage_index() to avoid switch statements
        static constexpr size_t TYPEID_NULL = 0;
        static constexpr size_t TYPEID_INT = 1;
        static constexpr size_t TYPEID_FLOAT = 2;
        static constexpr size_t TYPEID_STRING = 3;
        static constexpr size_t TYPEID_CHAR = 4;
        static constexpr size_t TYPEID_BOOL = 5;
        static constexpr size_t TYPEID_ARRAY = 6;
        static constexpr size_t TYPEID_MAP = 7;
        static constexpr size_t TYPEID_OBJECT = 8;
        static constexpr size_t TYPEID_FUNCTION = 9;
        static constexpr size_t TYPEID_REFERENCE = 10;
        static constexpr size_t TYPEID_SHARED_PTR = 11;
        static constexpr size_t TYPEID_WEAK_PTR = 12;
        static constexpr size_t TYPEID_INVALID = 13;
    private:

        // Type-erased storage using variant for efficiency
        // NOTE: string and function are wrapped in strong_ptr for cheap non-atomic copies
        // This shrinks the variant (all complex types are now 16-byte pointers)
        // and makes copies O(1) instead of O(n) for strings
        // strong_ptr uses non-atomic ref counting (fast!) until lock() is called
        using storage = std::variant<
            std::monostate,                               // 0 - Null
            script_int,                                   // 1 - script_int
            script_float,                                 // 2 - script_float
            strong_ptr<script_string>,                    // 3 - script_string (wrapped for cheap copies)
            script_char,                                  // 4 - script_char
            script_bool,                                  // 5 - script_bool
            strong_ptr<std::vector<script_value>>,        // 6 - Array<T>
            strong_ptr<std::map<script_value, script_value>>, // 7 - Map<K,V>
            strong_ptr<object_holder>,                    // 8 - Object<T>
            strong_ptr<script_function>,                  // 9 - Function (wrapped for cheap copies)
            strong_ptr<reference_holder>,                 // 10 - T&
            std::shared_ptr<script_value>,                // 11 - shared_ptr<T> (user-level, keeps std::shared_ptr for thread safety)
            jai::weaker_ptr<object_holder>,                 // 12 - weak_ptr<T>
            invalid_tag                                   // 13 - Invalid value marker
        >;
        
        storage storage_;
        void* cpp_bound_ptr_ = nullptr;  // If non-null, this value is bound to a C++ variable
        uint8_t cpp_bound_type_size_ = 0; // Low 7 bits: sizeof(T), bit 7: unsigned flag
        
    public:
        // Method to set engine pointer after construction
        void set_engine(engine* eng) { engine_ = eng; }

        // Clear engine pointer (called when engine is destroyed to prevent dangling pointers)
        void clear_engine() { engine_ = nullptr; }

        // Access raw storage for AST literals (bypasses type checking)
        const storage& get_storage() const { return storage_; }
        
        // Extract object_holder for class_instance operations
        // Returns nullptr if not actually storing an object (uses current_type, not defined_type)
        strong_ptr<object_holder> get_object_holder() {
            auto t = current_type();  // Use actual current type, not declared type
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<strong_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }

        const strong_ptr<object_holder> get_object_holder() const {
            auto t = current_type();  // Use actual current type, not declared type
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<strong_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }

        // Set object holder directly (used when restoring from weak_ptr.lock())
        void set_object_holder(strong_ptr<object_holder> holder) {
            storage_ = holder;
        }

        // Get raw array storage for interpreter operations
        strong_ptr<std::vector<script_value>>& get_array_storage() {
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return std::get<strong_ptr<std::vector<script_value>>>(storage_);
        }

        // Get raw map storage for interpreter operations
        strong_ptr<std::map<script_value, script_value>>& get_map_storage() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
        }

        // Get raw weak_ptr storage for interpreter operations
        jai::weaker_ptr<object_holder>& get_weak_ptr_storage() {
            if (type() != script_value_type::jai_weak_ptr_type) {
                throw runtime_error("script_value is not a weak_ptr");
            }
            return std::get<jai::weaker_ptr<object_holder>>(storage_);
        }
        
        // ===== Safe Public APIs for Interpreter Operations =====
        
        // Factory method for null values with engine pointer
        static script_value make_null(engine* eng) {
            return script_value(std::monostate{}, eng);
        }

        // Factory method for invalid values (used as sentinel for non-existent fields/methods)
        static script_value make_invalid(engine* eng);
        
        // Check if this value has a valid engine pointer
        bool has_valid_engine() const {
            return engine_ != nullptr;
        }

        // Safe access to reference holder for reference types
        // Use storage_type() because references may have type_info with different base_type
        reference_holder* get_reference_holder() {
            if (storage_type() == script_value_type::jai_reference_type) {
                return std::get<strong_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }

        const reference_holder* get_reference_holder() const {
            if (storage_type() == script_value_type::jai_reference_type) {
                return std::get<strong_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }
        
        
        // Check if array/map has unique ownership (for COW optimization)
        bool is_unique_reference() const {
            if (type() == script_value_type::jai_array_type) {
                const auto& ptr = std::get<strong_ptr<std::vector<script_value>>>(storage_);
                return ptr.use_count() == 1;
            } else if (type() == script_value_type::jai_map_type) {
                const auto& ptr = std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
                return ptr.use_count() == 1;
            }
            return false;
        }

        // Safe weak_ptr access
        jai::weaker_ptr<object_holder> get_weak_ptr() const {
            if (type() == script_value_type::jai_weak_ptr_type) {
                return std::get<jai::weaker_ptr<object_holder>>(storage_);
            }
            return jai::weaker_ptr<object_holder>();
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
        void set_weak_ptr(const jai::weaker_ptr<object_holder>& weak) {
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
        static script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type, engine* eng);

        // Create a weak_ptr from an object
        // Create a weak_ptr from an object (implemented in value.cpp)
        // Returns checked_result to avoid throwing exceptions
        static checked_result<script_value> make_weak_ptr(const script_value& obj, engine* eng);
        
        // Set type info for special cases (like weak_ptr creation)
        void set_type_info(type_info_ptr type) {
            type_info_ = type;
        }
        
    private:
        // Friends only for essential access patterns
        friend class interpreter;  // For coroutine handle creation (object_holder access)
        template<typename T> friend class dynamic_binder;  // For make_cpp_object
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

