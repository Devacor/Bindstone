#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>

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


        // ---- C++ return-type-first free functions with class types (top level) ----

        test("class_typed_return_first_top_level_call", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class Point { int x = 5; }
                Point mk() { return Point(); }
                mk().x;
            )");
            check_eq(int64_t(5), r.as_int());
        });

        test("class_typed_return_first_assign_and_pass", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class Point { int x = 6; }
                Point mk() { return Point{}; }
                function call0(g) { return g(); }
                var f = mk;
                f().x + call0(mk).x;
            )");
            check_eq(int64_t(12), r.as_int());
        });

        test("class_typed_return_type_enforced", [this]() {
            auto engine = make_engine();
            check_throws([&]() {
                engine->execute(R"(
                    class P { int x = 1; }
                    class Q { int y = 2; }
                    P mk() { return Q(); }
                    mk();
                )");
            });
        });

        test("class_typed_variable_decl_still_a_variable", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class Point { int x = 8; }
                Point p = Point();
                p.x;
            )");
            check_eq(int64_t(8), r.as_int());
        });

        test("class_typed_return_first_redefinition_last_wins", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class P { int x = 1; }
                P mk() { return P(); }
                P mk() { var p = P(); p.x = 2; return p; }
                mk().x;
            )");
            check_eq(int64_t(2), r.as_int());
        });

        // ---- untyped parameter defaults ----

        test("untyped_param_default_free_function", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(3), engine->execute("function f(x = 3) { return x; } f();").as_int());
            check_eq(int64_t(10), engine->execute("function g(x = 3) { return x; } g(10);").as_int());
        });

        test("untyped_param_default_lambda", [this]() {
            auto engine = make_engine();
            auto r = engine->execute("auto f = [](x = 8) { return x; }; f() + f(1);");
            check_eq(int64_t(9), r.as_int());
        });

        test("untyped_param_default_mixed", [this]() {
            auto engine = make_engine();
            auto r = engine->execute("function f(x, y = 4) { return x + y; } f(1) + f(1, 1);");
            check_eq(int64_t(7), r.as_int());
        });

        test("untyped_param_default_ordering_still_enforced", [this]() {
            auto engine = make_engine();
            // parse error surfaces as a throw (probed both backends); engine stays usable
            check_throws([&]() {
                engine->execute("function f(x = 1, y) { return y; } f(2);");
            });
            check_eq(int64_t(4), engine->execute("2 + 2").as_int());
        });

        // ---- function-typed variables and fields ----

        test("function_typed_variable_top_level", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                function f = [](x) { return x * 2; };
                f(21);
            )");
            check_eq(int64_t(42), r.as_int());
        });

        test("function_typed_variable_uninitialized_then_assigned", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                function f;
                f = [](){ return 7; };
                f();
            )");
            check_eq(int64_t(7), r.as_int());
        });

        test("function_typed_variable_holds_named_function", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                function g() { return 3; }
                function f = g;
                f();
            )");
            check_eq(int64_t(3), r.as_int());
        });

        test("function_typed_variable_in_function_body", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                function outer() {
                    function f = [](x) { return x + 1; };
                    return f(41);
                }
                outer();
            )");
            check_eq(int64_t(42), r.as_int());
        });

        test("function_typed_field", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class K { function cb = [](){ return 8; }; }
                K().cb();
            )");
            check_eq(int64_t(8), r.as_int());
        });

        test("function_typed_field_null_then_assigned", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class K { function cb; }
                auto k = K();
                k.cb = [](){ return 9; };
                k.cb();
            )");
            check_eq(int64_t(9), r.as_int());
        });

        test("function_operator_assign_still_a_method", [this]() {
            auto engine = make_engine();
            auto r = engine->execute(R"(
                class V { int x = 0; function operator=(o) { x = o.x + 100; return this; } }
                var a = V();
                var b = V();
                b.x = 1;
                a = b;
                a.x;
            )");
            // pins today's behavior (probed both backends): plain variable assignment
            // does NOT dispatch script operator=; a becomes a clone of b (x == 1).
            // The point of this test: `function operator=` must keep parsing as a METHOD
            // and never fall into the function-typed-field lookahead.
            check_eq(int64_t(1), r.as_int());
        });

        test("function_keyword_declaration_unchanged", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(42), engine->execute("function g(x) { return x * 2; } g(21);").as_int());
        });

        // ---- return-type conflicts (Dev ruling 2026-07: contradictory leading +
        // trailing types = parse error; a matching pair is redundant-legal) ----

        test("return_type_conflict_is_parse_error", [this]() {
            auto engine = make_engine();
            check_throws([&]() { engine->execute("int f() -> string { return \"s\"; } f();"); });
            check_throws([&]() { engine->execute("class K { int m() -> string { return \"s\"; } } K().m();"); });
            check_throws([&]() { engine->execute("namespace n { int f() -> string { return \"s\"; } } n::f();"); });
        });

        test("return_type_redundant_match_ok", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(4), engine->execute("int f() -> int { return 4; } f();").as_int());
            check_eq(int64_t(5), engine->execute("function g() -> int { return 5; } g();").as_int());
            check_eq(int64_t(6), engine->execute("auto h() -> int { return 6; } h();").as_int());
            check_eq(int64_t(7), engine->execute("class K { int m() -> int { return 7; } } K().m();").as_int());
        });

        test("arrow_auto_keeps_leading_type", [this]() {
            auto engine = make_engine();
            // -> { adds no information; the declared int still converts/enforces
            check_eq(int64_t(2), engine->execute("int f() -> { return 2.9; } f();").as_int());
            check_throws([&]() { engine->execute("int g() -> { return \"x\"; } g();"); });
        });

        // ---- initializer lists are constructor-only (Dev ruling 2026-07) ----

        test("initializer_list_only_on_constructors", [this]() {
            auto engine = make_engine();
            check_throws([&]() { engine->execute("function f() : super(1) { return 3; } f();"); });
            check_throws([&]() { engine->execute("int f() : this(1) { return 3; } f();"); });
            check_throws([&]() { engine->execute("class K { function m() : super(1) { return 1; } } 1;"); });
            check_throws([&]() { engine->execute("class A { int v = 0; } class B : A { int m() : super() { return 1; } } 1;"); });
            check_throws([&]() { engine->execute("class K { ~K() : super() {} } 1;"); });
        });

        test("constructor_initializers_still_work", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(15), engine->execute(
                "class K { int a = 0; K(int v) { a = v; } K() : this(15) {} } K().a;").as_int());
            check_eq(int64_t(9), engine->execute(
                "class A { int v = 0; A(int x) { v = x; } } class B : A { B() : super(9) {} } B().v;").as_int());
        });

        // ---- duplicate top-level definitions warn; include composition is silent
        // (Dev ruling 2026-07: warn severity always, scoped to one textual parse unit) ----

        auto duplicate_warnings = [](jai::engine& eng) {
            size_t n = 0;
            for (const auto& d : eng.last_check_diagnostics().diagnostics) {
                if (d.severity == jai::check_diagnostic::level::warning &&
                    d.message.find("duplicate definition") != std::string::npos) { n++; }
            }
            return n;
        };

        test("duplicate_top_level_definition_warns", [&, this]() {
            auto engine = make_engine();
            engine->static_checking(jai::check_mode::warn);
            check_eq(int64_t(2), engine->execute(
                "function f(a) { return 1; } function f(a, b) { return 2; } f(0, 0);").as_int());
            check_eq(size_t(1), duplicate_warnings(*engine));
        });

        test("duplicate_warning_spans_definition_forms", [&, this]() {
            auto engine = make_engine();
            engine->static_checking(jai::check_mode::warn);
            engine->execute("int f(int a) { return 1; } coroutine function f() { yield 1; } 1;");
            check_eq(size_t(1), duplicate_warnings(*engine));
        });

        test("duplicate_warning_silent_for_legit_overloads", [&, this]() {
            auto engine = make_engine();
            engine->static_checking(jai::check_mode::warn);
            engine->execute(
                "namespace n { int f() { return 1; } int f(int a) { return 2; } }"
                "class K { int m(a) { return 1; } int m(a, b) { return 2; } } n::f();");
            check_eq(size_t(0), duplicate_warnings(*engine));
        });

        test("duplicate_warning_silent_across_executes", [&, this]() {
            auto engine = make_engine();
            engine->static_checking(jai::check_mode::warn);
            engine->execute("function f(a) { return 1; } f(0);");
            engine->execute("function f(a) { return 2; } f(0);");   // hot-reload shape
            check_eq(size_t(0), duplicate_warnings(*engine));
        });

        test("duplicate_warning_silent_for_include_composition", [&, this]() {
            namespace fs = std::filesystem;
            auto dir = fs::temp_directory_path();
            auto file = dir / "fn_dupwarn_lib.jai";
            { std::ofstream out(file); out << "function f(a) { return 1; }\n"; }

            auto engine = make_engine();
            engine->add_include_path(dir.string());
            engine->static_checking(jai::check_mode::warn);
            // file defines f once; this unit includes it TWICE and defines its own f
            // once - all composition, zero textual duplicates, zero warnings
            auto r = engine->execute(
                "include \"fn_dupwarn_lib.jai\"; include \"fn_dupwarn_lib.jai\";"
                "function f(a, b) { return a + b; } f(1, 2);");
            check_eq(int64_t(3), r.as_int());
            check_eq(size_t(0), duplicate_warnings(*engine));
            fs::remove(file);
        });

        test("duplicate_warning_never_blocks_strict_mode", [&, this]() {
            auto engine = make_engine();
            engine->static_checking(jai::check_mode::strict);
            check_eq(int64_t(2), engine->execute(
                "function f(a) { return 1; } function f(a) { return 2; } f(0);").as_int());
            check_eq(size_t(1), duplicate_warnings(*engine));
        });

        // ---- anonymous function expressions: `function (params) {...}` ----
        // Pure desugar to a no-capture lambda ([]-equivalent auto-capture:
        // enclosing-function locals snapshot by value at creation, globals live)

        test("anonymous_function_assign_and_call", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(42), engine->execute(
                "var f = function(x) { return x * 2; }; f(21);").as_int());
            check_eq(int64_t(7), engine->execute(
                "auto g = function(a, b) { return a + b; }; g(3, 4);").as_int());
            check_eq(int64_t(9), engine->execute(
                "function h = function() { return 9; }; h();").as_int());
        });

        test("anonymous_function_as_argument", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(42), engine->execute(
                "function apply(g, v) { return g(v); }"
                "apply(function(x) { return x + 1; }, 41);").as_int());
        });

        test("anonymous_function_immediately_invoked", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(42), engine->execute(
                "function(x) { return x + 1; }(41);").as_int());
        });

        test("anonymous_function_trailing_return", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(5), engine->execute(
                "var f = function(a) -> int { return a; }; f(5);").as_int());
            check_eq(int64_t(6), engine->execute(
                "var g = function(a) -> { return a; }; g(6);").as_int());
            check_throws([&]() {
                engine->execute("var h = function() -> int { return \"x\"; }; h();");
            });
        });

        test("anonymous_function_capture_matches_empty_lambda", [this]() {
            auto engine = make_engine();
            // whatever [] auto-capture yields for enclosing locals, function() must match
            auto r = engine->execute(R"(
                function outer() {
                    int a = 1;
                    var f = function() { return a; };
                    var g = [](){ return a; };
                    a = 2;
                    return f() * 10 + g();
                }
                outer();
            )");
            // 11 = both snapshot the enclosing local BY VALUE at creation ([] parity)
            check_eq(int64_t(11), r.as_int());
        });

        test("anonymous_function_default_param", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(8), engine->execute(
                "var f = function(x = 8) { return x; }; f();").as_int());
        });

        test("anonymous_function_returned_and_nested", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(5), engine->execute(
                "var adder = function(x) { return function(y) { return x + y; }; };"
                "adder(2)(3);").as_int());
        });

        test("anonymous_function_statement_position_discarded", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(1), engine->execute(
                "function(x) { return x; }; 1;").as_int());
        });

        test("anonymous_function_named_form_still_error", [this]() {
            auto engine = make_engine();
            check_throws([&]() {
                engine->execute("var f = function g(x) { return x; }; f(1);");
            });
        });

        test("anonymous_function_coroutine_not_supported", [this]() {
            auto engine = make_engine();
            // no coroutine lambdas -> no coroutine anonymous functions (documented)
            check_throws([&]() {
                engine->execute("var f = coroutine function() { yield 1; }; 1;");
            });
        });

        test("anonymous_function_declarations_unchanged", [this]() {
            auto engine = make_engine();
            check_eq(int64_t(42), engine->execute(
                "function f(x) { return x * 2; } f(21);").as_int());
            check_eq(int64_t(3), engine->execute(
                "function h = [](){ return 3; }; h();").as_int());
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