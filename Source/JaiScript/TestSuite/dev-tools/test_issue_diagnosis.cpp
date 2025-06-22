#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(IssueDiagnosis)

JAI_TEST(lambda_basic_execution) {
    Engine engine;
    
    try {
        Value result = engine.execute(R"(
            auto f = [](x) -> auto { return x * 2; };
            f(5);
        )");
        
        std::cout << "Lambda result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 10);
    } catch (const std::exception& e) {
        std::cout << "Lambda execution failed: " << e.what() << std::endl;
        expect_eq(true, false); // Force test failure with error message
    }
}

JAI_TEST(control_flow_for_loop) {
    Engine engine;
    
    try {
        Value result = engine.execute(R"(
            auto sum = 0;
            for (i = 0; i < 5; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )");
        
        std::cout << "For loop result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 10); // 0+1+2+3+4 = 10
    } catch (const std::exception& e) {
        std::cout << "For loop failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(control_flow_while_loop) {
    Engine engine;
    
    try {
        Value result = engine.execute(R"(
            auto sum = 0;
            auto i = 0;
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            sum;
        )");
        
        std::cout << "While loop result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 10);
    } catch (const std::exception& e) {
        std::cout << "While loop failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(control_flow_if_else) {
    Engine engine;
    
    try {
        Value result = engine.execute(R"(
            auto x = 10;
            if (x > 5) {
                x = x * 2;
            } else {
                x = x / 2;
            }
            x;
        )");
        
        std::cout << "If-else result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 20);
    } catch (const std::exception& e) {
        std::cout << "If-else failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(string_concatenation_in_loop) {
    Engine engine;
    
    // Add function to create strings
    engine.addFunction("make_string", [](int size) -> String {
        return String(std::string(size, 'X'));
    });
    
    try {
        Value result = engine.execute(R"(
            auto s = make_string(10);
            for (i = 0; i < 3; i = i + 1) {
                s = s + "A";
            }
            s;
        )");
        
        std::cout << "String concat result length: " << result.as<String>().length() << std::endl;
        expect_eq(result.as<String>().length(), std::size_t(13)); // 10 + 3
    } catch (const std::exception& e) {
        std::cout << "String concat in loop failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST(function_call_in_expression) {
    Engine engine;
    
    engine.addFunction("add", [](int a, int b) -> int {
        return a + b;
    });
    
    try {
        Value result = engine.execute(R"(
            auto x = add(5, 3);
            auto y = add(x, 2);
            y;
        )");
        
        std::cout << "Function call result: " << result.toString() << std::endl;
        expect_eq(result.as<Int>(), 10);
    } catch (const std::exception& e) {
        std::cout << "Function call failed: " << e.what() << std::endl;
        expect_eq(true, false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()