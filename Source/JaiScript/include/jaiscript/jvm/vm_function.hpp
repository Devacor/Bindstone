#pragma once

#include "jaiscript/jvm/vm_types.hpp"
#include "jaiscript/jvm/bytecode.hpp"
#include "jaiscript/jvm/vm_executor.hpp"
#include "jaiscript/jvm/vm_compiler.hpp"
#include "jaiscript/core/function_binder.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace jai {

// Forward declarations from AST
struct function_decl;
struct lambda_expr;
struct method_decl;
struct block_stmt;

namespace jvm {

// Forward declarations
struct vm_bytecode_function;
struct vm_closure;
class vm_function_compiler;
class vm_function_executor;
class vm_function_registry;

// Function call optimization with inline caching
struct function_inline_cache {
    struct cache_entry {
        std::string last_function_name;
        uint16_t cached_index = 0;
        std::shared_ptr<vm_bytecode_function> cached_function;
        uint32_t hit_count = 0;
        uint32_t miss_count = 0;
    };
    
    std::unordered_map<uint32_t, cache_entry> entries;
    
    // Check cache and update statistics
    std::shared_ptr<vm_bytecode_function> check_cache(
        uint32_t call_site_id,
        const std::string& func_name,
        vm_function_registry* registry
    );
    
    // Update cache with new function
    void update_cache(
        uint32_t call_site_id,
        const std::string& func_name,
        uint16_t func_index,
        std::shared_ptr<vm_bytecode_function> func
    );
    
    // Clear cache
    void clear() { entries.clear(); }
};

// Captured variable information for closures
struct captured_variable {
    std::string name;
    uint16_t outer_index;    // Index in outer scope
    uint16_t inner_index;    // Index in closure
    bool is_mutable = false;
};

// VM bytecode function - compiled script function
struct vm_bytecode_function : std::enable_shared_from_this<vm_bytecode_function> {
    std::string name;
    std::vector<std::string> parameters;
    std::string return_type;  // Empty for auto
    
    // Bytecode and metadata
    std::vector<uint8_t> bytecode;
    std::vector<vm_value> constants;
    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    
    // Closure information
    bool is_closure = false;
    std::vector<captured_variable> captures;
    
    // Optimization hints
    bool is_inline_candidate = false;
    bool has_side_effects = true;
    uint32_t call_count = 0;
    
    // Execute this function
    vm_value execute(vm_context& ctx, const std::vector<vm_value>& args);
    
    // Create closure instance
    std::shared_ptr<vm_closure> create_closure(vm_context& ctx);
};

// VM closure - function with captured environment
struct vm_closure {
    std::shared_ptr<vm_bytecode_function> function;
    std::vector<vm_value> captured_values;
    
    // Execute closure
    vm_value execute(vm_context& ctx, const std::vector<vm_value>& args);
};

// Function-related bytecode instructions
enum class function_opcode : uint8_t {
    // Function definition
    define_function = 0xA0,     // Define a new function
    end_function,               // End function definition
    
    // Function calls
    call_function,              // Call function by name
    call_function_fast,         // Call with cached index
    call_bytecode_direct,       // Direct bytecode-to-bytecode call
    call_closure,               // Call closure
    tail_call,                  // Tail call optimization
    
    // Returns
    return_value,               // Return with value
    return_void,                // Return without value
    return_to_bytecode,         // Efficient bytecode return
    
    // Closures
    create_closure,             // Create closure from function
    capture_local,              // Capture local variable
    load_capture,               // Load captured variable
    store_capture,              // Store to captured variable
    
    // Function references
    load_function,              // Load function reference
    store_function,             // Store function reference
    
    // Optimization
    inline_call,                // Inlined function call
    builtin_call,               // Call built-in function
};

// VM function registry - stores compiled functions
class vm_function_registry {
public:
    // Register compiled function
    void register_function(std::shared_ptr<vm_bytecode_function> func);
    
    // Register overloaded function
    void register_overload(const std::string& name, std::shared_ptr<vm_bytecode_function> func);
    
    // Look up function
    std::shared_ptr<vm_bytecode_function> find_function(const std::string& name) const;
    
    // Look up overloaded function
    std::shared_ptr<vm_bytecode_function> find_overload(
        const std::string& name,
        const std::vector<vm_value>& args
    ) const;
    
    // Get function by index (for fast calls)
    std::shared_ptr<vm_bytecode_function> get_by_index(uint16_t index) const;
    
    // Get index for function (for caching)
    uint16_t get_index(const std::string& name) const;
    
    // Clear all functions
    void clear();
    
    // Get the function cache (owned by this registry)
    function_inline_cache& get_cache() { return cache_; }
    const function_inline_cache& get_cache() const { return cache_; }
    
private:
    // Simple functions (name -> function)
    std::unordered_map<std::string, std::shared_ptr<vm_bytecode_function>> functions_;
    
    // Overloaded functions (name -> vector of functions)
    std::unordered_map<std::string, std::vector<std::shared_ptr<vm_bytecode_function>>> overloads_;
    
    // Index mapping for fast access
    std::vector<std::shared_ptr<vm_bytecode_function>> indexed_functions_;
    std::unordered_map<std::string, uint16_t> function_indices_;
    
    // Function cache (owned by registry)
    function_inline_cache cache_;
    
    // Overload resolution
    std::shared_ptr<vm_bytecode_function> resolve_overload(
        const std::vector<std::shared_ptr<vm_bytecode_function>>& candidates,
        const std::vector<vm_value>& args
    ) const;
};

// VM function compiler - compiles AST functions to bytecode
class vm_function_compiler {
public:
    explicit vm_function_compiler(vm_compiler& parent_compiler);
    
