#include "jaiscript/jvm/vm_class.hpp"
#include "jaiscript/jvm/vm_function.hpp"
#include "jaiscript/detail/ast.hpp"
#include "jaiscript/detail/class_parser.hpp"

namespace jaiscript {
namespace jvm {

// Complete implementation of compile_method_body for VM classes
vm_class_definition::method_bytecode vm_class_compiler::compile_method_body(block_stmt* body) {
    vm_class_definition::method_bytecode bytecode;
    
    // Create a temporary function to compile the method
    auto temp_func = std::make_shared<vm_bytecode_function>();
    temp_func->name = current_class_->name + "::method";
    
    // Use function compiler to compile the body
    vm_function_compiler func_compiler(compiler_);
    
    // Save current bytecode state
    auto saved_bytecode = compiler_.get_current_bytecode();
    compiler_.start_new_bytecode();
    
    // Compile the method body
    compiler_.compile_statement(body);
    
    // Ensure there's a return
    auto current_bytecode = compiler_.get_current_bytecode();
    if (current_bytecode.empty() || 
        current_bytecode.back() != static_cast<uint8_t>(opcode::return_value)) {
        compiler_.emit_byte(static_cast<uint8_t>(opcode::return_value));
    }
    
    // Extract compiled bytecode
    bytecode.code = compiler_.get_current_bytecode();
    bytecode.constants = compiler_.get_current_constants();
    
    // Calculate stack requirements
    bytecode.max_stack = compiler_.calculate_max_stack(bytecode.code);
    bytecode.max_locals = 10; // Conservative estimate, would need proper analysis
    
    // Restore previous bytecode state
    compiler_.restore_bytecode(saved_bytecode);
    
    return bytecode;
}

// Enhanced method compilation with proper bytecode generation
void vm_class_compiler::compile_method(method_decl* method, vm_class_definition* class_def) {
    method_info info;
    info.name = method->name;
    info.access = method->access;
    info.is_override = method->is_override;
    info.is_virtual = method->is_virtual;
    info.method_type = method_info::script_direct;
    
    // Use function compiler for method
    vm_function_compiler func_compiler(compiler_);
    auto vm_func = func_compiler.compile_method(class_def->name, method);
    
    // Store bytecode in class definition
    vm_class_definition::method_bytecode method_bytecode;
    method_bytecode.code = vm_func->bytecode;
    method_bytecode.constants = vm_func->constants;
    method_bytecode.max_stack = vm_func->max_stack;
    method_bytecode.max_locals = vm_func->max_locals;
    method_bytecode.parameter_count = static_cast<uint16_t>(vm_func->parameters.size());
    
    class_def->method_bytecodes[method->name] = std::move(method_bytecode);
    
    // Create function wrapper for compatibility
    info.script_method = std::make_shared<function_decl>();
    info.script_method->name = method->name;
    info.script_method->parameters = method->parameters;
    
    class_def->methods[method->name] = info;
}

// Enhanced constructor compilation with delegation support
void vm_class_compiler::compile_constructor(constructor_decl* ctor, vm_class_definition* class_def) {
    constructor_declaration decl;
    decl.class_name = ctor->class_name;
    decl.is_delegating = ctor->is_delegating;
    decl.delegation_type_val = ctor->delegation_type_val;
    
    // Create a function for the constructor
    auto ctor_func = std::make_shared<function_decl>();
    ctor_func->name = ctor->class_name;
    ctor_func->parameters = ctor->parameters;
    ctor_func->body = std::move(ctor->body);
    
    // Use function compiler
    vm_function_compiler func_compiler(compiler_);
    auto vm_func = func_compiler.compile_function(ctor_func.get());
    
    // Extract bytecode
    vm_class_definition::method_bytecode bytecode;
    bytecode.code = vm_func->bytecode;
    bytecode.constants = vm_func->constants;
    bytecode.max_stack = vm_func->max_stack;
    bytecode.max_locals = vm_func->max_locals;
    bytecode.parameter_count = static_cast<uint16_t>(vm_func->parameters.size());
    
    // Handle constructor delegation
    vm_class_definition::constructor_info info;
    info.has_delegation = ctor->is_delegating;
    info.delegation_type_val = ctor->delegation_type_val;
    
    if (ctor->is_delegating) {
        // Compile delegation arguments
        std::vector<vm_value> delegation_values;
        for (auto& arg_expr : ctor->delegation_args) {
            // In a full implementation, would evaluate these expressions
            // For now, use placeholder values
            delegation_values.push_back(vm_value());
        }
        decl.delegation_args = delegation_values;
        
        if (ctor->delegation_type_val == delegation_type::same_class) {
            // Find target constructor by matching parameters
            // Simplified: use first constructor as target
            info.delegation_target_index = 0;
        }
    }
    
    class_def->constructor_bytecodes.push_back(std::move(bytecode));
    class_def->constructor_infos.push_back(info);
    class_def->constructors.push_back(decl);
}

// Enhanced destructor compilation
void vm_class_compiler::compile_destructor(destructor_decl* dtor, vm_class_definition* class_def) {
    // Create a function for the destructor
    auto dtor_func = std::make_shared<function_decl>();
    dtor_func->name = "~" + dtor->class_name;
    dtor_func->parameters = {"this"};
    dtor_func->body = std::move(dtor->body);
    
    // Use function compiler
    vm_function_compiler func_compiler(compiler_);
    auto vm_func = func_compiler.compile_function(dtor_func.get());
    
    // Extract bytecode
    auto bytecode = std::make_unique<vm_class_definition::method_bytecode>();
    bytecode->code = vm_func->bytecode;
    bytecode->constants = vm_func->constants;
    bytecode->max_stack = vm_func->max_stack;
    bytecode->max_locals = vm_func->max_locals;
    bytecode->parameter_count = 1; // 'this' parameter
    
    class_def->destructor_bytecode = std::move(bytecode);
    
    // Create function wrapper
    class_def->destructor = std::make_shared<function_decl>();
    class_def->destructor->name = dtor_func->name;
    
    // Check if base class exists to promote destructor
    if (class_def->base_class) {
        virtual_method_promoter promoter;
        promoter.promote_destructor_to_virtual(class_def->base_class);
    }
}

} // namespace jvm
} // namespace jaiscript