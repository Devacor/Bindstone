#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class function_tests : public suite {
public:
    function_tests() : suite("Function Tests") {}
    
    void forge_tests() override {
        test("simple_function_declaration_and_call", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto add(auto a, auto b) -> auto {
                    return a + b;
                }
                
                auto result = add(5, 3);
                result;
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 8);
        });

        test("function_with_no_parameters", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto getValue() -> auto {
                    return 42;
                }
                
                getValue();
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 42);
        });

        test("function_with_side_effects", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto x = 10;
                
                auto setX(auto value) -> auto {
                    x = value;
                }
                
                setX(25);
                x;
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 25);
        });

        test("function_with_multiple_parameters", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function multiply(auto a, auto b, auto c) -> auto {
                    return a * b * c;
                }
                
                multiply(2, 3, 4);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 24);
        });

        test("recursive_function", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function factorial(auto n) -> auto {
                    if (n <= 1) {
                        return 1;
                    }
                    return n * factorial(n - 1);
                }
                
                factorial(5);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 120);
        });

        test("function_returning_string", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function greeting(auto name) -> auto {
                    return "Hello, " + name + "!";
                }
                
                greeting("World");
            )";
            
            script_value result = engine->execute(script);
            check(result.is_string());
            check_eq(result.as<std::string>(), "Hello, World!");
        });

        test("nested_function_calls", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function double(auto x) -> auto {
                    return x * 2;
                }
                
                function add(auto a, auto b) -> auto {
                    return a + b;
                }
                
                double(add(3, 4));
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 14); // double(3 + 4) = double(7) = 14
        });

        test("function_with_local_variables", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function compute(auto n) -> auto {
                    auto temp = n * 2;
                    auto result = temp + 5;
                    return result;
                }
                
                compute(10);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 25); // (10 * 2) + 5 = 25
        });

        test("function_with_conditional_return", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function max(auto a, auto b) -> auto {
                    if (a > b) {
                        return a;
                    } else {
                        return b;
                    }
                }
                
                max(15, 10);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 15);
        });

        test("function_with_early_return", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                function findFirst(auto n) -> auto {
                    for (auto i = 0; i < 10; ++i) {
                        if (i == n) {
                            return i;
                        }
                    }
                    return -1;
                }
                
                findFirst(5);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 5);
        });

        test("function_scope_isolation", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto global_var = 100;
                
                function test_scope() -> auto {
                    auto local_var = 50;
                    return local_var + global_var;
                }
                
                test_scope();
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 150);
        });

        test("function_parameter_shadowing", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto x = 10;
                
                function shadow_test(auto x) -> auto {
                    return x * 2;
                }
                
                shadow_test(5);
            )";
            
            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 10); // uses parameter x=5, not global x=10
        });

        test("function_with_different_syntax_styles", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                // C++ style
                auto cpp_style(auto x) -> auto {
                    return x + 1;
                }
                
                // Function keyword style
                function func_style(auto x) -> auto {
                    return x + 2;
                }
                
                // Alternative function style
                function short_style(auto x) -> auto {
                    return x + 3;
                }
                
                cpp_style(10) + func_style(10) + short_style(10);
            )";

            script_value result = engine->execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 36); // 11 + 12 + 13 = 36
        });

        test("default_parameter_basic", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                function add(int a, int b = 10) -> int { return a + b; }
                add(5);
            )");
            check_eq(result.as<int64_t>(), int64_t(15));
        });

        test("default_parameter_override", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                function add(int a, int b = 10) -> int { return a + b; }
                add(5, 20);
            )");
            check_eq(result.as<int64_t>(), int64_t(25));
        });

        test("default_parameter_multiple", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                function make_point(int x = 0, int y = 0, int z = 0) -> int { return x + y + z; }
                make_point() + make_point(1) + make_point(1, 2) + make_point(1, 2, 3);
            )");
            check_eq(result.as<int64_t>(), int64_t(0 + 1 + 3 + 6));
        });

        test("default_parameter_string", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                function greet(string name, string greeting = "hello") -> string {
                    return greeting + " " + name;
                }
                greet("world");
            )");
            check_eq(result.as<std::string>(), std::string("hello world"));
        });

        test("enum_basic", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                enum Direction { north, south, east, west }
                Direction.east;
            )");
            check_eq(result.as<int64_t>(), int64_t(2));
        });

        test("enum_comparison", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                enum Color { red, green, blue }
                auto c = Color.green;
                c == Color.green;
            )");
            check_eq(result.as<bool>(), true);
        });

        test("enum_in_switch", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                enum Weapon { sword, bow, staff }
                auto w = Weapon.bow;
                auto damage = 0;
                switch (w) {
                    case Weapon.sword: damage = 10;
                    case Weapon.bow: damage = 8;
                    case Weapon.staff: damage = 12;
                }
                damage;
            )");
            check_eq(result.as<int64_t>(), int64_t(8));
        });

        test("null_safe_on_null", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                auto obj = null;
                obj?.toString();
            )");
            check(result.is_null());
        });

        test("null_safe_on_value", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                auto s = "hello";
                s?.length();
            )");
            check_eq(result.as<int64_t>(), int64_t(5));
        });

        test("null_safe_chain", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            auto result = eng->execute(R"(
                auto obj = null;
                auto x = obj?.toString();
                x == null;
            )");
            check_eq(result.as<bool>(), true);
        });

        test("destructuring_basic", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto [x, y, z] = [1, 2, 3];
            )");
            check_eq(eng->get_variable("x").as<int64_t>(), int64_t(1));
            check_eq(eng->get_variable("y").as<int64_t>(), int64_t(2));
            check_eq(eng->get_variable("z").as<int64_t>(), int64_t(3));
        });

        test("destructuring_fewer_vars", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto [a, b] = [10, 20, 30];
            )");
            check_eq(eng->get_variable("a").as<int64_t>(), int64_t(10));
            check_eq(eng->get_variable("b").as<int64_t>(), int64_t(20));
        });

        test("destructuring_more_vars", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto [a, b, c] = [10, 20];
            )");
            check_eq(eng->get_variable("a").as<int64_t>(), int64_t(10));
            check_eq(eng->get_variable("b").as<int64_t>(), int64_t(20));
            check(eng->get_variable("c").is_null());
        });

        test("destructuring_from_variable", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto arr = [100, 200];
                auto [x, y] = arr;
            )");
            check_eq(eng->get_variable("x").as<int64_t>(), int64_t(100));
            check_eq(eng->get_variable("y").as<int64_t>(), int64_t(200));
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::function_tests)