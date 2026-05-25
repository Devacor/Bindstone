#pragma once

#include "value.hpp"
#include "dynamic_binder.hpp"
#include <typeindex>

namespace jai {

// Forward declare engine to avoid circular includes
class engine;

// Template implementation for script_value::make_registered_object
// This is in a separate header to avoid circular includes
template<typename T, typename... Args>
script_value script_value::make_registered_object(engine* eng, Args&&... args) {
    // Check if T is registered in the class system
    const std::type_index type_idx(typeid(T));
    auto class_def = eng->get_class_definition_by_type(type_idx);
    
    if (!class_def) {
        throw runtime_error("Type '" + std::string(typeid(T).name()) + 
                          "' is not registered. Use dynamic_binder<T> to register it first.");
    }
    
    // Get the registered class name
    const std::string& class_name = class_def->get_name();
    
    // Create script_values from the arguments
    std::vector<script_value> script_args;
    if constexpr (sizeof...(args) > 0) {
        (script_args.push_back(script_value(std::forward<Args>(args), eng)), ...);
    }
    
    // Get the constructor function from the engine's global environment
    auto constructor_func = eng->get_variable(class_name);
    
    if (constructor_func.is_null() || constructor_func.type() != script_value_type::jai_function_type) {
        throw runtime_error("Constructor function not found for class '" + class_name + "'");
    }
    
    // Call the constructor function
    auto result = constructor_func.as_function()(script_args);
    if (!result) {
        throw runtime_error("Constructor failed for class '" + class_name + "': " + result.error_message());
    }
    return result.value();
}

} // namespace jai