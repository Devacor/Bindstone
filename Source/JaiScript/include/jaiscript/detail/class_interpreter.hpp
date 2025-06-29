#pragma once

#include "jaiscript/detail/interpreter.hpp"
#include "jaiscript/detail/class_parser.hpp"
#include "jaiscript/core/script_class.hpp"
#include "jaiscript/core/class_registry.hpp"

namespace jai {

// Class interpreter extensions for executing class-related AST nodes
class class_interpreter : public class_visitor {
public:
    explicit class_interpreter(interpreter& parent_interpreter);
    
    // Class visitor implementation
    void visit_script_class_decl(script_class_decl* stmt) override;
    void visit_field_decl(field_decl* stmt) override;
    void visit_method_decl(method_decl* stmt) override;
    void visit_constructor_decl(constructor_decl* stmt) override;
    void visit_destructor_decl(destructor_decl* stmt) override;
    void visit_script_super_expr(script_super_expr* expr) override;
    
    // Helper methods for class operations
    std::shared_ptr<script_class_instance> create_instance(
        const std::string& class_name,
        const std::vector<script_value>& args
    );
    
    script_value call_method_on_instance(
        std::shared_ptr<script_class_instance> instance,
        const std::string& method_name,
        const std::vector<script_value>& args
    );
    
    script_value get_field_value(
        std::shared_ptr<script_class_instance> instance,
        const std::string& field_name
    );
    
    void set_field_value(
        std::shared_ptr<script_class_instance> instance,
        const std::string& field_name,
        const script_value& value
    );
    
private:
    interpreter& interpreter_;
    
    // Current class context during compilation
    std::shared_ptr<script_class_definition> current_class_def_;
    std::shared_ptr<script_class_instance> current_instance_;
    
    // Helper methods
    std::shared_ptr<function_decl> create_function_from_method(method_decl* method);
    std::shared_ptr<function_decl> create_function_from_constructor(constructor_decl* ctor);
    std::shared_ptr<function_decl> create_function_from_destructor(destructor_decl* dtor);
    
    script_value evaluate_default_value(expression* default_expr);
    void validate_method_signature(method_decl* method);
    void validate_field_access(const std::string& field_name);
    void validate_method_access(const std::string& method_name);
    
    // Context management
    void push_class_context(std::shared_ptr<script_class_definition> class_def);
    void pop_class_context();
    void push_instance_context(std::shared_ptr<script_class_instance> instance);
    void pop_instance_context();
    
    std::shared_ptr<script_class_instance> get_current_instance();
    std::vector<script_value> get_current_args();
};

// Access control validator for script-to-script access
class script_access_controller {
public:
    // Only validate access within script boundaries
    bool can_access(
        const std::string& from_class,
        const std::string& to_class,
        const std::string& member_name,
        access_level member_access
    );
    
private:
    bool is_script_class(const std::string& class_name);
    bool is_inheritance_related(const std::string& class1, const std::string& class2);
    bool is_base_of(const std::string& base, const std::string& derived);
};

} // namespace jai