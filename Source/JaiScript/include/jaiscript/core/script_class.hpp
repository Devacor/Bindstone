#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

// Need these for the inline implementation at the end
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/ast.hpp>

namespace jai {

// Forward declarations - class_definition is available since we're included after it's defined
class interpreter;
class function_decl;

// Script class definition - now inherits from class_definition!
class script_class_definition : public class_definition {
public:
    // Constructor
    script_class_definition(const std::string& name, std::weak_ptr<engine> eng) 
        : class_definition(name, class_type::script_class, eng) {
        // That's it! Everything else is inherited from class_definition
    }
    
    // Helper to add a script method from AST
    void add_method_from_ast(const std::string& name, 
                            std::shared_ptr<function_decl> ast,
                            interpreter* interp) {
        // Store AST for potential VM compilation later
        method_asts_[name] = ast;
        
        // Use inherited add_script_method
        add_script_method(name, ast, interp);
        
        // Optionally set metadata if needed
        method_metadata metadata;
        metadata.is_virtual = true;  // Script methods are virtual by default
        set_method_metadata(name, metadata);
    }
    
    // Add constructor from AST
    void add_constructor_from_ast(std::shared_ptr<function_decl> ctor_ast,
                                 interpreter* interp) {
        // Store for later reference
        constructor_asts_.push_back(ctor_ast);
        
        // The constructor is already added to the environment by visit_class_decl
        // We just need to store it for potential VM compilation
    }
    
    // Add destructor from AST
    void add_destructor_from_ast(std::shared_ptr<function_decl> dtor_ast,
                                interpreter* interp) {
        destructor_ast_ = dtor_ast;
        
        // Add as a special method
        add_script_method("~" + get_name(), dtor_ast, interp);
        
        // Mark as virtual
        method_metadata metadata;
        metadata.is_virtual = true;
        set_method_metadata("~" + get_name(), metadata);
    }
    
    // Get ASTs for VM compilation
    const std::map<std::string, std::shared_ptr<function_decl>>& get_method_asts() const {
        return method_asts_;
    }
    
    const std::vector<std::shared_ptr<function_decl>>& get_constructor_asts() const {
        return constructor_asts_;
    }
    
    std::shared_ptr<function_decl> get_destructor_ast() const {
        return destructor_ast_;
    }
    
    // Clear all stored ASTs (for hot reload)
    void clear_asts() {
        method_asts_.clear();
        constructor_asts_.clear();
        destructor_ast_.reset();
    }
    
private:
    // Store ASTs for potential VM compilation
    std::map<std::string, std::shared_ptr<function_decl>> method_asts_;
    std::vector<std::shared_ptr<function_decl>> constructor_asts_;
    std::shared_ptr<function_decl> destructor_ast_;
};

// For backward compatibility during migration
using script_class_instance = class_instance;

// Implementation of add_script_method (needs full interpreter definition)
inline void class_definition::add_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp) {
    methods_.insert_or_assign(name, script_value::make_function(
        [ast, interp](const std::vector<script_value>& args) -> script_value {
            // First argument should be 'this' object
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }
            
            // Extract 'this' from first argument
            script_value this_obj = args[0];
            
            // Create remaining arguments (excluding 'this')
            std::vector<script_value> method_args(args.begin() + 1, args.end());
            
            // Create a new environment for the method that has 'this' defined
            auto method_env = std::make_shared<environment>(
                interp->get_current_environment(), 
                interp->get_string_symbolizer()
            );
            method_env->define("this", this_obj);
            
            // Call the interpreter method directly
            return interp->execute_method_ast(ast, method_env, method_args);
        }
    ));
}

} // namespace jai