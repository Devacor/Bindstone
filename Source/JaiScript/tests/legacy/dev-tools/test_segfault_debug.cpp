#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <chrono>
#include <cmath>

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

JAI_TEST_SUITE(SegfaultDebug)

JAI_TEST(simple_operator_overload) {
    std::cout << "Testing simple operator overload...\n";
    
    jai::engine engine;
    
    make_class_builder<Vec2>(engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
        
    engine.add_function("+", [](const Vec2& a, const Vec2& b) -> Vec2 {
        std::cout << "Adding vectors: (" << a.x << "," << a.y << ") + (" << b.x << "," << b.y << ")\n";
        return a + b;
    });
    
    std::cout << "About to execute script...\n";
    
    try {
        script_value result = engine.execute(R"(
            var v1 = Vec2(2.0, 3.0);
            var v2 = Vec2(1.0, 1.0);
            v1 + v2;
        )");
        
        std::cout << "Script executed successfully\n";
        
        // Try to extract result as Vec2
        Vec2 vec_result = result.as<Vec2>();
        std::cout << "Result: (" << vec_result.x << ", " << vec_result.y << ")\n";
        
    } catch (const std::exception& e) {
        std::cout << "exception caught: " << e.what() << "\n";
        expect_true(false);
        return;
    }
    
    std::cout << "Test completed successfully\n";
}

JAI_TEST(array_test) {
    std::cout << "Testing array operations...\n";
    
    try {
        jai::engine engine;
        
        // Test new array syntax with []
        script_value result = engine.execute(R"(
            var arr = [1, 2, 3, 4, 5];
            arr;
        )");
        
        std::cout << "Array creation successful!\n";
        expect_true(result.is_array());
        
    } catch (const std::exception& e) {
        std::cout << "Array test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(map_test) {
    std::cout << "Testing map operations...\n";
    
    try {
        jai::engine engine;
        
        // Test C++ style map initialization
        script_value result = engine.execute(R"(
            var scores = {{"Alice", 100}, {"Bob", 85}};
            scores;
        )");
        
        std::cout << "Map creation successful!\n";
        expect_true(result.is_map());
        
    } catch (const std::exception& e) {
        std::cout << "Map test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(fibonacci_simple) {
    std::cout << "Testing simple fibonacci...\n";
    
    try {
        jai::engine engine;
        
        script_value result = engine.execute(R"(
            auto fib(auto n) -> auto {
                if (n <= 1) return n;
                return fib(n-1) + fib(n-2);
            }
            fib(5);
        )");
        
        std::cout << "Fibonacci result: " << result.as<int>() << "\n";
        expect_eq(result.as<int>(), 5);
        
    } catch (const std::exception& e) {
        std::cout << "Fibonacci test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()