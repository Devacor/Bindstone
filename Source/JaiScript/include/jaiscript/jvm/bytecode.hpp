#pragma once

#include "../core/value.hpp"
#include "../core/types.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

namespace jai {
namespace jvm {

    // Bytecode instruction opcodes for JaiScript Virtual Machine
    enum class opcode : uint8_t {
        // Stack operations
        NOP,              // No operation
        POP,              // Pop top value from stack
        DUP,              // Duplicate top stack value
        SWAP,             // Swap top two stack values
        
        // Constants
        PUSH_NULL,        // Push null constant
        PUSH_TRUE,        // Push true constant  
        PUSH_FALSE,       // Push false constant
        PUSH_INT,         // Push integer constant (4-byte operand)
        PUSH_FLOAT,       // Push float constant (8-byte operand)
        PUSH_CHAR,        // Push char constant (1-byte operand)
        PUSH_STRING,      // Push string constant (2-byte string table index)
        PUSH_FUNCTION,    // Push function constant (2-byte function table index)
        
        // Variable operations
        LOAD_LOCAL,       // Load local variable (1-byte slot index)
        STORE_LOCAL,      // Store to local variable (1-byte slot index)
        LOAD_GLOBAL,      // Load global variable (2-byte symbol index)
        STORE_GLOBAL,     // Store to global variable (2-byte symbol index)
        LOAD_UPVALUE,     // Load closure upvalue (1-byte upvalue index)
        STORE_UPVALUE,    // Store to closure upvalue (1-byte upvalue index)
        LOAD_REFERENCE,   // Load through reference
        STORE_REFERENCE,  // Store through reference
        MAKE_REFERENCE,   // Create reference to local/global variable
        
        // Arithmetic operations
        ADD,              // Addition
        SUB,              // Subtraction
        MUL,              // Multiplication
        DIV,              // Division
        MOD,              // Modulo
        NEG,              // Unary negation
        
        // Comparison operations
        EQ,               // Equal
        NE,               // Not equal
        LT,               // Less than
        LE,               // Less than or equal
        GT,               // Greater than
        GE,               // Greater than or equal
        CMP,              // Three-way comparison (spaceship operator)
        
        // Logical operations
        AND,              // Logical AND
        OR,               // Logical OR
        NOT,              // Logical NOT
        
        // Bitwise operations
        BIT_AND,          // Bitwise AND
        BIT_OR,           // Bitwise OR
        BIT_XOR,          // Bitwise XOR
        BIT_NOT,          // Bitwise NOT (complement)
        SHIFT_LEFT,       // Left shift
        SHIFT_RIGHT,      // Right shift
        
        // Type conversion
        TO_INT,           // Convert to integer
        TO_FLOAT,         // Convert to float
        TO_STRING,        // Convert to string
        TO_BOOL,          // Convert to boolean
        
        // Array operations
        NEW_ARRAY,        // Create new array (1-byte initial size)
        ARRAY_GET,        // Get array element (index on stack)
        ARRAY_SET,        // Set array element (value, index on stack)
        ARRAY_PUSH,       // Push to array
        ARRAY_POP,        // Pop from array
        ARRAY_SIZE,       // Get array size
        
        // Map operations
        NEW_MAP,          // Create new map
        MAP_GET,          // Get map value (key on stack)
        MAP_SET,          // Set map value (value, key on stack)
        MAP_HAS,          // Check if map has key
        MAP_SIZE,         // Get map size
        
        // Object operations
        NEW_OBJECT,       // Create new object (2-byte class index)
        GET_PROPERTY,     // Get object property (2-byte property name index)
        SET_PROPERTY,     // Set object property (2-byte property name index)
        CALL_METHOD,      // Call object method (1-byte arg count, 2-byte method name)
        
        // Control flow
        JUMP,             // Unconditional jump (2-byte offset)
        JUMP_IF_FALSE,    // Jump if top of stack is false (2-byte offset)
        JUMP_IF_TRUE,     // Jump if top of stack is true (2-byte offset)
        JUMP_IF_NULL,     // Jump if top of stack is null (2-byte offset)
        
        // Function operations
        CALL,             // Call function (1-byte arg count)
        CALL_BUILTIN,     // Call built-in function (2-byte function index, 1-byte arg count)
        RETURN,           // Return from function
        RETURN_VALUE,     // Return value from function
        
        // Closure operations
        CLOSURE,          // Create closure (2-byte function index, 1-byte upvalue count)
        CAPTURE_LOCAL,    // Capture local variable for closure
        CAPTURE_UPVALUE,  // Capture upvalue for closure
        
        // Loop operations
        LOOP_START,       // Mark start of loop (for break/continue)
        LOOP_END,         // Mark end of loop
        BREAK,            // Break from current loop
        CONTINUE,         // Continue to next loop iteration
        
        // Exception handling (future)
        THROW,            // Throw exception
        TRY_START,        // Start try block
        TRY_END,          // End try block
        CATCH,            // Catch exception
        
