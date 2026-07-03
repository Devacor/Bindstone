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

        test("jaibite_execute_returns_value", [this]() {
            auto eng = make_engine();
            auto bite = eng->jaibite("2 + 2");
            check_eq(bite.execute().as<int>(), 4);
            check_eq(bite.execute().as<int>(), 4);   // steady-state re-execution
        });

        test("jaibite_engine_execute_form", [this]() {
            auto eng = make_engine();
            auto bite = eng->jaibite("10 * 3");
            check_eq(eng->execute(bite).as<int>(), 30);
            check_eq(eng->execute(bite).as<int>(), 30);
        });

        test("jaibite_state_persists_across_runs", [this]() {
            auto eng = make_engine();
            eng->execute("var counter = 0;");
            auto bite = eng->jaibite("counter = counter + 1; counter");
            check_eq(bite.execute().as<int>(), 1);
            check_eq(bite.execute().as<int>(), 2);
            check_eq(bite.execute().as<int>(), 3);
            check_eq(eng->execute("counter").as<int>(), 3);
        });

        test("jaibite_loop_and_functions", [this]() {
            auto eng = make_engine();
            auto bite = eng->jaibite(R"(
                function double_it(auto n) -> auto { return n * 2; }
                auto sum = 0;
                for (auto i = 0; i < 100; i += 1) {
                    sum += double_it(i);
                }
                sum
            )");
            check_eq(bite.execute().as<int>(), 9900);
            check_eq(bite.execute().as<int>(), 9900);
        });

        test("jaibite_parse_error_throws_at_creation", [this]() {
            auto eng = make_engine();
            check_throws([&]() {
                auto bite = eng->jaibite("auto x = = 5;");
            }, "Parse errors should surface when the jaibite is created");
        });

        test("jaibite_runtime_error_throws_on_execute", [this]() {
            auto eng = make_engine();
            auto bite = eng->jaibite("undefined_variable_xyz");
            check_throws([&]() { bite.execute(); }, "Runtime errors should throw on execute");
            check_throws([&]() { bite.execute(); }, "And keep throwing on re-execute");
        });

        test("jaibite_outlives_engine_safely", [this]() {
            jai::jaibite bite;
            {
                auto eng = make_engine();
                bite = eng->jaibite("1 + 1");
                check_eq(bite.execute().as<int>(), 2);
            }
            check_throws([&]() { bite.execute(); }, "Executing after engine destruction should throw");
        });

        test("jaibite_class_redefinition_across_runs", [this]() {
            // Re-executing the same AST redefines the class each run (hot-reload path)
            auto eng = make_engine();
            auto bite = eng->jaibite(R"(
                class Point {
                    int x = 7;
                    int doubled() { return x * 2; }
                }
                auto p = Point();
                p.doubled()
            )");
            check_eq(bite.execute().as<int>(), 14);
            check_eq(bite.execute().as<int>(), 14);
            check_eq(bite.execute().as<int>(), 14);
        });

        test("jaibite_try_catch_across_runs", [this]() {
            auto eng = make_engine();
            eng->execute("var caught = 0;");
            auto bite = eng->jaibite(R"(
                try {
                    throw "boom";
                } catch (e) {
                    caught = caught + 1;
                }
                caught
            )");
            check_eq(bite.execute().as<int>(), 1);
            check_eq(bite.execute().as<int>(), 2);
        });

        test("jaibite_coroutine_across_runs", [this]() {
            // Range-for over a STORED coroutine handle (each run mints a fresh handle)
            auto eng = make_engine();
            auto bite = eng->jaibite(R"(
                coroutine int counter() {
                    yield 1;
                    yield 2;
                    yield 3;
                }
                auto c = counter();
                auto total = 0;
                for (auto v : c) {
                    total += v;
                }
                total
            )");
            check_eq(bite.execute().as<int>(), 6);
            check_eq(bite.execute().as<int>(), 6);
        });

        test("jaibite_coroutine_resume_across_runs", [this]() {
            auto eng = make_engine();
            auto bite = eng->jaibite(R"(
                coroutine int counter() {
                    yield 1;
                    yield 2;
                    return 3;
                }
                auto c = counter();
                auto total = c.resume() + c.resume() + c.resume();
                total
            )");
            check_eq(bite.execute().as<int>(), 6);
            check_eq(bite.execute().as<int>(), 6);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::engine_tests)