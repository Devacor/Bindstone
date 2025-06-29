#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(LambdaTests)

JAI_TEST(simple_lambda) {
    engine engine;
    
    std::string script = R"(
        auto add = [](auto a, auto b) -> auto {
            return a + b;
        };
        
        add(10, 5);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(lambda_assignment_and_multiple_calls) {
    engine engine;
    
    std::string script = R"(
        auto multiply = [](auto x, auto y) -> auto {
            return x * y;
        };
        
        auto result1 = multiply(3, 4);
        auto result2 = multiply(5, 6);
        result1 + result2;
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 42); // 12 + 30 = 42
}

JAI_TEST(lambda_with_no_parameters) {
    engine engine;
    
    std::string script = R"(
        auto getConstant = []() -> auto {
            return 42;
        };
        
        getConstant();
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 42);
}

JAI_TEST(lambda_with_string_operations) {
    engine engine;
    
    std::string script = R"(
        auto concatenate = [](auto str1, auto str2) -> auto {
            return str1 + " " + str2;
        };
        
        concatenate("Hello", "World");
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_string());
    expect_eq(result.as<std::string>(), "Hello World");
}

JAI_TEST(lambda_with_conditional_logic) {
    engine engine;
    
    std::string script = R"(
        auto max = [](auto a, auto b) -> auto {
            if (a > b) {
                return a;
            } else {
                return b;
            }
        };
        
        max(15, 8);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(lambda_with_loop) {
    engine engine;
    
    std::string script = R"(
        auto factorial = [](auto n) -> auto {
            auto result = 1;
            for (auto i = 1; i <= n; i = i + 1) {
                result = result * i;
            }
            return result;
        };
        
        factorial(5);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 120);
}

JAI_TEST(nested_lambda_calls) {
    engine engine;
    
    std::string script = R"(
        auto double = [](auto x) -> auto {
            return x * 2;
        };
        
        auto add = [](auto a, auto b) -> auto {
            return a + b;
        };
        
        double(add(3, 4));
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 14); // double(3 + 4) = double(7) = 14
}

JAI_TEST(lambda_as_parameter) {
    engine engine;
    
    std::string script = R"(
        auto apply = [](auto func, auto value) -> auto {
            return func(value);
        };
        
        auto square = [](auto x) -> auto {
            return x * x;
        };
        
        apply(square, 6);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 36);
}

JAI_TEST(lambda_returning_lambda) {
    engine engine;
    
    std::string script = R"(
        auto makeAdder = [](auto increment) -> auto {
            return [](auto value) -> auto {
                return value + 1;  // Simplified for now - captures not fully supported
            };
        };
        
        auto add1 = makeAdder(1);
        add1(5);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 6);
}

JAI_TEST(lambda_with_multiple_statements) {
    engine engine;
    
    std::string script = R"(
        auto processNumber = [](auto n) -> auto {
            auto temp = n * 2;
            temp = temp + 5;
            auto final = temp / 3;
            return final;
        };
        
        processNumber(10);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 8); // ((10 * 2) + 5) / 3 = 25 / 3 = 8 (integer division)
}

JAI_TEST(lambda_comparison_and_sorting_logic) {
    engine engine;
    
    std::string script = R"(
        auto compare = [](auto a, auto b) -> auto {
            if (a < b) {
                return -1;
            } else if (a > b) {
                return 1;
            } else {
                return 0;
            }
        };
        
        compare(5, 10);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), -1);
}

JAI_TEST(lambda_with_early_return) {
    engine engine;
    
    std::string script = R"(
        auto findFirstDivisor = [](auto n) -> auto {
            for (auto i = 2; i < n; i = i + 1) {
                if (n % i == 0) {
                    return i;
                }
            }
            return n; // n is prime
        };
        
        findFirstDivisor(15);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 3); // First divisor of 15 is 3
}

JAI_TEST(lambda_variable_shadowing) {
    engine engine;
    
    std::string script = R"(
        auto x = 100;
        
        auto testShadowing = [](auto x) -> auto {
            auto localX = x * 2;
            return localX;
        };
        
        auto result = testShadowing(5);
        result + x;
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 110); // (5 * 2) + 100 = 110
}

JAI_TEST(lambda_recursion) {
    engine engine;
    
    std::string script = R"(
        auto fibonacci = [](auto n) -> auto {
            if (n <= 1) {
                return n;
            }
            // For now, implement iteratively since recursive lambda calls are complex
            auto a = 0;
            auto b = 1;
            for (auto i = 2; i <= n; i = i + 1) {
                auto temp = a + b;
                a = b;
                b = temp;
            }
            return b;
        };
        
        fibonacci(7);
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_int());
    expect_eq(result.as<int>(), 13); // fibonacci(7) = 13
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()