#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <cmath>
#include <memory>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(CppBindings)

JAI_TEST(operator_overloading_with_correct_types) {
    Engine engine;
    
    // Test all basic operators
    engine.addFunction("+", [](int a, int b) -> int { return a + b; });
    engine.addFunction("-", [](int a, int b) -> int { return a - b; });
    engine.addFunction("*", [](int a, int b) -> int { return a * b; });
    engine.addFunction("/", [](int a, int b) -> float { return static_cast<float>(a) / b; });
    engine.addFunction("%", [](int a, int b) -> int { return a % b; });
    
    expect_eq(engine.execute("10 + 5;").as<int>(), 15);
    expect_eq(engine.execute("10 - 5;").as<int>(), 5);
    expect_eq(engine.execute("10 * 5;").as<int>(), 50);
    expect_near(engine.execute("10 / 3;").as<float>(), 3.333f, 0.001f);
    expect_eq(engine.execute("10 % 3;").as<int>(), 1);
}

JAI_TEST(string_operations_with_const_ref_parameters) {
    Engine engine;
    
    // Test that const std::string& parameters work correctly
    engine.addFunction("join", [](const std::string& a, const std::string& b, const std::string& sep) -> std::string {
        return a + sep + b;
    });
    
    Value result = engine.execute("join(\"Hello\", \"World\", \", \");");
    expect_eq(result.as<std::string>(), std::string("Hello, World"));
    
    // Test multiple calls to ensure no parameter corruption
    result = engine.execute("join(\"foo\", \"bar\", \"_\");");
    expect_eq(result.as<std::string>(), std::string("foo_bar"));
}

JAI_TEST(void_functions_with_side_effects) {
    Engine engine;
    
    std::vector<std::string> log;
    engine.addFunction("log", [&log](const std::string& msg) -> void {
        log.push_back(msg);
    });
    
    Value result = engine.execute("log(\"First message\");");
    expect_true(result.isNull());
    expect_eq(log.size(), std::size_t(1));
    expect_eq(log[0], std::string("First message"));
    
    engine.execute("log(\"Second\"); log(\"Third\");");
    expect_eq(log.size(), std::size_t(3));
    expect_eq(log[1], std::string("Second"));
    expect_eq(log[2], std::string("Third"));
}

JAI_TEST(lambda_captures_by_value) {
    Engine engine;
    
    int base = 100;
    engine.addFunction("add_base", [base](int x) -> int {
        return x + base;
    });
    
    expect_eq(engine.execute("add_base(50);").as<int>(), 150);
    
    // Changing base shouldn't affect the captured value
    base = 200;
    expect_eq(engine.execute("add_base(50);").as<int>(), 150);
}

JAI_TEST(lambda_captures_by_reference) {
    Engine engine;
    
    int counter = 0;
    engine.addFunction("inc", [&counter]() -> int {
        return ++counter;
    });
    
    expect_eq(engine.execute("inc();").as<int>(), 1);
    expect_eq(engine.execute("inc();").as<int>(), 2);
    expect_eq(counter, 2);
    
    // Direct modification should be visible
    counter = 10;
    expect_eq(engine.execute("inc();").as<int>(), 11);
}

JAI_TEST(shared_ptr_captures_for_persistent_state) {
    Engine engine;
    
    auto state = std::make_shared<int>(0);
    engine.addFunction("next", [state]() -> int {
        return ++(*state);
    });
    
    engine.addFunction("reset", [state]() -> void {
        *state = 0;
    });
    
    engine.addFunction("get", [state]() -> int {
        return *state;
    });
    
    expect_eq(engine.execute("next();").as<int>(), 1);
    expect_eq(engine.execute("next();").as<int>(), 2);
    expect_eq(engine.execute("get();").as<int>(), 2);
    
    engine.execute("reset();");
    expect_eq(engine.execute("get();").as<int>(), 0);
}

