#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>

using namespace jai;
using namespace jai::test;

class TempFile {
public:
    TempFile(const std::string& content) {
        filename_ = std::tmpnam(nullptr);
        filename_ += ".jai";
        std::ofstream file(filename_);
        file << content;
        file.close();
    }
    
    ~TempFile() {
        std::filesystem::remove(filename_);
    }
    
    const std::string& path() const { return filename_; }
    
private:
    std::string filename_;
};

JAI_TEST_SUITE(EngineAPITests)

JAI_TEST(basic_execution) {
    engine engine;
    script_value result = engine.execute("2 + 2;");
    expect_eq(result.as<int>(), 4);
}

JAI_TEST(execute_with_variables) {
    engine engine;
    script_value result = engine.execute("var x = 10; var y = 20; x + y;");
    expect_eq(result.as<int>(), 30);
}

JAI_TEST(execute_with_string_operations) {
    engine engine;
    script_value result = engine.execute("\"Hello, \" + \"World!\";");
    expect_eq(result.as<std::string>(), "Hello, World!");
}

JAI_TEST(execute_with_function_definition) {
    engine engine;
    script_value result = engine.execute(R"(
        function add(int a, int b) -> int {
            return a + b;
        }
        add(3, 4);
    )");
    expect_eq(result.as<int>(), 7);
}

JAI_TEST(execute_with_lambda) {
    engine engine;
    script_value result = engine.execute(R"(
        var multiply = [](int a, int b) -> int { return a * b; };
        multiply(5, 6);
    )");
    expect_eq(result.as<int>(), 30);
}

// Basic interpreter tests
JAI_TEST(integer_literal) {
    engine engine;
    script_value result = engine.execute("42;");
    expect_eq(result.as<script_int>(), 42);
}

JAI_TEST(float_literal) {
    engine engine;
    script_value result = engine.execute("3.14;");
    expect_eq(result.as<script_float>(), 3.14);
}

JAI_TEST(string_literal) {
    engine engine;
    script_value result = engine.execute("\"hello\";");
    expect_eq(result.as<script_string>(), std::string("hello"));
}

JAI_TEST(boolean_literals) {
    engine engine;
    expect_eq(engine.execute("true;").as<script_bool>(), true);
    expect_eq(engine.execute("false;").as<script_bool>(), false);
}

JAI_TEST(null_literal) {
    engine engine;
    script_value result = engine.execute("null;");
    expect_eq(result.is_null(), true);
}

JAI_TEST(basic_arithmetic) {
    engine engine;
    expect_eq(engine.execute("3 + 4;").as<script_int>(), 7);
    expect_eq(engine.execute("10 - 6;").as<script_int>(), 4);
    expect_eq(engine.execute("5 * 3;").as<script_int>(), 15);
    expect_eq(engine.execute("15 / 3;").as<script_int>(), 5);  // Integer division returns integer
    expect_eq(engine.execute("10 % 3;").as<script_int>(), 1);
}

JAI_TEST(float_arithmetic) {
    engine engine;
    expect_eq(engine.execute("3.5 + 2.5;").as<script_float>(), 6.0);
    expect_eq(engine.execute("3 + 2.5;").as<script_float>(), 5.5);
    expect_eq(engine.execute("15.0 / 3.0;").as<script_float>(), 5.0);  // script_float division
    expect_eq(engine.execute("15.0 / 3;").as<script_float>(), 5.0);    // Mixed promotes to float
}

JAI_TEST(string_concatenation) {
    engine engine;
    expect_eq(engine.execute("\"hello\" + \" world\";").as<script_string>(), std::string("hello world"));
    expect_eq(engine.execute("\"Count: \" + 42;").as<script_string>(), std::string("Count: 42"));
}

JAI_TEST(unary_operators) {
    engine engine;
    expect_eq(engine.execute("-5;").as<script_int>(), -5);
    expect_eq(engine.execute("-3.14;").as<script_float>(), -3.14);
    expect_eq(engine.execute("!true;").as<script_bool>(), false);
    expect_eq(engine.execute("!false;").as<script_bool>(), true);
}

JAI_TEST(comparison_operators) {
    engine engine;
    expect_eq(engine.execute("3 < 5;").as<script_bool>(), true);
    expect_eq(engine.execute("5 > 3;").as<script_bool>(), true);
    expect_eq(engine.execute("5 == 5;").as<script_bool>(), true);
    expect_eq(engine.execute("5 != 3;").as<script_bool>(), true);
}

JAI_TEST(logical_operators) {
    engine engine;
    expect_eq(engine.execute("true && true;").as<script_bool>(), true);
    expect_eq(engine.execute("true && false;").as<script_bool>(), false);
    expect_eq(engine.execute("false || true;").as<script_bool>(), true);
    expect_eq(engine.execute("false || false;").as<script_bool>(), false);
}

JAI_TEST(variable_assignment_and_access) {
    engine engine;
    expect_eq(engine.execute("var x = 5; x;").as<script_int>(), 5);
    expect_eq(engine.execute("var x = 5; var y = 10; x + y;").as<script_int>(), 15);
    expect_eq(engine.execute("var x; var y; x = y = 5;").as<script_int>(), 5);
}

