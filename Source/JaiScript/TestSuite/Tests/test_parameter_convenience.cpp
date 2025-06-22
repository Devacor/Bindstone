#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

class Circle {
public:
    float radius;
    Circle(float r = 1.0f) : radius(r) {}
    float area() const { return 3.14159f * radius * radius; }
};

JAI_TEST_SUITE(ParameterConvenience)

JAI_TEST(function_parameter_styles) {
    Engine engine;
    
    makeClassBuilder<Circle>(engine, "Circle")
        .constructor<float>()
        .property("radius", &Circle::radius)
        .method("area", &Circle::area)
        .build();
    
    // Style 1: By-value parameter (convenient, NO copy with our fix)
    engine.addFunction("scaleByValue", [](const Circle& c, float factor) -> Circle {
        std::cout << "scaleByValue called with radius=" << c.radius << ", factor=" << factor << " (const ref - no copy)\n";
        return Circle(c.radius * factor);
    });
    
    // Style 2: By shared_ptr parameter (explicit, no copy)
    engine.addFunction("scaleByPtr", [](std::shared_ptr<Circle> c, float factor) -> Circle {
        std::cout << "scaleByPtr called with radius=" << c->radius << ", factor=" << factor << " (shared_ptr - no copy)\n";
        return Circle(c->radius * factor);
    });
    
    // Test both styles work
    Value result1 = engine.execute(R"(
        var c = Circle(5.0);
        scaleByValue(c, 2.0);
    )");
    
    Circle scaled1 = result1.as<Circle>();
    std::cout << "Result 1: radius=" << scaled1.radius << "\n";
    expect_near(scaled1.radius, 10.0f, 0.001f);
    
    Value result2 = engine.execute(R"(
        var c = Circle(3.0);
        scaleByPtr(c, 3.0);
    )");
    
    Circle scaled2 = result2.as<Circle>();
    std::cout << "Result 2: radius=" << scaled2.radius << "\n";
    expect_near(scaled2.radius, 9.0f, 0.001f);
}

JAI_TEST(mixed_parameter_styles) {
    Engine engine;
    
    makeClassBuilder<Circle>(engine, "Circle")
        .constructor<float>()
        .property("radius", &Circle::radius)
        .build();
    
    // Function mixing both styles
    engine.addFunction("combine", [](const Circle& c1, std::shared_ptr<Circle> c2) -> float {
        std::cout << "combine called: r1=" << c1.radius << ", r2=" << c2->radius << "\n";
        return c1.radius + c2->radius;
    });
    
    // Test shared_ptr reference parameters
    engine.addFunction("distance", [](std::shared_ptr<Circle>& c1, std::shared_ptr<Circle>& c2) -> float {
        std::cout << "distance called with c1=" << (c1 ? std::to_string(c1->radius) : "null") 
                  << ", c2=" << (c2 ? std::to_string(c2->radius) : "null") << "\n";
        if (!c1 || !c2) return 0.0f;
        float diff = c1->radius - c2->radius;
        return diff < 0 ? -diff : diff; // abs
    });
    
    Value result = engine.execute(R"(
        var c1 = Circle(2.0);
        var c2 = Circle(3.0);
        combine(c1, c2);
    )");
    
    float sum = result.as<float>();
    std::cout << "Combined radius: " << sum << "\n";
    expect_near(sum, 5.0f, 0.001f);
    
    // Test non-const reference (Circle&)
    engine.addFunction("modifyRadius", [](Circle& c, float newRadius) -> void {
        std::cout << "modifyRadius called: old=" << c.radius << ", new=" << newRadius << " (Circle& - no copy)\n";
        c.radius = newRadius;
    });
    
    // Test shared_ptr reference (std::shared_ptr<Circle>&)
    engine.addFunction("swapCircles", [](std::shared_ptr<Circle>& c1, std::shared_ptr<Circle>& c2) -> void {
        std::cout << "swapCircles called: c1=" << (c1 ? std::to_string(c1->radius) : "null") 
                  << ", c2=" << (c2 ? std::to_string(c2->radius) : "null") << " (shared_ptr& - no copy)\n";
        std::swap(c1, c2);
    });
    
    // Test the reference parameter function
    Value distResult = engine.execute(R"(
        var c1 = Circle(5.0);
        var c2 = Circle(2.0);
        distance(c1, c2);
    )");
    
    float dist = distResult.as<float>();
    std::cout << "Distance: " << dist << "\n";
    expect_near(dist, 3.0f, 0.001f);
    
    // Test Circle& modification
    engine.execute(R"(
        var c = Circle(10.0);
        modifyRadius(c, 20.0);
    )");
    
    // Test std::shared_ptr<Circle>& swap
    engine.execute(R"(
        var c1 = Circle(1.0);
        var c2 = Circle(2.0);
        swapCircles(c1, c2);
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()