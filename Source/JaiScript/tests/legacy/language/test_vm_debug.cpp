#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

JAI_TEST_SUITE(VMDebug)

JAI_TEST(vm_debug_simple_number) {
    try {
        // Test just a simple number
        lexer lex("42;");
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        auto backend = create_vm_backend();
        auto result = backend->execute(declarations);
        
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Is int: " << result.is_int() << std::endl;
        std::cout << "Is null: " << result.is_null() << std::endl;
        
        if (result.is_int()) {
            expect_eq(result.as<script_int>(), 42);
        } else {
            std::cout << "Result is not an integer!" << std::endl;
            expect_true(false); // Force failure
        }
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST(vm_debug_simple_addition) {
    try {
        // Test simple addition
        lexer lex("2 + 3;");
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        auto backend = create_vm_backend();
        auto result = backend->execute(declarations);
        
        std::cout << "Addition result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Is int: " << result.is_int() << std::endl;
        
        if (result.is_int()) {
            expect_eq(result.as<script_int>(), 5);
        } else {
            std::cout << "Addition result is not an integer!" << std::endl;
            expect_true(false);
        }
    } catch (const std::exception& e) {
        std::cout << "Exception in addition: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()