JAI_TEST(mixed_parameter_types) {
    Engine engine;
    
    engine.addFunction("format_record", [](int id, const std::string& name, float score, bool active) -> std::string {
        return "Record[id=" + std::to_string(id) + 
               ", name=" + name + 
               ", score=" + std::to_string(score).substr(0, 4) + 
               ", active=" + (active ? "true" : "false") + "]";
    });
    
    Value result = engine.execute("format_record(42, \"Alice\", 95.5, true);");
    std::string str = result.as<std::string>();
    expect_true(str.find("id=42") != std::string::npos);
    expect_true(str.find("name=Alice") != std::string::npos);
    expect_true(str.find("score=95.5") != std::string::npos);
    expect_true(str.find("active=true") != std::string::npos);
}

JAI_TEST(type_conversions_int_float) {
    Engine engine;
    
    // Test that int/float conversions work
    engine.addFunction("as_int", [](float f) -> int {
        return static_cast<int>(f);
    });
    
    engine.addFunction("as_float", [](int i) -> float {
        return static_cast<float>(i);
    });
    
    expect_eq(engine.execute("as_int(3.7);").as<int>(), 3);
    expect_near(engine.execute("as_float(42);").as<float>(), 42.0f, 0.001f);
}

JAI_TEST(error_handling_in_bound_functions) {
    Engine engine;
    
    engine.addFunction("safe_divide", [](float a, float b) -> float {
        if (b == 0.0f) {
            throw RuntimeError("Division by zero in safe_divide");
        }
        return a / b;
    });
    
    Value result = engine.execute("safe_divide(10.0, 2.0);");
    expect_near(result.as<float>(), 5.0f, 0.001f);
    
    auto testFunc = [&]() { engine.execute("safe_divide(10.0, 0.0);"); };
    expect_throws<decltype(testFunc), RuntimeError>(std::move(testFunc));
}

JAI_TEST(complex_expression_with_bound_operators) {
    Engine engine;
    
    // Register a full set of operators
    engine.addFunction("+", [](int a, int b) -> int { return a + b; });
    engine.addFunction("-", [](int a, int b) -> int { return a - b; });
    engine.addFunction("*", [](int a, int b) -> int { return a * b; });
    engine.addFunction("/", [](int a, int b) -> int { return a / b; });
    engine.addFunction("==", [](int a, int b) -> bool { return a == b; });
    engine.addFunction("!=", [](int a, int b) -> bool { return a != b; });
    engine.addFunction("<", [](int a, int b) -> bool { return a < b; });
    engine.addFunction(">", [](int a, int b) -> bool { return a > b; });
    
    // Test complex expression
    Value result = engine.execute(R"(
        let x = 5;
        let y = 3;
        let z = x * y + 10;
        let check = z == 25;
        check;
    )");
    expect_eq(result.as<bool>(), true);
}

JAI_TEST(overloaded_functions_by_arity) {
    Engine engine;
    
    // Register functions with different arities
    engine.addFunction("sum", []() -> int { return 0; });
    engine.addFunction("sum", [](int a) -> int { return a; });
    engine.addFunction("sum", [](int a, int b) -> int { return a + b; });
    engine.addFunction("sum", [](int a, int b, int c) -> int { return a + b + c; });
    
    expect_eq(engine.execute("sum();").as<int>(), 0);
    expect_eq(engine.execute("sum(5);").as<int>(), 5);
    expect_eq(engine.execute("sum(5, 3);").as<int>(), 8);
    expect_eq(engine.execute("sum(5, 3, 2);").as<int>(), 10);
}

JAI_TEST(function_with_default_arguments) {
    Engine engine;
    
    // Simulate default arguments by registering multiple overloads
    engine.addFunction("greet", [](const std::string& name) -> std::string {
        return "Hello, " + name + "!";
    });
    
    engine.addFunction("greet", []() -> std::string {
        return "Hello, World!";  // Default behavior
    });
    
    expect_eq(engine.execute("greet(\"Alice\");").as<std::string>(), std::string("Hello, Alice!"));
    expect_eq(engine.execute("greet();").as<std::string>(), std::string("Hello, World!"));
}

