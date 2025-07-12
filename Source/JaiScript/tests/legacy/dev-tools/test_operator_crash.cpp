#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(OperatorCrashDebug)

class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
    
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
    
    Vec2 operator*(float scalar) const {
        return Vec2(x * scalar, y * scalar);
    }
};

JAI_TEST(vec2_operator_return_type) {
    auto engine = engine::make();
    
    // Register Vec2 class
    class_builder<Vec2>(*engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    // Test 1: Basic property access works
    {
        auto result = engine->execute("var v = Vec2(2.0, 3.0); v.x + v.y;");
        expect_eq(result.as<double>(), 5.0);
    }
    
    // Test 2: Register operator that returns Vec2
    engine->add_function("+", [](const Vec2& a, const Vec2& b) -> Vec2 {
        return a + b;
    });
    
    // Test 3: Check if operator returns proper Vec2
    {
        auto result = engine->execute(R"(
            var v1 = Vec2(2.0, 3.0);
            var v2 = Vec2(1.0, 1.0);
            var result = v1 + v2;
            // Return the type name to debug
            type_of(result);
        )");
        
        std::cout << "Type of result: " << result.as<std::string>() << "\n";
    }
    
    // Test 4: Try to access property on operator result
    {
        try {
            auto result = engine->execute(R"(
                var v1 = Vec2(2.0, 3.0);
                var v2 = Vec2(1.0, 1.0);
                var result = v1 + v2;
                result.x;  // This might crash
            )");
            expect_eq(result.as<float>(), 3.0f);
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            expect_true(false); // Should not throw
        }
    }
    
    // Test 5: Multiplication operator
    engine->add_function("*", [](const Vec2& v, float s) -> Vec2 {
        return v * s;
    });
    
    // Test 6: Chained operations
    {
        try {
            auto result = engine->execute(R"(
                var v1 = Vec2(2.0, 3.0);
                var v2 = Vec2(1.0, 1.0);
                var result = (v1 + v2) * 2.0;
                result.x + result.y;
            )");
            // (2,3) + (1,1) = (3,4); (3,4) * 2 = (6,8); 6 + 8 = 14
            expect_eq(result.as<double>(), 14.0);
        } catch (const std::exception& e) {
            std::cerr << "Exception in chained operation: " << e.what() << "\n";
            expect_true(false); // Should not throw
        }
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()