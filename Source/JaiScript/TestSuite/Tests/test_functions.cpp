#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(FunctionTests)

JAI_TEST(simple_function_declaration_and_call) {
    Engine engine;
    
    std::string script = R"(
        auto add(auto a, auto b) -> auto {
            return a + b;
        }
        
        auto result = add(5, 3);
        result;
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 8);
}

JAI_TEST(function_with_no_parameters) {
    Engine engine;
    
    std::string script = R"(
        auto getValue() -> auto {
            return 42;
        }
        
        getValue();
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 42);
}

JAI_TEST(function_with_side_effects) {
    Engine engine;
    
    std::string script = R"(
        auto x = 10;
        
        auto setX(auto value) -> auto {
            x = value;
        }
        
        setX(25);
        x;
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 25);
}

JAI_TEST(function_with_multiple_parameters) {
    Engine engine;
    
    std::string script = R"(
        function multiply(auto a, auto b, auto c) -> auto {
            return a * b * c;
        }
        
        multiply(2, 3, 4);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 24);
}

JAI_TEST(recursive_function) {
    Engine engine;
    
    std::string script = R"(
        function factorial(auto n) -> auto {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        factorial(5);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 120);
}

JAI_TEST(function_returning_string) {
    Engine engine;
    
    std::string script = R"(
        function greeting(auto name) -> auto {
            return "Hello, " + name + "!";
        }
        
        greeting("World");
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isString());
    expect_eq(result.as<std::string>(), "Hello, World!");
}

JAI_TEST(nested_function_calls) {
    Engine engine;
    
    std::string script = R"(
        function double(auto x) -> auto {
            return x * 2;
        }
        
        function add(auto a, auto b) -> auto {
            return a + b;
        }
        
        double(add(3, 4));
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 14); // double(3 + 4) = double(7) = 14
}

JAI_TEST(function_with_local_variables) {
    Engine engine;
    
    std::string script = R"(
        function compute(auto n) -> auto {
            auto temp = n * 2;
            auto result = temp + 5;
            return result;
        }
        
        compute(10);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 25); // (10 * 2) + 5 = 25
}

JAI_TEST(function_with_conditional_return) {
    Engine engine;
    
    std::string script = R"(
        function absolute(auto x) -> auto {
            if (x < 0) {
                return -x;
            } else {
                return x;
            }
        }
        
        absolute(-15);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(function_with_loop) {
    Engine engine;
    
    std::string script = R"(
        function sumTo(auto n) -> auto {
            auto sum = 0;
            auto i = 1;
            while (i <= n) {
                sum = sum + i;
                i = i + 1;
            }
            return sum;
        }
        
        sumTo(5);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5 = 15
}

JAI_TEST(multiple_functions_in_script) {
    Engine engine;
    
    std::string script = R"(
        function square(auto x) -> auto {
            return x * x;
        }
        
        function cube(auto x) -> auto {
            return x * x * x;
        }
        
        function powerSum(auto x) -> auto {
            return square(x) + cube(x);
        }
        
        powerSum(3);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 36); // 3^2 + 3^3 = 9 + 27 = 36
}

JAI_TEST(function_scope_isolation) {
    Engine engine;
    
    std::string script = R"(
        auto globalVar = 100;
        
        function modifyLocal() -> auto {
            auto localVar = 50;
            localVar = localVar + 10;
            return localVar;
        }
        
        auto result = modifyLocal();
        result + globalVar;
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 160); // 60 + 100 = 160
}

JAI_TEST(function_early_return) {
    Engine engine;
    
    std::string script = R"(
        function findFirst(auto limit) -> auto {
            for (auto i = 0; i < limit; i = i + 1) {
                if (i * i > 50) {
                    return i;
                }
            }
            return -1;
        }
        
        findFirst(10);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 8); // First i where i^2 > 50 is i=8 (64 > 50)
}

JAI_TEST(function_with_boolean_logic) {
    Engine engine;
    
    std::string script = R"(
        function isEven(auto n) -> auto {
            return (n % 2) == 0;
        }
        
        function testNumber(auto x) -> auto {
            if (isEven(x)) {
                return x / 2;
            } else {
                return x * 3 + 1;
            }
        }
        
        testNumber(6);
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isInt());
    expect_eq(result.as<int>(), 3); // 6 is even, so 6/2 = 3
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()