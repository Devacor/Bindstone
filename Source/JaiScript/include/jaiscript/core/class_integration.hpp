#pragma once

#include "jaiscript/core/engine.hpp"
#include "jaiscript/core/script_class.hpp"
#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/detail/class_parser.hpp"
#include "jaiscript/detail/class_interpreter.hpp"
#include "jaiscript/core/value_extensions.hpp"

namespace jai {

// Main integration class for class system with JaiScript engine
class class_system_integration {
public:
    explicit class_system_integration(engine& jaiscript_engine);
    
    // Initialize class system
    void initialize();
    
    // Register built-in class support
    void register_builtin_class_support();
    
    // Parser integration
    void integrate_class_parser();
    
    // Interpreter integration
    void integrate_class_interpreter();
    
    // Value system integration
    void integrate_value_extensions();
    
    // Enhanced ClassBuilder integration
    void integrate_enhanced_class_builder();
    
    // Register class-related global functions
    void register_class_globals();
    
private:
    engine& engine_;
    std::unique_ptr<class_parser> class_parser_;
    std::unique_ptr<class_interpreter> class_interpreter_;
    
    // Helper methods
    void register_new_operator();
    void register_make_shared_functions();
    void register_instanceof_function();
    void register_typeof_function();
    void register_super_support();
};

// Enhanced ClassBuilder for script inheritance support
template<typename T>
class enhanced_class_builder : public class_builder<T> {
public:
    using class_builder<T>::class_builder;
    
    // Allow script classes to inherit from this C++ class
    enhanced_class_builder& allow_script_inheritance() {
        this->class_def_->allow_script_inheritance = true;
        return *this;
    }
    
    // Virtual method registration for script overrides
    template<typename R, typename... Args>
    enhanced_class_builder& virtual_method(const std::string& name, R(T::*method)(Args...)) {
        auto method_info = std::make_shared<method_info>();
        method_info->name = name;
        method_info->is_virtual = true;
        method_info->method_type = method_info::cpp;
        method_info->cpp_method = bind_virtual_method(method);
        this->class_def_->methods[name] = method_info;
        return *this;
    }
    
    // Virtual destructor support
    enhanced_class_builder& virtual_destructor() {
        this->class_def_->has_virtual_destructor = true;
        return *this;
    }
    
private:
    template<typename R, typename... Args>
    std::function<script_value(const std::vector<script_value>&)> bind_virtual_method(R(T::*method)(Args...)) {
        return [method](const std::vector<script_value>& args) -> script_value {
            // Extract 'this' pointer and arguments
            if (args.empty()) {
                throw std::runtime_error("Virtual method requires 'this' parameter");
            }
            
            auto instance = args[0].as<std::shared_ptr<T>>();
            if (!instance) {
                throw std::runtime_error("Invalid 'this' pointer for virtual method");
            }
            
            // Call method with proper argument conversion
            if constexpr (sizeof...(Args) == 0) {
                if constexpr (std::is_void_v<R>) {
                    (instance.get()->*method)();
                    return script_value();
                } else {
                    return script_value((instance.get()->*method)());
                }
            } else {
                // Handle arguments - simplified for now
                return call_with_args(instance.get(), method, args);
            }
        };
    }
    
    template<typename R, typename... Args>
    script_value call_with_args(T* instance, R(T::*method)(Args...), const std::vector<script_value>& args) {
        // Simplified argument handling - full implementation would handle all types
        return script_value();
    }
};

// Global factory functions for script class creation
script_value create_script_instance(const std::string& class_name, const std::vector<script_value>& args);
script_value make_shared_script_instance(const std::string& class_name, const std::vector<script_value>& args);

// instanceof function for type checking
bool instanceof_check(const script_value& instance, const std::string& class_name);

// typeof function for getting class name
std::string typeof_class(const script_value& instance);

// Super call support
script_value super_method_call(
    const script_value& instance,
    const std::string& method_name,
    const std::vector<script_value>& args
);

} // namespace jai