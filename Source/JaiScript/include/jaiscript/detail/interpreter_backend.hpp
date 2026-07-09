#pragma once

#include <jaiscript/core/execution_backend.hpp>
#include "interpreter.hpp"
#include <memory>

namespace jai {

/**
 * Wrapper that adapts the existing interpreter to the execution_backend interface.
 * This allows us to keep all existing interpreter code unchanged.
 */
class interpreter_backend : public execution_backend {
public:
    interpreter_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env)
        : interpreter_(std::make_shared<interpreter>(symbolizer, global_env)) {}
    
    // Core execution
    script_value execute(const std::vector<declaration_ptr>& declarations) override {
        return interpreter_->execute(declarations);
    }
    
    void prepare_for_execution() override {
        interpreter_->prepare_for_execution();
    }

    checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args) override {
        return interpreter_->execute_callable(payload, args);
    }
    
    // Variable access
    script_value get_variable(const std::string& name) const override {
        return interpreter_->get_variable(name);
    }
    
    bool has_variable(const std::string& name) const override {
        return interpreter_->has_variable(name);
    }
    
    // Scope management
    void push_scope() override {
        interpreter_->push_scope();
    }
    
    void pop_scope() override {
        interpreter_->pop_scope();
    }
    
    void define_variable(const std::string& name, const script_value& value) override {
        interpreter_->define_variable(name, value);
    }
    
    // Configuration
    void set_execution_budget(std::chrono::nanoseconds budget) override {
        interpreter_->execution_budget(budget);
    }

    void set_has_custom_numeric_ops(bool value) override {
        interpreter_->set_has_custom_numeric_ops(value);
    }

    void set_has_custom_binary_ops(bool value) override {
        interpreter_->set_has_custom_binary_ops(value);
    }

    void set_operator_table(const detail::engine_operator_table* table) override {
        interpreter_->set_operator_table(table);
    }
    
    void set_subscript_resolver(std::function<checked_result<script_value>(const std::vector<script_value>&)> resolver) override {
        interpreter_->set_subscript_resolver(resolver);
    }
    
    void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override {
        interpreter_->set_class_lookup_callback(callback);
    }
    
    void set_engine_reference(engine* engine_ref) override {
        interpreter_->set_engine_reference(engine_ref);
    }

    void set_debug_controller(debug::controller* controller) override {
        interpreter_->set_debug_controller(controller);
    }

    std::shared_ptr<environment> get_current_environment() const override {
        return interpreter_->get_current_environment();
    }

    std::vector<std::pair<std::string, script_value>> get_current_frame_locals() const override {
        return interpreter_->get_current_frame_locals();
    }

    // Exception handling
    bool is_unwinding() const override {
        return interpreter_->is_unwinding();
    }

    bool is_executing() const override {
        return interpreter_->is_executing();
    }

    const script_exception& get_current_exception() const override {
        return interpreter_->get_current_exception();
    }

    std::vector<stack_frame> last_stack_trace() const override {
        return interpreter_->last_stack_trace();
    }

    std::string format_stack_trace() const override {
        return interpreter_->format_stack_trace();
    }

    void push_external_call_scope() override {
        interpreter_->push_external_call_scope();
    }

    void pop_external_call_scope() override {
        interpreter_->pop_external_call_scope();
    }

    checked_result<script_value> resume_coroutine(coroutine_handle& handle) override {
        return interpreter_->resume_coroutine(handle);
    }

    std::string get_backend_name() const override {
        return "interpreter";
    }
    
    // Access to underlying interpreter if needed (for backwards compatibility)
    interpreter* get_interpreter() { return interpreter_.get(); }
    const interpreter* get_interpreter() const { return interpreter_.get(); }
    
private:
    std::shared_ptr<interpreter> interpreter_;
};

} // namespace jai