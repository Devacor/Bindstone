#include "../../include/jaiscript/jvm/virtual_machine.hpp"
#include "../../include/jaiscript/core/types.hpp"
#include "../../include/jaiscript/core/type_info.hpp"
#include "../../include/jaiscript/core/builtin_methods.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace jai {
namespace jvm {

    // Implementation details hidden from header
    struct virtual_machine::implementation {
        // Core VM state
        vm_state state;
        
        // Configuration
        bool debug_mode = false;
        bool bounds_checking = true;
        bool type_checking = true;
        bool has_custom_numeric_ops = false;
        
        // Built-in functions registry
        std::vector<native_function> builtin_functions;
        std::unordered_map<std::string, size_t> builtin_name_to_index;
        
        // Type converters (from engine)
        const std::unordered_map<std::string, std::function<script_value(const void*)>>* type_converters = nullptr;
        
        // Engine reference for script_value creation (enables shared conversion registry)
        std::weak_ptr<engine> engine_ref;
        
        // Breakpoints for debugging
        struct breakpoint {
            std::string function_name;
            size_t line_number;
        };
        std::vector<breakpoint> breakpoints;
        
        // Performance statistics
        execution_stats stats;
        std::chrono::high_resolution_clock::time_point execution_start;
        
        implementation() {
            // Reserve space for common VM state sizes
            state.stack.reserve(1024);
            state.locals.reserve(256);
            state.call_stack.reserve(64);
            state.loop_stack.reserve(32);
            
            // Register essential built-in functions
            register_essential_builtins();
        }
        
        void register_essential_builtins() {
            // Register print function
            auto print_func = [](const std::vector<script_value>& args) -> script_value {
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) std::cout << " ";
                    std::cout << args[i].to_string();
                }
                std::cout << std::endl;
                return script_value(std::monostate{}, impl_->engine_ref); // null/void return
            };
            
