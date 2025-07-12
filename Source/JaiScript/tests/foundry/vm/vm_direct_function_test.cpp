#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <iostream>

using namespace jai;
using namespace jai::jvm;
using namespace jai::foundry;

class vm_direct_function_tests : public suite {
public:
    vm_direct_function_tests() : suite("VM Direct Function Test") {}
    
    void forge_tests() override {
        test("direct_bytecode_function_call", [this]() {
            // Create a module with bytecode directly
            auto mod = std::make_unique<module>();
            
            // Create function: fun double(x) { return x * 2; }
            auto double_func = std::make_unique<function>();
            double_func->name = "double";
            double_func->parameter_names = {"x"};
            double_func->local_count = 1; // One parameter
            
            // Bytecode: load parameter, push 2, multiply, return
            double_func->instructions = {
                instruction(opcode::LOAD_LOCAL, uint8_t(0)),     // Load x (parameter 0)
                instruction(opcode::PUSH_INT, uint32_t(2)),      // Push 2
                instruction(opcode::MUL),                        // Multiply
                instruction(opcode::RETURN_VALUE)                // Return result
            };
            
            // Add function to module
            size_t double_func_idx = mod->functions.size();
            mod->functions.push_back(std::move(double_func));
            mod->global_names.push_back("double");
            
            // Create main function: double(21)
            auto main_func = std::make_unique<function>();
            main_func->name = "__main__";
            main_func->local_count = 0;
            
            // Bytecode: push function, store as global, load it, push 21, call
            main_func->instructions = {
                instruction(opcode::PUSH_FUNCTION, uint16_t(double_func_idx)), // Push double function
                instruction(opcode::STORE_GLOBAL, uint16_t(0)),                // Store as global[0] "double"
                instruction(opcode::LOAD_GLOBAL, uint16_t(0)),                 // Load global[0] "double"  
                instruction(opcode::PUSH_INT, uint32_t(21)),                   // Push 21
                instruction(opcode::CALL, uint8_t(1)),                         // Call with 1 arg
                instruction(opcode::RETURN_VALUE)                              // Return result
            };
            
            // Add main function
            mod->functions.push_back(std::move(main_func));
            mod->main_function = mod->functions.size() - 1;
            
            // Create VM and execute
            auto vm = create_vm();
            vm->set_debug_mode(true);
            
            string_symbolizer symbolizer;
            auto global_env = std::make_shared<environment>(&symbolizer);
            vm->set_global_environment(global_env);
            
            vm->load_module(mod.get());
            
            std::cout << "\n=== EXECUTING DIRECT BYTECODE ===\n";
            auto result = vm->execute();
            
            std::cout << "\nResult:\n";
            std::cout << "  Type: " << static_cast<int>(result.type()) << "\n";
            std::cout << "  Is null: " << result.is_null() << "\n";
            std::cout << "  Is int: " << result.is_int() << "\n";
            
            check(!result.is_null(), "Result should not be null");
            check(result.is_int(), "Result should be an integer");
            
            std::cout << "  Value: " << result.as<script_int>() << "\n";
            check_eq(result.as<script_int>(), script_int(42));
        });
    }
};

// Main function
int main() {
    vm_direct_function_tests tests;
    return tests.quench();
}