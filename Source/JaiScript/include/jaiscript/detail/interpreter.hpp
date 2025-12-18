#pragma once

#ifndef __JAISCRIPT_DETAIL_INTERPRETER_HPP__
#define __JAISCRIPT_DETAIL_INTERPRETER_HPP__

// ============================================================
// Configurable limits - define these before including to override
// ============================================================

// Maximum function call recursion depth (default: 10000)
// This is high enough for complex valid code but prevents stack overflow
#ifndef JAI_MAX_CALL_DEPTH
#define JAI_MAX_CALL_DEPTH 10000
#endif

#include "ast.hpp"
#include "string_symbolizer.hpp"
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/types.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <deque>
#include <iostream>
#include <system_error>
#include <optional>

namespace jai {

    // Forward declarations
    class script_class_definition;

    // environment for storing variables in a scope
    class environment {
    public:
        environment(string_symbolizer* symbolizer) : symbolizer_(symbolizer) {}
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer)
            : parent_(parent), symbolizer_(symbolizer) {
            validate_parent_chain(parent);
            // Lazy caching: start empty, populate on first access
            // This gives O(1) construction instead of O(N) map copy
            // Lookups walk parent chain on cache miss, then cache for O(1) subsequent access
        }
        virtual ~environment() = default;
        
        // Reference tracking for proper reference parameter support
        struct ref_info {
            uint64_t target_id;  // Symbol ID of the target variable
            std::shared_ptr<environment> target_env;  // environment containing the target
        };
        
        // Define a variable in the current scope
        void define(const std::string& name, const script_value& value);
        void define(const std::string& name, script_value&& value);
        void define(uint64_t id, const script_value& value);
        void define(uint64_t id, script_value&& value);
        
        // Get a variable value (searches parent scopes)
        // Returns checked_result to avoid exceptions in regular control flow
        virtual checked_result<script_value> get(const std::string& name) const;
        virtual checked_result<script_value> get(uint64_t id) const;  // Optimized direct lookup
        
        // Get a reference to variable value (for avoiding copies)
        // Returns checked_result to avoid exceptions in regular control flow
        virtual checked_result<std::reference_wrapper<const script_value>> get_ref(const std::string& name) const;
        virtual checked_result<std::reference_wrapper<const script_value>> get_ref(uint64_t id) const;
        virtual checked_result<std::reference_wrapper<script_value>> get_ref(const std::string& name);
        virtual checked_result<std::reference_wrapper<script_value>> get_ref(uint64_t id);
        
        // Assign to an existing variable (searches parent scopes)
        // Returns checked_result to avoid exceptions in regular control flow
        [[nodiscard]] virtual checked_result<void> assign(const std::string& name, const script_value& value);
        [[nodiscard]] virtual checked_result<void> assign(const std::string& name, script_value&& value);
        [[nodiscard]] virtual checked_result<void> assign(uint64_t id, const script_value& value);
        [[nodiscard]] virtual checked_result<void> assign(uint64_t id, script_value&& value);
        
        // Check if variable exists in current or parent scopes
        bool contains(const std::string& name) const;
        bool contains(uint64_t id) const;
        
        // Get a pointer to a value (for references)
        script_value* get_value_ptr(uint64_t id);
        
        // Get all variables in this scope (not including parent scopes)
        // Returns a map with string_view keys pointing into the symbolizer (stable until engine destruction)
        std::unordered_map<std::string_view, script_value> get_local_variables() const;

        // Get all variables including parent scopes
        std::unordered_map<std::string_view, script_value> get_all_variables() const;
        
        // Reset environment for reuse (optimization helper)
        virtual void reset(std::shared_ptr<environment> new_parent);

        // Clear all values but keep parent chain intact (for proper scope cleanup)
        void clear_values();

        // Get parent environment (needed for method_environment handling)
        std::shared_ptr<environment> get_parent() const { return parent_; }

        // Set parent environment with validation (use this instead of directly setting parent_)
        void set_parent(std::shared_ptr<environment> new_parent) {
            validate_parent_chain(new_parent);
            parent_ = new_parent;
        }

