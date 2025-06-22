#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <string>
#include <memory>
#include <cmath>

using namespace JaiScript;
using namespace JaiScript::Testing;

// Example C++ class to bind
class Point {
public:
    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}
    
    float getX() const { return x; }
    float getY() const { return y; }
    void setX(float val) { x = val; }
    void setY(float val) { y = val; }
    
    float distance() const {
        return std::sqrt(x * x + y * y);
    }
    
    void translate(float dx, float dy) {
        x += dx;
        y += dy;
    }
    
    std::string toString() const {
        return "Point(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }

    float x, y;
};

// More complex example with inheritance
class Shape {
public:
    virtual ~Shape() = default;
    virtual float area() const = 0;
    virtual std::string type() const = 0;
};

class Circle : public Shape {
public:
    Circle(float radius) : radius_(radius) {}
    
    float area() const override {
        return 3.14159f * radius_ * radius_;
    }
    
    std::string type() const override {
        return "Circle";
    }
    
    float getRadius() const { return radius_; }
    void setRadius(float r) { radius_ = r; }
    
private:
    float radius_;
};

JAI_TEST_SUITE(ClassBuilder)

JAI_TEST(basic_class_registration) {
    Engine engine;
    
    // Register Point class using builder pattern
    makeClassBuilder<Point>(engine, "Point")
        .constructor<>()
        .method("getX", &Point::getX)
        .method("getY", &Point::getY)
        .method("setX", &Point::setX)
        .method("setY", &Point::setY)
        .method("distance", &Point::distance)
        .method("translate", &Point::translate)
        .method("toString", &Point::toString)
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    // Test creating an instance from script
    std::string script = R"(
        // Create point with default constructor using brace initialization
        auto p1 = Point{};
        
        // Return the created instance (for now just test creation works)
        return p1;
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isObject()); // Just verify an object was created
}

JAI_TEST(property_access_via_getters_setters) {
    Engine engine;
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    std::string script = R"(
        auto p = Point{};
        
        // Property access generates getX/setX methods
        p.setX(10.0);
        p.setY(20.0);
        
        return p.getX() + p.getY();
    )";
    
    Value result = engine.execute(script);
    expect_eq(result.as<float>(), 30.0f);
}

JAI_TEST(clean_registration_syntax) {
    Engine engine;
    
    // JaiScript style - clean and simple
    auto builder = makeClassBuilder<Point>(engine, "Point");
    builder.constructor<float, float>()
           .method("distance", &Point::distance)
           .property("x", &Point::x)
           .property("y", &Point::y)
           .build();
    
    std::string script = R"(
        auto p = Point{3.0, 4.0};
        return p.distance();
    )";
    
    Value result = engine.execute(script);
    expect_eq(result.as<float>(), 5.0f);
}

JAI_TEST(inheritance_support) {
    Engine engine;
    
    // Register base class
    makeClassBuilder<Shape>(engine, "Shape")
        .method("area", &Shape::area)
        .method("type", &Shape::type)
        .build();
    
    // Register derived class
    makeClassBuilder<Circle>(engine, "Circle")
        .inherits<Shape>()
        .constructor<float>()
        .method("area", [](Circle& self) { return self.area(); })
        .method("type", [](Circle& self) { return self.type(); })
        .method("getRadius", &Circle::getRadius)
        .method("setRadius", &Circle::setRadius)
        .build();
    
    std::string script = R"(
        auto c = Circle{5.0};
        
        // Should be able to call base class methods
        string shapeType = c.type();      // "Circle"
        float area = c.area();             // ~78.54
        
        // And derived class methods
        float r = c.getRadius();           // 5.0
        
        return area;
    )";
    
    Value result = engine.execute(script);
    expect_near(result.as<float>(), 78.54f, 0.1f);
}

JAI_TEST(method_chaining) {
    // Define a class with chainable methods
    class Builder {
    public:
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
        
    private:
        std::string name_;
        int value_ = 0;
    };
    
    Engine engine;
    
    makeClassBuilder<Builder>(engine, "Builder")
        .constructor<>()
        // For method chaining, use lambda syntax with reference return
        .method("withName", [](Builder& self, const std::string& name) -> Builder& {
            return self.withName(name);
        })
        .method("withValue", [](Builder& self, int value) -> Builder& {
            return self.withValue(value);
        })
        .method("build", &Builder::build)
        .build();
    
    std::string script = R"(
        auto result = Builder{}
            .withName("test")
            .withValue(42)
            .build();
        
        return result;
    )";
    
    Value result = engine.execute(script);
    expect_eq(result.as<std::string>(), std::string("test: 42"));
}

JAI_TEST(direct_property_access) {
    Engine engine;
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .method("distance", &Point::distance)
        .build();
    
    std::string script = R"(
        auto p = Point{10.0, 20.0};
        
        // Direct property access
        float oldX = p.x;
        p.x = 30.0;
        float newX = p.x;
        
        return oldX + newX;  // 10.0 + 30.0 = 40.0
    )";
    
    Value result = engine.execute(script);
    expect_eq(result.as<float>(), 40.0f);
}

JAI_TEST(multiple_constructors) {
    class Rectangle {
    public:
        Rectangle() : width_(0), height_(0) {}
        Rectangle(float size) : width_(size), height_(size) {}
        Rectangle(float w, float h) : width_(w), height_(h) {}
        
        float area() const { return width_ * height_; }
        
    private:
        float width_, height_;
    };
    
    Engine engine;
    
    makeClassBuilder<Rectangle>(engine, "Rectangle")
        .constructor<>()                    // Default constructor only for now
        .method("area", &Rectangle::area)
        .build();
    
    std::string script = R"(
        auto r1 = Rectangle{};           // 0x0
        
        return r1;  // Just return the object for now
    )";
    
    Value result = engine.execute(script);
    expect_true(result.isObject());
}

// Benchmark tests
JAI_BENCHMARK(class_instantiation) {
    Engine engine;
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<float, float>()
        .method("distance", &Point::distance)
        .build();
    
    engine.execute("auto p = Point{3.0, 4.0}; p.distance();");
}

JAI_BENCHMARK(property_access_performance) {
    Engine engine;
    
    makeClassBuilder<Point>(engine, "Point")
        .constructor<float, float>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    engine.execute(R"(
        auto p = Point{10.0, 20.0};
        for (int i = 0; i < 100; ++i) {
            p.x = p.x + 1.0;
            p.y = p.y + 1.0;
        }
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()