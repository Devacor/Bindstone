#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/compiler.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/interpreter.hpp>  // For string_symbolizer
#include <iostream>

using namespace jai;
using namespace jai::jvm;
using namespace jai::foundry;

class vm_function_tests : public suite {
public:
    vm_function_tests() : suite("VM Function Return Debug") {}
    
    void forge_tests() override {
        test("vm_function_works_with_semicolon", [this]() {
            // Test with explicit semicolon separation
            std::string code = "fun double(x) { return x * 2; }; double(21);";

            auto eng = engine::make();
            lexer lex(code);
            auto tokens = lex.tokenize();

            string_symbolizer symbolizer;
            std::unordered_set<std::string> empty_types;
            parser parse(tokens, &symbolizer, eng.get(), empty_types);
            auto declarations = parse.parse();
            
            std::cout << "\n=== PARSED DECLARATIONS ===\n";
            std::cout << "Total declarations: " << declarations.size() << "\n";
            for (size_t i = 0; i < declarations.size(); ++i) {
                if (auto* func = dynamic_cast<function_decl*>(declarations[i].get())) {
                    std::cout << "  [" << i << "] Function: " << func->name << "\n";
                } else if (auto* expr = dynamic_cast<expression_decl*>(declarations[i].get())) {
                    std::cout << "  [" << i << "] Expression\n";
                } else if (auto* stmt = dynamic_cast<statement_decl*>(declarations[i].get())) {
                    std::cout << "  [" << i << "] Statement\n";
                    if (auto* block = dynamic_cast<block_stmt*>(stmt->statement.get())) {
                        std::cout << "      Block with " << block->declarations.size() << " declarations\n";
                    }
                } else {
                    std::cout << "  [" << i << "] Other\n";
                }
            }
            
            compiler comp;
            auto mod = comp.compile(declarations);
            
            if (!mod) {
                throw test_failure("Module compilation failed");
            }
            if (comp.has_errors()) {
                throw test_failure("Compiler has errors");
            }
            
            // Print bytecode for debugging
            std::cout << "\n=== BYTECODE ===\n";
            for (size_t i = 0; i < mod->functions.size(); ++i) {
                auto& func = mod->functions[i];
                std::cout << "\nFunction " << i << ": " << func->name 
                          << " (locals: " << static_cast<int>(func->local_count) << ")\n";
                for (size_t j = 0; j < func->instructions.size(); ++j) {
                    auto& instr = func->instructions[j];
                    std::cout << "  [" << j << "] ";
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
            
            // Execute with debug mode
            auto vm = create_vm();
            vm->set_debug_mode(true);
            
            string_symbolizer symbolizer;
            auto global_env = std::make_shared<environment>(&symbolizer);
            vm->set_global_environment(global_env);
            
            vm->load_module(mod.get());
            
            std::cout << "\n=== EXECUTION ===\n";
            auto result = vm->execute();
            
            std::cout << "\nFinal result:\n";
            std::cout << "  Type: " << static_cast<int>(result.type()) << "\n";
            std::cout << "  Is null: " << result.is_null() << "\n";
            std::cout << "  Is int: " << result.is_int() << "\n";
            
            if (result.is_null()) {
                throw test_failure("Result should not be null");
            }
            if (!result.is_int()) {
                throw test_failure("Result should be an integer");
            }
            
            std::cout << "  Value: " << result.as<script_int>() << "\n";
            check_eq(result.as<script_int>(), script_int(42));
        });
    }
};

FOUNDRY_REGISTER(vm_function_tests)