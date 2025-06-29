#include <iostream>
#include "../../../include/jaiscript/core/engine.hpp"
#include "../../../include/jaiscript/jvm/compiler.hpp"
#include "../../../include/jaiscript/jvm/virtual_machine.hpp"
#include "../../../include/jaiscript/detail/parser.hpp"
#include "../../../include/jaiscript/detail/lexer.hpp"

using namespace jai;
using namespace jai::jvm;

int main() {
    try {
        // Simple test script
        std::string script = R"(
            var sum = 0;
            for (var i = 0; i < 3; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )";
        
        std::cout << "=== Testing VM return value issue ===\n";
        std::cout << "Script:\n" << script << "\n\n";
        
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
        
        std::cout << "Main function has " << module->functions[module->main_function]->instructions.size() << " instructions\n";
        
        // Print last few instructions
        const auto& main_func = module->functions[module->main_function];
        size_t start = main_func->instructions.size() > 5 ? main_func->instructions.size() - 5 : 0;
        for (size_t i = start; i < main_func->instructions.size(); ++i) {
            const auto& instr = main_func->instructions[i];
            std::cout << "Instruction " << i << ": opcode=" << static_cast<int>(instr.op);
            if (instr.op == opcode::RETURN_VALUE) {
                std::cout << " (RETURN_VALUE)";
            } else if (instr.op == opcode::LOAD_LOCAL) {
                std::cout << " (LOAD_LOCAL " << static_cast<int>(instr.byte_operand) << ")";
            } else if (instr.op == opcode::POP) {
                std::cout << " (POP)";
            }
            std::cout << "\n";
        }
        
        // Create VM and execute
        virtual_machine vm;
        vm.set_debug_mode(true);
        
        // Create global environment
        auto global_env = std::make_shared<environment>();
        vm.set_global_environment(global_env);
        
        // Load and execute
        vm.load_module(module.get());
        script_value result = vm.execute();
        
        std::cout << "\nResult type: " << static_cast<int>(result.type()) << "\n";
        if (result.is_null()) {
            std::cout << "Result is NULL!\n";
        } else if (result.is_int()) {
            std::cout << "Result value: " << result.as_int() << "\n";
        } else {
            std::cout << "Result is unexpected type\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}