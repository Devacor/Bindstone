#include "../../include/jaiscript/jvm/vm_backend.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"
#include <iostream>
#include <chrono>

namespace jai {
namespace jvm {

    // VM backend implementation details
    struct vm_backend::implementation {
        // Core components
        std::unique_ptr<virtual_machine> vm;
        std::unique_ptr<compiler> comp;
        
        // Configuration
        compilation_options comp_options;
        vm_config vm_configuration;
        bool auto_recompile = true;
        bool debug_mode = false;
        
        // Shared state with engine
        string_symbolizer* symbolizer = nullptr;
        std::shared_ptr<environment> global_env;
        
        // Compiled modules cache
        std::unique_ptr<module> current_module;
        
        // Statistics
        compilation_stats last_comp_stats;
        virtual_machine::execution_stats last_exec_stats;
        
        // Error handling
        std::vector<std::string> compilation_errors;
        bool needs_recompilation = true;
        
        implementation() {
            vm = std::make_unique<virtual_machine>();
            comp = std::make_unique<compiler>();
        }
        
        void setup_vm_environment() {
            if (global_env) {
                vm->set_global_environment(global_env);
            }
        }
        
        void register_engine_functions() {
            // Register essential built-in functions that the engine expects
            vm->register_builtin("print", [](const std::vector<script_value>& args) -> script_value {
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) std::cout << " ";
                    std::cout << args[i].to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            vm->register_builtin("to_string", [](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("to_string() takes exactly one argument");
                }
                return script_value(args[0].to_string());
            });
            
            vm->register_builtin("size", [](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("size() takes exactly one argument");
                }
                
                const auto& arg = args[0];
                if (arg.is_array()) {
                    return script_value(static_cast<script_int>(arg.as_array().size()));
                } else if (arg.is_map()) {
                    return script_value(static_cast<script_int>(arg.as_map().size()));
                } else if (arg.is_string()) {
                    return script_value(static_cast<script_int>(arg.as_string().size()));
                } else {
                    throw runtime_error("size() can only be called on arrays, maps, or strings");
                }
            });
        }
    };
    
    vm_backend::vm_backend() : impl_(std::make_unique<implementation>()) {
        impl_->register_engine_functions();
    }
    
