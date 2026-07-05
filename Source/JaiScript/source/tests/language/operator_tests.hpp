#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
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
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::operator_tests)