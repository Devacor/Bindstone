#include "jaiscript/jvm/vm_class.hpp"
#include "jaiscript/jvm/vm_executor.hpp"
#include "jaiscript/core/class_registry.hpp"
#include <algorithm>
#include <stdexcept>

namespace jai {
namespace jvm {

// Global inline cache
inline_method_cache g_method_cache;

// vm_class_definition implementation

void vm_class_definition::build_vtable() {
    vtable.clear();
    vtable_indices.clear();
    
    // First, inherit vtable from base class
    if (base_class) {
        auto vm_base = std::static_pointer_cast<vm_class_definition>(base_class);
        vm_base->build_vtable();  // Ensure base vtable is built
        vtable = vm_base->vtable;  // Copy base vtable
        vtable_indices = vm_base->vtable_indices;
    }
    
    // Then, add or override with our methods
    for (auto& [method_name, method_info] : methods) {
        auto it = vtable_indices.find(method_name);
        
        if (it != vtable_indices.end()) {
            // Override existing method
            uint16_t index = it->second;
            vtable[index].defining_class = this;
            vtable[index].bytecode = &method_bytecodes[method_name];
            vtable[index].is_virtual = true;  // Promoted to virtual
            
            // Promote base method to virtual if not already
            if (base_class) {
                base_class->promote_method_to_virtual(method_name);
            }
        } else {
            // New method
            vtable_entry entry;
            entry.method_name = method_name;
            entry.defining_class = this;
            entry.bytecode = &method_bytecodes[method_name];
            entry.is_virtual = method_info.is_virtual;
            
            uint16_t index = static_cast<uint16_t>(vtable.size());
            vtable.push_back(entry);
            vtable_indices[method_name] = index;
        }
    }
}

vm_class_definition::vtable_entry* vm_class_definition::get_method_for_dispatch(
    const std::string& method_name, 
    uint32_t cache_id
) const {
    // Check inline cache first
    auto cached = g_method_cache.check_cache(cache_id, name);
    if (cached) {
        return cached;
    }
    
    // Look up in vtable
    auto it = vtable_indices.find(method_name);
    if (it != vtable_indices.end()) {
        auto* entry = const_cast<vtable_entry*>(&vtable[it->second]);
        g_method_cache.update_cache(cache_id, name, entry);
        return entry;
    }
    
    return nullptr;
}

void vm_class_definition::vm_call_method(
    vm_class_instance* instance,
    const std::string& method_name,
    vm_context& ctx
) const {
    // Use centralized dispatch logic
    auto method = find_method(method_name);
    if (!method) {
        throw std::runtime_error("Method not found: " + method_name);
    }
    
    if (method->is_virtual) {
        vm_call_method_virtual(instance, method_name, ctx);
    } else {
        // Get vtable entry for direct call
        auto it = vtable_indices.find(method_name);
        if (it != vtable_indices.end()) {
            auto* entry = const_cast<vtable_entry*>(&vtable[it->second]);
            vm_call_method_direct(instance, entry, ctx);
        } else {
            // TODO: Fall back to interpreter execution for non-compiled methods
            // Currently throws error if method not in vtable
            throw std::runtime_error("Method not in vtable: " + method_name);
        }
    }
}

void vm_class_definition::vm_call_destructor(vm_class_instance* instance, vm_context& ctx) const {
    if (destructor_is_virtual) {
        vm_call_destructor_virtual(instance, ctx);
    } else {
        vm_call_destructor_direct(instance, ctx);
    }
}

void vm_class_definition::vm_call_method_direct(
    vm_class_instance* instance,
    vtable_entry* method,
    vm_context& ctx
) const {
    if (!method || !method->bytecode) {
        throw std::runtime_error("Invalid method for direct call");
    }
    
    // Push 'this' as first argument
    ctx.push(vm_value::make_object(std::static_pointer_cast<vm_class_instance>(instance->shared_from_this())));
    
    // Execute method bytecode
    ctx.execute_bytecode(method->bytecode->code, method->bytecode->constants);
}

void vm_class_definition::vm_call_method_virtual(
    vm_class_instance* instance,
    const std::string& method_name,
    vm_context& ctx
) const {
    // Find actual class of instance
    auto actual_class = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    
    // Look up method in actual class's vtable
    auto it = actual_class->vtable_indices.find(method_name);
    if (it != actual_class->vtable_indices.end()) {
        auto* entry = &actual_class->vtable[it->second];
        vm_call_method_direct(instance, entry, ctx);
    } else {
        throw std::runtime_error("Virtual method not found: " + method_name);
    }
}

void vm_class_definition::vm_call_destructor_direct(vm_class_instance* instance, vm_context& ctx) const {
    if (destructor_bytecode) {
        // Push 'this' as argument
        ctx.push(vm_value::make_object(std::static_pointer_cast<vm_class_instance>(instance->shared_from_this())));
        
        // Execute destructor bytecode
        ctx.execute_bytecode(destructor_bytecode->code, destructor_bytecode->constants);
    }
}

void vm_class_definition::vm_call_destructor_virtual(vm_class_instance* instance, vm_context& ctx) const {
    // Walk inheritance chain from most derived to base
    auto current_class = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    
    while (current_class) {
        current_class->vm_call_destructor_direct(instance, ctx);
        current_class = std::static_pointer_cast<vm_class_definition>(current_class->base_class);
    }
}

// vm_class_compiler implementation

vm_class_compiler::vm_class_compiler(vm_compiler& parent_compiler)
    : compiler_(parent_compiler) {
}

std::shared_ptr<vm_class_definition> vm_class_compiler::compile_class(class_decl* class_ast) {
    auto class_def = std::make_shared<vm_class_definition>();
    class_def->name = class_ast->name;
    
    // Set base class if specified
    if (!class_ast->base_class_name.empty()) {
        auto base = class_registry::instance().find_script_class(class_ast->base_class_name);
        if (!base) {
            throw std::runtime_error("Base class not found: " + class_ast->base_class_name);
        }
        class_def->base_class = base;
    }
    
    current_class_ = class_def.get();
    
    // Compile fields
    uint16_t field_index = 0;
    for (auto& field : class_ast->fields) {
        compile_field(field.get(), class_def.get());
        class_def->field_indices[field->name] = field_index++;
    }
    
    // Compile methods
    for (auto& method : class_ast->methods) {
        compile_method(method.get(), class_def.get());
    }
    
    // Compile constructors
    for (size_t i = 0; i < class_ast->constructors.size(); ++i) {
        compile_constructor(class_ast->constructors[i].get(), class_def.get());
    }
    
    // Compile destructor
    if (class_ast->destructor) {
        compile_destructor(class_ast->destructor.get(), class_def.get());
    }
    
    // Build vtable after all methods are compiled
    class_def->build_vtable();
    
    current_class_ = nullptr;
    return class_def;
}

void vm_class_compiler::compile_field(field_decl* field, vm_class_definition* class_def) {
    field_declaration decl;
    decl.name = field->name;
    decl.type_name = field->type_name;
    decl.access = field->access;
    
    // Compile default value expression
    if (field->default_value) {
        // In a full implementation, would compile the expression to bytecode
        // For now, evaluate it directly
        decl.default_value = script_value();  // Placeholder
    }
    
    class_def->fields.push_back(decl);
}

void vm_class_compiler::compile_method(method_decl* method, vm_class_definition* class_def) {
    method_info info;
    info.name = method->name;
    info.access = method->access;
    info.is_override = method->is_override;
    info.is_virtual = method->is_virtual;
    info.method_type = method_info::script_direct;
    
    // Compile method body to bytecode
    auto bytecode = compile_method_body(method->body.get());
    class_def->method_bytecodes[method->name] = std::move(bytecode);
    
    // Create function wrapper for interpreter compatibility
    auto func = std::make_shared<function_decl>();
    func->name = method->name;
    info.script_method = func;
    
    class_def->methods[method->name] = info;
}

void vm_class_compiler::compile_constructor(constructor_decl* ctor, vm_class_definition* class_def) {
    constructor_declaration decl;
    decl.class_name = ctor->class_name;
    decl.is_delegating = ctor->is_delegating;
    decl.delegation_type_val = ctor->delegation_type_val;
    
    // Compile constructor body
    auto bytecode = compile_method_body(ctor->body.get());
    bytecode.parameter_count = static_cast<uint16_t>(ctor->parameters.size());
    
    // Handle delegation
    vm_class_definition::constructor_info info;
    info.has_delegation = ctor->is_delegating;
    info.delegation_type_val = ctor->delegation_type_val;
    
    if (ctor->is_delegating && ctor->delegation_type_val == delegation_type::same_class) {
        // Find target constructor index
        // In a full implementation, would match parameter signatures
        info.delegation_target_index = 0;  // Placeholder
    }
    
    class_def->constructor_bytecodes.push_back(std::move(bytecode));
    class_def->constructor_infos.push_back(info);
    class_def->constructors.push_back(decl);
}

void vm_class_compiler::compile_destructor(destructor_decl* dtor, vm_class_definition* class_def) {
    auto bytecode = std::make_unique<vm_class_definition::method_bytecode>();
    *bytecode = compile_method_body(dtor->body.get());
    class_def->destructor_bytecode = std::move(bytecode);
    
    // Create function wrapper
    auto func = std::make_shared<function_decl>();
    func->name = "~" + dtor->class_name;
    class_def->destructor = func;
}

vm_class_definition::method_bytecode vm_class_compiler::compile_method_body(block_stmt* body) {
    vm_class_definition::method_bytecode bytecode;
    
    // In a full implementation, would compile the AST to bytecode
    // For now, return placeholder bytecode
    bytecode.code.push_back(static_cast<uint8_t>(opcode::return_value));
    bytecode.max_stack = 10;
    bytecode.max_locals = 10;
    
    return bytecode;
}

void vm_class_compiler::emit_method_call(const std::string& method_name, uint16_t arg_count) {
    emit_opcode(class_opcode::invoke_method);
    emit_string_constant(method_name);
    emit_u16(arg_count);
}

void vm_class_compiler::emit_virtual_call(const std::string& method_name, uint16_t arg_count) {
    emit_opcode(class_opcode::invoke_virtual);
    emit_string_constant(method_name);
    emit_u16(arg_count);
}

void vm_class_compiler::emit_super_call(const std::string& method_name, uint16_t arg_count) {
    emit_opcode(class_opcode::invoke_super);
    emit_string_constant(method_name);
    emit_u16(arg_count);
}

void vm_class_compiler::emit_field_get(const std::string& field_name) {
    // Try to optimize to fast access
    if (current_class_) {
        auto it = current_class_->field_indices.find(field_name);
        if (it != current_class_->field_indices.end()) {
            emit_field_get_fast(it->second);
            return;
        }
    }
    
    emit_opcode(class_opcode::get_field);
    emit_string_constant(field_name);
}

void vm_class_compiler::emit_field_set(const std::string& field_name) {
    // Try to optimize to fast access
    if (current_class_) {
        auto it = current_class_->field_indices.find(field_name);
        if (it != current_class_->field_indices.end()) {
            emit_field_set_fast(it->second);
            return;
        }
    }
    
    emit_opcode(class_opcode::set_field);
    emit_string_constant(field_name);
}

void vm_class_compiler::emit_field_get_fast(uint16_t field_index) {
    emit_opcode(class_opcode::get_field_fast);
    emit_u16(field_index);
}

void vm_class_compiler::emit_field_set_fast(uint16_t field_index) {
    emit_opcode(class_opcode::set_field_fast);
    emit_u16(field_index);
}

void vm_class_compiler::emit_opcode(class_opcode op) {
    compiler_.emit_byte(static_cast<uint8_t>(op));
}

void vm_class_compiler::emit_u16(uint16_t value) {
    compiler_.emit_byte(value >> 8);
    compiler_.emit_byte(value & 0xFF);
}

void vm_class_compiler::emit_string_constant(const std::string& str) {
    uint16_t index = compiler_.add_constant(vm_value::make_string(str));
    emit_u16(index);
}

// vm_class_executor implementation

vm_class_executor::vm_class_executor(vm_executor& parent_executor)
    : executor_(parent_executor) {
}

bool vm_class_executor::execute_class_opcode(class_opcode op, vm_context& ctx) {
    switch (op) {
    case class_opcode::define_class:
        execute_define_class(ctx);
        break;
    case class_opcode::define_field:
        execute_define_field(ctx);
        break;
    case class_opcode::define_method:
        execute_define_method(ctx);
        break;
    case class_opcode::define_constructor:
        execute_define_constructor(ctx);
        break;
    case class_opcode::define_destructor:
        execute_define_destructor(ctx);
        break;
    case class_opcode::end_class:
        execute_end_class(ctx);
        break;
    case class_opcode::new_instance:
        execute_new_instance(ctx);
        break;
    case class_opcode::make_shared_instance:
        execute_make_shared_instance(ctx);
        break;
    case class_opcode::get_field:
        execute_get_field(ctx);
        break;
    case class_opcode::set_field:
        execute_set_field(ctx);
        break;
    case class_opcode::get_field_fast:
        execute_get_field_fast(ctx);
        break;
    case class_opcode::set_field_fast:
        execute_set_field_fast(ctx);
        break;
    case class_opcode::invoke_method:
        execute_invoke_method(ctx);
        break;
    case class_opcode::invoke_virtual:
        execute_invoke_virtual(ctx);
        break;
    case class_opcode::invoke_direct:
        execute_invoke_direct(ctx);
        break;
    case class_opcode::invoke_super:
        execute_invoke_super(ctx);
        break;
    case class_opcode::invoke_constructor:
        execute_invoke_constructor(ctx);
        break;
    case class_opcode::instanceof:
        execute_instanceof(ctx);
        break;
    case class_opcode::checkcast:
        execute_checkcast(ctx);
        break;
    case class_opcode::get_class:
        execute_get_class(ctx);
        break;
    default:
        return false;  // Unknown opcode
    }
    
    return true;
}

void vm_class_executor::execute_define_class(vm_context& ctx) {
    std::string class_name = pop_string(ctx);
    std::string base_class_name = pop_string(ctx);  // Empty string if no base
    
    current_class_def_ = std::make_shared<vm_class_definition>();
    current_class_def_->name = class_name;
    
    if (!base_class_name.empty()) {
        auto base = class_registry::instance().find_script_class(base_class_name);
        if (!base) {
            throw std::runtime_error("Base class not found: " + base_class_name);
        }
        current_class_def_->base_class = base;
    }
}

void vm_class_executor::execute_define_field(vm_context& ctx) {
    if (!current_class_def_) {
        throw std::runtime_error("Field definition outside class context");
    }
    
    std::string field_name = pop_string(ctx);
    auto default_value = ctx.pop();  // Default value or null
    
    field_declaration field;
    field.name = field_name;
    field.default_value = vm_class_bridge::to_script_value(default_value);
    
    current_class_def_->fields.push_back(field);
    uint16_t index = static_cast<uint16_t>(current_class_def_->fields.size() - 1);
    current_class_def_->field_indices[field_name] = index;
}

void vm_class_executor::execute_define_method(vm_context& ctx) {
    if (!current_class_def_) {
        throw std::runtime_error("Method definition outside class context");
    }
    
    std::string method_name = pop_string(ctx);
    uint16_t bytecode_length = read_u16(ctx);
    
    // Read method bytecode
    vm_class_definition::method_bytecode bytecode;
    bytecode.code.reserve(bytecode_length);
    for (uint16_t i = 0; i < bytecode_length; ++i) {
        bytecode.code.push_back(ctx.read_byte());
    }
    
    // Read method metadata
    bytecode.max_stack = read_u16(ctx);
    bytecode.max_locals = read_u16(ctx);
    bytecode.parameter_count = read_u16(ctx);
    
    current_class_def_->method_bytecodes[method_name] = std::move(bytecode);
    
    // Create method info
    method_info info;
    info.name = method_name;
    info.method_type = method_info::script_direct;
    current_class_def_->methods[method_name] = info;
}

void vm_class_executor::execute_end_class(vm_context& ctx) {
    if (!current_class_def_) {
        throw std::runtime_error("End class without class definition");
    }
    
    // Build vtable
    current_class_def_->build_vtable();
    
    // Register the class
    class_registry::instance().register_script_class(current_class_def_);
    
    // Push class object onto stack
    ctx.push(vm_value::make_class(current_class_def_));
    
    current_class_def_ = nullptr;
}

void vm_class_executor::execute_new_instance(vm_context& ctx) {
    std::string class_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get constructor arguments
    std::vector<script_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(vm_class_bridge::to_script_value(ctx.pop()));
    }
    std::reverse(args.begin(), args.end());
    
