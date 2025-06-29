#include <iostream>
#include <iomanip>
#include "../../../include/jaiscript/core/engine.hpp"
#include "../../../include/jaiscript/jvm/compiler.hpp"
#include "../../../include/jaiscript/jvm/virtual_machine.hpp"
#include "../../../include/jaiscript/detail/parser.hpp"
#include "../../../include/jaiscript/detail/lexer.hpp"

using namespace jai;
using namespace jai::jvm;

void print_opcode(opcode op) {
    switch(op) {
        case opcode::NOP: std::cout << "NOP"; break;
        case opcode::POP: std::cout << "POP"; break;
        case opcode::PUSH_INT: std::cout << "PUSH_INT"; break;
        case opcode::PUSH_NULL: std::cout << "PUSH_NULL"; break;
        case opcode::LOAD_LOCAL: std::cout << "LOAD_LOCAL"; break;
        case opcode::STORE_LOCAL: std::cout << "STORE_LOCAL"; break;
        case opcode::ADD: std::cout << "ADD"; break;
        case opcode::LT: std::cout << "LT"; break;
        case opcode::JUMP_IF_FALSE: std::cout << "JUMP_IF_FALSE"; break;
        case opcode::JUMP: std::cout << "JUMP"; break;
        case opcode::RETURN_VALUE: std::cout << "RETURN_VALUE"; break;
        default: std::cout << "OPCODE(" << static_cast<int>(op) << ")"; break;
    }
}

int main() {
    try {
        // The failing script
        std::string script = R"(
            var sum = 0;
            for (var i = 0; i < 3; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )";
        
        std::cout << "=== Analyzing VM bytecode for for-loop script ===\n";
        
        // Parse the script
        lexer lex(script);
        auto tokens = lex.tokenize();
        parser parse(tokens, "test.jai");
        auto declarations = parse.parse();
        
        // Compile with debug output
        compiler comp;
        auto module = comp.compile(declarations);
        
        if (!module) {
            std::cerr << "Compilation failed!\n";
            return 1;
        }
        
        // Print all instructions in main function
        const auto& main_func = module->functions[module->main_function];
        std::cout << "\nMain function bytecode (" << main_func->instructions.size() << " instructions):\n";
        std::cout << "----------------------------------------\n";
        
        for (size_t i = 0; i < main_func->instructions.size(); ++i) {
            const auto& instr = main_func->instructions[i];
            std::cout << std::setw(3) << i << ": ";
            print_opcode(instr.op);
            
            switch(instr.op) {
                case opcode::PUSH_INT:
                    std::cout << " " << instr.int_operand;
                    break;
                case opcode::LOAD_LOCAL:
                case opcode::STORE_LOCAL:
                    std::cout << " " << static_cast<int>(instr.byte_operand);
                    break;
                case opcode::JUMP:
                case opcode::JUMP_IF_FALSE:
                    std::cout << " -> " << instr.short_operand;
                    break;
            }
            std::cout << "\n";
        }
        
        std::cout << "\n=== Key observations ===\n";
        
        // Look for the final LOAD_LOCAL before RETURN_VALUE
        bool found_final_load = false;
        for (size_t i = main_func->instructions.size() - 1; i > 0; --i) {
            if (main_func->instructions[i].op == opcode::RETURN_VALUE) {
                std::cout << "RETURN_VALUE at position " << i << "\n";
                if (i > 0 && main_func->instructions[i-1].op == opcode::LOAD_LOCAL) {
                    std::cout << "Found LOAD_LOCAL before RETURN_VALUE - good!\n";
                    found_final_load = true;
                } else if (i > 0) {
                    std::cout << "Previous instruction: ";
                    print_opcode(main_func->instructions[i-1].op);
                    std::cout << "\n";
                }
                break;
            }
        }
        
        if (!found_final_load) {
            std::cout << "WARNING: No LOAD_LOCAL found before RETURN_VALUE!\n";
        }
        
        // Now execute with debug mode
        std::cout << "\n=== Executing with VM debug mode ===\n";
        virtual_machine vm;
        vm.set_debug_mode(true);
        
        auto global_env = std::make_shared<environment>(nullptr);
        vm.set_global_environment(global_env);
        
        vm.load_module(module.get());
        script_value result = vm.execute();
        
        std::cout << "\nFinal result type: " << static_cast<int>(result.type()) << "\n";
        if (result.is_null()) {
            std::cout << "Result is NULL - this is the bug!\n";
        } else if (result.is_int()) {
            std::cout << "Result value: " << result.as_int() << "\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}