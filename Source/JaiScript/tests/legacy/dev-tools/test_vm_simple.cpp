#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Helper function to compile and execute JaiScript source using VM
script_value compile_and_execute_vm(const std::string& source) {
    try {
        // Parse the source into AST
        lexer lex(source);
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        // Create VM backend and execute
        auto backend = create_vm_backend();
        
        // Check for compilation errors
        if (backend->has_compilation_errors()) {
            auto errors = backend->get_compilation_errors();
            for (const auto& error : errors) {
                std::cout << "Compilation error: " << error << std::endl;
            }
            throw std::runtime_error("Compilation failed");
        }
        
        auto result = backend->execute(declarations);
        
        // Check for VM errors
        auto* vm = backend->get_vm();
        if (vm && vm->has_error()) {
            std::cout << "VM error: " << vm->get_error_message() << std::endl;
            throw std::runtime_error("VM execution failed");
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cout << "Exception in compile_and_execute_vm: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE(VMSimple)

JAI_TEST(vm_simple_expression_result) {
    // Test that expressions return their result
    auto result = compile_and_execute_vm("5 + 3");
    expect_true(result.is_int());
    expect_eq(result.as<script_int>(), 8);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()