#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(SimpleOp)

JAI_TEST(basic_builtin_operators) {
    Engine engine;
    
    // Test built-in operators work
    Value result = engine.execute("3 + 4;");
    expect_eq(result.as<int>(), 7);
    
    result = engine.execute("10.0 / 2.0;");
    expect_near(result.as<float>(), 5.0f, 0.001f);
}

JAI_TEST(simple_function_overload) {
    Engine engine;
    
    // Register a simple overloaded function
    engine.addFunction("add", [](int a, int b) -> int {
        std::cout << "add(int, int) called\n";
        return a + b;
    });
    
    engine.addFunction("add", [](float a, float b) -> float {
        std::cout << "add(float, float) called\n";
        return a + b;
    });
    
    Value result = engine.execute("add(3, 4);");
    expect_eq(result.as<int>(), 7);
    
    result = engine.execute("add(3.5, 4.5);");
    expect_near(result.as<float>(), 8.0f, 0.001f);
}

JAI_TEST(custom_type_creation) {
    Engine engine;
    
    class Point {
    public:
        float x, y;
        Point(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
    };
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    // First test: just create and access a Point
    Value result = engine.execute(R"(
        var p = Point(3.0, 4.0);
        p.x;
    )");
    expect_near(result.as<float>(), 3.0f, 0.001f);
    
    // Test returning the whole object
    result = engine.execute(R"(
        var p = Point(5.0, 6.0);
        p;
    )");
    auto p = result.as<std::shared_ptr<Point>>();
    expect_near(p->x, 5.0f, 0.001f);
    expect_near(p->y, 6.0f, 0.001f);
}

JAI_TEST(custom_type_function_return) {
    Engine engine;
    
    class Point {
    public:
        float x, y;
        Point(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
    };
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    // Add a function that returns a custom type
    engine.addFunction("makePoint", [](float x, float y) -> Point {
        std::cout << "makePoint(" << x << ", " << y << ") called\n";
        return Point(x, y);
    });
    
    try {
        Value result = engine.execute(R"(
            var p = makePoint(7.0, 8.0);
            p;
        )");
        auto p = result.as<std::shared_ptr<Point>>();
        std::cout << "Got Point: x=" << p->x << ", y=" << p->y << "\n";
        expect_near(p->x, 7.0f, 0.001f);
        expect_near(p->y, 8.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()