#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

class Point {
public:
    float x, y;
    Point(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
};

JAI_TEST_SUITE(ExtractionConvenience)

JAI_TEST(both_extraction_methods_work) {
    engine engine;
    
    class_builder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    script_value result = engine.execute("Point(3.5, 4.5);");
    
    // Method 1: Extract by value (convenience)
    Point pointByValue = result.as<Point>();
    std::cout << "By value: Point(" << pointByValue.x << ", " << pointByValue.y << ")\n";
    expect_near(pointByValue.x, 3.5f, 0.001f);
    expect_near(pointByValue.y, 4.5f, 0.001f);
    
    // Method 2: Extract as shared_ptr (explicit)
    auto pointPtr = result.as<std::shared_ptr<Point>>();
    std::cout << "By shared_ptr: Point(" << pointPtr->x << ", " << pointPtr->y << ")\n";
    expect_near(pointPtr->x, 3.5f, 0.001f);
    expect_near(pointPtr->y, 4.5f, 0.001f);
    
    // Both should give the same values
    expect_eq(pointByValue.x, pointPtr->x);
    expect_eq(pointByValue.y, pointPtr->y);
}

JAI_TEST(convenient_operator_usage) {
    engine engine;
    
    class_builder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    // Operator can use by-value parameters for convenience
    engine.add_function("+", [](const Point& a, const Point& b) -> Point {
        return Point(a.x + b.x, a.y + b.y);
    });
    
    script_value result = engine.execute(R"(
        var p1 = Point(1.0, 2.0);
        var p2 = Point(3.0, 4.0);
        p1 + p2;
    )");
    
    // Can extract result conveniently by value
    Point sum = result.as<Point>();
    std::cout << "Sum by value: Point(" << sum.x << ", " << sum.y << ")\n";
    expect_near(sum.x, 4.0f, 0.001f);
    expect_near(sum.y, 6.0f, 0.001f);
    
    // Or by shared_ptr if needed
    auto sumPtr = result.as<std::shared_ptr<Point>>();
    std::cout << "Sum by shared_ptr: Point(" << sumPtr->x << ", " << sumPtr->y << ")\n";
    expect_near(sumPtr->x, 4.0f, 0.001f);
    expect_near(sumPtr->y, 6.0f, 0.001f);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()