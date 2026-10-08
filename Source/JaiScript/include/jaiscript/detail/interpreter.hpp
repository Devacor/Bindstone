#pragma once

#ifndef __JAISCRIPT_DETAIL_INTERPRETER_HPP__
#define __JAISCRIPT_DETAIL_INTERPRETER_HPP__

// NOTE: the compile-time integer-overflow policy (kCheckedOverflow and the
// jai::ints helpers) lives in <jaiscript/detail/integer_ops.hpp>, which the
// engine .cpp files include directly. It is intentionally kept out of this
// widely-included header so its <intrin.h> dependency does not bloat consumer
// translation units.

#include "ast.hpp"
#include "null_member_diagnostic.hpp"
#include "arity_diagnostic.hpp"
#include "slot_site.hpp"
#include "string_symbolizer.hpp"
#include "environment.hpp"
#include "member_scope.hpp"
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
#include <system_error>
#include <optional>
#include <chrono>
#include <atomic>

namespace jai {

    // Forward declarations
    class script_class_definition;
    class class_definition;
    class class_instance;
    namespace detail { struct namespace_store_target; struct aggregate_plan; }
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
            // Expression replay: which evaluation path of the node suspended (replay_phase) and
            // the operands it had produced before the suspended child, in evaluation order;
            // for those frames `index` is the suspended child's ordinal
            uint32_t phase = 0;
            std::vector<script_value> partial;
        };

        std::shared_ptr<environment> saved_environment;
        std::vector<call_frame> saved_call_stack;
        std::optional<script_value> saved_return_value;
        bool saved_has_return = false;
        // The yield-point member scope rides the suspension exactly like the env
        // chain (call_function's yield path skips cleanup, so the ordinary
        // save/restore never runs for a suspending body).
        detail::member_scope_ctx saved_member_scope{};
        // The exception trio is coroutine state too (vm run_fiber parity): a yield inside a
        // catch keeps the caught exception (bare throw;) and the in-catch marker across the
        // resumer's own try/catch
        std::optional<script_exception> parked_exception;
        std::optional<script_value> parked_exception_value;
        uint64_t parked_catch_marker = 0;
        // The body's function object outlives its first resume: the saved frames point at
        // it across every later resume (return-site conversion, debugger locals)
        std::optional<script_defined_function> function;

        // A coroutine dropped while suspended: its frames die here, then the cells they shared are checked for cycles
        ~interpreter_coroutine_state() override {
            std::vector<script_value> shared;
            for (auto& frame : saved_call_stack) {
                if (frame.boxed_cells || (frame.function && frame.function->body && frame.function->body->shares_cells)) {
                    script_value::gather_shared_cells(frame.locals.data(), frame.locals.size(), shared);
                }
            }
            if (shared.empty()) { return; }
            engine* eng = shared.front().get_engine();
            saved_call_stack.clear();
            script_value::release_cell_cycles(shared, eng);
        }

        void push_continuation(ast_node* node, size_t index,
                               std::shared_ptr<environment> env = nullptr,
                               std::optional<script_value> container = std::nullopt) {
            continuations_.push_back({node, index, std::move(env), std::move(container)});
        }
        void push_replay(ast_node* node, uint32_t phase, size_t index,
                         std::vector<script_value> partial = {},
                         std::shared_ptr<environment> env = nullptr,
                         std::optional<script_value> container = std::nullopt) {
            continuations_.push_back({node, index, std::move(env), std::move(container), phase, std::move(partial)});
        }
        // The innermost frame pushed so far (the node that owns the suspended child)
        ast_node* back_node() const { return continuations_.empty() ? nullptr : continuations_.back().node; }
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
    class interpreter : public std::enable_shared_from_this<interpreter> {
    public:
		checked_result<bool> language_less(const script_value& a_left, const script_value& a_right);
		checked_result<script_value> array_transform(uint64_t a_member, script_value& a_self, const std::vector<script_value>& a_args);
		checked_result<script_value> map_transform(uint64_t a_member, script_value& a_self, const std::vector<script_value>& a_args);
		bool native_less_available() const {
			return !operator_table_ || !operator_table_->entry(detail::op_slot::less);
		}

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
            const auto now = std::chrono::steady_clock::now();   // one read serves the deadline and the engine's clock sample
            if (budget_active_) {
                execution_deadline_ = now + execution_budget_;
            }
            note_clock_sample(now);
        }
        void note_clock_sample(std::chrono::steady_clock::time_point now);


        // Scope management for local variables
        void push_scope();
        void pop_scope();
        void push_instance_scope();
        void pop_instance_scope();
        
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
        // A string literal's value: the engine that parsed the program shares the AST
        // node (copies share string storage everywhere else; a write unshares); a parallel
        // worker copies the bytes, because the node's refcount is not atomic and the main
        // engine and the other workers hold the same literal
        script_value literal_string_value(const strong_ptr<script_string>& node) const {
            if (!engine_) {
                throw runtime_error("Engine pointer is null - cannot create script_value");
            }
            return parallel_worker_ ? script_value(*node, engine_) : script_value(node, engine_);
        }
        script_value hydrate_literal(const script_value& lit) const {
            const auto& storage = lit.get_storage();
            switch (storage.index()) {
                case 1: return make_value(storage.get<script_int>());
                case 2: return make_value(storage.get<script_float>());
                case 3: return literal_string_value(storage.get<strong_ptr<script_string>>());
                case 4: return make_value(storage.get<script_char>());
                case 5: return make_value(storage.get<script_bool>());
                case 0: return make_value();
                default: { script_value v = lit; v.set_engine(engine_); return v; }
            }
        }

        // Execute a list of declarations and return the last value
        script_value execute(const std::vector<declaration_ptr>& declarations, bool textual = false);   // textual: an include, run where it is written
        // the unit of the running code, derived from the executing call frame (docs/global_shadowing.md), and the
        // re-alignment of the symbolizer's ambient with it (a store only while a name has versions)
        unit_stamp running_unit() const noexcept { return current_unit(); }
        void sync_unit() noexcept { if (!parallel_worker_ && string_symbolizer_->shadow_count() != 0) { string_symbolizer_->set_current_unit(current_unit()); } }
        
        // Execute a single expression and return its value
        script_value evaluate(expression_ptr expr);
        
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
        std::string current_source_file() const;
        void park_nested_failure();

        // Resume a suspended (or start a created) coroutine on this interpreter.
        // Reached via execution_backend::resume_coroutine; the handle's opaque
        // backend_state holds the interpreter_coroutine_state.
        checked_result<script_value> resume_coroutine(coroutine_handle& handle);

        // Execute a script-defined callable payload (reached via execution_backend::execute_callable)
        checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args);

        // expression visitors
        checked_result<void> visit_literal_expr(literal_expr* expr);
        checked_result<void> visit_identifier_expr(identifier_expr* expr);
        checked_result<void> visit_binary_expr(binary_expr* expr);
        checked_result<void> visit_unary_expr(unary_expr* expr);
        checked_result<void> visit_assignment_expr(assignment_expr* expr);
        checked_result<void> visit_call_expr(call_expr* expr);
        checked_result<void> visit_member_expr(member_expr* expr);
        checked_result<void> visit_lambda_expr(lambda_expr* expr);
        checked_result<void> visit_new_expr(new_expr* expr);
        checked_result<void> visit_ternary_expr(ternary_expr* expr);
        checked_result<void> visit_array_literal_expr(array_literal_expr* expr);
        checked_result<void> visit_map_literal_expr(map_literal_expr* expr);
        checked_result<void> visit_this_expr(this_expr* expr);
        checked_result<void> visit_super_expr(super_expr* expr);
        checked_result<void> visit_throw_expr(throw_expr* expr);
        checked_result<void> visit_cast_expr(cast_expr* expr);
        checked_result<void> visit_yield_expr(yield_expr* expr);

        // Coroutine expression replay: a yield unwinds as the coroutine_suspended signal,
        // each visitor on the way out parks the operands it had produced (replay_phase +
        // child ordinal), and the <true> instantiations resume from the parked child. The
        // <false> instantiations are the ordinary visitors, byte for byte.
        using replay_point = interpreter_coroutine_state::continuation_point;
        enum replay_phase : uint32_t {
            rp_none = 0,
            rp_root,           // statement: its one expression suspended
            rp_cond,           // if / while / for / switch: the condition
            rp_update,         // for: the update expression
            rp_init,           // for: the initializer declaration
            rp_container,      // range-for: the container expression
            rp_body_done,      // if / while / for / range-for: a brace-less body completed at the yield
            rp_case_value,     // switch: case[index]'s value (partial[0] = the switch value)
            rp_case_body,      // switch: case[index]'s body (saved_env = its scope)
            rp_default_body,   // switch: the default body (saved_env = its scope)
            rp_catch,          // try: inside the catch block (saved_container = the caught value)
            rp_operand,        // ordinal-only expression nodes
            rp_call_math, rp_call_string, rp_call_direct, rp_call_generic,
            rp_new_weak, rp_new_shared, rp_new_class,
            rp_asg_rhs, rp_asg_member_compound, rp_asg_sub_fast, rp_asg_general, rp_asg_regular,
        };
        template<bool Replaying> checked_result<void> visit_binary_expr_t(binary_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_unary_expr_t(unary_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_assignment_expr_t(assignment_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_call_expr_t(call_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_member_expr_t(member_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_new_expr_t(new_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_ternary_expr_t(ternary_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_array_literal_expr_t(array_literal_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_map_literal_expr_t(map_literal_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_cast_expr_t(cast_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_throw_expr_t(throw_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_yield_expr_t(yield_expr* expr, const replay_point* rs);
        template<bool Replaying> checked_result<void> visit_expression_stmt_t(expression_stmt* stmt);
        template<bool Replaying> checked_result<void> visit_return_stmt_t(return_stmt* stmt);
        template<bool Replaying> checked_result<void> visit_variable_decl_t(variable_decl* decl);
        template<bool Replaying> checked_result<void> visit_destructuring_decl_t(destructuring_decl* decl);
        template<bool Replaying> checked_result<void> visit_expression_decl_t(expression_decl* decl);
        // Resume side: run the suspended child of a parked node (a child with no frame is the
        // consumed yield leaf: null, its operand never re-evaluated)
        checked_result<void> replay_expression(expression* expr);
        checked_result<void> dispatch_stmt_resuming(statement* stmt);
        checked_result<void> dispatch_decl_resuming(declaration* decl);
        checked_result<void> run_case_body(ast_node* self, const std::vector<statement_ptr>& body);
        checked_result<void> store_through_subscript_reference(script_value& target_ref, script_value value);
        checked_result<void> non_lvalue_target(const expression* target);
        checked_result<void> const_guard_at(const ast_node* node, const const_site& site);
        script_value string_element_pin(script_value& base_raw, const expression* base_expr);
        // Unwind side (cold): park a node's produced operands / mark a statement's root
        void park_operands(ast_node* node, uint32_t phase, size_t index, std::vector<script_value> partial);
        void note_statement_yield(ast_node* stmt, expression* root);
        template<typename T> static bool is_yield_signal(const checked_result<T>& r) {
            return !r && r.error() == make_error_code(runtime_error_code::coroutine_suspended);
        }
        // Operand helpers: the <false> instantiations fold to the plain dispatch
        template<bool Replaying> checked_result<void> eval_child(expression* child, const replay_point* rs, size_t ordinal) {
            if constexpr (Replaying) { if (rs && rs->index == ordinal) return replay_expression(child); }
            return dispatch_expr(child);
        }
        template<bool Replaying> checked_result<void> eval_root(expression* root) {
            if constexpr (Replaying) return replay_expression(root); else return dispatch_expr(root);
        }
        template<bool Replaying> static constexpr bool parked(const replay_point* rs, size_t ordinal) {
            if constexpr (Replaying) return rs && ordinal < rs->index; else return false;
        }

        // statement visitors
        checked_result<void> visit_expression_stmt(expression_stmt* stmt);
        checked_result<void> visit_block_stmt(block_stmt* stmt);
        checked_result<void> visit_if_stmt(if_stmt* stmt);
        checked_result<void> visit_while_stmt(while_stmt* stmt);
        checked_result<void> visit_for_stmt(for_stmt* stmt);
        checked_result<void> visit_range_for_stmt(range_for_stmt* stmt);
        checked_result<void> run_for_stmt(for_stmt* stmt);
        checked_result<void> run_range_for_stmt(range_for_stmt* stmt);
        // Scope exits destroy the frame-slot locals that may own a script destructor, last declared first (the vm's
        // op_kill_slot); nothing to do until a destructor-armed instance exists
        void kill_scope_slot(const statement* decl);
        template <class Decls> void kill_scope_slots(const Decls& decls) {
            if (call_stack_.empty() || !engine_->destructors_armed()) { return; }
            for (auto decl = decls.rbegin(); decl != decls.rend(); ++decl) { kill_scope_slot(decl->get()); }
        }
        checked_result<void> visit_parallel_for_stmt(parallel_for_stmt* stmt);
        checked_result<void> visit_return_stmt(return_stmt* stmt);
        checked_result<void> visit_break_stmt(break_stmt* stmt);
        checked_result<void> visit_continue_stmt(continue_stmt* stmt);
        checked_result<void> visit_try_stmt(try_stmt* stmt);
        checked_result<void> visit_switch_stmt(switch_stmt* stmt);
        checked_result<void> visit_case_stmt(case_stmt* stmt);
        checked_result<void> visit_default_stmt(default_stmt* stmt);
        checked_result<void> visit_fallthrough_stmt(fallthrough_stmt* stmt);

        // declaration visitors
        checked_result<void> visit_variable_decl(variable_decl* decl);
        checked_result<void> visit_function_decl(function_decl* decl);
        checked_result<void> visit_class_decl(class_decl* decl);
        checked_result<void> visit_namespace_decl(namespace_decl* decl);
        checked_result<void> visit_expression_decl(expression_decl* decl);
        checked_result<void> visit_include_decl(include_decl* decl);
        checked_result<void> visit_include_expr(include_expr* expr);
        checked_result<void> visit_import_expr(import_expr* expr);
        checked_result<void> import_step(import_site& site, const std::vector<std::string>* literal, const std::string* single,
                                         const std::vector<expression_ptr>& computed, bool force, bool yields);
        checked_result<void> include_into_value_stack(expression* path_expr_node, const std::string& literal_path);
        checked_result<void> visit_import_decl(import_decl* decl);
        checked_result<void> visit_enum_decl(enum_decl* decl);
        checked_result<void> visit_destructuring_decl(destructuring_decl* decl);
        checked_result<void> visit_extern_decl(extern_decl* decl);
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value) { has_custom_numeric_ops_ = value; }
        bool has_custom_numeric_ops() const { return has_custom_numeric_ops_; }
        void set_has_custom_binary_ops(bool value) { has_custom_binary_ops_ = value; }
        void set_operator_table(const detail::engine_operator_table* table) { operator_table_ = table; }
        
        // Accessors for script class support
        std::shared_ptr<environment> get_current_environment() const { return environment_; }

        // Caller context for builtins (reflect::set/invoke enforcement): a builtin call
        // pushes no script frame, so the live environment chain answers for the caller
        const class_definition* current_access_context() const {
            return environment_ ? environment_->find_access_context() : nullptr;
        }

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
        void begin_host_call();
        void push_external_call_scope() {
            // A host-level callback (no script in flight) is its own budgeted entry
            if (execute_depth_ == 0 && call_stack_.empty()) { arm_execution_deadline(); }
            external_ctx_stack_.push_back(pending_call_ctx_);
            pending_call_ctx_ = nullptr;
        }
        checked_result<script_value> host_reference_argument(const std::vector<script_value>& args, size_t index, size_t offset,
                                                             host_slot& slot);
        checked_result<void> host_slot_store(const host_slot& slot, script_value& value);
        void pop_external_call_scope() {
            if (!external_ctx_stack_.empty()) {
                pending_call_ctx_ = external_ctx_stack_.back();
                external_ctx_stack_.pop_back();
            }
        }

        // Execute a method AST with a given environment
        checked_result<script_value> execute_method_ast(std::shared_ptr<function_decl> ast,
                                      std::shared_ptr<environment> method_env,
                                      const std::vector<script_value>& args);

        // Evaluate field initializers for a script class instance at construction time
        // This should be called BEFORE the constructor body executes
        // If skip_parent_recursion is true, only evaluate THIS class's field initializers (parents already handled)
        checked_result<void> evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                        std::shared_ptr<script_class_definition> class_def,
                                        std::shared_ptr<environment> init_env,
                                        bool skip_parent_recursion = false,
                                        const detail::aggregate_plan* aggregate = nullptr);
        // interpreter_backend forwards execution_backend::apply_field_initializers here
        void apply_field_initializers(const std::shared_ptr<class_instance>& instance,
                                      const std::shared_ptr<class_definition>& class_def);

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
        // `ctor`'s member initialization on an instance under construction: its initializer list (a base constructor, or the
        // whole delegated-to constructor) and then the field initializers; the body is the caller's
        checked_result<script_value> initialize_members(const std::shared_ptr<script_class_definition>& class_def,
                                                        const std::shared_ptr<environment>& definition_env,
                                                        const std::shared_ptr<class_instance>& instance,
                                                        const script_value& this_value,
                                                        const std::shared_ptr<function_decl>& matching_ctor,
                                                        const std::vector<script_value>& args,
                                                        std::optional<detail::call_depth_guard>& initializer_depth,
                                                        size_t delegation_depth,
                                                        std::vector<script_value>* captured_out);
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
        struct binary_host;   // this backend's hooks for detail/binary_handlers.hpp
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
        checked_result<bool> language_equal(const script_value& left, const script_value& right);
        const expression* compound_map_target_ = nullptr;   // the subscript a general compound is reading as its target
        builtin_method_context builtin_ctx() {
            return builtin_method_context{engine_, string_symbolizer_,
                [](void* self, const script_value& a, const script_value& b) { return static_cast<interpreter*>(self)->language_equal(a, b); },
                [](void* self) { return static_cast<interpreter*>(self)->is_unwinding_; }, this};
        }
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
        // An operand-shape fast lane: numbers (and, with concat, two strings under +) push their result; true = handled,
        // false = the general handlers take it
        checked_result<bool> scalar_lane(token_type op, const script_value& left, const script_value& right, bool concat);
        
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
        std::vector<std::shared_ptr<environment>> instance_scope_saved_;
        std::shared_ptr<environment> pending_instance_env_;

        // Current frame's member scope (detail/member_scope.hpp): stamped by
        // call_function's callee-kind branches (and the field-initializer site),
        // saved/restored around every call like environment_. Drives the C++
        // members-shadow-globals resolution order in the identifier ladders.
        detail::member_scope_ctx member_scope_{};

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

            // Discard the top value (ignored return values)
            void discard() {
                if (top_ == 0) {
                    throw runtime_error("Internal error: empty value stack");
                }
                script_value released = std::move(values_[--top_]);   // a discarded value dies now, not when the slot is reused
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
        // A C++ exception raised under a call becomes the script exception its text throws; the call yields null
        checked_result<void> raise_native_exception(const std::exception& e);

        std::vector<stack_trace_entry> captured_trace_;
        bool trace_captured_ = false;
        const ast_node* top_level_node_ = nullptr;
        void capture_stack_trace();

        std::optional<error_propagator> run_error_;   // the runtime error that ended this run's top level
        detail::nested_failure nested_failure_;
        void absorb_nested_trace();
        checked_result<void> raise_nested_failure(const std::exception& e);

        // Class parsing context - tracks unresolved identifiers in methods
        struct class_context {
            std::string class_name;
            std::set<uint64_t> unresolved_identifiers;  // Interned IDs of identifiers found in methods but not yet resolved
            bool in_method = false;
        };
        std::optional<class_context> current_class_context_;
        // "Inside a catch block" marker of the rethrow protocol (0 = none); the catch
        // VARIABLE is a frame slot / scope local bound by visit_try_stmt, never resolved
        // through this. Parked per coroutine with the exception pair (resume_coroutine).
        uint64_t current_catch_var_id_ = 0;

        // Switch statement control flow state
        bool in_switch_ = false;
        bool should_fallthrough_ = false;

        // True only while evaluating the outermost subscript that is the TARGET of
        // an assignment (e.g. the `m[k]` in `m[k] = v`). Map subscript needs to
        // auto-insert a slot in that case, but a plain READ (`x = m[k]`) must NOT
        // grow the map. visit_binary captures and clears this immediately so
        // nested subscripts are treated as reads.
        bool lvalue_target_context_ = false;
        // The compound-store target read (s[i] op= v): a string base mints a STRING-ELEMENT
        // reference like the `=` target does; captured and cleared with the flag above
        bool string_target_context_ = false;

        // Call depth tracking for recursion limit
        int current_call_depth_ = 0;

        // Execution budget state (see execution_budget()/arm_execution_deadline above)
        std::chrono::nanoseconds execution_budget_{std::chrono::seconds(5)};
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
        uint64_t eq_method_id_ = 0;   // "==" interned at construction (worker probes must not intern)
        uint64_t to_bool_id_ = 0;
        uint64_t to_string_id_ = 0;
        base_type_symbols base_type_ids_;   // scalar_member_message spellings, interned at construction
        detail::arity_count_symbols arity_count_ids_;   // arity text count spellings, interned at construction
        bool identifier_callee_lookup_ = false;   // visit_call_expr -> visit_identifier: the bare name is being called
        bool member_callee_lookup_ = false;   // visit_call_expr -> visit_member_expr: the member is being called (null-receiver text)

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
        uint64_t op_tilde_id_ = 0;   // "~" (a class's operator~)
        uint64_t op_call_id_ = 0;    // "()" (a class's operator(), consulted only where a call would raise not callable)
        uint64_t subscript_op_id_;  // "[]"
        uint64_t assign_operator_id_;  // "=" for class assignment operator overload

        // a compound assignment's operator method (`&=` -> operator&); 0 for none
        uint64_t compound_op_symbol(token_type assign_op) const noexcept {
            switch (assign_op) {
                case token_type::plus_equal: return op_plus_id_;
                case token_type::minus_equal: return op_minus_id_;
                case token_type::star_equal: return op_star_id_;
                case token_type::slash_equal: return op_slash_id_;
                case token_type::percent_equal: return op_percent_id_;
                case token_type::ampersand_equal: return op_ampersand_id_;
                case token_type::pipe_equal: return op_pipe_id_;
                case token_type::caret_equal: return op_caret_id_;
                case token_type::left_shift_equal: return op_left_shift_id_;
                case token_type::right_shift_equal: return op_right_shift_id_;
                default: return 0;
            }
        }

		// Cached symbol IDs for common keywords (initialized in constructor)
		uint64_t this_id_;
		uint64_t super_id_;
		uint64_t getValue_id_;
		uint64_t cpp_object_field_id_;  // "_cpp_object"

        // The running assignment's value is consumed (it reads the destination back); false only for the root of an
        // expression statement, whose value is discarded
        bool expression_result_needed_ = true;
        const expression* unconsumed_root_ = nullptr;

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
        // The next dispatch is no new statement execution (set only while armed): a for initializer, whose header
        // stopped, or a statement a coroutine resume re-enters.
        const ast_node* debug_quiet_node_ = nullptr;

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
        script_value share_boxed_slot(script_value& storage);

        inline script_value pop_value() {
            return valueStack_.pop();
        }

        inline void push_value(const script_value& value) {
            valueStack_.push(value);
        }

        inline void push_value(script_value&& value) {
            valueStack_.push(std::move(value));
        }

        // Resolve variable: slot-based O(1) first, then the scope ladder. Inside a
        // member scope (detail/member_scope.hpp) the order is C++ unqualified lookup:
        // the function's own scopes, the class's members, then the definition/global
        // chain. A member METHOD or C++ PROPERTY hit answers nullptr - it shadows (never
        // fall past the class scope to a same-named global) but only value/callee/ref-bind
        // sites mint, so pointer callers land on their existing miss paths.
        [[nodiscard]] JAI_FORCEINLINE script_value* resolve_local_or_env(size_t slot_index, uint64_t symbol_id, bool global_only = false) noexcept {
            if (global_only) [[unlikely]] {
                // ::name - GLOBAL env only (locals, captures, member scope all skipped)
                return get_global_environment()->get_value_ptr(symbol_id);
            }
            if (slot_index != SIZE_MAX && !call_stack_.empty()) {
                if (auto* ptr = call_stack_.back().get_local(slot_index)) {
                    return ptr;
                }
            }
            if (member_scope_.active()) {
                return scoped_env_lookup(symbol_id);
            }
            return environment_->get_value_ptr(symbol_id);
        }

        // A reference to a bare member out of reach names the access text, never the undefined one
        checked_result<void> bare_reference_access(uint64_t symbol_id) {
            if (!member_scope_.active()) { return {}; }
            auto hit = detail::probe_member_scope(member_scope_.state, member_scope_.this_storage,
                                                  member_scope_.statics, symbol_id, /*want_methods*/ false);
            return hit ? detail::bare_member_access(hit, symbol_id, environment_->find_access_context()) : checked_result<void>{};
        }

        // The class whose code is running, for a bare static name off the member-scope ladder
        class_definition* bare_declaring_class() const noexcept {
            if (member_scope_.state == detail::member_scope_state::instance && member_scope_.statics) { return member_scope_.statics; }
            return detail::lexical_access_class(environment_.get());
        }

        // The non-slot rungs of the member-scope ladder (member_scope_.active() only).
        script_value* scoped_env_lookup(uint64_t symbol_id) noexcept {
            if (script_value* own = detail::own_scope_lookup(environment_.get(), member_scope_.floor, symbol_id)) {
                return own;
            }
            auto hit = detail::probe_member_scope(member_scope_.state, member_scope_.this_storage,
                                                  member_scope_.statics, symbol_id, /*want_methods*/ false);
            if (hit) {
                if (hit.class_def && hit.class_def->chain_has_nonpublic() &&
                    !detail::bare_member_access(hit, symbol_id, environment_->find_access_context())) [[unlikely]] {
                    return nullptr;   // out of reach: the value site raises the access text
                }
                return hit.value_ptr;   // a C++ property answers nullptr: a shadow, minted by value sites
            }
            if (detail::member_scope_has_method(member_scope_.state, member_scope_.this_storage,
                                                member_scope_.statics, symbol_id)) {
                return nullptr;
            }
            return member_scope_.floor ? member_scope_.floor->get_value_ptr(symbol_id)
                                       : environment_->get_value_ptr(symbol_id);
        }

        // The cell an identifier RECEIVER of a string builtin mutates: visit_identifier_expr's
        // ladder as a pointer (:: door, slot, member scope, env). KEEP BYTE-PARALLEL
        // with vm_backend::string_receiver_cell.
        [[nodiscard]] script_value* string_receiver_cell(const identifier_expr* ident) noexcept {
            return resolve_local_or_env(ident->slot_index, ident->symbol_id, ident->global_qualified);
        }

        // The bare-name ref-bind rung after resolve_local_or_env misses: a hybrid's C++
        // property under the current member scope mints a CPP-MEMBER / PROPERTY reference
        // (nullopt = not a property here; the caller keeps its undefined-variable text).
        // Defined in interpreter.cpp (needs detail/ref_lvalue.hpp).
        std::optional<checked_result<script_value>> bind_bare_cpp_property_ref(uint64_t symbol_id, bool global_qualified,
                                                                                environment* env = nullptr);

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
            const auto now = std::chrono::steady_clock::now();
            limits_->note_clock_sample(now);
            if (limits_->step_budget_spent()) [[unlikely]] { limits_->terminal_error = true; return true; }   // the clock-free bound (vm twin)
            if (!budget_active_ || debug_hook_) { return false; }
            if (now < execution_deadline_) { return false; }
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
        [[nodiscard]] checked_result<script_value> evaluate_arithmetic(const script_value& left, token_type op, const script_value& right, int compound = -1);

        // Strong types: Type enforcement for assignment
        // Returns error if value cannot be assigned to a variable with target_type
        // - nullptr target = uninitialized (auto x;), locks to source type
        // - any_type target = dynamic (var x), allows anything
        // - locked target = enforce compatibility or convert
        // ns::x = v / op= / ++ on a namespace variable's cell (detail/namespace_scope.hpp resolves the target)
        checked_result<script_value> store_namespace_cell(const detail::namespace_store_target& target, const member_expr* member, script_value incoming);
        checked_result<script_value> enforce_type_compatibility(
            script_value value,
            type_info_ptr target_type,
            std::string_view var_name = "",
            detail::slot_kind kind = detail::slot_kind::assignment
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
        // An operand's text for string +: an object's to_string() when it declares one (its error is the caller's)
        checked_result<std::string> value_to_string_with_method(const script_value& val);
        checked_result<script_value> concat_with_methods(const script_value& left, const script_value& right);
        // User operator methods (==, <, <=, >, >=, + - * / %): nullopt when the operand defines none (or the method's result
        // does not answer the question), else the method's result or the runtime error it raised
        bool object_defines_custom_equality(const script_value& v) const;
        std::optional<checked_result<bool>> object_equality_via_method(const script_value& left, const script_value& right);
        std::optional<checked_result<bool>> object_inequality_via_method(const script_value& left, const script_value& right);
        std::optional<checked_result<bool>> object_comparison_reversed_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);
        std::optional<checked_result<script_value>> object_spaceship_reversed_via_method(const script_value& left, const script_value& right);
        std::optional<checked_result<bool>> object_spaceship_equality_via_method(const script_value& left, const script_value& right);
        std::optional<checked_result<bool>> object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);
        std::optional<checked_result<script_value>> object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);
        std::optional<checked_result<script_value>> object_unary_via_method(const script_value& operand, token_type op);   // a class's unary - + ~
        script_value call_operator_of(const script_value& callee);   // the instance's operator() bound to it, else null
        checked_result<void> method_value_error(const member_expr* expr, const script_value& object, bool bindable);   // obj.method without a call
        // the unwinding state for a composed text, built out of line so the member paths carry no string temporaries
        void raise_script_text(const std::string& text, const source_location& location);
        void raise_no_member(std::string_view kind, std::string_view member, const source_location& location);
        checked_result<void> bare_method_value_error(const identifier_expr* expr, const script_value& self);   // a bare method name without a call
        std::optional<checked_result<void>> bare_method_reference_error(const identifier_expr* expr);   // a reference to a bare method name

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
            script_value user_accessor(uint64_t id) const; // nearest user _get_/_set_ (synthesized skipped); invalid when absent
            bool has_field(uint64_t id) const;
            const script_value& get_field(uint64_t id) const;
            std::string_view class_name() const;           // empty when unknown
        };
        member_target resolve_member_target(const script_value& objectValue) const;

        // Writes 'value' to a member of 'objectValue' (deref'd, object-typed): C++ property
        // setters (including on cpp_bound receivers) take precedence over instance fields.
        // Raises a script exception (is_unwinding_) for non-existent members.
        checked_result<void> assign_member_value(const script_value& objectValue, member_expr* memberExpr, const script_value& value);
        // `base.x = v` on a math value: the store lands in the storage the base names (detail/math_component_store.hpp)
        checked_result<script_value> assign_math_component(script_value& base, member_expr* memberExpr, const script_value& value);
        // The member read on an already-evaluated receiver (visit_member_expr's tail; the
        // compound-member store evaluates the receiver once and reads through this)
        checked_result<void> member_read_on(member_expr* expr, script_value objectValue, bool as_callee);

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
                case script_value::TYPEID_UTF8_STRING: return !value.unchecked_as_utf8().bytes.empty();
                case script_value::TYPEID_VEC2: case script_value::TYPEID_IVEC2: case script_value::TYPEID_VEC3: case script_value::TYPEID_IVEC3: case script_value::TYPEID_VEC4: case script_value::TYPEID_COLOR: return value.math_truthy();   // not all zero
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
        // a by-reference argument is the caller's code: it resolves under caller_unit (the call has entered the callee's unit)
        checked_result<void> bind_reference_parameter(const parameter& param, size_t frame_index,
                                                      const expression* argExpr,
                                                      const std::shared_ptr<environment>& caller_env,
                                                      const script_value* evaluated_arg,
                                                      unit_stamp caller_unit,
                                                      const script_defined_function* callee = nullptr, size_t param_index = 0);
        // Share-or-box against the caller's variable storage (cell model)
        checked_result<bool> reference_to_storage(script_value& storage, script_value& out);
        // `v.x` where a reference binds: a COMPONENT reference (detail/math_component_store.hpp), else a non-reference
        checked_result<script_value> component_reference(const expression* e, call_frame* caller_locals, const std::shared_ptr<environment>& caller_env);
        // A reference declaration's math-component initializer as a COMPONENT reference, else the declaration's non-lvalue error
        checked_result<script_value> declared_component_reference(variable_decl* decl);
        checked_result<bool> bind_site_reference(const expression* argExpr, call_frame* caller_locals,
                                                 const std::shared_ptr<environment>& caller_env, script_value& out,
                                                 host_slot* slot_out = nullptr, size_t caller_index = 0);
        checked_result<bool> mint_argument_place(const expression* argExpr, script_value& out);
        bool later_place_argument(const script_defined_function& function, const detail::call_site_context* call_ctx, size_t from,
                                  const std::vector<script_value>& args) const;
        checked_result<void> bind_later_places(const script_defined_function& function, const detail::call_site_context* call_ctx,
                                               size_t from, std::vector<script_value>& args, size_t frame_index,
                                               const std::shared_ptr<environment>& caller_env, unit_stamp caller_unit);

        // Type conversion helpers (constructor-based conversions)
        // Attempts to convert a value to a target type using constructor-based conversion.
        // Returns the converted value on success, or the original value if no conversion is needed/possible.
        // Returns checked_result with error if conversion fails.
        [[nodiscard]] checked_result<script_value> try_convert_for_parameter(const script_value& arg, type_info_ptr target_type,
                                                                            const detail::slot_site& site = detail::slot_site{});

        // Check if a conversion is possible from source value to target type
        bool can_convert_to_type(const script_value& source, type_info_ptr target_type) const;

        // Static method registries for built-in types
        // Built-in method registries (using interned IDs for O(1) lookup)
        std::unordered_map<uint64_t, builtin_method> array_methods_;
        std::unordered_map<uint64_t, builtin_method> map_methods_;
        std::unordered_map<uint64_t, builtin_method> string_methods_;
        std::unordered_map<uint64_t, builtin_method> utf8_methods_;
        std::unordered_set<uint64_t> string_mutator_ids_;   // k_string_mutator_method_names (receiver lvalue binding)
        std::unordered_map<uint64_t, builtin_method> weak_ptr_methods_;
        std::unordered_map<uint64_t, builtin_method> shared_ptr_methods_;

        // Initialize built-in method registries with interned method names
        void init_builtin_methods();
        
        // Helper to access value's private storage (since interpreter is a friend)
        static strong_ptr<script_array>& get_array_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_array_storage();
        }

        static strong_ptr<script_map>& get_map_storage(const script_value& value) {
            return const_cast<script_value&>(value).get_map_storage();
        }
        
    public:
        checked_result<std::string> text_of(const script_value& v) { return value_to_string_with_method(v.deref()); }

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
        // Shadowing unit of the running code (string_symbolizer::current_unit): set around a call from the
        // callee's function or class, restored after; a worker interpreter never writes it (the symbolizer
        // is shared, and a worker's environments consult their private sink, never the versions)
        void enter_unit(unit_stamp unit) noexcept { if (!parallel_worker_ && string_symbolizer_->shadow_count() != 0) { string_symbolizer_->set_current_unit(unit); } }
        // derived: a scoped override at its depth (a class's field initializers, a coroutine's replay), else the top call
        // frame's function, else the ambient (top-level code: the run's stamp execute_parsed installed)
        unit_stamp current_unit() const noexcept {
            if (unit_override_ && call_stack_.size() <= unit_override_depth_) return *unit_override_;
            if (!call_stack_.empty() && call_stack_.back().function) return call_stack_.back().function->unit;
            return is_executing() ? run_unit_ : string_symbolizer_->current_unit();   // top-level code: the run's stamp
        }
        const unit_stamp* unit_override_ = nullptr;
        size_t unit_override_depth_ = 0;
        unit_stamp run_unit_{};   // the stamp execute_parsed installed for the run in flight (execute saves and restores it around a nested run)
        // the override for a span of code without a frame of its own; the ambient re-aligned when the span ends
        struct unit_override_scope {
            interpreter* in; const unit_stamp* prev; size_t prev_depth;
            unit_override_scope(interpreter* i, const unit_stamp* unit) : in(i), prev(i->unit_override_), prev_depth(i->unit_override_depth_) {
                in->unit_override_ = unit; in->unit_override_depth_ = in->call_stack_.size(); in->enter_unit(*unit);
            }
            ~unit_override_scope() { in->unit_override_ = prev; in->unit_override_depth_ = prev_depth; in->sync_unit(); }
        };
        struct unit_sync_scope { interpreter* in; ~unit_sync_scope() { in->sync_unit(); } };   // the frame below's unit again when this scope ends
        size_t structural_equality_depth_ = 0;   // memberwise == recursion guard (detail/structural_equality.hpp)
        // Barrier-provisioned method pins for the active region (worker instances only;
        // interpreter_backend forwards the execution_backend virtual here)
        const detail::parallel_method_pin_table* parallel_method_pins_ = nullptr;

        void set_parallel_method_pins(const detail::parallel_method_pin_table* pins) {
            parallel_method_pins_ = pins;
        }

        // Environment pooling functions (public: scoped_method_environment uses them)
        std::shared_ptr<environment> get_pooled_environment(std::shared_ptr<environment> parent);
        std::shared_ptr<environment> get_block_environment(std::shared_ptr<environment> parent);   // a { } / loop / case scope
        std::shared_ptr<environment> get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj, class_definition* access_ctx = nullptr);
        // a script class's static method called directly (the dispatcher's overload and static scope env, no callable hop): false when
        // callee is not one this interpreter enters, else the call's result in out
        bool call_static_method(const script_function& callee, const std::vector<script_value>& arguments, std::optional<checked_result<script_value>>& out);
        void release_environment(const std::shared_ptr<environment>& env, bool clear_now = true);
        void reset_environment_pool();
    };
    
} // namespace jai

#endif // __JAISCRIPT_DETAIL_INTERPRETER_HPP__
