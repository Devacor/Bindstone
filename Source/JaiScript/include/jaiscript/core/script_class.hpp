#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <iostream>
#include <climits>

// Include class_definition (extracted from dynamic_binder.hpp for faster compilation)
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/overload_resolution.hpp>

// Needed for the inline thunk implementations at the end (execute_callable seam)
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/ast.hpp>

namespace jai {

// Forward declarations - class_definition is available since we're included after it's defined
class function_decl;

// Script class definition - now inherits from class_definition!
class script_class_definition : public class_definition {
public:
    // Constructor - takes pre-interned type_id from parser or runtime
    script_class_definition(std::string_view name, uint64_t type_id, engine* eng)
        : class_definition(name, type_id, class_type::script_class, eng) {
        // That's it! Everything else is inherited from class_definition
    }

    // Helper to add a script method from AST
    void add_method_from_ast(std::string_view name,
                            std::shared_ptr<function_decl> ast,
                            std::shared_ptr<environment> definition_env,
                            bool is_hot_reload = false) {
        // Intern the name for fast lookups
        auto eng = get_engine();
        if (!eng) return;
        uint64_t name_id = eng->symbolize(name);

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
                throw runtime_error("Method '" + std::string(name) + "' marked as override but no base class method found");
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
                if (current && !current->has_method(name_id)) {
                    break; // Method doesn't exist higher up
                }
            }
        } else if (!is_constructor && !is_destructor && !is_hot_reload) {
            // Not marked as override (skip check for constructors/destructors and hot reload)
            if (method_info.owner_class && method_info.owner_class.get() != this) {
                // Method exists in parent
                if (method_info.metadata && method_info.metadata->is_virtual) {
                    // Parent method is virtual, MUST use override
                    throw runtime_error("Method '" + std::string(name) + "' overrides virtual method from base class. Must use 'override' keyword");
                } else {
                    // Parent method exists but isn't virtual - this is shadowing
                    throw runtime_error("Method '" + std::string(name) + "' shadows base class method. Use 'override' keyword if this is intentional");
                }
            }
        }
        
        // Store AST for potential VM compilation later (keyed by ID)
        method_asts_[name_id] = ast;

        // Use inherited add_script_method with the caller-supplied definition environment
        add_script_method(name, ast, definition_env);
        
        // Set metadata for this method
        method_metadata metadata;
        metadata.is_virtual = ast->is_override || is_destructor;  // Override methods and destructors are virtual
        
        set_method_metadata(name, metadata);
    }
    
    // Add constructor from AST
    void add_constructor_from_ast(std::shared_ptr<function_decl> ctor_ast) {
        // Store for later reference
        constructor_asts_.push_back(ctor_ast);

        // The constructor is already added to the environment by visit_class_decl
        // We just need to store it for potential VM compilation
    }

    // Add destructor from AST
    void add_destructor_from_ast(std::shared_ptr<function_decl> dtor_ast,
                                std::shared_ptr<environment> definition_env) {
        destructor_ast_ = dtor_ast;

        // Add as a special method (will be marked virtual automatically)
        add_method_from_ast("~" + get_name(), dtor_ast, definition_env);
    }
    
    // Get ASTs for VM compilation
    const std::map<uint64_t, std::shared_ptr<function_decl>>& get_method_asts() const {
        return method_asts_;
    }
    
    const std::vector<std::shared_ptr<function_decl>>& get_constructor_asts() const {
        return constructor_asts_;
    }
    
    std::shared_ptr<function_decl> get_destructor_ast() const {
        return destructor_ast_;
    }
    
    // Add field initializer AST (will be evaluated at instance construction time).
    // Stored in SOURCE DECLARATION ORDER (append), not interned-id order — a field
    // initializer may read a sibling field (this.other), so order must match the
    // source. (Previously a std::map keyed by name_id ran initializers in interning
    // order, which only coincides with declaration order if each name is first seen
    // at its declaration; a name interned earlier elsewhere broke it.)
    void add_field_initializer_ast(uint64_t field_id, expression_ptr initializer) {
        for (auto& [id, ast] : field_initializer_asts_) {
            if (id == field_id) { ast = initializer; return; }
        }
        field_initializer_asts_.emplace_back(field_id, initializer);
    }

    // Get field initializer AST (for instance construction)
    expression_ptr get_field_initializer_ast(uint64_t field_id) const {
        for (const auto& [id, ast] : field_initializer_asts_) {
            if (id == field_id) return ast;
        }
        return nullptr;
    }

    // Get all field initializer ASTs (in declaration order)
    const std::vector<std::pair<uint64_t, expression_ptr>>& get_field_initializer_asts() const {
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
    // Store ASTs for potential VM compilation (keyed by name_id for efficiency)
    std::map<uint64_t, std::shared_ptr<function_decl>> method_asts_;
    std::vector<std::shared_ptr<function_decl>> constructor_asts_;
    std::shared_ptr<function_decl> destructor_ast_;
    // Vector (not map) to preserve source declaration order for field initializers.
    std::vector<std::pair<uint64_t, expression_ptr>> field_initializer_asts_;
};

