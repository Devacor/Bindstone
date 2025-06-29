#pragma once

#include "bytecode.hpp"
#include "../core/value.hpp"
#include "../detail/interpreter.hpp"
#include <memory>
#include <unordered_map>
#include <functional>

namespace jai {
namespace jvm {

    // Forward declarations
    class compiler;
    
    // JaiScript Virtual Machine - executes bytecode
    class virtual_machine {
    public:
        virtual_machine();
        ~virtual_machine();
        
        // Non-copyable, moveable
        virtual_machine(const virtual_machine&) = delete;
        virtual_machine& operator=(const virtual_machine&) = delete;
        virtual_machine(virtual_machine&&) noexcept;
        virtual_machine& operator=(virtual_machine&&) noexcept;
        
        // Module management
        void load_module(const module* mod);
        const module* get_current_module() const;
        
        // Execution
        script_value execute();
        script_value execute_function(size_t function_index, const std::vector<script_value>& args = {});
        script_value execute_function(const std::string& function_name, const std::vector<script_value>& args = {});
        
        // Global variable management
        void set_global(const std::string& name, const script_value& value);
        script_value get_global(const std::string& name) const;
        bool has_global(const std::string& name) const;
        void set_global_environment(std::shared_ptr<environment> env);
        
        // Built-in function registration
        void register_builtin(const std::string& name, native_function func);
        void register_builtin(const std::string& name, std::function<script_value()> func);
        template<typename... Args>
        void register_builtin(const std::string& name, std::function<script_value(Args...)> func);
        
        // Type converter support (for engine integration)
        void set_type_converters(const std::unordered_map<std::string, std::function<script_value(const void*)>>* converters);
        
        // Performance optimization flags
        void set_has_custom_numeric_ops(bool value);
        void set_bounds_checking_enabled(bool enabled);
        void set_type_checking_enabled(bool enabled);
        
        // Debug support
        void set_debug_mode(bool enabled);
        std::vector<debug_info> get_call_stack() const;
        void set_breakpoint(const std::string& function_name, size_t line_number);
        void clear_breakpoints();
        
        // Error handling
        bool has_error() const;
        std::string get_error_message() const;
        void clear_error();
        
        // Statistics and profiling
        struct execution_stats {
            size_t instructions_executed = 0;
            size_t function_calls = 0;
            size_t garbage_collections = 0;
            double execution_time_ms = 0.0;
        };
        execution_stats get_stats() const;
        void reset_stats();
        
    private:
        struct implementation;
        std::unique_ptr<implementation> impl_;
        
        // Core execution loop
        script_value run();
        script_value execute_function_impl(const function* func, const std::vector<script_value>& args);
        script_value call_function_nested(size_t function_index, const std::vector<script_value>& args);
        void execute_instruction(const instruction& instr);
        
        // Instruction implementations
        void execute_arithmetic_op(opcode op);
        void execute_comparison_op(opcode op);
        void execute_logical_op(opcode op);
        void execute_bitwise_op(opcode op);
        void execute_array_op(opcode op);
        void execute_map_op(opcode op);
        void execute_object_op(opcode op);
        void execute_function_call(uint8_t arg_count);
        void execute_builtin_call(uint16_t function_index, uint8_t arg_count);
        
        // Stack management
        void push(const script_value& value);
        void push(script_value&& value);
        script_value pop();
        script_value& peek(size_t offset = 0);
        
        // Local variable management
        void set_local(uint8_t slot, const script_value& value);
        script_value get_local(uint8_t slot) const;
        
        // Error handling
        void runtime_error(const std::string& message);
        void runtime_error(const std::string& message, size_t line_number);
        
        // Type checking helpers
        bool check_numeric_type(const script_value& value) const;
        bool check_array_type(const script_value& value) const;
        bool check_map_type(const script_value& value) const;
        
        // Optimization helpers
        bool can_use_fast_arithmetic(const script_value& left, const script_value& right) const;
        script_value fast_add_int(int64_t a, int64_t b) const;
        script_value fast_multiply_int(int64_t a, int64_t b) const;
        
        friend class compiler;
    };
    
    // Template implementation for built-in function registration
    template<typename... Args>
    void virtual_machine::register_builtin(const std::string& name, std::function<script_value(Args...)> func) {
        auto wrapper = [func, name](const std::vector<script_value>& args) -> script_value {
            if (args.size() != sizeof...(Args)) {
                throw std::runtime_error("Function '" + name + "' expects " + 
                                        std::to_string(sizeof...(Args)) + " arguments, got " + 
                                        std::to_string(args.size()));
            }
            
            // For now, simple implementation without complex template metaprogramming
            if constexpr (sizeof...(Args) == 0) {
                return func();
            } else if constexpr (sizeof...(Args) == 1) {
                return func(args[0].as<std::tuple_element_t<0, std::tuple<Args...>>>());
            } else if constexpr (sizeof...(Args) == 2) {
                return func(args[0].as<std::tuple_element_t<0, std::tuple<Args...>>>(),
                           args[1].as<std::tuple_element_t<1, std::tuple<Args...>>>());
            } else {
                throw std::runtime_error("Complex template functions not yet supported");
            }
        };
        
        register_builtin(name, wrapper);
    }
    
    
    // Factory function for creating VM instances
    std::unique_ptr<virtual_machine> create_vm();
    
    // VM configuration options
    struct vm_config {
        size_t initial_stack_size = 1024;
        size_t max_stack_size = 65536;
        size_t initial_locals_size = 256;
        size_t max_call_depth = 1000;
        bool enable_profiling = false;
        bool enable_gc = true;
        optimization_level opt_level = optimization_level::STANDARD;
    };
    
    std::unique_ptr<virtual_machine> create_vm(const vm_config& config);

} // namespace jvm
} // namespace jai