JAI_TEST(arithmetic_operator_bindings) {
    Engine engine;
    
    engine.addFunction("+", [](int a, int b) -> int { return a + b; });
    engine.addFunction("+", [](float a, float b) -> float { return a + b; });
    engine.addFunction("+", [](const std::string& a, const std::string& b) -> std::string { return a + b; });
    
    expect_eq(engine.execute("5 + 3;").as<int>(), 8);
    expect_eq(engine.execute("2.5 + 3.5;").as<float>(), 6.0f);
    expect_eq(engine.execute("\"Hello\" + \" World\";").as<std::string>(), std::string("Hello World"));
}

JAI_TEST(comparison_operator_bindings) {
    Engine engine;
    
    engine.addFunction("==", [](int a, int b) -> bool { return a == b; });
    engine.addFunction("!=", [](int a, int b) -> bool { return a != b; });
    engine.addFunction("<", [](int a, int b) -> bool { return a < b; });
    engine.addFunction(">", [](int a, int b) -> bool { return a > b; });
    engine.addFunction("<=", [](int a, int b) -> bool { return a <= b; });
    engine.addFunction(">=", [](int a, int b) -> bool { return a >= b; });
    
    expect_eq(engine.execute("5 == 5;").as<bool>(), true);
    expect_eq(engine.execute("5 != 3;").as<bool>(), true);
    expect_eq(engine.execute("3 < 5;").as<bool>(), true);
    expect_eq(engine.execute("7 > 5;").as<bool>(), true);
    expect_eq(engine.execute("3 <= 5;").as<bool>(), true);
    expect_eq(engine.execute("7 >= 5;").as<bool>(), true);
}

JAI_TEST(custom_operator_behaviors) {
    Engine engine;
    
    // String multiplication (repeat)
    engine.addFunction("*", [](const std::string& str, int count) -> std::string {
        std::string result;
        for (int i = 0; i < count; i++) {
            result += str;
        }
        return result;
    });
    
    expect_eq(engine.execute("\"abc\" * 3;").as<std::string>(), std::string("abcabcabc"));
    
    // Division with float result for integers
    engine.addFunction("/", [](int a, int b) -> float {
        if (b == 0) throw RuntimeError("Division by zero");
        return static_cast<float>(a) / b;
    });
    
    expect_eq(engine.execute("10 / 4;").as<float>(), 2.5f);
}

JAI_TEST(operator_precedence_with_bindings) {
    Engine engine;
    
    engine.addFunction("+", [](int a, int b) -> int { return a + b; });
    engine.addFunction("-", [](int a, int b) -> int { return a - b; });
    engine.addFunction("*", [](int a, int b) -> int { return a * b; });
    engine.addFunction("/", [](int a, int b) -> int { return a / b; });
    
    expect_eq(engine.execute("2 + 3 * 4;").as<int>(), 14);
    expect_eq(engine.execute("10 - 6 / 2;").as<int>(), 7);
    expect_eq(engine.execute("(2 + 3) * 4;").as<int>(), 20);
}

JAI_TEST(chained_operator_calls) {
    Engine engine;
    
    engine.addFunction("+", [](int a, int b) -> int { return a + b; });
    engine.addFunction("+", [](const std::string& a, const std::string& b) -> std::string { return a + b; });
    
    expect_eq(engine.execute("1 + 2 + 3 + 4;").as<int>(), 10);
    expect_eq(engine.execute("\"a\" + \"b\" + \"c\" + \"d\";").as<std::string>(), std::string("abcd"));
}

// Benchmark tests
JAI_BENCHMARK(function_call_overhead) {
    Engine engine;
    
    engine.addFunction("add", [](int a, int b) -> int { return a + b; });
    
    engine.execute(R"(
        int sum = 0;
        for (int i = 0; i < 1000; ++i) {
            sum = add(sum, i);
        }
    )");
}

JAI_BENCHMARK(lambda_capture_performance) {
    Engine engine;
    
    auto state = std::make_shared<std::vector<int>>();
    engine.addFunction("push", [state](int value) -> void {
        state->push_back(value);
    });
    
    engine.execute(R"(
        for (int i = 0; i < 1000; ++i) {
            push(i);
        }
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()