JAI_TEST(operator_precedence) {
    engine engine;
    expect_eq(engine.execute("2 + 3 * 4;").as<script_int>(), 14);
    expect_eq(engine.execute("(2 + 3) * 4;").as<script_int>(), 20);
}

JAI_TEST(execute_file_basic) {
    TempFile script("3 * 7");
    engine engine;
    script_value result = engine.execute_file(script.path());
    expect_eq(result.as<int>(), 21);
}

JAI_TEST(execute_file_with_functions) {
    TempFile script(R"(
        function factorial(int n) -> int {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
        factorial(5);
    )");
    engine engine;
    script_value result = engine.execute_file(script.path());
    expect_eq(result.as<int>(), 120);
}

JAI_TEST(add_global) {
    engine engine;
    engine.add_global("PI", script_value(script_float(3.14159)));
    script_value result = engine.execute("PI * 2.0;");
    expect_near(result.as<float>(), 6.28318f, 0.0001f);
}

JAI_TEST(add_global_with_modification) {
    engine engine;
    engine.add_global("counter", script_value(script_int(0)));
    engine.execute("counter = counter + 1;");
    script_value result = engine.execute("counter;");
    expect_eq(result.as_int(), script_int(1));
}

JAI_TEST(add_function_basic) {
    engine engine;
    engine.add_function("square", [](script_int n) -> script_int {
        return n * n;
    });
    script_value result = engine.execute("square(8);");
    expect_eq(result.as_int(), script_int(64));
}

JAI_TEST(add_function_with_multiple_args) {
    engine engine;
    engine.add_function("max", [](script_int a, script_int b) -> script_int {
        return a > b ? a : b;
    });
    script_value result = engine.execute("max(10, 20);");
    expect_eq(result.as_int(), script_int(20));
}

JAI_TEST(add_function_with_string_return) {
    engine engine;
    engine.add_function("greet", [](const script_string& name) -> script_string {
        return "Hello, " + name + "!";
    });
    script_value result = engine.execute("greet(\"JaiScript\");");
    expect_eq(result.as_string(), "Hello, JaiScript!");
}

JAI_TEST(execute_with_int_return) {
    engine engine;
    auto result = engine.execute("40 + 2;");
    expect_eq(result.as_int(), script_int(42));
}

JAI_TEST(execute_with_string_return) {
    engine engine;
    auto result = engine.execute("\"test\" + \"ing\";");
    expect_eq(result.as_string(), "testing");
}

JAI_TEST(execute_with_type_mismatch) {
    engine engine;
    auto result = engine.execute("\"not a number\";");
    expect_true(result.is_string());
    expect_false(result.is_int());
}

JAI_TEST(get_state_and_set_state) {
    engine engine1;
    engine1.add_global("x", script_value(script_int(100)));
    engine1.add_global("name", script_value("test"));
    engine1.execute("var y = 200;");
    
    auto state = engine1.get_state();
    
    engine engine2;
    engine2.set_state(state);
    
    script_value x = engine2.execute("x;");
    expect_eq(x.as<int>(), 100);
    
    script_value name = engine2.execute("name;");
    expect_eq(name.as<std::string>(), "test");
    
    script_value y = engine2.execute("y;");
    expect_eq(y.as<int>(), 200);
}

JAI_TEST(multiple_engines_isolation) {
    engine engine1;
    engine engine2;
    
    engine1.add_global("value", script_value(script_int(111)));
    engine2.add_global("value", script_value(script_int(222)));
    
    script_value result1 = engine1.execute("value;");
    script_value result2 = engine2.execute("value;");
    
    expect_eq(result1.as<int>(), 111);
    expect_eq(result2.as<int>(), 222);
}

JAI_TEST(complex_script_execution) {
    engine engine;
    engine.add_global("data", script_value(script_int(10)));
    
    script_value result = engine.execute(R"(
        function processData(int n) -> int {
            var sum = 0;
            var i = 1;
            while (i <= n) {
                sum = sum + i;
                i = i + 1;
            }
            return sum;
        }
        
        var result = processData(data);
        result * 2;
    )");
    
    expect_eq(result.as<int>(), 110); // (1+2+...+10) * 2 = 55 * 2 = 110
}

JAI_TEST(nested_function_calls) {
    engine engine;
    engine.add_function("double", [](script_int x) -> script_int {
        return x * 2;
    });
    
    engine.add_function("add", [](script_int a, script_int b) -> script_int {
        return a + b;
    });
    
    script_value result = engine.execute("double(add(3, 4));");
    expect_eq(result.as_int(), script_int(14));
}

JAI_TEST(error_handling_in_execute) {
    engine engine;
    try {
        engine.execute("undefinedVariable");
        expect_true(false); // Should have thrown
    } catch (const runtime_error&) {
        expect_true(true); // Expected
    }
}

JAI_TEST(error_handling_in_execute_file) {
    engine engine;
    try {
        engine.execute_file("non_existent_file.jai");
        expect_true(false); // Should have thrown
    } catch (const runtime_error&) {
        expect_true(true); // Expected - execute_file throws runtime_error not std::runtime_error
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()