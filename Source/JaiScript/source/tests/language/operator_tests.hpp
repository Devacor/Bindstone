#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <memory>
#include <cmath>

namespace jai::foundry::tests {

class operator_tests : public suite {
public:
    operator_tests() : suite("Operators") {}
    
    void forge_tests() override {
        // Arithmetic operators
        test("basic_arithmetic", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("10 + 5").as<int>(), 15);
            check_eq(eng->execute("10 - 5").as<int>(), 5);
            check_eq(eng->execute("10 * 5").as<int>(), 50);
            check_eq(eng->execute("10 / 5").as<int>(), 2);
            check_eq(eng->execute("10 % 3").as<int>(), 1);
        });
        
        test("float_arithmetic", [this]() {
            auto eng = make_engine();
            check_near(eng->execute("10.5 + 5.5").as<double>(), 16.0, 0.0001);
            check_near(eng->execute("10.5 - 5.5").as<double>(), 5.0, 0.0001);
            check_near(eng->execute("10.5 * 2.0").as<double>(), 21.0, 0.0001);
            check_near(eng->execute("10.5 / 2.0").as<double>(), 5.25, 0.0001);
        });
        
        test("mixed_type_arithmetic", [this]() {
            auto eng = make_engine();
            check_near(eng->execute("10 + 5.5").as<double>(), 15.5, 0.0001);
            check_near(eng->execute("10.5 - 5").as<double>(), 5.5, 0.0001);
        });
        
