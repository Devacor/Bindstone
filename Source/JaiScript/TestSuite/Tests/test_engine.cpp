#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>

using namespace JaiScript;
using namespace JaiScript::Testing;

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
    Engine engine;
    Value result = engine.execute("2 + 2;");
    expect_eq(result.as<int>(), 4);
}

JAI_TEST(execute_with_variables) {
    Engine engine;
    Value result = engine.execute("var x = 10; var y = 20; x + y;");
    expect_eq(result.as<int>(), 30);
}

JAI_TEST(execute_with_string_operations) {
    Engine engine;
    Value result = engine.execute("\"Hello, \" + \"World!\";");
    expect_eq(result.as<std::string>(), "Hello, World!");
}

JAI_TEST(execute_with_function_definition) {
    Engine engine;
    Value result = engine.execute(R"(
        function add(int a, int b) -> int {
            return a + b;
        }
        add(3, 4);
    )");
    expect_eq(result.as<int>(), 7);
}

JAI_TEST(execute_with_lambda) {
    Engine engine;
    Value result = engine.execute(R"(
        var multiply = [](int a, int b) -> int { return a * b; };
        multiply(5, 6);
    )");
    expect_eq(result.as<int>(), 30);
}

// Basic interpreter tests
JAI_TEST(integer_literal) {
    Engine engine;
    Value result = engine.execute("42;");
    expect_eq(result.as<Int>(), 42);
}

JAI_TEST(float_literal) {
    Engine engine;
    Value result = engine.execute("3.14;");
    expect_eq(result.as<Float>(), 3.14);
}

JAI_TEST(string_literal) {
    Engine engine;
    Value result = engine.execute("\"hello\";");
    expect_eq(result.as<String>(), std::string("hello"));
}

JAI_TEST(boolean_literals) {
    Engine engine;
    expect_eq(engine.execute("true;").as<Bool>(), true);
    expect_eq(engine.execute("false;").as<Bool>(), false);
}

JAI_TEST(null_literal) {
    Engine engine;
    Value result = engine.execute("null;");
    expect_eq(result.isNull(), true);
}

JAI_TEST(basic_arithmetic) {
    Engine engine;
    expect_eq(engine.execute("3 + 4;").as<Int>(), 7);
    expect_eq(engine.execute("10 - 6;").as<Int>(), 4);
    expect_eq(engine.execute("5 * 3;").as<Int>(), 15);
    expect_eq(engine.execute("15 / 3;").as<Float>(), 5.0);
    expect_eq(engine.execute("10 % 3;").as<Int>(), 1);
}

JAI_TEST(float_arithmetic) {
    Engine engine;
    expect_eq(engine.execute("3.5 + 2.5;").as<Float>(), 6.0);
    expect_eq(engine.execute("3 + 2.5;").as<Float>(), 5.5);
}

JAI_TEST(string_concatenation) {
    Engine engine;
    expect_eq(engine.execute("\"hello\" + \" world\";").as<String>(), std::string("hello world"));
    expect_eq(engine.execute("\"Count: \" + 42;").as<String>(), std::string("Count: 42"));
}

JAI_TEST(unary_operators) {
    Engine engine;
    expect_eq(engine.execute("-5;").as<Int>(), -5);
    expect_eq(engine.execute("-3.14;").as<Float>(), -3.14);
    expect_eq(engine.execute("!true;").as<Bool>(), false);
    expect_eq(engine.execute("!false;").as<Bool>(), true);
}

JAI_TEST(comparison_operators) {
    Engine engine;
    expect_eq(engine.execute("3 < 5;").as<Bool>(), true);
    expect_eq(engine.execute("5 > 3;").as<Bool>(), true);
    expect_eq(engine.execute("5 == 5;").as<Bool>(), true);
    expect_eq(engine.execute("5 != 3;").as<Bool>(), true);
}

JAI_TEST(logical_operators) {
    Engine engine;
    expect_eq(engine.execute("true && true;").as<Bool>(), true);
    expect_eq(engine.execute("true && false;").as<Bool>(), false);
    expect_eq(engine.execute("false || true;").as<Bool>(), true);
    expect_eq(engine.execute("false || false;").as<Bool>(), false);
}

JAI_TEST(variable_assignment_and_access) {
    Engine engine;
    expect_eq(engine.execute("x = 5; x;").as<Int>(), 5);
    expect_eq(engine.execute("x = 5; y = 10; x + y;").as<Int>(), 15);
    expect_eq(engine.execute("x = y = 5;").as<Int>(), 5);
}

JAI_TEST(operator_precedence) {
    Engine engine;
    expect_eq(engine.execute("2 + 3 * 4;").as<Int>(), 14);
    expect_eq(engine.execute("(2 + 3) * 4;").as<Int>(), 20);
}

