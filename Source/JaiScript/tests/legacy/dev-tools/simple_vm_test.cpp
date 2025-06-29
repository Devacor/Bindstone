#include <iostream>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>

int main() {
    using namespace jai::jvm;
    
    try {
        std::cout << "Creating VM..." << std::endl;
        auto vm = create_vm();
        
        std::cout << "Creating simple module..." << std::endl;
        auto mod = std::make_unique<module>();
        auto func = std::make_unique<function>();
        
        func->name = "__main__";
        func->local_count = 0;
        func->max_stack_size = 2;
        
        // Simple bytecode: push 2, push 3, add, return_value
        func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(2));
        func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(3));
        func->instructions.emplace_back(opcode::ADD);
        func->instructions.emplace_back(opcode::RETURN_VALUE);
        
        mod->functions.push_back(std::move(func));
        mod->main_function = 0;
        
        std::cout << "Loading module..." << std::endl;
        vm->load_module(mod.get());
        
        std::cout << "Executing..." << std::endl;
        auto result = vm->execute();
        
        // std::cout << "Result type: " << result.get_type_name() << std::endl;
        std::cout << "Expected 5, got: " << result.as<jai::script_int>() << std::endl;
        
        if (result.as<jai::script_int>() == 5) {
            std::cout << "✓ VM test PASSED!" << std::endl;
            return 0;
        } else {
            std::cout << "✗ VM test FAILED!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
}