    // Create instance
    auto instance = std::make_shared<vm_class_instance>();
    instance->class_name = class_name;
    instance->class_def = class_registry::instance().find_script_class(class_name);
    
    if (!instance->class_def) {
        throw std::runtime_error("Class not found: " + class_name);
    }
    
    // Initialize fields
    auto vm_def = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    instance->field_values.resize(vm_def->fields.size());
    
    // Apply default values
    for (size_t i = 0; i < vm_def->fields.size(); ++i) {
        instance->field_values[i] = vm_class_bridge::to_vm_value(vm_def->fields[i].default_value);
    }
    
    // Execute constructor
    if (!vm_def->constructor_bytecodes.empty()) {
        execute_constructor_chain(instance, 0, ctx);
    }
    
    ctx.push(vm_value::make_object(instance));
}

void vm_class_executor::execute_make_shared_instance(vm_context& ctx) {
    // Same as new_instance but wrapped in shared_ptr
    execute_new_instance(ctx);
}

void vm_class_executor::execute_get_field(vm_context& ctx) {
    std::string field_name = pop_string(ctx);
    auto instance = pop_instance(ctx);
    
    auto vm_instance = std::static_pointer_cast<vm_class_instance>(instance);
    auto vm_def = std::static_pointer_cast<vm_class_definition>(vm_instance->class_def);
    
    auto it = vm_def->field_indices.find(field_name);
    if (it == vm_def->field_indices.end()) {
        throw std::runtime_error("Field not found: " + field_name);
    }
    
    ctx.push(vm_instance->field_values[it->second]);
}

void vm_class_executor::execute_set_field(vm_context& ctx) {
    std::string field_name = pop_string(ctx);
    auto value = ctx.pop();
    auto instance = pop_instance(ctx);
    
    auto vm_instance = std::static_pointer_cast<vm_class_instance>(instance);
    auto vm_def = std::static_pointer_cast<vm_class_definition>(vm_instance->class_def);
    
    auto it = vm_def->field_indices.find(field_name);
    if (it == vm_def->field_indices.end()) {
        throw std::runtime_error("Field not found: " + field_name);
    }
    
    vm_instance->field_values[it->second] = value;
}

void vm_class_executor::execute_get_field_fast(vm_context& ctx) {
    uint16_t field_index = read_u16(ctx);
    auto instance = pop_instance(ctx);
    
    auto vm_instance = std::static_pointer_cast<vm_class_instance>(instance);
    ctx.push(vm_instance->field_values[field_index]);
}

void vm_class_executor::execute_set_field_fast(vm_context& ctx) {
    uint16_t field_index = read_u16(ctx);
    auto value = ctx.pop();
    auto instance = pop_instance(ctx);
    
    auto vm_instance = std::static_pointer_cast<vm_class_instance>(instance);
    vm_instance->field_values[field_index] = value;
}

void vm_class_executor::execute_invoke_method(vm_context& ctx) {
    std::string method_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get instance (it's under the arguments on stack)
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    
    auto instance = pop_instance(ctx);
    
    // Push args back for method
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        ctx.push(*it);
    }
    
    dispatch_method(instance, method_name, arg_count, ctx);
}

void vm_class_executor::execute_invoke_virtual(vm_context& ctx) {
    std::string method_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get instance (it's under the arguments on stack)
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    
    auto instance = pop_instance(ctx);
    
    // Push args back for method
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        ctx.push(*it);
    }
    
    dispatch_virtual_method(instance, method_name, arg_count, ctx);
}

void vm_class_executor::execute_invoke_super(vm_context& ctx) {
    std::string method_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get current instance from context
    auto instance = ctx.get_current_instance();
    if (!instance) {
        throw std::runtime_error("Super call outside instance method");
    }
    
    auto vm_instance = std::static_pointer_cast<vm_class_instance>(instance);
    auto base_class = vm_instance->class_def->base_class;
    
    if (!base_class) {
        throw std::runtime_error("No base class for super call");
    }
    
    // Call method directly on base class
    auto vm_base = std::static_pointer_cast<vm_class_definition>(base_class);
    vm_base->vm_call_method(vm_instance.get(), method_name, ctx);
}

void vm_class_executor::execute_instanceof(vm_context& ctx) {
    std::string class_name = pop_string(ctx);
    auto value = ctx.pop();
    
    bool result = false;
    if (value.is_object()) {
        auto instance = std::dynamic_pointer_cast<script_class_instance>(value.as_object());
        if (instance) {
            result = class_registry::instance().is_assignable_from(instance->class_name, class_name);
        }
    }
    
    ctx.push(vm_value::make_bool(result));
}

void vm_class_executor::execute_checkcast(vm_context& ctx) {
    std::string target_class = pop_string(ctx);
    auto value = ctx.peek();  // Don't pop - checkcast leaves value on stack
    
    if (value.is_object()) {
        auto instance = std::dynamic_pointer_cast<script_class_instance>(value.as_object());
        if (instance) {
            if (!class_registry::instance().is_assignable_from(instance->class_name, target_class)) {
                throw std::runtime_error("Cast failed: " + instance->class_name + " to " + target_class);
            }
        }
    }
    
    // Value remains on stack
}

void vm_class_executor::execute_get_class(vm_context& ctx) {
    auto value = ctx.pop();
    
    if (value.is_object()) {
        auto instance = std::dynamic_pointer_cast<script_class_instance>(value.as_object());
        if (instance) {
            ctx.push(vm_value::make_string(instance->class_name));
            return;
        }
    }
    
    ctx.push(vm_value::make_string("unknown"));
}

std::shared_ptr<vm_class_instance> vm_class_executor::pop_instance(vm_context& ctx) {
    auto value = ctx.pop();
    if (!value.is_object()) {
        throw std::runtime_error("Expected class instance");
    }
    
    auto instance = std::dynamic_pointer_cast<vm_class_instance>(value.as_object());
    if (!instance) {
        // Try to convert from interpreter instance
        auto interp_instance = std::dynamic_pointer_cast<script_class_instance>(value.as_object());
        if (interp_instance) {
            instance = vm_class_bridge::create_interpreter_instance_wrapper(interp_instance);
        } else {
            throw std::runtime_error("Invalid class instance");
        }
    }
    
    return instance;
}

std::string vm_class_executor::pop_string(vm_context& ctx) {
    auto value = ctx.pop();
    return value.as_string();
}

uint16_t vm_class_executor::read_u16(vm_context& ctx) {
    uint8_t high = ctx.read_byte();
    uint8_t low = ctx.read_byte();
    return (high << 8) | low;
}

void vm_class_executor::dispatch_method(
    std::shared_ptr<vm_class_instance> instance,
    const std::string& method_name,
    uint16_t arg_count,
    vm_context& ctx
) {
    auto vm_def = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    vm_def->vm_call_method(instance.get(), method_name, ctx);
}

void vm_class_executor::dispatch_virtual_method(
    std::shared_ptr<vm_class_instance> instance,
    const std::string& method_name,
    uint16_t arg_count,
    vm_context& ctx
) {
    auto vm_def = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    vm_def->vm_call_method_virtual(instance.get(), method_name, ctx);
}

void vm_class_executor::execute_constructor_chain(
    std::shared_ptr<vm_class_instance> instance,
    uint16_t constructor_index,
    vm_context& ctx
) {
    auto vm_def = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    
    if (constructor_index >= vm_def->constructor_bytecodes.size()) {
        return;  // No constructor
    }
    
    auto& ctor_info = vm_def->constructor_infos[constructor_index];
    auto& bytecode = vm_def->constructor_bytecodes[constructor_index];
    
    // Handle delegation
    if (ctor_info.has_delegation) {
        if (ctor_info.delegation_type_val == delegation_type::same_class) {
            // Delegate to another constructor in same class
            execute_constructor_chain(instance, ctor_info.delegation_target_index, ctx);
        } else if (ctor_info.delegation_type_val == delegation_type::base_class) {
            // Delegate to base class constructor
            if (vm_def->base_class) {
                // Execute base constructor
                // In full implementation, would handle base constructor properly
            }
        }
    }
    
    // Execute this constructor body
    ctx.push(vm_value::make_object(instance));  // Push 'this'
    ctx.execute_bytecode(bytecode.code, bytecode.constants);
}

// vm_class_bridge implementation

std::shared_ptr<vm_class_definition> vm_class_bridge::compile_to_vm(
    std::shared_ptr<script_class_definition> interpreter_class
) {
    // Convert interpreter class to VM class
    auto vm_class = std::make_shared<vm_class_definition>();
    
    // Copy basic properties
    vm_class->name = interpreter_class->name;
    vm_class->fields = interpreter_class->fields;
    vm_class->methods = interpreter_class->methods;
    vm_class->constructors = interpreter_class->constructors;
    vm_class->destructor = interpreter_class->destructor;
    vm_class->destructor_is_virtual = interpreter_class->destructor_is_virtual;
    vm_class->base_class = interpreter_class->base_class;
    
    // TODO: Compile interpreter methods to bytecode
    
    return vm_class;
}

script_value vm_class_bridge::call_vm_method(
    std::shared_ptr<vm_class_instance> instance,
    const std::string& method_name,
    const std::vector<script_value>& args
) {
    // Create temporary VM context
    vm_context ctx;
    
    // Push arguments
    for (const auto& arg : args) {
        ctx.push(to_vm_value(arg));
    }
    
    // Call method
    auto vm_def = std::static_pointer_cast<vm_class_definition>(instance->class_def);
    vm_def->vm_call_method(instance.get(), method_name, ctx);
    
    // Get result
    if (!ctx.empty()) {
        return to_script_value(ctx.pop());
    }
    
    return script_value();
}

vm_value vm_class_bridge::call_interpreter_method(
    std::shared_ptr<script_class_instance> instance,
    const std::string& method_name,
    vm_context& ctx
) {
    // Pop arguments from VM stack
    auto method = instance->class_def->find_method(method_name);
    if (!method) {
        throw std::runtime_error("Method not found: " + method_name);
    }
    
    // Get argument count - simplified
    uint16_t arg_count = 0;  // Would get from method signature
    
    std::vector<script_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(to_script_value(ctx.pop()));
    }
    std::reverse(args.begin(), args.end());
    