JAI_TEST(executeFile_basic) {
    TempFile script("3 * 7");
    Engine engine;
    Value result = engine.executeFile(script.path());
    expect_eq(result.as<int>(), 21);
}

JAI_TEST(executeFile_with_functions) {
    TempFile script(R"(
        function factorial(int n) -> int {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
        factorial(5);
    )");
    Engine engine;
    Value result = engine.executeFile(script.path());
    expect_eq(result.as<int>(), 120);
}

JAI_TEST(addGlobal) {
    Engine engine;
    engine.addGlobal("PI", Value(3.14159f));
    Value result = engine.execute("PI * 2.0;");
    expect_near(result.as<float>(), 6.28318f, 0.0001f);
}

JAI_TEST(addGlobal_with_modification) {
    Engine engine;
    engine.addGlobal("counter", Value(0));
    engine.execute("counter = counter + 1;");
    Value result = engine.execute("counter;");
    expect_eq(result.as<int>(), 1);
}

JAI_TEST(addFunction_basic) {
    Engine engine;
    engine.addFunction("square", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw RuntimeError("square expects exactly 1 argument");
        }
        int n = args[0].as<int>();
        return Value(n * n);
    });
    Value result = engine.execute("square(8);");
    expect_eq(result.as<int>(), 64);
}

JAI_TEST(addFunction_with_multiple_args) {
    Engine engine;
    engine.addFunction("max", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw RuntimeError("max expects exactly 2 arguments");
        }
        int a = args[0].as<int>();
        int b = args[1].as<int>();
        return Value(a > b ? a : b);
    });
    Value result = engine.execute("max(10, 20);");
    expect_eq(result.as<int>(), 20);
}

JAI_TEST(addFunction_with_string_return) {
    Engine engine;
    engine.addFunction("greet", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw RuntimeError("greet expects exactly 1 argument");
        }
        std::string name = args[0].as<std::string>();
        return Value("Hello, " + name + "!");
    });
    Value result = engine.execute("greet(\"JaiScript\");");
    expect_eq(result.as<std::string>(), "Hello, JaiScript!");
}

JAI_TEST(eval_template_with_int_return) {
    Engine engine;
    auto result = engine.eval<int>("test", "40 + 2;");
    expect_true(result.has_value());
    expect_eq(result.value(), 42);
}

JAI_TEST(eval_template_with_string_return) {
    Engine engine;
    auto result = engine.eval<std::string>("test", "\"test\" + \"ing\";");
    expect_true(result.has_value());
    expect_eq(result.value(), "testing");
}

JAI_TEST(eval_template_with_type_mismatch) {
    Engine engine;
    auto result = engine.eval<int>("test", "\"not a number\";");
    expect_false(result.has_value());
}

JAI_TEST(getState_and_setState) {
    Engine engine1;
    engine1.addGlobal("x", Value(100));
    engine1.addGlobal("name", Value("test"));
    engine1.execute("var y = 200;");
    
    auto state = engine1.getState();
    
    Engine engine2;
    engine2.setState(state);
    
    Value x = engine2.execute("x;");
    expect_eq(x.as<int>(), 100);
    
    Value name = engine2.execute("name;");
    expect_eq(name.as<std::string>(), "test");
    
    Value y = engine2.execute("y;");
    expect_eq(y.as<int>(), 200);
}

JAI_TEST(multiple_engines_isolation) {
    Engine engine1;
    Engine engine2;
    
    engine1.addGlobal("value", Value(111));
    engine2.addGlobal("value", Value(222));
    
    Value result1 = engine1.execute("value;");
    Value result2 = engine2.execute("value;");
    
    expect_eq(result1.as<int>(), 111);
    expect_eq(result2.as<int>(), 222);
}

JAI_TEST(complex_script_execution) {
    Engine engine;
    engine.addGlobal("data", Value(10));
    
    Value result = engine.execute(R"(
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
    Engine engine;
    engine.addFunction("double", [](const std::vector<Value>& args) -> Value {
        return Value(args[0].as<int>() * 2);
    });
    
    engine.addFunction("add", [](const std::vector<Value>& args) -> Value {
        return Value(args[0].as<int>() + args[1].as<int>());
    });
    
    Value result = engine.execute("double(add(3, 4));");
    expect_eq(result.as<int>(), 14);
}

JAI_TEST(error_handling_in_execute) {
    Engine engine;
    expect_throws<RuntimeError>([&]() {
        engine.execute("invalid syntax @#$");
    });
}

JAI_TEST(error_handling_in_executeFile) {
    Engine engine;
    expect_throws<std::runtime_error>([&]() {
        engine.executeFile("non_existent_file.jai");
    });
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()