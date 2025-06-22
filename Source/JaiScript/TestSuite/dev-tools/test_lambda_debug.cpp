#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(LambdaDebug)

JAI_TEST(lambda_simple_no_params) {
    Engine engine;
    
    try {
        // Test lambda with no parameters first
        Value result = engine.execute(R"(
            auto f = []() -> auto { return 42; };
            f();
        )");
        
        std::cout << "No-param lambda result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 42);
    } catch (const std::exception& e) {
        std::cout << "No-param lambda failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(lambda_with_closure) {
    Engine engine;
    
    try {
        // Test lambda that captures a variable
        Value result = engine.execute(R"(
            auto x = 10;
            auto f = []() -> auto { return x; };
            f();
        )");
        
        std::cout << "Closure lambda result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 10);
    } catch (const std::exception& e) {
        std::cout << "Closure lambda failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(lambda_single_param_direct) {
    Engine engine;
    
    try {
        // Create and immediately call the lambda
        Value result = engine.execute(R"(
            ([](x) -> auto { return x + 1; })(5);
        )");
        
        std::cout << "Direct lambda call result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 6);
    } catch (const std::exception& e) {
        std::cout << "Direct lambda call failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(lambda_param_visibility) {
    Engine engine;
    
    try {
        // Test if parameter is visible in lambda body
        engine.execute(R"(
            auto f = [](x) -> auto { 
                // Try to use x in a simple way
                auto y = x;
                return y;
            };
        )");
        
        std::cout << "Lambda definition succeeded" << std::endl;
        
        // Now try to call it
        Value result = engine.execute("f(123);");
        std::cout << "Lambda call result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 123);
        
    } catch (const std::exception& e) {
        std::cout << "Lambda param visibility failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(function_definition_comparison) {
    Engine engine;
    
    try {
        // Compare with regular function definition
        Value result = engine.execute(R"(
            function add(x, y) -> auto {
                return x + y;
            }
            add(3, 4);
        )");
        
        std::cout << "Function definition result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 7);
    } catch (const std::exception& e) {
        std::cout << "Function definition failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()