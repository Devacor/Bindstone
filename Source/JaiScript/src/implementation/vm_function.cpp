#include "jaiscript/jvm/vm_function.hpp"
#include "jaiscript/jvm/vm_executor.hpp"
#include "jaiscript/jvm/vm_class.hpp"
#include "jaiscript/detail/interpreter.hpp"
#include <algorithm>
#include <stdexcept>

namespace jai {
namespace jvm {

// Global function cache
function_inline_cache g_function_cache;

// vm_bytecode_function implementation

vm_value vm_bytecode_function::execute(vm_context& ctx, const std::vector<vm_value>& args) {
    // Validate argument count
    if (args.size() != parameters.size()) {
        throw std::runtime_error("Function " + name + " expects " + 
            std::to_string(parameters.size()) + " arguments, got " + 
            std::to_string(args.size()));
    }
    
    // Create new call frame
    ctx.push_call_frame(max_locals);
    
    // Set up parameters as locals
    for (size_t i = 0; i < args.size(); ++i) {
        ctx.set_local(i, args[i]);
    }
    
    // Execute bytecode
    ctx.execute_bytecode(bytecode, constants);
    
    // Get return value (if any)
    vm_value result;
    if (!ctx.empty()) {
        result = ctx.pop();
    }
    
    // Clean up call frame
    ctx.pop_call_frame();
    
    // Update call statistics
    call_count++;
    
    return result;
}

std::shared_ptr<vm_closure> vm_bytecode_function::create_closure(vm_context& ctx) {
    auto closure = std::make_shared<vm_closure>();
    closure->function = shared_from_this();
    
    // Capture variables
    closure->captured_values.reserve(captures.size());
    for (const auto& capture : captures) {
        closure->captured_values.push_back(ctx.get_local(capture.outer_index));
    }
    
    return closure;
}

// vm_closure implementation

vm_value vm_closure::execute(vm_context& ctx, const std::vector<vm_value>& args) {
    // Set up captured variables in context
    for (size_t i = 0; i < captured_values.size(); ++i) {
        ctx.set_captured(i, captured_values[i]);
    }
    
    // Execute the function with captures available
    return function->execute(ctx, args);
}

// vm_function_registry implementation

void vm_function_registry::register_function(std::shared_ptr<vm_bytecode_function> func) {
    if (!func || func->name.empty()) {
        throw std::runtime_error("Invalid function for registration");
    }
    
    // Add to simple function map
    functions_[func->name] = func;
    
    // Add to indexed storage
    uint16_t index = static_cast<uint16_t>(indexed_functions_.size());
    indexed_functions_.push_back(func);
    function_indices_[func->name] = index;
}

void vm_function_registry::register_overload(
    const std::string& name, 
    std::shared_ptr<vm_bytecode_function> func
) {
    if (!func || name.empty()) {
        throw std::runtime_error("Invalid overloaded function for registration");
    }
    
    overloads_[name].push_back(func);
    
    // Also add to indexed storage
    uint16_t index = static_cast<uint16_t>(indexed_functions_.size());
    indexed_functions_.push_back(func);
}

std::shared_ptr<vm_bytecode_function> vm_function_registry::find_function(const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) {
        return it->second;
    }
    
    // Check overloads (return first if multiple)
    auto overload_it = overloads_.find(name);
    if (overload_it != overloads_.end() && !overload_it->second.empty()) {
        return overload_it->second[0];
    }
    
