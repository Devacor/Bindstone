#pragma once

#ifndef __JAISCRIPT_VM_VM_BACKEND_HPP__
#define __JAISCRIPT_VM_VM_BACKEND_HPP__

#include "chunk.hpp"
#include "vm_compiler.hpp"
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace jai {
    class script_class_definition;
}

namespace jai::vm {

    class vm_backend : public execution_backend {
    public:
        vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env);
        ~vm_backend() override = default;

        script_value execute(const std::vector<declaration_ptr>& declarations) override;
        void prepare_for_execution() override;
        checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args) override;

        script_value get_variable(const std::string& name) const override;
        bool has_variable(const std::string& name) const override;

        void push_scope() override;
        void pop_scope() override;
        void define_variable(const std::string& name, const script_value& value) override;

        void set_execution_budget(std::chrono::nanoseconds budget) override;
        void set_has_custom_numeric_ops(bool value) override { has_custom_numeric_ops_ = value; }
        void set_subscript_resolver(std::function<checked_result<script_value>(const std::vector<script_value>&)> resolver) override {
            subscript_resolver_ = std::move(resolver);
        }
        void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override {
            class_lookup_ = std::move(callback);
        }
        void set_engine_reference(engine* engine_ref) override;

        bool is_unwinding() const override { return is_unwinding_; }
        const script_exception& get_current_exception() const override { return current_exception_.value(); }

        std::vector<stack_frame> last_stack_trace() const override { return captured_trace_; }
        std::string format_stack_trace() const override;

        void push_external_call_scope() override;
        void pop_external_call_scope() override;

        checked_result<script_value> resume_coroutine(coroutine_handle& handle) override;

        std::string get_backend_name() const override { return "vm"; }

        // Compiles without executing (disassembler/tests)
        std::shared_ptr<chunk> compile_only(const std::vector<declaration_ptr>& declarations);

    private:
        struct frame {
            chunk* code = nullptr;
            std::shared_ptr<chunk> pin;
            size_t ip = 0;
            call_frame* locals = nullptr;
            std::shared_ptr<environment> entry_env;
            size_t stack_base = 0;
            bool top_level = false;
        };

        struct frame_guard;

        string_symbolizer* symbolizer_;
        std::shared_ptr<environment> environment_;
        engine* engine_ = nullptr;

        vm_compiler compiler_;
        struct chunk_cache_entry {
            std::shared_ptr<chunk> compiled;
            std::shared_ptr<block_stmt> pin;
        };
        std::unordered_map<const block_stmt*, chunk_cache_entry> chunk_cache_;

        std::vector<script_value> stack_;
        std::vector<frame*> frames_;

        std::optional<script_value> return_value_;
        bool has_return_value_ = false;
        std::optional<script_value> implicit_result_;

        std::optional<script_exception> current_exception_;
        std::optional<script_value> active_exception_value_;
        bool is_unwinding_ = false;
        bool trace_captured_ = false;
        std::vector<stack_frame> captured_trace_;
        uint64_t current_catch_var_id_ = 0;

        // One record per active try/catch region, innermost last (dual error model:
        // catches checked_result failures AND throw-flag unwinding, like the interpreter)
        struct try_record {
            frame* owner = nullptr;
            size_t handler_ip = 0;
            uint64_t catch_var = 0;
            std::optional<script_exception> saved_exception;
            std::optional<script_value> saved_exception_value;
            uint64_t saved_catch_var_id = 0;
            bool saved_unwinding = false;
            bool in_catch = false;
            size_t stack_size = 0;
            size_t iter_size = 0;
            std::shared_ptr<environment> entry_env;
        };
        std::vector<try_record> try_records_;

        struct iter_state {
            std::optional<script_value> container;
            std::optional<script_value> pair_ctor;
            size_t index = 0;
            script_map::iterator map_it{};
            bool is_map = false;
            std::shared_ptr<coroutine_handle> coroutine;   // set = coroutine-driven range-for
        };
        std::vector<iter_state> iter_states_;

        // Suspended-fiber state stored opaquely on a coroutine_handle (frames+ip+stack model,
        // not the interpreter's AST continuation replay). Defined in vm_backend.cpp.
        struct vm_coroutine_state;
        coroutine_handle* active_coroutine_ = nullptr;
        bool yielding_ = false;

        std::vector<std::pair<uint64_t, environment*>> current_arg_metadata_;
        std::vector<std::vector<std::pair<uint64_t, environment*>>> external_metadata_stack_;

        bool has_custom_numeric_ops_ = false;
        std::function<checked_result<script_value>(const std::vector<script_value>&)> subscript_resolver_;
        std::function<std::shared_ptr<class_definition>(const std::string&)> class_lookup_;

        std::chrono::nanoseconds execution_budget_{std::chrono::seconds(1)};
        std::chrono::steady_clock::time_point execution_deadline_{};
        uint32_t budget_tick_ = 0;
        bool budget_active_ = false;
        int current_call_depth_ = 0;

        uint64_t op_plus_id_ = 0, op_minus_id_ = 0, op_star_id_ = 0, op_slash_id_ = 0, op_percent_id_ = 0;
        uint64_t op_less_id_ = 0, op_less_equal_id_ = 0, op_greater_id_ = 0, op_greater_equal_id_ = 0;
        uint64_t op_equal_equal_id_ = 0, op_bang_equal_id_ = 0, op_spaceship_id_ = 0;
        uint64_t op_ampersand_id_ = 0, op_pipe_id_ = 0, op_caret_id_ = 0, op_left_shift_id_ = 0, op_right_shift_id_ = 0;
        uint64_t this_id_ = 0;
        uint64_t subscript_op_id_ = 0;
        uint64_t assign_operator_id_ = 0;
        uint64_t to_string_id_ = 0;
        uint64_t to_bool_id_ = 0;
        uint64_t eq_method_id_ = 0;
        uint64_t cpp_object_field_id_ = 0;
        uint64_t same_as_id_ = 0;
        uint64_t coroutine_handle_type_id_ = 0;
        uint64_t resume_id_ = 0;
        uint64_t done_id_ = 0;
        uint64_t class_definition_type_id_ = 0;
        uint64_t hot_reload_migrate_id_ = 0;
        uint64_t pair_id_ = 0;

        builtin_method_registries builtins_;

        struct expr_chunk_cache_entry {
            std::shared_ptr<chunk> compiled;
            expression_ptr pin;
        };
        std::unordered_map<const expression*, expr_chunk_cache_entry> expr_chunk_cache_;

        void arm_execution_deadline();
        bool execution_budget_exhausted();

        std::shared_ptr<chunk> chunk_for_body(std::string_view name,
                                              const std::vector<parameter>& params,
                                              const std::shared_ptr<block_stmt>& body,
                                              size_t local_count);

        checked_result<void> run(frame& f);
        checked_result<script_value> call_script_function(const script_defined_function& function,
                                                          const std::vector<script_value>& args);

        void capture_stack_trace();

        script_value make_null() const;
        bool is_truthy(const script_value& value);
        bool object_to_bool_via_method(const script_value& value);
        std::string value_to_string_with_method(const script_value& val);
        std::optional<bool> object_equality_via_method(const script_value& left, const script_value& right);
        std::optional<bool> object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);
        std::optional<script_value> object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);

        struct member_target {
            std::shared_ptr<class_instance> instance;
            std::shared_ptr<class_definition> engine_def;
            class_definition* class_def = nullptr;
            explicit operator bool() const { return instance != nullptr || class_def != nullptr; }
            script_value method(uint64_t id) const;
            bool has_field(uint64_t id) const;
            const script_value& get_field(uint64_t id) const;
            const std::string& class_name() const;
        };
        member_target resolve_member_target(const script_value& objectValue) const;

        script_value* resolve_local_or_env(frame& f, uint32_t slot, uint64_t symbol_id);
        checked_result<void> define_decl_value(frame& f, uint64_t name_id, size_t slot_index, script_value value);

        checked_result<script_value> enforce_type_compatibility(script_value value, type_info_ptr target_type);
        checked_result<script_value> try_convert_for_parameter(const script_value& arg, type_info_ptr target_type);
        checked_result<script_value> evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in);

        checked_result<script_value> binary_general(token_type op, const script_value& left, const script_value& right);
        bool binary_fast_shape(token_type op, uint32_t shape, const script_value& left, const script_value& right,
                               std::optional<checked_result<script_value>>& out);
        checked_result<script_value> handle_binary_op(token_type op, const script_value& left, const script_value& right);
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

        builtin_method_context builtin_ctx() const { return {engine_, symbolizer_}; }

        void raise_script_exception(const std::string& message, const source_location& location);

        // Runs a standalone expression chunk (node-keyed cache) in the given environment
        checked_result<script_value> eval_expression(const expression_ptr& expr, const std::shared_ptr<environment>& env);

        checked_result<script_value> execute_method_ast(const std::shared_ptr<function_decl>& ast,
                                                        std::shared_ptr<environment> method_env,
                                                        const std::vector<script_value>& args);

        checked_result<script_value> construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                        std::shared_ptr<environment> definition_env,
                                                        const std::vector<script_value>& args);
        checked_result<script_value> construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                const std::vector<script_value>& args);
        void evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                         std::shared_ptr<script_class_definition> class_def,
                                         std::shared_ptr<environment> init_env,
                                         bool skip_parent_recursion = false);

        // Mirrors visit_member_expr's non-static path; unwinding cases leave out null
        checked_result<void> member_access_value(const script_value& raw_object, member_expr* expr, script_value& out);
        checked_result<void> static_member_value(member_expr* expr, script_value& out);
        checked_result<void> assign_member(const script_value& object_value, member_expr* member, const script_value& value);
        checked_result<void> invoke_callee(const script_value& callee, std::vector<script_value>& arguments, const call_site& site);

        checked_result<void> exec_class_decl_node(class_decl* decl);
        checked_result<void> exec_namespace_decl_node(namespace_decl* decl);

        checked_result<void> exec_load(frame& f, const vm_instruction& ins);
        checked_result<void> exec_store(frame& f, const vm_instruction& ins);
        checked_result<void> exec_compound_store(frame& f, const vm_instruction& ins);
        checked_result<void> exec_incdec(frame& f, const vm_instruction& ins);
        checked_result<void> exec_decl_var(frame& f, const vm_instruction& ins);
        checked_result<void> exec_decl_ref_ident(frame& f, const vm_instruction& ins);
        checked_result<void> exec_decl_ref_value(frame& f, const vm_instruction& ins);
        checked_result<void> exec_binary(frame& f, const vm_instruction& ins);
        checked_result<void> exec_index(frame& f, const vm_instruction& ins);
        checked_result<void> exec_index_assign(frame& f, const vm_instruction& ins);
        checked_result<void> exec_index_compound(frame& f, const vm_instruction& ins);
        checked_result<void> exec_unary(frame& f, const vm_instruction& ins);
        checked_result<void> exec_call(frame& f, const vm_instruction& ins);
        checked_result<void> exec_func_decl(frame& f, const vm_instruction& ins);
        checked_result<void> exec_closure(frame& f, const vm_instruction& ins);
        checked_result<void> exec_destructure(frame& f, const vm_instruction& ins);
        checked_result<void> exec_extended(frame& f, const vm_instruction& ins);
        checked_result<void> exec_throw(frame& f, const vm_instruction& ins);
        checked_result<void> exec_try_push(frame& f, const vm_instruction& ins);
        checked_result<void> exec_try_pop(frame& f, const vm_instruction& ins);
        checked_result<void> exec_catch_end(frame& f, const vm_instruction& ins);
        checked_result<void> exec_case_eq(frame& f, const vm_instruction& ins);
        checked_result<void> exec_iter_init(frame& f, const vm_instruction& ins);
        checked_result<void> exec_iter_next(frame& f, const vm_instruction& ins);
        checked_result<void> exec_iter_pop(frame& f, const vm_instruction& ins);
        void exec_yield(frame& f, const vm_instruction& ins);
        checked_result<void> exec_include(frame& f, const vm_instruction& ins);
        checked_result<void> exec_import(frame& f, const vm_instruction& ins);

        vm_coroutine_state& coroutine_fiber_state(coroutine_handle& handle);
        // Runs (or continues) the fiber; returns the yield value, the final return value on the
        // completing resume, or an error (handle marked failed) — never a stale prior yield.
        checked_result<script_value> run_fiber(coroutine_handle& handle, vm_coroutine_state& state);
        // Walks try records innermost-out: false = no handler in this frame, propagate
        bool unwind_to_handler(frame& f, const error_propagator* failure);
        bool handle_op_error(frame& f, const checked_result<void>& result);
        bool handle_throw_unwind(frame& f);
        checked_result<void> budget_exceeded_error() const;
        checked_result<void> exec_this(frame& f, const vm_instruction& ins);
        checked_result<void> exec_super(frame& f, const vm_instruction& ins);
        checked_result<void> exec_from_this(frame& f, const vm_instruction& ins);
        checked_result<void> exec_get_member(frame& f, const vm_instruction& ins);
        checked_result<void> exec_get_static(frame& f, const vm_instruction& ins);
        checked_result<void> exec_set_member(frame& f, const vm_instruction& ins);
        checked_result<void> exec_set_static(frame& f, const vm_instruction& ins);
        checked_result<void> exec_member_compound(frame& f, const vm_instruction& ins);
        checked_result<void> exec_call_method(frame& f, const vm_instruction& ins);
        checked_result<void> exec_new(frame& f, const vm_instruction& ins);
        checked_result<void> exec_class_decl(frame& f, const vm_instruction& ins);
        checked_result<void> exec_namespace_decl(frame& f, const vm_instruction& ins);
        checked_result<void> exec_enum_decl(frame& f, const vm_instruction& ins);
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_VM_BACKEND_HPP__
