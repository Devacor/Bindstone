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
            auto eng = engine::make();
            check_eq(eng->execute("10 + 5").as<int>(), 15);
            check_eq(eng->execute("10 - 5").as<int>(), 5);
            check_eq(eng->execute("10 * 5").as<int>(), 50);
            check_eq(eng->execute("10 / 5").as<int>(), 2);
            check_eq(eng->execute("10 % 3").as<int>(), 1);
        });
        
        test("float_arithmetic", [this]() {
            auto eng = engine::make();
            check_near(eng->execute("10.5 + 5.5").as<double>(), 16.0, 0.0001);
            check_near(eng->execute("10.5 - 5.5").as<double>(), 5.0, 0.0001);
            check_near(eng->execute("10.5 * 2.0").as<double>(), 21.0, 0.0001);
            check_near(eng->execute("10.5 / 2.0").as<double>(), 5.25, 0.0001);
        });
        
        test("mixed_type_arithmetic", [this]() {
            auto eng = engine::make();
            check_near(eng->execute("10 + 5.5").as<double>(), 15.5, 0.0001);
            check_near(eng->execute("10.5 - 5").as<double>(), 5.5, 0.0001);
        });
        
        test("unary_operators", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("-5").as<int>(), -5);
            check_eq(eng->execute("-(-5)").as<int>(), 5);
            check_eq(eng->execute("!true").as<bool>(), false);
            check_eq(eng->execute("!false").as<bool>(), true);
            check_eq(eng->execute("~5").as<int>(), ~5);
        });
        
        test("increment_decrement", [this]() {
            auto eng = engine::make();
            eng->execute("var x = 5;");
            check_eq(eng->execute("x++").as<int>(), 5);  // Postfix returns old value
            check_eq(eng->execute("x").as<int>(), 6);
            check_eq(eng->execute("++x").as<int>(), 7);  // Prefix returns new value
            check_eq(eng->execute("x--").as<int>(), 7);  // Postfix returns old value
            check_eq(eng->execute("x").as<int>(), 6);
            check_eq(eng->execute("--x").as<int>(), 5);  // Prefix returns new value
        });
        
        test("compound_assignments", [this]() {
            auto eng = engine::make();
            eng->execute("var x = 10;");
            check_eq(eng->execute("x += 5").as<int>(), 15);
            check_eq(eng->execute("x -= 3").as<int>(), 12);
            check_eq(eng->execute("x *= 2").as<int>(), 24);
            check_eq(eng->execute("x /= 4").as<int>(), 6);
        });
        
        test("string_concatenation", [this]() {
            auto eng = engine::make();
            eng->execute("var s = \"Hello\";");
            check_eq(eng->execute("s += \", World!\"").as<std::string>(), "Hello, World!");
        });
        
        test("comparison_operators", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("5 < 10").as<bool>(), true);
            check_eq(eng->execute("5 > 10").as<bool>(), false);
            check_eq(eng->execute("5 <= 5").as<bool>(), true);
            check_eq(eng->execute("5 >= 5").as<bool>(), true);
            check_eq(eng->execute("5 == 5").as<bool>(), true);
            check_eq(eng->execute("5 != 3").as<bool>(), true);
        });
        
        test("spaceship_operator", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("5 <=> 10").as<int>(), -1);
            check_eq(eng->execute("10 <=> 5").as<int>(), 1);
            check_eq(eng->execute("5 <=> 5").as<int>(), 0);
            
            // String comparison
            check_eq(eng->execute("\"apple\" <=> \"banana\"").as<int>(), -1);
            check_eq(eng->execute("\"banana\" <=> \"apple\"").as<int>(), 1);
            check_eq(eng->execute("\"apple\" <=> \"apple\"").as<int>(), 0);
        });
        
        test("logical_operators", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("true && true").as<bool>(), true);
            check_eq(eng->execute("true && false").as<bool>(), false);
            check_eq(eng->execute("false || true").as<bool>(), true);
            check_eq(eng->execute("false || false").as<bool>(), false);
        });
        
        test("logical_short_circuit", [this]() {
            auto eng = engine::make();
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
            auto eng = engine::make();
            check_eq(eng->execute("5 & 3").as<int>(), 1);
            check_eq(eng->execute("5 | 3").as<int>(), 7);
            check_eq(eng->execute("5 ^ 3").as<int>(), 6);
            check_eq(eng->execute("5 << 2").as<int>(), 20);
            check_eq(eng->execute("20 >> 2").as<int>(), 5);
        });
        
        test("ternary_operator", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("true ? 1 : 2").as<int>(), 1);
            check_eq(eng->execute("false ? 1 : 2").as<int>(), 2);
            check_eq(eng->execute("5 > 3 ? \"yes\" : \"no\"").as<std::string>(), "yes");
        });
        
        test("operator_precedence", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("2 + 3 * 4").as<int>(), 14);  // Not 20
            check_eq(eng->execute("(2 + 3) * 4").as<int>(), 20);
            check_eq(eng->execute("10 - 2 - 3").as<int>(), 5);  // Left associative
            check_eq(eng->execute("2 * 3 + 4 * 5").as<int>(), 26);
        });
        
        test("division_by_zero", [this]() {
            auto eng = engine::make();
            check_throws([&]() {
                eng->execute("5 / 0");
            }, "Integer division by zero should throw");
            
            check_throws([&]() {
                eng->execute("5.0 / 0.0");
            }, "Float division by zero should throw");
        });
        
        test("operator_overloading_with_custom_types", [this]() {
            auto eng = engine::make();
            
            // For now, test operator overloading with built-in types
            // Custom operators can override built-in behavior
            eng->execute("var has_custom_ops = false;");
            
            // Define a custom + operator that doubles the result
            eng->add_function("+", [&eng](int a, int b) -> int {
                eng->execute("has_custom_ops = true");
                return (a + b) * 2;
            });
            
            // This should use the custom operator
            auto result = eng->execute("5 + 3");
            check_eq(result.as<int>(), 16);  // (5 + 3) * 2
            check_eq(eng->execute("has_custom_ops").as<bool>(), true);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::operator_tests)