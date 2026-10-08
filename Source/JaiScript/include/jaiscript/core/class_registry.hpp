#pragma once

#include "jaiscript/core/dynamic_binder.hpp"
#include "jaiscript/core/script_class.hpp"
#include <memory>
#include <map>
#include <string>

namespace jai {

// Base class definition for unified registry
struct base_class_definition {
    std::string name;
    virtual ~base_class_definition() = default;
};

// Unified class registry for both script and C++ classes
class class_registry {
public:
    
    // Register script-defined class
    checked_result<void> register_script_class(std::shared_ptr<script_class_definition> class_def);

    checked_result<void> register_cpp_class(std::shared_ptr<class_definition> class_def);
    
    // Unified lookup
    std::shared_ptr<script_class_definition> find_script_class(const std::string& name);
    std::shared_ptr<class_definition> find_cpp_class(const std::string& name);
    std::shared_ptr<base_class_definition> find_class(const std::string& name);
    
    // Check inheritance relationships
    bool is_assignable_from(const std::string& derived, const std::string& base);
    bool is_script_class(const std::string& name);
    bool is_cpp_class(const std::string& name);
    
    // Class instantiation
    std::shared_ptr<script_class_instance> create_script_instance(
        const std::string& class_name,
        const std::vector<script_value>& args
    );
    
    // Validation and promotion
    void validate_class_hierarchy(const std::string& class_name);
    void promote_methods_to_virtual(
        const std::string& base_class_name,
        const std::string& derived_class_name
    );
    
    class_registry() = default;
    
    std::map<std::string, std::shared_ptr<script_class_definition>> script_classes_;
    std::map<std::string, std::shared_ptr<class_definition>> cpp_classes_;
    
    // Validators and promoters
    // TODO: Implement these from the tech spec
    // override_validator override_validator_;
    // virtual_method_promoter method_promoter_;
    // constructor_resolver constructor_resolver_;
    
    // Helper methods
    bool check_inheritance_chain(const std::string& derived, const std::string& base);
    void apply_default_field_values(
        std::shared_ptr<script_class_instance> instance,
        std::shared_ptr<script_class_definition> class_def
    );
};

// A slot declared `t` can own an instance whose destruction runs a script destructor: a by-value class, array or map whose
// value tree can hold one, a shared_ptr whose pointee's can (it may hold the last handle), a var or auto slot while any
// script class declares a destructor; never a primitive, a string, a weak_ptr or a reference. Decides which locals a scope
// exit destroys (both backends).
bool slot_may_own_destructor(engine& eng, const type_info_ptr& t);

} // namespace jai