    vm_backend::vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env) 
        : impl_(std::make_unique<implementation>()) {
        impl_->symbolizer = symbolizer;
        impl_->global_env = global_env;
        impl_->setup_vm_environment();
        impl_->register_engine_functions();
    }
    
    vm_backend::~vm_backend() = default;
    
    vm_backend::vm_backend(vm_backend&&) noexcept = default;
    vm_backend& vm_backend::operator=(vm_backend&&) noexcept = default;
    
    script_value vm_backend::execute(const std::vector<declaration_ptr>& declarations) {
        try {
            // Compile if needed
            compile_if_needed(declarations);
            
            if (!impl_->current_module) {
                handle_runtime_error("No compiled module available");
                return script_value();
            }
            
            // Load module into VM (just pass the pointer, keep ownership)
            impl_->vm->load_module(impl_->current_module.get());
            
            // Execute main function
            script_value result = impl_->vm->execute();
            
            // Force recompilation next time since we have new code each execute()
            impl_->needs_recompilation = true;
            
            // Update statistics
            impl_->last_exec_stats = impl_->vm->get_stats();
            
            // Check for VM errors
            if (impl_->vm->has_error()) {
                handle_runtime_error("VM execution error: " + impl_->vm->get_error_message());
                impl_->vm->clear_error();
                return script_value();
            }
            
            return result;
            
        } catch (const std::exception& e) {
            handle_runtime_error("Execution failed: " + std::string(e.what()));
            return script_value();
        }
    }
    
    void vm_backend::prepare_for_execution() {
        // Clear VM state
        if (impl_->vm) {
            impl_->vm->clear_error();
            impl_->vm->reset_stats();
        }
        
        // Setup environment
        impl_->setup_vm_environment();
    }
    
    script_value vm_backend::get_variable(const std::string& name) const {
        if (impl_->global_env) {
            try {
                return impl_->global_env->get(name);
            } catch (...) {
                return script_value();
            }
        }
        return script_value();
    }
    
    bool vm_backend::has_variable(const std::string& name) const {
        if (impl_->global_env) {
            return impl_->global_env->contains(name);
        }
        return false;
    }
    
    void vm_backend::push_scope() {
        // VM doesn't support runtime scope pushing in the same way as interpreter
        // This would need to be implemented differently for VM
        // For now, just log a warning
        if (impl_->debug_mode) {
            std::cerr << "Warning: push_scope() not fully implemented for VM backend" << std::endl;
        }
    }
    
    void vm_backend::pop_scope() {
        // VM doesn't support runtime scope popping in the same way as interpreter
        // This would need to be implemented differently for VM
        // For now, just log a warning
        if (impl_->debug_mode) {
            std::cerr << "Warning: pop_scope() not fully implemented for VM backend" << std::endl;
        }
    }
    
    void vm_backend::define_variable(const std::string& name, const script_value& value) {
        if (impl_->global_env) {
            impl_->global_env->define(name, value);
        }
        
        // Also set in VM
        if (impl_->vm) {
            impl_->vm->set_global(name, value);
        }
    }
    
    void vm_backend::set_type_converters(const std::unordered_map<std::string, std::function<script_value(const void*)>>* converters) {
        if (impl_->vm) {
            impl_->vm->set_type_converters(converters);
        }
    }
    
    void vm_backend::set_has_custom_numeric_ops(bool value) {
        if (impl_->vm) {
            impl_->vm->set_has_custom_numeric_ops(value);
        }
    }
    
    void vm_backend::set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) {
        // Subscript resolver support would need to be implemented in VM
        // For now, just log a warning
        if (impl_->debug_mode) {
            std::cerr << "Warning: set_subscript_resolver() not fully implemented for VM backend" << std::endl;
        }
    }
    
    std::string vm_backend::get_backend_name() const {
        return "JaiScript VM (Bytecode)";
    }
    
    // Exception handling (stub implementation - VM exception handling not yet implemented)
    bool vm_backend::is_unwinding() const {
        // For now, VM backend doesn't support exception handling
        return false;
    }
    
    const script_exception& vm_backend::get_current_exception() const {
        // For now, VM backend doesn't support exception handling
        static script_exception dummy_exception("VM backend exception handling not implemented");
        return dummy_exception;
    }
    
    // VM-specific configuration
    void vm_backend::set_optimization_level(optimization_level level) {
        impl_->comp_options.opt_level = level;
        if (impl_->comp) {
            impl_->comp->set_optimization_level(level);
        }
        impl_->needs_recompilation = true;
    }
    
    void vm_backend::set_debug_mode(bool enabled) {
        impl_->debug_mode = enabled;
        impl_->comp_options.include_debug_info = enabled;
        
        if (impl_->vm) {
            impl_->vm->set_debug_mode(enabled);
        }
        if (impl_->comp) {
            impl_->comp->set_debug_info_enabled(enabled);
        }
        
        impl_->needs_recompilation = true;
    }
    
    void vm_backend::set_compilation_options(const compilation_options& options) {
        impl_->comp_options = options;
        impl_->needs_recompilation = true;
    }
    
    void vm_backend::set_vm_config(const vm_config& config) {
        impl_->vm_configuration = config;
        // VM config changes would require recreating the VM
        // For now, just note that recompilation may be needed
        impl_->needs_recompilation = true;
    }
    
    // Performance and debugging
    compilation_stats vm_backend::get_compilation_stats() const {
        return impl_->last_comp_stats;
    }
    
    virtual_machine::execution_stats vm_backend::get_execution_stats() const {
        return impl_->last_exec_stats;
    }
    
    void vm_backend::reset_stats() {
        impl_->last_comp_stats = compilation_stats{};
        impl_->last_exec_stats = virtual_machine::execution_stats{};
        
        if (impl_->vm) {
            impl_->vm->reset_stats();
        }
    }
    
    // Built-in function registration
    void vm_backend::register_builtin_function(const std::string& name, std::function<script_value(const std::vector<script_value>&)> func) {
        if (impl_->vm) {
            impl_->vm->register_builtin(name, func);
        }
    }
    
    // Error handling
    bool vm_backend::has_compilation_errors() const {
        return !impl_->compilation_errors.empty();
    }
    
    std::vector<std::string> vm_backend::get_compilation_errors() const {
        return impl_->compilation_errors;
    }
    
    void vm_backend::clear_errors() {
        impl_->compilation_errors.clear();
        
        if (impl_->vm) {
            impl_->vm->clear_error();
        }
        if (impl_->comp) {
            impl_->comp->clear_errors();
        }
    }
    
    // Access to underlying VM
    virtual_machine* vm_backend::get_vm() const {
        return impl_->vm.get();
    }
    
    // Compilation control
    void vm_backend::set_auto_recompile(bool enabled) {
        impl_->auto_recompile = enabled;
    }
    
    bool vm_backend::needs_recompilation() const {
        return impl_->needs_recompilation;
    }
    
    void vm_backend::force_recompilation() {
        impl_->needs_recompilation = true;
    }
    
    // Private implementation methods
    void vm_backend::compile_if_needed(const std::vector<declaration_ptr>& declarations) {
        if (!impl_->needs_recompilation && impl_->current_module) {
            return; // Already compiled and up to date
        }
        
        try {
            // Set up compiler with current options
            impl_->comp = std::make_unique<compiler>(impl_->comp_options);
            
            // Compile the declarations
            impl_->current_module = impl_->comp->compile(declarations);
            
            if (!impl_->current_module) {
                handle_compilation_error("Compilation failed");
                return;
            }
            
            // Check for compilation errors
            if (impl_->comp->has_errors()) {
                auto errors = impl_->comp->get_errors();
                for (const auto& error : errors) {
                    handle_compilation_error(error);
                }
                impl_->current_module = nullptr;
                return;
            }
            
            // Update statistics
            impl_->last_comp_stats = impl_->comp->get_stats();
            impl_->needs_recompilation = false;
            
            if (impl_->debug_mode) {
                std::cout << "VM Backend: Successfully compiled " 
                         << impl_->last_comp_stats.functions_compiled << " functions, "
                         << impl_->last_comp_stats.instructions_generated << " instructions"
                         << std::endl;
            }
            
        } catch (const std::exception& e) {
            handle_compilation_error("Compilation exception: " + std::string(e.what()));
            impl_->current_module = nullptr;
        }
    }
    
    void vm_backend::handle_compilation_error(const std::string& error) {
        impl_->compilation_errors.push_back(error);
        
        if (impl_->debug_mode) {
            std::cerr << "VM Backend Compilation Error: " << error << std::endl;
        }
    }
    
    void vm_backend::handle_runtime_error(const std::string& error) {
        // For runtime errors, we could potentially fall back to interpreter
        // For now, just log the error
        
        if (impl_->debug_mode) {
            std::cerr << "VM Backend Runtime Error: " << error << std::endl;
        }
        
        // Could implement interpreter fallback here
        // if (should_use_interpreter_fallback()) {
        //     // Switch to interpreter backend temporarily
        // }
    }
    
    // Factory functions
    std::unique_ptr<vm_backend> create_vm_backend() {
        return std::make_unique<vm_backend>();
    }
    
    std::unique_ptr<vm_backend> create_vm_backend(string_symbolizer* symbolizer, 
                                                 std::shared_ptr<environment> global_env) {
        return std::make_unique<vm_backend>(symbolizer, global_env);
    }
    
    std::unique_ptr<vm_backend> create_vm_backend(const compilation_options& comp_options,
                                                 const vm_config& vm_config) {
        auto backend = std::make_unique<vm_backend>();
        backend->set_compilation_options(comp_options);
        backend->set_vm_config(vm_config);
        return backend;
    }
    
    std::unique_ptr<vm_backend> create_vm_backend(const vm_backend_config& config) {
        auto backend = std::make_unique<vm_backend>();
        backend->set_compilation_options(config.compilation);
        backend->set_vm_config(config.vm);
        backend->set_auto_recompile(config.auto_recompile);
        return backend;
    }
    
    // Hybrid backend implementation
    struct hybrid_backend::hybrid_implementation {
        string_symbolizer* symbolizer;
        std::shared_ptr<environment> global_env;
        
        std::unique_ptr<vm_backend> vm_backend_impl;
        std::unique_ptr<execution_backend> interpreter_backend_impl;
        
        // Configuration
        size_t vm_threshold = 100; // Switch to VM for scripts with >100 instructions
        bool debug_uses_interpreter = true;
        std::string forced_backend = "auto"; // "vm", "interpreter", or "auto"
        
        // Statistics
        hybrid_stats stats;
        
        hybrid_implementation(string_symbolizer* sym, std::shared_ptr<environment> env)
            : symbolizer(sym), global_env(env) {
            vm_backend_impl = create_vm_backend(symbolizer, global_env);
            // Would need to create interpreter_backend here
            // interpreter_backend_impl = std::make_unique<interpreter_backend>(symbolizer, global_env);
        }
    };
    
    hybrid_backend::hybrid_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env)
        : impl_(std::make_unique<hybrid_implementation>(symbolizer, global_env)) {
    }
    
    hybrid_backend::~hybrid_backend() = default;
    
    script_value hybrid_backend::execute(const std::vector<declaration_ptr>& declarations) {
        // Decide which backend to use
        bool use_vm = should_use_vm(declarations);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        script_value result;
        
        if (use_vm) {
            result = impl_->vm_backend_impl->execute(declarations);
            impl_->stats.vm_executions++;
        } else {
            // Would use interpreter backend here
            // result = impl_->interpreter_backend_impl->execute(declarations);
            impl_->stats.interpreter_executions++;
            
            // For now, fall back to VM
            result = impl_->vm_backend_impl->execute(declarations);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (use_vm) {
            impl_->stats.vm_time_ms += duration.count() / 1000.0;
        } else {
            impl_->stats.interpreter_time_ms += duration.count() / 1000.0;
        }
        
        return result;
    }
    
    void hybrid_backend::prepare_for_execution() {
        impl_->vm_backend_impl->prepare_for_execution();
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->prepare_for_execution();
        // }
    }
    
    script_value hybrid_backend::get_variable(const std::string& name) const {
        return impl_->vm_backend_impl->get_variable(name);
    }
    
    bool hybrid_backend::has_variable(const std::string& name) const {
        return impl_->vm_backend_impl->has_variable(name);
    }
    
    void hybrid_backend::push_scope() {
        impl_->vm_backend_impl->push_scope();
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->push_scope();
        // }
    }
    
    void hybrid_backend::pop_scope() {
        impl_->vm_backend_impl->pop_scope();
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->pop_scope();
        // }
    }
    
    void hybrid_backend::define_variable(const std::string& name, const script_value& value) {
        impl_->vm_backend_impl->define_variable(name, value);
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->define_variable(name, value);
        // }
    }
    
    void hybrid_backend::set_type_converters(const std::unordered_map<std::string, std::function<script_value(const void*)>>* converters) {
        impl_->vm_backend_impl->set_type_converters(converters);
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->set_type_converters(converters);
        // }
    }
    
    void hybrid_backend::set_has_custom_numeric_ops(bool value) {
        impl_->vm_backend_impl->set_has_custom_numeric_ops(value);
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->set_has_custom_numeric_ops(value);
        // }
    }
    
    void hybrid_backend::set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) {
        impl_->vm_backend_impl->set_subscript_resolver(resolver);
        // if (impl_->interpreter_backend_impl) {
        //     impl_->interpreter_backend_impl->set_subscript_resolver(resolver);
        // }
    }
    
    std::string hybrid_backend::get_backend_name() const {
        return "JaiScript Hybrid (VM + Interpreter)";
    }
    
    // Hybrid-specific configuration
    void hybrid_backend::set_vm_threshold(size_t instruction_count) {
        impl_->vm_threshold = instruction_count;
    }
    
    void hybrid_backend::set_debug_mode_uses_interpreter(bool use_interpreter) {
        impl_->debug_uses_interpreter = use_interpreter;
    }
    
    void hybrid_backend::force_backend(const std::string& backend_name) {
        impl_->forced_backend = backend_name;
    }
    
    hybrid_backend::hybrid_stats hybrid_backend::get_stats() const {
        return impl_->stats;
    }
    
    bool hybrid_backend::should_use_vm(const std::vector<declaration_ptr>& declarations) const {
        if (impl_->forced_backend == "vm") return true;
        if (impl_->forced_backend == "interpreter") return false;
        
        // Auto selection logic
        if (impl_->debug_uses_interpreter) {
            // Could check for debug mode here
            // For now, default to VM
        }
        
        // Simple heuristic: use VM for larger scripts
        return declarations.size() > 5; // Arbitrary threshold
    }

} // namespace jvm
} // namespace jai