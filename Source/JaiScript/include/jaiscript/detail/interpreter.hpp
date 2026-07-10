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

// One static recursion-limit message for BOTH backends and BOTH triggers (depth cap and
// native-stack exhaustion): which limit fires first differs per backend (the vm's in-loop
// frames never recurse natively), so byte-identical error text requires a single message.
// The depth is baked in by stringification - checked_result messages are static string_views
// and the old "{0}" form passed the depth as a SYMBOL id, printing an empty "()".
// (vm_backend.cpp repeats this definition; both are #ifndef-guarded - keep them in sync.)
#ifndef JAI_MAX_CALL_DEPTH_MESSAGE
#define JAI_STRINGIFY_IMPL(x) #x
#define JAI_STRINGIFY(x) JAI_STRINGIFY_IMPL(x)
#define JAI_MAX_CALL_DEPTH_MESSAGE \
	"Maximum recursion depth (" JAI_STRINGIFY(JAI_MAX_CALL_DEPTH) ") exceeded - possible infinite recursion"
#endif

// Portable force-inline (MSVC and clang-cl accept __forceinline; GCC/Clang need the attribute)
#ifndef JAI_FORCEINLINE
#if defined(_MSC_VER)
#define JAI_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define JAI_FORCEINLINE inline __attribute__((always_inline))
#else
#define JAI_FORCEINLINE inline
#endif
#endif

// NOTE: the compile-time integer-overflow policy (kCheckedOverflow and the
// jai::ints helpers) lives in <jaiscript/detail/integer_ops.hpp>, which the
// engine .cpp files include directly. It is intentionally kept out of this
// widely-included header so its <intrin.h> dependency does not bloat consumer
// (Bindstone) translation units.

#include "ast.hpp"
#include "string_symbolizer.hpp"
#include "environment.hpp"
#include "builtin_methods.hpp"
#include "execution_limits.hpp"
#include "operator_table.hpp"
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/coroutine.hpp>
#include <jaiscript/core/execution_backend.hpp>
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
#include <chrono>
#include <atomic>

namespace jai {

    // Forward declarations
    class script_class_definition;
    class class_definition;
    class class_instance;
    namespace debug { class controller; }

    // Forward declare interpreter for scoped_method_environment
    class interpreter;

    // RAII wrapper for method environments to ensure proper cleanup
    // Automatically defines 'this' and releases the environment with clear=true on destruction
    class scoped_method_environment {
    public:
        scoped_method_environment(
            interpreter* interp,
            std::shared_ptr<environment> parent,
            const script_value& this_obj,
            class_definition* access_ctx = nullptr);

        ~scoped_method_environment();

        // Get the managed environment
        std::shared_ptr<environment> get() const { return env_; }

        // Implicit conversion to environment for convenience
        operator std::shared_ptr<environment>() const { return env_; }

        // Prevent copying
        scoped_method_environment(const scoped_method_environment&) = delete;
        scoped_method_environment& operator=(const scoped_method_environment&) = delete;

    private:
        interpreter* interp_;
        std::shared_ptr<environment> env_;
    };

    using stack_trace_entry = stack_frame;

    // Interpreter-shaped coroutine continuation state, stored opaquely on the handle.
    struct interpreter_coroutine_state : coroutine_backend_state {
        // A breadcrumb recording where we were in the AST when yield fired.
        // Built bottom-up during yield unwind, consumed top-down (LIFO) during resume.
        struct continuation_point {
            ast_node* node;   // which block/loop/if we were inside
            size_t index;     // statement index (blocks), iteration (range-for), branch (if: 0=then,1=else)
            std::shared_ptr<environment> saved_env;  // environment to restore on resume (may be null)
            // range-for only: the evaluated container (array/map/inner coroutine handle),
            // so resume continues the SAME iteration instead of re-evaluating the
            // container expression (which would re-mint a fresh inner coroutine)
            std::optional<script_value> saved_container;
        };

        std::shared_ptr<environment> saved_environment;
        std::vector<call_frame> saved_call_stack;
        std::optional<script_value> saved_return_value;
        bool saved_has_return = false;

        void push_continuation(ast_node* node, size_t index,
                               std::shared_ptr<environment> env = nullptr,
                               std::optional<script_value> container = std::nullopt) {
            continuations_.push_back({node, index, std::move(env), std::move(container)});
        }
        continuation_point* peek_continuation(ast_node* node) {
            if (!continuations_.empty() && continuations_.back().node == node) {
                return &continuations_.back();
            }
            return nullptr;
        }
        void pop_continuation() {
            if (!continuations_.empty()) {
                continuations_.pop_back();
            }
        }
        bool has_continuations() const { return !continuations_.empty(); }

