#pragma once

#include "../core/execution_backend.hpp"
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
        : interpreter_(std::make_unique<interpreter>(symbolizer, global_env)) {}
    
    // Core execution
    script_value execute(const std::vector<declaration_ptr>& declarations) override {
        return interpreter_->execute(declarations);
    }
    
    void prepare_for_execution() override {
        interpreter_->prepare_for_execution();
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
    void set_type_converters(const std::unordered_map<std::string, std::function<script_value(const void*)>>* converters) override {
        interpreter_->set_type_converters(converters);
    }
    
    void set_has_custom_numeric_ops(bool value) override {
        interpreter_->set_has_custom_numeric_ops(value);
    }
    
    void set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) override {
        interpreter_->set_subscript_resolver(resolver);
    }
    
    // Exception handling
    bool is_unwinding() const override {
        return interpreter_->is_unwinding();
    }
    
    const script_exception& get_current_exception() const override {
        return interpreter_->get_current_exception();
    }
    
    std::string get_backend_name() const override {
        return "interpreter";
    }
    
    // Access to underlying interpreter if needed (for backwards compatibility)
    interpreter* get_interpreter() { return interpreter_.get(); }
    const interpreter* get_interpreter() const { return interpreter_.get(); }
    
private:
    std::unique_ptr<interpreter> interpreter_;
};

} // namespace jai