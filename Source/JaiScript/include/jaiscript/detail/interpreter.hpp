#pragma once

#ifndef __JAISCRIPT_DETAIL_INTERPRETER_HPP__
#define __JAISCRIPT_DETAIL_INTERPRETER_HPP__

#include "ast.hpp"
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/types.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>
#include <system_error>

namespace jai {

    // Transparent hasher for string_view lookup in unordered_map<string, ...>
    struct string_hash {
        using is_transparent = void;  // Enable heterogeneous lookup

        [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }

        [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string>{}(s);
        }
    };

    // Transparent equality for string_view lookup
    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }
    };

    // script_string symbolizer for faster variable lookups (like FName in Unreal engine)
    // IMPORTANT: This is a LOCAL-ONLY optimization. script_string IDs are NOT deterministic
    // across sessions/machines. Always serialize actual string names, never IDs!
    // For network sync or save/load, use the original string keys, not symbolized IDs.
    class string_symbolizer {
    private:
        std::unordered_map<std::string, uint64_t, string_hash, string_equal> string_id_map_;
        std::vector<std::string> strings_;
        mutable uint64_t cached_this_id_ = UINT64_MAX;  // Cached ID for "this"

    public:
        string_symbolizer() {
            // Reserve capacity for typical script usage
            strings_.reserve(256);
            string_id_map_.reserve(256);
        }

        uint64_t intern(std::string_view str) {
            // C++20 heterogeneous lookup - avoid string construction on lookup
            // Only create string if we need to insert
            if (auto it = string_id_map_.find(str); it != string_id_map_.end()) {
                return it->second;
            }

            // Not found - need to insert
            uint64_t id = static_cast<uint64_t>(strings_.size());
            strings_.emplace_back(str);
            string_id_map_.emplace(strings_.back(), id);  // Use the stored string
            return id;
        }
        
        const std::string& get_string(uint64_t id) const {
            return strings_[id];
        }
        
        // Get cached "this" ID (lazily initialized)
        uint64_t get_this_id() const {
            if (cached_this_id_ == UINT64_MAX) {
                cached_this_id_ = const_cast<string_symbolizer*>(this)->intern("this");
            }
            return cached_this_id_;
        }
    };

    // environment for storing variables in a scope
    class environment {
    public:
        environment(string_symbolizer* symbolizer) : symbolizer_(symbolizer) {}
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer)
            : parent_(parent), symbolizer_(symbolizer) {}
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
        virtual script_value get(const std::string& name) const;
        virtual script_value get(uint64_t id) const;  // Optimized direct lookup
        
        // Get a reference to variable value (for avoiding copies)
        virtual const script_value& get_ref(const std::string& name) const;
        virtual const script_value& get_ref(uint64_t id) const;
        virtual script_value& get_ref(const std::string& name);
        virtual script_value& get_ref(uint64_t id);
        
        // Assign to an existing variable (searches parent scopes)
        virtual void assign(const std::string& name, const script_value& value);
        virtual void assign(const std::string& name, script_value&& value);
        virtual void assign(uint64_t id, const script_value& value);
        virtual void assign(uint64_t id, script_value&& value);
        
        // Check if variable exists in current or parent scopes
        bool contains(const std::string& name) const;
        bool contains(uint64_t id) const;
        
        // Get a pointer to a value (for references)
        script_value* get_value_ptr(uint64_t id);
        
        // Get all variables in this scope (not including parent scopes)  
        // Returns a new map with string keys for compatibility
        std::unordered_map<std::string, script_value> get_local_variables() const;
        
        // Get all variables including parent scopes
        std::unordered_map<std::string, script_value> get_all_variables() const;
        
        // Reset environment for reuse (optimization helper)
        void reset(std::shared_ptr<environment> new_parent);
        
        // Get parent environment (needed for method_environment handling)
        std::shared_ptr<environment> get_parent() const { return parent_; }
        
    private:
        std::unordered_map<uint64_t, script_value> values_;  // Use symbolized string IDs
        std::shared_ptr<environment> parent_;
        string_symbolizer* symbolizer_;
        
        // Internal get method with recursion depth tracking
        script_value get(uint64_t id, int depth) const;
        const script_value& get_ref(uint64_t id, int depth) const;
        
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
        
        // Override get to check 'this' object fields as fallback
        script_value get(const std::string& name) const override;
        script_value get(uint64_t id) const override;
        
        // Override get_ref to check 'this' object fields as fallback
        const script_value& get_ref(const std::string& name) const override;
        const script_value& get_ref(uint64_t id) const override;
        script_value& get_ref(const std::string& name) override;
        script_value& get_ref(uint64_t id) override;
        
        // Override assign to update 'this' object fields when appropriate
        void assign(const std::string& name, const script_value& value) override;
        void assign(uint64_t id, const script_value& value) override;

        // Override contains to include 'this'
        bool contains(const std::string& name) const;
        bool contains(uint64_t id) const;

        // Get the 'this' object for super access
        const script_value& get_this_object() const { return this_object_; }
        
        // Reset method for pooling
        void reset(std::shared_ptr<environment> parent, script_value this_obj) {
            // Clear local values
            values_.clear();
            parent_ = parent;
            this_object_ = std::move(this_obj);
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
        
        // Override get to check static fields as fallback
        script_value get(const std::string& name) const override;
        script_value get(uint64_t id) const override;
        
        // Override get_ref to check static fields as fallback
        const script_value& get_ref(const std::string& name) const override;
        const script_value& get_ref(uint64_t id) const override;
        script_value& get_ref(const std::string& name) override;
        script_value& get_ref(uint64_t id) override;
        
        // Override assign to update static fields when appropriate
        void assign(const std::string& name, const script_value& value) override;
        void assign(const std::string& name, script_value&& value) override;
        void assign(uint64_t id, const script_value& value) override;
        void assign(uint64_t id, script_value&& value) override;

    private:
        std::shared_ptr<class_definition> class_def_;
        mutable script_value bound_method_storage_;  // Storage for bound methods
    };
    
    // The interpreter implements the visitor pattern to execute the AST
    class interpreter : public ast_visitor, public std::enable_shared_from_this<interpreter> {
    public:
        using class_lookup_callback = std::function<std::shared_ptr<class_definition>(const std::string&)>;
        
        // Method type for built-in type methods
        using builtin_method = std::function<script_value(interpreter*, script_value&, const std::vector<script_value>&)>;
        
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
        }
        
        // Helper to create script_value with engine context
        script_value make_value(script_int i) const {
            if (!engine_ref_.expired()) {
                return script_value(i, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
        }
        
        script_value make_value(script_float f) const {
            if (!engine_ref_.expired()) {
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
        
        script_value make_value(script_bool b) const {
            if (!engine_ref_.expired()) {
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
                return script_value(std::monostate{}, engine_ref_);
            }
            throw runtime_error("Engine reference expired - cannot create script_value");
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
            return returnValue_.value_or(script_value::make_null(engine_ref_)); 
        }
        
        // Variable access methods
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        std::unordered_map<std::string, script_value> get_all_variables() const;
        
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
        checked_result<void> visit_expression_decl(expression_decl* decl) override;
        checked_result<void> visit_include_decl(include_decl* decl) override;
        checked_result<void> visit_import_decl(import_decl* decl) override;
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value) { has_custom_numeric_ops_ = value; }
        bool has_custom_numeric_ops() const { return has_custom_numeric_ops_; }
        
        // Accessors for script class support
        std::shared_ptr<environment> get_current_environment() const { return environment_; }
        string_symbolizer* get_string_symbolizer() const { return string_symbolizer_; }
        
        // Access to current argument metadata for reference parameter support
        const std::vector<std::pair<uint64_t, std::shared_ptr<environment>>>& get_current_arg_metadata() const { 
            return current_arg_metadata_; 
        }
        
        // Execute a method AST with a given environment
        script_value execute_method_ast(std::shared_ptr<function_decl> ast, 
                                      std::shared_ptr<environment> method_env,
                                      const std::vector<script_value>& args);
                                      
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
                // Clear the moved-from value to release references
                values_[top_] = script_value::make_null(std::weak_ptr<engine>{});
                return result;
            }
            
            bool empty() const { return top_ == 0; }
            size_t size() const { return top_; }

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
        
        // Exception handling state
        std::optional<script_exception> current_exception_;
        bool is_unwinding_ = false;
        std::optional<script_value> active_exception_value_;  // The exception message as a script_value (optional)

        // Class parsing context - tracks unresolved identifiers in methods
        struct class_context {
            std::string class_name;
            std::set<std::string> unresolved_identifiers;  // Identifiers found in methods but not yet resolved
            bool in_method = false;
        };
        std::optional<class_context> current_class_context_;
        std::string current_catch_var_;  // Name of the current catch variable (if any)
        
        // Switch statement control flow state
        bool in_switch_ = false;
        bool should_fallthrough_ = false;
        
        
        // Function call optimization pools
        mutable std::vector<script_value> argument_pool_;  // Reusable argument vector
        std::vector<std::shared_ptr<environment>> environment_pool_;  // Pool of reusable environments
        size_t environment_pool_index_ = 0;  // Current pool position
        std::vector<std::shared_ptr<method_environment>> method_environment_pool_;  // Pool of reusable method environments
        size_t method_environment_pool_index_ = 0;  // Current pool position for method environments
        
        // Class lookup callback for finding C++ classes
        class_lookup_callback class_lookup_callback_;
        
        // Engine reference for script_value creation (weak reference to avoid circular dependency)
        std::weak_ptr<engine> engine_ref_;
        
        // Temporary storage for 'this' value during method/constructor execution
        script_value current_method_this_;
        
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
        script_value evaluate_arithmetic(const script_value& left, token_type op, const script_value& right);
        script_value evaluate_comparison(const script_value& left, token_type op, const script_value& right);
        script_value evaluate_logical(const script_value& left, token_type op, const script_value& right);
        script_value evaluate_bitwise(const script_value& left, token_type op, const script_value& right);
        
        // Type conversion helpers (inlined for performance)
        inline bool is_truthy(const script_value& value) {
            if (value.is_null()) return false;
            if (value.is_bool()) return value.as_bool();
            if (value.is_int()) return value.as_int() != 0;
            if (value.is_float()) return value.as_float() != 0.0;
            if (value.is_string()) return !value.as_string().empty();
            return true;  // Other types are truthy
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
        script_value call_function(const script_defined_function& function, const std::vector<script_value>& args);
        void validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args);
        script_value make_function(std::shared_ptr<script_defined_function> func);
        
        // Function call optimization helpers
        std::shared_ptr<environment> get_pooled_environment(std::shared_ptr<environment> parent);
        std::shared_ptr<method_environment> get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj);
        void release_environment(std::shared_ptr<environment> env);
        void reset_environment_pool();
        
        // Callback for resolving subscript operators when built-in logic fails
        using subscript_resolver = std::function<script_value(const std::vector<script_value>&)>;
        subscript_resolver subscriptResolver_;
        
        // Static method registries for built-in types
        static const std::unordered_map<std::string, builtin_method> arrayMethods_;
        static const std::unordered_map<std::string, builtin_method> mapMethods_;
        static const std::unordered_map<std::string, builtin_method> weakPtrMethods_;
        static const std::unordered_map<std::string, builtin_method> sharedPtrMethods_;
        
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
    };
    
} // namespace jai

#endif // __JAISCRIPT_DETAIL_INTERPRETER_HPP__