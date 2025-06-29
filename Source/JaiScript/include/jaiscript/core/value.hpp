#pragma once

#include "types.hpp"
#include "type_info.hpp"
#include "../jaiscript_fwd.hpp"
#include <variant>
#include <memory>
#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <cmath>

namespace jai {

    // Forward declaration
    class environment;
    
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
        // Constructors for each type
        script_value() : type_info_(nullptr), storage_(std::monostate{}) {
            // Null value
        }
        script_value(std::nullptr_t) : script_value() {}
        
        // Primary constructors for our types
        script_value(script_int i) : type_info_(type_info::make_int()), storage_(i) {}
        script_value(script_float f) : type_info_(type_info::make_float()), storage_(f) {}
        script_value(const script_string& s) : type_info_(type_info::make_string()), storage_(s) {}
        script_value(script_string&& s) : type_info_(type_info::make_string()), storage_(std::move(s)) {}
        script_value(const char* s) : type_info_(type_info::make_string()), storage_(script_string(s)) {}
        script_value(script_char c) : type_info_(type_info::make_char()), storage_(c) {}
        script_value(script_bool b) : type_info_(type_info::make_bool()), storage_(b) {}
        
        // Template constructor for integral types (except bool and char)
        // This handles all integer types safely without ambiguity
        template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
        script_value(T i) : type_info_(type_info::make_int()), storage_(script_int(i)) {}
        
        // Handle float conversions (script_float is double, so we need float constructor)
        template<typename T>
        requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
        script_value(T f) : type_info_(type_info::make_float()), storage_(script_float(f)) {}
        
        // Move constructor and assignment operator for performance
        script_value(script_value&& other) noexcept 
            : type_info_(std::move(other.type_info_)), storage_(std::move(other.storage_)) {
            // Reset the moved-from object to a valid null state
            other.type_info_ = nullptr;
            other.storage_ = std::monostate{};
        }
        
