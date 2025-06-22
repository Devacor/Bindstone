#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
};

JAI_TEST_SUITE(OperatorDebug)

JAI_TEST(simple_vec2_test) {
    Engine engine;
    
    // Register Vec2
    makeClassBuilder<Vec2>(engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    // Try basic instantiation first
    try {
        Value result = engine.execute(R"(
            var v = Vec2(3.0, 4.0);
            v;
        )");
        
        std::cout << "Created Vec2 successfully\n";
        auto v = result.as<std::shared_ptr<Vec2>>();
        std::cout << "Vec2: x=" << v->x << ", y=" << v->y << "\n";
        expect_near(v->x, 3.0f, 0.001f);
        expect_near(v->y, 4.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "Error creating Vec2: " << e.what() << "\n";
        expect_true(false);
    }
    
    // Now try with operator
    engine.addFunction("+", [](const Vec2& a, const Vec2& b) -> Vec2 {
        std::cout << "Operator + called with: (" << a.x << "," << a.y << ") + (" 
                  << b.x << "," << b.y << ")\n";
        return Vec2(a.x + b.x, a.y + b.y);
    });
    
    try {
        Value result = engine.execute(R"(
            var v1 = Vec2(1.0, 2.0);
            var v2 = Vec2(3.0, 4.0);
            var v3 = v1 + v2;
            v3;
        )");
        
        std::cout << "Addition succeeded\n";
        auto v = result.as<std::shared_ptr<Vec2>>();
        std::cout << "Result: x=" << v->x << ", y=" << v->y << "\n";
        expect_near(v->x, 4.0f, 0.001f);
        expect_near(v->y, 6.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "Error with addition: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()