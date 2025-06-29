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
        std::cout << "=== DEBUGGING FUNCTION CALL ===\n";
        
        // Parse simple function
        lexer lex("fun double(x) { return x * 2; } double(21);");
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
            std::cout << "Compilation failed!\n";
            return 1;
        }
        
        std::cout << "Module compiled successfully!\n";
        std::cout << "Functions: " << mod->functions.size() << "\n";
        std::cout << "Global names: " << mod->global_names.size() << "\n";
        
        // Print all function info
        for (size_t i = 0; i < mod->functions.size(); ++i) {
            auto& func = mod->functions[i];
            std::cout << "Function[" << i << "]: " << func->name 
                      << " (locals: " << static_cast<int>(func->local_count) 
                      << ", params: " << func->parameter_names.size() << ")\n";
            
            // Print parameter names
            for (size_t j = 0; j < func->parameter_names.size(); ++j) {
                std::cout << "  param[" << j << "]: " << func->parameter_names[j] << "\n";
            }
            
            // Print first few instructions
            std::cout << "  Instructions:\n";
            for (size_t j = 0; j < std::min(func->instructions.size(), size_t(10)); ++j) {
                auto& instr = func->instructions[j];
                std::cout << "    [" << j << "] ";
                switch (instr.op) {
                    case opcode::PUSH_INT:
                        std::cout << "PUSH_INT " << instr.int_operand;
                        break;
                    case opcode::LOAD_LOCAL:
                        std::cout << "LOAD_LOCAL " << static_cast<int>(instr.byte_operand);
                        break;
                    case opcode::MUL:
                        std::cout << "MUL";
                        break;
                    case opcode::RETURN_VALUE:
                        std::cout << "RETURN_VALUE";
                        break;
                    case opcode::PUSH_FUNCTION:
                        std::cout << "PUSH_FUNCTION " << instr.short_operand;
                        break;
                    case opcode::STORE_GLOBAL:
                        std::cout << "STORE_GLOBAL " << instr.short_operand;
                        break;
                    case opcode::LOAD_GLOBAL:
                        std::cout << "LOAD_GLOBAL " << instr.short_operand;
                        break;
                    case opcode::CALL:
                        std::cout << "CALL " << static_cast<int>(instr.byte_operand);
                        break;
                    default:
                        std::cout << "OPCODE " << static_cast<int>(instr.op);
                }
                std::cout << "\n";
            }
        }
        
        // Create VM and load module
        auto vm = create_vm();
        vm->set_debug_mode(true);
        
        string_symbolizer symbolizer;
        auto global_env = std::make_shared<environment>(&symbolizer);
        vm->set_global_environment(global_env);
        
        vm->load_module(mod.get());
        
        std::cout << "\n=== EXECUTING VM ===\n";
        auto result = vm->execute();
        
        if (vm->has_error()) {
            std::cout << "VM error: " << vm->get_error_message() << "\n";
            return 1;
        }
        
        std::cout << "VM execution completed.\n";
        std::cout << "Result type: " << static_cast<int>(result.type()) << "\n";
        std::cout << "Result is_null: " << result.is_null() << "\n";
        std::cout << "Result is_int: " << result.is_int() << "\n";
        
        if (result.is_int()) {
            std::cout << "Result value: " << result.as<script_int>() << "\n";
        } else {
            std::cout << "Result: " << result.to_string() << "\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
        return 1;
    }
}