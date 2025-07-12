#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

// Simple Vec2 for debugging
class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {
        std::cout << "Vec2 constructor: x=" << x << ", y=" << y << "\n";
    }
};

JAI_TEST_SUITE(OperatorOverloadingDebug)

JAI_TEST(debug_vec2_creation) {
    engine engine;
    
    std::cout << "=== DEBUG: Vec2 Creation Test ===\n";
    
    class_builder<Vec2>(engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    try {
        script_value result = engine.execute("var v = Vec2(3.0, 4.0); v;");
        auto v = result.as<std::shared_ptr<Vec2>>();
        std::cout << "Success: Created Vec2(" << v->x << ", " << v->y << ")\n";
        expect_near(v->x, 3.0f, 0.001f);
        expect_near(v->y, 4.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(debug_operator_registration) {
    engine engine;
    
    std::cout << "\n=== DEBUG: Operator Registration Test ===\n";
    
    class_builder<Vec2>(engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    // Register the + operator
    engine.add_function("+", [](const Vec2& a, const Vec2& b) -> Vec2 {
        std::cout << "DEBUG: operator+ called with (" << a.x << "," << a.y 
                  << ") + (" << b.x << "," << b.y << ")\n";
        Vec2 result(a.x + b.x, a.y + b.y);
        std::cout << "DEBUG: returning Vec2(" << result.x << "," << result.y << ")\n";
        return result;
    });
    
    try {
        // Test individual parts first
        std::cout << "Creating v1...\n";
        script_value v1_result = engine.execute("var v1 = Vec2(1.0, 2.0); v1;");
        auto v1 = v1_result.as<std::shared_ptr<Vec2>>();
        std::cout << "v1 = (" << v1->x << ", " << v1->y << ")\n";
        
        std::cout << "Creating v2...\n";
        script_value v2_result = engine.execute("var v2 = Vec2(3.0, 4.0); v2;");
        auto v2 = v2_result.as<std::shared_ptr<Vec2>>();
        std::cout << "v2 = (" << v2->x << ", " << v2->y << ")\n";
        
        std::cout << "Performing addition (this should call our operator)...\n";
        try {
            script_value result = engine.execute("var v3 = v1 + v2; v3;");
            
            std::cout << "Extracting result...\n";
            auto v3 = result.as<std::shared_ptr<Vec2>>();
            std::cout << "v3 = (" << v3->x << ", " << v3->y << ")\n";
            
            expect_near(v3->x, 4.0f, 0.001f);
            expect_near(v3->y, 6.0f, 0.001f);
        } catch (const std::exception& e) {
            std::cout << "Addition failed with: " << e.what() << "\n";
            
            // Let's debug what types the + operator is receiving
            std::cout << "Let's test the + operator manually...\n";
            std::vector<script_value> args = {v1_result, v2_result};
            
            // Try calling the overloaded function directly
            auto var = engine.get_variable("+");
            if (var.is_function()) {
                std::cout << "Found + function, trying to call it...\n";
                try {
                    auto func = var.as_function();
                    script_value result = func(args);
                    auto v3 = result.as<std::shared_ptr<Vec2>>();
                    std::cout << "Manual call worked: v3 = (" << v3->x << ", " << v3->y << ")\n";
                } catch (const std::exception& e2) {
                    std::cout << "Manual call failed: " << e2.what() << "\n";
                }
            } else {
                std::cout << "+ is not a function\n";
            }
        }
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(debug_type_info) {
    engine engine;
    
    std::cout << "\n=== DEBUG: Type Info Test ===\n";
    
    // Check what type name is registered
    std::cout << "typeid(Vec2).name() = " << typeid(Vec2).name() << "\n";
    
    class_builder<Vec2>(engine, "Vec2")
        .constructor<float, float>()
        .property("x", &Vec2::x)
        .property("y", &Vec2::y)
        .build();
    
    // Test with a function that returns Vec2
    engine.add_function("makeVec2", [](float x, float y) -> Vec2 {
        std::cout << "DEBUG: makeVec2(" << x << ", " << y << ") called\n";
        return Vec2(x, y);
    });
    
    try {
        script_value result = engine.execute("var v = makeVec2(5.0, 6.0); v;");
        auto v = result.as<std::shared_ptr<Vec2>>();
        std::cout << "Success: makeVec2 returned (" << v->x << ", " << v->y << ")\n";
        expect_near(v->x, 5.0f, 0.001f);
        expect_near(v->y, 6.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()