    // Call interpreter method
    auto result = call_method(instance, method_name, args);
    
    return to_vm_value(result);
}

vm_value vm_class_bridge::to_vm_value(const script_value& val) {
    // Convert script_value to vm_value
    switch (val.get_type()) {
    case script_value_type::null_type:
        return vm_value::make_null();
    case script_value_type::bool_type:
        return vm_value::make_bool(val.as_bool());
    case script_value_type::int_type:
        return vm_value::make_int(val.as_int());
    case script_value_type::float_type:
        return vm_value::make_float(val.as_float());
    case script_value_type::string_type:
        return vm_value::make_string(val.as_string());
    case script_value_type::object_type:
        return vm_value::make_object(val.as_object());
    default:
        return vm_value::make_null();
    }
}

script_value vm_class_bridge::to_script_value(const vm_value& val) {
    // Convert vm_value to script_value
    if (val.is_null()) {
        return script_value();
    } else if (val.is_bool()) {
        return script_value(val.as_bool());
    } else if (val.is_int()) {
        return script_value(val.as_int());
    } else if (val.is_float()) {
        return script_value(val.as_float());
    } else if (val.is_string()) {
        return script_value(val.as_string());
    } else if (val.is_object()) {
        return script_value::make_object(val.as_object());
    }
    
    return script_value();
}

