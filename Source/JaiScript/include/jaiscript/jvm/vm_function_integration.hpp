#pragma once

#include "jaiscript/core/engine.hpp"
#include "jaiscript/jvm/vm_function.hpp"
#include "jaiscript/detail/ast.hpp"

namespace jai {
namespace jvm {

// Performance monitoring for VM functions
class vm_function_profiler {
public:
    struct function_profile {
        std::string name;
        uint64_t total_calls = 0;
        uint64_t total_cycles = 0;
        uint64_t bytecode_calls = 0;
        uint64_t interpreter_fallbacks = 0;
        double average_cycles_per_call = 0.0;
        bool is_hot_function = false;
    };
    
    // Start/stop profiling
    void start_profiling();
    void stop_profiling();
    
    // Record function execution
    void record_function_call(
        const std::string& name,
        uint64_t cycles,
        bool was_bytecode
    );
    
    // Get profiling data
    std::vector<function_profile> get_hot_functions(uint64_t threshold = 1000) const;
    std::string generate_report() const;
    
    // Optimization recommendations
    std::vector<std::string> get_optimization_candidates() const;
    
private:
    std::unordered_map<std::string, function_profile> profiles_;
    bool profiling_enabled_ = false;
    
    void update_hot_functions();
};

// VM function system integration - enables bytecode functions
class vm_function_system_integration {
public:
    vm_function_system_integration(
        engine& js_engine,
        vm_executor& executor,
        vm_compiler& compiler
    );
    
    // Initialize VM function system
    void initialize();
    
    // Register function-related opcodes
    void register_function_opcodes();
    
    // Enable function compilation to bytecode
    void enable_function_compilation();
    
    // Enable cross-calling between backends
    void enable_cross_backend_calls();
    
    // Register built-in function optimizations
    void register_optimizations();
    
    // Get function registry
    vm_function_registry& get_registry() { return registry_; }
    
private:
    engine& engine_;
    vm_executor& executor_;
    vm_compiler& compiler_;
    vm_function_registry registry_;
    std::unique_ptr<vm_function_executor> function_executor_;
    std::unique_ptr<vm_function_compiler> function_compiler_;
    
    // Register individual optimizations
    void register_function_inlining();
    void register_tail_call_optimization();
    void register_closure_optimization();
};

// Enhanced engine with VM function support
class vm_aware_engine : public engine {
public:
    vm_aware_engine();
    
    // Override execute to use VM for functions
    script_value execute(const std::string& script) override;
    
    // Execute with specific backend choice
    script_value execute_with_interpreter(const std::string& script);
    script_value execute_with_vm(const std::string& script);
    
    // Get VM components
    vm_executor& get_vm_executor() { return *vm_executor_; }
    vm_compiler& get_vm_compiler() { return *vm_compiler_; }
    vm_function_registry& get_function_registry() { return function_integration_->get_registry(); }
    
    // Enable/disable VM function compilation
    void set_compile_functions_to_bytecode(bool enable) {
        compile_functions_to_bytecode_ = enable;
    }
    
    // Get the function profiler (owned by this engine)
    vm_function_profiler& get_profiler() { return profiler_; }
    const vm_function_profiler& get_profiler() const { return profiler_; }
    
private:
    std::unique_ptr<vm_executor> vm_executor_;
    std::unique_ptr<vm_compiler> vm_compiler_;
    std::unique_ptr<vm_function_system_integration> function_integration_;
    bool compile_functions_to_bytecode_ = true;
    
    // Function profiler (owned by engine)
    vm_function_profiler profiler_;
};

// Function definition interceptor for VM compilation
class vm_function_definition_visitor : public ast_visitor {
public:
    vm_function_definition_visitor(
        vm_function_registry& registry,
        vm_compiler& compiler
    );
    
    // Intercept function definitions
    void visit_function_decl(function_decl* decl) override;
    void visit_lambda_expr(lambda_expr* expr) override;
    
    // Get compiled functions
    const std::vector<std::shared_ptr<vm_bytecode_function>>& get_compiled_functions() const {
        return compiled_functions_;
    }
    
    // Implement other visitor methods as no-ops since we only care about functions
    void visit_literal_expr(literal_expr*) override {}
    void visit_identifier_expr(identifier_expr*) override {}
    void visit_binary_expr(binary_expr*) override {}
    void visit_unary_expr(unary_expr*) override {}
    void visit_assignment_expr(assignment_expr*) override {}
    void visit_call_expr(call_expr*) override {}
    void visit_member_expr(member_expr*) override {}
    void visit_array_literal_expr(array_literal_expr*) override {}
    void visit_map_literal_expr(map_literal_expr*) override {}
    void visit_ternary_expr(ternary_expr*) override {}
    void visit_this_expr(this_expr*) override {}
    void visit_super_expr(super_expr*) override {}
    void visit_throw_expr(throw_expr*) override {}
    void visit_new_expr(new_expr*) override {}
    void visit_expression_stmt(expression_stmt*) override {}
    void visit_block_stmt(block_stmt*) override {}
    void visit_variable_decl(variable_decl*) override {}
    void visit_if_stmt(if_stmt*) override {}
    void visit_while_stmt(while_stmt*) override {}
    void visit_for_stmt(for_stmt*) override {}
    void visit_break_stmt(break_stmt*) override {}
    void visit_continue_stmt(continue_stmt*) override {}
    void visit_return_stmt(return_stmt*) override {}
    void visit_try_stmt(try_stmt*) override {}
    void visit_class_decl(class_decl*) override {}
    
private:
    vm_function_registry& registry_;
    vm_compiler& compiler_;
    std::vector<std::shared_ptr<vm_bytecode_function>> compiled_functions_;
};


// Utility functions for VM function operations
namespace vm_function_integration_utils {
    // Check if a script contains function definitions
    bool contains_function_definitions(const std::string& script);
    
    // Extract function definitions from script
    std::vector<std::string> extract_function_definitions(const std::string& script);
    
    // Compile standalone function to bytecode
    std::shared_ptr<vm_bytecode_function> compile_function_string(
        const std::string& func_code,
        vm_compiler& compiler
    );
    
    // Benchmark function execution
    struct benchmark_result {
        uint64_t interpreter_cycles;
        uint64_t bytecode_cycles;
        double speedup_factor;
    };
    
    benchmark_result benchmark_function(
        const std::string& func_name,
        const std::vector<script_value>& test_args,
        engine& engine,
        size_t iterations = 1000
    );
}

// Simple VM function configuration
struct vm_function_config {
    // Use VM for functions with cyclomatic complexity > threshold
    size_t complexity_threshold = 5;
    
    // Use VM for functions longer than this many characters
    size_t length_threshold = 128;
};

// Example usage utilities
inline void example_vm_function_usage() {
    // Create VM-aware engine
    vm_aware_engine engine;
    
    // Define and compile function to bytecode
    engine.execute(R"(
        // This function is compiled to bytecode
        auto fibonacci(auto n) -> auto {
            if (n <= 1) return n;
            return fibonacci(n - 1) + fibonacci(n - 2);
        }
    )");
    
    // Call the bytecode function
    auto result = engine.execute("fibonacci(30)");
    std::cout << "Fibonacci(30) = " << result.as_int() << "\n";
    
    // Check if it actually ran as bytecode
    auto& profiler = engine.get_profiler();
    auto profile = profiler.get_hot_functions();
    for (const auto& func : profile) {
        std::cout << func.name << ": " 
                  << func.bytecode_calls << " bytecode calls, "
                  << func.interpreter_fallbacks << " interpreter fallbacks\n";
    }
}

} // namespace jvm
} // namespace jai