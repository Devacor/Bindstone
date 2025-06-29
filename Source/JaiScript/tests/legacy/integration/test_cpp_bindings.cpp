#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <cmath>
#include <memory>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(CppBindings)

JAI_TEST(operator_overloading_with_correct_types) {
    engine engine;
    
    // Test all basic operators
    engine.add_function("+", [](int a, int b) -> int { return a + b; });
    engine.add_function("-", [](int a, int b) -> int { return a - b; });
    engine.add_function("*", [](int a, int b) -> int { return a * b; });
    engine.add_function("/", [](int a, int b) -> float { return static_cast<float>(a) / b; });
    engine.add_function("%", [](int a, int b) -> int { return a % b; });
    
    expect_eq(engine.execute("10 + 5;").as<int>(), 15);
    expect_eq(engine.execute("10 - 5;").as<int>(), 5);
    expect_eq(engine.execute("10 * 5;").as<int>(), 50);
    expect_near(engine.execute("10 / 3;").as<float>(), 3.333f, 0.001f);
    expect_eq(engine.execute("10 % 3;").as<int>(), 1);
}

JAI_TEST(string_operations_with_const_ref_parameters) {
    engine engine;
    
    // Test that const std::string& parameters work correctly
    engine.add_function("join", [](const std::string& a, const std::string& b, const std::string& sep) -> std::string {
        return a + sep + b;
    });
    
    script_value result = engine.execute("join(\"Hello\", \"World\", \", \");");
    expect_eq(result.as<std::string>(), std::string("Hello, World"));
    
    // Test multiple calls to ensure no parameter corruption
    result = engine.execute("join(\"foo\", \"bar\", \"_\");");
    expect_eq(result.as<std::string>(), std::string("foo_bar"));
}

JAI_TEST(void_functions_with_side_effects) {
    engine engine;
    
    std::vector<std::string> log;
    engine.add_function("log", [&log](const std::string& msg) -> void {
        log.push_back(msg);
    });
    
    script_value result = engine.execute("log(\"First message\");");
    expect_true(result.is_null());
    expect_eq(log.size(), std::size_t(1));
    expect_eq(log[0], std::string("First message"));
    
    engine.execute("log(\"Second\"); log(\"Third\");");
    expect_eq(log.size(), std::size_t(3));
    expect_eq(log[1], std::string("Second"));
    expect_eq(log[2], std::string("Third"));
}

JAI_TEST(lambda_captures_by_value) {
    engine engine;
    
    int base = 100;
    engine.add_function("add_base", [base](int x) -> int {
        return x + base;
    });
    
    expect_eq(engine.execute("add_base(50);").as<int>(), 150);
    
    // Changing base shouldn't affect the captured value
    base = 200;
    expect_eq(engine.execute("add_base(50);").as<int>(), 150);
}