std::shared_ptr<script_class_instance> vm_class_bridge::create_vm_instance_wrapper(
    std::shared_ptr<vm_class_instance> vm_instance
) {
    // VM instance already implements script_class_instance interface
    return std::static_pointer_cast<script_class_instance>(vm_instance);
}

std::shared_ptr<vm_class_instance> vm_class_bridge::create_interpreter_instance_wrapper(
    std::shared_ptr<script_class_instance> interpreter_instance
) {
    // Create a VM wrapper for interpreter instance
    auto wrapper = std::make_shared<vm_class_instance>();
    wrapper->class_name = interpreter_instance->class_name;
    wrapper->class_def = interpreter_instance->class_def;
    wrapper->fields = interpreter_instance->fields;
    
    // Convert fields to VM format
    auto vm_def = std::dynamic_pointer_cast<vm_class_definition>(wrapper->class_def);
    if (vm_def) {
        wrapper->field_values.resize(vm_def->fields.size());
        for (const auto& [name, value] : interpreter_instance->fields) {
            auto it = vm_def->field_indices.find(name);
            if (it != vm_def->field_indices.end()) {
                wrapper->field_values[it->second] = to_vm_value(value);
            }
        }
    }
    
    return wrapper;
}

// inline_method_cache implementation

vm_class_definition::vtable_entry* inline_method_cache::check_cache(
    uint32_t call_site_id,
    const std::string& class_name
) {
    auto it = entries.find(call_site_id);
    if (it != entries.end()) {
        auto& entry = it->second;
        if (entry.last_class_name == class_name && entry.cached_method) {
            entry.hit_count++;
            return entry.cached_method;
        }
        entry.miss_count++;
    }
    
    return nullptr;
}

void inline_method_cache::update_cache(
    uint32_t call_site_id,
    const std::string& class_name,
    vm_class_definition::vtable_entry* method
) {
    auto& entry = entries[call_site_id];
    
    // Only cache if monomorphic
    if (entry.last_class_name.empty() || entry.last_class_name == class_name) {
        entry.last_class_name = class_name;
        entry.cached_method = method;
    } else {
        // Polymorphic - disable caching
        entry.cached_method = nullptr;
    }
}

} // namespace jvm
} // namespace jaiscript