#pragma once

#include "jaiscript/core/script_class.hpp"
#include "jaiscript/jvm/vm_types.hpp"
#include "jaiscript/jvm/bytecode.hpp"
#include "jaiscript/detail/ast.hpp"
#include "jaiscript/detail/class_parser.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace jai {

namespace jvm {

// Forward declarations
struct vm_class_definition;
struct vm_class_instance;
struct vm_method_info;

// VM-specific bytecode instructions for class operations
enum class class_opcode : uint8_t {
    // Class definition
    define_class = 0x80,        // Define a new class
    define_field,               // Define a field with default value
    define_method,              // Define a method
    define_constructor,         // Define a constructor
    define_destructor,          // Define a destructor
    end_class,                  // End class definition
    
    // Instance operations
    new_instance,               // Create new instance
    make_shared_instance,       // Create shared_ptr instance
    dup_instance,               // Duplicate instance reference
    
    // Field operations
    get_field,                  // Get field value
    set_field,                  // Set field value
    get_field_fast,             // Optimized field access (by index)
    set_field_fast,             // Optimized field set (by index)
    
    // Method operations
    invoke_method,              // Call method with dispatch
    invoke_virtual,             // Virtual method call
    invoke_direct,              // Direct method call (non-virtual)
    invoke_super,               // Super method call
    invoke_constructor,         // Constructor call
    invoke_destructor,          // Destructor call (usually automatic)
    
    // Type operations
    instanceof,                 // Type check
    checkcast,                  // Cast with type check
    get_class,                  // Get class of instance
    
    // Optimization hints
    cache_method,               // Cache method lookup
    inline_cache_hit,           // Inline cache hit (fast path)
    inline_cache_miss,          // Inline cache miss (slow path)
};

// VM class definition - bytecode representation
struct vm_class_definition : public script_class_definition {
    // Bytecode for methods, constructors, destructors
    struct method_bytecode {
        std::vector<uint8_t> code;
        std::vector<vm_value> constants;
        uint16_t max_stack = 0;
        uint16_t max_locals = 0;
        uint16_t parameter_count = 0;
    };
    
    // Field indices for fast access
    std::unordered_map<std::string, uint16_t> field_indices;
    
    // Method bytecode storage
    std::unordered_map<std::string, method_bytecode> method_bytecodes;
    std::vector<method_bytecode> constructor_bytecodes;
    std::unique_ptr<method_bytecode> destructor_bytecode;
    
    // Virtual method table (vtable) for fast dispatch
    struct vtable_entry {
        std::string method_name;
        vm_class_definition* defining_class = nullptr;
        method_bytecode* bytecode = nullptr;
        bool is_virtual = false;
    };
    std::vector<vtable_entry> vtable;
    std::unordered_map<std::string, uint16_t> vtable_indices;
    
    // Method cache for monomorphic call sites
    mutable std::unordered_map<uint32_t, vtable_entry*> method_cache;
    
    // Constructor delegation info
    struct constructor_info {
        bool has_delegation = false;
        delegation_type delegation_type_val = delegation_type::none;
        uint16_t delegation_target_index = 0;  // Index of target constructor
    };
    std::vector<constructor_info> constructor_infos;
    
    // Build vtable from inheritance hierarchy
    void build_vtable();
    
    // Get method for dispatch (with caching)
    vtable_entry* get_method_for_dispatch(const std::string& method_name, uint32_t cache_id) const;
    
    // Centralized VM dispatch methods
    void vm_call_method(vm_class_instance* instance, const std::string& method_name, vm_context& ctx) const;
    void vm_call_destructor(vm_class_instance* instance, vm_context& ctx) const;
    
private:
    void vm_call_method_direct(vm_class_instance* instance, vtable_entry* method, vm_context& ctx) const;
    void vm_call_method_virtual(vm_class_instance* instance, const std::string& method_name, vm_context& ctx) const;
    void vm_call_destructor_direct(vm_class_instance* instance, vm_context& ctx) const;
    void vm_call_destructor_virtual(vm_class_instance* instance, vm_context& ctx) const;
};

// VM class instance
struct vm_class_instance : public script_class_instance {
    // Optimized field storage (array for fast indexed access)
    std::vector<vm_value> field_values;
    
    // VM-specific destructor
    ~vm_class_instance() {
        if (class_def) {
            auto vm_def = static_cast<vm_class_definition*>(class_def.get());
            // Create a temporary context for destructor execution
            vm_context temp_ctx;
            vm_def->vm_call_destructor(this, temp_ctx);
        }
    }
    
    // Fast field access by index
    vm_value& get_field_by_index(uint16_t index) {
        return field_values[index];
    }
    
    const vm_value& get_field_by_index(uint16_t index) const {
        return field_values[index];
    }
};

// VM class compiler - converts AST to bytecode
class vm_class_compiler {
public:
    explicit vm_class_compiler(vm_compiler& parent_compiler);
    
    // Compile class declaration to VM bytecode
    std::shared_ptr<vm_class_definition> compile_class(script_class_decl* class_ast);
    