// For backward compatibility during migration
using script_class_instance = class_instance;

// Instance-method dispatcher body (declared in execution_backend.hpp). Resolves the
// overload against the live class every call (hot reload) and executes through the
// current backend; args[0] is the 'this' receiver.
inline checked_result<script_value> script_method_dispatch::operator()(const std::vector<script_value>& args) const {
    if (args.empty()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::this_outside_method), "Method called without 'this' object");
    }

    std::vector<script_value> method_args(args.begin() + 1, args.end());

    auto resolved = cls->resolve_method_overload(name_id, method_args);
    if (!resolved) {
        return resolved.error_value();
    }

    execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
    if (!backend) {
        return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
    }

    script_callable payload;
    payload.kind = script_callable::kind_type::method;
    payload.ast = std::move(resolved.value());
    payload.cls = cls;
    payload.definition_env = definition_env;
    payload.this_obj = args[0];
    return backend->execute_callable(payload, method_args);
}

// Implementation of add_script_method. The stored dispatcher captures only
// (engine*, backend-neutral payload) and resolves the live backend at call time.
inline void class_definition::add_script_method(std::string_view name, std::shared_ptr<function_decl> ast, std::shared_ptr<environment> definition_env) {
    engine* eng = get_engine();
    if (!eng) return;

    // Track if this is a property getter for fast-path optimization
    if (name.size() > 5 && name.substr(0, 5) == "_get_") {
        has_property_getters_ = true;
    }

    uint64_t name_id = eng->symbolize(name);

    // Store the AST in method_overloads_ for type-based resolution
    method_overloads_[name_id].push_back(ast);

    // Invalidate overload resolution cache for this method name (new overload may change resolution)
    for (auto it = overload_resolution_cache_.begin(); it != overload_resolution_cache_.end(); ) {
        if (it->first.name_id == name_id) {
            it = overload_resolution_cache_.erase(it);
        } else {
            ++it;
        }
    }

    // Capture shared_ptr to this class definition for overload resolution
    // (intentional lifetime cycle: methods_ -> thunk -> class_def keeps the class alive
    // for bound-method copies)
    std::shared_ptr<class_definition> class_def = shared_from_this();

    methods_.insert_or_assign(name_id, script_value::make_function(
        script_method_dispatch{eng, class_def, name_id, definition_env}, eng));
    bump_method_epoch();
}

