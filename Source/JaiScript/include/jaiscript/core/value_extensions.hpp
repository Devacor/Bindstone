#pragma once

#include "jaiscript/core/value.hpp"
#include "jaiscript/core/script_class.hpp"

namespace jai {

// Enhanced value_type enum for class support
enum class enhanced_value_type {
    // Existing types (would extend the existing enum)
    null_type, int_type, float_type, string_type, char_type, bool_type,
    array_type, map_type, object_type, function_type, reference_type, 
    shared_ptr_type, weak_ptr_type,
    
    // New class-related types
    script_class,      // Instance of script-defined class
    cpp_class,         // Instance of C++-bound class
    class_definition   // Class type itself (for static access)
};

// Extensions to script_value class for class support
class script_value_class_extensions {
public:
    // Check if value is an instance of a specific class
    static bool is_instance_of(const script_value& value, const std::string& class_name);
    
    // Get the class name of this instance
    static std::string get_class_name(const script_value& value);
    
    // Check class inheritance
    static bool is_derived_from(const script_value& value, const std::string& base_class_name);
    
    // Create script_value from script class instance
    static script_value make_script_class_instance(std::shared_ptr<script_class_instance> instance);
    
    // Extract script class instance from script_value
    static std::shared_ptr<script_class_instance> as_script_class_instance(const script_value& value);
    
    // Check if a class instance has a C++ base object (hybrid class)
    static bool has_cpp_base(const script_value& value);
    
    // Get the C++ base object from a hybrid class instance
    template<typename T>
    static std::shared_ptr<T> get_cpp_base(const script_value& value) {
        auto instance = as_script_class_instance(value);
        if (instance && instance->has_cpp_object()) {
            return instance->get_cpp_object_as<T>();
        }
        return nullptr;
    }
    
    // Check if value holds a script class instance
    static bool is_script_class_instance(const script_value& value);
    
    // Check if value holds a hybrid class instance
    static bool is_hybrid_class_instance(const script_value& value);
    
    // Method call support
    static script_value call_method(
        const script_value& instance_value,
        const std::string& method_name,
        const std::vector<script_value>& args
    );
    
    // Field access support
    static script_value get_field(
        const script_value& instance_value,
        const std::string& field_name
    );
    
    static void set_field(
        script_value& instance_value,
        const std::string& field_name,
        const script_value& field_value
    );
};

// Type conversion helpers for class instances
class class_type_converter {
public:
    // Convert between script_value and class instances
    template<typename T>
    static script_value to_script_value(std::shared_ptr<T> instance);
    
    template<typename T>
    static std::shared_ptr<T> from_script_value(const script_value& value);
    
    // Check type compatibility
    static bool can_convert_to_class(const script_value& value, const std::string& target_class);
    
    // Perform inheritance-aware conversions
    static script_value convert_to_base_class(
        const script_value& derived_instance,
        const std::string& base_class_name
    );
    
    // Smart pointer conversions
    static script_value make_shared_ptr_value(std::shared_ptr<script_class_instance> instance);
    static script_value make_weak_ptr_value(std::weak_ptr<script_class_instance> instance);
    
    static std::shared_ptr<script_class_instance> as_shared_ptr(const script_value& value);
    static std::weak_ptr<script_class_instance> as_weak_ptr(const script_value& value);
};

// Operators for class instances
class class_operators {
public:
    // Equality comparison
    static bool equals(const script_value& lhs, const script_value& rhs);
    
    // Assignment operator
    static script_value assign(script_value& lhs, const script_value& rhs);
    
    // Member access operator (.)
    static script_value member_access(
        const script_value& instance,
        const std::string& member_name
    );
    
    // Method call operator
    static script_value method_call(
        const script_value& instance,
        const std::string& method_name,
        const std::vector<script_value>& args
    );
    
    // Array subscript operator (if class supports it)
    static script_value subscript(
        const script_value& instance,
        const script_value& index
    );
    
    // Arithmetic operators (if overloaded in class)
    static script_value add(const script_value& lhs, const script_value& rhs);
    static script_value subtract(const script_value& lhs, const script_value& rhs);
    static script_value multiply(const script_value& lhs, const script_value& rhs);
    static script_value divide(const script_value& lhs, const script_value& rhs);
    
    // Comparison operators
    static bool less_than(const script_value& lhs, const script_value& rhs);
    static bool greater_than(const script_value& lhs, const script_value& rhs);
    static bool less_equal(const script_value& lhs, const script_value& rhs);
    static bool greater_equal(const script_value& lhs, const script_value& rhs);
};

} // namespace jai