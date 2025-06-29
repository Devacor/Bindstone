#include "jaiscript/detail/class_interpreter.hpp"
#include "jaiscript/core/class_registry.hpp"
#include <stdexcept>

namespace jaiscript {

class_interpreter::class_interpreter(interpreter& parent_interpreter)
    : interpreter_(parent_interpreter) {
}

void class_interpreter::visit_class_decl(class_decl* stmt) {
    // Create class definition
    auto class_def = std::make_shared<script_class_definition>();
    class_def->name = stmt->name;
    class_def->default_access = stmt->default_access;
    
    // Set base class if specified
    if (!stmt->base_class_name.empty()) {
        auto base_class = class_registry::instance().find_script_class(stmt->base_class_name);
        if (!base_class) {
            throw std::runtime_error("Base class not found: " + stmt->base_class_name);
        }
        class_def->base_class = base_class;
    }
    
    // Push class context for member processing
    push_class_context(class_def);
    
    // Process fields
    for (auto& field : stmt->fields) {
        visit_field_decl(field.get());
    }
    
    // Process methods
    for (auto& method : stmt->methods) {
        visit_method_decl(method.get());
    }
    
    // Process constructors
    for (auto& constructor : stmt->constructors) {
        visit_constructor_decl(constructor.get());
    }
    
    // Process destructor
    if (stmt->destructor) {
        visit_destructor_decl(stmt->destructor.get());
    }
    
    // Pop class context
    pop_class_context();
    
    // Register the class
    class_registry::instance().register_script_class(class_def);
}

void class_interpreter::visit_field_decl(field_decl* stmt) {
    if (!current_class_def_) {
        throw std::runtime_error("Field declaration outside class context");
    }
    
    field_declaration field;
    field.name = stmt->name;
    field.type_name = stmt->type_name;
    field.access = stmt->access;
    
    // Evaluate default value if provided
    if (stmt->default_value) {
        field.default_value = evaluate_default_value(stmt->default_value.get());
    } else {
        field.default_value = script_value();  // null value
    }
    
    current_class_def_->fields.push_back(field);
}

void class_interpreter::visit_method_decl(method_decl* stmt) {
    if (!current_class_def_) {
        throw std::runtime_error("Method declaration outside class context");
    }
    
    // Validate method signature
    validate_method_signature(stmt);
    
    // Create method info
    method_info method;
    method.name = stmt->name;
    method.access = stmt->access;
    method.is_override = stmt->is_override;
    method.is_virtual = stmt->is_virtual;
    method.method_type = method_info::script_direct;  // Start as direct, promote if needed
    
    // Convert method body to function
    method.script_method = create_function_from_method(stmt);
    
    // Add to class definition
    current_class_def_->methods[stmt->name] = method;
}

void class_interpreter::visit_constructor_decl(constructor_decl* stmt) {
    if (!current_class_def_) {
        throw std::runtime_error("Constructor declaration outside class context");
    }
    
    constructor_declaration ctor;
    ctor.class_name = stmt->class_name;
    ctor.parameters = stmt->parameters;
    ctor.is_delegating = stmt->is_delegating;
    ctor.delegation_type_val = stmt->delegation_type_val;
    
    // Convert delegation arguments
    for (auto& arg : stmt->delegation_args) {
        ctor.delegation_args.push_back(interpreter_.evaluate(arg.get()));
    }
    
    // Convert constructor body to function
    ctor.implementation = create_function_from_constructor(stmt);
    
    current_class_def_->constructors.push_back(ctor);
}

void class_interpreter::visit_destructor_decl(destructor_decl* stmt) {
    if (!current_class_def_) {
        throw std::runtime_error("Destructor declaration outside class context");
    }
    
    // Convert destructor body to function
    current_class_def_->destructor = create_function_from_destructor(stmt);
    
    // If base class exists, promote its destructor to virtual
    if (current_class_def_->base_class) {
        virtual_method_promoter promoter;
        promoter.promote_destructor_to_virtual(current_class_def_->base_class);
    }
}

void class_interpreter::visit_super_expr(super_expr* expr) {
    auto instance = get_current_instance();
    if (!instance) {
        throw std::runtime_error("Super call outside method context");
    }
    
    auto base_class = instance->class_def->base_class;
    if (!base_class) {
        throw std::runtime_error("No base class for super call");
    }
    
    // Evaluate arguments
    std::vector<script_value> args;
    for (auto& arg : expr->arguments) {
        args.push_back(interpreter_.evaluate(arg.get()));
    }
    
    // Call method on base class, skipping current class override
    auto method = base_class->find_method(expr->method_name);
    if (!method) {
        throw std::runtime_error("Base method not found: " + expr->method_name);
    }
    
    // Direct call to base method
    std::vector<script_value> full_args;
    full_args.push_back(script_value::make_object(instance));
    full_args.insert(full_args.end(), args.begin(), args.end());
    
    auto result = method->script_method->call(full_args);
    interpreter_.set_result(result);
}

std::shared_ptr<script_class_instance> class_interpreter::create_instance(
    const std::string& class_name,
    const std::vector<script_value>& args
) {
    return class_registry::instance().create_script_instance(class_name, args);
}

script_value class_interpreter::call_method_on_instance(
    std::shared_ptr<script_class_instance> instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    validate_method_access(method_name);
    return call_method(instance, method_name, args);
}

script_value class_interpreter::get_field_value(
    std::shared_ptr<script_class_instance> instance,
    const std::string& field_name
) {
    validate_field_access(field_name);
    
    auto it = instance->fields.find(field_name);
    if (it != instance->fields.end()) {
        return it->second;
    }
    
    throw std::runtime_error("Field not found: " + field_name);
}

void class_interpreter::set_field_value(
    std::shared_ptr<script_class_instance> instance,
    const std::string& field_name,
    const script_value& value
) {
    validate_field_access(field_name);
    instance->fields[field_name] = value;
}

std::shared_ptr<function_decl> class_interpreter::create_function_from_method(method_decl* method) {
    // Convert method AST to function_decl
    // This is a simplified implementation - in practice would need full conversion
    auto func = std::make_shared<function_decl>();
    func->name = method->name;
    // Add 'this' parameter as first parameter
    func->parameters.insert(func->parameters.begin(), "this");
    func->parameters.insert(func->parameters.end(), method->parameters.begin(), method->parameters.end());
    // Convert body - simplified for now
    return func;
}

std::shared_ptr<function_decl> class_interpreter::create_function_from_constructor(constructor_decl* ctor) {
    // Convert constructor AST to function_decl
    auto func = std::make_shared<function_decl>();
    func->name = ctor->class_name;
    func->parameters.insert(func->parameters.begin(), "this");
    func->parameters.insert(func->parameters.end(), ctor->parameters.begin(), ctor->parameters.end());
    return func;
}

std::shared_ptr<function_decl> class_interpreter::create_function_from_destructor(destructor_decl* dtor) {
    // Convert destructor AST to function_decl
    auto func = std::make_shared<function_decl>();
    func->name = "~" + dtor->class_name;
    func->parameters.push_back("this");
    return func;
}

script_value class_interpreter::evaluate_default_value(expr* default_expr) {
    return interpreter_.evaluate(default_expr);
}

void class_interpreter::validate_method_signature(method_decl* method) {
    // Validate method signature - implementation depends on specific requirements
}

void class_interpreter::validate_field_access(const std::string& field_name) {
    // Validate field access permissions
    script_access_controller controller;
    // Implementation would check access permissions
}

void class_interpreter::validate_method_access(const std::string& method_name) {
    // Validate method access permissions
    script_access_controller controller;
    // Implementation would check access permissions
}

void class_interpreter::push_class_context(std::shared_ptr<script_class_definition> class_def) {
    current_class_def_ = class_def;
}

void class_interpreter::pop_class_context() {
    current_class_def_ = nullptr;
}

void class_interpreter::push_instance_context(std::shared_ptr<script_class_instance> instance) {
    current_instance_ = instance;
}

void class_interpreter::pop_instance_context() {
    current_instance_ = nullptr;
}

std::shared_ptr<script_class_instance> class_interpreter::get_current_instance() {
    return current_instance_;
}

std::vector<script_value> class_interpreter::get_current_args() {
    // Return current method arguments - implementation depends on interpreter state
    return {};
}

// script_access_controller implementation

bool script_access_controller::can_access(
    const std::string& from_class,
    const std::string& to_class,
    const std::string& member_name,
    access_level member_access
) {
    // Only enforce for script-to-script access
    if (!is_script_class(from_class) || !is_script_class(to_class)) {
        return true;  // Let C++ handle its own access control
    }
    
    switch (member_access) {
    case access_level::public_access:
        return true;
        
    case access_level::private_access:
        return from_class == to_class;  // Same class only
        
    case access_level::protected_access:
        return from_class == to_class || 
               is_inheritance_related(from_class, to_class);
    }
    
    return false;
}

bool script_access_controller::is_script_class(const std::string& class_name) {
    return class_registry::instance().is_script_class(class_name);
}

bool script_access_controller::is_inheritance_related(const std::string& class1, const std::string& class2) {
    return is_base_of(class1, class2) || is_base_of(class2, class1);
}

bool script_access_controller::is_base_of(const std::string& base, const std::string& derived) {
    return class_registry::instance().is_assignable_from(derived, base);
}

} // namespace jaiscript