            builtin_functions.push_back(print_func);
            builtin_name_to_index["print"] = 0;
        }
    };
    
    virtual_machine::virtual_machine() 
        : impl_(std::make_unique<implementation>()) {
    }
    
    virtual_machine::~virtual_machine() = default;
    
    virtual_machine::virtual_machine(virtual_machine&&) noexcept = default;
    virtual_machine& virtual_machine::operator=(virtual_machine&&) noexcept = default;
    
    void virtual_machine::load_module(const module* mod) {
        impl_->state.current_module = mod; // Just store the pointer, don't own it
        impl_->state.has_error = false;
        impl_->state.error_message.clear();
    }
    
    const module* virtual_machine::get_current_module() const {
        return impl_->state.current_module;
    }
    
    script_value virtual_machine::execute() {
        if (!impl_->state.current_module) {
            runtime_error("No module loaded");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        if (impl_->debug_mode) {
            std::cout << "VM execute() called. Main function index: " 
                      << impl_->state.current_module->main_function << "\n";
        }
        
        return execute_function(impl_->state.current_module->main_function);
    }
    
    script_value virtual_machine::execute_function(size_t function_index, const std::vector<script_value>& args) {
        if (!impl_->state.current_module) {
            runtime_error("No module loaded");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        if (function_index >= impl_->state.current_module->functions.size()) {
            runtime_error("Function index out of bounds");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        const auto& func = impl_->state.current_module->functions[function_index];
        return execute_function_impl(func.get(), args);
    }
    
    script_value virtual_machine::execute_function(const std::string& function_name, const std::vector<script_value>& args) {
        if (!impl_->state.current_module) {
            runtime_error("No module loaded");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        // Find function by name
        for (size_t i = 0; i < impl_->state.current_module->functions.size(); ++i) {
            if (impl_->state.current_module->functions[i]->name == function_name) {
                return execute_function(i, args);
            }
        }
        
        runtime_error("Function '" + function_name + "' not found");
        return script_value(std::monostate{}, impl_->engine_ref);
    }
    
    script_value virtual_machine::execute_function_impl(const function* func, const std::vector<script_value>& args) {
        // Start timing
        impl_->execution_start = std::chrono::high_resolution_clock::now();
        
        if (impl_->debug_mode) {
            std::cout << "VM execute_function_impl: " << func->name 
                      << ", instructions: " << func->instructions.size() << "\n";
        }
        
        // Validate arguments
        if (args.size() != func->parameter_names.size() && !func->is_variadic) {
            runtime_error("Function '" + func->name + "' expects " + 
                         std::to_string(func->parameter_names.size()) + " arguments, got " + 
                         std::to_string(args.size()));
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        // Clear previous state
        impl_->state.stack.clear();
        impl_->state.call_stack.clear();
        impl_->state.loop_stack.clear();
        impl_->state.has_error = false;
        
        // Setup locals with parameters
        impl_->state.locals.clear();
        impl_->state.locals.resize(func->local_count);
        
        // Copy arguments to local slots
        for (size_t i = 0; i < args.size(); ++i) {
            impl_->state.locals[i] = args[i];
        }
        
        // Create initial call frame
        impl_->state.call_stack.emplace_back(func, 0, 0);
        
        // Execute the main loop
        script_value result = run();
        
        // Update statistics
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - impl_->execution_start);
        impl_->stats.execution_time_ms += duration.count() / 1000.0;
        impl_->stats.function_calls++;
        
        return result;
    }
    
    script_value virtual_machine::call_function_nested(size_t function_index, const std::vector<script_value>& args) {
        if (impl_->debug_mode) {
            std::cout << "=== call_function_nested: index=" << function_index 
                      << ", args=" << args.size() << " ===\n";
        }
        
        if (!impl_->state.current_module) {
            runtime_error("No module loaded");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        if (function_index >= impl_->state.current_module->functions.size()) {
            runtime_error("Function index out of bounds");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        const auto& func = impl_->state.current_module->functions[function_index];
        
        // Validate arguments
        if (args.size() != func->parameter_names.size() && !func->is_variadic) {
            runtime_error("Function '" + func->name + "' expects " + 
                         std::to_string(func->parameter_names.size()) + " arguments, got " + 
                         std::to_string(args.size()));
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        // Save current locals count (we'll restore it later)
        size_t saved_locals_size = impl_->state.locals.size();
        
        // Extend locals array for the new function's locals
        impl_->state.locals.resize(saved_locals_size + func->local_count);
        
        // Copy arguments to the new function's local slots
        for (size_t i = 0; i < args.size(); ++i) {
            impl_->state.locals[saved_locals_size + i] = args[i];
        }
        
        // Create new call frame
        impl_->state.call_stack.emplace_back(func.get(), 0, static_cast<uint8_t>(saved_locals_size));
        
        // Execute until this function returns
        script_value result;
        while (!impl_->state.call_stack.empty() && !impl_->state.has_error) {
            auto& frame = impl_->state.call_stack.back();
            
            // Check if this is our function frame
            if (frame.func != func.get()) {
                // This is a different function frame, continue normal execution
                execute_instruction(frame.func->instructions[frame.ip]);
                frame.ip++;
                continue;
            }
            
            // Check bounds
            if (frame.ip >= func->instructions.size()) {
                // Function ended without explicit return
                impl_->state.call_stack.pop_back();
                result = script_value(std::monostate{}, impl_->engine_ref); // null return
                break;
            }
            
            const auto& instr = func->instructions[frame.ip];
            frame.ip++; // Advance instruction pointer
            impl_->stats.instructions_executed++;
            
            // Execute instruction
            execute_instruction(instr);
            
            // Check if function returned
            if (impl_->state.call_stack.empty() || impl_->state.call_stack.back().func != func.get()) {
                // Function returned, get result from stack
                if (impl_->debug_mode) {
                    std::cout << "Function " << func->name << " returned. Stack size: " 
                              << impl_->state.stack.size() << "\n";
                }
                result = impl_->state.stack.empty() ? script_value(std::monostate{}, impl_->engine_ref) : pop();
                break;
            }
        }
        
        // Restore locals array size
        impl_->state.locals.resize(saved_locals_size);
        
        return result;
    }
    
    script_value virtual_machine::run() {
        if (impl_->state.call_stack.empty()) {
            runtime_error("Empty call stack");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        while (!impl_->state.call_stack.empty() && !impl_->state.has_error) {
            auto& frame = impl_->state.call_stack.back();
            const auto& func = *frame.func;
            
            // Check bounds
            if (frame.ip >= func.instructions.size()) {
                // End of function reached without explicit return
                if (impl_->debug_mode) {
                    std::cout << "Function " << func.name << " reached end. Stack size: " 
                              << impl_->state.stack.size() << "\n";
                }
                impl_->state.call_stack.pop_back();
                if (impl_->state.call_stack.empty()) {
                    // Main function ended, return top of stack if available
                    // This handles implicit returns where the last expression's value is on the stack
                    if (impl_->debug_mode) {
                        std::cout << "Main function ended. Returning from stack.\n";
                    }
                    return impl_->state.stack.empty() ? script_value(std::monostate{}, impl_->engine_ref) : pop();
                }
                continue;
            }
            
            const auto& instr = func.instructions[frame.ip];
            frame.ip++; // Advance instruction pointer
            impl_->stats.instructions_executed++;
            
            // Debug breakpoint check
            if (impl_->debug_mode && !impl_->breakpoints.empty()) {
                size_t current_line = frame.ip < func.line_numbers.size() ? 
                                    func.line_numbers[frame.ip - 1] : 0;
                
                for (const auto& bp : impl_->breakpoints) {
                    if (bp.function_name == func.name && bp.line_number == current_line) {
                        // Breakpoint hit - would pause execution in a real debugger
                        // For now, just continue
                    }
                }
            }
            
            // Execute instruction
            if (impl_->debug_mode && instr.op == opcode::RETURN_VALUE) {
                std::cout << "About to execute RETURN_VALUE. Stack size before: " 
                          << impl_->state.stack.size() << "\n";
            }
            execute_instruction(instr);
            if (impl_->debug_mode && instr.op == opcode::RETURN_VALUE) {
                std::cout << "After RETURN_VALUE. Stack size: " 
                          << impl_->state.stack.size() 
                          << ", call stack size: " << impl_->state.call_stack.size() << "\n";
            }
        }
        
        // If we exit due to error, return null
        if (impl_->state.has_error) {
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        // Return top of stack if available
        if (impl_->debug_mode) {
            std::cout << "VM run() completed. Final stack size: " << impl_->state.stack.size() << "\n";
            if (!impl_->state.stack.empty()) {
                std::cout << "Returning top of stack, type: " << static_cast<int>(impl_->state.stack.back().type()) << "\n";
            }
        }
        return impl_->state.stack.empty() ? script_value(std::monostate{}, impl_->engine_ref) : pop();
    }
    
    void virtual_machine::execute_instruction(const instruction& instr) {
        switch (instr.op) {
            // Stack operations
            case opcode::NOP:
                break;
                
            case opcode::POP:
                if (!impl_->state.stack.empty()) {
                    impl_->state.stack.pop_back();
                }
                break;
                
            case opcode::DUP:
                if (!impl_->state.stack.empty()) {
                    push(impl_->state.stack.back());
                }
                break;
                
            case opcode::SWAP:
                if (impl_->state.stack.size() >= 2) {
                    std::swap(impl_->state.stack[impl_->state.stack.size() - 1],
                             impl_->state.stack[impl_->state.stack.size() - 2]);
                }
                break;
                
            // Constants
            case opcode::PUSH_NULL:
                push(script_value(std::monostate{}, impl_->engine_ref));
                break;
                
            case opcode::PUSH_TRUE:
                push(script_value(true));
                break;
                
            case opcode::PUSH_FALSE:
                push(script_value(false));
                break;
                
            case opcode::PUSH_INT:
                push(script_value(static_cast<script_int>(instr.int_operand)));
                if (impl_->debug_mode) {
                    std::cout << "PUSH_INT: " << instr.int_operand << " (stack size: " 
                              << impl_->state.stack.size() << ")\n";
                }
                break;
                
            case opcode::PUSH_FLOAT:
                push(script_value(instr.float_operand));
                break;
                
            case opcode::PUSH_CHAR:
                push(script_value(static_cast<char>(instr.byte_operand)));
                break;
                
            case opcode::PUSH_STRING:
                if (impl_->state.current_module && 
                    instr.short_operand < impl_->state.current_module->string_constants.size()) {
                    push(script_value(impl_->state.current_module->string_constants[instr.short_operand]));
                } else {
                    runtime_error("String constant index out of bounds");
                }
                break;
                
            case opcode::PUSH_FUNCTION:
                if (impl_->state.current_module && 
                    instr.short_operand < impl_->state.current_module->functions.size()) {
                    // Create a callable that captures the function index
                    uint16_t func_index = instr.short_operand;
                    auto vm_func = [this, func_index](const std::vector<script_value>& args) -> script_value {
                        if (impl_->debug_mode) {
                            std::cout << "Calling function index " << func_index << " with " << args.size() << " args\n";
                        }
                        auto result = call_function_nested(func_index, args);
                        if (impl_->debug_mode) {
                            std::cout << "Function returned, result type: " << static_cast<int>(result.type()) << "\n";
                        }
                        return result;
                    };
                    // Use the stored engine reference for proper conversion registry access
                    push(script_value::make_function(vm_func, impl_->engine_ref));
                    if (impl_->debug_mode) {
                        std::cout << "PUSH_FUNCTION: index=" << func_index 
                                  << " name=" << impl_->state.current_module->functions[func_index]->name << "\n";
                    }
                } else {
                    runtime_error("Function index out of bounds");
                }
                break;
                
            // Variable operations
            case opcode::LOAD_LOCAL:
                if (instr.byte_operand < impl_->state.locals.size()) {
                    // Load reference to local variable instead of copying
                    push(script_value::make_reference(&impl_->state.locals[instr.byte_operand], impl_->state.global_env));
                } else {
                    runtime_error("Local variable index out of bounds");
                }
                break;
                
            case opcode::STORE_LOCAL:
                if (instr.byte_operand < impl_->state.locals.size()) {
                    if (!impl_->state.stack.empty()) {
                        script_value value = pop();
                        // Store the value directly (no cloning for containers)
                        // This preserves shared ownership of arrays/maps/objects
                        if (value.is_reference()) {
                            // If storing a reference, dereference first
                            impl_->state.locals[instr.byte_operand] = value.deref();
                        } else {
                            impl_->state.locals[instr.byte_operand] = value;
                        }
                    } else {
                        runtime_error("Stack underflow in STORE_LOCAL");
                    }
                } else {
                    runtime_error("Local variable index out of bounds");
                }
                break;
                
            case opcode::LOAD_GLOBAL:
                if (impl_->state.current_module && 
                    instr.short_operand < impl_->state.current_module->global_names.size()) {
                    const std::string& name = impl_->state.current_module->global_names[instr.short_operand];
                    try {
                        // Load reference to global variable instead of copying
                        script_value& global_val = impl_->state.global_env->get_ref(name);
                        push(script_value::make_reference(&global_val, impl_->state.global_env));
                    } catch (...) {
                        runtime_error("Global variable '" + name + "' not found");
                    }
                } else {
                    runtime_error("Global variable index out of bounds");
                }
                break;
                
            case opcode::STORE_GLOBAL:
                if (impl_->state.current_module && 
                    instr.short_operand < impl_->state.current_module->global_names.size()) {
                    const std::string& name = impl_->state.current_module->global_names[instr.short_operand];
                    if (!impl_->state.stack.empty()) {
                        script_value value = pop();
                        if (impl_->state.global_env) {
                            // Store the value directly (no cloning for containers)
                            if (value.is_reference()) {
                                // If storing a reference, dereference first
                                impl_->state.global_env->define(name, value.deref());
                            } else {
                                impl_->state.global_env->define(name, value);
                            }
                            if (impl_->debug_mode) {
                                std::cout << "STORE_GLOBAL: " << name << " = " << value.to_string()
                                          << " (is_function: " << value.is_function() << ")\n";
                            }
                        } else {
                            runtime_error("Global environment not set");
                        }
                    } else {
                        runtime_error("Stack underflow in STORE_GLOBAL");
                    }
                } else {
                    runtime_error("Global variable index out of bounds");
                }
                break;
                
            // Reference operations
            case opcode::MAKE_REFERENCE:
                {
                    // MAKE_REFERENCE creates a reference to a variable
                    // Operand determines the type: 0=local, 1=global
                    uint8_t ref_type = instr.byte_operand;
                    uint16_t index = instr.short_operand;
                    
                    if (ref_type == 0) {
                        // Reference to local variable
                        if (index < impl_->state.locals.size()) {
                            push(script_value::make_reference(&impl_->state.locals[index], impl_->state.global_env));
                        } else {
                            runtime_error("Local variable index out of bounds for MAKE_REFERENCE");
                        }
                    } else if (ref_type == 1) {
                        // Reference to global variable
                        if (impl_->state.current_module && index < impl_->state.current_module->global_names.size()) {
                            const std::string& name = impl_->state.current_module->global_names[index];
                            // Get pointer to the value in the environment
                            // Note: This is a simplified approach - in production, we'd need to ensure
                            // the environment keeps values at stable addresses
                            try {
                                script_value& global_val = impl_->state.global_env->get_ref(name);
                                push(script_value::make_reference(&global_val, impl_->state.global_env));
                            } catch (...) {
                                runtime_error("Global variable '" + name + "' not found for MAKE_REFERENCE");
                            }
                        } else {
                            runtime_error("Global variable index out of bounds for MAKE_REFERENCE");
                        }
                    } else {
                        runtime_error("Invalid reference type in MAKE_REFERENCE");
                    }
                }
                break;
                
            case opcode::LOAD_REFERENCE:
                {
                    // LOAD_REFERENCE dereferences a reference value
                    if (impl_->state.stack.empty()) {
                        runtime_error("Stack underflow in LOAD_REFERENCE");
                        break;
                    }
                    script_value ref = pop();
                    if (ref.is_reference()) {
                        push(ref.deref());
                    } else {
                        // Not a reference, push back as-is
                        push(ref);
                    }
                }
                break;
                
            case opcode::STORE_REFERENCE:
                {
                    // STORE_REFERENCE stores a value through a reference
                    if (impl_->state.stack.size() < 2) {
                        runtime_error("Stack underflow in STORE_REFERENCE");
                        break;
                    }
                    script_value value = pop();
                    script_value ref = pop();
                    
                    if (ref.is_reference()) {
                        ref.assign_through(value);
                    } else {
                        runtime_error("STORE_REFERENCE on non-reference value");
                    }
                }
                break;
                
            // Arithmetic operations
            case opcode::ADD:
            case opcode::SUB:
            case opcode::MUL:
            case opcode::DIV:
            case opcode::MOD:
                execute_arithmetic_op(instr.op);
                break;
                
            case opcode::NEG:
                if (!impl_->state.stack.empty()) {
                    script_value operand = pop();
                    if (operand.is_int()) {
                        push(script_value(-operand.as_int()));
                    } else if (operand.is_float()) {
                        push(script_value(-operand.as_float()));
                    } else {
                        runtime_error("Cannot negate non-numeric value");
                    }
                } else {
                    runtime_error("Stack underflow in NEG");
                }
                break;
                
            // Comparison operations
            case opcode::EQ:
            case opcode::NE:
            case opcode::LT:
            case opcode::LE:
            case opcode::GT:
            case opcode::GE:
            case opcode::CMP:
                execute_comparison_op(instr.op);
                break;
                
            // Control flow
            case opcode::JUMP:
                {
                    auto& frame = impl_->state.call_stack.back();
                    frame.ip = instr.short_operand;
                }
                break;
                
            case opcode::JUMP_IF_FALSE:
                if (!impl_->state.stack.empty()) {
                    script_value condition = pop();
                    // Auto-dereference for conditional evaluation
                    const script_value& condition_val = condition.deref();
                    bool is_truthy = condition_val.is_bool() ? condition_val.as_bool() :
                                   condition_val.is_int() ? (condition_val.as_int() != 0) :
                                   condition_val.is_float() ? (condition_val.as_float() != 0.0) :
                                   !condition_val.is_null();
                    
                    if (!is_truthy) {
                        auto& frame = impl_->state.call_stack.back();
                        frame.ip = instr.short_operand;
                    }
                } else {
                    runtime_error("Stack underflow in JUMP_IF_FALSE");
                }
                break;
                
            case opcode::JUMP_IF_TRUE:
                if (!impl_->state.stack.empty()) {
                    script_value condition = pop();
                    bool is_truthy = condition.is_bool() ? condition.as_bool() :
                                   condition.is_int() ? (condition.as_int() != 0) :
                                   condition.is_float() ? (condition.as_float() != 0.0) :
                                   !condition.is_null();
                    
                    if (is_truthy) {
                        auto& frame = impl_->state.call_stack.back();
                        frame.ip = instr.short_operand;
                    }
                } else {
                    runtime_error("Stack underflow in JUMP_IF_TRUE");
                }
                break;
                
            // Function operations
            case opcode::CALL:
                execute_function_call(instr.byte_operand);
                break;
                
            case opcode::CALL_BUILTIN:
                execute_builtin_call(instr.short_operand, instr.byte_operand);
                break;
                
            case opcode::RETURN:
                impl_->state.call_stack.pop_back();
                break;
                
            case opcode::RETURN_VALUE:
                // Value should already be on stack
                // For nested function calls, we need to preserve the return value
                // The call_function_nested method expects the return value on the stack
                if (impl_->debug_mode) {
                    std::cout << "RETURN_VALUE: stack size=" << impl_->state.stack.size();
                    if (!impl_->state.stack.empty()) {
                        std::cout << ", top value type=" << static_cast<int>(impl_->state.stack.back().type());
                    }
                    std::cout << "\n";
                }
                impl_->state.call_stack.pop_back();
                break;
                
            // Array operations
            case opcode::NEW_ARRAY:
                {
                    // Create empty array with correct capacity
                    auto element_type = type_info::make_int(); // TODO: Better type inference
                    script_value arrayValue = script_value::make_array(element_type);
                    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
                    array.reserve(instr.byte_operand);
                    push(std::move(arrayValue));
                }
                break;
                
            case opcode::ARRAY_GET:
                execute_array_op(instr.op);
                break;
                
            case opcode::ARRAY_SET:
                execute_array_op(instr.op);
                break;
                
            // Map operations
            case opcode::NEW_MAP:
                {
                    // Create a map with void types (allows any key/value)
                    auto key_type = type_info::make_void();
                    auto value_type = type_info::make_void();
                    script_value mapValue = script_value::make_map(key_type, value_type);
                    push(std::move(mapValue));
                }
                break;
                
            case opcode::MAP_GET:
                {
                    if (impl_->state.stack.size() < 2) {
                        runtime_error("Stack underflow in MAP_GET");
                        break;
                    }
                    script_value key = pop();
                    
                    // Get reference to the map on stack instead of popping it
                    if (impl_->state.stack.empty()) {
                        runtime_error("Stack underflow - missing map in MAP_GET");
                        break;
                    }
                    
                    script_value& map = impl_->state.stack.back();
                    
                    if (!map.is_map()) {
                        runtime_error("MAP_GET on non-map value");
                        pop(); // Remove the map
                        push(script_value(std::monostate{}, impl_->engine_ref)); // Push null
                        break;
                    }
                    
                    try {
                        // For now, simplify by just returning the value (no reference)
                        // This is a temporary fix until we can properly implement
                        // references to map elements that keep the container alive
                        const auto& mapData = map.as_map();
                        auto it = mapData.find(key);
                        
                        // Pop the map 
                        pop();
                        
                        if (it != mapData.end()) {
                            push(it->second);
                        } else {
                            push(script_value(std::monostate{}, impl_->engine_ref)); // Key not found, push null
                        }
                    } catch (const std::exception& e) {
                        pop(); // Remove the map
                        runtime_error("Error accessing map: " + std::string(e.what()));
                        push(script_value(std::monostate{}, impl_->engine_ref));
                    }
                }
                break;
                
            case opcode::MAP_SET:
                {
                    if (impl_->state.stack.size() < 3) {
                        runtime_error("Stack underflow in MAP_SET");
                        break;
                    }
                    // Stack: [map, key, value]
                    script_value value = pop();
                    script_value key = pop();
                    script_value map = pop();
                    
                    // Dereference if it's a reference
                    script_value& actual_map = map.deref();
                    
                    if (!actual_map.is_map()) {
                        runtime_error("MAP_SET on non-map value");
                        break;
                    }
                    
                    try {
                        auto& mapData = const_cast<std::map<script_value, script_value>&>(actual_map.as_map());
                        mapData[key] = value;
                        // Push the map back (preserving reference if it was one)
                        push(map);
                    } catch (...) {
                        runtime_error("Error setting map value");
                    }
                }
                break;
                
            // Method call operation
            case opcode::CALL_METHOD:
                {
                    // Extract method name index and arg count from packed long_operand
                    uint16_t method_name_index = static_cast<uint16_t>(instr.long_operand >> 16);
                    uint8_t arg_count = static_cast<uint8_t>(instr.long_operand & 0xFF);
                    
                    // Get method name from constant table
                    if (!impl_->state.current_module) {
                        runtime_error("No current module in CALL_METHOD");
                        break;
                    }
                    
                    if (method_name_index >= impl_->state.current_module->string_constants.size()) {
                        runtime_error("Invalid method name index: " + std::to_string(method_name_index) + 
                                     " >= " + std::to_string(impl_->state.current_module->string_constants.size()));
                        break;
                    }
                    const std::string& method_name = impl_->state.current_module->string_constants[method_name_index];
                    
                    // Check we have enough values on stack (object + args)
                    if (impl_->state.stack.size() < arg_count + 1) {
                        runtime_error("Stack underflow in CALL_METHOD");
                        break;
                    }
                    
                    // Extract arguments
                    std::vector<script_value> args;
                    args.reserve(arg_count);
                    for (uint8_t i = 0; i < arg_count; ++i) {
                        args.insert(args.begin(), pop()); // Insert at beginning to maintain order
                    }
                    
                    // Get the object (this will be a reference now)
                    script_value object = pop();
                    
                    // For builtin methods, we need to handle references specially
                    // Instead of dereferencing first, check if it's a reference and handle appropriately
                    script_value* target_object = &object;
                    if (object.is_reference()) {
                        // For references, we want to pass the original referenced object
                        // to ensure mutations persist to the actual storage location
                        target_object = &object.deref();
                    }
                    
                    // Handle built-in methods for arrays
                    if (target_object->is_array()) {
                        auto method = builtin_method_registry::instance().find_array_method(method_name);
                        if (method) {
                            try {
                                // Call the built-in method with the target object
                                // This ensures mutations are applied to the actual stored object
                                script_value result = method(nullptr, *target_object, args);
                                push(result);
                            } catch (const std::exception& e) {
                                runtime_error("Array method '" + method_name + "' failed: " + e.what());
                            }
                        } else {
                            runtime_error("Array has no method '" + method_name + "'");
                        }
                    }
                    // Handle built-in methods for maps
                    else if (target_object->is_map()) {
                        auto method = builtin_method_registry::instance().find_map_method(method_name);
                        if (method) {
                            try {
                                // Call the built-in method with the target object
                                script_value result = method(nullptr, *target_object, args);
                                push(result);
                            } catch (const std::exception& e) {
                                runtime_error("Map method '" + method_name + "' failed: " + e.what());
                            }
                        } else {
                            runtime_error("Map has no method '" + method_name + "'");
                        }
                    }
                    // Handle object methods
                    else if (target_object->is_object()) {
                        // TODO: Implement object method calls
                        runtime_error("Object method calls not yet implemented in VM");
                    }
                    else {
                        runtime_error("Cannot call method '" + method_name + "' on non-object type");
                    }
                }
                break;
                
            // Debug operations
            case opcode::DEBUG_LINE:
                // Store current line number for debugging
                break;
                
            default:
                runtime_error("Unknown opcode: " + std::to_string(static_cast<int>(instr.op)));
                break;
        }
    }
    
    // Helper methods
    void virtual_machine::push(const script_value& value) {
        impl_->state.stack.push_back(value);
    }
    
    void virtual_machine::push(script_value&& value) {
        impl_->state.stack.push_back(std::move(value));
    }
    
    script_value virtual_machine::pop() {
        if (impl_->state.stack.empty()) {
            runtime_error("Stack underflow");
            return script_value(std::monostate{}, impl_->engine_ref);
        }
        
        script_value result = std::move(impl_->state.stack.back());
        impl_->state.stack.pop_back();
        return result;
    }
    
    script_value& virtual_machine::peek(size_t offset) {
        if (offset >= impl_->state.stack.size()) {
            runtime_error("Stack peek out of bounds");
            static script_value null_value;
            return null_value;
        }
        
        return impl_->state.stack[impl_->state.stack.size() - 1 - offset];
    }
    
    void virtual_machine::runtime_error(const std::string& message) {
        impl_->state.has_error = true;
        impl_->state.error_message = message;
        
        // Get current line number if available
        if (!impl_->state.call_stack.empty()) {
            const auto& frame = impl_->state.call_stack.back();
            if (frame.ip < frame.func->line_numbers.size()) {
                impl_->state.error_line = frame.func->line_numbers[frame.ip];
            }
        }
    }
    
    void virtual_machine::runtime_error(const std::string& message, size_t line_number) {
        impl_->state.has_error = true;
        impl_->state.error_message = message;
        impl_->state.error_line = line_number;
    }
    
    // Public interface methods
    void virtual_machine::set_global_environment(std::shared_ptr<environment> env) {
        impl_->state.global_env = env;
    }
    
    void virtual_machine::set_global(const std::string& name, const script_value& value) {
        if (impl_->state.global_env) {
            impl_->state.global_env->define(name, value);
        }
    }
    
    script_value virtual_machine::get_global(const std::string& name) const {
        if (impl_->state.global_env) {
            try {
                return impl_->state.global_env->get(name);
            } catch (...) {
                return script_value(std::monostate{}, impl_->engine_ref);
            }
        }
        return script_value(std::monostate{}, impl_->engine_ref);
    }
    
    bool virtual_machine::has_global(const std::string& name) const {
        if (impl_->state.global_env) {
            return impl_->state.global_env->contains(name);
        }
        return false;
    }
    
    void virtual_machine::register_builtin(const std::string& name, native_function func) {
        impl_->builtin_functions.push_back(func);
        impl_->builtin_name_to_index[name] = impl_->builtin_functions.size() - 1;
    }
    
    void virtual_machine::set_type_converters(const std::unordered_map<std::string, std::function<script_value(const void*)>>* converters) {
        impl_->type_converters = converters;
    }
    
    void virtual_machine::set_has_custom_numeric_ops(bool value) {
        impl_->has_custom_numeric_ops = value;
    }
    
    void virtual_machine::set_bounds_checking_enabled(bool enabled) {
        impl_->bounds_checking = enabled;
    }
    
    void virtual_machine::set_type_checking_enabled(bool enabled) {
        impl_->type_checking = enabled;
    }
    
    void virtual_machine::set_debug_mode(bool enabled) {
        impl_->debug_mode = enabled;
    }
    
    bool virtual_machine::has_error() const {
        return impl_->state.has_error;
    }
    
    std::string virtual_machine::get_error_message() const {
        return impl_->state.error_message;
    }
    
    void virtual_machine::clear_error() {
        impl_->state.has_error = false;
        impl_->state.error_message.clear();
        impl_->state.error_line = 0;
    }
    
    virtual_machine::execution_stats virtual_machine::get_stats() const {
        return impl_->stats;
    }
    
    void virtual_machine::reset_stats() {
        impl_->stats = execution_stats{};
    }
    
    // Arithmetic operation implementations
    void virtual_machine::execute_arithmetic_op(opcode op) {
        if (impl_->state.stack.size() < 2) {
            runtime_error("Stack underflow in arithmetic operation");
            return;
        }
        
        script_value right = pop();
        script_value left = pop();
        
        // Auto-dereference for arithmetic operations
        const script_value& left_val = left.deref();
        const script_value& right_val = right.deref();
        
        // Fast path for integer arithmetic
        if (left_val.is_int() && right_val.is_int() && !impl_->has_custom_numeric_ops) {
            script_int l = left_val.as_int();
            script_int r = right_val.as_int();
            
            switch (op) {
                case opcode::ADD: push(script_value(l + r)); return;
                case opcode::SUB: push(script_value(l - r)); return;
                case opcode::MUL: push(script_value(l * r)); return;
                case opcode::DIV:
                    if (r == 0) {
                        runtime_error("Division by zero");
                        return;
                    }
                    push(script_value(l / r));
                    return;
                case opcode::MOD:
                    if (r == 0) {
                        runtime_error("Division by zero");
                        return;
                    }
                    push(script_value(l % r));
                    return;
                default: break;
            }
        }
        
        // Mixed or floating point arithmetic
        script_float l_val_float, r_val_float;
        if (left_val.is_int()) l_val_float = static_cast<script_float>(left_val.as_int());
        else if (left_val.is_float()) l_val_float = left_val.as_float();
        else {
            runtime_error("Left operand must be numeric");
            return;
        }
        
        if (right_val.is_int()) r_val_float = static_cast<script_float>(right_val.as_int());
        else if (right_val.is_float()) r_val_float = right_val.as_float();
        else {
            runtime_error("Right operand must be numeric");
            return;
        }
        
        switch (op) {
            case opcode::ADD: push(script_value(l_val_float + r_val_float)); break;
            case opcode::SUB: push(script_value(l_val_float - r_val_float)); break;
            case opcode::MUL: push(script_value(l_val_float * r_val_float)); break;
            case opcode::DIV:
                if (r_val_float == 0.0) {
                    runtime_error("Division by zero");
                    return;
                }
                push(script_value(l_val_float / r_val_float));
                break;
            case opcode::MOD:
                if (r_val_float == 0.0) {
                    runtime_error("Division by zero");
                    return;
                }
                push(script_value(std::fmod(l_val_float, r_val_float)));
                break;
            default:
                runtime_error("Unknown arithmetic operator");
        }
    }
    
    void virtual_machine::execute_comparison_op(opcode op) {
        if (impl_->state.stack.size() < 2) {
            runtime_error("Stack underflow in comparison operation");
            return;
        }
        
        script_value right = pop();
        script_value left = pop();
        
        // Auto-dereference for comparison operations
        const script_value& left_val = left.deref();
        const script_value& right_val = right.deref();
        
        // Handle different type combinations
        if (left_val.type() == right_val.type()) {
            // Same types - direct comparison
            switch (op) {
                case opcode::EQ:
                    push(script_value(left_val == right_val));
                    break;
                case opcode::NE:
                    push(script_value(!(left_val == right_val)));
                    break;
                case opcode::LT:
                    if (left_val.is_int() && right_val.is_int()) {
                        push(script_value(left_val.as_int() < right_val.as_int()));
                    } else if ((left_val.is_int() || left_val.is_float()) && (right_val.is_int() || right_val.is_float())) {
                        script_float l = left_val.is_int() ? static_cast<script_float>(left_val.as_int()) : left_val.as_float();
                        script_float r = right_val.is_int() ? static_cast<script_float>(right_val.as_int()) : right_val.as_float();
                        push(script_value(l < r));
                    } else {
                        runtime_error("Cannot compare non-numeric types with <");
                    }
                    break;
                case opcode::LE:
                    if (left_val.is_int() && right_val.is_int()) {
                        push(script_value(left_val.as_int() <= right_val.as_int()));
                    } else if ((left_val.is_int() || left_val.is_float()) && (right_val.is_int() || right_val.is_float())) {
                        script_float l = left_val.is_int() ? static_cast<script_float>(left_val.as_int()) : left_val.as_float();
                        script_float r = right_val.is_int() ? static_cast<script_float>(right_val.as_int()) : right_val.as_float();
                        push(script_value(l <= r));
                    } else {
                        runtime_error("Cannot compare non-numeric types with <=");
                    }
                    break;
                case opcode::GT:
                    if (left_val.is_int() && right_val.is_int()) {
                        push(script_value(left_val.as_int() > right_val.as_int()));
                    } else if ((left_val.is_int() || left_val.is_float()) && (right_val.is_int() || right_val.is_float())) {
                        script_float l = left_val.is_int() ? static_cast<script_float>(left_val.as_int()) : left_val.as_float();
                        script_float r = right_val.is_int() ? static_cast<script_float>(right_val.as_int()) : right_val.as_float();
                        push(script_value(l > r));
                    } else {
                        runtime_error("Cannot compare non-numeric types with >");
                    }
                    break;
                case opcode::GE:
                    if (left_val.is_int() && right_val.is_int()) {
                        push(script_value(left_val.as_int() >= right_val.as_int()));
                    } else if ((left_val.is_int() || left_val.is_float()) && (right_val.is_int() || right_val.is_float())) {
                        script_float l = left_val.is_int() ? static_cast<script_float>(left_val.as_int()) : left_val.as_float();
                        script_float r = right_val.is_int() ? static_cast<script_float>(right_val.as_int()) : right_val.as_float();
                        push(script_value(l >= r));
                    } else {
                        runtime_error("Cannot compare non-numeric types with >=");
                    }
                    break;
                case opcode::CMP:
                    // Three-way comparison (spaceship operator)
                    if (left_val.is_int() && right_val.is_int()) {
                        script_int l = left_val.as_int();
                        script_int r = right_val.as_int();
                        push(script_value(l < r ? script_int(-1) : (l > r ? script_int(1) : script_int(0))));
                    } else if ((left_val.is_int() || left_val.is_float()) && (right_val.is_int() || right_val.is_float())) {
                        script_float l = left_val.is_int() ? static_cast<script_float>(left_val.as_int()) : left_val.as_float();
                        script_float r = right_val.is_int() ? static_cast<script_float>(right_val.as_int()) : right_val.as_float();
                        push(script_value(l < r ? script_int(-1) : (l > r ? script_int(1) : script_int(0))));
                    } else {
                        runtime_error("Cannot use spaceship operator on non-numeric types");
                    }
                    break;
                default:
                    runtime_error("Unknown comparison operator");
            }
        } else {
            // Mixed types - only equality makes sense
            switch (op) {
                case opcode::EQ:
                    push(script_value(false)); // Different types are never equal
                    break;
                case opcode::NE:
                    push(script_value(true)); // Different types are always not equal
                    break;
                default:
                    runtime_error("Cannot compare different types with relational operators");
            }
        }
    }
    
    void virtual_machine::execute_array_op(opcode op) {
        switch (op) {
            case opcode::ARRAY_GET:
                if (impl_->state.stack.size() < 2) {
                    runtime_error("Stack underflow in ARRAY_GET");
                    return;
                }
                {
                    script_value index = pop();
                    script_value container = pop();
                    
                    if (container.is_array()) {
                        if (!index.is_int()) {
                            runtime_error("Array index must be integer");
                            push(script_value(std::monostate{}, impl_->engine_ref)); // Push null
                            return;
                        }
                        
                        const auto& arr = container.as_array();
                        script_int idx = index.as_int();
                        
                        if (impl_->bounds_checking) {
                            if (idx < 0 || idx >= static_cast<script_int>(arr.size())) {
                                runtime_error("Array index out of bounds");
                                push(script_value(std::monostate{}, impl_->engine_ref)); // Push null
                                return;
                            }
                        }
                        
                        push(arr[idx]);
                    } else if (container.is_map()) {
                        const auto& map = container.as_map();
                        auto it = map.find(index);
                        
                        if (it != map.end()) {
                            push(it->second);
                        } else {
                            push(script_value(std::monostate{}, impl_->engine_ref)); // Key not found, return null
                        }
                    } else {
                        runtime_error("Cannot index non-array/non-map value");
                        push(script_value(std::monostate{}, impl_->engine_ref)); // Push null
                        return;
                    }
                }
                break;
                
            case opcode::ARRAY_SET:
                if (impl_->state.stack.size() < 3) {
                    runtime_error("Stack underflow in ARRAY_SET");
                    return;
                }
                {
                    // Stack: [array, index, value]
                    script_value value = pop();
                    script_value index = pop();
                    // Don't pop the array - modify it in place
                    
                    if (impl_->state.stack.empty()) {
                        runtime_error("Stack underflow - missing array in ARRAY_SET");
                        return;
                    }
                    
                    script_value& container = impl_->state.stack.back();
                    
                    if (container.is_array()) {
                        if (!index.is_int()) {
                            runtime_error("Array index must be integer");
                            return;
                        }
                        
                        auto& arr = const_cast<std::vector<script_value>&>(container.as_array());
                        script_int idx = index.as_int();
                        
                        // For array literals, we need to push elements, not set them
                        // Check if index equals current size (append operation)
                        if (idx == static_cast<script_int>(arr.size())) {
                            arr.push_back(value);
                        } else if (impl_->bounds_checking) {
                            if (idx < 0 || idx >= static_cast<script_int>(arr.size())) {
                                runtime_error("Array index out of bounds");
                                return;
                            }
                            arr[static_cast<size_t>(idx)] = value;
                        } else {
                            arr[static_cast<size_t>(idx)] = value;
                        }
                    } else if (container.is_map()) {
                        auto& map = const_cast<std::map<script_value, script_value>&>(container.as_map());
                        map[index] = value;
                    } else {
                        runtime_error("Cannot index non-array/non-map value");
                        return;
                    }
                    
                    // Container stays on stack
                }
                break;
                
            default:
                runtime_error("Unknown array operation");
        }
    }
    
    void virtual_machine::execute_function_call(uint8_t arg_count) {
        if (impl_->state.stack.size() < static_cast<size_t>(arg_count + 1)) {
            runtime_error("Stack underflow in function call");
            return;
        }
        
        // Extract arguments
        std::vector<script_value> args;
        args.reserve(arg_count);
        for (uint8_t i = 0; i < arg_count; ++i) {
            args.insert(args.begin(), pop()); // Insert at beginning to maintain order
        }
        
        script_value function = pop();
        
        if (!function.is_function()) {
            runtime_error("Cannot call non-function value");
            return;
        }
        
        // Call the function
        try {
            const auto& func = function.as_function();
            script_value result = func(args);
            push(result);
            impl_->stats.function_calls++;
        } catch (const std::exception& e) {
            runtime_error("Function call failed: " + std::string(e.what()));
        }
    }
    
    void virtual_machine::execute_builtin_call(uint16_t function_index, uint8_t arg_count) {
        if (function_index >= impl_->builtin_functions.size()) {
            runtime_error("Built-in function index out of bounds");
            return;
        }
        
        if (impl_->state.stack.size() < arg_count) {
            runtime_error("Stack underflow in built-in function call");
            return;
        }
        
        // Extract arguments
        std::vector<script_value> args;
        args.reserve(arg_count);
        for (uint8_t i = 0; i < arg_count; ++i) {
            args.insert(args.begin(), pop()); // Insert at beginning to maintain order
        }
        
        // Call the built-in function
        try {
            const auto& func = impl_->builtin_functions[function_index];
            script_value result = func(args);
            push(result);
            impl_->stats.function_calls++;
        } catch (const std::exception& e) {
            runtime_error("Built-in function call failed: " + std::string(e.what()));
        }
    }
    
    void virtual_machine::set_engine_reference(std::weak_ptr<engine> engine_ref) {
        impl_->engine_ref = engine_ref;
    }
    
    std::weak_ptr<engine> virtual_machine::get_engine_reference() const {
        return impl_->engine_ref;
    }
    
    std::unique_ptr<virtual_machine> create_vm() {
        return std::make_unique<virtual_machine>();
    }

} // namespace jvm
} // namespace jai