// Implementation of add_static_script_method. Same engine*+payload pattern; overloads
// are resolved over static_method_overloads_ with the shared resolver, falling back to
// the mint-time AST when nothing matches (preserving the old last-registered behavior).
inline void class_definition::add_static_script_method(std::string_view name, std::shared_ptr<function_decl> ast, std::shared_ptr<environment> definition_env) {
    engine* eng = get_engine();
    if (!eng) return;

    // Track if this is a property getter for fast-path optimization
    if (name.size() > 5 && name.substr(0, 5) == "_get_") {
        has_property_getters_ = true;
    }

    uint64_t name_id = eng->symbolize(name);

    // Capture shared_ptr to this class definition (C++ scope rules for static methods)
    std::shared_ptr<class_definition> class_def = shared_from_this();

    // Store in overloads map for arity-aware lookup
    uint64_t method_name_id = ast->name_id;
    if (method_name_id == UINT64_MAX) {
        method_name_id = name_id;
    }
    static_method_overloads_[method_name_id].push_back(ast);

    static_methods_.insert_or_assign(name_id, script_value::make_function(
        [ast, class_def, definition_env, eng, method_name_id](const std::vector<script_value>& args) -> checked_result<script_value> {
            execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
            if (!backend) {
                return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
            }

            script_callable payload;
            payload.kind = script_callable::kind_type::static_method;
            payload.ast = ast;
            payload.cls = class_def;
            payload.definition_env = definition_env;

            auto it = class_def->static_method_overloads_.find(method_name_id);
            if (it != class_def->static_method_overloads_.end() && it->second.size() > 1) {
                if (auto best = pick_best_overload(it->second, args)) {
                    payload.ast = best;
                }
            }

            return backend->execute_callable(payload, args);
        },
        eng
    ));
}

// Implementation of has_static_method_with_arity (needs full function_decl definition)
inline bool class_definition::has_static_method_with_arity(uint64_t name_id, size_t arity) const {
    // Check script method overloads (have full AST)
    auto it = static_method_overloads_.find(name_id);
    if (it != static_method_overloads_.end()) {
        for (const auto& func_decl : it->second) {
            if (arity_accepts(func_decl->parameters, arity)) {   // trailing defaults widen
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

// Dev ruling (2026-07): a defaulted-parameter constructor is NOT a converting
// constructor (deliberate C++ divergence) - implicit conversion requires a TRUE
// one-parameter constructor; opt in with a delegating overload
// (Money(int a) : this(a, 0) {}). Explicit construction keeps the arity window.
// Host-registered classes are unaffected (their ctor overloads are exact-arity).
inline bool has_true_single_param_conversion_ctor(const std::shared_ptr<class_definition>& def) {
    auto script_def = std::dynamic_pointer_cast<script_class_definition>(def);
    if (!script_def) { return true; }   // host classes: the dispatcher stays authoritative
    for (const auto& ctor_ast : script_def->get_constructor_asts()) {
        if (ctor_ast->parameters.size() == 1) { return true; }
    }
    return false;
}

// Collect a class declaration's private:/protected: labels into the runtime access map
// (both backends call this from their class-decl execution — parity by construction).
// Constructors are exempt: visibility labels govern member access, not construction.
template<typename Symbolizer>
inline void apply_member_access_labels(class_definition& def, const class_decl& decl, Symbolizer& symbols) {
    std::unordered_map<uint64_t, access_level> nonpublic;
    for (const auto& member : decl.members) {
        if (member.visibility == class_decl::Public || !member.declaration) continue;
        uint64_t id = UINT64_MAX;
        if (member.declaration->get_type() == node_type::variable_decl) {
            auto* vd = static_cast<variable_decl*>(member.declaration.get());
            id = vd->name_id != UINT64_MAX ? vd->name_id : symbols.intern(std::string(vd->name));
        } else if (member.declaration->get_type() == node_type::function_decl) {
            auto* fd = static_cast<function_decl*>(member.declaration.get());
            if (fd->name == decl.name) continue;   // constructor
            id = fd->name_id != UINT64_MAX ? fd->name_id : symbols.intern(std::string(fd->name));
        }
        if (id != UINT64_MAX) {
            nonpublic.emplace(id, member.visibility == class_decl::Private
                                      ? access_level::private_access
                                      : access_level::protected_access);
        }
    }
    def.set_nonpublic_members(std::move(nonpublic));
}

} // namespace jai