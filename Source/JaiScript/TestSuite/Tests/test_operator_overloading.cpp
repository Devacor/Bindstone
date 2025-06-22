#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <cmath>
#include <memory>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(OperatorOverloading)

// Custom Vector2 class for testing operator overloading
class Vector2 {
public:
    float x, y;
    
    Vector2() : x(0), y(0) {}
    Vector2(float x_, float y_) : x(x_), y(y_) {}
    
    float length() const {
        return std::sqrt(x * x + y * y);
    }
    
    float dot(const Vector2& other) const {
        return x * other.x + y * other.y;
    }
};

JAI_TEST(basic_operator_overloading_for_custom_type) {
    Engine engine;
    
    // Register Vector2 type
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<>()
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .method("length", &Vector2::length)
        .method("dot", &Vector2::dot)
        .build();
    
    // Overload + operator for Vector2 - now returns naturally!
    engine.addFunction("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2(a.x + b.x, a.y + b.y);
    });
    
    // Overload - operator for Vector2
    engine.addFunction("-", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2(a.x - b.x, a.y - b.y);
    });
    
    // Overload * operator for scalar multiplication
    engine.addFunction("*", [](const Vector2& v, float scalar) -> Vector2 {
        return Vector2(v.x * scalar, v.y * scalar);
    });
    
    engine.addFunction("*", [](float scalar, const Vector2& v) -> Vector2 {
        return Vector2(v.x * scalar, v.y * scalar);
    });
    
    // Test vector addition
    Value result = engine.execute(R"(
        var v1 = Vector2(3.0, 4.0);
        var v2 = Vector2(1.0, 2.0);
        var v3 = v1 + v2;
        v3;
    )");
    
    auto v3 = result.as<std::shared_ptr<Vector2>>();
    expect_near(v3->x, 4.0f, 0.001f);
    expect_near(v3->y, 6.0f, 0.001f);
    
    // Test vector subtraction
    result = engine.execute(R"(
        var v4 = v1 - v2;
        v4;
    )");
    
    auto v4 = result.as<std::shared_ptr<Vector2>>();
    expect_near(v4->x, 2.0f, 0.001f);
    expect_near(v4->y, 2.0f, 0.001f);
    
    // Test scalar multiplication
    result = engine.execute(R"(
        var v5 = v1 * 2.0;
        v5;
    )");
    
    auto v5 = result.as<std::shared_ptr<Vector2>>();
    expect_near(v5->x, 6.0f, 0.001f);
    expect_near(v5->y, 8.0f, 0.001f);
}

JAI_TEST(comparison_operator_overloading) {
    Engine engine;
    
    // Custom comparison for Vector2 based on length
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .method("length", &Vector2::length)
        .build();
    
    // Overload comparison operators
    engine.addFunction("==", [](const Vector2& a, const Vector2& b) -> bool {
        return std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f;
    });
    
    engine.addFunction("!=", [](const Vector2& a, const Vector2& b) -> bool {
        return !(std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f);
    });
    
    engine.addFunction("<", [](const Vector2& a, const Vector2& b) -> bool {
        return a.length() < b.length();
    });
    
    engine.addFunction(">", [](const Vector2& a, const Vector2& b) -> bool {
        return a.length() > b.length();
    });
    
    // Test comparisons
    Value result = engine.execute(R"(
        var v1 = Vector2(3.0, 4.0);  // length = 5
        var v2 = Vector2(0.0, 3.0);  // length = 3
        var v3 = Vector2(3.0, 4.0);  // same as v1
        
        var eq = v1 == v3;
        var neq = v1 != v2;
        var lt = v2 < v1;
        var gt = v1 > v2;
        
        eq && neq && lt && gt;
    )");
    
    expect_eq(result.as<bool>(), true);
}

JAI_TEST(operator_overloading_with_multiple_types) {
    Engine engine;
    
    // Define custom types
    class Money {
    public:
        float amount;
        Money(float a = 0) : amount(a) {}
    };
    
    class Percentage {
    public:
        float value;
        Percentage(float v = 0) : value(v) {}
    };
    
    // Register types
    makeClassBuilder<Money>(engine, "Money")
        .constructor<float>()
        .property("amount", &Money::amount)
        .build();
    
    makeClassBuilder<Percentage>(engine, "Percentage")
        .constructor<float>()
        .property("value", &Percentage::value)
        .build();
    
    // Overload operators for Money
    engine.addFunction("+", [](const Money& a, const Money& b) -> Money {
        return Money(a.amount + b.amount);
    });
    
    engine.addFunction("-", [](const Money& a, const Money& b) -> Money {
        return Money(a.amount - b.amount);
    });
    
    // Money * Percentage = Money (apply percentage)
    engine.addFunction("*", [](const Money& m, const Percentage& p) -> Money {
        return Money(m.amount * (p.value / 100.0f));
    });
    
    // Money + Money from percentage calculation
    engine.addFunction("+", [](const Money& m, float amount) -> Money {
        return Money(m.amount + amount);
    });
    
    Value result = engine.execute(R"(
        var price = Money(100.0);
        var tax = Percentage(8.0);
        var taxAmount = price * tax;
        var total = price + taxAmount;
        total;
    )");
    
    auto total = result.as<std::shared_ptr<Money>>();
    expect_near(total->amount, 108.0f, 0.001f);
}

