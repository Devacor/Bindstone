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

JAI_TEST_SUITE(VMVariables)

JAI_TEST(vm_single_variable) {
    try {
        // Test basic variable declaration
        std::cout << "Testing: var x = 42; x;" << std::endl;
        auto result = compile_and_execute_vm("var x = 42; x;");
        std::cout << "Result is_int: " << result.is_int() << std::endl;
        std::cout << "Result is_null: " << result.is_null() << std::endl;
        if (result.is_int()) {
            std::cout << "Result value: " << result.as<script_int>() << std::endl;
            expect_eq(result.as<script_int>(), 42);
        } else {
            std::cout << "Result is not an integer!" << std::endl;
            expect_true(false);
        }
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()