        script_value& operator=(script_value&& other) noexcept {
            if (this != &other) {
                type_info_ = std::move(other.type_info_);
                storage_ = std::move(other.storage_);
                // Reset the moved-from object to a valid null state
                other.type_info_ = nullptr;
                other.storage_ = std::monostate{};
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
        static script_value make_cpp_object(const std::string& type_name, std::shared_ptr<void> data); // For raw C++ objects
        static script_value make_shared_ptr(const script_value& value);
        static script_value make_weak_ptr(const script_value& value);
        static script_value make_reference(script_value* target, const std::shared_ptr<environment>& env);
        static script_value make_function(const script_function& func);
        
        // Type information
        type_info_ptr get_type_info() const { return type_info_; }
        value_type type() const { return type_info_ ? type_info_->base_type : value_type::jai_null_type; }
        bool is_null() const { return deref().type() == value_type::jai_null_type; }
        bool is_int() const { return deref().type() == value_type::jai_int_type; }
        bool is_float() const { return deref().type() == value_type::jai_float_type; }
        bool is_string() const { return deref().type() == value_type::jai_string_type; }
        bool is_char() const { return deref().type() == value_type::jai_char_type; }
        bool is_bool() const { return deref().type() == value_type::jai_bool_type; }
        bool is_array() const { return deref().type() == value_type::jai_array_type; }
        bool is_map() const { return deref().type() == value_type::jai_map_type; }
        bool is_object() const { return deref().type() == value_type::jai_object_type; }
        bool is_function() const { return deref().type() == value_type::jai_function_type; }
        bool is_reference() const { return type() == value_type::jai_reference_type; }  // Don't deref for this check!
        bool is_shared_ptr() const { return deref().type() == value_type::jai_shared_ptr_type; }
        bool is_weak_ptr() const { return deref().type() == value_type::jai_weak_ptr_type; }
        
        // script_value extraction (inlined for performance)
        inline script_int as_int() const {
            const script_value& val = deref();
            if (val.type() != value_type::jai_int_type) {
                throw runtime_error("script_value is not an integer");
            }
            return std::get<script_int>(val.storage_);
        }
        
        inline script_float as_float() const {
            const script_value& val = deref();
            if (val.type() != value_type::jai_float_type) {
                throw runtime_error("script_value is not a float");
            }
            return std::get<script_float>(val.storage_);
        }
        
        inline const script_string& as_string() const {
            const script_value& val = deref();
            if (val.type() != value_type::jai_string_type) {
                throw runtime_error("script_value is not a string");
            }
            return std::get<script_string>(val.storage_);
        }
        
        inline script_bool as_bool() const {
            const script_value& val = deref();
            if (val.type() != value_type::jai_bool_type) {
                throw runtime_error("script_value is not a boolean");
            }
            return std::get<script_bool>(val.storage_);
        }
        
        inline script_char as_char() const {
            const script_value& val = deref();
            if (val.type() != value_type::jai_char_type) {
                throw runtime_error("script_value is not a character");
            }
            return std::get<script_char>(val.storage_);
        }
        
        inline const std::vector<script_value>& as_array() const {
            if (type() != value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
        }
        
        inline const std::map<script_value, script_value>& as_map() const {
            if (type() != value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return *std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
        }
        
        const script_function& as_function() const;
        
        // Generic extraction with type checking
        template<typename T>
        T as() const {
            // Handle reference types
            if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;
                
                // For const references, we can return references to our internal data
                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        if (type() != value_type::jai_int_type) {
                            throw runtime_error("script_value is not an integer");
                        }
                        return std::get<script_int>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_float>) {
                        if (type() != value_type::jai_float_type) {
                            throw runtime_error("script_value is not a float");
                        }
                        return std::get<script_float>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        if (type() != value_type::jai_bool_type) {
                            throw runtime_error("script_value is not a boolean");
                        }
                        return std::get<script_bool>(storage_);
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        if (type() != value_type::jai_char_type) {
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
                        // For user-defined types stored as objects
                        if (type_info_ && type_info_->is_object()) {
                            auto objectPtr = std::get<std::shared_ptr<void>>(storage_);
                            // Try to cast to the requested type
                            if (auto typedPtr = std::static_pointer_cast<base_type>(objectPtr)) {
                                return *typedPtr;
                            }
                            throw runtime_error("Object type mismatch");
                        }
                        throw runtime_error("Unsupported type for const reference extraction");
                    }
                } else {
                    // For non-const references, we can't return references to our internal data
                    // as it would break const-correctness, so we throw an error
                    throw runtime_error("Cannot extract non-const reference from const script_value");
                }
            }
            // Handle value types
            else if constexpr (std::is_same_v<T, script_int>) {
                return as_int();
            } else if constexpr (std::is_same_v<T, script_float>) {
                return as_float();
            } else if constexpr (std::is_same_v<T, script_string>) {
                return as_string();
            } else if constexpr (std::is_same_v<T, script_char>) {
                return as_char();
            } else if constexpr (std::is_same_v<T, script_bool>) {
                return as_bool();
            }
            // Allow common C++ type conversions with bounds checking
            else if constexpr (std::is_same_v<T, int>) {
                script_int val = as_int();
                if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
                    throw runtime_error("Integer value out of range for int");
                }
                return static_cast<int>(val);
            } else if constexpr (std::is_same_v<T, int8_t>) {
                script_int val = as_int();
                if (val < std::numeric_limits<int8_t>::min() || val > std::numeric_limits<int8_t>::max()) {
                    throw runtime_error("Integer value out of range for int8_t");
                }
                return static_cast<int8_t>(val);
            } else if constexpr (std::is_same_v<T, int16_t>) {
                script_int val = as_int();
                if (val < std::numeric_limits<int16_t>::min() || val > std::numeric_limits<int16_t>::max()) {
                    throw runtime_error("Integer value out of range for int16_t");
                }
                return static_cast<int16_t>(val);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                script_int val = as_int();
                if (val < std::numeric_limits<int32_t>::min() || val > std::numeric_limits<int32_t>::max()) {
                    throw runtime_error("Integer value out of range for int32_t");
                }
                return static_cast<int32_t>(val);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return static_cast<int64_t>(as_int());  // No bounds check needed, same size
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                script_int val = as_int();
                if (val < 0 || val > std::numeric_limits<uint8_t>::max()) {
                    throw runtime_error("Integer value out of range for uint8_t (must be 0-255)");
                }
                return static_cast<uint8_t>(val);
            } else if constexpr (std::is_same_v<T, uint16_t>) {
                script_int val = as_int();
                if (val < 0 || val > std::numeric_limits<uint16_t>::max()) {
                    throw runtime_error("Integer value out of range for uint16_t (must be non-negative)");
                }
                return static_cast<uint16_t>(val);
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                script_int val = as_int();
                if (val < 0 || val > std::numeric_limits<uint32_t>::max()) {
                    throw runtime_error("Integer value out of range for uint32_t (must be non-negative)");
                }
                return static_cast<uint32_t>(val);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                script_int val = as_int();
                if (val < 0) {
                    throw runtime_error("Integer value must be non-negative for uint64_t");
                }
                return static_cast<uint64_t>(val);
            } else if constexpr (std::is_same_v<T, size_t>) {
                script_int val = as_int();
                if (val < 0) {
                    throw runtime_error("Integer value must be non-negative for size_t");
                }
                return static_cast<size_t>(val);
            } else if constexpr (std::is_same_v<T, float>) {
                // Handle both float and int types for automatic conversion
                if (type() == value_type::jai_float_type) {
                    return static_cast<float>(as_float());
                } else if (type() == value_type::jai_int_type) {
                    return static_cast<float>(as_int());  // Convert int to float
                } else {
                    throw runtime_error("Cannot convert script_value to float");
                }
            } else if constexpr (std::is_same_v<T, double>) {
                // Handle both float and int types for automatic conversion
                if (type() == value_type::jai_float_type) {
                    return as_float();  // script_float is already double
                } else if (type() == value_type::jai_int_type) {
                    return static_cast<double>(as_int());  // Convert int to double
                } else {
                    throw runtime_error("Cannot convert script_value to double");
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                return as_string();  // script_string is already std::string
            }
            // Support for shared_ptr extraction from objects
            else if constexpr (std::is_same_v<T, std::shared_ptr<class_instance>>) {
                if (type() != value_type::jai_object_type) {
                    throw runtime_error("script_value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                if (!objHolder->is_cpp_class_instance) {
                    throw runtime_error("Object is not a class_instance");
                }
                return std::static_pointer_cast<class_instance>(objHolder->data);
            }
            // Support for shared_ptr<void> extraction
            else if constexpr (std::is_same_v<T, std::shared_ptr<void>>) {
                if (type() != value_type::jai_object_type) {
                    throw runtime_error("script_value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                return objHolder->data;
            }
            // Support for shared_ptr<user_type> extraction from objects
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                if (type() != value_type::jai_object_type) {
                    throw runtime_error("script_value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<object_holder>>(storage_);
                
                // Check if we need to use the custom extractor (for class_instance wrapping)
                // Only use extractor if this is a class_instance wrapper
                if (custom_extractor_ && objHolder->is_cpp_class_instance) {
                    auto extracted = custom_extractor_(objHolder->type_name, objHolder->data);
                    if (extracted) {
                        return std::static_pointer_cast<typename T::element_type>(extracted);
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
                // For custom classes, try to extract shared_ptr and dereference
                if (type() == value_type::jai_object_type) {
                    auto ptr = as<std::shared_ptr<T>>();
                    return *ptr;
                }
                throw runtime_error("Cannot extract custom type by value from non-object");
            }
            else {
                throw runtime_error("Unsupported type conversion");
            }
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
                    // For non-const references to our internal data
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        // Handle float-to-int narrowing conversion (like C++)
                        if (type() == value_type::jai_int_type) {
                            return std::get<script_int>(storage_);
                        } else if (type() == value_type::jai_float_type) {
                            auto float_val = std::get<script_float>(storage_);
                            // Check for overflow and NaN/inf (script_float is double)
                            if (std::isnan(float_val) || std::isinf(float_val)) {
                                throw runtime_error("Cannot convert NaN or infinity to integer");
                            }
                            if (float_val < std::numeric_limits<script_int>::min() || 
                                float_val > std::numeric_limits<script_int>::max()) {
                                throw runtime_error("Double value out of range for integer conversion");
                            }
                            return static_cast<script_int>(float_val);
                        } else {
                            throw runtime_error("script_value is not an integer or double");
                        }
                    } else if constexpr (std::is_same_v<base_type, script_float>) {
                        // Handle int-to-float conversion (like C++)
                        if (type() == value_type::jai_float_type) {
                            return std::get<script_float>(storage_);
                        } else if (type() == value_type::jai_int_type) {
                            return static_cast<script_float>(std::get<script_int>(storage_));
                        } else {
                            throw runtime_error("script_value is not a float or integer");
                        }
                    } else if constexpr (std::is_same_v<base_type, double>) {
                        // Handle both float and int types for automatic conversion (for references)
                        if (type() == value_type::jai_float_type) {
                            return std::get<script_float>(storage_);  // script_float is already double
                        } else if (type() == value_type::jai_int_type) {
                            return static_cast<double>(std::get<script_int>(storage_));  // Convert int to double
                        } else {
                            throw runtime_error("Cannot convert script_value to double (non-const ref)");
                        }
                    } else if constexpr (std::is_same_v<base_type, float>) {
                        // Handle double-to-float and int-to-float conversion (like C++)
                        if (type() == value_type::jai_float_type) {
                            auto double_val = std::get<script_float>(storage_);  // script_float is double
                            // Check for overflow (double to float narrowing)
                            if (std::isnan(double_val) || std::isinf(double_val)) {
                                return static_cast<float>(double_val);  // Preserve NaN/inf
                            }
                            if (std::abs(double_val) > std::numeric_limits<float>::max()) {
                                return std::copysign(std::numeric_limits<float>::infinity(), double_val);
                            }
                            return static_cast<float>(double_val);
                        } else if (type() == value_type::jai_int_type) {
                            return static_cast<float>(std::get<script_int>(storage_));  // Convert int to float
                        } else {
                            throw runtime_error("Cannot convert script_value to float (non-const ref)");
                        }
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        // Handle int/float-to-bool conversion (like C++)
                        if (type() == value_type::jai_bool_type) {
                            return std::get<script_bool>(storage_);
                        } else if (type() == value_type::jai_int_type) {
                            return std::get<script_int>(storage_) != 0;  // 0 = false, non-zero = true
                        } else if (type() == value_type::jai_float_type) {
                            auto float_val = std::get<script_float>(storage_);
                            return float_val != 0.0 && !std::isnan(float_val);  // 0.0 or NaN = false
                        } else {
                            throw runtime_error("script_value is not a boolean, integer, or float");
                        }
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        // Handle int-to-char conversion (like C++)
                        if (type() == value_type::jai_char_type) {
                            return std::get<script_char>(storage_);
                        } else if (type() == value_type::jai_int_type) {
                            auto int_val = std::get<script_int>(storage_);
                            // Check char range
                            if (int_val < std::numeric_limits<char>::min() || 
                                int_val > std::numeric_limits<char>::max()) {
                                throw runtime_error("Integer value out of range for char conversion");
                            }
                            return static_cast<script_char>(int_val);
                        } else {
                            throw runtime_error("script_value is not a character or integer");
                        }
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        if (type() != value_type::jai_string_type) {
                            throw runtime_error("script_value is not a string");
                        }
                        return std::get<script_string>(storage_);
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        if (type() != value_type::jai_array_type) {
                            throw runtime_error("script_value is not an array");
                        }
                        return *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
                    } else if constexpr (std::is_same_v<base_type, std::map<script_value, script_value>>) {
                        if (type() != value_type::jai_map_type) {
                            throw runtime_error("script_value is not a map");
                        }
                        return *std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
                    } else {
                        // For user-defined types stored as objects
                        if (type_info_ && type_info_->is_object()) {
                            auto objectPtr = std::get<std::shared_ptr<void>>(storage_);
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
        
        // Custom extractor for unwrapping objects (e.g., extracting C++ objects from class_instance)
        using extractor_func = std::function<std::shared_ptr<void>(const std::string&, std::shared_ptr<void>)>;
        
        // Set custom extractor for unwrapping objects
        static void set_custom_extractor(extractor_func extractor) {
            custom_extractor_ = std::move(extractor);
        }
        
        // Implicit conversion operators for common C++ types
        // These enable natural overload resolution in C++ function calls
        operator int() const {
            const script_value& val = deref();
            if (val.type() == value_type::jai_int_type) {
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
            if (val.type() == value_type::jai_int_type) {
                return std::get<script_int>(val.storage_);
            }
            throw runtime_error("script_value is not an integer");
        }
        
        operator double() const {
            const script_value& val = deref();
            if (val.type() == value_type::jai_float_type) {
                return std::get<script_float>(val.storage_);
            } else if (val.type() == value_type::jai_int_type) {
                return static_cast<double>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        operator float() const {
            const script_value& val = deref();
            if (val.type() == value_type::jai_float_type) {
                return static_cast<float>(std::get<script_float>(val.storage_));
            } else if (val.type() == value_type::jai_int_type) {
                return static_cast<float>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        
    private:
        type_info_ptr type_info_;  // Complete type information
        
        // For object storage - external serialization will handle this
        struct object_holder {
            std::string type_name;           // Type identification for serialization
            std::shared_ptr<void> data;     // The actual object
            bool is_cpp_class_instance = false;  // True if data is a class_instance wrapping a C++ object
            
            // Note: Serialization functions will be managed externally
            // by ISerializer implementations to keep JaiScript dependency-free
        };
        
        // Reference wrapper for reference types
        struct reference_holder {
            script_value* target;  // Points to the referenced value
            std::weak_ptr<environment> sourceEnv;  // environment that owns the target
        };
        
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
            std::weak_ptr<script_value>                          // weak_ptr<T>
        >;
        
        storage storage_;
        
        // Custom extractor static member
        static inline extractor_func custom_extractor_;
        
        friend class engine;
        friend class interpreter;
        friend class class_instance;
        friend class serialization::binary_archive_writer;
        friend class serialization::binary_archive_reader;
        friend class serialization::json_archive_writer;
        friend class serialization::json_archive_reader;
    };
    
} // namespace jai