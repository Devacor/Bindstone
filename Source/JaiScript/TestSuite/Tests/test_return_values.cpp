#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/lexer.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(ReturnValues)

JAI_TEST(basic_return_values) {
    Engine engine;
    
    // Test integer return
    Value result1 = engine.execute("return 42;");
    expect_eq(result1.as<Int>(), 42);
    
    // Test float return
    Value result2 = engine.execute("return 3.14;");
    expect_near(result2.as<Float>(), 3.14, 0.001);
    
    // Test string return
    Value result3 = engine.execute("return \"hello\";");
    expect_eq(result3.as<String>(), "hello");
    
    // Test boolean return
    Value result4 = engine.execute("return true;");
    expect_eq(result4.as<Bool>(), true);
}

JAI_TEST(expression_returns) {
    Engine engine;
    
    // Test arithmetic expression return
    Value result1 = engine.execute("return 10 + 32;");
    expect_eq(result1.as<Int>(), 42);
    
    // Test string concatenation return
    Value result2 = engine.execute("return \"Hello, \" + \"World!\";");
    expect_eq(result2.as<String>(), "Hello, World!");
    
    // Test comparison return
    Value result3 = engine.execute("return 5 > 3;");
    expect_eq(result3.as<Bool>(), true);
    
    // Test conditional expression return
    Value result4 = engine.execute("return true ? 100 : 200;");
    expect_eq(result4.as<Int>(), 100);
}

JAI_TEST(variable_returns) {
    Engine engine;
    
    // Test variable return
    Value result = engine.execute(R"(
        var x = 42;
        var y = 58;
        return x + y;
    )");
    expect_eq(result.as<Int>(), 100);
}

JAI_TEST(function_returns) {
    Engine engine;
    
    // Test function return values
    Value result = engine.execute(R"(
        function add(int a, int b) -> int {
            return a + b;
        }
        
        function multiply(int x, int y) -> int {
            return x * y;
        }
        
        return add(5, 3) + multiply(4, 2);
    )");
    expect_eq(result.as<Int>(), 16); // (5+3) + (4*2) = 8 + 8 = 16
}

JAI_TEST(conditional_returns) {
    Engine engine;
    
    // Test return from if statement
    Value result1 = engine.execute(R"(
        var x = 10;
        if (x > 5) {
            return "large";
        } else {
            return "small";
        }
    )");
    expect_eq(result1.as<String>(), "large");
    
    // Test return from else branch
    Value result2 = engine.execute(R"(
        var x = 2;
        if (x > 5) {
            return "large";
        } else {
            return "small";
        }
    )");
    expect_eq(result2.as<String>(), "small");
}

JAI_TEST(loop_returns) {
    Engine engine;
    
    // Test return from loop
    Value result1 = engine.execute(R"(
        for (var i = 1; i <= 10; i = i + 1) {
            if (i * i > 50) {
                return i;
            }
        }
        return -1;
    )");
    expect_eq(result1.as<Int>(), 8); // First i where i^2 > 50 is i=8 (64 > 50)
    
    // Test return from while loop
    Value result2 = engine.execute(R"(
        var count = 0;
        while (count < 100) {
            count = count + 1;
            if (count == 42) {
                return count;
            }
        }
        return -1;
    )");
    expect_eq(result2.as<Int>(), 42);
}

JAI_TEST(early_returns) {
    Engine engine;
    
    // Test early return prevents further execution
    Value result = engine.execute(R"(
        var x = 10;
        if (x > 5) {
            return x * 2;
        }
        
        // This should not execute
        x = x + 100;
        return x;
    )");
    expect_eq(result.as<Int>(), 20); // Should be 10*2, not 110
}

JAI_TEST(nested_function_returns) {
    Engine engine;
    
    // Test nested function call returns
    Value result = engine.execute(R"(
        function inner(int n) -> int {
            return n * 3;
        }
        
        function outer(int x) -> int {
            var temp = inner(x + 2);
            return temp + 5;
        }
        
        return outer(8);
    )");
    expect_eq(result.as<Int>(), 35); // outer(8) = inner(10) + 5 = 30 + 5 = 35
}

JAI_TEST(lambda_returns) {
    Engine engine;
    
    // Test lambda return values
    Value result = engine.execute(R"(
        var square = [](int x) -> int {
            return x * x;
        };
        
        var cube = [](int x) -> int {
            return x * x * x;
        };
        
        return square(4) + cube(3);
    )");
    expect_eq(result.as<Int>(), 43); // 4^2 + 3^3 = 16 + 27 = 43
}

JAI_TEST(complex_return_expressions) {
    Engine engine;
    
    // Test complex return expressions
    Value result = engine.execute(R"(
        function factorial(int n) -> int {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        return factorial(5);
    )");
    expect_eq(result.as<Int>(), 120); // 5! = 120
}

JAI_TEST(return_type_consistency) {
    Engine engine;
    
    // Test different return types from same script
    Value result1 = engine.execute("return 42;");
    expect_true(result1.isInt());
    
    Value result2 = engine.execute("return 3.14;");
    expect_true(result2.isFloat());
    
    Value result3 = engine.execute("return \"test\";");
    expect_true(result3.isString());
    
    Value result4 = engine.execute("return false;");
    expect_true(result4.isBool());
}

JAI_TEST(templated_execute_methods) {
    Engine engine;
    
    // Test templated execute methods
    auto intResult = engine.eval<int>("test", "40 + 2;");
    expect_true(intResult.has_value());
    expect_eq(intResult.value(), 42);
    
    auto stringResult = engine.eval<std::string>("test", "\"Hello\" + \" World\";");
    expect_true(stringResult.has_value());
    expect_eq(stringResult.value(), "Hello World");
    
    auto floatResult = engine.eval<double>("test", "3.14 * 2.0;");
    expect_true(floatResult.has_value());
    expect_near(floatResult.value(), 6.28, 0.001);
}

JAI_TEST(templated_execute_type_mismatches) {
    Engine engine;
    
    // Test type mismatch with templated execute
    auto badResult = engine.eval<int>("test", "\"not a number\";");
    expect_false(badResult.has_value());
    
    auto anotherBadResult = engine.eval<std::string>("test", "42;");
    expect_false(anotherBadResult.has_value());
}

JAI_TEST(return_value_from_expressions) {
    Engine engine;
    
    // Test return values from various expression types
    Value mathResult = engine.execute("(5 + 3) * (10 - 6);");
    expect_eq(mathResult.as<Int>(), 32); // 8 * 4 = 32
    
    Value comparisonResult = engine.execute("10 > 5 && 3 < 7;");
    expect_eq(comparisonResult.as<Bool>(), true);
    
    Value stringResult = engine.execute("\"Value: \" + (20 + 22);");
    expect_eq(stringResult.as<String>(), "Value: 42");
}

JAI_TEST(return_null_values) {
    Engine engine;
    
    // Test explicit null return
    Value result1 = engine.execute("return null;");
    expect_true(result1.isNull());
    
    // Test implicit null return (no return statement)
    Value result2 = engine.execute(R"(
        var x = 10;
        x = x + 5;
    )");
    expect_true(result2.isNull());
}

JAI_TEST(return_from_cpp_functions) {
    Engine engine;
    
    // Add C++ function that returns a value
    engine.addFunction("calculate", [](const std::vector<Value>& args) -> Value {
        int a = args[0].as<Int>();
        int b = args[1].as<Int>();
        return Int(a * b + 10);
    });
    
    // Test C++ function return
    Value result = engine.execute("return calculate(6, 7);");
    expect_eq(result.as<Int>(), 52); // 6*7 + 10 = 52
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()