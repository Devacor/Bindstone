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

    // Arithmetic operators
    Point operator+(const Point& other) const { return Point(x + other.x, y + other.y); }
    Point operator-(const Point& other) const { return Point(x - other.x, y - other.y); }
    Point operator*(double scalar) const { return Point(x * scalar, y * scalar); }
    Point operator/(double scalar) const { return Point(x / scalar, y / scalar); }

    // Comparison operators
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    bool operator<(const Point& other) const { return length() < other.length(); }
    bool operator<=(const Point& other) const { return length() <= other.length(); }
    bool operator>(const Point& other) const { return length() > other.length(); }
    bool operator>=(const Point& other) const { return length() >= other.length(); }

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

    // Arithmetic operators
    Counter operator+(const Counter& other) const { return Counter(value + other.value); }
    Counter operator-(const Counter& other) const { return Counter(value - other.value); }
    Counter operator*(int scalar) const { return Counter(value * scalar); }
    Counter operator/(int scalar) const { return Counter(value / scalar); }
    Counter operator%(int mod) const { return Counter(value % mod); }

    // Comparison operators
    bool operator<(const Counter& other) const { return value < other.value; }
    bool operator<=(const Counter& other) const { return value <= other.value; }
    bool operator>(const Counter& other) const { return value > other.value; }
    bool operator>=(const Counter& other) const { return value >= other.value; }
};

class class_builder_tests : public suite {
public:
    class_builder_tests() : suite("Class Builder") {}
    
    void pre_test() override {
        // Reset static state before each test
        Point::point_count = 0;
    }
    
    void forge_tests() override {
        test("basic_class_registration", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<>()
                .constructor<double, double>()
                .method("length", &Point::length)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            auto result = eng->execute("auto p = Point(3, 4); p.length()");
            check_eq(result.as<double>(), 5.0);
        });
        