        // Debug operations
        DEBUG_LINE,       // Set current line number (2-byte line number)
        DEBUG_ENTER,      // Enter function for debugging
        DEBUG_EXIT,       // Exit function for debugging
    };
    
    // Single bytecode instruction
    struct instruction {
        opcode op;
        union {
            uint8_t byte_operand;
            uint16_t short_operand;
            uint32_t int_operand;
            uint64_t long_operand;
            double float_operand;
        };
        
        instruction() : op(opcode::NOP), long_operand(0) {}
        instruction(opcode op_code) : op(op_code), long_operand(0) {}
        instruction(opcode op_code, uint8_t operand) : op(op_code), byte_operand(operand) {}
        instruction(opcode op_code, uint16_t operand) : op(op_code), short_operand(operand) {}
        instruction(opcode op_code, uint32_t operand) : op(op_code), int_operand(operand) {}
        instruction(opcode op_code, uint64_t operand) : op(op_code), long_operand(operand) {}
        instruction(opcode op_code, double operand) : op(op_code), float_operand(operand) {}
    };
    
    // Compiled function containing bytecode
    struct function {
        std::string name;
        std::vector<instruction> instructions;
        std::vector<std::string> parameter_names;
        std::vector<value_type> parameter_types;
        value_type return_type;
        uint8_t local_count;        // Number of local variable slots
        uint8_t max_stack_size;     // Maximum stack size needed
        bool is_variadic;           // Takes variable arguments
        bool captures_upvalues;     // Has closure upvalues
        
        // Debug information
        std::vector<uint16_t> line_numbers; // Line number for each instruction
        std::string source_file;
        
        function() : return_type(value_type::jai_null_type), local_count(0), 
                    max_stack_size(0), is_variadic(false), captures_upvalues(false) {}
    };
    
    // Function prototype for native (C++) functions
    using native_function = std::function<script_value(const std::vector<script_value>&)>;
    
    // Compiled module containing multiple functions and constants
    struct module {
        std::vector<std::unique_ptr<function>> functions;
        std::vector<native_function> native_functions;
        std::vector<std::string> string_constants;
        std::vector<script_value> value_constants;
        std::vector<std::string> global_names;
        
        // Class information
        struct class_info {
            std::string name;
            std::vector<std::string> method_names;
            std::vector<std::string> property_names;
            std::vector<std::string> base_classes;
        };
        std::vector<class_info> classes;
        
        // Entry point function index
        size_t main_function = 0;
        
        // Debug information
        std::string source_file;
        std::vector<std::string> source_lines;
    };
    
    // Runtime closure value
    struct closure {
        const function* func;
        std::vector<script_value> upvalues;
        
        closure(const function* f) : func(f) {}
    };
    
    // Stack frame for function calls
    struct call_frame {
        const function* func;           // Current function
        const closure* closure_data;    // Closure data (null for non-closures)
        size_t ip;                     // Instruction pointer
        size_t stack_base;             // Base of this frame's stack
        uint8_t local_base;            // Base index for local variables
        
        call_frame(const function* f, size_t stack_base_idx, uint8_t local_base_idx)
            : func(f), closure_data(nullptr), ip(0), stack_base(stack_base_idx), 
              local_base(local_base_idx) {}
              
        call_frame(const closure* c, size_t stack_base_idx, uint8_t local_base_idx)
            : func(c->func), closure_data(c), ip(0), stack_base(stack_base_idx),
              local_base(local_base_idx) {}
    };
    
    // VM execution state
    struct vm_state {
        std::vector<script_value> stack;         // Value stack
        std::vector<script_value> locals;        // Local variable slots
        std::vector<call_frame> call_stack;      // Function call stack
        std::vector<size_t> loop_stack;          // Loop break/continue targets
        
        // Global state
        std::shared_ptr<environment> global_env; // Global variables
        const module* current_module;            // Currently executing module
        
        // Error handling
        bool has_error;
        std::string error_message;
        size_t error_line;
        
        vm_state() : current_module(nullptr), has_error(false), error_line(0) {
            stack.reserve(1024);
            locals.reserve(256);
            call_stack.reserve(64);
            loop_stack.reserve(32);
        }
    };
    
    // Debugging information
    struct debug_info {
        std::string function_name;
        std::string source_file;
        size_t line_number;
        size_t instruction_pointer;
        std::vector<std::pair<std::string, script_value>> local_variables;
    };
    
    // Optimization hints for the compiler
    enum class optimization_level {
        NONE,       // No optimizations
        BASIC,      // Basic constant folding, dead code elimination
        STANDARD,   // Standard optimizations (default)
        AGGRESSIVE  // Aggressive optimizations (may increase compile time)
    };
    
    struct compilation_options {
        optimization_level opt_level = optimization_level::STANDARD;
        bool include_debug_info = true;
        bool enable_type_checking = true;
        bool enable_bounds_checking = true;
        size_t max_inline_depth = 3;
    };

} // namespace jvm
} // namespace jai