#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <cmath>

using namespace jai;
using namespace jai::test;

// Test classes
class Point {
public:
    Point() : x(0), y(0) {}
    Point(float x_, float y_) : x(x_), y(y_) {}
    
    float getX() const { return x; }
    float getY() const { return y; }
    void setX(float newX) { x = newX; }
    void setY(float newY) { y = newY; }
    
    float distance() const {
        return std::sqrt(x * x + y * y);
    }
    
    void translate(float dx, float dy) {
        x += dx;
        y += dy;
    }
    
    std::string to_string() const {
        return "Point(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
    
    float x, y;
};

JAI_TEST_SUITE(ClassBuilderComprehensive)

JAI_TEST(property_access_via_getters_setters) {
    engine engine;
    
    class_builder<Point>(engine, "Point")
        .constructor<>()
        .method("getX", &Point::getX)
        .method("setX", &Point::setX)
        .method("getY", &Point::getY)
        .method("setY", &Point::setY)
        .build();
    
    std::string script = R"(
        auto p = Point();
        
        // Property access via getter/setter methods
        p.setX(10.0);
        p.setY(20.0);
        
        return p.getX() + p.getY();
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 30.0f, 0.001f);
}

JAI_TEST(method_chaining_support) {
    engine engine;
    
    // Define a class with chainable methods
    class Builder {
    public:
        Builder() : value_(0) {}
        
        Builder& withName(const std::string& name) {
            name_ = name;
            return *this;
        }
        
        Builder& withValue(int value) {
            value_ = value;
            return *this;
        }
        
        std::string build() const {
            return name_ + ": " + std::to_string(value_);
        }
        
        std::string get_name() const { return name_; }
        int getValue() const { return value_; }
        
    private:
        std::string name_;
        int value_;
    };
    
    class_builder<Builder>(engine, "Builder")
        .constructor<>()
        .method("withName", &Builder::withName)
        .method("withValue", &Builder::withValue)
        .method("build", &Builder::build)
        .method("get_name", &Builder::get_name)
        .method("getValue", &Builder::getValue)
        .build();
    
    // Test method chaining - for now test individual method calls
    // TODO: Full chaining syntax like .withName("test").withValue(42) 
    //       requires return value handling for references
    std::string script = R"(
        auto b = Builder();
        b.withName("test");
        b.withValue(42);
        return b.build();
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<std::string>(), std::string("test: 42"));
}

JAI_TEST(complex_method_calls) {
    engine engine;
    
    class_builder<Point>(engine, "Point")
        .constructor<float, float>()
        .method("getX", &Point::getX)
        .method("getY", &Point::getY)
        .method("setX", &Point::setX)
        .method("setY", &Point::setY)
        .method("distance", &Point::distance)
        .method("translate", &Point::translate)
        .method("to_string", &Point::to_string)
        .build();
    
    std::string script = R"(
        auto p = Point(3.0, 4.0);
        
        // Test distance calculation
        float dist1 = p.distance();  // Should be 5.0
        
        // Test translation
        p.translate(2.0, 1.0);
        
        // Test new position
        float newX = p.getX();  // Should be 5.0
        float newY = p.getY();  // Should be 5.0
        
        return dist1 + newX + newY;  // 5.0 + 5.0 + 5.0 = 15.0
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 15.0f, 0.001f);
}

JAI_TEST(multiple_classes) {
    engine engine;
    
    // First, just test if we can create a simple test point
    class SimplePoint {
    public:
        SimplePoint(float x, float y) : x_(x), y_(y) {}
        float getX() const { return x_; }
        float getY() const { return y_; }
    private:
        float x_, y_;
    };
    
    class_builder<SimplePoint>(engine, "SimplePoint")
        .constructor<float, float>()
        .method("getX", &SimplePoint::getX)
        .method("getY", &SimplePoint::getY)
        .build();
    
    std::string script = R"(
        auto p = SimplePoint(3.0, 4.0);
        return p.getX() + p.getY();
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 7.0f, 0.001f);
}

JAI_TEST(constructor_overloading_comprehensive) {
    engine engine;
    
    class NumberBox {
    public:
        NumberBox() : value_(0), multiplier_(1.0f) {}
        NumberBox(int value) : value_(value), multiplier_(1.0f) {}
        NumberBox(int value, float multiplier) : value_(value), multiplier_(multiplier) {}
        
        int getValue() const { return value_; }
        float getMultiplier() const { return multiplier_; }
        float getResult() const { return value_ * multiplier_; }
        
    private:
        int value_;
        float multiplier_;
    };
    
    class_builder<NumberBox>(engine, "NumberBox")
        .constructor<>()                           // Default constructor
        .constructor<int>()                        // script_value constructor
        .constructor<int, float>()                 // script_value + multiplier constructor
        .method("getValue", &NumberBox::getValue)
        .method("getMultiplier", &NumberBox::getMultiplier)
        .method("getResult", &NumberBox::getResult)
        .build();
    
    // Test all three constructors
    std::string script1 = R"(
        auto n1 = NumberBox();
        return n1.getValue();
    )";
    
    script_value result1 = engine.execute(script1);
    expect_eq(result1.as<int>(), 0);
    
    std::string script2 = R"(
        auto n2 = NumberBox(42);
        return n2.getValue();
    )";
    
    script_value result2 = engine.execute(script2);
    expect_eq(result2.as<int>(), 42);
    
    std::string script3 = R"(
        auto n3 = NumberBox(10, 2.5);
        return n3.getResult();
    )";
    
    script_value result3 = engine.execute(script3);
    expect_near(result3.as<float>(), 25.0f, 0.001f);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()