#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <iostream>

// Include class_definition from class_builder.hpp
#include <jaiscript/core/class_builder.hpp>

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
    // Constructor - takes pre-interned type_id from parser or runtime
    script_class_definition(const std::string& name, uint64_t type_id, std::weak_ptr<engine> eng)
        : class_definition(name, type_id, class_type::script_class, eng) {
        // That's it! Everything else is inherited from class_definition
    }

    // Helper to add a script method from AST
    void add_method_from_ast(const std::string& name,
                            std::shared_ptr<function_decl> ast,
                            interpreter* interp,
                            bool is_hot_reload = false) {
        // Constructors and destructors don't need override validation
        bool is_constructor = (name == get_name());
        bool is_destructor = (name.size() > 0 && name[0] == '~');
        
        // Find if this method exists in parent classes
        auto method_info = find_method(name);
        
        // Validate override usage (skip for constructors/destructors)
        if (!is_constructor && !is_destructor && ast->is_override) {
            // Debug output
            // std::cerr << "DEBUG: Checking override for method '" << name << "'\n";
            // std::cerr << "  method_info.owner_class: " << (method_info.owner_class ? "exists" : "null") << "\n";
            // if (method_info.owner_class) {
            //     std::cerr << "  owner is this: " << (method_info.owner_class.get() == this) << "\n";
            // }
            
            if (!method_info.owner_class || method_info.owner_class.get() == this) {
                throw runtime_error("Method '" + name + "' marked as override but no base class method found");
            }
            
            // Promote the entire hierarchy to virtual
            // Walk up from the method owner to promote all instances
            auto current = method_info.owner_class;
            while (current) {
                auto* metadata = current->get_method_metadata_mutable(name);
                if (metadata) {
                    if (metadata->is_virtual) {
                        // Already virtual, we're done
                        break;
                    }
                    metadata->is_virtual = true;
                }
                // Check parent for the same method
                current = current->get_parent();
                if (current && !current->has_method(name)) {
                    break; // Method doesn't exist higher up
                }
            }
        } else if (!is_constructor && !is_destructor && !is_hot_reload) {
            // Not marked as override (skip check for constructors/destructors and hot reload)
            if (method_info.owner_class && method_info.owner_class.get() != this) {
                // Method exists in parent
                if (method_info.metadata && method_info.metadata->is_virtual) {
                    // Parent method is virtual, MUST use override
                    throw runtime_error("Method '" + name + "' overrides virtual method from base class. Must use 'override' keyword");
                } else {
                    // Parent method exists but isn't virtual - this is shadowing
                    throw runtime_error("Method '" + name + "' shadows base class method. Use 'override' keyword if this is intentional");
                }
            }
        }
        
        // Store AST for potential VM compilation later
        method_asts_[name] = ast;
        
        // Use inherited add_script_method - pass current environment as definition environment
        add_script_method(name, ast, interp, interp->get_current_environment());
        
        // Set metadata for this method
        method_metadata metadata;
        metadata.is_virtual = ast->is_override || is_destructor;  // Override methods and destructors are virtual
        
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
        
        // Add as a special method (will be marked virtual automatically)
        add_method_from_ast("~" + get_name(), dtor_ast, interp);
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
    
    // Add field initializer AST (will be evaluated at instance construction time)
    void add_field_initializer_ast(const std::string& field_name, expression_ptr initializer) {
        field_initializer_asts_[field_name] = initializer;
    }

    // Get field initializer AST (for instance construction)
    expression_ptr get_field_initializer_ast(const std::string& field_name) const {
        auto it = field_initializer_asts_.find(field_name);
        if (it != field_initializer_asts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Get all field initializer ASTs
    const std::map<std::string, expression_ptr>& get_field_initializer_asts() const {
        return field_initializer_asts_;
    }

    // Clear all stored ASTs (for hot reload)
    void clear_asts() {
        method_asts_.clear();
        constructor_asts_.clear();
        destructor_ast_.reset();
        field_initializer_asts_.clear();
    }

private:
    // Store ASTs for potential VM compilation
    std::map<std::string, std::shared_ptr<function_decl>> method_asts_;
    std::vector<std::shared_ptr<function_decl>> constructor_asts_;
    std::shared_ptr<function_decl> destructor_ast_;
    std::map<std::string, expression_ptr> field_initializer_asts_;
};

// For backward compatibility during migration
using script_class_instance = class_instance;

// Implementation of add_script_method (needs full interpreter definition)
inline void class_definition::add_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env) {
    methods_.insert_or_assign(name, script_value::make_function(
        [ast, interp, name, definition_env](const std::vector<script_value>& args) -> script_value {
            // First argument should be 'this' object
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }

            // Extract 'this' from first argument
            script_value this_obj = args[0];

            // Create remaining arguments (excluding 'this')
            std::vector<script_value> method_args(args.begin() + 1, args.end());

            // Create a method environment that provides implicit 'this' field access
            // Use definition_env (captured at class definition time) as parent
            auto method_env = interp->get_pooled_method_environment(
                definition_env,
                this_obj
            );
            method_env->define("this", this_obj);

            // Call the interpreter method directly
            auto result = interp->execute_method_ast(ast, method_env, method_args);

            // Release the method environment back to the pool
            interp->release_environment(method_env, false);

            return result;
        },
        engine_ref_  // Pass engine reference for proper function value creation
    ));
}

// Implementation of add_static_script_method (needs full interpreter definition)
inline void class_definition::add_static_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env) {
    // Capture shared_ptr to this class definition (C++ scope rules for static methods)
    std::shared_ptr<class_definition> class_def = shared_from_this();

    static_methods_.insert_or_assign(name, script_value::make_function(
        [ast, interp, class_def, definition_env](const std::vector<script_value>& args) -> script_value {
            // Create a static method environment (C++ scope rules for static members)
            // This environment automatically resolves unqualified static member access
            // Use definition_env (captured at class definition time) as parent
            auto static_env = std::make_shared<static_method_environment>(
                definition_env,
                interp->get_string_symbolizer(),
                class_def
            );

            // Call the interpreter method directly without 'this'
            return interp->execute_method_ast(ast, static_env, args);
        },
        engine_ref_  // Pass engine reference for proper function value creation
    ));

    // Also store in overloads map for arity-aware lookup
    uint64_t name_id = ast->name_id;
    if (name_id == UINT64_MAX) {
        name_id = interp->get_string_symbolizer()->intern(name);
    }
    static_method_overloads_[name_id].push_back(ast);
}

// Implementation of has_static_method_with_arity (needs full function_decl definition)
inline bool class_definition::has_static_method_with_arity(uint64_t name_id, size_t arity) const {
    // Check script method overloads (have full AST)
    auto it = static_method_overloads_.find(name_id);
    if (it != static_method_overloads_.end()) {
        for (const auto& func_decl : it->second) {
            if (func_decl->parameters.size() == arity) {
                return true;
            }
        }
    }

    // Check C++ method arities
    auto arity_it = static_method_arities_.find(name_id);
    if (arity_it != static_method_arities_.end()) {
        for (size_t cpp_arity : arity_it->second) {
            if (cpp_arity == arity) {
                return true;
            }
        }
    }

    return false;
}

} // namespace jai