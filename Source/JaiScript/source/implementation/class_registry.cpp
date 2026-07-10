#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/core/dynamic_binder.hpp"
#include "jaiscript/core/engine.hpp"
#include <stdexcept>
#include <set>

namespace jai {

// ===== flat_value_semantics (class_definition.hpp doc) =====
// Out of line: nested field classes resolve through the engine's registry, which the
// class_definition header cannot see.

bool class_definition::flat_value_semantics() const {
    if (!engine_) {
        return false;
    }
    const uint64_t epoch = engine_->class_definition_epoch();
    if (flat_value_epoch_ == epoch) {
        return flat_value_cached_;
    }
    std::vector<const class_definition*> in_progress;
    flat_value_cached_ = flat_value_semantics_impl(in_progress);
    flat_value_epoch_ = epoch;
    return flat_value_cached_;
}

bool class_definition::flat_value_semantics_impl(std::vector<const class_definition*>& in_progress) const {
    if (class_type_ != script_class && class_type_ != vm_class) {
        return false;
    }
    if (cpp_base_class_) {
        return false;
    }
    for (const class_definition* visiting : in_progress) {
        if (visiting == this) {
            return false;   // declared-type cycle: unconstructible by value, never flat
        }
    }
    in_progress.push_back(this);
    bool flat = true;
    for (const auto& parent : parent_classes_) {
        if (!parent || !parent->flat_value_semantics_impl(in_progress)) {
            flat = false;
            break;
        }
    }
    if (flat) {
        for (const auto& [field_id, field_default] : field_defaults_) {
            (void)field_default;
            if (!flat_field_type(get_field_declared_type(field_id), in_progress)) {
                flat = false;
                break;
            }
        }
    }
    in_progress.pop_back();
    return flat;
}

// Transitive value-closure (Dev ruling: any type that cannot point at something else
// qualifies - primitives, strings, containers OF value-closed types to any depth,
// nested flat classes). What poisons: shared_ptr/weak_ptr/function/reference (their
// semantics IS pointing elsewhere) and auto/var (no store-enforced shape).
bool class_definition::flat_field_type(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const {
    if (!t) {
        return false;   // auto/untyped field: no store-enforced shape to lean on
    }
    switch (t->base_type) {
    case script_value_type::jai_int_type:
    case script_value_type::jai_float_type:
    case script_value_type::jai_bool_type:
    case script_value_type::jai_char_type:
    case script_value_type::jai_string_type:
        return true;
    case script_value_type::jai_array_type:
        return flat_field_type(t->element_type(), in_progress);
    case script_value_type::jai_map_type:
        return flat_field_type(t->key_type(), in_progress) &&
               flat_field_type(t->value_type(), in_progress);
    case script_value_type::jai_object_type: {
        if (!engine_ || t->type_name.empty()) {
            return false;
        }
        auto nested = engine_->get_class_registry().find_script_class(std::string(t->type_name));
        return nested && nested->flat_value_semantics_impl(in_progress);
    }
    default:
        return false;   // function, shared_ptr/weak_ptr (reference semantics), reference, var
    }
}

checked_result<void> class_registry::register_script_class(std::shared_ptr<script_class_definition> class_def) {
    if (!class_def) {
        return checked_result<void>(make_error_code(runtime_error_code::class_not_found));
    }

    script_classes_[class_def->get_name()] = class_def;

    validate_class_hierarchy(class_def->get_name());

    return {};
}

checked_result<void> class_registry::register_cpp_class(std::shared_ptr<class_definition> class_def) {
    if (!class_def) {
        return checked_result<void>(make_error_code(runtime_error_code::class_not_found));
    }

    cpp_classes_[class_def->get_name()] = class_def;

    return {};
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
    
    auto instance = class_def->create_instance();
    
    // TODO: Handle constructor arguments
    // The current API doesn't provide a way to pass constructor args to create_instance
    // Constructor arguments would need to be handled by the caller after instance creation
    
    return instance;
}

void class_registry::validate_class_hierarchy(const std::string& class_name) {
    auto class_def = find_script_class(class_name);
    if (!class_def) {
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
    
    std::set<std::string> visited;
    std::shared_ptr<class_definition> current = class_def;

    while (current) {
        if (visited.count(current->get_name())) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        visited.insert(current->get_name());
        current = current->get_parent();
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
    
    auto derived_script = find_script_class(derived);
    if (derived_script) {
        // Check if the derived class has the base as a parent
        // Since we can't access parent_class_ directly, we need to use
        // the inheritance information stored during class registration
        
        // For now, we can only check direct parent relationships
        // A full implementation would need to maintain an inheritance graph
        // or have access to the parent_class_ member
        auto cpp_base = derived_script->get_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }
    }
    
    auto derived_cpp = find_cpp_class(derived);
    if (derived_cpp) {
        auto cpp_base = derived_cpp->get_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }

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
}

} // namespace jai