    // Compile function declaration to bytecode
    std::shared_ptr<vm_bytecode_function> compile_function(function_decl* func_ast);
    
    // Compile lambda expression to bytecode
    std::shared_ptr<vm_bytecode_function> compile_lambda(lambda_expr* lambda_ast);
    
    // Compile method to bytecode (for class methods)
    std::shared_ptr<vm_bytecode_function> compile_method(
        const std::string& class_name,
        method_decl* method_ast
    );
    
    // Emit function call bytecode
    void emit_function_call(const std::string& func_name, uint16_t arg_count);
    void emit_method_call(const std::string& method_name, uint16_t arg_count);
    void emit_closure_call(uint16_t arg_count);
    
    // Optimization: emit direct call when function is known
    void emit_direct_call(uint16_t func_index, uint16_t arg_count);
    
    // Emit return bytecode
    void emit_return(bool has_value);
    
private:
    vm_compiler& compiler_;
    
    // Current function being compiled
    std::shared_ptr<vm_bytecode_function> current_function_;
    
    // Local variable tracking
    struct local_info {
        std::string name;
        uint16_t index;
        bool is_parameter;
        bool is_captured;
    };
    std::vector<local_info> locals_;
    std::unordered_map<std::string, uint16_t> local_indices_;
    
    // Closure compilation
    void analyze_captures(block_stmt* body);
    void emit_capture(const std::string& var_name);
    
    // Helper methods
    void compile_function_body(block_stmt* body);
    void emit_opcode(function_opcode op);
    void emit_u16(uint16_t value);
    uint16_t add_local(const std::string& name, bool is_parameter = false);
    uint16_t find_local(const std::string& name) const;
};

// VM function executor - executes function-related bytecode
class vm_function_executor {
public:
    explicit vm_function_executor(vm_executor& parent_executor);
    
    // Set function registry
    void set_registry(vm_function_registry* registry) { registry_ = registry; }
    
    // Execute function-related opcodes
    bool execute_function_opcode(function_opcode op, vm_context& ctx);
    
    // Function definition
    void execute_define_function(vm_context& ctx);
    void execute_end_function(vm_context& ctx);
    
    // Function calls
    void execute_call_function(vm_context& ctx);
    void execute_call_function_fast(vm_context& ctx);
    void execute_call_bytecode_direct(vm_context& ctx);
    void execute_call_closure(vm_context& ctx);
    void execute_tail_call(vm_context& ctx);
    
    // Returns
    void execute_return_value(vm_context& ctx);
    void execute_return_void(vm_context& ctx);
    void execute_return_to_bytecode(vm_context& ctx);
    
    // Closures
    void execute_create_closure(vm_context& ctx);
    void execute_capture_local(vm_context& ctx);
    void execute_load_capture(vm_context& ctx);
    void execute_store_capture(vm_context& ctx);
    
    // Function references
    void execute_load_function(vm_context& ctx);
    void execute_store_function(vm_context& ctx);
    
    // Built-in calls
    void execute_builtin_call(vm_context& ctx);
    
private:
    vm_executor& executor_;
    vm_function_registry* registry_ = nullptr;
    
    // Current function being defined
    std::shared_ptr<vm_bytecode_function> current_function_;
    
    // Call stack management
    void push_call_frame(
        std::shared_ptr<vm_bytecode_function> func,
        const std::vector<vm_value>& args,
        vm_context& ctx
    );
    
    void pop_call_frame(vm_context& ctx);
    
    // Helper methods
    std::string pop_string(vm_context& ctx);
    uint16_t read_u16(vm_context& ctx);
};


// Bridge between interpreter and VM functions
class vm_function_bridge {
public:
    // Convert interpreter function to VM bytecode function
    static std::shared_ptr<vm_bytecode_function> compile_to_bytecode(
        const std::string& name,
        script_function interp_func
    );
    
    // Create interpreter wrapper for VM function
    static script_function create_interpreter_wrapper(
        std::shared_ptr<vm_bytecode_function> vm_func
    );
    
    // Allow VM to call interpreter function
    static vm_value call_interpreter_function(
        const std::string& name,
        script_function func,
        const std::vector<vm_value>& args,
        vm_context& ctx
    );
    
    // Register VM function in both registries
    static void register_in_both(
        engine& js_engine,
        vm_function_registry& vm_registry,
        std::shared_ptr<vm_bytecode_function> func
    );
};

// Utility functions for function operations
namespace vm_function_utils {
    // Check if function should be inlined
    bool should_inline(const vm_bytecode_function* func);
    
    // Inline function bytecode at call site
    void inline_function(
        std::vector<uint8_t>& bytecode,
        size_t call_site,
        const vm_bytecode_function* func
    );
    
    // Optimize tail calls
    bool optimize_tail_call(
        std::vector<uint8_t>& bytecode,
        const vm_bytecode_function* func
    );
    
    // Profile function for optimization
    void profile_function_call(
        vm_bytecode_function* func,
        uint64_t execution_cycles
    );
}

// Macros for VM function operations
#define VM_DEFINE_FUNCTION(name, params, body) \
    vm_function_compiler compiler(vm_compiler); \
    auto func = compiler.compile_function(#name, params, body); \
    vm_registry.register_function(func)

#define VM_CALL_FUNCTION(name, ...) \
    vm_function_utils::call_optimized(name, {__VA_ARGS__})

} // namespace jvm
} // namespace jai