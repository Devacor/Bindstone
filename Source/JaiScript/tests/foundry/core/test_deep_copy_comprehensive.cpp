#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

// Test classes
class Point {
public:
    double x, y;
    
    Point() : x(0), y(0) {}
    Point(double x, double y) : x(x), y(y) {}
    Point(const Point& other) : x(other.x), y(other.y) {
        // We can verify copy constructor is called
    }
};

// Polymorphic classes
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    virtual std::string type() const = 0;
};

class Circle : public Shape {
public:
    double radius;
    
    Circle(double r) : radius(r) {}
    double area() const override { return 3.14159 * radius * radius; }
    std::string type() const override { return "Circle"; }
};

class Rectangle : public Shape {
public:
    double width, height;
    
    Rectangle(double w, double h) : width(w), height(h) {}
    double area() const override { return width * height; }
    std::string type() const override { return "Rectangle"; }
};

class deep_copy_comprehensive_tests : public suite {
public:
    deep_copy_comprehensive_tests() : suite("Deep Copy Comprehensive Tests") {}
    
    void forge_tests() override {
        test("container_deep_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Test map deep copy
            std::string script = R"(
                var map1 = {"a": 1, "b": [1, 2, 3]};
                var map2 = map1;
                
                // Modify map2
                map2["c"] = 3;
                map2["b"].push(4);
                
                // map1 should be unchanged
                map1.size() == 2 && map1["b"].size() == 3
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Map deep copy failed");
        });
        
        test("nested_container_deep_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            std::string script = R"(
                var data = {
                    "array": [
                        {"x": 1, "y": 2},
                        {"x": 3, "y": 4}
                    ],
                    "nested": {
                        "deep": {
                            "value": 42
                        }
                    }
                };
                
                var copy = data;
                
                // Modify copy
                copy["array"][0]["x"] = 99;
                copy["nested"]["deep"]["value"] = 100;
                
                // Original should be unchanged
                data["array"][0]["x"] == 1 && 
                data["nested"]["deep"]["value"] == 42
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Nested container deep copy failed");
        });
        
        test("cpp_object_deep_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register Point class
            make_class_builder<Point>(engine, "Point")
                .constructor<>()
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            std::string script = R"(
                var p1 = Point(3.0, 4.0);
                var p2 = p1;  // Should deep copy
                
                p2.x = 5.0;
                p2.y = 6.0;
                
                // p1 should be unchanged
                p1.x == 3.0 && p1.y == 4.0
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "C++ object deep copy failed");
        });
        
        test("polymorphic_deep_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register polymorphic classes
            make_class_builder<Shape>(engine, "Shape")
                .method("area", &Shape::area)
                .method("type", &Shape::type)
                .build();
                
            make_class_builder<Circle>(engine, "Circle")
                .base_class<Shape>()
                .constructor<double>()
                .property("radius", &Circle::radius)
                .build();
                
            make_class_builder<Rectangle>(engine, "Rectangle")
                .base_class<Shape>()
                .constructor<double, double>()
                .property("width", &Rectangle::width)
                .property("height", &Rectangle::height)
                .build();
            
            std::string script = R"(
                // Create shapes
                var shape1 = Circle(5.0);
                var shape2 = shape1;  // Should deep copy as Circle
                
                // Modify shape2
                shape2.radius = 10.0;
                
                // shape1 should be unchanged
                var area1 = shape1.area();
                var area2 = shape2.area();
                
                // Area of circle with radius 5 vs radius 10
                area1 < 80 && area2 > 300
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Polymorphic deep copy failed");
        });
        
        test("mixed_container_object_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register Point class
            make_class_builder<Point>(engine, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            std::string script = R"(
                // Array of objects
                var points = [
                    Point(1.0, 2.0),
                    Point(3.0, 4.0),
                    Point(5.0, 6.0)
                ];
                
                var copy = points;
                
                // Modify copy
                copy[0].x = 99.0;
                copy.push(Point(7.0, 8.0));
                
                // Original should be unchanged
                points.size() == 3 && points[0].x == 1.0
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Mixed container/object deep copy failed");
        });
        
        test("assignment_operator_deep_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            std::string script = R"(
                var arr1 = [1, 2, 3];
                var arr2 = [4, 5];
                
                arr2 = arr1;  // Assignment should also deep copy
                arr2.push(4);
                
                arr1.size() == 3 && arr2.size() == 4
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Assignment operator deep copy failed");
        });
        
        test("function_parameter_copy", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            std::string script = R"(
                function modify(auto arr) {
                    arr.push(99);
                    return arr.size();
                }
                
                var original = [1, 2, 3];
                var size = modify(original);
                
                // original should be unchanged (passed by value)
                original.size() == 3 && size == 4
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Function parameter copy failed");
        });
    }
};

FOUNDRY_REGISTER(deep_copy_comprehensive_tests)