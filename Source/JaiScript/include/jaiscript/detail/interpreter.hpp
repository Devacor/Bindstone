#pragma once

#include "ast.hpp"
#include "../core/value.hpp"
#include "../core/types.hpp"
#include <unordered_map>
#include <memory>
#include <vector>

namespace jai {

    // script_string symbolizer for faster variable lookups (like FName in Unreal engine)
    // IMPORTANT: This is a LOCAL-ONLY optimization. script_string IDs are NOT deterministic
    // across sessions/machines. Always serialize actual string names, never IDs!
    // For network sync or save/load, use the original string keys, not symbolized IDs.
    class string_symbolizer {
    private:
        std::unordered_map<std::string, uint64_t> string_id_map_;
        std::vector<std::string> strings_;
        
    public:
        string_symbolizer() {
            // Reserve capacity for typical script usage
            strings_.reserve(256);
            string_id_map_.reserve(256);
        }
        
        uint64_t intern(std::string_view str) {
            std::string key(str);
            if (auto it = string_id_map_.find(key); it != string_id_map_.end()) {
                return it->second;
            }
            uint64_t id = static_cast<uint64_t>(strings_.size());
            strings_.emplace_back(key);
            string_id_map_[key] = id;
            return id;
        }
        
        const std::string& get_string(uint64_t id) const {
            return strings_[id];
        }
    };

    // environment for storing variables in a scope
    class environment {
    public:
        environment(string_symbolizer* symbolizer) : symbolizer_(symbolizer) {}
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer) 
            : parent_(parent), symbolizer_(symbolizer) {}
        
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
        script_value get(const std::string& name) const;
        script_value get(uint64_t id) const;  // Optimized direct lookup
        
        // Get a reference to variable value (for avoiding copies)
        const script_value& get_ref(const std::string& name) const;
        const script_value& get_ref(uint64_t id) const;
        script_value& get_ref(const std::string& name);
        script_value& get_ref(uint64_t id);
        
        // Assign to an existing variable (searches parent scopes)
        void assign(const std::string& name, const script_value& value);
        void assign(const std::string& name, script_value&& value);
        void assign(uint64_t id, const script_value& value);
        void assign(uint64_t id, script_value&& value);
        
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
        
    private:
        std::unordered_map<uint64_t, script_value> values_;  // Use symbolized string IDs
        std::shared_ptr<environment> parent_;
        string_symbolizer* symbolizer_;
        
        // Internal get method with recursion depth tracking
        script_value get(uint64_t id, int depth) const;
        const script_value& get_ref(uint64_t id, int depth) const;
        