    private:
        std::vector<continuation_point> continuations_;
    };

    // The interpreter implements the visitor pattern to execute the AST
    class interpreter : public ast_visitor, public std::enable_shared_from_this<interpreter> {
    public:
        using class_lookup_callback = std::function<std::shared_ptr<class_definition>(const std::string&)>;

        // Method type for built-in type methods (shared registries, see detail/builtin_methods.hpp)
        using builtin_method = ::jai::builtin_method;

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

        // ===== Execution budget (engine::execution_budget) =====
        // A top-level execute()/resume() that runs past the armed deadline raises a
        // catchable runtime error. Ticked at loop back-edges and call entry; the clock
        // is only sampled every 1024 ticks so the hot path pays one increment+branch.
        void execution_budget(std::chrono::nanoseconds budget) {
            execution_budget_ = budget;
            if (budget.count() <= 0) { budget_active_ = false; }
        }
        std::chrono::nanoseconds execution_budget() const { return execution_budget_; }

        void arm_execution_deadline() {
            budget_tick_ = 0;
            budget_active_ = execution_budget_.count() > 0;
            if (budget_active_) {
                execution_deadline_ = std::chrono::steady_clock::now() + execution_budget_;
            }
        }


        // Scope management for local variables
        void push_scope();
        void pop_scope();
        
        // Define a variable in the current scope
        void define_variable(const std::string& name, const script_value& value);
        
        // Set class lookup callback (for finding C++ classes)
        void set_class_lookup_callback(class_lookup_callback callback) {
            class_lookup_callback_ = std::move(callback);
        }
        
        // Set engine pointer for script_value creation
        void set_engine_reference(engine* eng) {
            engine_ = eng;

            // Initialize cached common values to avoid repeated allocations
            cached_null_ = script_value(std::monostate{}, engine_);
            cached_true_ = script_value(true, engine_);
            cached_false_ = script_value(false, engine_);
            cached_zero_int_ = script_value(static_cast<script_int>(0), engine_);
            cached_one_int_ = script_value(static_cast<script_int>(1), engine_);
            cached_zero_float_ = script_value(0.0, engine_);
            cached_one_float_ = script_value(1.0, engine_);

            if (cached_zero_int_.has_value()) cached_type_info_int_ = cached_zero_int_->get_type_info();
            if (cached_zero_float_.has_value()) cached_type_info_float_ = cached_zero_float_->get_type_info();
            if (cached_true_.has_value()) cached_type_info_bool_ = cached_true_->get_type_info();
        }

        // Wire (or clear) the step-debugger. The statement hook itself tests only the
        // plain cached gate debug_hook_, refreshed from this atomic at the debug sync
        // points (sync_debug_hook): execute entry and the 1024-tick budget sample.
        void set_debug_controller(debug::controller* c) {
            debugger_.store(c, std::memory_order_release);
        }

        // Get engine pointer (for internal use in lambdas)
        engine* get_engine() const {
            return engine_;
        }