    // Compile individual class members
    void compile_field(field_decl* field, vm_class_definition* class_def);
    void compile_method(method_decl* method, vm_class_definition* class_def);
    void compile_constructor(constructor_decl* ctor, vm_class_definition* class_def);
    void compile_destructor(destructor_decl* dtor, vm_class_definition* class_def);
    
    // Generate bytecode for method dispatch
    void emit_method_call(const std::string& method_name, uint16_t arg_count);
    void emit_virtual_call(const std::string& method_name, uint16_t arg_count);
    void emit_super_call(const std::string& method_name, uint16_t arg_count);
    
    // Generate bytecode for field access
    void emit_field_get(const std::string& field_name);
    void emit_field_set(const std::string& field_name);
    
    // Optimization: emit fast field access when index is known
    void emit_field_get_fast(uint16_t field_index);
    void emit_field_set_fast(uint16_t field_index);
    
private:
    vm_compiler& compiler_;
    vm_class_definition* current_class_ = nullptr;
    
    // Helper methods
    vm_class_definition::method_bytecode compile_method_body(block_stmt* body);
    void compile_constructor_delegation(constructor_decl* ctor, vm_class_definition* class_def);
    void emit_opcode(class_opcode op);
    void emit_u16(uint16_t value);
    void emit_string_constant(const std::string& str);
};

// VM class executor - executes class-related bytecode
class vm_class_executor {
public:
    explicit vm_class_executor(vm_executor& parent_executor);
    
    // Execute class-related opcodes
    bool execute_class_opcode(class_opcode op, vm_context& ctx);
    
    // Class definition execution
    void execute_define_class(vm_context& ctx);
    void execute_define_field(vm_context& ctx);
    void execute_define_method(vm_context& ctx);
    void execute_define_constructor(vm_context& ctx);
    void execute_define_destructor(vm_context& ctx);
    void execute_end_class(vm_context& ctx);
    
    // Instance operations
    void execute_new_instance(vm_context& ctx);
    void execute_make_shared_instance(vm_context& ctx);
    
    // Field operations
    void execute_get_field(vm_context& ctx);
    void execute_set_field(vm_context& ctx);
    void execute_get_field_fast(vm_context& ctx);
    void execute_set_field_fast(vm_context& ctx);
    
    // Method operations
    void execute_invoke_method(vm_context& ctx);
    void execute_invoke_virtual(vm_context& ctx);
    void execute_invoke_direct(vm_context& ctx);
    void execute_invoke_super(vm_context& ctx);
    void execute_invoke_constructor(vm_context& ctx);
    
    // Type operations
    void execute_instanceof(vm_context& ctx);
    void execute_checkcast(vm_context& ctx);
    void execute_get_class(vm_context& ctx);
    
private:
    vm_executor& executor_;
    
    // Current class being defined (during class definition phase)
    std::shared_ptr<vm_class_definition> current_class_def_;
    
    // Helper methods
    std::shared_ptr<vm_class_instance> pop_instance(vm_context& ctx);
    std::string pop_string(vm_context& ctx);
    uint16_t read_u16(vm_context& ctx);
    
    // Method dispatch helpers
    void dispatch_method(
        std::shared_ptr<vm_class_instance> instance,
        const std::string& method_name,
        uint16_t arg_count,
        vm_context& ctx
    );
    
    void dispatch_virtual_method(
        std::shared_ptr<vm_class_instance> instance,
        const std::string& method_name,
        uint16_t arg_count,
        vm_context& ctx
    );
    
    // Constructor execution with delegation
    void execute_constructor_chain(
        std::shared_ptr<vm_class_instance> instance,
        uint16_t constructor_index,
        vm_context& ctx
    );
};

// Integration with interpreter - allows interpreter to call VM classes
class vm_class_bridge {
public:
    // Convert interpreter class to VM class
    static std::shared_ptr<vm_class_definition> compile_to_vm(
        std::shared_ptr<script_class_definition> interpreter_class
    );
    
    // Allow interpreter to call VM class methods
    static script_value call_vm_method(
        std::shared_ptr<vm_class_instance> instance,
        const std::string& method_name,
        const std::vector<script_value>& args
    );
    
    // Allow VM to call interpreter class methods
    static vm_value call_interpreter_method(
        std::shared_ptr<script_class_instance> instance,
        const std::string& method_name,
        vm_context& ctx
    );
    
    // Convert between VM and interpreter values
    static vm_value to_vm_value(const script_value& val);
    static script_value to_script_value(const vm_value& val);
    
    // Create VM instance callable from interpreter
    static std::shared_ptr<script_class_instance> create_vm_instance_wrapper(
        std::shared_ptr<vm_class_instance> vm_instance
    );
    
    // Create interpreter instance callable from VM
    static std::shared_ptr<vm_class_instance> create_interpreter_instance_wrapper(
        std::shared_ptr<script_class_instance> interpreter_instance
    );
};

// Note: inline_method_cache is already defined in vm_types.hpp

// Global inline cache instance
extern inline_method_cache g_method_cache;

} // namespace jvm
} // namespace jai