        friend class interpreter;
    };
    
    // The interpreter implements the visitor pattern to execute the AST
    class interpreter : public ast_visitor {
    public:
        using type_converter = std::function<script_value(const void*)>;
        using type_converter_map = std::unordered_map<std::string, type_converter>;
        
        // Method type for built-in type methods
        using builtin_method = std::function<script_value(interpreter*, const script_value&, const std::vector<script_value>&)>;
        
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
        
        // Set type converter map (owned by engine)
        void set_type_converters(const type_converter_map* converters) {
            type_converters_ = converters;
        }
        
        // Get type converter for a specific type
        type_converter get_type_converter(const std::string& type_id_name) const {
            if (!type_converters_) return nullptr;
            auto it = type_converters_->find(type_id_name);
            return it != type_converters_->end() ? it->second : nullptr;
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
            return returnValue_.as<T>();
        }
        
        // Execute a single expression and return its value
        script_value evaluate(expression_ptr expr);
        
        // Return value access (for global scope return statements)
        bool has_return_value() const { return hasReturnValue_; }
        script_value get_return_value() const { return returnValue_; }
        
        // Variable access methods
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        std::unordered_map<std::string, script_value> get_all_variables() const;
        
        // Exception handling methods
        bool is_unwinding() const { return is_unwinding_; }
        const script_exception& get_current_exception() const { return current_exception_.value(); }
        
        // expression visitors
        void visit_literal_expr(literal_expr* expr) override;
        void visit_identifier_expr(identifier_expr* expr) override;
        void visit_binary_expr(binary_expr* expr) override;
        void visit_unary_expr(unary_expr* expr) override;
        void visit_assignment_expr(assignment_expr* expr) override;
        void visit_call_expr(call_expr* expr) override;
        void visit_member_expr(member_expr* expr) override;
        void visit_lambda_expr(lambda_expr* expr) override;
        void visit_new_expr(new_expr* expr) override;
        void visit_ternary_expr(ternary_expr* expr) override;
        void visit_array_literal_expr(array_literal_expr* expr) override;
        void visit_map_literal_expr(map_literal_expr* expr) override;
        void visit_this_expr(this_expr* expr) override;
        void visit_super_expr(super_expr* expr) override;
        void visit_throw_expr(throw_expr* expr) override;
        
        // statement visitors
        void visit_expression_stmt(expression_stmt* stmt) override;
        void visit_block_stmt(block_stmt* stmt) override;
        void visit_if_stmt(if_stmt* stmt) override;
        void visit_while_stmt(while_stmt* stmt) override;
        void visit_for_stmt(for_stmt* stmt) override;
        void visit_range_for_stmt(range_for_stmt* stmt) override;
        void visit_return_stmt(return_stmt* stmt) override;
        void visit_break_stmt(break_stmt* stmt) override;
        void visit_continue_stmt(continue_stmt* stmt) override;
        void visit_try_stmt(try_stmt* stmt) override;
        
        // declaration visitors
        void visit_variable_decl(variable_decl* decl) override;
        void visit_function_decl(function_decl* decl) override;
        void visit_class_decl(class_decl* decl) override;
        void visit_expression_decl(expression_decl* decl) override;
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value) { has_custom_numeric_ops_ = value; }
        bool has_custom_numeric_ops() const { return has_custom_numeric_ops_; }
        
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
        string_symbolizer* stringSymbolizer_;  // Points to either owned or external
        
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
                    values_.resize(values_.size() == 0 ? 256 : values_.size() * 2);
                }
                values_[top_++] = std::move(v);
            }
            
            void push(const script_value& v) {
                if (top_ >= values_.size()) {
                    values_.resize(values_.size() == 0 ? 256 : values_.size() * 2);
                }
                values_[top_++] = v;
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
                return std::move(values_[--top_]);
            }
            
            bool empty() const { return top_ == 0; }
            size_t size() const { return top_; }
            
            void clear() { top_ = 0; }
        };
        
        script_valueStack valueStack_;
        
        // Return value storage for global scope returns
        script_value returnValue_;
        bool hasReturnValue_ = false;
        
        // Exception handling state
        std::optional<script_exception> current_exception_;
        bool is_unwinding_ = false;
        script_value active_exception_value_;  // The exception message as a script_value
        std::string current_catch_var_;  // Name of the current catch variable (if any)
        
        
        // Function call optimization pools
        mutable std::vector<script_value> argument_pool_;  // Reusable argument vector
        std::vector<std::shared_ptr<environment>> environment_pool_;  // Pool of reusable environments
        size_t environment_pool_index_ = 0;  // Current pool position
        
        // Type converter registry (non-owning pointer to engine's converters)
        const type_converter_map* type_converters_ = nullptr;
        
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
                return script_value(static_cast<script_float>(value.as_int()));
            } else if (value.is_float()) {
                return value;
            } else if (value.is_bool()) {
                return script_value(value.as_bool() ? 1.0 : 0.0);
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
        void reset_environment_pool();
        
        // Callback for resolving subscript operators when built-in logic fails
        using subscript_resolver = std::function<script_value(const std::vector<script_value>&)>;
        subscript_resolver subscriptResolver_;
        
        // Static method registries for built-in types
        static const std::unordered_map<std::string, builtin_method> arrayMethods_;
        static const std::unordered_map<std::string, builtin_method> mapMethods_;
        
        // Helper to access value's private storage (since interpreter is a friend)
        static std::shared_ptr<std::vector<script_value>>& get_array_storage(const script_value& value) {
            return const_cast<std::shared_ptr<std::vector<script_value>>&>(
                std::get<std::shared_ptr<std::vector<script_value>>>(
                    const_cast<script_value&>(value).storage_
                )
            );
        }
        
        static std::shared_ptr<std::map<script_value, script_value>>& get_map_storage(const script_value& value) {
            return const_cast<std::shared_ptr<std::map<script_value, script_value>>&>(
                std::get<std::shared_ptr<std::map<script_value, script_value>>>(
                    const_cast<script_value&>(value).storage_
                )
            );
        }
        
    public:
        // Set the subscript resolver callback
        void set_subscript_resolver(subscript_resolver resolver) {
            subscriptResolver_ = std::move(resolver);
        }
    };
    
} // namespace jai