        // Helper to create script_value with engine context
        script_value make_value(script_int i) const {
            if (engine_) {
                // Return cached common values to avoid allocations
                if (i == 0 && cached_zero_int_.has_value()) return *cached_zero_int_;
                if (i == 1 && cached_one_int_.has_value()) return *cached_one_int_;
                return script_value(i, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        script_value make_value(script_float f) const {
            if (engine_) {
                // Return cached common values to avoid allocations. IEEE -0.0 == 0.0, so the
                // zero cache must exclude the negative sign or -0.0 collapses to +0.0 (vm parity)
                if (f == 0.0 && !std::signbit(f) && cached_zero_float_.has_value()) return *cached_zero_float_;
                if (f == 1.0 && cached_one_float_.has_value()) return *cached_one_float_;
                return script_value(f, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        script_value make_value(const script_string& s) const {
            if (engine_) {
                return script_value(s, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        // Move overload for string temporaries (avoids copy)
        script_value make_value(script_string&& s) const {
            if (engine_) {
                return script_value(std::move(s), engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        script_value make_value(script_bool b) const {
            if (engine_) {
                // Return cached common values to avoid allocations
                if (b && cached_true_.has_value()) return *cached_true_;
                if (!b && cached_false_.has_value()) return *cached_false_;
                return script_value(b, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        script_value make_value(script_char c) const {
            if (engine_) {
                return script_value(c, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        script_value make_value() const {
            if (engine_) {
                // Return cached null value
                if (cached_null_.has_value()) return *cached_null_;
                return script_value(std::monostate{}, engine_);
            }
            throw runtime_error("Engine pointer is null - cannot create script_value");
        }

        // AST literals carry no engine/type_info; rebuild a real value before any slow-path
        // consumer sees them (operator-method args get cloned during param bind).
        script_value hydrate_literal(const script_value& lit) const {
            const auto& storage = lit.get_storage();
            switch (storage.index()) {
                case 1: return make_value(std::get<script_int>(storage));
                case 2: return make_value(std::get<script_float>(storage));
                case 3: return make_value(*std::get<strong_ptr<script_string>>(storage));
                case 4: return make_value(std::get<script_char>(storage));
                case 5: return make_value(std::get<script_bool>(storage));
                case 0: return make_value();
                default: { script_value v = lit; v.set_engine(engine_); return v; }
            }
        }

        // Helper to create namespace-related objects with both type_name and type_id for fast comparison
        template<typename T>
        script_value make_namespace_object(uint64_t type_id, const char* type_name, std::shared_ptr<T> data) const {
            return script_value::make_object(type_name, type_id, std::static_pointer_cast<void>(data), engine_);
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

        // Host-boundary discriminator (same rule as resume_coroutine's host-level
        // detection): false only when no execute is in flight (counted - nested
        // executes decrement without clearing the outer run) and no frames are live
        bool is_executing() const { return execute_depth_ != 0 || current_call_depth_ != 0; }

        const std::vector<stack_trace_entry>& last_stack_trace() const { return captured_trace_; }
        std::string format_stack_trace() const;

        // Resume a suspended (or start a created) coroutine on this interpreter.
        // Reached via execution_backend::resume_coroutine; the handle's opaque
        // backend_state holds the interpreter_coroutine_state.
        checked_result<script_value> resume_coroutine(coroutine_handle& handle);

        // Execute a script-defined callable payload (reached via execution_backend::execute_callable)
        checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args);

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
        checked_result<void> visit_yield_expr(yield_expr* expr);

        // statement visitors
        checked_result<void> visit_expression_stmt(expression_stmt* stmt) override;
        checked_result<void> visit_block_stmt(block_stmt* stmt) override;
        checked_result<void> visit_if_stmt(if_stmt* stmt) override;
        checked_result<void> visit_while_stmt(while_stmt* stmt) override;
        checked_result<void> visit_for_stmt(for_stmt* stmt) override;
        checked_result<void> visit_range_for_stmt(range_for_stmt* stmt) override;
        checked_result<void> visit_parallel_for_stmt(parallel_for_stmt* stmt) override;
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
        checked_result<void> visit_include_expr(include_expr* expr);
        checked_result<void> include_into_value_stack(expression* path_expr_node, const std::string& literal_path);
        checked_result<void> visit_import_decl(import_decl* decl) override;
        checked_result<void> visit_enum_decl(enum_decl* decl) override;
        checked_result<void> visit_destructuring_decl(destructuring_decl* decl) override;
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value) { has_custom_numeric_ops_ = value; }
        bool has_custom_numeric_ops() const { return has_custom_numeric_ops_; }
        void set_has_custom_binary_ops(bool value) { has_custom_binary_ops_ = value; }
        void set_operator_table(const detail::engine_operator_table* table) { operator_table_ = table; }
        
        // Accessors for script class support
        std::shared_ptr<environment> get_current_environment() const { return environment_; }

        // Debugger: named slot-locals (params + reached body decls) of the current call frame,
        // reconstructed from the frame's function AST. Empty at global scope / for foreign frames.
        std::vector<std::pair<std::string, script_value>> get_current_frame_locals() const;

        // Get the global (root) environment from the engine directly
        // Previously walked up the parent chain, but that can diverge when closures/methods
        // capture stale environment references from different execute() calls
        std::shared_ptr<environment> get_global_environment() const;

        string_symbolizer* get_string_symbolizer() const { return string_symbolizer_; }
        
        // External (C++) invocations of script functions must not see the pending
        // call-site context of whatever script call is in flight - reference params
        // would bind its variables. A one-pointer shelf replaces the old metadata stack.
        void push_external_call_scope() {
            external_ctx_stack_.push_back(pending_call_ctx_);
            pending_call_ctx_ = nullptr;
        }
        void pop_external_call_scope() {
            if (!external_ctx_stack_.empty()) {
                pending_call_ctx_ = external_ctx_stack_.back();
                external_ctx_stack_.pop_back();
            }
        }

        // Get current environment as shared_ptr (needed for make_reference calls)
        std::shared_ptr<environment> get_environment_shared() const { return environment_; }
        
        // Execute a method AST with a given environment
        checked_result<script_value> execute_method_ast(std::shared_ptr<function_decl> ast,
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
        // Full script-class construction (ctor overload selection, field initializers,
        // super()/this() chains). Bodies were the visit_class_decl ctor thunks.
        checked_result<script_value> construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                        std::shared_ptr<environment> definition_env,
                                                        const std::vector<script_value>& args);
        checked_result<script_value> construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                const std::vector<script_value>& args);

        // Binary operator dispatch: flat array indexed by the shared op_slot enum
        // (detail/operator_table.hpp) - direct index, no hashing on the generic path.
        // Returns checked_result for zero-allocation error handling
        using binary_op_handler = checked_result<script_value>(interpreter::*)(const script_value&, const script_value&);
        std::array<binary_op_handler, static_cast<size_t>(detail::op_slot::count)> binary_handlers_{};

        // Initialize dispatch table
        void init_dispatch_table();

        // Optimized binary operation handlers - return checked_result for error propagation
        checked_result<script_value> handle_add(const script_value& left, const script_value& right);
        checked_result<script_value> handle_subtract(const script_value& left, const script_value& right);
        checked_result<script_value> handle_multiply(const script_value& left, const script_value& right);
        checked_result<script_value> handle_divide(const script_value& left, const script_value& right);
        checked_result<script_value> handle_modulo(const script_value& left, const script_value& right);
        checked_result<script_value> handle_less(const script_value& left, const script_value& right);
        checked_result<script_value> handle_less_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_greater(const script_value& left, const script_value& right);
        checked_result<script_value> handle_greater_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_not_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_spaceship(const script_value& left, const script_value& right);
        checked_result<script_value> handle_bitwise_and(const script_value& left, const script_value& right);
        checked_result<script_value> handle_bitwise_or(const script_value& left, const script_value& right);
        checked_result<script_value> handle_bitwise_xor(const script_value& left, const script_value& right);
        checked_result<script_value> handle_left_shift(const script_value& left, const script_value& right);
        checked_result<script_value> handle_right_shift(const script_value& left, const script_value& right);
        
        // Performance optimization: Skip custom operator checks for numeric types
        bool has_custom_numeric_ops_ = false;
        // ANY binary-operator / "[]" override: transient subscript reads keep minting
        bool has_custom_binary_ops_ = false;
        const detail::engine_operator_table* operator_table_ = nullptr;   // engine-owned flat dispatch
        
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
        // Script-defined function storage (hoisted to namespace scope in environment.hpp)
        using script_defined_function = jai::script_defined_function;


        // script_string symbolizer for variable names
        std::unique_ptr<string_symbolizer> ownedSymbolizer_;  // Only used if we own it
        string_symbolizer* string_symbolizer_;  // Points to either owned or external

        // Epoch sink handed to every environment this interpreter constructs. Equal to
        // string_symbolizer_ everywhere except a parallel worker, whose private instance
        // keeps worker env churn from bumping the shared engine env-epoch.
        string_symbolizer* env_symbolizer_ = nullptr;
        
        // Current environment for variable storage
        std::shared_ptr<environment> environment_;

        // Pending call-site context (set by visit_call around the invoke, consumed by
        // the next call_function entered): ref params bind against the caller's
        // variables through it. Save/restored as a single pointer by nested calls.
        const detail::call_site_context* pending_call_ctx_ = nullptr;
        std::vector<const detail::call_site_context*> external_ctx_stack_;
        
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

            // Indexed peek below the top (0 = top): in-place reads for multi-arg
            // consumers (math:: intrinsics) - no pops, no moves
            script_value& peek(size_t from_top) {
                if (from_top >= top_) {
                    throw runtime_error("Internal error: value stack peek past bottom");
                }
                return values_[top_ - 1 - from_top];
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

        // Execute-nesting depth (a COUNTER, not a flag - Dev ruling: a nested C++
        // execute() on the same engine must not clear the outer run's in-flight
        // state on exit). Non-zero while any execute() runs a program - a reentrant
        // execute (host callback mid-script, e.g. hot reload) isolates itself at
        // top level and must not reset the environment pool the outer run still uses
        int execute_depth_ = 0;

        // Coroutine support
        coroutine_handle* active_coroutine_ = nullptr;
        bool hasYieldRequest_ = false;

        // Exception handling state
        std::optional<script_exception> current_exception_;
        bool is_unwinding_ = false;
        std::optional<script_value> active_exception_value_;  // The exception message as a script_value (optional)

        std::vector<stack_trace_entry> captured_trace_;
        bool trace_captured_ = false;
        const ast_node* top_level_node_ = nullptr;
        void capture_stack_trace();

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

        // True only while evaluating the outermost subscript that is the TARGET of
        // an assignment (e.g. the `m[k]` in `m[k] = v`). Map subscript needs to
        // auto-insert a slot in that case, but a plain READ (`x = m[k]`) must NOT
        // grow the map. visit_binary captures and clears this immediately so
        // nested subscripts are treated as reads.
        bool lvalue_target_context_ = false;

        // Call depth tracking for recursion limit
        int current_call_depth_ = 0;

        // Execution budget state (see execution_budget()/arm_execution_deadline above)
        std::chrono::nanoseconds execution_budget_{std::chrono::seconds(1)};
        std::chrono::steady_clock::time_point execution_deadline_{};
        uint32_t budget_tick_ = 0;
        bool budget_active_ = false;

        // Execution-limit state (terminal-error latch). Engine-less fallback storage;
        // prepare_for_execution repoints limits_ at the engine's per-engine instance so
        // reentrant executes share it and terminal errors cross the reentrant boundary.
        detail::execution_limits local_limits_;
        detail::execution_limits* limits_ = &local_limits_;

        // ============================================================
        // CALL FRAME OPTIMIZATION
        // ============================================================
        // Stack-based call frames for fast function parameter access.
        // Parameters are stored in a simple vector for O(n) lookup where n is small (typically < 8).
        // This avoids hash map overhead for the most frequently accessed variables.
        // Closure/global variables still use environment_ for lookup.
        // NOTE: call_frame struct is defined at namespace scope in environment.hpp
        // so that coroutine_handle can reference it without circular includes.

        /// Call stack for function execution
        std::vector<call_frame> call_stack_;

        // Function call optimization pools
        mutable std::vector<script_value> argument_pool_;  // Reusable argument vector
        std::vector<std::shared_ptr<environment>> environment_pool_;  // Pool of reusable environments (all kinds)
        size_t environment_pool_index_ = 0;  // Current pool position

        // Class lookup callback for finding C++ classes
        class_lookup_callback class_lookup_callback_;

        // Cached symbol IDs for object type names (initialized in constructor)
        uint64_t namespace_function_type_id_;
        uint64_t namespace_class_type_id_;
        uint64_t namespace_identifier_type_id_;
        uint64_t class_definition_type_id_;
        uint64_t weak_ptr_holder_type_id_;
        uint64_t shared_ptr_holder_type_id_;
        uint64_t weak_from_this_id_;
        uint64_t shared_from_this_id_;
        uint64_t coroutine_handle_type_id_;
        uint64_t resume_id_;
        uint64_t done_id_;
        uint64_t same_as_id_;

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

        // When false, assignment expressions skip the deep clone of the result
        // (the result would be immediately discarded by visit_expression_stmt)
        bool expression_result_needed_ = true;

        // Engine pointer for script_value creation (raw pointer - no atomic ops on copy)
        // Interpreter lifetime is managed by engine, so engine will always outlive interpreter
        engine* engine_ = nullptr;

        // Step-debugger controller (engine-owned). Atomic because the transport thread
        // may attach/detach while this thread is running — but it is NOT read per
        // statement: sync_debug_hook (below) mirrors it into the plain gate at the debug
        // sync points only (Dev ruling: no atomics on the statement path).
        std::atomic<debug::controller*> debugger_{nullptr};

        // Script-thread cached statement-hook gate: non-null only while a debug session
        // is enabled. dispatch_stmt/dispatch_decl test this one plain pointer per
        // statement; when no debugger was ever constructed that test is the hook's
        // entire cost. Written ONLY by sync_debug_hook on this thread.
        debug::controller* debug_hook_ = nullptr;

        // Debug sync point (out of line: needs the controller type). Refreshes
        // debug_hook_ + the controller's hot cache, and hands a session-end a fresh
        // budget deadline (the armed one is long past after a pause). Called at execute
        // entry (prepare_for_execution) and every 1024 budget ticks.
        void sync_debug_hook();

        // Cached common values to avoid repeated allocations (initialized in set_engine_reference)
        std::optional<script_value> cached_null_;
        std::optional<script_value> cached_true_;
        std::optional<script_value> cached_false_;
        std::optional<script_value> cached_zero_int_;
        std::optional<script_value> cached_one_int_;
        std::optional<script_value> cached_zero_float_;
        std::optional<script_value> cached_one_float_;

        // Pre-cached type_info pointers for fast value construction (bypasses engine indirection)
        type_info_ptr cached_type_info_int_ = nullptr;
        type_info_ptr cached_type_info_float_ = nullptr;
        type_info_ptr cached_type_info_bool_ = nullptr;

        // Fast value factories — skip null checks, cache comparisons, and engine indirection
        inline script_value make_int_fast(script_int i) const noexcept {
            script_value v(script_value::ast_literal_tag{}, i);
            v.engine_ = engine_;
            v.type_info_ = cached_type_info_int_;
            return v;
        }
        inline script_value make_float_fast(script_float f) const noexcept {
            script_value v(script_value::ast_literal_tag{}, f);
            v.engine_ = engine_;
            v.type_info_ = cached_type_info_float_;
            return v;
        }
        inline script_value make_bool_fast(script_bool b) const noexcept {
            script_value v(script_value::ast_literal_tag{}, b);
            v.engine_ = engine_;
            v.type_info_ = cached_type_info_bool_;
            return v;
        }

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

        // Resolve variable: slot-based O(1) first, then environment fallback
        [[nodiscard]] JAI_FORCEINLINE script_value* resolve_local_or_env(size_t slot_index, uint64_t symbol_id) noexcept {
            if (slot_index != SIZE_MAX && !call_stack_.empty()) {
                if (auto* ptr = call_stack_.back().get_local(slot_index)) {
                    return ptr;
                }
            }
            return environment_->get_value_ptr(symbol_id);
        }

        // Resolve variable with error on not found (avoids value copy vs environment_->get())
        [[nodiscard]] JAI_FORCEINLINE checked_result<script_value*> resolve_variable_required(
            size_t slot_index, uint64_t symbol_id) noexcept {
            if (auto* ptr = resolve_local_or_env(slot_index, symbol_id)) {
                return ptr;
            }
            return checked_result<script_value*>(
                make_error_code(runtime_error_code::undefined_variable),
                "Undefined variable '{0}'", symbol_id);
        }

        // Execution-budget check (see the public execution_budget API above)
        [[nodiscard]] JAI_FORCEINLINE bool execution_budget_exhausted() noexcept {
            if (++budget_tick_ < 1024) [[likely]] { return false; }
            budget_tick_ = 0;
            // Off-cycle debug sync: the ONLY mid-run point where this thread notices a
            // debugger attach/detach or breakpoint edit (one relaxed load when no
            // debugger was ever constructed). The vm twin carries the same shape
            // (vm_backend::execution_budget_exhausted) — keep them parallel.
            if (debugger_.load(std::memory_order_relaxed) || debug_hook_) [[unlikely]] {
                sync_debug_hook();
            }
            // An armed debug session suspends the wall-clock deadline (a paused script
            // must not be killed by the act of debugging); sync_debug_hook re-arms a
            // fresh deadline when the session ends.
            if (!budget_active_ || debug_hook_) { return false; }
            if (std::chrono::steady_clock::now() < execution_deadline_) { return false; }
            // Budget overruns are TERMINAL: no script catch may swallow a timeout
            limits_->terminal_error = true;
            return true;
        }

        template<typename T = void>
        [[nodiscard]] checked_result<T> execution_budget_error() const {
            return checked_result<T>(
                make_error_code(runtime_error_code::execution_budget_exceeded),
                "Script execution budget exceeded - raise engine::execution_budget or break up the work");
        }

        // Combined limit check at loop back-edges / call entry: the budget tick plus a
        // one-compare memory high-water test (deferred raise point for charge sites
        // that cannot deny in place). KEEP BYTE-PARALLEL with the vm twin.
        [[nodiscard]] JAI_FORCEINLINE bool execution_limit_exhausted() noexcept {
            return execution_budget_exhausted() || limits_->memory_tripped();
        }

        // Cold raise twin for execution_limit_exhausted (error_propagator converts to
        // any checked_result<T> the site returns)
        [[nodiscard]] error_propagator execution_limit_failure() {
            if (limits_->memory_tripped()) {
                return detail::raise_memory_cap(*limits_);
            }
            return error_propagator{
                make_error_code(runtime_error_code::execution_budget_exceeded),
                "Script execution budget exceeded - raise engine::execution_budget or break up the work"};
        }

        // Binary operation helpers
        [[nodiscard]] checked_result<script_value> evaluate_arithmetic(const script_value& left, token_type op, const script_value& right);

        // Strong types: Type enforcement for assignment
        // Returns error if value cannot be assigned to a variable with target_type
        // - nullptr target = uninitialized (auto x;), locks to source type
        // - any_type target = dynamic (var x), allows anything
        // - locked target = enforce compatibility or convert
        checked_result<script_value> enforce_type_compatibility(
            script_value value,
            type_info_ptr target_type,
            std::string_view var_name = ""
        );

        // Compound-assignment store-back (x op= rhs ≡ x = T(x op rhs)): a locked target
        // converts the promoted result through enforce_type_compatibility exactly like '=';
        // var (any-tagged) targets keep the promoted result AND their dynamic tag.
        checked_result<void> compound_typed_store_back(
            script_value& target,
            script_value promoted,
            std::string_view var_name
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

        // One resolution for every shape a C++/script object reaches the interpreter in:
        // class_instance wrappers (make_object, script classes), cpp_bound non-owning
        // references (T& returns), and raw C++ holders (unregistered make_object fallback).
        // Every member/method/operator path resolves through this so the shapes can't drift.
        struct member_target {
            std::shared_ptr<class_instance> instance;      // fields; null for cpp_bound/raw holders
            std::shared_ptr<class_definition> engine_def;  // anchors engine-resolved definitions
            class_definition* class_def = nullptr;         // methods/getters/setters/unwrap, both shapes

            explicit operator bool() const { return instance != nullptr || class_def != nullptr; }
            script_value method(uint64_t id) const;        // invalid value when absent
            bool has_field(uint64_t id) const;
            const script_value& get_field(uint64_t id) const;
            const std::string& class_name() const;         // empty when unknown
        };
        member_target resolve_member_target(const script_value& objectValue) const;

        // Writes 'value' to a member of 'objectValue' (deref'd, object-typed): C++ property
        // setters (including on cpp_bound receivers) take precedence over instance fields.
        // Raises a script exception (is_unwinding_) for non-existent members.
        checked_result<void> assign_member_value(const script_value& objectValue, member_expr* memberExpr, const script_value& value);

        // Helper to check if an expression is an lvalue (existing object that should be cloned)
        // Lvalues: identifiers, member access, subscript access
        // Non-lvalues (temporaries): function calls, constructors, literals, operators
        bool is_lvalue_expression(expression* e) const;

        inline bool is_truthy(const script_value& value) {
            // ULTRA-FAST: Use raw_storage_index() - single integer read, no pointer chasing
            // Avoids type_info_ null check and double-switch overhead
            // Bool case first since loop conditions almost always evaluate to bool
            switch (value.raw_storage_index()) {
                case script_value::TYPEID_BOOL: return value.unchecked_as_bool();    // bool - MOST COMMON for conditions
                case script_value::TYPEID_NULL: return false;                         // null (monostate)
                case script_value::TYPEID_INT: return value.unchecked_as_int() != 0; // int
                case script_value::TYPEID_FLOAT: return value.unchecked_as_float() != 0.0; // float
                case script_value::TYPEID_STRING: return !value.unchecked_as_string().empty(); // string
                case script_value::TYPEID_CHAR: return true;                          // char - any char is truthy
                case script_value::TYPEID_ARRAY: {
                    // array - empty is falsy (const_cast required, get_array_storage is non-const)
                    auto& arr = const_cast<script_value&>(value).get_array_storage();
                    return arr && !arr->empty();
                }
                case script_value::TYPEID_MAP: {
                    // map - empty is falsy (const_cast required, get_map_storage is non-const)
                    auto& map = const_cast<script_value&>(value).get_map_storage();
                    return map && !map->empty();
                }
                case script_value::TYPEID_OBJECT: return object_to_bool_via_method(value); // object - check for to_bool() method
                case script_value::TYPEID_CPP_BOUND:
                    // KEEP BYTE-PARALLEL with vm_backend::is_truthy (vm_backend.cpp) - the VM has its own copy
                    if (value.is_bool()) return value.unchecked_as_bool();
                    if (value.is_int()) return value.unchecked_as_int() != 0;
                    if (value.is_float()) return value.unchecked_as_float() != 0.0;
                    if (value.is_string()) return !value.unchecked_as_string().empty();
                    if (value.is_char()) return true;
                    // Opaque bound: truthy while the host pointer is live (§13, 2026-07)
                    return value.get_cpp_bound_ptr() != nullptr;
                case script_value::TYPEID_REFERENCE:
                    // References are transparent to truthiness like every other consumer
                    // (element reads reach conditions as reference wrappers; `if (a[0])`
                    // on a false element was always-truthy before this case existed)
                    return is_truthy(value.deref());
                default: return true;                         // functions, other complex types
            }
        }
        
        inline checked_result<script_value> to_numeric(const script_value& value) {
            if (value.is_int()) {
                return make_value(static_cast<script_float>(value.unchecked_as_int()));
            } else if (value.is_float()) {
                return value;
            } else if (value.is_bool()) {
                return make_value(value.unchecked_as_bool() ? 1.0 : 0.0);
            } else {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Cannot convert to numeric value");
            }
        }
        
        // Coroutine helper - creates a script_value wrapping a coroutine_handle.
        // Static (friend access to script_value) so coroutine-factory thunks need no interpreter.
        static script_value make_coroutine_object(engine* eng, uint64_t type_id, std::shared_ptr<coroutine_handle> handle);

        // Function call helpers
        // Returns checked_result - exceptions only thrown at execute() boundary
        // method_ctx supplies the method scope pieces (definition env, receiver, access
        // class) directly for the direct-dispatch fast path - same net frame/env shape
        // as a method-env closure_env carrier, minus the carrier.
        struct method_call_override {
            const std::shared_ptr<environment>* definition_env;
            const script_value* this_obj;
            class_definition* access_ctx;
        };
        checked_result<script_value> call_function(const script_defined_function& function, const std::vector<script_value>& args,
                                                   const method_call_override* method_ctx = nullptr);
        // Stateless reference-parameter binding from the call-site arg expression,
        // factored out of call_function so its locals don't sit in the
        // per-recursion-level frame (Debug stack ceiling)
        checked_result<void> bind_reference_parameter(const parameter& param, size_t frame_index,
                                                      const expression* argExpr,
                                                      const std::shared_ptr<environment>& caller_env);
        // Share-or-box against the caller's variable storage (cell model)
        checked_result<void> bind_reference_to_storage(script_value& storage, size_t frame_index, size_t param_slot);
        void validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args);

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
        static strong_ptr<script_array>& get_array_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_array_storage();
        }

        static strong_ptr<std::map<script_value, script_value>>& get_map_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_map_storage();
        }
        
    public:
        // Set the subscript resolver callback
        void set_subscript_resolver(subscript_resolver resolver) {
            subscriptResolver_ = std::move(resolver);
        }

        // Parallel worker setup (parallel_transform v0): pins this instance to a worker
        // context built at the region barrier - root environment (parent = nullptr, the
        // hard partition), a private env-epoch sink, the worker's own limits, and an
        // armed budget clock. Workers never call prepare_for_execution, so nothing here
        // is ever repointed at engine-shared state.
        void configure_parallel_worker(std::shared_ptr<environment> root_env,
                                       string_symbolizer* env_epoch_sink,
                                       detail::execution_limits* worker_limits,
                                       std::chrono::nanoseconds budget) {
            environment_ = std::move(root_env);
            env_symbolizer_ = env_epoch_sink;
            limits_ = worker_limits;
            // Pool envs minted before this point carry the shared epoch sink - drop them
            environment_pool_.clear();
            environment_pool_index_ = 0;
            // Slot REUSE reset (worker contexts persist across regions): a prior call
            // may have ended mid-chunk on an error - clear every piece of residual
            // execution state so the slot starts this region pristine.
            valueStack_.clear();
            call_stack_.clear();
            current_call_depth_ = 0;
            is_unwinding_ = false;
            current_exception_.reset();
            active_exception_value_.reset();
            trace_captured_ = false;
            captured_trace_.clear();
            hasBreakRequest_ = false;
            hasContinueRequest_ = false;
            hasYieldRequest_ = false;
            returnValue_.reset();
            hasReturnValue_ = false;
            pending_call_ctx_ = nullptr;
            external_ctx_stack_.clear();
            execution_budget(budget);
            parallel_worker_ = true;
            arm_execution_deadline();
        }

        // True on worker-configured instances only: the script-class method dispatch
        // wall (methods aren't provisioned per worker - their shared body caches and
        // arbitrary env reach are unsafe off-thread) checks this, gated by
        // engine::allow_unsafe_parallel. Never set on the engine's own backend.
        bool parallel_worker_ = false;

        // Environment pooling functions (public: scoped_method_environment uses them)
        std::shared_ptr<environment> get_pooled_environment(std::shared_ptr<environment> parent);
        std::shared_ptr<environment> get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj, class_definition* access_ctx = nullptr);
        void release_environment(const std::shared_ptr<environment>& env, bool clear_now = true);
        void reset_environment_pool();
    };
    
} // namespace jai

#endif // __JAISCRIPT_DETAIL_INTERPRETER_HPP__