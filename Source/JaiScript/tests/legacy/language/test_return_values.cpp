#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/lexer.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ReturnValues)

JAI_TEST(basic_return_values) {
    engine engine;
    
    // Test integer return
    script_value result1 = engine.execute("return 42;");
    expect_eq(result1.as<script_int>(), 42);
    
    // Test float return
    script_value result2 = engine.execute("return 3.14;");
    expect_near(result2.as<script_float>(), 3.14, 0.001);
    
    // Test string return
    script_value result3 = engine.execute("return \"hello\";");
    expect_eq(result3.as<script_string>(), "hello");
    
    // Test boolean return
    script_value result4 = engine.execute("return true;");
    expect_eq(result4.as<script_bool>(), true);
}

JAI_TEST(expression_returns) {
    engine engine;
    
    // Test arithmetic expression return
    script_value result1 = engine.execute("return 10 + 32;");
    expect_eq(result1.as<script_int>(), 42);
    
    // Test string concatenation return
    script_value result2 = engine.execute("return \"Hello, \" + \"World!\";");
    expect_eq(result2.as<script_string>(), "Hello, World!");
    
    // Test comparison return
    script_value result3 = engine.execute("return 5 > 3;");
    expect_eq(result3.as<script_bool>(), true);
    
    // Test conditional expression return
    script_value result4 = engine.execute("return true ? 100 : 200;");
    expect_eq(result4.as<script_int>(), 100);
}

JAI_TEST(variable_returns) {
    engine engine;
    
    // Test variable return
    script_value result = engine.execute(R"(
        var x = 42;
        var y = 58;
        return x + y;
    )");
    expect_eq(result.as<script_int>(), 100);
}

JAI_TEST(function_returns) {
    engine engine;
    
    // Test function return values
    script_value result = engine.execute(R"(
        function add(int a, int b) -> int {
            return a + b;
        }
        
        function multiply(int x, int y) -> int {
            return x * y;
        }
        
        return add(5, 3) + multiply(4, 2);
    )");
    expect_eq(result.as<script_int>(), 16); // (5+3) + (4*2) = 8 + 8 = 16
}

JAI_TEST(conditional_returns) {
    engine engine;
    
    // Test return from if statement
    script_value result1 = engine.execute(R"(
        var x = 10;
        if (x > 5) {
            return "large";
        } else {
            return "small";
        }
    )");
    expect_eq(result1.as<script_string>(), "large");
    
    // Test return from else branch
    script_value result2 = engine.execute(R"(
        var x = 2;
        if (x > 5) {
            return "large";
        } else {
            return "small";
        }
    )");
    expect_eq(result2.as<script_string>(), "small");
}

JAI_TEST(loop_returns) {
    engine engine;
    
    // Test return from loop
    script_value result1 = engine.execute(R"(
        for (var i = 1; i <= 10; i = i + 1) {
            if (i * i > 50) {
                return i;
            }
        }
        return -1;
    )");
    expect_eq(result1.as<script_int>(), 8); // First i where i^2 > 50 is i=8 (64 > 50)
    
    // Test return from while loop
    script_value result2 = engine.execute(R"(
        var count = 0;
        while (count < 100) {
            count = count + 1;
            if (count == 42) {
                return count;
            }
        }
        return -1;
    )");
    expect_eq(result2.as<script_int>(), 42);
}

JAI_TEST(early_returns) {
    engine engine;
    
    // Test early return prevents further execution
    script_value result = engine.execute(R"(
        var x = 10;
        if (x > 5) {
            return x * 2;
        }
        
        // This should not execute
        x = x + 100;
        return x;
    )");
    expect_eq(result.as<script_int>(), 20); // Should be 10*2, not 110
}

JAI_TEST(nested_function_returns) {
    engine engine;
    
    // Test nested function call returns
    script_value result = engine.execute(R"(
        function inner(int n) -> int {
            return n * 3;
        }
        
        function outer(int x) -> int {
            var temp = inner(x + 2);
            return temp + 5;
        }
        
        return outer(8);
    )");
    expect_eq(result.as<script_int>(), 35); // outer(8) = inner(10) + 5 = 30 + 5 = 35
}

JAI_TEST(lambda_returns) {
    engine engine;
    
    // Test lambda return values
    script_value result = engine.execute(R"(
        var square = [](int x) -> int {
            return x * x;
        };
        
        var cube = [](int x) -> int {
            return x * x * x;
        };
        
        return square(4) + cube(3);
    )");
    expect_eq(result.as<script_int>(), 43); // 4^2 + 3^3 = 16 + 27 = 43
}

JAI_TEST(complex_return_expressions) {
    engine engine;
    
    // Test complex return expressions
    script_value result = engine.execute(R"(
        function factorial(int n) -> int {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        return factorial(5);
    )");
    expect_eq(result.as<script_int>(), 120); // 5! = 120
}

JAI_TEST(return_type_consistency) {
    engine engine;
    
    // Test different return types from same script
    script_value result1 = engine.execute("return 42;");
    expect_true(result1.is_int());
    
    script_value result2 = engine.execute("return 3.14;");
    expect_true(result2.is_float());
    
    script_value result3 = engine.execute("return \"test\";");
    expect_true(result3.is_string());
    
    script_value result4 = engine.execute("return false;");
    expect_true(result4.is_bool());
}

JAI_TEST(templated_execute_methods) {
    engine engine;
    
    // Test regular execute since eval templates were removed
    auto intResult = engine.execute("40 + 2;");
    expect_eq(intResult.as_int(), script_int(42));
    
    auto stringResult = engine.execute("\"Hello\" + \" World\";");
    expect_eq(stringResult.as_string(), "Hello World");
    
    auto floatResult = engine.execute("3.14 * 2.0;");
    expect_near(floatResult.as_float(), 6.28, 0.001);
}

JAI_TEST(execute_type_checking) {
    engine engine;
    
    // Test type checking with regular execute
    auto result1 = engine.execute("\"not a number\";");
    expect_true(result1.is_string());
    expect_false(result1.is_int());
    
    auto result2 = engine.execute("42;");
    expect_true(result2.is_int());
    expect_false(result2.is_string());
}

JAI_TEST(return_value_from_expressions) {
    engine engine;
    
    // Test return values from various expression types
    script_value mathResult = engine.execute("(5 + 3) * (10 - 6);");
    expect_eq(mathResult.as<script_int>(), 32); // 8 * 4 = 32
    
    script_value comparisonResult = engine.execute("10 > 5 && 3 < 7;");
    expect_eq(comparisonResult.as<script_bool>(), true);
    
    script_value stringResult = engine.execute("\"value: \" + (20 + 22);");
    expect_eq(stringResult.as<script_string>(), "value: 42");
}

JAI_TEST(return_null_values) {
    engine engine;
    
    // Test explicit null return
    script_value result1 = engine.execute("return null;");
    expect_true(result1.is_null());
    
    // Test implicit null return (no return statement)
    script_value result2 = engine.execute(R"(
        var x = 10;
        x = x + 5;
    )");
    expect_true(result2.is_null());
}

JAI_TEST(return_from_cpp_functions) {
    engine engine;
    
    // Add C++ function that returns a value
    engine.add_function("calculate", [](const std::vector<script_value>& args) -> script_value {
        int a = args[0].as<script_int>();
        int b = args[1].as<script_int>();
        return script_int(a * b + 10);
    });
    
    // Test C++ function return
    script_value result = engine.execute("return calculate(6, 7);");
    expect_eq(result.as<script_int>(), 52); // 6*7 + 10 = 52
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()