JAI_TEST(lambda_captures_by_reference) {
    engine engine;
    
    int counter = 0;
    engine.add_function("inc", [&counter]() -> int {
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
    engine engine;
    
    auto state = std::make_shared<int>(0);
    engine.add_function("next", [state]() -> int {
        return ++(*state);
    });
    
    engine.add_function("reset", [state]() -> void {
        *state = 0;
    });
    
    engine.add_function("get", [state]() -> int {
        return *state;
    });
    
    expect_eq(engine.execute("next();").as<int>(), 1);
    expect_eq(engine.execute("next();").as<int>(), 2);
    expect_eq(engine.execute("get();").as<int>(), 2);
    
    engine.execute("reset();");
    expect_eq(engine.execute("get();").as<int>(), 0);
}

JAI_TEST(mixed_parameter_types) {
    engine engine;
    
    engine.add_function("format_record", [](int id, const std::string& name, float score, bool active) -> std::string {
        return "Record[id=" + std::to_string(id) + 
               ", name=" + name + 
               ", score=" + std::to_string(score).substr(0, 4) + 
               ", active=" + (active ? "true" : "false") + "]";
    });
    
    script_value result = engine.execute("format_record(42, \"Alice\", 95.5, true);");
    std::string str = result.as<std::string>();
    expect_true(str.find("id=42") != std::string::npos);
    expect_true(str.find("name=Alice") != std::string::npos);
    expect_true(str.find("score=95.5") != std::string::npos);
    expect_true(str.find("active=true") != std::string::npos);
}

JAI_TEST(type_conversions_int_float) {
    engine engine;
    
    // Test that int/float conversions work
    engine.add_function("as_int", [](float f) -> int {
        return static_cast<int>(f);
    });
    
    engine.add_function("as_float", [](int i) -> float {
        return static_cast<float>(i);
    });
    
    expect_eq(engine.execute("as_int(3.7);").as<int>(), 3);
    expect_near(engine.execute("as_float(42);").as<float>(), 42.0f, 0.001f);
}

JAI_TEST(error_handling_in_bound_functions) {
    engine engine;
    
    engine.add_function("safe_divide", [](float a, float b) -> float {
        if (b == 0.0f) {
            throw runtime_error("Division by zero in safe_divide");
        }
        return a / b;
    });
    
    script_value result = engine.execute("safe_divide(10.0, 2.0);");
    expect_near(result.as<float>(), 5.0f, 0.001f);
    
    auto testFunc = [&]() { engine.execute("safe_divide(10.0, 0.0);"); };
    expect_throws<runtime_error>(std::move(testFunc));
}

JAI_TEST(complex_expression_with_bound_operators) {
    engine engine;
    
    // Register a full set of operators
    engine.add_function("+", [](int a, int b) -> int { return a + b; });
    engine.add_function("-", [](int a, int b) -> int { return a - b; });
    engine.add_function("*", [](int a, int b) -> int { return a * b; });
    engine.add_function("/", [](int a, int b) -> int { return a / b; });
    engine.add_function("==", [](int a, int b) -> bool { return a == b; });
    engine.add_function("!=", [](int a, int b) -> bool { return a != b; });
    engine.add_function("<", [](int a, int b) -> bool { return a < b; });
    engine.add_function(">", [](int a, int b) -> bool { return a > b; });
    
    // Test complex expression
    script_value result = engine.execute(R"(
        let x = 5;
        let y = 3;
        let z = x * y + 10;
        let check = z == 25;
        check;
    )");
    expect_eq(result.as<bool>(), true);
}

JAI_TEST(overloaded_functions_by_arity) {
    engine engine;
    
    // Register functions with different arities
    engine.add_function("sum", []() -> int { return 0; });
    engine.add_function("sum", [](int a) -> int { return a; });
    engine.add_function("sum", [](int a, int b) -> int { return a + b; });
    engine.add_function("sum", [](int a, int b, int c) -> int { return a + b + c; });
    
    expect_eq(engine.execute("sum();").as<int>(), 0);
    expect_eq(engine.execute("sum(5);").as<int>(), 5);
    expect_eq(engine.execute("sum(5, 3);").as<int>(), 8);
    expect_eq(engine.execute("sum(5, 3, 2);").as<int>(), 10);
}

JAI_TEST(function_with_default_arguments) {
    engine engine;
    
    // Simulate default arguments by registering multiple overloads
    engine.add_function("greet", [](const std::string& name) -> std::string {
        return "Hello, " + name + "!";
    });
    
    engine.add_function("greet", []() -> std::string {
        return "Hello, World!";  // Default behavior
    });
    
    expect_eq(engine.execute("greet(\"Alice\");").as<std::string>(), std::string("Hello, Alice!"));
    expect_eq(engine.execute("greet();").as<std::string>(), std::string("Hello, World!"));
}

JAI_TEST(arithmetic_operator_bindings) {
    engine engine;
    
    engine.add_function("+", [](int a, int b) -> int { return a + b; });
    engine.add_function("+", [](float a, float b) -> float { return a + b; });
    engine.add_function("+", [](const std::string& a, const std::string& b) -> std::string { return a + b; });
    
    expect_eq(engine.execute("5 + 3;").as<int>(), 8);
    expect_eq(engine.execute("2.5 + 3.5;").as<float>(), 6.0f);
    expect_eq(engine.execute("\"Hello\" + \" World\";").as<std::string>(), std::string("Hello World"));
}

JAI_TEST(comparison_operator_bindings) {
    engine engine;
    
    engine.add_function("==", [](int a, int b) -> bool { return a == b; });
    engine.add_function("!=", [](int a, int b) -> bool { return a != b; });
    engine.add_function("<", [](int a, int b) -> bool { return a < b; });
    engine.add_function(">", [](int a, int b) -> bool { return a > b; });
    engine.add_function("<=", [](int a, int b) -> bool { return a <= b; });
    engine.add_function(">=", [](int a, int b) -> bool { return a >= b; });
    
    expect_eq(engine.execute("5 == 5;").as<bool>(), true);
    expect_eq(engine.execute("5 != 3;").as<bool>(), true);
    expect_eq(engine.execute("3 < 5;").as<bool>(), true);
    expect_eq(engine.execute("7 > 5;").as<bool>(), true);
    expect_eq(engine.execute("3 <= 5;").as<bool>(), true);
    expect_eq(engine.execute("7 >= 5;").as<bool>(), true);
}

JAI_TEST(custom_operator_behaviors) {
    engine engine;
    
    // script_string multiplication (repeat)
    engine.add_function("*", [](const std::string& str, int count) -> std::string {
        std::string result;
        for (int i = 0; i < count; i++) {
            result += str;
        }
        return result;
    });
    
    expect_eq(engine.execute("\"abc\" * 3;").as<std::string>(), std::string("abcabcabc"));
    
    // Division with float result for integers
    engine.add_function("/", [](int a, int b) -> float {
        if (b == 0) throw runtime_error("Division by zero");
        return static_cast<float>(a) / b;
    });
    
    expect_eq(engine.execute("10 / 4;").as<float>(), 2.5f);
}

JAI_TEST(operator_precedence_with_bindings) {
    engine engine;
    
    engine.add_function("+", [](int a, int b) -> int { return a + b; });
    engine.add_function("-", [](int a, int b) -> int { return a - b; });
    engine.add_function("*", [](int a, int b) -> int { return a * b; });
    engine.add_function("/", [](int a, int b) -> int { return a / b; });
    
    expect_eq(engine.execute("2 + 3 * 4;").as<int>(), 14);
    expect_eq(engine.execute("10 - 6 / 2;").as<int>(), 7);
    expect_eq(engine.execute("(2 + 3) * 4;").as<int>(), 20);
}

JAI_TEST(chained_operator_calls) {
    engine engine;
    
    engine.add_function("+", [](int a, int b) -> int { return a + b; });
    engine.add_function("+", [](const std::string& a, const std::string& b) -> std::string { return a + b; });
    
    expect_eq(engine.execute("1 + 2 + 3 + 4;").as<int>(), 10);
    expect_eq(engine.execute("\"a\" + \"b\" + \"c\" + \"d\";").as<std::string>(), std::string("abcd"));
}

// Benchmark tests
JAI_BENCHMARK(function_call_overhead) {
    engine engine;
    
    engine.add_function("add", [](int a, int b) -> int { return a + b; });
    
    engine.execute(R"(
        int sum = 0;
        for (int i = 0; i < 1000; ++i) {
            sum = add(sum, i);
        }
    )");
}

JAI_BENCHMARK(lambda_capture_performance) {
    engine engine;
    
    auto state = std::make_shared<std::vector<int>>();
    engine.add_function("push", [state](int value) -> void {
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