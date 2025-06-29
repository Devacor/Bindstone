#include <iostream>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <jaiscript/jvm/compiler.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

int main() {
    using namespace jai;
    using namespace jai::jvm;
    
    try {
        std::cout << "=== DEBUGGING VM VARIABLE ISSUE ===" << std::endl;
        
        // Parse the problematic source
        lexer lex("var x = 42; x;");
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        // Create compiler and compile
        compiler comp;
        auto mod = comp.compile(declarations);
        
        if (comp.has_errors()) {
            auto errors = comp.get_errors();
            for (const auto& error : errors) {
                std::cout << "Compiler error: " << error << std::endl;
            }
            return 1;
        }
        
        if (!mod) {
            std::cout << "Compilation failed!" << std::endl;
            return 1;
        }
        
        std::cout << "Module compiled successfully!" << std::endl;
        std::cout << "Functions: " << mod->functions.size() << std::endl;
        std::cout << "Global names: " << mod->global_names.size() << std::endl;
        
        for (size_t i = 0; i < mod->global_names.size(); ++i) {
            std::cout << "  Global[" << i << "]: " << mod->global_names[i] << std::endl;
        }
        
        if (!mod->functions.empty()) {
            auto& main_func = mod->functions[0];
            std::cout << "Main function instructions:" << std::endl;
            for (size_t i = 0; i < main_func->instructions.size(); ++i) {
                auto& instr = main_func->instructions[i];
                std::cout << "  [" << i << "] ";
                switch (instr.op) {
                    case opcode::PUSH_INT:
                        std::cout << "PUSH_INT " << instr.int_operand;
                        break;
                    case opcode::STORE_GLOBAL:
                        std::cout << "STORE_GLOBAL " << instr.short_operand;
                        break;
                    case opcode::LOAD_GLOBAL:
                        std::cout << "LOAD_GLOBAL " << instr.short_operand;
                        break;
                    case opcode::RETURN_VALUE:
                        std::cout << "RETURN_VALUE";
                        break;
                    default:
                        std::cout << "OPCODE " << static_cast<int>(instr.op);
                }
                std::cout << std::endl;
            }
        }
        
        // Create VM and load module
        auto vm = create_vm();
        vm->set_debug_mode(true);
        
        // Create a simple global environment for testing  
        string_symbolizer symbolizer;
        auto global_env = std::make_shared<environment>(&symbolizer);
        vm->set_global_environment(global_env);
        
        vm->load_module(mod.get());
        
        std::cout << "Executing VM..." << std::endl;
        auto result = vm->execute();
        
        if (vm->has_error()) {
            std::cout << "VM error: " << vm->get_error_message() << std::endl;
            return 1;
        }
        
        std::cout << "VM execution completed." << std::endl;
        std::cout << "Result is_null: " << result.is_null() << std::endl;
        std::cout << "Result is_int: " << result.is_int() << std::endl;
        
        if (result.is_int()) {
            std::cout << "Result value: " << result.as<script_int>() << std::endl;
            if (result.as<script_int>() == 42) {
                std::cout << "✓ SUCCESS!" << std::endl;
                return 0;
            }
        }
        
        std::cout << "✗ FAILED - unexpected result!" << std::endl;
        return 1;
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
}