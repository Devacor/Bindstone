#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class function_tests : public suite {
public:
    function_tests() : suite("Function Tests") {}
    
    void forge_tests() override {
        test("simple_function_declaration_and_call", [this]() {
            engine engine;
            
            std::string script = R"(
                auto add(auto a, auto b) -> auto {
                    return a + b;
                }
                
                auto result = add(5, 3);
                result;
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 8);
        });

        test("function_with_no_parameters", [this]() {
            engine engine;
            
            std::string script = R"(
                auto getValue() -> auto {
                    return 42;
                }
                
                getValue();
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 42);
        });

        test("function_with_side_effects", [this]() {
            engine engine;
            
            std::string script = R"(
                auto x = 10;
                
                auto setX(auto value) -> auto {
                    x = value;
                }
                
                setX(25);
                x;
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 25);
        });

        test("function_with_multiple_parameters", [this]() {
            engine engine;
            
            std::string script = R"(
                function multiply(auto a, auto b, auto c) -> auto {
                    return a * b * c;
                }
                
                multiply(2, 3, 4);
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 24);
        });

        test("recursive_function", [this]() {
            engine engine;
            
            std::string script = R"(
                function factorial(auto n) -> auto {
                    if (n <= 1) {
                        return 1;
                    }
                    return n * factorial(n - 1);
                }
                
                factorial(5);
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 120);
        });

        test("function_returning_string", [this]() {
            engine engine;
            
            std::string script = R"(
                function greeting(auto name) -> auto {
                    return "Hello, " + name + "!";
                }
                
                greeting("World");
            )";
            
            script_value result = engine.execute(script);
            check(result.is_string());
            check_eq(result.as<std::string>(), "Hello, World!");
        });

        test("nested_function_calls", [this]() {
            engine engine;
            
            std::string script = R"(
                function double(auto x) -> auto {
                    return x * 2;
                }
                
                function add(auto a, auto b) -> auto {
                    return a + b;
                }
                
                double(add(3, 4));
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 14); // double(3 + 4) = double(7) = 14
        });

        test("function_with_local_variables", [this]() {
            engine engine;
            
            std::string script = R"(
                function compute(auto n) -> auto {
                    auto temp = n * 2;
                    auto result = temp + 5;
                    return result;
                }
                
                compute(10);
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 25); // (10 * 2) + 5 = 25
        });

        test("function_with_conditional_return", [this]() {
            engine engine;
            
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
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 15);
        });

        test("function_with_early_return", [this]() {
            engine engine;
            
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
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 5);
        });

        test("function_scope_isolation", [this]() {
            engine engine;
            
            std::string script = R"(
                auto global_var = 100;
                
                function test_scope() -> auto {
                    auto local_var = 50;
                    return local_var + global_var;
                }
                
                test_scope();
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 150);
        });

        test("function_parameter_shadowing", [this]() {
            engine engine;
            
            std::string script = R"(
                auto x = 10;
                
                function shadow_test(auto x) -> auto {
                    return x * 2;
                }
                
                shadow_test(5);
            )";
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 10); // uses parameter x=5, not global x=10
        });

        test("function_with_different_syntax_styles", [this]() {
            engine engine;
            
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
            
            script_value result = engine.execute(script);
            check(result.is_int());
            check_eq(result.as<int>(), 36); // 11 + 12 + 13 = 36
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::function_tests)