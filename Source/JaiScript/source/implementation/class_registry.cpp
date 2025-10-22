#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <stdexcept>
#include <set>

namespace jai {

void class_registry::register_script_class(std::shared_ptr<script_class_definition> class_def) {
    if (!class_def) {
        throw std::runtime_error("Cannot register null script class definition");
    }
    
    // Register the class
    script_classes_[class_def->get_name()] = class_def;
    
    // Validate the complete hierarchy
    validate_class_hierarchy(class_def->get_name());
}

void class_registry::register_cpp_class(std::shared_ptr<class_definition> class_def) {
    if (!class_def) {
        throw std::runtime_error("Cannot register null C++ class definition");
    }
    
    cpp_classes_[class_def->get_name()] = class_def;
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
    // Since base_class_definition is just a simple struct with a name,
    // and both script_class_definition and class_definition have get_name(),
    // we can create a base_class_definition to return
    
    auto script_class = find_script_class(name);
    if (script_class) {
        auto base_def = std::make_shared<base_class_definition>();
        base_def->name = script_class->get_name();
        return base_def;
    }
    
    auto cpp_class = find_cpp_class(name);
    if (cpp_class) {
        auto base_def = std::make_shared<base_class_definition>();
        base_def->name = cpp_class->get_name();
        return base_def;
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
    
    // Use the class_definition's create_instance method
    auto instance = class_def->create_instance();
    
    // TODO: Handle constructor arguments
    // The current API doesn't provide a way to pass constructor args to create_instance
    // Constructor arguments would need to be handled by the caller after instance creation
    
    return instance;
}

void class_registry::validate_class_hierarchy(const std::string& class_name) {
    auto class_def = find_script_class(class_name);
    if (!class_def) {
        // Also check C++ classes
        auto cpp_def = find_cpp_class(class_name);
        if (!cpp_def) return;
        
        // For C++ classes, we can't easily traverse the hierarchy
        // but we can at least check the immediate parent
        auto parent = cpp_def->get_cpp_base_class();
        if (parent && parent->get_name() == class_name) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        return;
    }
    
    // Check for circular inheritance in script classes
    std::set<std::string> visited;
    std::shared_ptr<class_definition> current = class_def;
    
    while (current) {
        if (visited.count(current->get_name())) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        visited.insert(current->get_name());
        
        // Script classes inherit from class_definition, which doesn't expose parent_class_
        // directly. We need to check if there's a parent by looking it up
        // This is a limitation of the current API
        break; // Can't traverse further without access to parent_class_
    }
}

void class_registry::promote_methods_to_virtual(
    const std::string& base_class_name,
    const std::string& derived_class_name
) {
    auto base_class = find_script_class(base_class_name);
    auto derived_class = find_script_class(derived_class_name);
    
    if (!base_class || !derived_class) return;
    
    // The current API uses method_metadata to track virtual methods
    // but doesn't expose a way to iterate over methods or modify their metadata
    // This functionality would need to be added to class_definition if needed
}

bool class_registry::check_inheritance_chain(const std::string& derived, const std::string& base) {
    if (derived == base) return true;
    
    // First check script classes
    auto derived_script = find_script_class(derived);
    if (derived_script) {
        // Check if the derived class has the base as a parent
        // Since we can't access parent_class_ directly, we need to use
        // the inheritance information stored during class registration
        
        // For now, we can only check direct parent relationships
        // A full implementation would need to maintain an inheritance graph
        // or have access to the parent_class_ member
        
        // Check if there's a C++ base class
        auto cpp_base = derived_script->get_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }
    }
    
    // Check C++ classes
    auto derived_cpp = find_cpp_class(derived);
    if (derived_cpp) {
        // Check C++ base class
        auto cpp_base = derived_cpp->get_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }
        
        // Recursively check the parent's inheritance chain
        if (cpp_base) {
            return check_inheritance_chain(cpp_base->get_name(), base);
        }
    }
    
    // Check for cross-type inheritance (script class inheriting from C++ class)
    if (derived_script) {
        auto cpp_base = derived_script->get_cpp_base_class();
        if (cpp_base) {
            return check_inheritance_chain(cpp_base->get_name(), base);
        }
    }
    
    return false;
}

void class_registry::apply_default_field_values(
    std::shared_ptr<script_class_instance> instance,
    std::shared_ptr<script_class_definition> class_def
) {
    // The create_instance() method already applies default field values
    // This method is no longer needed with the current API
}

} // namespace jai