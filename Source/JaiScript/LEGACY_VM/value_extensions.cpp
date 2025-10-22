#include "jaiscript/core/value_extensions.hpp"
#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/detail/interpreter.hpp"
#include <stdexcept>

namespace jai {

// script_value_class_extensions implementation

bool script_value_class_extensions::is_instance_of(const script_value& value, const std::string& class_name) {
    if (!is_script_class_instance(value)) {
        return false;
    }
    
    auto instance = as_script_class_instance(value);
    if (!instance) return false;
    
    // Get engine from script_value to access its class_registry
    auto eng = value.get_engine();
    if (!eng) {
        // Fallback to simple name comparison if no engine available
        return instance->class_name == class_name;
    }
    
    return instance->class_name == class_name || 
           eng->get_class_registry().is_assignable_from(instance->class_name, class_name);
}

std::string script_value_class_extensions::get_class_name(const script_value& value) {
    if (is_script_class_instance(value)) {
        auto instance = as_script_class_instance(value);
        return instance ? instance->class_name : "";
    } /* else if (is_hybrid_class_instance(value)) {
        // TODO: hybrid_class_instance not yet defined
        auto instance = as_hybrid_class_instance(value);
        return instance ? instance->class_name : "";
    } */
    
    return "";
}

bool script_value_class_extensions::is_derived_from(const script_value& value, const std::string& base_class_name) {
    if (!is_script_class_instance(value)) {
        return false;
    }
    
    auto instance = as_script_class_instance(value);
    if (!instance) return false;
    
    // Get engine from script_value to access its class_registry
    auto eng = value.get_engine();
    if (!eng) {
        // Fallback to simple name comparison if no engine available
        return instance->class_name == base_class_name;
    }
    
    return eng->get_class_registry().is_assignable_from(instance->class_name, base_class_name);
}

script_value script_value_class_extensions::make_script_class_instance(std::shared_ptr<script_class_instance> instance) {
    // This would integrate with the existing script_value implementation
    // For now, return a placeholder
    return script_value::make_object(instance);
}

std::shared_ptr<script_class_instance> script_value_class_extensions::as_script_class_instance(const script_value& value) {
    // This would extract from the existing script_value implementation
    // For now, return a placeholder
    if (value.type() == script_value_type::jai_object_type) {
        // Extract script_class_instance from object holder
        auto obj_holder = value.get_object_holder();
        if (obj_holder && obj_holder->is_cpp_class_instance) {
            return std::static_pointer_cast<script_class_instance>(obj_holder->data);
        }
    }
    return nullptr;
}

// TODO: hybrid_class_instance not yet defined
/*
script_value script_value_class_extensions::make_hybrid_class_instance(std::shared_ptr<hybrid_class_instance> instance) {
    return script_value::make_object(std::static_pointer_cast<script_class_instance>(instance));
}

std::shared_ptr<hybrid_class_instance> script_value_class_extensions::as_hybrid_class_instance(const script_value& value) {
    if (value.get_type() == script_value_type::object_type) {
        auto base_instance = std::static_pointer_cast<script_class_instance>(value.as_object());
        return std::dynamic_pointer_cast<hybrid_class_instance>(base_instance);
    }
    return nullptr;
}
*/

bool script_value_class_extensions::is_script_class_instance(const script_value& value) {
    if (value.type() != script_value_type::jai_object_type) {
        return false;
    }
    
    auto obj_holder = value.get_object_holder();
    if (obj_holder && obj_holder->is_cpp_class_instance) {
        auto instance = std::dynamic_pointer_cast<script_class_instance>(obj_holder->data);
        return instance != nullptr;
    }
    return false;
}

// TODO: hybrid_class_instance not yet defined
bool script_value_class_extensions::is_hybrid_class_instance(const script_value& value) {
    /*
    if (value.get_type() != script_value_type::object_type) {
        return false;
    }
    
    auto instance = std::dynamic_pointer_cast<hybrid_class_instance>(value.as_object());
    return instance != nullptr;
    */
    return false; // For now, no hybrid class instances exist
}

script_value script_value_class_extensions::call_method(
    const script_value& instance_value,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    auto instance = as_script_class_instance(instance_value);
    if (!instance) {
        throw std::runtime_error("Cannot call method on non-class instance");
    }
    
    return jaiscript::call_method(instance, method_name, args);
}

script_value script_value_class_extensions::get_field(
    const script_value& instance_value,
    const std::string& field_name
) {
    auto instance = as_script_class_instance(instance_value);
    if (!instance) {
        throw std::runtime_error("Cannot access field on non-class instance");
    }
    
    auto it = instance->fields.find(field_name);
    if (it != instance->fields.end()) {
        return it->second;
    }
    
    throw std::runtime_error("Field not found: " + field_name);
}

void script_value_class_extensions::set_field(
    script_value& instance_value,
    const std::string& field_name,
    const script_value& field_value
) {
    auto instance = as_script_class_instance(instance_value);
    if (!instance) {
        throw std::runtime_error("Cannot set field on non-class instance");
    }
    
    instance->fields[field_name] = field_value;
}

// class_type_converter implementation

template<typename T>
script_value class_type_converter::to_script_value(std::shared_ptr<T> instance) {
    if constexpr (std::is_base_of_v<script_class_instance, T>) {
        return script_value_class_extensions::make_script_class_instance(
            std::static_pointer_cast<script_class_instance>(instance)
        );
    } else {
        return script_value::make_object(instance);
    }
}

template<typename T>
std::shared_ptr<T> class_type_converter::from_script_value(const script_value& value) {
    if constexpr (std::is_base_of_v<script_class_instance, T>) {
        auto instance = script_value_class_extensions::as_script_class_instance(value);
        return std::dynamic_pointer_cast<T>(instance);
    } else {
        // Objects are already stored as shared_ptr internally
        auto obj_holder = value.get_object_holder();
        if (obj_holder) {
            return std::static_pointer_cast<T>(obj_holder->data);
        }
        return nullptr;
    }
}

bool class_type_converter::can_convert_to_class(const script_value& value, const std::string& target_class) {
    return script_value_class_extensions::is_instance_of(value, target_class);
}

script_value class_type_converter::convert_to_base_class(
    const script_value& derived_instance,
    const std::string& base_class_name
) {
    if (!script_value_class_extensions::is_derived_from(derived_instance, base_class_name)) {
        throw std::runtime_error("Cannot convert to unrelated base class");
    }
    
    // In a full implementation, this would create a view of the instance as the base class
    return derived_instance;
}

script_value class_type_converter::make_shared_ptr_value(std::shared_ptr<script_class_instance> instance) {
    return script_value::make_shared_ptr(instance);
}

script_value class_type_converter::make_weak_ptr_value(std::weak_ptr<script_class_instance> instance) {
    return script_value::make_weak_ptr(instance);
}

std::shared_ptr<script_class_instance> class_type_converter::as_shared_ptr(const script_value& value) {
    if (value.get_type() == script_value_type::shared_ptr_type) {
        return std::static_pointer_cast<script_class_instance>(value.as_shared_ptr());
    }
    return nullptr;
}

std::weak_ptr<script_class_instance> class_type_converter::as_weak_ptr(const script_value& value) {
    if (value.get_type() == script_value_type::weak_ptr_type) {
        return std::static_pointer_cast<script_class_instance>(value.as_weak_ptr().lock());
    }
    return {};
}

// class_operators implementation

bool class_operators::equals(const script_value& lhs, const script_value& rhs) {
    // Check if both are class instances
    if (script_value_class_extensions::is_script_class_instance(lhs) &&
        script_value_class_extensions::is_script_class_instance(rhs)) {
        
        auto lhs_instance = script_value_class_extensions::as_script_class_instance(lhs);
        auto rhs_instance = script_value_class_extensions::as_script_class_instance(rhs);
        
        // Reference equality by default
        return lhs_instance == rhs_instance;
    }
    
    // Fallback to default equality
    return lhs == rhs;
}

script_value class_operators::assign(script_value& lhs, const script_value& rhs) {
    lhs = rhs;
    return lhs;
}

script_value class_operators::member_access(
    const script_value& instance,
    const std::string& member_name
) {
    // Try field access first
    try {
        return script_value_class_extensions::get_field(instance, member_name);
    } catch (...) {
        // If field access fails, this might be a method access
        // In a full implementation, would return a method handle
        throw std::runtime_error("Member not found: " + member_name);
    }
}

script_value class_operators::method_call(
    const script_value& instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    return script_value_class_extensions::call_method(instance, method_name, args);
}

script_value class_operators::subscript(
    const script_value& instance,
    const script_value& index
) {
    // Check if class has overloaded subscript operator
    try {
        return script_value_class_extensions::call_method(instance, "operator[]", {index});
    } catch (...) {
        throw std::runtime_error("Subscript operator not supported");
    }
}

script_value class_operators::add(const script_value& lhs, const script_value& rhs) {
    // Check if lhs has overloaded + operator
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            return script_value_class_extensions::call_method(lhs, "operator+", {rhs});
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Addition not supported for these types");
}

script_value class_operators::subtract(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            return script_value_class_extensions::call_method(lhs, "operator-", {rhs});
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Subtraction not supported for these types");
}

script_value class_operators::multiply(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            return script_value_class_extensions::call_method(lhs, "operator*", {rhs});
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Multiplication not supported for these types");
}

script_value class_operators::divide(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            return script_value_class_extensions::call_method(lhs, "operator/", {rhs});
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Division not supported for these types");
}

bool class_operators::less_than(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            auto result = script_value_class_extensions::call_method(lhs, "operator<", {rhs});
            return result.as_bool();
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Less than comparison not supported for these types");
}

bool class_operators::greater_than(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            auto result = script_value_class_extensions::call_method(lhs, "operator>", {rhs});
            return result.as_bool();
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Greater than comparison not supported for these types");
}

bool class_operators::less_equal(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            auto result = script_value_class_extensions::call_method(lhs, "operator<=", {rhs});
            return result.as_bool();
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Less equal comparison not supported for these types");
}

bool class_operators::greater_equal(const script_value& lhs, const script_value& rhs) {
    if (script_value_class_extensions::is_script_class_instance(lhs)) {
        try {
            auto result = script_value_class_extensions::call_method(lhs, "operator>=", {rhs});
            return result.as_bool();
        } catch (...) {
            // Fall through to default behavior
        }
    }
    
    throw std::runtime_error("Greater equal comparison not supported for these types");
}

// Explicit template instantiations for common types
template script_value class_type_converter::to_script_value<script_class_instance>(std::shared_ptr<script_class_instance>);
// TODO: hybrid_class_instance not yet defined
// template script_value class_type_converter::to_script_value<hybrid_class_instance>(std::shared_ptr<hybrid_class_instance>);

template std::shared_ptr<script_class_instance> class_type_converter::from_script_value<script_class_instance>(const script_value&);
// TODO: hybrid_class_instance not yet defined
// template std::shared_ptr<hybrid_class_instance> class_type_converter::from_script_value<hybrid_class_instance>(const script_value&);

} // namespace jai