#include "jaiscript/core/class_registry.hpp"
#include <stdexcept>

namespace jaiscript {

class_registry& class_registry::instance() {
    static class_registry instance_;
    return instance_;
}

void class_registry::register_script_class(std::shared_ptr<script_class_definition> class_def) {
    if (!class_def) {
        throw std::runtime_error("Cannot register null script class definition");
    }
    
    // Validate the class before registration
    override_validator_.validate_overrides(class_def);
    
    // Check if this class has a base class and handle virtual promotion
    if (class_def->base_class) {
        promote_methods_to_virtual(class_def->base_class->name, class_def->name);
        
        // If derived class has destructor, promote base destructor to virtual
        if (class_def->destructor) {
            method_promoter_.promote_destructor_to_virtual(class_def->base_class);
        }
    }
    
    // Register the class
    script_classes_[class_def->name] = class_def;
    
    // Validate the complete hierarchy
    validate_class_hierarchy(class_def->name);
}

void class_registry::register_cpp_class(std::shared_ptr<class_definition> class_def) {
    if (!class_def) {
        throw std::runtime_error("Cannot register null C++ class definition");
    }
    
    cpp_classes_[class_def->name] = class_def;
}

std::shared_ptr<script_class_definition> class_registry::find_script_class(const std::string& name) {
    auto it = script_classes_.find(name);
    return (it != script_classes_.end()) ? it->second : nullptr;
}

std::shared_ptr<class_definition> class_registry::find_cpp_class(const std::string& name) {
    auto it = cpp_classes_.find(name);
    return (it != cpp_classes_.end()) ? it->second : nullptr;
}

std::shared_ptr<base_class_definition> class_registry::find_class(const std::string& name) {
    // Try script classes first
    auto script_class = find_script_class(name);
    if (script_class) {
        return std::static_pointer_cast<base_class_definition>(script_class);
    }
    
    // Try C++ classes
    auto cpp_class = find_cpp_class(name);
    if (cpp_class) {
        return std::static_pointer_cast<base_class_definition>(cpp_class);
    }
    
    return nullptr;
}

bool class_registry::is_assignable_from(const std::string& derived, const std::string& base) {
    return check_inheritance_chain(derived, base);
}

bool class_registry::is_script_class(const std::string& name) {
    return script_classes_.find(name) != script_classes_.end();
}

bool class_registry::is_cpp_class(const std::string& name) {
    return cpp_classes_.find(name) != cpp_classes_.end();
}

std::shared_ptr<script_class_instance> class_registry::create_script_instance(
    const std::string& class_name,
    const std::vector<script_value>& args
) {
    auto class_def = find_script_class(class_name);
    if (!class_def) {
        throw std::runtime_error("Script class not found: " + class_name);
    }
    
    // Create instance
    auto instance = std::make_shared<script_class_instance>();
    instance->class_name = class_name;
    instance->class_def = class_def;
    
    // Apply default field values
    apply_default_field_values(instance, class_def);
    
    // Resolve and execute constructor chain
    auto constructor_chain = constructor_resolver_.resolve_delegation_chain(class_name, args);
    constructor_resolver_.execute_constructor_chain(instance, constructor_chain, args);
    
    return instance;
}

void class_registry::validate_class_hierarchy(const std::string& class_name) {
    auto class_def = find_script_class(class_name);
    if (!class_def) return;
    
    // Check for circular inheritance
    std::set<std::string> visited;
    auto current = class_def;
    
    while (current) {
        if (visited.count(current->name)) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        visited.insert(current->name);
        current = current->base_class;
    }
    
    // Validate constructor delegation cycles
    if (!constructor_resolver_.validate_no_cycles(class_name)) {
        throw std::runtime_error("Circular constructor delegation in class: " + class_name);
    }
}

void class_registry::promote_methods_to_virtual(
    const std::string& base_class_name,
    const std::string& derived_class_name
) {
    auto base_class = find_script_class(base_class_name);
    auto derived_class = find_script_class(derived_class_name);
    
    if (!base_class || !derived_class) return;
    
    // Check each method in derived class for overrides
    for (const auto& method_pair : derived_class->methods) {
        const auto& method_name = method_pair.first;
        const auto& method_info = method_pair.second;
        
        // If this method exists in base class and is marked as override
        if (method_info.is_override) {
            auto base_method = base_class->find_method_in_hierarchy(method_name);
            if (base_method) {
                method_promoter_.promote_to_virtual(base_class, method_name);
            }
        }
    }
}

bool class_registry::check_inheritance_chain(const std::string& derived, const std::string& base) {
    if (derived == base) return true;
    
    auto derived_class = find_script_class(derived);
    if (!derived_class) return false;
    
    auto current = derived_class->base_class;
    while (current) {
        if (current->name == base) return true;
        current = current->base_class;
    }
    
    return false;
}

void class_registry::apply_default_field_values(
    std::shared_ptr<script_class_instance> instance,
    std::shared_ptr<script_class_definition> class_def
) {
    // Apply default values for all fields in the inheritance chain
    std::vector<std::shared_ptr<script_class_definition>> class_chain;
    
    // Build inheritance chain (base to derived)
    auto current = class_def;
    while (current) {
        class_chain.push_back(current);
        current = current->base_class;
    }
    
    // Apply defaults from base to derived
    for (auto it = class_chain.rbegin(); it != class_chain.rend(); ++it) {
        for (const auto& field : (*it)->fields) {
            // Only set if not already set (respects constructor delegation)
            if (instance->fields.find(field.name) == instance->fields.end()) {
                instance->fields[field.name] = field.default_value;
            }
        }
    }
}

} // namespace jaiscript