        test("constructors", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<>()
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Default constructor
            eng->execute("auto p1 = Point();");
            check_eq(eng->execute("p1.x").as<double>(), 0.0);
            check_eq(eng->execute("p1.y").as<double>(), 0.0);
            
            // Parameterized constructor
            eng->execute("auto p2 = Point(3, 4);");
            check_eq(eng->execute("p2.x").as<double>(), 3.0);
            check_eq(eng->execute("p2.y").as<double>(), 4.0);
        });
        
        test("methods", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .method("move_by", &Point::move_by)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng->execute("auto p = Point(1, 2);");
            eng->execute("p.move_by(3, 4);");
            check_eq(eng->execute("p.x").as<double>(), 4.0);
            check_eq(eng->execute("p.y").as<double>(), 6.0);
        });
        
        test("properties", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng->execute("auto p = Point();");
            eng->execute("p.x = 5;");
            eng->execute("p.y = 7;");
            check_eq(eng->execute("p.x").as<double>(), 5.0);
            check_eq(eng->execute("p.y").as<double>(), 7.0);
        });
        
        test("operator_overloading", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .method("+", &Point::operator+)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            eng->execute("auto p1 = Point(1, 2);");
            eng->execute("auto p2 = Point(3, 4);");
            eng->execute("auto p3 = p1 + p2;");
            check_eq(eng->execute("p3.x").as<double>(), 4.0);
            check_eq(eng->execute("p3.y").as<double>(), 6.0);
        });
        
        test("static_methods", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Register static method as a global function
            eng->add_function("Point_origin", &Point::origin);
            
            eng->execute("auto p = Point_origin();");
            check_eq(eng->execute("p.x").as<double>(), 0.0);
            check_eq(eng->execute("p.y").as<double>(), 0.0);
        });
        
        test("static_properties", [this]() {
            auto eng = engine::make();

            class_builder<Point>(*eng, "Point")
                .constructor<>()
                .build();

            // Register static property as global reference (not copy!)
            // Using add_global_ref ensures script modifications update the C++ static
            Point::point_count = 42;
            eng->add_global_ref("Point_count", Point::point_count);

            check_eq(eng->execute("Point_count").as<int>(), 42);

            eng->execute("Point_count = 100;");
            check_eq(Point::point_count, 100);
        });
        
        test("method_chaining", [this]() {
            auto eng = engine::make();
            
            class_builder<Counter>(*eng, "Counter")
                .constructor<>()
                .constructor<int>()
                .method("increment", &Counter::increment)
                .method("add", &Counter::add)
                .method("get", &Counter::get)
                .method("reset", &Counter::reset)
                .build();
            
            auto result = eng->execute("Counter().increment().add(5).increment().get()");
            check_eq(result.as<int>(), 7);
        });
        
        test("lambda_methods", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
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
            
            eng->execute("auto p1 = Point(0, 0);");
            eng->execute("auto p2 = Point(3, 4);");
            auto dist = eng->execute("p1.distance_to(p2)");
            check_eq(dist.as<double>(), 5.0);
            
            eng->execute("p2.scale(2);");
            check_eq(eng->execute("p2.x").as<double>(), 6.0);
            check_eq(eng->execute("p2.y").as<double>(), 8.0);
        });
        
        test("custom_getters_setters", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
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
            
            eng->execute("auto p = Point(3, 4);");
            check_eq(eng->execute("p.length_squared()").as<double>(), 25.0);
            
            eng->execute("p.set_both(5);");
            check_eq(eng->execute("p.x").as<double>(), 5.0);
            check_eq(eng->execute("p.y").as<double>(), 5.0);
        });
        
        test("shared_ptr_support", [this]() {
            auto eng = engine::make();
            
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Function that returns shared_ptr
            eng->add_function("make_shared_point", [](double x, double y) {
                return std::make_shared<Point>(x, y);
            });
            
            // Function that accepts shared_ptr
            eng->add_function("point_length", [](std::shared_ptr<Point> p) {
                return p->length();
            });
            
            auto result = eng->execute("auto p = make_shared_point(3, 4); point_length(p)");
            check_eq(result.as<double>(), 5.0);
        });
        
        test("const_correctness", [this]() {
            auto eng = engine::make();
            
            class_builder<Counter>(*eng, "Counter")
                .constructor<int>()
                .method("get", &Counter::get)  // const method
                .method("reset", &Counter::reset)  // non-const method
                .build();
            
            eng->execute("auto c = Counter(42);");
            check_eq(eng->execute("c.get()").as<int>(), 42);
            
            eng->execute("c.reset();");
            check_eq(eng->execute("c.get()").as<int>(), 0);
        });
        
        test("overloaded_methods", [this]() {
            auto eng = engine::make();
            
            class Calculator {
            public:
                int add(int a) { return a; }
                int add(int a, int b) { return a + b; }
                int add(int a, int b, int c) { return a + b + c; }
            };
            
            class_builder<Calculator>(*eng, "Calculator")
                .constructor<>()
                .method("add", 
                    static_cast<int(Calculator::*)(int)>(&Calculator::add))
                .method("add", 
                    static_cast<int(Calculator::*)(int, int)>(&Calculator::add))
                .method("add", 
                    static_cast<int(Calculator::*)(int, int, int)>(&Calculator::add))
                .build();
            
            eng->execute("auto calc = Calculator();");
            check_eq(eng->execute("calc.add(5)").as<int>(), 5);
            check_eq(eng->execute("calc.add(5, 3)").as<int>(), 8);
            check_eq(eng->execute("calc.add(5, 3, 2)").as<int>(), 10);
        });

        test("equality_operator", [this]() {
            auto eng = engine::make();

            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("==", &Point::operator==)
                .build();

            // Same values should be equal
            eng->execute("auto p1 = Point(3, 4);");
            eng->execute("auto p2 = Point(3, 4);");
            auto eq_result = eng->execute("p1 == p2");
            check_eq(eq_result.as<bool>(), true, "Points with same values are equal");

            // Different values should not be equal
            eng->execute("auto p3 = Point(5, 6);");
            auto neq_result = eng->execute("p1 == p3");
            check_eq(neq_result.as<bool>(), false, "Points with different values are not equal");

            // Self-equality
            auto self_eq = eng->execute("p1 == p1");
            check_eq(self_eq.as<bool>(), true, "Point equals itself");

            // Not-equal operator should also work
            auto neq_result2 = eng->execute("p1 != p3");
            check_eq(neq_result2.as<bool>(), true, "p1 != p3 is true");

            auto neq_result3 = eng->execute("p1 != p2");
            check_eq(neq_result3.as<bool>(), false, "p1 != p2 is false (same values)");
        });

        test("equality_operator_via_lambda", [this]() {
            auto eng = engine::make();

            // Test registering operator== via lambda (for cases where the class
            // doesn't have a C++ operator==, or for custom comparison logic)
            class_builder<Counter>(*eng, "Counter")
                .constructor<int>()
                .method("get", &Counter::get)
                .method("==", [](const Counter& self, const Counter& other) {
                    return self.value == other.value;
                })
                .build();

            eng->execute("auto c1 = Counter(42);");
            eng->execute("auto c2 = Counter(42);");
            eng->execute("auto c3 = Counter(99);");

            check_eq(eng->execute("c1 == c2").as<bool>(), true, "Counters with same value are equal");
            check_eq(eng->execute("c1 == c3").as<bool>(), false, "Counters with different value are not equal");
            check_eq(eng->execute("c1 != c3").as<bool>(), true, "c1 != c3");
        });

        test("comparison_operators", [this]() {
            auto eng = engine::make();

            // Register Point with all comparison operators
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("length", &Point::length)
                .method("<", &Point::operator<)
                .method("<=", &Point::operator<=)
                .method(">", &Point::operator>)
                .method(">=", &Point::operator>=)
                .build();

            // Create points with different lengths: (3,4)=5, (0,0)=0, (6,8)=10
            eng->execute("auto p1 = Point(3, 4);");   // length = 5
            eng->execute("auto p2 = Point(0, 0);");   // length = 0
            eng->execute("auto p3 = Point(6, 8);");   // length = 10

            // Test less than
            check_eq(eng->execute("p2 < p1").as<bool>(), true, "Point(0,0) < Point(3,4)");
            check_eq(eng->execute("p1 < p3").as<bool>(), true, "Point(3,4) < Point(6,8)");
            check_eq(eng->execute("p3 < p1").as<bool>(), false, "Point(6,8) < Point(3,4) is false");

            // Test greater than
            check_eq(eng->execute("p3 > p1").as<bool>(), true, "Point(6,8) > Point(3,4)");
            check_eq(eng->execute("p1 > p2").as<bool>(), true, "Point(3,4) > Point(0,0)");
            check_eq(eng->execute("p1 > p3").as<bool>(), false, "Point(3,4) > Point(6,8) is false");

            // Test less than or equal
            check_eq(eng->execute("p1 <= p3").as<bool>(), true, "Point(3,4) <= Point(6,8)");
            eng->execute("auto p4 = Point(3, 4);");   // same as p1
            check_eq(eng->execute("p1 <= p4").as<bool>(), true, "Point(3,4) <= Point(3,4) (equal)");

            // Test greater than or equal
            check_eq(eng->execute("p3 >= p1").as<bool>(), true, "Point(6,8) >= Point(3,4)");
            check_eq(eng->execute("p1 >= p4").as<bool>(), true, "Point(3,4) >= Point(3,4) (equal)");
        });

        test("comparison_operators_via_lambda", [this]() {
            auto eng = engine::make();

            // Register Counter with comparison operators via lambda
            class_builder<Counter>(*eng, "Counter")
                .constructor<int>()
                .method("get", &Counter::get)
                .method("<", [](const Counter& self, const Counter& other) {
                    return self.value < other.value;
                })
                .method(">", [](const Counter& self, const Counter& other) {
                    return self.value > other.value;
                })
                .build();

            eng->execute("auto c1 = Counter(10);");
            eng->execute("auto c2 = Counter(20);");

            check_eq(eng->execute("c1 < c2").as<bool>(), true, "Counter(10) < Counter(20)");
            check_eq(eng->execute("c2 > c1").as<bool>(), true, "Counter(20) > Counter(10)");
            check_eq(eng->execute("c2 < c1").as<bool>(), false, "Counter(20) < Counter(10) is false");
        });

        test("arithmetic_operators", [this]() {
            auto eng = engine::make();

            // Register Point with arithmetic operators
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("+", &Point::operator+)
                .method("-", &Point::operator-)
                .build();

            // Test addition
            eng->execute("auto p1 = Point(1, 2);");
            eng->execute("auto p2 = Point(3, 4);");
            eng->execute("auto sum = p1 + p2;");
            check_eq(eng->execute("sum.x").as<double>(), 4.0, "Point addition x");
            check_eq(eng->execute("sum.y").as<double>(), 6.0, "Point addition y");

            // Test subtraction
            eng->execute("auto diff = p2 - p1;");
            check_eq(eng->execute("diff.x").as<double>(), 2.0, "Point subtraction x");
            check_eq(eng->execute("diff.y").as<double>(), 2.0, "Point subtraction y");
        });

        test("arithmetic_operators_all", [this]() {
            auto eng = engine::make();

            // Register Counter with all arithmetic operators
            class_builder<Counter>(*eng, "Counter")
                .constructor<int>()
                .method("get", &Counter::get)
                .method("+", &Counter::operator+)
                .method("-", &Counter::operator-)
                .method("*", static_cast<Counter(Counter::*)(int) const>(&Counter::operator*))
                .method("/", static_cast<Counter(Counter::*)(int) const>(&Counter::operator/))
                .method("%", static_cast<Counter(Counter::*)(int) const>(&Counter::operator%))
                .build();

            eng->execute("auto a = Counter(10);");
            eng->execute("auto b = Counter(3);");

            // Test addition
            check_eq(eng->execute("(a + b).get()").as<int>(), 13, "10 + 3 = 13");

            // Test subtraction
            check_eq(eng->execute("(a - b).get()").as<int>(), 7, "10 - 3 = 7");

            // Test multiplication
            check_eq(eng->execute("(a * 2).get()").as<int>(), 20, "10 * 2 = 20");

            // Test division
            check_eq(eng->execute("(a / 2).get()").as<int>(), 5, "10 / 2 = 5");

            // Test modulo
            check_eq(eng->execute("(a % 3).get()").as<int>(), 1, "10 % 3 = 1");
        });

        test("arithmetic_operators_via_lambda", [this]() {
            auto eng = engine::make();

            // Register Point with arithmetic operators via lambda
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("*", [](const Point& self, double scalar) {
                    return Point(self.x * scalar, self.y * scalar);
                })
                .method("/", [](const Point& self, double scalar) {
                    return Point(self.x / scalar, self.y / scalar);
                })
                .build();

            eng->execute("auto p = Point(4, 6);");

            // Test scalar multiplication
            eng->execute("auto scaled = p * 2;");
            check_eq(eng->execute("scaled.x").as<double>(), 8.0, "Point * 2 x");
            check_eq(eng->execute("scaled.y").as<double>(), 12.0, "Point * 2 y");

            // Test scalar division
            eng->execute("auto halved = p / 2;");
            check_eq(eng->execute("halved.x").as<double>(), 2.0, "Point / 2 x");
            check_eq(eng->execute("halved.y").as<double>(), 3.0, "Point / 2 y");
        });

        test("compound_assignment_operators", [this]() {
            auto eng = engine::make();

            // Register Point with arithmetic operators
            class_builder<Point>(*eng, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("+", &Point::operator+)
                .method("-", &Point::operator-)
                .method("*", static_cast<Point(Point::*)(double) const>(&Point::operator*))
                .method("/", static_cast<Point(Point::*)(double) const>(&Point::operator/))
                .build();

            // Test += operator
            auto add_x = eng->execute(R"(
                auto p = Point(1, 2);
                p += Point(3, 4);
                p.x
            )");
            auto add_y = eng->execute(R"(
                auto p = Point(1, 2);
                p += Point(3, 4);
                p.y
            )");
            check_eq(add_x.as<double>(), 4.0, "Point += x");
            check_eq(add_y.as<double>(), 6.0, "Point += y");

            // Test -= operator
            auto sub_x = eng->execute(R"(
                auto p = Point(5, 7);
                p -= Point(2, 3);
                p.x
            )");
            auto sub_y = eng->execute(R"(
                auto p = Point(5, 7);
                p -= Point(2, 3);
                p.y
            )");
            check_eq(sub_x.as<double>(), 3.0, "Point -= x");
            check_eq(sub_y.as<double>(), 4.0, "Point -= y");

            // Test *= operator (scalar)
            auto mul_x = eng->execute(R"(
                auto p = Point(3, 4);
                p *= 2;
                p.x
            )");
            auto mul_y = eng->execute(R"(
                auto p = Point(3, 4);
                p *= 2;
                p.y
            )");
            check_eq(mul_x.as<double>(), 6.0, "Point *= x");
            check_eq(mul_y.as<double>(), 8.0, "Point *= y");

            // Test /= operator (scalar)
            auto div_x = eng->execute(R"(
                auto p = Point(6, 8);
                p /= 2;
                p.x
            )");
            auto div_y = eng->execute(R"(
                auto p = Point(6, 8);
                p /= 2;
                p.y
            )");
            check_eq(div_x.as<double>(), 3.0, "Point /= x");
            check_eq(div_y.as<double>(), 4.0, "Point /= y");
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::class_builder_tests)