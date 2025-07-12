#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace jai::test;

// Simple test class
class Point {
public:
    Point() : x(0), y(0) {}
    Point(float x_, float y_) : x(x_), y(y_) {}
    
    float x, y;
};

JAI_TEST_SUITE(ClassBuilderMinimal)

JAI_TEST(basic_object_creation) {
    engine engine;
    
    // Register Point class with just a constructor
    class_builder<Point>(engine, "Point")
        .constructor<>()
        .build();
    
    // Test creating an instance from script
    std::string script = R"(
        auto p = Point();
        return p;
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_object());
}

JAI_TEST(constructor_with_arguments) {
    engine engine;
    
    // Register Point class with parameterized constructor  
    class_builder<Point>(engine, "Point")
        .constructor<float, float>()
        .build();
    
    // Test creating an instance with arguments
    std::string script = R"(
        auto p = Point(3.0, 4.0);
        return p;
    )";
    
    script_value result = engine.execute(script);
    expect_true(result.is_object());
}

JAI_TEST(member_access_basic) {
    engine engine;
    
    // Add a simple method to Point
    class TestPoint {
    public:
        TestPoint() : x(0), y(0) {}
        TestPoint(float x_, float y_) : x(x_), y(y_) {}
        
        float getX() const { return x; }
        void setX(float newX) { x = newX; }
        
        float x, y;
    };
    
    // Register Point class with methods
    class_builder<TestPoint>(engine, "TestPoint")
        .constructor<>()
        .method("getX", &TestPoint::getX)
        .method("setX", &TestPoint::setX)
        .build();
    
    // Test member access - get method
    std::string script1 = R"(
        auto p = TestPoint();
        return p.getX;  // Get the method, don't call it yet
    )";
    
    script_value method = engine.execute(script1);
    expect_true(method.is_function());
}

JAI_TEST(method_calls) {
    engine engine;
    
    // Add a simple method to Point
    class TestPoint {
    public:
        TestPoint() : x(0), y(0) {}
        TestPoint(float x_, float y_) : x(x_), y(y_) {}
        
        float getX() const { return x; }
        void setX(float newX) { x = newX; }
        
        float x, y;
    };
    
    // Register Point class with methods
    class_builder<TestPoint>(engine, "TestPoint")
        .constructor<>()
        .method("getX", &TestPoint::getX)
        .method("setX", &TestPoint::setX)
        .build();
    
    // Test method calls
    std::string script = R"(
        auto p = TestPoint();
        p.setX(42.0);
        return p.getX();
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 42.0f, 0.001f);
}

JAI_TEST(constructor_overloading) {
    engine engine;
    
    class Rectangle {
    public:
        Rectangle() : width_(0), height_(0) {}
        Rectangle(float size) : width_(size), height_(size) {}
        Rectangle(float w, float h) : width_(w), height_(h) {}
        
        float area() const { return width_ * height_; }
        float getWidth() const { return width_; }
        float getHeight() const { return height_; }
        
    private:
        float width_, height_;
    };
    
    // Register Rectangle class with multiple constructors
    class_builder<Rectangle>(engine, "Rectangle")
        .constructor<>()                    // Default constructor
        .constructor<float>()               // Square constructor  
        .constructor<float, float>()        // Full constructor
        .method("area", &Rectangle::area)
        .method("getWidth", &Rectangle::getWidth)
        .method("getHeight", &Rectangle::getHeight)
        .build();
    
    // Test different constructors
    std::string script1 = R"(
        auto r1 = Rectangle();           // 0x0
        return r1.area();
    )";
    
    script_value result1 = engine.execute(script1);
    expect_near(result1.as<float>(), 0.0f, 0.001f);
    
    std::string script2 = R"(
        auto r2 = Rectangle(5.0);        // 5x5 square
        return r2.area();
    )";
    
    script_value result2 = engine.execute(script2);
    expect_near(result2.as<float>(), 25.0f, 0.001f);
    
    std::string script3 = R"(
        auto r3 = Rectangle(3.0, 4.0);   // 3x4 rectangle
        return r3.area();
    )";
    
    script_value result3 = engine.execute(script3);
    expect_near(result3.as<float>(), 12.0f, 0.001f);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()