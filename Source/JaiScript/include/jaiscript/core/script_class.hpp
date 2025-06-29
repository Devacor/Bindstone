#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <functional>
#include "jaiscript/core/value.hpp"
#include "jaiscript/core/function_binder.hpp"

namespace jai {

// Forward declarations
struct script_class_instance;
struct script_class_definition;
struct method_info;
struct field_declaration;
struct method_declaration;
struct constructor_declaration;
struct function_decl;

enum class access_level {
    public_access,
    private_access,
    protected_access
};

enum class delegation_type {
    none,
    same_class,
    base_class
};

// Method information for dispatch
struct method_info {
    enum type { 
        script_direct, 
        script_virtual, 
        cpp, 
        destructor 
    } method_type = script_direct;
    
    std::string name;
    std::vector<std::string> param_types;
    access_level access = access_level::public_access;
    
    // For script methods
    std::shared_ptr<function_decl> script_method = nullptr;
    
    // For C++ methods
    std::function<script_value(const std::vector<script_value>&)> cpp_method = nullptr;
    
    // Dynamic virtualization
    bool is_virtual = false;
    bool is_override = false;
    bool can_be_virtualized = true;
    
    // TODO: Add compiled bytecode for script methods
    // Currently methods execute via interpreter even in VM
    // std::shared_ptr<bytecode_module> compiled_method = nullptr;
};

// Field declaration
struct field_declaration {
    std::string name;
    std::string type_name;
    script_value default_value;
    access_level access = access_level::public_access;
};

// Method declaration
struct method_declaration {
    std::string name;
    std::vector<std::string> parameters;
    std::string return_type;
    std::shared_ptr<function_decl> implementation;
    access_level access = access_level::public_access;
    bool is_override = false;
    bool is_virtual = true;
};

// Constructor declaration
struct constructor_declaration {
    std::string class_name;
    std::vector<std::string> parameters;
    std::shared_ptr<function_decl> implementation;
    
    // Delegation information
    bool is_delegating = false;
    delegation_type delegation_type_val = delegation_type::none;
    std::vector<script_value> delegation_args;
};

// Core script class definition
struct script_class_definition {
    std::string name;
    std::vector<field_declaration> fields;
    std::map<std::string, method_info> methods;
    std::vector<constructor_declaration> constructors;
    std::shared_ptr<function_decl> destructor = nullptr;
    bool destructor_is_virtual = false;
    std::shared_ptr<script_class_definition> base_class = nullptr;
    access_level default_access = access_level::public_access;
    
    // Method caching per class - more efficient than per-call-site
    mutable std::unordered_map<std::string, method_info*> method_cache_;
    
    // Centralized method dispatch - same pattern as destructor dispatch
    script_value call_method(
        script_class_instance* instance,
        const std::string& method_name,
        const std::vector<script_value>& args
    ) const;
    
    // Centralized destructor dispatch
    void call_destructor(script_class_instance* instance) const;
    
    // Method lookup
    method_info* find_method(const std::string& method_name) const;
    method_info* find_method_in_hierarchy(const std::string& method_name) const;
    
    // Virtual promotion
    void promote_method_to_virtual(const std::string& method_name);
    void promote_destructor_to_virtual();
    
private:
    script_value call_method_direct(
        script_class_instance* instance,
        method_info* method,
        const std::vector<script_value>& args
    ) const;
    
    script_value call_method_virtual(
        script_class_instance* instance,
        const std::string& method_name,
        const std::vector<script_value>& args
    ) const;
    
    void call_destructor_direct(script_class_instance* instance) const;
    void call_destructor_virtual(script_class_instance* instance) const;
    
    std::shared_ptr<script_class_definition> find_actual_class(script_class_instance* instance) const;
};

// Script class instance
struct script_class_instance : public std::enable_shared_from_this<script_class_instance> {
    std::string class_name;
    std::shared_ptr<script_class_definition> class_def;
    std::map<std::string, script_value> fields;
    std::shared_ptr<void> cpp_object = nullptr;  // C++ base object (if inheriting)
    
    // Always delegate to class definition for consistent dispatch
    ~script_class_instance() {
        if (class_def) {
            class_def->call_destructor(this);
        }
    }
};

// Hybrid class instance for C++/script inheritance
struct hybrid_class_instance : script_class_instance {
    std::shared_ptr<void> cpp_base_object;
    std::string cpp_base_type_name;
    
    template<typename T>
    T& get_cpp_base() {
        return *static_cast<T*>(cpp_base_object.get());
    }
};

// Constructor resolver for delegation chains
class constructor_resolver {
public:
    // TODO: Implement constructor delegation resolution
    // Currently no implementation for constructor chaining
    
    // Resolve constructor delegation chain
    std::vector<constructor_declaration*> resolve_delegation_chain(
        const std::string& class_name,
        const std::vector<script_value>& args
    );
    
    // Execute constructor with delegation
    void execute_constructor_chain(
        std::shared_ptr<script_class_instance> instance,
        const std::vector<constructor_declaration*>& chain,
        const std::vector<script_value>& original_args
    );
    
    // Validate no circular delegation
    bool validate_no_cycles(const std::string& class_name);
};

// Virtual method promoter for dynamic virtualization
class virtual_method_promoter {
public:
    // Called when derived class defines override method
    void promote_to_virtual(
        std::shared_ptr<script_class_definition> base_class,
        const std::string& method_name
    );
    
    // Validate override requirements
    void validate_override(
        std::shared_ptr<script_class_definition> derived_class,
        const method_declaration& method
    );
    
    // Promote destructor to virtual when derived class has destructor
    void promote_destructor_to_virtual(std::shared_ptr<script_class_definition> base_class);
};

// Override validator for strict override enforcement
class override_validator {
public:
    // Called during class registration
    void validate_overrides(std::shared_ptr<script_class_definition> class_def);
    
private:
    void validate_signature_compatibility(
        const method_declaration& derived,
        const method_declaration& base
    );
    
    method_declaration* find_method_in_hierarchy(
        std::shared_ptr<script_class_definition> class_def,
        const std::string& method_name
    );
};

// Simplified method call interface
script_value call_method(
    std::shared_ptr<script_class_instance> instance,
    const std::string& method_name,
    const std::vector<script_value>& args
);

// Factory function for creating script class instances
template<typename... Args>
std::shared_ptr<script_class_instance> make_shared_script_class(
    const std::string& class_name,
    Args&&... args
);

// TODO: Implement factory function template
// Currently no implementation for creating script class instances

} // namespace jai