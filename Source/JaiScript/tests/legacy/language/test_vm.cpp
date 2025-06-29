#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

JAI_TEST_SUITE(VMTests)

JAI_TEST(vm_backend_creation) {
    auto backend = create_vm_backend();
    expect_true(backend != nullptr);
    expect_eq(backend->get_backend_name(), "JaiScript VM (Bytecode)");
}

JAI_TEST(vm_direct_bytecode_execution) {
    // Test VM directly with hand-crafted bytecode
    auto vm = create_vm();
    
    // Create a simple module with bytecode for: push 2, push 2, add, return
    auto mod = std::make_unique<module>();
    auto func = std::make_unique<function>();
    
    func->name = "__main__";
    func->local_count = 0;
    func->max_stack_size = 2;
    
    // Bytecode: push 2, push 2, add, return_value
    func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(2));
    func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(2));
    func->instructions.emplace_back(opcode::ADD);
    func->instructions.emplace_back(opcode::RETURN_VALUE);
    
    mod->functions.push_back(std::move(func));
    mod->main_function = 0;
    
    vm->load_module(mod.get());
    script_value result = vm->execute();
    
    expect_eq(result.as<script_int>(), 4);
}

JAI_TEST(vm_stack_operations) {
    // Test basic stack operations
    auto vm = create_vm();
    
    auto mod = std::make_unique<module>();
    auto func = std::make_unique<function>();
    
    func->name = "__main__";
    func->local_count = 1;
    func->max_stack_size = 3;
    
    // Bytecode: push 10, store_local 0, load_local 0, push 5, multiply, return_value
    func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(10));
    func->instructions.emplace_back(opcode::STORE_LOCAL, static_cast<uint8_t>(0));
    func->instructions.emplace_back(opcode::LOAD_LOCAL, static_cast<uint8_t>(0));
    func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(5));
    func->instructions.emplace_back(opcode::MUL);
    func->instructions.emplace_back(opcode::RETURN_VALUE);
    
    mod->functions.push_back(std::move(func));
    mod->main_function = 0;
    
    vm->load_module(mod.get());
    script_value result = vm->execute();
    
    expect_eq(result.as<script_int>(), 50);
}

JAI_TEST(vm_builtin_function_call) {
    auto vm = create_vm();
    
    // Register a simple built-in function
    vm->register_builtin("double", [](const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) return script_value();
        return script_value(args[0].as<script_int>() * 2);
    });
    
    auto mod = std::make_unique<module>();
    auto func = std::make_unique<function>();
    
    func->name = "__main__";
    func->local_count = 0;
    func->max_stack_size = 2;
    
    // Bytecode: push 21, call_builtin "double" (index 0), return_value
    func->instructions.emplace_back(opcode::PUSH_INT, static_cast<uint32_t>(21));
    
    // CALL_BUILTIN needs both function index (short_operand) and arg count (byte_operand)
    instruction call_builtin(opcode::CALL_BUILTIN);
    call_builtin.short_operand = 0;  // function index 0 ("double")
    call_builtin.byte_operand = 1;   // 1 argument
    func->instructions.push_back(call_builtin);
    
    func->instructions.emplace_back(opcode::RETURN_VALUE);
    
    mod->functions.push_back(std::move(func));
    mod->main_function = 0;
    
    vm->load_module(mod.get());
    script_value result = vm->execute();
    
    expect_eq(result.as<script_int>(), 42);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()