    return nullptr;
}

std::shared_ptr<vm_bytecode_function> vm_function_registry::find_overload(
    const std::string& name,
    const std::vector<vm_value>& args
) const {
    auto it = overloads_.find(name);
    if (it == overloads_.end()) {
        // Try simple function
        return find_function(name);
    }
    
    return resolve_overload(it->second, args);
}

std::shared_ptr<vm_bytecode_function> vm_function_registry::get_by_index(uint16_t index) const {
    if (index < indexed_functions_.size()) {
        return indexed_functions_[index];
    }
    return nullptr;
}

uint16_t vm_function_registry::get_index(const std::string& name) const {
    auto it = function_indices_.find(name);
    if (it != function_indices_.end()) {
        return it->second;
    }
    return std::numeric_limits<uint16_t>::max();
}

void vm_function_registry::clear() {
    functions_.clear();
    overloads_.clear();
    indexed_functions_.clear();
    function_indices_.clear();
}

std::shared_ptr<vm_bytecode_function> vm_function_registry::resolve_overload(
    const std::vector<std::shared_ptr<vm_bytecode_function>>& candidates,
    const std::vector<vm_value>& args
) const {
    // Simple resolution: match argument count
    // In a full implementation, would do type-based resolution
    for (const auto& candidate : candidates) {
        if (candidate->parameters.size() == args.size()) {
            return candidate;
        }
    }
    
    // No exact match, return first if any
    return candidates.empty() ? nullptr : candidates[0];
}

// vm_function_compiler implementation

vm_function_compiler::vm_function_compiler(vm_compiler& parent_compiler)
    : compiler_(parent_compiler) {
}

std::shared_ptr<vm_bytecode_function> vm_function_compiler::compile_function(function_decl* func_ast) {
    auto func = std::make_shared<vm_bytecode_function>();
    func->name = func_ast->name;
    
    // Extract parameter names
    for (const auto& param : func_ast->parameters) {
        func->parameters.push_back(param.name);
    }
    
    // TODO: Handle return type properly
    func->return_type = "";
    
    current_function_ = func;
    locals_.clear();
    local_indices_.clear();
    
    // Add parameters as locals
    for (const auto& param : func_ast->parameters) {
        add_local(param.name, true);
    }
    
    // Analyze captures if this might be a closure
    analyze_captures(func_ast->body.get());
    
    // Compile function body
    compile_function_body(func_ast->body.get());
    
    // Add implicit return if needed
    if (func->bytecode.empty() || func->bytecode.back() != static_cast<uint8_t>(function_opcode::return_value)) {
        emit_return(false);
    }
    
    // Set metadata
    func->max_locals = static_cast<uint16_t>(locals_.size());
    func->max_stack = compiler_.calculate_max_stack(func->bytecode);
    
    current_function_ = nullptr;
    return func;
}

std::shared_ptr<vm_bytecode_function> vm_function_compiler::compile_lambda(lambda_expr* lambda_ast) {
    auto func = std::make_shared<vm_bytecode_function>();
    func->name = "<lambda>";
    
    // Extract parameters from lambda
    for (const auto& param : lambda_ast->parameters) {
        func->parameters.push_back(param);
    }
    
    current_function_ = func;
    locals_.clear();
    local_indices_.clear();
    
    // Add parameters as locals
    for (const auto& param : func->parameters) {
        add_local(param, true);
    }
    
    // Lambdas are often closures
    analyze_captures(lambda_ast->body.get());
    func->is_closure = !func->captures.empty();
    
    // Compile lambda body
    compile_function_body(lambda_ast->body.get());
    
    // Add implicit return
    if (func->bytecode.empty() || func->bytecode.back() != static_cast<uint8_t>(function_opcode::return_value)) {
        emit_return(false);
    }
    
    func->max_locals = static_cast<uint16_t>(locals_.size());
    func->max_stack = compiler_.calculate_max_stack(func->bytecode);
    
    current_function_ = nullptr;
    return func;
}

std::shared_ptr<vm_bytecode_function> vm_function_compiler::compile_method(
    const std::string& class_name,
    method_decl* method_ast
) {
    auto func = std::make_shared<vm_bytecode_function>();
    func->name = class_name + "::" + method_ast->name;
    
    // Methods have implicit 'this' parameter
    func->parameters.push_back("this");
    for (const auto& param : method_ast->parameters) {
        func->parameters.push_back(param);
    }
    
    func->return_type = method_ast->return_type;
    
    current_function_ = func;
    locals_.clear();
    local_indices_.clear();
    
    // Add all parameters as locals
    for (const auto& param : func->parameters) {
        add_local(param, true);
    }
    
    // Compile method body
    compile_function_body(method_ast->body.get());
    
    // Add implicit return if needed
    if (func->bytecode.empty() || func->bytecode.back() != static_cast<uint8_t>(function_opcode::return_value)) {
        emit_return(false);
    }
    
    func->max_locals = static_cast<uint16_t>(locals_.size());
    func->max_stack = compiler_.calculate_max_stack(func->bytecode);
    
    current_function_ = nullptr;
    return func;
}

void vm_function_compiler::emit_function_call(const std::string& func_name, uint16_t arg_count) {
    // Try to optimize to direct call if possible
    uint16_t func_index = compiler_.get_function_index(func_name);
    if (func_index != std::numeric_limits<uint16_t>::max()) {
        emit_direct_call(func_index, arg_count);
    } else {
        emit_opcode(function_opcode::call_function);
        compiler_.emit_string_constant(func_name);
        emit_u16(arg_count);
    }
}

void vm_function_compiler::emit_method_call(const std::string& method_name, uint16_t arg_count) {
    // Method calls go through class dispatch
    compiler_.emit_byte(static_cast<uint8_t>(class_opcode::invoke_method));
    compiler_.emit_string_constant(method_name);
    emit_u16(arg_count);
}

void vm_function_compiler::emit_closure_call(uint16_t arg_count) {
    emit_opcode(function_opcode::call_closure);
    emit_u16(arg_count);
}

void vm_function_compiler::emit_direct_call(uint16_t func_index, uint16_t arg_count) {
    emit_opcode(function_opcode::call_function_fast);
    emit_u16(func_index);
    emit_u16(arg_count);
}

void vm_function_compiler::emit_return(bool has_value) {
    if (has_value) {
        emit_opcode(function_opcode::return_value);
    } else {
        emit_opcode(function_opcode::return_void);
    }
}

void vm_function_compiler::analyze_captures(block_stmt* body) {
    // In a full implementation, would walk the AST to find captured variables
    // For now, simplified version
    if (!current_function_) return;
    
    // Mark any referenced non-local variables as captures
    // This would require AST traversal
}

void vm_function_compiler::emit_capture(const std::string& var_name) {
    auto local_idx = find_local(var_name);
    if (local_idx != std::numeric_limits<uint16_t>::max()) {
        // Local variable - mark as captured
        locals_[local_idx].is_captured = true;
        
        captured_variable capture;
        capture.name = var_name;
        capture.outer_index = local_idx;
        capture.inner_index = static_cast<uint16_t>(current_function_->captures.size());
        current_function_->captures.push_back(capture);
        
        emit_opcode(function_opcode::capture_local);
        emit_u16(local_idx);
    }
}

void vm_function_compiler::compile_function_body(block_stmt* body) {
    // Delegate to main compiler for statement compilation
    compiler_.compile_statement(body);
}

void vm_function_compiler::emit_opcode(function_opcode op) {
    current_function_->bytecode.push_back(static_cast<uint8_t>(op));
}

void vm_function_compiler::emit_u16(uint16_t value) {
    current_function_->bytecode.push_back(value >> 8);
    current_function_->bytecode.push_back(value & 0xFF);
}

uint16_t vm_function_compiler::add_local(const std::string& name, bool is_parameter) {
    uint16_t index = static_cast<uint16_t>(locals_.size());
    
    local_info local;
    local.name = name;
    local.index = index;
    local.is_parameter = is_parameter;
    local.is_captured = false;
    
    locals_.push_back(local);
    local_indices_[name] = index;
    
    return index;
}

uint16_t vm_function_compiler::find_local(const std::string& name) const {
    auto it = local_indices_.find(name);
    if (it != local_indices_.end()) {
        return it->second;
    }
    return std::numeric_limits<uint16_t>::max();
}

// vm_function_executor implementation

vm_function_executor::vm_function_executor(vm_executor& parent_executor)
    : executor_(parent_executor) {
}

bool vm_function_executor::execute_function_opcode(function_opcode op, vm_context& ctx) {
    switch (op) {
    case function_opcode::define_function:
        execute_define_function(ctx);
        break;
    case function_opcode::end_function:
        execute_end_function(ctx);
        break;
    case function_opcode::call_function:
        execute_call_function(ctx);
        break;
    case function_opcode::call_function_fast:
        execute_call_function_fast(ctx);
        break;
    case function_opcode::call_bytecode_direct:
        execute_call_bytecode_direct(ctx);
        break;
    case function_opcode::call_closure:
        execute_call_closure(ctx);
        break;
    case function_opcode::return_value:
        execute_return_value(ctx);
        break;
    case function_opcode::return_void:
        execute_return_void(ctx);
        break;
    case function_opcode::create_closure:
        execute_create_closure(ctx);
        break;
    case function_opcode::capture_local:
        execute_capture_local(ctx);
        break;
    case function_opcode::load_capture:
        execute_load_capture(ctx);
        break;
    case function_opcode::store_capture:
        execute_store_capture(ctx);
        break;
    case function_opcode::load_function:
        execute_load_function(ctx);
        break;
    case function_opcode::builtin_call:
        execute_builtin_call(ctx);
        break;
    default:
        return false;
    }
    
    return true;
}

void vm_function_executor::execute_define_function(vm_context& ctx) {
    std::string func_name = pop_string(ctx);
    uint16_t param_count = read_u16(ctx);
    uint16_t bytecode_length = read_u16(ctx);
    
    // Create new function
    current_function_ = std::make_shared<vm_bytecode_function>();
    current_function_->name = func_name;
    
    // Read parameters
    for (uint16_t i = 0; i < param_count; ++i) {
        current_function_->parameters.push_back(pop_string(ctx));
    }
    
    // Read bytecode
    current_function_->bytecode.reserve(bytecode_length);
    for (uint16_t i = 0; i < bytecode_length; ++i) {
        current_function_->bytecode.push_back(ctx.read_byte());
    }
    
    // Read metadata
    current_function_->max_stack = read_u16(ctx);
    current_function_->max_locals = read_u16(ctx);
}

void vm_function_executor::execute_end_function(vm_context& ctx) {
    if (!current_function_) {
        throw std::runtime_error("End function without function definition");
    }
    
    // Register the function
    registry_->register_function(current_function_);
    
    // Push function reference onto stack
    ctx.push(vm_value::make_function(current_function_));
    
    current_function_ = nullptr;
}

void vm_function_executor::execute_call_function(vm_context& ctx) {
    std::string func_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get arguments from stack
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    std::reverse(args.begin(), args.end());
    
    // Look up function
    auto func = registry_->find_overload(func_name, args);
    if (!func) {
        // Try interpreter function fallback
        auto interp_func = executor_.find_interpreter_function(func_name);
        if (interp_func) {
            auto result = vm_function_bridge::call_interpreter_function(
                func_name, interp_func, args, ctx
            );
            ctx.push(result);
            return;
        }
        
        throw std::runtime_error("Function not found: " + func_name);
    }
    
    // Execute bytecode function
    push_call_frame(func, args, ctx);
}

void vm_function_executor::execute_call_function_fast(vm_context& ctx) {
    uint16_t func_index = read_u16(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get function by index
    auto func = registry_->get_by_index(func_index);
    if (!func) {
        throw std::runtime_error("Invalid function index: " + std::to_string(func_index));
    }
    
    // Get arguments
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    std::reverse(args.begin(), args.end());
    
    // Execute function
    push_call_frame(func, args, ctx);
}

void vm_function_executor::execute_call_bytecode_direct(vm_context& ctx) {
    // Direct bytecode-to-bytecode call
    // Function reference is on stack
    auto func_value = ctx.pop();
    if (!func_value.is_function()) {
        throw std::runtime_error("Expected function for direct call");
    }
    
    auto func = func_value.as_function();
    uint16_t arg_count = read_u16(ctx);
    
    // Get arguments
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    std::reverse(args.begin(), args.end());
    
    // Direct execution without interpreter callback
    push_call_frame(func, args, ctx);
}

void vm_function_executor::execute_call_closure(vm_context& ctx) {
    auto closure_value = ctx.pop();
    if (!closure_value.is_closure()) {
        throw std::runtime_error("Expected closure");
    }
    
    auto closure = closure_value.as_closure();
    uint16_t arg_count = read_u16(ctx);
    
    // Get arguments
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    std::reverse(args.begin(), args.end());
    
    // Execute closure
    auto result = closure->execute(ctx, args);
    ctx.push(result);
}

void vm_function_executor::execute_return_value(vm_context& ctx) {
    // Value to return is on stack
    auto return_value = ctx.pop();
    
    // Pop call frame
    pop_call_frame(ctx);
    
    // Push return value
    ctx.push(return_value);
    
    // Set PC to return address
    ctx.return_from_call();
}

void vm_function_executor::execute_return_void(vm_context& ctx) {
    // Pop call frame
    pop_call_frame(ctx);
    
    // No return value
    ctx.return_from_call();
}

void vm_function_executor::execute_return_to_bytecode(vm_context& ctx) {
    // Optimized return for bytecode-to-bytecode calls
    execute_return_value(ctx);
}

void vm_function_executor::execute_create_closure(vm_context& ctx) {
    auto func_value = ctx.pop();
    if (!func_value.is_function()) {
        throw std::runtime_error("Expected function for closure creation");
    }
    
    auto func = func_value.as_function();
    auto closure = func->create_closure(ctx);
    
    ctx.push(vm_value::make_closure(closure));
}

void vm_function_executor::execute_capture_local(vm_context& ctx) {
    uint16_t local_index = read_u16(ctx);
    
    // Capture the local variable
    // In a full implementation, would set up capture storage
}

void vm_function_executor::execute_load_capture(vm_context& ctx) {
    uint16_t capture_index = read_u16(ctx);
    auto value = ctx.get_captured(capture_index);
    ctx.push(value);
}

void vm_function_executor::execute_store_capture(vm_context& ctx) {
    uint16_t capture_index = read_u16(ctx);
    auto value = ctx.pop();
    ctx.set_captured(capture_index, value);
}

void vm_function_executor::execute_load_function(vm_context& ctx) {
    std::string func_name = pop_string(ctx);
    
    auto func = registry_->find_function(func_name);
    if (!func) {
        throw std::runtime_error("Function not found: " + func_name);
    }
    
    ctx.push(vm_value::make_function(func));
}

void vm_function_executor::execute_store_function(vm_context& ctx) {
    std::string var_name = pop_string(ctx);
    auto func_value = ctx.pop();
    
    // Store function in variable
    ctx.set_global(var_name, func_value);
}

void vm_function_executor::execute_builtin_call(vm_context& ctx) {
    std::string builtin_name = pop_string(ctx);
    uint16_t arg_count = read_u16(ctx);
    
    // Get arguments
    std::vector<vm_value> args;
    for (uint16_t i = 0; i < arg_count; ++i) {
        args.push_back(ctx.pop());
    }
    std::reverse(args.begin(), args.end());
    
    // Call built-in function
    auto result = executor_.call_builtin(builtin_name, args);
    ctx.push(result);
}

void vm_function_executor::push_call_frame(
    std::shared_ptr<vm_bytecode_function> func,
    const std::vector<vm_value>& args,
    vm_context& ctx
) {
    // Save current state and switch to function
    ctx.push_call_frame(func->max_locals);
    
    // Set up parameters
    for (size_t i = 0; i < args.size() && i < func->parameters.size(); ++i) {
        ctx.set_local(i, args[i]);
    }
    
    // Execute function bytecode
    ctx.execute_bytecode(func->bytecode, func->constants);
}

void vm_function_executor::pop_call_frame(vm_context& ctx) {
    ctx.pop_call_frame();
}

std::string vm_function_executor::pop_string(vm_context& ctx) {
    auto value = ctx.pop();
    return value.as_string();
}

uint16_t vm_function_executor::read_u16(vm_context& ctx) {
    uint8_t high = ctx.read_byte();
    uint8_t low = ctx.read_byte();
    return (high << 8) | low;
}

// function_inline_cache implementation

std::shared_ptr<vm_bytecode_function> function_inline_cache::check_cache(
    uint32_t call_site_id,
    const std::string& func_name,
    vm_function_registry* registry
) {
    auto it = entries.find(call_site_id);
    if (it != entries.end()) {
        auto& entry = it->second;
        if (entry.last_function_name == func_name && entry.cached_function) {
            entry.hit_count++;
            return entry.cached_function;
        }
        entry.miss_count++;
    }
    
    return nullptr;
}

void function_inline_cache::update_cache(
    uint32_t call_site_id,
    const std::string& func_name,
    uint16_t func_index,
    std::shared_ptr<vm_bytecode_function> func
) {
    auto& entry = entries[call_site_id];
    
    // Only cache if monomorphic
    if (entry.last_function_name.empty() || entry.last_function_name == func_name) {
        entry.last_function_name = func_name;
        entry.cached_index = func_index;
        entry.cached_function = func;
    } else {
        // Polymorphic - disable caching
        entry.cached_function = nullptr;
    }
}

// vm_function_bridge implementation

std::shared_ptr<vm_bytecode_function> vm_function_bridge::compile_to_bytecode(
    const std::string& name,
    script_function interp_func
) {
    // This would require full AST access to compile properly
    // For now, create a wrapper that calls back to interpreter
    auto vm_func = std::make_shared<vm_bytecode_function>();
    vm_func->name = name;
    
    // Mark as needing interpreter callback
    vm_func->bytecode.push_back(static_cast<uint8_t>(function_opcode::builtin_call));
    
    return vm_func;
}

script_function vm_function_bridge::create_interpreter_wrapper(
    std::shared_ptr<vm_bytecode_function> vm_func
) {
    return [vm_func](const std::vector<script_value>& args) -> script_value {
        // Create temporary VM context
        vm_context ctx;
        
        // Convert arguments
        std::vector<vm_value> vm_args;
        for (const auto& arg : args) {
            vm_args.push_back(vm_class_bridge::to_vm_value(arg));
        }
        
        // Execute VM function
        auto result = vm_func->execute(ctx, vm_args);
        
        // Convert result back
        return vm_class_bridge::to_script_value(result);
    };
}

vm_value vm_function_bridge::call_interpreter_function(
    const std::string& name,
    script_function func,
    const std::vector<vm_value>& args,
    vm_context& ctx
) {
    // Convert VM values to script values
    std::vector<script_value> script_args;
    for (const auto& arg : args) {
        script_args.push_back(vm_class_bridge::to_script_value(arg));
    }
    
    // Call interpreter function
    auto result = func(script_args);
    
    // Convert result back to VM value
    return vm_class_bridge::to_vm_value(result);
}

void vm_function_bridge::register_in_both(
    engine& js_engine,
    vm_function_registry& vm_registry,
    std::shared_ptr<vm_bytecode_function> func
) {
    // Register in VM registry
    vm_registry.register_function(func);
    
    // Create interpreter wrapper and register
    auto wrapper = create_interpreter_wrapper(func);
    js_engine.add_function(func->name, wrapper);
}

// vm_function_utils implementation

namespace vm_function_utils {

bool should_inline(const vm_bytecode_function* func) {
    if (!func || !func->is_inline_candidate) {
        return false;
    }
    
    // Heuristics for inlining
    const size_t MAX_INLINE_SIZE = 50;  // Max bytecode size
    const uint32_t MIN_CALL_COUNT = 100;  // Min calls before considering
    
    return func->bytecode.size() <= MAX_INLINE_SIZE &&
           func->call_count >= MIN_CALL_COUNT &&
           !func->has_side_effects;
}

void inline_function(
    std::vector<uint8_t>& bytecode,
    size_t call_site,
    const vm_bytecode_function* func
) {
    // Replace function call with inlined bytecode
    // This is a complex operation requiring bytecode rewriting
    // Simplified version here
    
    // Remove call instruction
    bytecode.erase(bytecode.begin() + call_site, bytecode.begin() + call_site + 3);
    
    // Insert function bytecode (excluding return)
    auto func_code = func->bytecode;
    if (!func_code.empty() && 
        func_code.back() == static_cast<uint8_t>(function_opcode::return_value)) {
        func_code.pop_back();
    }
    
    bytecode.insert(bytecode.begin() + call_site, func_code.begin(), func_code.end());
}

bool optimize_tail_call(
    std::vector<uint8_t>& bytecode,
    const vm_bytecode_function* func
) {
    if (bytecode.size() < 2) {
        return false;
    }
    
    // Check if last instructions are call + return
    size_t pos = bytecode.size() - 1;
    if (bytecode[pos] == static_cast<uint8_t>(function_opcode::return_value) && pos > 0) {
        uint8_t prev_op = bytecode[pos - 1];
        if (prev_op == static_cast<uint8_t>(function_opcode::call_function) ||
            prev_op == static_cast<uint8_t>(function_opcode::call_function_fast)) {
            // Replace with tail call
            bytecode[pos - 1] = static_cast<uint8_t>(function_opcode::tail_call);
            bytecode.pop_back();  // Remove return
            return true;
        }
    }
    
    return false;
}

void profile_function_call(
    vm_bytecode_function* func,
    uint64_t execution_cycles
) {
    // Update profiling data
    func->call_count++;
    
    // Simple heuristic for inline candidacy
    if (execution_cycles < 100 && func->bytecode.size() < 30) {
        func->is_inline_candidate = true;
    }
}

} // namespace vm_function_utils

} // namespace jvm
} // namespace jai