        test("unary_operators", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("-5").as<int>(), -5);
            check_eq(eng->execute("-(-5)").as<int>(), 5);
            check_eq(eng->execute("!true").as<bool>(), false);
            check_eq(eng->execute("!false").as<bool>(), true);
            check_eq(eng->execute("~5").as<int>(), ~5);
        });
        
        test("increment_decrement", [this]() {
            auto eng = make_engine();
            eng->execute("var x = 5;");
            check_eq(eng->execute("x++").as<int>(), 5);  // Postfix returns old value
            check_eq(eng->execute("x").as<int>(), 6);
            check_eq(eng->execute("++x").as<int>(), 7);  // Prefix returns new value
            check_eq(eng->execute("x--").as<int>(), 7);  // Postfix returns old value
            check_eq(eng->execute("x").as<int>(), 6);
            check_eq(eng->execute("--x").as<int>(), 5);  // Prefix returns new value
        });
        
        test("compound_assignments", [this]() {
            auto eng = make_engine();
            eng->execute("var x = 10;");
            check_eq(eng->execute("x += 5").as<int>(), 15);
            check_eq(eng->execute("x -= 3").as<int>(), 12);
            check_eq(eng->execute("x *= 2").as<int>(), 24);
            check_eq(eng->execute("x /= 4").as<int>(), 6);
        });
        
        test("string_concatenation", [this]() {
            auto eng = make_engine();
            eng->execute("var s = \"Hello\";");
            check_eq(eng->execute("s += \", World!\"").as<std::string>(), "Hello, World!");
        });
        
        test("comparison_operators", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("5 < 10").as<bool>(), true);
            check_eq(eng->execute("5 > 10").as<bool>(), false);
            check_eq(eng->execute("5 <= 5").as<bool>(), true);
            check_eq(eng->execute("5 >= 5").as<bool>(), true);
            check_eq(eng->execute("5 == 5").as<bool>(), true);
            check_eq(eng->execute("5 != 3").as<bool>(), true);
        });
        
        test("spaceship_operator", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("5 <=> 10").as<int>(), -1);
            check_eq(eng->execute("10 <=> 5").as<int>(), 1);
            check_eq(eng->execute("5 <=> 5").as<int>(), 0);
            
            // String comparison
            check_eq(eng->execute("\"apple\" <=> \"banana\"").as<int>(), -1);
            check_eq(eng->execute("\"banana\" <=> \"apple\"").as<int>(), 1);
            check_eq(eng->execute("\"apple\" <=> \"apple\"").as<int>(), 0);
        });
        
        test("logical_operators", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("true && true").as<bool>(), true);
            check_eq(eng->execute("true && false").as<bool>(), false);
            check_eq(eng->execute("false || true").as<bool>(), true);
            check_eq(eng->execute("false || false").as<bool>(), false);
        });
        
        test("logical_short_circuit", [this]() {
            auto eng = make_engine();
            eng->execute("var counter = 0;");
            eng->execute("function inc() -> auto { counter = counter + 1; return true; }");
            
            // && short-circuits on false
            eng->execute("false && inc()");
            check_eq(eng->execute("counter").as<int>(), 0);
            
            // || short-circuits on true
            eng->execute("true || inc()");
            check_eq(eng->execute("counter").as<int>(), 0);
            
            // Should evaluate second operand
            eng->execute("true && inc()");
            check_eq(eng->execute("counter").as<int>(), 1);
        });
        
        test("bitwise_operators", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("5 & 3").as<int>(), 1);
            check_eq(eng->execute("5 | 3").as<int>(), 7);
            check_eq(eng->execute("5 ^ 3").as<int>(), 6);
            check_eq(eng->execute("5 << 2").as<int>(), 20);
            check_eq(eng->execute("20 >> 2").as<int>(), 5);
        });
        
        test("ternary_operator", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("true ? 1 : 2").as<int>(), 1);
            check_eq(eng->execute("false ? 1 : 2").as<int>(), 2);
            check_eq(eng->execute("5 > 3 ? \"yes\" : \"no\"").as<std::string>(), "yes");
        });
        
        test("operator_precedence", [this]() {
            auto eng = make_engine();
            check_eq(eng->execute("2 + 3 * 4").as<int>(), 14);  // Not 20
            check_eq(eng->execute("(2 + 3) * 4").as<int>(), 20);
            check_eq(eng->execute("10 - 2 - 3").as<int>(), 5);  // Left associative
            check_eq(eng->execute("2 * 3 + 4 * 5").as<int>(), 26);
        });
        
        test("division_by_zero", [this]() {
            auto eng = make_engine();
            check_throws([&]() {
                eng->execute("5 / 0");
            }, "Integer division by zero should throw");
            
            check_throws([&]() {
                eng->execute("5.0 / 0.0");
            }, "Float division by zero should throw");
        });
        
        // Regression: identifier+literal binary fast path fed the engine-less AST literal to the
        // operator-method arg path -> uncatchable "Cannot clone script_value: missing engine pointer"
        test("script_class_operator_primitive_rhs", [this]() {
            auto eng = make_engine();
            check_eq(3, eng->execute(
                "class S { int value = 0; S(int v) { value = v; } function operator+(var o) -> S { return S(value + o); } }\n"
                "var a = S(2); var c = a + 1; c.value;").as<int>());
            check_eq(true, eng->execute(
                "class T { int value = 0; T(int v) { value = v; } bool operator<(var o) { return value < o; } }\n"
                "var a = T(1); a < 5;").as<bool>());
            check_eq(true, eng->execute(
                "class U { int value = 0; U(int v) { value = v; } bool operator<(var o) { return value < o; } }\n"
                "var a = U(1); var lim = 5; a < lim;").as<bool>());
            check_eq(std::string("T"), eng->execute(
                "class V { int value = 0; V(int v) { value = v; } bool operator<(var o) { return value < o; } }\n"
                "var a = V(1); var out = \"\"; if (a < 5) { out = \"T\"; } else { out = \"F\"; } out;").as<std::string>());
            // untyped operator return, plain int result (w4 shape)
            check_eq(7, eng->execute(
                "class W { int value = 0; W(int v) { value = v; } function operator+(var o) { return value + o; } }\n"
                "var a = W(3); var r = a + 4; r;").as<int>());
        });

        test("operator_overloading_with_custom_types", [this]() {
            auto eng = make_engine();

            // Define a custom + operator that doubles the result
            bool has_custom_ops = false;
            eng->add_function("+", [&has_custom_ops](int a, int b) -> int {
                has_custom_ops = true;
                return (a + b) * 2;
            });
            // Global numeric operators are opt-in: fast paths stay active until enabled
            eng->set_has_custom_numeric_operators(true);

            // Literal operands constant-fold at parse time, so use variables
            auto result = eng->execute("var a = 5; var b = 3; a + b");
            check_eq(16, result.as<int>());  // (5 + 3) * 2
            check_true(has_custom_ops);
        });

        // FIXED (2026-07, open question #11): typed operator methods accept named-class
        // return types — `Money operator+(Money o)` parses inside class Money (the
        // own-class-name member branch consumes the operator symbol like the general
        // typed path). Parity with the function-keyword spelling.
        test("operator_named_class_return_type", [this]() {
            const char* src = R"(
                class Money {
                    int cents = 0;
                    Money() { cents = 0; }
                    Money(int c) { cents = c; }
                    Money operator+(Money o) { return Money(cents + o.cents); }
                    bool operator<(Money o) { return cents < o.cents; }
                }
                var a = Money(25);
                var b = Money(50);
                var c = a + b;
                var chained = a + b + Money(100);
                to_string(c.cents) + "|" + to_string(chained.cents) + "|" + to_string(a < b);
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                stdlib::register_all(*e);
                check_eq(std::string("75|175|true"), e->execute(src).as<std::string>(),
                         use_vm ? "vm class-return operator" : "interp class-return operator");
            }
        });

        test("operator_class_return_matches_function_spelling", [this]() {
            // The typed spelling and the function-keyword spelling produce the same result
            const char* src = R"(
                class V1 {
                    int v = 0;
                    V1() { v = 0; }
                    V1(int n) { v = n; }
                    V1 operator+(V1 o) { return V1(v + o.v); }
                }
                class V2 {
                    int v = 0;
                    V2() { v = 0; }
                    V2(int n) { v = n; }
                    function operator+(V2 o) { return V2(v + o.v); }
                }
                var s1 = (V1(3) + V1(4)).v;
                var s2 = (V2(3) + V2(4)).v;
                to_string(s1) + "|" + to_string(s2);
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                stdlib::register_all(*e);
                check_eq(std::string("7|7"), e->execute(src).as<std::string>(),
                         use_vm ? "vm spelling parity" : "interp spelling parity");
            }
        });

        test("operator_subscript_returns_class", [this]() {
            const char* src = R"(
                class Cell {
                    int payload = 0;
                    Cell() { payload = 0; }
                    Cell(int p) { payload = p; }
                }
                class Grid {
                    int base = 10;
                    Cell operator[](int i) { return Cell(base + i); }
                }
                var g = Grid();
                g[5].payload;
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                check_eq((int64_t)15, e->execute(src).as_int(),
                         use_vm ? "vm operator[] class return" : "interp operator[] class return");
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::operator_tests)