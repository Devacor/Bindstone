#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <cmath>
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

class Vector2 {
public:
    float x, y;
    
    Vector2() : x(0), y(0) {
        std::cout << "Vector2() constructor called\n";
    }
    Vector2(float x_, float y_) : x(x_), y(y_) {
        std::cout << "Vector2(" << x << ", " << y << ") constructor called\n";
    }
    
    float length() const {
        return std::sqrt(x * x + y * y);
    }
};

JAI_TEST_SUITE(Vector2Debug)

JAI_TEST(vector2_operator_test) {
    Engine engine;
    
    std::cout << "=== Registering Vector2 ===\n";
    makeClassBuilder<Vector2>(engine, "Vector2")
        .constructor<>()
        .constructor<float, float>()
        .property("x", &Vector2::x)
        .property("y", &Vector2::y)
        .method("length", &Vector2::length)
        .build();
    
    std::cout << "=== Registering + operator ===\n";
    engine.addFunction("+", [](const Vector2& a, const Vector2& b) -> Vector2 {
        std::cout << "Operator+ called: (" << a.x << "," << a.y << ") + (" << b.x << "," << b.y << ")\n";
        return Vector2(a.x + b.x, a.y + b.y);
    });
    
    try {
        std::cout << "=== Creating Vector2 objects ===\n";
        Value result = engine.execute(R"(
            var v1 = Vector2(3.0, 4.0);
            var v2 = Vector2(1.0, 2.0);
            var v3 = v1 + v2;
            v3;
        )");
        
        std::cout << "=== Extracting result ===\n";
        auto v3 = result.as<std::shared_ptr<Vector2>>();
        std::cout << "Result: Vector2(" << v3->x << ", " << v3->y << ")\n";
        
        expect_near(v3->x, 4.0f, 0.001f);
        expect_near(v3->y, 6.0f, 0.001f);
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()