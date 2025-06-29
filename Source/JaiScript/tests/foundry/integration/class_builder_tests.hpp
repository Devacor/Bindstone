#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

namespace jai::foundry::tests {

// Test classes for binding
class Point {
public:
    double x, y;
    
    Point() : x(0), y(0) {}
    Point(double x, double y) : x(x), y(y) {}
    
    double length() const { return std::sqrt(x * x + y * y); }
    void move_by(double dx, double dy) { x += dx; y += dy; }
    Point operator+(const Point& other) const { return Point(x + other.x, y + other.y); }
    
    static Point origin() { return Point(0, 0); }
    static int point_count;
};

int Point::point_count = 0;

class Counter {
public:
    int value;
    
    Counter() : value(0) { }
    explicit Counter(int v) : value(v) { }
    
    Counter& increment() { ++value; return *this; }
    Counter& add(int n) { value += n; return *this; }
    int get() const { return value; }
    void reset() { value = 0; }
};

class class_builder_tests : public suite {
public:
    class_builder_tests() : suite("Class Builder") {}
    
    void forge_tests() override {
        test("basic_class_registration", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<>()
                .constructor<double, double>()
                .method("length", &Point::length)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            auto result = eng.execute("auto p = Point(3, 4); p.length()");
            check_eq(result.as<double>(), 5.0);
        });
        
        test("constructors", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<>()
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Default constructor
            eng.execute("auto p1 = Point();");
            check_eq(eng.execute("p1.x").as<double>(), 0.0);
            check_eq(eng.execute("p1.y").as<double>(), 0.0);
            
            // Parameterized constructor
            eng.execute("auto p2 = Point(3, 4);");
            check_eq(eng.execute("p2.x").as<double>(), 3.0);
            check_eq(eng.execute("p2.y").as<double>(), 4.0);
        });
        
        test("methods", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .method("move_by", &Point::move_by)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng.execute("auto p = Point(1, 2);");
            eng.execute("p.move_by(3, 4);");
            check_eq(eng.execute("p.x").as<double>(), 4.0);
            check_eq(eng.execute("p.y").as<double>(), 6.0);
        });
        
        test("properties", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng.execute("auto p = Point();");
            eng.execute("p.x = 5;");
            eng.execute("p.y = 7;");
            check_eq(eng.execute("p.x").as<double>(), 5.0);
            check_eq(eng.execute("p.y").as<double>(), 7.0);
        });
        
        test("operator_overloading", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .method("+", &Point::operator+)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng.execute("auto p1 = Point(1, 2);");
            eng.execute("auto p2 = Point(3, 4);");
            eng.execute("auto p3 = p1 + p2;");
            check_eq(eng.execute("p3.x").as<double>(), 4.0);
            check_eq(eng.execute("p3.y").as<double>(), 6.0);
        });
        
        test("static_methods", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Register static method as a global function
            eng.add_function("Point_origin", &Point::origin);
            
            eng.execute("auto p = Point_origin();");
            check_eq(eng.execute("p.x").as<double>(), 0.0);
            check_eq(eng.execute("p.y").as<double>(), 0.0);
        });
        
        test("static_properties", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<>()
                .build();
            
            // Register static property as global variable
            Point::point_count = 42;
            eng.add_global("Point_count", &Point::point_count);
            
            check_eq(eng.execute("Point_count").as<int>(), 42);
            
            eng.execute("Point_count = 100;");
            check_eq(Point::point_count, 100);
        });
        
        test("method_chaining", [this]() {
            engine eng;
            
            make_class_builder<Counter>(eng, "Counter")
                .constructor<>()
                .constructor<int>()
                .method("increment", &Counter::increment)
                .method("add", &Counter::add)
                .method("get", &Counter::get)
                .method("reset", &Counter::reset)
                .build();
            
            auto result = eng.execute("Counter().increment().add(5).increment().get()");
            check_eq(result.as<int>(), 7);
        });
        
        test("lambda_methods", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("distance_to", [](const Point& self, const Point& other) {
                    double dx = self.x - other.x;
                    double dy = self.y - other.y;
                    return std::sqrt(dx * dx + dy * dy);
                })
                .method("scale", [](Point& self, double factor) -> Point& {
                    self.x *= factor;
                    self.y *= factor;
                    return self;
                })
                .build();
            
            eng.execute("auto p1 = Point(0, 0);");
            eng.execute("auto p2 = Point(3, 4);");
            auto dist = eng.execute("p1.distance_to(p2)");
            check_eq(dist.as<double>(), 5.0);
            
            eng.execute("p2.scale(2);");
            check_eq(eng.execute("p2.x").as<double>(), 6.0);
            check_eq(eng.execute("p2.y").as<double>(), 8.0);
        });
        
        test("custom_getters_setters", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("length_squared", [](const Point& self) {
                    return self.x * self.x + self.y * self.y;
                })
                .method("set_both", [](Point& self, double val) -> Point& {
                    self.x = val;
                    self.y = val;
                    return self;
                })
                .build();
            
            eng.execute("auto p = Point(3, 4);");
            check_eq(eng.execute("p.length_squared()").as<double>(), 25.0);
            
            eng.execute("p.set_both(5);");
            check_eq(eng.execute("p.x").as<double>(), 5.0);
            check_eq(eng.execute("p.y").as<double>(), 5.0);
        });
        
        test("shared_ptr_support", [this]() {
            engine eng;
            
            make_class_builder<Point>(eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Function that returns shared_ptr
            eng.add_function("make_shared_point", [](double x, double y) {
                return std::make_shared<Point>(x, y);
            });
            
            // Function that accepts shared_ptr
            eng.add_function("point_length", [](std::shared_ptr<Point> p) {
                return p->length();
            });
            
            auto result = eng.execute("auto p = make_shared_point(3, 4); point_length(p)");
            check_eq(result.as<double>(), 5.0);
        });
        
        test("const_correctness", [this]() {
            engine eng;
            
            make_class_builder<Counter>(eng, "Counter")
                .constructor<int>()
                .method("get", &Counter::get)  // const method
                .method("reset", &Counter::reset)  // non-const method
                .build();
            
            eng.execute("auto c = Counter(42);");
            check_eq(eng.execute("c.get()").as<int>(), 42);
            
            eng.execute("c.reset();");
            check_eq(eng.execute("c.get()").as<int>(), 0);
        });
        
        test("overloaded_methods", [this]() {
            engine eng;
            
            class Calculator {
            public:
                int add(int a) { return a; }
                int add(int a, int b) { return a + b; }
                int add(int a, int b, int c) { return a + b + c; }
            };
            
            make_class_builder<Calculator>(eng, "Calculator")
                .constructor<>()
                .method("add", 
                    static_cast<int(Calculator::*)(int)>(&Calculator::add))
                .method("add", 
                    static_cast<int(Calculator::*)(int, int)>(&Calculator::add))
                .method("add", 
                    static_cast<int(Calculator::*)(int, int, int)>(&Calculator::add))
                .build();
            
            eng.execute("auto calc = Calculator();");
            check_eq(eng.execute("calc.add(5)").as<int>(), 5);
            check_eq(eng.execute("calc.add(5, 3)").as<int>(), 8);
            check_eq(eng.execute("calc.add(5, 3, 2)").as<int>(), 10);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::class_builder_tests)