JAI_TEST(operator_precedence_with_overloading) {
    Engine engine;
    
    // String operators with custom behavior
    engine.addFunction("+", [](const std::string& a, const std::string& b) -> std::string {
        return a + b;
    });
    
    // String * int = repeat string
    engine.addFunction("*", [](const std::string& s, int n) -> std::string {
        std::string result;
        for (int i = 0; i < n; i++) {
            result += s;
        }
        return result;
    });
    
    // Test precedence is maintained
    Value result = engine.execute(R"(
        "a" + "b" * 3;
    )");
    
    expect_eq(result.as<std::string>(), std::string("abbb"));
    
    result = engine.execute(R"(
        ("a" + "b") * 3;
    )");
    
    expect_eq(result.as<std::string>(), std::string("ababab"));
}

JAI_TEST(chained_operator_overloading) {
    Engine engine;
    
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .build();
    
    // Chain-friendly operators
    engine.addFunction("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2(a.x + b.x, a.y + b.y);
    });
    
    engine.addFunction("*", [](const Vector2& v, float s) -> Vector2 {
        return Vector2(v.x * s, v.y * s);
    });
    
    Value result = engine.execute(R"(
        var v1 = Vector2(1.0, 0.0);
        var v2 = Vector2(0.0, 1.0);
        var v3 = Vector2(1.0, 1.0);
        var result = v1 + v2 + v3 * 2.0;
        result;
    )");
    
    auto v = result.as<std::shared_ptr<Vector2>>();
    expect_near(v->x, 3.0f, 0.001f);
    expect_near(v->y, 3.0f, 0.001f);
}

JAI_TEST(operator_overloading_error_handling) {
    Engine engine;
    
    // Division operator with error checking
    engine.addFunction("/", [](float a, float b) -> float {
        if (std::abs(b) < 0.0001f) {
            throw RuntimeError("Division by zero!");
        }
        return a / b;
    });
    
    // Should work normally
    Value result = engine.execute("10.0 / 2.0;");
    expect_near(result.as<float>(), 5.0f, 0.001f);
    
    // Should throw error
    auto testFunc = [&]() { engine.execute("10.0 / 0.0;"); };
    expect_throws<decltype(testFunc), RuntimeError>(std::move(testFunc));
}

JAI_TEST(compound_assignment_with_overloading) {
    Engine engine;
    
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .build();
    
    // Regular operators
    engine.addFunction("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2(a.x + b.x, a.y + b.y);
    });
    
    engine.addFunction("*", [](const Vector2& v, float s) -> Vector2 {
        return Vector2(v.x * s, v.y * s);
    });
    
    // Test compound assignment uses the overloaded operators
    Value result = engine.execute(R"(
        var v1 = Vector2(1.0, 2.0);
        var v2 = Vector2(3.0, 4.0);
        v1 += v2;  // Should use overloaded + operator
        v1;
    )");
    
    auto v = result.as<std::shared_ptr<Vector2>>();
    expect_near(v->x, 4.0f, 0.001f);
    expect_near(v->y, 6.0f, 0.001f);
    
    result = engine.execute(R"(
        v1 *= 2.0;  // Should use overloaded * operator
        v1;
    )");
    
    v = result.as<std::shared_ptr<Vector2>>();
    expect_near(v->x, 8.0f, 0.001f);
    expect_near(v->y, 12.0f, 0.001f);
}

JAI_TEST(mixed_type_arithmetic_overloading) {
    Engine engine;
    
    // Complex number class
    class Complex {
    public:
        float real, imag;
        Complex(float r = 0, float i = 0) : real(r), imag(i) {}
    };
    
    makeClassBuilder<Complex>(engine, "Complex")
        .constructor<float, float>()
        .property("real", &Complex::real)
        .property("imag", &Complex::imag)
        .build();
    
    // Complex + Complex
    engine.addFunction("+", [](const Complex& a, const Complex& b) -> Complex {
        return Complex(a.real + b.real, a.imag + b.imag);
    });
    
    // Complex + float (real number)
    engine.addFunction("+", [](const Complex& c, float r) -> Complex {
        return Complex(c.real + r, c.imag);
    });
    
    // float + Complex
    engine.addFunction("+", [](float r, const Complex& c) -> Complex {
        return Complex(c.real + r, c.imag);
    });
    
    // Complex * Complex
    engine.addFunction("*", [](const Complex& a, const Complex& b) -> Complex {
        return Complex(
            a.real * b.real - a.imag * b.imag,
            a.real * b.imag + a.imag * b.real
        );
    });
    
    Value result = engine.execute(R"(
        var c1 = Complex(3.0, 4.0);
        var c2 = Complex(1.0, 2.0);
        var c3 = c1 + c2;
        var c4 = c1 + 5.0;
        var c5 = 10.0 + c2;
        var c6 = c1 * c2;  // (3+4i)(1+2i) = 3+6i+4i+8i² = 3+10i-8 = -5+10i
        c6;
    )");
    
    auto c = result.as<std::shared_ptr<Complex>>();
    expect_near(c->real, -5.0f, 0.001f);
    expect_near(c->imag, 10.0f, 0.001f);
}

// Performance benchmark for operator overloading
JAI_BENCHMARK(operator_overloading_performance) {
    Engine engine;
    
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .build();
    
    engine.addFunction("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        return Vector2(a.x + b.x, a.y + b.y);
    });
    
    engine.execute(R"(
        var v1 = Vector2(1.0, 2.0);
        var v2 = Vector2(3.0, 4.0);
        var result = Vector2(0.0, 0.0);
        
        for (int i = 0; i < 1000; ++i) {
            result = result + v1 + v2;
        }
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()