    protected:
        // Debug helper to validate parent chain doesn't create a cycle
        // Only active when JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES is defined
        void validate_parent_chain(std::shared_ptr<environment> new_parent) const {
/*
#ifdef JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES
            if (!new_parent) return;  // nullptr parent is always valid

            // Check if new_parent's chain eventually points back to 'this'
            std::unordered_set<const environment*> visited;
            auto current = new_parent;

            while (current) {
                // If we encounter 'this', we're creating a cycle!
                if (current.get() == this) {
                    std::cerr << "ERROR: Circular parent chain detected!\n";
                    std::cerr << "  Attempting to set parent of environment " << this
                              << " (type: " << typeid(*this).name() << ")\n";
                    std::cerr << "  to parent " << new_parent.get()
                              << " (type: " << typeid(*new_parent).name() << ")\n";
                    std::cerr << "  but new_parent's chain already includes this environment!\n";
                    std::cerr << "  Chain depth before cycle: " << visited.size() << "\n";

                    // Print the chain
                    std::cerr << "  Parent chain: ";
                    auto trace = new_parent;
                    int depth = 0;
                    while (trace && depth < 10) {
                        std::cerr << trace.get() << " (" << typeid(*trace).name() << ")";
                        if (trace.get() == this) {
                            std::cerr << " <- CYCLE HERE!";
                        }
                        trace = trace->parent_;
                        if (trace) std::cerr << " -> ";
                        depth++;
                    }
                    std::cerr << "\n";

                    // Throw to break and get a stack trace
                    throw runtime_error("Circular environment parent chain detected!");
                }

                // Check for other cycles in the parent chain
                if (visited.count(current.get()) > 0) {
                    std::cerr << "ERROR: Parent chain already has a cycle before this assignment!\n";
                    std::cerr << "  Environment at " << current.get()
                              << " (type: " << typeid(*current).name() << ") appears twice\n";
                    throw runtime_error("Circular environment parent chain detected in new_parent's existing chain!");
                }

                visited.insert(current.get());
                current = current->parent_;

                // Sanity check - prevent infinite loops even in debug mode
                if (visited.size() > 1000) {
                    std::cerr << "ERROR: Parent chain depth exceeds 1000! Likely a cycle.\n";
                    throw runtime_error("Parent chain too deep - likely circular!");
                }
            }
#endif
*/
        }

    private:
        // === Lazy-Cached Flat Lookup ===
        // Stable storage for THIS scope's values - deque doesn't invalidate pointers on push_back
        std::deque<script_value> local_storage_;

        // Lazy lookup cache - starts empty, populated on first access to each variable
        // Key: symbol ID, Value: pointer to script_value in some environment's local_storage_
        // mutable because const lookup methods cache for performance (doesn't change logical state)
        mutable std::unordered_map<uint64_t, script_value*> flat_lookup_;

        // Track which IDs we defined locally (for O(1) "is local" checks)
        std::unordered_set<uint64_t> local_ids_;

        // Parent pointer kept for: closure semantics, method_environment 'this' lookup, debugging
        std::shared_ptr<environment> parent_;
        string_symbolizer* symbolizer_;

        // Legacy: Internal get method with recursion depth tracking (only used as fallback now)
        checked_result<script_value> get(uint64_t id, int depth) const;
        checked_result<std::reference_wrapper<const script_value>> get_ref(uint64_t id, int depth) const;

