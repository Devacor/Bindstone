#include "jaiscript/core/class_integration.hpp"
#include "jaiscript/core/function_binder.hpp"
#include <functional>

namespace jaiscript {

class_system_integration::class_system_integration(engine& jaiscript_engine)
    : engine_(jaiscript_engine) {
}

void class_system_integration::initialize() {
    // Initialize all class system components
    integrate_class_parser();
    integrate_class_interpreter();
    integrate_value_extensions();
    integrate_enhanced_class_builder();
    register_builtin_class_support();
    register_class_globals();
}

void class_system_integration::register_builtin_class_support() {
    // Register built-in support for shared_ptr and weak_ptr with script classes
    
    // shared_ptr<script_class_instance> support
    engine_.add_function("make_shared", 
        [](const std::string& class_name) -> script_value {
            return create_script_instance(class_name, {});
        }
    );
    
    // weak_ptr support
    engine_.add_function("weak_ptr", 
        [](const script_value& shared_instance) -> script_value {
            auto instance = script_value_class_extensions::as_script_class_instance(shared_instance);
            if (instance) {
                std::weak_ptr<script_class_instance> weak_instance = instance;
                return class_type_converter::make_weak_ptr_value(weak_instance);
            }
            throw std::runtime_error("Cannot create weak_ptr from non-class instance");
        }
    );
    
    // lock() method for weak_ptr
    engine_.add_function("lock",
        [](const script_value& weak_instance) -> script_value {
            auto weak_ptr = class_type_converter::as_weak_ptr(weak_instance);
            auto locked = weak_ptr.lock();
            if (locked) {
                return class_type_converter::make_shared_ptr_value(locked);
            }
            return script_value();  // null
        }
    );
}

void class_system_integration::integrate_class_parser() {
    // Integration with parser would happen here
    // This would extend the main parser to recognize class keywords and syntax
    class_parser_ = std::make_unique<class_parser>(engine_.get_parser());
}

void class_system_integration::integrate_class_interpreter() {
    // Integration with interpreter would happen here
    // This would extend the main interpreter to handle class AST nodes
    class_interpreter_ = std::make_unique<class_interpreter>(engine_.get_interpreter());
}

void class_system_integration::integrate_value_extensions() {
    // Register value system extensions for class support
    // This would modify the existing script_value class to support class instances
    
    // Add new operators for class instances
    engine_.add_operator(".", 
        [](const script_value& instance, const std::string& member) -> script_value {
            return class_operators::member_access(instance, member);
        }
    );
    
    // Method call operator
    engine_.add_operator("()", 
        [](const script_value& method_ref, const std::vector<script_value>& args) -> script_value {
            // Implementation would handle method calls
            return script_value();
        }
    );
}

void class_system_integration::integrate_enhanced_class_builder() {
    // Integration point for enhanced ClassBuilder
    // This would modify the existing ClassBuilder to support virtual methods and script inheritance
}

void class_system_integration::register_class_globals() {
    register_new_operator();
    register_make_shared_functions();
    register_instanceof_function();
    register_typeof_function();
    register_super_support();
}

void class_system_integration::register_new_operator() {
    // Register 'new' operator for class instantiation
    engine_.add_function("new", 
        [](const std::string& class_name) -> script_value {
            return create_script_instance(class_name, {});
        }
    );
    
    // Overloaded versions with arguments
    engine_.add_function("new",
        [](const std::string& class_name, const script_value& arg1) -> script_value {
            return create_script_instance(class_name, {arg1});
        }
    );
    
    engine_.add_function("new",
        [](const std::string& class_name, const script_value& arg1, const script_value& arg2) -> script_value {
            return create_script_instance(class_name, {arg1, arg2});
        }
    );
    
    // More overloads as needed...
}

void class_system_integration::register_make_shared_functions() {
    // Register make_shared functions for RAII support
    engine_.add_function("make_shared",
        [](const std::string& class_name) -> script_value {
            return make_shared_script_instance(class_name, {});
        }
    );
    
    engine_.add_function("make_shared",
        [](const std::string& class_name, const script_value& arg1) -> script_value {
            return make_shared_script_instance(class_name, {arg1});
        }
    );
    
    engine_.add_function("make_shared",
        [](const std::string& class_name, const script_value& arg1, const script_value& arg2) -> script_value {
            return make_shared_script_instance(class_name, {arg1, arg2});
        }
    );
}

void class_system_integration::register_instanceof_function() {
    engine_.add_function("instanceof",
        [](const script_value& instance, const std::string& class_name) -> bool {
            return instanceof_check(instance, class_name);
        }
    );
}

void class_system_integration::register_typeof_function() {
    engine_.add_function("typeof",
        [](const script_value& instance) -> std::string {
            return typeof_class(instance);
        }
    );
}

void class_system_integration::register_super_support() {
    // Super call support would be handled in the parser/interpreter
    // This is just a placeholder for any global super-related functions
    engine_.add_function("super_call",
        [](const script_value& instance, const std::string& method_name, const std::vector<script_value>& args) -> script_value {
            return super_method_call(instance, method_name, args);
        }
    );
}

// Global factory functions implementation

script_value create_script_instance(const std::string& class_name, const std::vector<script_value>& args) {
    auto instance = class_registry::instance().create_script_instance(class_name, args);
    return script_value_class_extensions::make_script_class_instance(instance);
}

script_value make_shared_script_instance(const std::string& class_name, const std::vector<script_value>& args) {
    auto instance = make_shared_script_class(class_name, args);
    return class_type_converter::make_shared_ptr_value(instance);
}

bool instanceof_check(const script_value& instance, const std::string& class_name) {
    return script_value_class_extensions::is_instance_of(instance, class_name);
}

std::string typeof_class(const script_value& instance) {
    auto class_name = script_value_class_extensions::get_class_name(instance);
    return class_name.empty() ? "undefined" : class_name;
}

script_value super_method_call(
    const script_value& instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    auto script_instance = script_value_class_extensions::as_script_class_instance(instance);
    if (!script_instance || !script_instance->class_def->base_class) {
        throw std::runtime_error("Super call not valid for this instance");
    }
    
    // Find method in base class
    auto base_method = script_instance->class_def->base_class->find_method(method_name);
    if (!base_method) {
        throw std::runtime_error("Base method not found: " + method_name);
    }
    
    // Direct call to base method
    std::vector<script_value> full_args;
    full_args.push_back(instance);
    full_args.insert(full_args.end(), args.begin(), args.end());
    
    return base_method->script_method->call(full_args);
}

} // namespace jaiscript