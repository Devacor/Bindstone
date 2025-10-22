#pragma once

#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/core/function_binder.hpp>
#include "virtual_machine.hpp"
#include "compiler.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace jai {
namespace jvm {

    // VM backend implementation of execution_backend interface
    // Provides bytecode compilation and execution for JaiScript
    class vm_backend : public execution_backend {
    public:
        vm_backend();
        vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env);
        ~vm_backend() override;
        
        // Non-copyable, moveable
        vm_backend(const vm_backend&) = delete;
        vm_backend& operator=(const vm_backend&) = delete;
        vm_backend(vm_backend&&) noexcept;
        vm_backend& operator=(vm_backend&&) noexcept;
        
        // execution_backend interface implementation
        script_value execute(const std::vector<declaration_ptr>& declarations) override;
        void prepare_for_execution() override;
        
        script_value get_variable(const std::string& name) const override;
        bool has_variable(const std::string& name) const override;
        
        void push_scope() override;
        void pop_scope() override;
        void define_variable(const std::string& name, const script_value& value) override;
        
        void set_has_custom_numeric_ops(bool value) override;
        void set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) override;
        void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override;
        void set_engine_reference(std::weak_ptr<engine> engine_ref) override;
        
        // Exception handling
        bool is_unwinding() const override;
        const script_exception& get_current_exception() const override;
        
        std::string get_backend_name() const override;
        
        // VM-specific configuration
        void set_optimization_level(optimization_level level);
        void set_debug_mode(bool enabled);
        void set_compilation_options(const compilation_options& options);
        void set_vm_config(const vm_config& config);
        
        // Performance and debugging
        compilation_stats get_compilation_stats() const;
        virtual_machine::execution_stats get_execution_stats() const;
        void reset_stats();
        
        // Built-in function registration (for engine integration)
        void register_builtin_function(const std::string& name, std::function<script_value(const std::vector<script_value>&)> func);
        template<typename Func>
        void register_builtin_function(const std::string& name, Func&& func);
        
        // Error handling
        bool has_compilation_errors() const;
        std::vector<std::string> get_compilation_errors() const;
        void clear_errors();
        
        // Access to underlying VM (for advanced usage)
        virtual_machine* get_vm() const;
        
        // Compilation control
        void set_auto_recompile(bool enabled);
        bool needs_recompilation() const;
        void force_recompilation();
        
    private:
        struct implementation;
        std::unique_ptr<implementation> impl_;
        
        // Compilation and execution workflow
        void compile_if_needed(const std::vector<declaration_ptr>& declarations);
        void setup_vm_environment();
        void register_engine_functions();
        
        // Error handling helpers
        void handle_compilation_error(const std::string& error);
        void handle_runtime_error(const std::string& error);
        
        // Performance optimization
        void optimize_for_repeated_execution();
        bool should_use_interpreter_fallback() const;
    };
    
    // Template implementation for built-in function registration
    template<typename Func>
    void vm_backend::register_builtin_function(const std::string& name, Func&& func) {
        // Convert any callable to the standard signature
        auto wrapper = [func = std::forward<Func>(func), name](const std::vector<script_value>& args) -> script_value {
            if constexpr (std::is_invocable_v<Func, const std::vector<script_value>&>) {
                // Function already takes vector<script_value>
                return func(args);
            } else {
                // Function has specific parameter types - need conversion
                using traits = detail::function_traits<std::decay_t<Func>>;
                using args_tuple = typename traits::argument_types;
                constexpr size_t arity = traits::arity;
                
                if (args.size() != arity) {
                    throw runtime_error("Function '" + name + "' expects " + 
                                      std::to_string(arity) + " arguments, got " + 
                                      std::to_string(args.size()));
                }
                
                // Convert and call
                return detail::call_with_converted_args<Func, args_tuple>(func, args, std::make_index_sequence<arity>{});
            }
        };
        
        register_builtin_function(name, wrapper);
    }
    
    // Factory functions for creating VM backends
    std::unique_ptr<vm_backend> create_vm_backend();
    std::unique_ptr<vm_backend> create_vm_backend(string_symbolizer* symbolizer, 
                                                 std::shared_ptr<environment> global_env);
    std::unique_ptr<vm_backend> create_vm_backend(const compilation_options& comp_options,
                                                 const vm_config& vm_config);
    
    // VM backend configuration
    struct vm_backend_config {
        compilation_options compilation;
        vm_config vm;
        bool auto_recompile = true;
        bool fallback_to_interpreter = false;
        bool enable_profiling = false;
    };
    
    std::unique_ptr<vm_backend> create_vm_backend(const vm_backend_config& config);
    
    // Hybrid execution mode - combines interpreter and VM
    class hybrid_backend : public execution_backend {
    public:
        hybrid_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env);
        ~hybrid_backend() override;
        
        // execution_backend interface
        script_value execute(const std::vector<declaration_ptr>& declarations) override;
        void prepare_for_execution() override;
        script_value get_variable(const std::string& name) const override;
        bool has_variable(const std::string& name) const override;
        void push_scope() override;
        void pop_scope() override;
        void define_variable(const std::string& name, const script_value& value) override;
        void set_has_custom_numeric_ops(bool value) override;
        void set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) override;
        void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override;
        void set_engine_reference(std::weak_ptr<engine> engine_ref) override;
        bool is_unwinding() const override;
        const script_exception& get_current_exception() const override;
        std::string get_backend_name() const override;
        
        // Hybrid-specific configuration
        void set_vm_threshold(size_t instruction_count);
        void set_debug_mode_uses_interpreter(bool use_interpreter);
        void force_backend(const std::string& backend_name); // "vm", "interpreter", or "auto"
        
        // Statistics
        struct hybrid_stats {
            size_t vm_executions = 0;
            size_t interpreter_executions = 0;
            double vm_time_ms = 0.0;
            double interpreter_time_ms = 0.0;
            size_t automatic_switches = 0;
        };
        hybrid_stats get_stats() const;
        
    private:
        struct hybrid_implementation;
        std::unique_ptr<hybrid_implementation> impl_;
        
        bool should_use_vm(const std::vector<declaration_ptr>& declarations) const;
        void switch_backend_if_needed();
    };

} // namespace jvm
} // namespace jai