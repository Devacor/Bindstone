#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <memory>

namespace jai::foundry::tests {

class engine_tests : public suite {
public:
    engine_tests() : suite("Core Engine") {}
    
    void forge_tests() override {
        test("engine_creation", [this]() {
            auto eng = make_engine();
            check(true, "Engine should be created successfully");
        });
        
        test("simple_arithmetic", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("2 + 2");
            check_eq(result.as<int>(), 4);
        });
        
        test("variable_definition_and_access", [this]() {
            auto eng = make_engine();
            eng->execute("var x = 42;");
            auto result = eng->execute("x");
            check_eq(result.as<int>(), 42);
        });
        
        test("global_variable_persistence", [this]() {
            auto eng = make_engine();
            eng->add_global("PI", script_value(3.14159, eng.get()));
            auto result = eng->execute("PI * 2");
            check_near(result.as<double>(), 6.28318, 0.00001);
        });
        
        test("function_registration", [this]() {
            auto eng = make_engine();
            eng->add_function("square", [](int n) { return n * n; });
            auto result = eng->execute("square(5)");
            check_eq(result.as<int>(), 25);
        });
        
        test("multiple_executions", [this]() {
            auto eng = make_engine();
            eng->execute("var counter = 0;");
            eng->execute("counter = counter + 1;");
            eng->execute("counter = counter + 1;");
            auto result = eng->execute("counter");
            check_eq(result.as<int>(), 2);
        });
        
        test("error_handling", [this]() {
            auto eng = make_engine();
            check_throws([&]() {
                eng->execute("undefined_variable");
            }, "Should throw on undefined variable");
        });
        
        test("script_defined_functions", [this]() {
            auto eng = make_engine();
            eng->execute("function add(auto a, auto b) -> auto { return a + b; }");
            auto result = eng->execute("add(3, 4)");
            check_eq(result.as<int>(), 7);
        });
        
        test("string_operations", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("\"Hello, \" + \"World!\"");
            check_eq(result.as<std::string>(), "Hello, World!");
        });
        
        test("boolean_operations", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("true && true").as<bool>(), true);
            check_eq(eng->execute("true && false").as<bool>(), false);
            check_eq(eng->execute("false || true").as<bool>(), true);
        });
        
        test("comparison_operators", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("5 > 3").as<bool>(), true);
            check_eq(eng->execute("5 < 3").as<bool>(), false);
            check_eq(eng->execute("5 == 5").as<bool>(), true);
            check_eq(eng->execute("5 != 3").as<bool>(), true);
        });
        
        test("null_handling", [this]() {
            auto eng = make_engine();
            eng->execute("var x = null;");
            check_eq(eng->execute("x == null").as<bool>(), true);
            check_eq(eng->execute("x != null").as<bool>(), false);
        });
        
        test("implicit_return", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("5 * 6");
            check_eq(result.as<int>(), 30);
        });
        
        test("complex_expression", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("(2 + 3) * (4 - 1) / 5");
            check_eq(result.as<int>(), 3);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::engine_tests)