        friend class interpreter;
        friend class method_environment;
        friend class static_method_environment;
    };
    
    // Special environment for script class methods that provides implicit 'this' field access
    class method_environment : public environment {
    public:
        method_environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, script_value this_obj)
            : environment(parent, symbolizer), this_object_(std::move(this_obj)),
              bound_method_storage_(std::monostate{}, std::weak_ptr<engine>{}) {
        }
        ~method_environment() override = default;

        // Override get to check 'this' object fields as fallback
        checked_result<script_value> get(const std::string& name) const override;
        checked_result<script_value> get(uint64_t id) const override;

        // Override get_ref to check 'this' object fields as fallback
        checked_result<std::reference_wrapper<const script_value>> get_ref(const std::string& name) const override;
        checked_result<std::reference_wrapper<const script_value>> get_ref(uint64_t id) const override;
        checked_result<std::reference_wrapper<script_value>> get_ref(const std::string& name) override;
        checked_result<std::reference_wrapper<script_value>> get_ref(uint64_t id) override;
        
        // Override assign to update 'this' object fields when appropriate
        [[nodiscard]] checked_result<void> assign(const std::string& name, const script_value& value) override;
        [[nodiscard]] checked_result<void> assign(uint64_t id, const script_value& value) override;
        [[nodiscard]] checked_result<void> assign(uint64_t id, script_value&& value) override;

        // Override contains to include 'this'
        bool contains(const std::string& name) const;
        bool contains(uint64_t id) const;

        // Get the 'this' object for super access
        const script_value& get_this_object() const { return this_object_; }
        
        // Reset method for pooling
        void reset(std::shared_ptr<environment> parent, script_value this_obj) {
            // Clear local values (removes from flat_lookup_ too)
            clear_values();
            // Use set_parent helper which validates the chain (debug mode only)
            set_parent(parent);
            // With lazy caching, we start with empty flat_lookup_ (no copy needed)
            flat_lookup_.clear();
            this_object_ = std::move(this_obj);
        }

        // Override base reset to clear this_object_
        void reset(std::shared_ptr<environment> new_parent) override {
            // Clear local values using base implementation
            environment::reset(new_parent);
            // Also clear the this object to release its reference (no engine ref needed for cleanup)
            this_object_ = script_value::make_null(std::weak_ptr<engine>{});
            bound_method_storage_ = script_value::make_null(std::weak_ptr<engine>{});
        }

        // Clear just the this_object without clearing local values
        // Called when a method/constructor returns to ensure timely destruction
        void clear_this_reference() {
            this_object_ = script_value::make_null(std::weak_ptr<engine>{});
            bound_method_storage_ = script_value::make_null(std::weak_ptr<engine>{});
        }

    private:
        script_value this_object_;
        mutable script_value bound_method_storage_;  // Storage for bound methods
    };
    
    // Special environment for static script methods that provides implicit static field access
    class static_method_environment : public environment {
    public:
        static_method_environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, std::shared_ptr<class_definition> class_def)
            : environment(parent, symbolizer), class_def_(class_def),
              bound_method_storage_(std::monostate{}, std::weak_ptr<engine>{}) {
        }
        ~static_method_environment() override = default;

        // Override get to check static fields as fallback
        checked_result<script_value> get(const std::string& name) const override;
        checked_result<script_value> get(uint64_t id) const override;

        // Override get_ref to check static fields as fallback
        checked_result<std::reference_wrapper<const script_value>> get_ref(const std::string& name) const override;
        checked_result<std::reference_wrapper<const script_value>> get_ref(uint64_t id) const override;
        checked_result<std::reference_wrapper<script_value>> get_ref(const std::string& name) override;
        checked_result<std::reference_wrapper<script_value>> get_ref(uint64_t id) override;
        
        // Override assign to update static fields when appropriate
        [[nodiscard]] checked_result<void> assign(const std::string& name, const script_value& value) override;
        [[nodiscard]] checked_result<void> assign(const std::string& name, script_value&& value) override;
        [[nodiscard]] checked_result<void> assign(uint64_t id, const script_value& value) override;
        [[nodiscard]] checked_result<void> assign(uint64_t id, script_value&& value) override;

        // Get the class definition for creating child static environments
        std::shared_ptr<class_definition> get_class_definition() const { return class_def_; }

    private:
        std::shared_ptr<class_definition> class_def_;
        mutable script_value bound_method_storage_;  // Storage for bound methods
    };

    // Forward declare interpreter for scoped_method_environment
    class interpreter;

    // RAII wrapper for method environments to ensure proper cleanup
    // Automatically defines 'this' and releases the environment with clear=true on destruction
    class scoped_method_environment {
    public:
        scoped_method_environment(
            interpreter* interp,
            std::shared_ptr<environment> parent,
            const script_value& this_obj);

        ~scoped_method_environment();

        // Get the managed environment
        std::shared_ptr<method_environment> get() const { return env_; }

        // Implicit conversion to method_environment for convenience
        operator std::shared_ptr<method_environment>() const { return env_; }

        // Prevent copying
        scoped_method_environment(const scoped_method_environment&) = delete;
        scoped_method_environment& operator=(const scoped_method_environment&) = delete;

    private:
        interpreter* interp_;
        std::shared_ptr<method_environment> env_;
    };

    // The interpreter implements the visitor pattern to execute the AST
    class interpreter : public ast_visitor, public std::enable_shared_from_this<interpreter> {
    public:
        using class_lookup_callback = std::function<std::shared_ptr<class_definition>(const std::string&)>;
        
        // Method type for built-in type methods
        using builtin_method = std::function<checked_result<script_value>(interpreter*, script_value&, const std::vector<script_value>&)>;
        
        interpreter();
        interpreter(string_symbolizer* external_symbolizer);
        interpreter(string_symbolizer* external_symbolizer, std::shared_ptr<environment> global_env);
        ~interpreter() = default;
        
        // Add global variables before execution
        void add_globals(const std::unordered_map<std::string, script_value>& globals);
        
        // Add a single global variable
        void add_global(const std::string& name, const script_value& value);
        
        // Prepare interpreter for a new execution (clears transient state)
        void prepare_for_execution();
        
        // Scope management for local variables
        void push_scope();
        void pop_scope();
        
        // Define a variable in the current scope
        void define_variable(const std::string& name, const script_value& value);
        
        // Set class lookup callback (for finding C++ classes)
        void set_class_lookup_callback(class_lookup_callback callback) {
            class_lookup_callback_ = std::move(callback);
        }
        
        // Set engine reference for script_value creation
        void set_engine_reference(std::weak_ptr<engine> engine_ref) {
            engine_ref_ = std::move(engine_ref);

            // Initialize cached common values to avoid repeated allocations
            cached_null_ = script_value(std::monostate{}, engine_ref_);
            cached_true_ = script_value(true, engine_ref_);
            cached_false_ = script_value(false, engine_ref_);
            cached_zero_int_ = script_value(static_cast<script_int>(0), engine_ref_);
            cached_one_int_ = script_value(static_cast<script_int>(1), engine_ref_);
            cached_zero_float_ = script_value(0.0, engine_ref_);
            cached_one_float_ = script_value(1.0, engine_ref_);
        }

        // Get engine reference (for internal use in lambdas)
        std::weak_ptr<engine> get_engine_ref() const {
            return engine_ref_;
        }
        
        // Helper to create script_value with engine context
        script_value make_value(script_int i) const {
            if (!engine_ref_.expired()) {
                // Return cached common values to avoid allocations
                if (i == 0 && cached_zero_int_.has_value()) return *cached_zero_int_;
                if (i == 1 && cached_one_int_.has_value()) return *cached_one_int_;
                return script_value(i, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }

        script_value make_value(script_float f) const {
            if (!engine_ref_.expired()) {
                // Return cached common values to avoid allocations
                if (f == 0.0 && cached_zero_float_.has_value()) return *cached_zero_float_;
                if (f == 1.0 && cached_one_float_.has_value()) return *cached_one_float_;
                return script_value(f, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }
        
        script_value make_value(const script_string& s) const {
            if (!engine_ref_.expired()) {
                return script_value(s, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }

        // Move overload for string temporaries (avoids copy)
        script_value make_value(script_string&& s) const {
            if (!engine_ref_.expired()) {
                return script_value(std::move(s), engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }
        
        script_value make_value(script_bool b) const {
            if (!engine_ref_.expired()) {
                // Return cached common values to avoid allocations
                if (b && cached_true_.has_value()) return *cached_true_;
                if (!b && cached_false_.has_value()) return *cached_false_;
                return script_value(b, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }

        script_value make_value(script_char c) const {
            if (!engine_ref_.expired()) {
                return script_value(c, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }

        script_value make_value() const {
            if (!engine_ref_.expired()) {
                // Return cached null value
                if (cached_null_.has_value()) return *cached_null_;
                return script_value(std::monostate{}, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }

        // Helper to create namespace-related objects with both type_name and type_id for fast comparison
        template<typename T>
        script_value make_namespace_object(uint64_t type_id, const char* type_name, std::shared_ptr<T> data) const {
            return script_value::make_object(type_name, type_id, std::static_pointer_cast<void>(data), engine_ref_);
        }

        // Execute a list of declarations and return the last value
        script_value execute(const std::vector<declaration_ptr>& declarations);
        
        // Execute with typed return value (throws if no return statement or type doesn't match)
        template<typename T>
        T execute(const std::vector<declaration_ptr>& declarations) {
            execute(declarations);  // Execute the script
            
            // Require a return statement when requesting typed result
            if (!hasReturnValue_) {
                throw runtime_error("Script must have a return statement when requesting typed result");
            }
            
            // Try to convert the return value to the requested type
            return returnValue_.value().as<T>();
        }
        
        // Execute a single expression and return its value
        script_value evaluate(expression_ptr expr);
        
        // Return value access (for global scope return statements)
        bool has_return_value() const { return hasReturnValue_; }
        script_value get_return_value() const {
            return returnValue_.value_or(make_value());
        }
        
        // Variable access methods
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        std::unordered_map<std::string_view, script_value> get_all_variables() const;
        
        // Exception handling methods
        bool is_unwinding() const { return is_unwinding_; }
        const script_exception& get_current_exception() const { return current_exception_.value(); }

        // expression visitors
        checked_result<void> visit_literal_expr(literal_expr* expr) override;
        checked_result<void> visit_identifier_expr(identifier_expr* expr) override;
        checked_result<void> visit_binary_expr(binary_expr* expr) override;
        checked_result<void> visit_unary_expr(unary_expr* expr) override;
        checked_result<void> visit_assignment_expr(assignment_expr* expr) override;
        checked_result<void> visit_call_expr(call_expr* expr) override;
        checked_result<void> visit_member_expr(member_expr* expr) override;
        checked_result<void> visit_lambda_expr(lambda_expr* expr) override;
        checked_result<void> visit_new_expr(new_expr* expr) override;
        checked_result<void> visit_ternary_expr(ternary_expr* expr) override;
        checked_result<void> visit_array_literal_expr(array_literal_expr* expr) override;
        checked_result<void> visit_map_literal_expr(map_literal_expr* expr) override;
        checked_result<void> visit_this_expr(this_expr* expr) override;
        checked_result<void> visit_super_expr(super_expr* expr) override;
        checked_result<void> visit_throw_expr(throw_expr* expr) override;

        // statement visitors
        checked_result<void> visit_expression_stmt(expression_stmt* stmt) override;
        checked_result<void> visit_block_stmt(block_stmt* stmt) override;
        checked_result<void> visit_if_stmt(if_stmt* stmt) override;
        checked_result<void> visit_while_stmt(while_stmt* stmt) override;
        checked_result<void> visit_for_stmt(for_stmt* stmt) override;
        checked_result<void> visit_range_for_stmt(range_for_stmt* stmt) override;
        checked_result<void> visit_return_stmt(return_stmt* stmt) override;
        checked_result<void> visit_break_stmt(break_stmt* stmt) override;
        checked_result<void> visit_continue_stmt(continue_stmt* stmt) override;
        checked_result<void> visit_try_stmt(try_stmt* stmt) override;
        checked_result<void> visit_switch_stmt(switch_stmt* stmt) override;
        checked_result<void> visit_case_stmt(case_stmt* stmt) override;
        checked_result<void> visit_default_stmt(default_stmt* stmt) override;
        checked_result<void> visit_fallthrough_stmt(fallthrough_stmt* stmt) override;

        // declaration visitors
        checked_result<void> visit_variable_decl(variable_decl* decl) override;
        checked_result<void> visit_function_decl(function_decl* decl) override;
        checked_result<void> visit_class_decl(class_decl* decl) override;
        checked_result<void> visit_namespace_decl(namespace_decl* decl) override;
        checked_result<void> visit_expression_decl(expression_decl* decl) override;
        checked_result<void> visit_include_decl(include_decl* decl) override;
        checked_result<void> visit_import_decl(import_decl* decl) override;
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value) { has_custom_numeric_ops_ = value; }
        bool has_custom_numeric_ops() const { return has_custom_numeric_ops_; }
        
        // Accessors for script class support
        std::shared_ptr<environment> get_current_environment() const { return environment_; }

        // Get the global (root) environment by walking up the parent chain
        std::shared_ptr<environment> get_global_environment() const {
            auto global_env = environment_;
            while (global_env && global_env->get_parent()) {
                global_env = global_env->get_parent();
            }
            return global_env ? global_env : environment_;
        }

        string_symbolizer* get_string_symbolizer() const { return string_symbolizer_; }
        
        // Access to current argument metadata for reference parameter support
        const std::vector<std::pair<uint64_t, std::shared_ptr<environment>>>& get_current_arg_metadata() const { 
            return current_arg_metadata_; 
        }
        
        // Execute a method AST with a given environment
        script_value execute_method_ast(std::shared_ptr<function_decl> ast,
                                      std::shared_ptr<environment> method_env,
                                      const std::vector<script_value>& args);

        // Evaluate field initializers for a script class instance at construction time
        // This should be called BEFORE the constructor body executes
        // If skip_parent_recursion is true, only evaluate THIS class's field initializers (parents already handled)
        void evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                        std::shared_ptr<script_class_definition> class_def,
                                        std::shared_ptr<environment> init_env,
                                        bool skip_parent_recursion = false);

        // Parameter binding semantics
        enum class parameter_semantics {
            value,       // Clone the argument (default C++ behavior)
            reference    // Share the argument (auto&, shared_ptr<T>)
        };

        // Determine whether a parameter should use value or reference semantics
        parameter_semantics get_parameter_semantics(
            const parameter& param,
            const script_value& arg) const;

        // Bind a parameter according to C++ semantics
        // Returns cloned value for value semantics, or shared reference for reference semantics
        script_value bind_parameter(
            const parameter& param,
            const script_value& arg) const;

        // Helper to create a bound method - binds 'this' as the first argument
        static script_value create_bound_method(const script_value& this_obj, const script_value& method);
        
    private:
        // Binary operator dispatch table for performance
        using binary_op_handler = script_value(interpreter::*)(const script_value&, const script_value&);
        std::unordered_map<token_type, binary_op_handler> binary_dispatch_table_;
        
        // Initialize dispatch table
        void init_dispatch_table();
        
        // Optimized binary operation handlers
        script_value handle_add(const script_value& left, const script_value& right);
        script_value handle_subtract(const script_value& left, const script_value& right);
        script_value handle_multiply(const script_value& left, const script_value& right);
        script_value handle_divide(const script_value& left, const script_value& right);
        script_value handle_modulo(const script_value& left, const script_value& right);
        script_value handle_less(const script_value& left, const script_value& right);
        script_value handle_less_equal(const script_value& left, const script_value& right);
        script_value handle_greater(const script_value& left, const script_value& right);
        script_value handle_greater_equal(const script_value& left, const script_value& right);
        script_value handle_equal(const script_value& left, const script_value& right);
        script_value handle_not_equal(const script_value& left, const script_value& right);
        script_value handle_spaceship(const script_value& left, const script_value& right);
        script_value handle_bitwise_and(const script_value& left, const script_value& right);
        script_value handle_bitwise_or(const script_value& left, const script_value& right);
        script_value handle_bitwise_xor(const script_value& left, const script_value& right);
        script_value handle_left_shift(const script_value& left, const script_value& right);
        script_value handle_right_shift(const script_value& left, const script_value& right);
        
        // Performance optimization: Skip custom operator checks for numeric types
        bool has_custom_numeric_ops_ = false;
        
        // Helper to check if we should use fast path
        bool can_use_fast_path(token_type op) const {
            return !has_custom_numeric_ops_ && is_numeric_op(op);
        }
        
        bool is_numeric_op(token_type op) const {
            switch (op) {
                case token_type::plus:
                case token_type::minus:
                case token_type::star:
                case token_type::slash:
                case token_type::percent:
                case token_type::less:
                case token_type::less_equal:
                case token_type::greater:
                case token_type::greater_equal:
                case token_type::equal_equal:
                case token_type::bang_equal:
                case token_type::spaceship:
                    return true;
                default:
                    return false;
            }
        }
        // Script-defined function storage
        struct script_defined_function {
            std::string name;
            std::vector<parameter> parameters;
            type_info_ptr return_type;
            std::shared_ptr<block_stmt> body;
            std::shared_ptr<environment> closure_env;  // capture environment for closures
            
            script_defined_function(const std::string& n, std::vector<parameter> params,
                                type_info_ptr retType, std::shared_ptr<block_stmt> b, 
                                std::shared_ptr<environment> env = nullptr)
                : name(n), parameters(std::move(params)), return_type(retType), body(b), closure_env(env) {}
        };
        
        
        // script_string symbolizer for variable names
        std::unique_ptr<string_symbolizer> ownedSymbolizer_;  // Only used if we own it
        string_symbolizer* string_symbolizer_;  // Points to either owned or external
        
        // Current environment for variable storage
        std::shared_ptr<environment> environment_;

        // Current argument metadata for reference parameter passing
        std::vector<std::pair<uint64_t, std::shared_ptr<environment>>> current_arg_metadata_;
        
        // Optimized stack for expression evaluation results
        class script_valueStack {
        private:
            std::vector<script_value> values_;
            size_t top_ = 0;
            
        public:
            script_valueStack() { 
                values_.reserve(512);  // Increased capacity for complex expressions
            }
            
            void push(script_value&& v) {
                if (top_ >= values_.size()) {
                    values_.emplace_back(std::move(v));
                    top_++;
                } else {
                    values_[top_++] = std::move(v);
                }
            }
            
            void push(const script_value& v) {
                if (top_ >= values_.size()) {
                    values_.emplace_back(v);
                    top_++;
                } else {
                    values_[top_++] = v;
                }
            }
            
            script_value& top() { 
                if (top_ == 0) {
                    throw runtime_error("Internal error: empty value stack");
                }
                return values_[top_ - 1]; 
            }
            
            script_value pop() {
                if (top_ == 0) {
                    throw runtime_error("Internal error: empty value stack");
                }
                script_value result = std::move(values_[--top_]);
                // Note: moved-from value is in valid but unspecified state, which is fine
                // Don't create a new null value here as it would have invalid engine_ref
                return result;
            }

            // Discard top value without constructing it (optimization for ignored return values)
            void discard() {
                if (top_ == 0) {
                    throw runtime_error("Internal error: empty value stack");
                }
                --top_;  // Just decrement, don't move/copy the value
            }

            bool empty() const { return top_ == 0; }
            size_t size() const { return top_; }

            const script_value& peek_at(size_t index) const {
                if (index >= top_) {
                    throw runtime_error("Internal error: stack index out of range");
                }
                return values_[index];
            }

            void clear() {
                // Actually destroy the values to release references
                values_.clear();
                top_ = 0;
            }
        };
        
        script_valueStack valueStack_;
        
        // Return value storage for global scope returns
        std::optional<script_value> returnValue_;  // Use optional to avoid needing default constructor
        bool hasReturnValue_ = false;

        // Control flow flags (for break/continue without exception overhead)
        bool hasBreakRequest_ = false;
        bool hasContinueRequest_ = false;

        // Exception handling state
        std::optional<script_exception> current_exception_;
        bool is_unwinding_ = false;
        std::optional<script_value> active_exception_value_;  // The exception message as a script_value (optional)

        // Class parsing context - tracks unresolved identifiers in methods
        struct class_context {
            std::string class_name;
            std::set<uint64_t> unresolved_identifiers;  // Interned IDs of identifiers found in methods but not yet resolved
            bool in_method = false;
        };
        std::optional<class_context> current_class_context_;
        uint64_t current_catch_var_id_ = 0;  // Symbol ID of current catch variable (0 = none)

        // Switch statement control flow state
        bool in_switch_ = false;
        bool should_fallthrough_ = false;

        // Call depth tracking for recursion limit
        int current_call_depth_ = 0;

        // Function call optimization pools
        mutable std::vector<script_value> argument_pool_;  // Reusable argument vector
        std::vector<std::shared_ptr<environment>> environment_pool_;  // Pool of reusable environments
        size_t environment_pool_index_ = 0;  // Current pool position
        std::vector<std::shared_ptr<method_environment>> method_environment_pool_;  // Pool of reusable method environments
        size_t method_environment_pool_index_ = 0;  // Current pool position for method environments

        // Class lookup callback for finding C++ classes
        class_lookup_callback class_lookup_callback_;

        // Namespace registry (flat - no true nesting)
        struct namespace_data {
            std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> functions;  // function_name_id -> overloads
            std::unordered_map<uint64_t, script_value> variables;  // variable_name_id -> value
            std::unordered_map<uint64_t, std::shared_ptr<class_definition>> classes;  // class_name_id -> definition
        };
        std::unordered_map<uint64_t, std::shared_ptr<namespace_data>> namespaces_;  // namespace_id (interned name) -> data

        // Cached symbol IDs for object type names (initialized in constructor)
        uint64_t namespace_function_type_id_;
        uint64_t namespace_class_type_id_;
        uint64_t namespace_identifier_type_id_;
        uint64_t class_definition_type_id_;
        uint64_t weak_ptr_holder_type_id_;
        uint64_t shared_ptr_holder_type_id_;
        uint64_t weak_from_this_id_;
        uint64_t shared_from_this_id_;

        // Cached symbol IDs for operator overloading (initialized in constructor)
        uint64_t op_plus_id_;
        uint64_t op_minus_id_;
        uint64_t op_star_id_;
        uint64_t op_slash_id_;
        uint64_t op_percent_id_;
        uint64_t op_less_id_;
        uint64_t op_less_equal_id_;
        uint64_t op_greater_id_;
        uint64_t op_greater_equal_id_;
        uint64_t op_equal_equal_id_;
        uint64_t op_bang_equal_id_;
        uint64_t op_spaceship_id_;
        uint64_t op_ampersand_id_;
        uint64_t op_pipe_id_;
        uint64_t op_caret_id_;
        uint64_t op_left_shift_id_;
        uint64_t op_right_shift_id_;
        uint64_t subscript_op_id_;  // "[]"
        uint64_t assign_operator_id_;  // "=" for class assignment operator overload

		// Cached symbol IDs for common keywords (initialized in constructor)
		uint64_t this_id_;
		uint64_t super_id_;
		uint64_t getValue_id_;
		uint64_t cpp_object_field_id_;  // "_cpp_object"

        // Engine reference for script_value creation (weak reference to avoid circular dependency)
        std::weak_ptr<engine> engine_ref_;

        // Cached common values to avoid repeated allocations (initialized in set_engine_reference)
        std::optional<script_value> cached_null_;
        std::optional<script_value> cached_true_;
        std::optional<script_value> cached_false_;
        std::optional<script_value> cached_zero_int_;
        std::optional<script_value> cached_one_int_;
        std::optional<script_value> cached_zero_float_;
        std::optional<script_value> cached_one_float_;

        // Temporary storage for 'this' value during method/constructor execution
        script_value current_method_this_;

        // ============================================================
        // SWITCH-BASED DISPATCH (faster than virtual calls)
        // ============================================================

        // Evaluate an expression using switch-based dispatch
        // Returns checked_result<void>, result is pushed to valueStack_
        [[nodiscard]] checked_result<void> dispatch_expr(expression* expr);

        // Execute a statement using switch-based dispatch
        [[nodiscard]] checked_result<void> dispatch_stmt(statement* stmt);

        // Execute a declaration using switch-based dispatch
        [[nodiscard]] checked_result<void> dispatch_decl(declaration* decl);

        // Helper to get the last evaluated value (inlined for performance)
        inline script_value pop_value() {
            return valueStack_.pop();
        }

        inline void push_value(const script_value& value) {
            valueStack_.push(value);
        }

        inline void push_value(script_value&& value) {
            valueStack_.push(std::move(value));
        }
        
        // Binary operation helpers
        [[nodiscard]] checked_result<script_value> evaluate_arithmetic(const script_value& left, token_type op, const script_value& right);
        [[nodiscard]] checked_result<script_value> evaluate_comparison(const script_value& left, token_type op, const script_value& right);
        [[nodiscard]] checked_result<script_value> evaluate_logical(const script_value& left, token_type op, const script_value& right);
        [[nodiscard]] checked_result<script_value> evaluate_bitwise(const script_value& left, token_type op, const script_value& right);

        // Strong types: Type enforcement for assignment
        // Returns error if value cannot be assigned to a variable with target_type
        // - nullptr target = uninitialized (auto x;), locks to source type
        // - any_type target = dynamic (var x), allows anything
        // - locked target = enforce compatibility or convert
        checked_result<script_value> enforce_type_compatibility(
            script_value value,
            type_info_ptr target_type,
            const std::string& var_name = ""
        );

        // Type conversion helpers (inlined for performance)
        // Helper function for object to_bool() method lookup (defined in interpreter.cpp)
        bool object_to_bool_via_method(const script_value& value);
        // Helper function to convert value to string, checking for to_string() method on objects
        std::string value_to_string_with_method(const script_value& val);
        // Helper function for object equality via operator== method (defined in interpreter.cpp)
        // Returns: nullopt if no custom equality, true/false if method found and returned a result
        std::optional<bool> object_equality_via_method(const script_value& left, const script_value& right);

        // Generic helper for binary comparison operators (<, <=, >, >=) via custom methods
        // Returns: nullopt if no custom method, true/false if method found and returned valid result
        std::optional<bool> object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);

        // Generic helper for binary arithmetic operators (+, -, *, /, %) via custom methods
        // Returns: nullopt if no custom method, result value if method found
        std::optional<script_value> object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);

        // Helper to check if an expression is an lvalue (existing object that should be cloned)
        // Lvalues: identifiers, member access, subscript access
        // Non-lvalues (temporaries): function calls, constructors, literals, operators
        bool is_lvalue_expression(expression* e) const;

        inline bool is_truthy(const script_value& value) {
            // ULTRA-FAST: Use raw_storage_index() - single integer read, no pointer chasing
            // Avoids type_info_ null check and double-switch overhead
            // Bool case first since loop conditions almost always evaluate to bool
            switch (value.raw_storage_index()) {
                case 5: return value.unchecked_as_bool();    // bool - MOST COMMON for conditions
                case 0: return false;                         // null (monostate)
                case 1: return value.unchecked_as_int() != 0; // int
                case 2: return value.unchecked_as_float() != 0.0; // float
                case 3: return !value.unchecked_as_string().empty(); // string
                case 4: return true;                          // char - any char is truthy
                case 6: {
                    // array - empty is falsy (const_cast required, get_array_storage is non-const)
                    auto& arr = const_cast<script_value&>(value).get_array_storage();
                    return arr && !arr->empty();
                }
                case 7: {
                    // map - empty is falsy (const_cast required, get_map_storage is non-const)
                    auto& map = const_cast<script_value&>(value).get_map_storage();
                    return map && !map->empty();
                }
                case 8: return object_to_bool_via_method(value); // object (index 8) - check for to_bool() method
                default: return true;                         // functions, other complex types
            }
        }
        
        inline script_value to_numeric(const script_value& value) {
            if (value.is_int()) {
                return make_value(static_cast<script_float>(value.as_int()));
            } else if (value.is_float()) {
                return value;
            } else if (value.is_bool()) {
                return make_value(value.as_bool() ? 1.0 : 0.0);
            } else {
                throw runtime_error("Cannot convert to numeric value");
            }
        }
        
        // Function call helpers
        // Returns checked_result - exceptions only thrown at execute() boundary
        checked_result<script_value> call_function(const script_defined_function& function, const std::vector<script_value>& args);
        void validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args);
        script_value make_function(std::shared_ptr<script_defined_function> func);

        // Type conversion helpers (constructor-based conversions)
        // Attempts to convert a value to a target type using constructor-based conversion.
        // Returns the converted value on success, or the original value if no conversion is needed/possible.
        // Returns checked_result with error if conversion fails.
        [[nodiscard]] checked_result<script_value> try_convert_for_parameter(const script_value& arg, type_info_ptr target_type);

        // Check if a conversion is possible from source value to target type
        bool can_convert_to_type(const script_value& source, type_info_ptr target_type) const;
        
        
        // Callback for resolving subscript operators when built-in logic fails
        using subscript_resolver = std::function<checked_result<script_value>(const std::vector<script_value>&)>;
        subscript_resolver subscriptResolver_;
        
        // Static method registries for built-in types
        // Built-in method registries (using interned IDs for O(1) lookup)
        std::unordered_map<uint64_t, builtin_method> array_methods_;
        std::unordered_map<uint64_t, builtin_method> map_methods_;
        std::unordered_map<uint64_t, builtin_method> string_methods_;
        std::unordered_map<uint64_t, builtin_method> weak_ptr_methods_;
        std::unordered_map<uint64_t, builtin_method> shared_ptr_methods_;

        // Initialize built-in method registries with interned method names
        void init_builtin_methods();
        
        // Helper to access value's private storage (since interpreter is a friend)
        static std::shared_ptr<std::vector<script_value>>& get_array_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_array_storage();
        }
        
        static std::shared_ptr<std::map<script_value, script_value>>& get_map_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_map_storage();
        }
        
    public:
        // Set the subscript resolver callback
        void set_subscript_resolver(subscript_resolver resolver) {
            subscriptResolver_ = std::move(resolver);
        }

        // Environment pooling functions (public so class_definition can use them)
        std::shared_ptr<environment> get_pooled_environment(std::shared_ptr<environment> parent);
        std::shared_ptr<method_environment> get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj);
        void release_environment(std::shared_ptr<environment> env, bool clear_now = true);
        void reset_environment_pool();
    };
    
} // namespace jai

#endif // __JAISCRIPT_DETAIL_INTERPRETER_HPP__