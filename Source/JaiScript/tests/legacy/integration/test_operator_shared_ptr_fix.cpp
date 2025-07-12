#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace jai::test;

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

JAI_TEST_SUITE(OperatorSharedPtrFix)

JAI_TEST(test_operator_return_types) {
    auto engine = engine::make();
    
    class_builder<Vec2>(*engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    // Test 1: Basic property access (should work)
    {
        auto result = engine->execute("var v = Vec2(2.0, 3.0); v.x;");
        expect_eq(result.as<float>(), 2.0f);
    }
    
    // Test 2: Operator returning Vec2 by value (might crash)
    std::cout << "\nTest 2: Operator returning Vec2 by value\n";
    engine->add_function("+_value", [](const Vec2& a, const Vec2& b) -> Vec2 {
        return a + b;
    });
    
    try {
        auto result = engine->execute(R"(
            var v1 = Vec2(1.0, 2.0);
            var v2 = Vec2(3.0, 4.0);
            var sum = v1 +_value v2;
            sum.x;
        )");
        std::cout << "  By-value SUCCESS: sum.x = " << result.as<float>() << "\n";
    } catch (const std::exception& e) {
        std::cout << "  By-value FAILED: " << e.what() << "\n";
    }
    
    // Test 3: Operator returning shared_ptr<Vec2> (should work)
    std::cout << "\nTest 3: Operator returning shared_ptr<Vec2>\n";
    engine->add_function("+", [](const Vec2& a, const Vec2& b) -> std::shared_ptr<Vec2> {
        return std::make_shared<Vec2>(a + b);
    });
    
    try {
        auto result = engine->execute(R"(
            var v1 = Vec2(1.0, 2.0);
            var v2 = Vec2(3.0, 4.0);
            var sum = v1 + v2;
            sum.x;
        )");
        std::cout << "  Shared_ptr SUCCESS: sum.x = " << result.as<float>() << "\n";
        expect_eq(result.as<float>(), 4.0f);
    } catch (const std::exception& e) {
        std::cout << "  Shared_ptr FAILED: " << e.what() << "\n";
        expect_true(false);
    }
    
    // Test 4: Chained operations with shared_ptr
    std::cout << "\nTest 4: Chained operations\n";
    engine->add_function("*", [](const Vec2& v, float s) -> std::shared_ptr<Vec2> {
        return std::make_shared<Vec2>(v * s);
    });
    
    try {
        auto result = engine->execute(R"(
            var v1 = Vec2(2.0, 3.0);
            var v2 = Vec2(1.0, 1.0);
            var result = (v1 + v2) * 2.0;
            result.x + result.y;
        )");
        std::cout << "  Chained SUCCESS: result = " << result.as<double>() << "\n";
        expect_eq(result.as<double>(), 14.0); // (3,4) * 2 = (6,8), 6+8=14
    } catch (const std::exception& e) {
        std::cout << "  Chained FAILED: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()