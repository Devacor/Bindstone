#include "jaiscript/core/script_class.hpp"
#include "jaiscript/core/engine.hpp"
#include <stdexcept>
#include <iostream>

namespace jai {

// script_class_definition implementation

script_value script_class_definition::call_method(
    script_class_instance* instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) const {
    auto method = find_method(method_name);
    if (!method) {
        throw std::runtime_error("Method not found: " + method_name);
    }
    
    if (method->is_virtual) {
        return call_method_virtual(instance, method_name, args);
    } else {
        return call_method_direct(instance, method, args);
    }
}

void script_class_definition::call_destructor(script_class_instance* instance) const {
    if (destructor_is_virtual) {
        call_destructor_virtual(instance);
    } else {
        call_destructor_direct(instance);
    }
}

method_info* script_class_definition::find_method(const std::string& method_name) const {
    // Check cache first
    auto cache_it = method_cache_.find(method_name);
    if (cache_it != method_cache_.end()) {
        return cache_it->second;
    }
    
    // Look in methods
    auto method_it = methods.find(method_name);
    if (method_it != methods.end()) {
        method_cache_[method_name] = const_cast<method_info*>(&method_it->second);
        return const_cast<method_info*>(&method_it->second);
    }
    
    return nullptr;
}

method_info* script_class_definition::find_method_in_hierarchy(const std::string& method_name) const {
    // Search current class first
    auto method = find_method(method_name);
    if (method) {
        return method;
    }
    
    // Search base class hierarchy
    auto current = base_class;
    while (current) {
        method = current->find_method(method_name);
        if (method) {
            return method;
        }
        current = current->base_class;
    }
    
    return nullptr;
}

void script_class_definition::promote_method_to_virtual(const std::string& method_name) {
    auto method_it = methods.find(method_name);
    if (method_it != methods.end()) {
        method_it->second.is_virtual = true;
        method_it->second.method_type = method_info::script_virtual;
        
        // Invalidate cache
        method_cache_.erase(method_name);
    }
}

void script_class_definition::promote_destructor_to_virtual() {
    destructor_is_virtual = true;
}

script_value script_class_definition::call_method_direct(
    script_class_instance* instance,
    method_info* method,
    const std::vector<script_value>& args
) const {
    if (!method->script_method) {
        throw std::runtime_error("No implementation for method: " + method->name);
    }
    
    // TODO: Execute method bytecode directly in VM mode
    // Currently all script methods execute via interpreter callback
    
    // Fast direct call - no virtual dispatch
    std::vector<script_value> full_args;
    full_args.push_back(script_value::make_object(instance->shared_from_this()));
    full_args.insert(full_args.end(), args.begin(), args.end());
    
    return method->script_method->call(full_args);
}

script_value script_class_definition::call_method_virtual(
    script_class_instance* instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) const {
    // Virtual dispatch - find most derived implementation
    auto actual_class = find_actual_class(instance);
    auto method = actual_class->find_method_in_hierarchy(method_name);
    
    if (!method || !method->script_method) {
        throw std::runtime_error("No virtual method implementation found: " + method_name);
    }
    
    std::vector<script_value> full_args;
    full_args.push_back(script_value::make_object(instance->shared_from_this()));
    full_args.insert(full_args.end(), args.begin(), args.end());
    
    return method->script_method->call(full_args);
}

void script_class_definition::call_destructor_direct(script_class_instance* instance) const {
    // Direct call - just this class destructor
    if (destructor) {
        try {
            std::vector<script_value> args;
            args.push_back(script_value::make_object(instance->shared_from_this()));
            destructor->call(args);
        } catch (const std::exception& e) {
            std::cerr << "Exception in destructor for " << name << ": " << e.what() << std::endl;
        }
    }
}

void script_class_definition::call_destructor_virtual(script_class_instance* instance) const {
    // Virtual dispatch - call entire destructor chain
    // 1. Find most derived class with destructor
    auto current_class = find_actual_class(instance);
    
    // 2. Call destructors in reverse inheritance order (derived -> base)
    while (current_class) {
        if (current_class->destructor) {
            try {
                std::vector<script_value> args;
                args.push_back(script_value::make_object(instance->shared_from_this()));
                current_class->destructor->call(args);
            } catch (const std::exception& e) {
                std::cerr << "Exception in destructor for " << current_class->name << ": " << e.what() << std::endl;
            }
        }
        current_class = current_class->base_class;
    }
}

std::shared_ptr<script_class_definition> script_class_definition::find_actual_class(script_class_instance* instance) const {
    // For now, return the instance's class definition
    // In future, this could be enhanced for more complex inheritance scenarios
    return instance->class_def;
}

// Global method call interface
script_value call_method(
    std::shared_ptr<script_class_instance> instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    if (!instance || !instance->class_def) {
        throw std::runtime_error("Invalid instance for method call");
    }
    
    // Always delegate to class definition for consistent dispatch
    return instance->class_def->call_method(instance.get(), method_name, args);
}

// virtual_method_promoter implementation

void virtual_method_promoter::promote_to_virtual(
    std::shared_ptr<script_class_definition> base_class,
    const std::string& method_name
) {
    if (!base_class) return;
    
    auto method_it = base_class->methods.find(method_name);
    if (method_it != base_class->methods.end()) {
        if (method_it->second.method_type == method_info::script_direct) {
            method_it->second.method_type = method_info::script_virtual;
            method_it->second.is_virtual = true;
            
            // Invalidate method cache
            base_class->method_cache_.erase(method_name);
        }
    }
}

void virtual_method_promoter::validate_override(
    std::shared_ptr<script_class_definition> derived_class,
    const method_declaration& method
) {
    if (!derived_class->base_class) return;
    
    auto base_method_info = derived_class->base_class->find_method_in_hierarchy(method.name);
    
    if (base_method_info) {
        if (!method.is_override) {
            throw std::runtime_error(
                "Method '" + method.name + "' in class '" + derived_class->name + 
                "' shadows base method. Use 'override' keyword or rename method."
            );
        }
        
        // Promote base method to virtual
        promote_to_virtual(derived_class->base_class, method.name);
    } else if (method.is_override) {
        throw std::runtime_error(
            "Method '" + method.name + "' marked override but no base method found"
        );
    }
}

void virtual_method_promoter::promote_destructor_to_virtual(std::shared_ptr<script_class_definition> base_class) {
    if (base_class) {
        base_class->promote_destructor_to_virtual();
    }
}

// override_validator implementation

void override_validator::validate_overrides(std::shared_ptr<script_class_definition> class_def) {
    if (!class_def->base_class) return;
    
    // Validate all methods in this class
    for (const auto& method_pair : class_def->methods) {
        const auto& method_info = method_pair.second;
        auto base_method = find_method_in_hierarchy(class_def->base_class, method_info.name);
        
        if (base_method) {
            // Method exists in base class
            if (!method_info.is_override) {
                throw std::runtime_error(
                    "Method '" + method_info.name + "' in class '" + class_def->name + 
                    "' overrides base method but missing 'override' keyword. " +
                    "Add 'override' keyword or rename method."
                );
            }
        } else {
            // Method doesn't exist in base
            if (method_info.is_override) {
                throw std::runtime_error(
                    "Method '" + method_info.name + "' marked override but no base method found"
                );
            }
        }
    }
}

void override_validator::validate_signature_compatibility(
    const method_declaration& derived,
    const method_declaration& base
) {
    // In scripting language: parameter count must match
    if (derived.parameters.size() != base.parameters.size()) {
        throw std::runtime_error(
            "Override method '" + derived.name + "' parameter count mismatch. " +
            "Expected " + std::to_string(base.parameters.size()) + 
            ", got " + std::to_string(derived.parameters.size())
        );
    }
    
    // Return type validation: allow flexible types for scripting
    // The script_value system handles type conversions at runtime
}

method_declaration* override_validator::find_method_in_hierarchy(
    std::shared_ptr<script_class_definition> class_def,
    const std::string& method_name
) {
    // This is a simplified version - in practice, we'd need to convert
    // between method_info and method_declaration or unify them
    return nullptr;
}

// constructor_resolver implementation

std::vector<constructor_declaration*> constructor_resolver::resolve_delegation_chain(
    const std::string& class_name,
    const std::vector<script_value>& args
) {
    std::vector<constructor_declaration*> chain;
    // Implementation would resolve the delegation chain based on constructor signatures
    // and delegation types (same_class vs base_class)
    return chain;
}

void constructor_resolver::execute_constructor_chain(
    std::shared_ptr<script_class_instance> instance,
    const std::vector<constructor_declaration*>& chain,
    const std::vector<script_value>& original_args
) {
    // Execute constructors in the correct order:
    // 1. Base class constructors first
    // 2. Apply default field values only once
    // 3. Execute constructor bodies in delegation order
    
    for (auto* ctor : chain) {
        if (ctor->implementation) {
            std::vector<script_value> full_args;
            full_args.push_back(script_value::make_object(instance));
            full_args.insert(full_args.end(), original_args.begin(), original_args.end());
            ctor->implementation->call(full_args);
        }
    }
}

bool constructor_resolver::validate_no_cycles(const std::string& class_name) {
    // TODO: Implement circular delegation detection
    // Should check if constructor A delegates to B which delegates to A
    // Implementation would check for circular delegation chains
    return true;
}

} // namespace jai