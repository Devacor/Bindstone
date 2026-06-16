#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
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

// Test class using JAI_PROPERTY macros for auto_bind property tests
class PropertyOwnerTest : public property_owner<PropertyOwnerTest> {
public:
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((float), speed, 1.5f);
    JAI_PROPERTY((std::string), name, "Player");
    JAI_PROPERTY((bool), active, true);

    PropertyOwnerTest() = default;

    // Method to verify C++ side sees the property values
    int get_health_cpp() const { return health.get(); }
    void set_health_cpp(int val) { health = val; }
};

// Test class using JAI_OBSERVABLE_PROPERTY for signal observation tests
class ObservableOwnerTest : public property_owner<ObservableOwnerTest> {
public:
    JAI_OBSERVABLE_PROPERTY((int), score, 0);
    JAI_OBSERVABLE_PROPERTY((std::string), status, "idle");

    ObservableOwnerTest() = default;

    int get_score_cpp() const { return score.get(); }
    void set_score_cpp(int val) { score = val; }
};

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

class dynamic_binder_tests : public suite {
public:
    dynamic_binder_tests() : suite("Class Builder") {}
    
    void pre_test() override {
        // Reset static state before each test
        Point::point_count = 0;
    }
    
    void forge_tests() override {
        test("basic_class_registration", [this]() {
            auto eng = engine::make();
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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

            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Counter>(*eng, "Counter")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Point>(*eng, "Point")
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
            
            dynamic_binder<Counter>(*eng, "Counter")
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
            
            dynamic_binder<Calculator>(*eng, "Calculator")
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

        // Same NAME, same ARITY, different parameter TYPES must resolve by argument type. Mirrors
        // MV::Color: set(float...) stores channels raw; set(int...) divides by 255. Before the fix
        // the two collapsed to the last registration (int), so a float set() truncated to near-black.
        test("same_arity_overload_resolves_by_type", [this]() {
            auto eng = engine::make();

            class ColorLike {
            public:
                float r = 0, g = 0, b = 0, a = 0;
                ColorLike& set(float R, float G, float B, float A) { r=R; g=G; b=B; a=A; return *this; }
                ColorLike& set(int R, int G, int B, int A) { r=R/255.0f; g=G/255.0f; b=B/255.0f; a=A/255.0f; return *this; }
            };

            dynamic_binder<ColorLike>(*eng, "ColorLike")
                .constructor<>()
                .property("r", &ColorLike::r)
                .property("g", &ColorLike::g)
                .method("set", static_cast<ColorLike&(ColorLike::*)(float, float, float, float)>(&ColorLike::set))
                .method("set", static_cast<ColorLike&(ColorLike::*)(int, int, int, int)>(&ColorLike::set))
                .build();

            // Float arguments -> the float overload (raw); near-black ~0.004 if it were sliced to int.
            eng->execute("auto c = ColorLike(); c.set(1.0, 0.5, 0.25, 1.0);");
            check_near(1.0, eng->execute("c.r").as<double>(), 1e-5);
            check_near(0.5, eng->execute("c.g").as<double>(), 1e-5);

            // Integer arguments -> the int overload (divided by 255).
            eng->execute("auto d = ColorLike(); d.set(255, 128, 64, 255);");
            check_near(1.0, eng->execute("d.r").as<double>(), 1e-5);
            check_near(128.0/255.0, eng->execute("d.g").as<double>(), 1e-3);
        });

        // Same-arity CONSTRUCTORS (the other half of the MV::Color clobber: Color(float...) vs
        // Color(int...)) must also resolve by argument type. This also guards the registration-path
        // recursion bug: routing the 2nd ctor through the typed entry point used to migrate the
        // dispatcher into the overload set, so a 0-arg call self-recursed into a stack overflow.
        test("same_arity_constructor_resolves_by_type", [this]() {
            auto eng = engine::make();

            class Rgb {
            public:
                float r = 0, g = 0, b = 0;
                Rgb() {}
                Rgb(float R, float G, float B) { r=R; g=G; b=B; }
                Rgb(int R, int G, int B) { r=R/255.0f; g=G/255.0f; b=B/255.0f; }
            };

            dynamic_binder<Rgb>(*eng, "Rgb")
                .constructor<>()
                .constructor<float, float, float>()
                .constructor<int, int, int>()
                .property("r", &Rgb::r)
                .property("g", &Rgb::g)
                .build();

            // The 0-arg ctor must still work after typed overloads are registered (no recursion).
            eng->execute("auto z = Rgb();");
            check_near(0.0, eng->execute("z.r").as<double>(), 1e-5);

            eng->execute("auto a = Rgb(1.0, 0.5, 0.25);");
            check_near(1.0, eng->execute("a.r").as<double>(), 1e-5);
            check_near(0.5, eng->execute("a.g").as<double>(), 1e-5);

            eng->execute("auto b = Rgb(255, 128, 64);");
            check_near(1.0, eng->execute("b.r").as<double>(), 1e-5);
            check_near(128.0/255.0, eng->execute("b.g").as<double>(), 1e-3);
        });

        test("equality_operator", [this]() {
            auto eng = engine::make();

            dynamic_binder<Point>(*eng, "Point")
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
            dynamic_binder<Counter>(*eng, "Counter")
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
            dynamic_binder<Point>(*eng, "Point")
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
            dynamic_binder<Counter>(*eng, "Counter")
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
            dynamic_binder<Point>(*eng, "Point")
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
            dynamic_binder<Counter>(*eng, "Counter")
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
            dynamic_binder<Point>(*eng, "Point")
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
            dynamic_binder<Point>(*eng, "Point")
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

        test("auto_bind_inheritance_chain", [this]() {
            // Test that auto_bind with property_owner correctly chains inheritance
            // class C : property_owner<C>        -> no base
            // class B : property_owner<B, C>     -> inherits from C
            // class A : property_owner<A, B>     -> inherits from B
            // When calling method from C on instance of A, it should work via chain A -> B -> C

            struct TestC : property_owner<TestC> {
                std::string c_method() const { return "from_C"; }
            };

            struct TestB : property_owner<TestB, TestC> {
                std::string b_method() const { return "from_B"; }
            };

            struct TestA : property_owner<TestA, TestB> {
                std::string a_method() const { return "from_A"; }
            };

            auto eng = engine::make();

            // Register in dependency order: C first, then B, then A
            dynamic_binder<TestC>(*eng, "TestC")
                .constructor<>()
                .method("c_method", &TestC::c_method)
                .build();

            dynamic_binder<TestB>(*eng, "TestB")
                .auto_bind()  // Should auto-register TestC as base
                .constructor<>()
                .method("b_method", &TestB::b_method)
                .build();

            dynamic_binder<TestA>(*eng, "TestA")
                .auto_bind()  // Should auto-register TestB as base
                .constructor<>()
                .method("a_method", &TestA::a_method)
                .build();

            // Create instance of A
            eng->execute("auto a = TestA();");

            // Test calling methods at each level
            check_eq(eng->execute("a.a_method()").as<std::string>(), "from_A", "Direct method");
            check_eq(eng->execute("a.b_method()").as<std::string>(), "from_B", "Inherited from B");
            check_eq(eng->execute("a.c_method()").as<std::string>(), "from_C", "Inherited from C via chain A->B->C");
        });

        test("auto_bind_multiple_inheritance", [this]() {
            // Test that auto_bind correctly handles multiple base classes
            // class Left  : property_owner<Left>           -> no base
            // class Right : property_owner<Right>          -> no base
            // class Multi : property_owner<Multi, Left, Right>  -> inherits from BOTH
            // Methods from both Left and Right should be accessible on Multi

            struct Left : property_owner<Left> {
                std::string left_method() const { return "from_Left"; }
            };

            struct Right : property_owner<Right> {
                std::string right_method() const { return "from_Right"; }
            };

            struct Multi : property_owner<Multi, Left, Right> {
                std::string multi_method() const { return "from_Multi"; }
            };

            auto eng = engine::make();

            // Register base classes first
            dynamic_binder<Left>(*eng, "Left")
                .constructor<>()
                .method("left_method", &Left::left_method)
                .build();

            dynamic_binder<Right>(*eng, "Right")
                .constructor<>()
                .method("right_method", &Right::right_method)
                .build();

            // Register Multi with auto_bind - should auto-register both Left and Right as bases
            dynamic_binder<Multi>(*eng, "Multi")
                .auto_bind()  // Should register both Left AND Right as bases
                .constructor<>()
                .method("multi_method", &Multi::multi_method)
                .build();

            // Create instance of Multi
            eng->execute("auto m = Multi();");

            // Test calling methods from all three classes
            check_eq(eng->execute("m.multi_method()").as<std::string>(), "from_Multi", "Direct method");
            check_eq(eng->execute("m.left_method()").as<std::string>(), "from_Left", "Inherited from Left");
            check_eq(eng->execute("m.right_method()").as<std::string>(), "from_Right", "Inherited from Right");
        });

        test("auto_bind_deep_chain", [this]() {
            // Test deep inheritance chain with multiple inheritance
            // This tests that chains are followed correctly even with complex hierarchies
            //
            //   GrandparentA     GrandparentB
            //        |               |
            //     ParentA         ParentB
            //        \             /
            //          Child
            //
            // Child inherits from ParentA and ParentB, which have independent grandparents
            // All methods should be accessible

            struct GrandparentA : property_owner<GrandparentA> {
                std::string gpa_method() const { return "from_GrandparentA"; }
            };

            struct GrandparentB : property_owner<GrandparentB> {
                std::string gpb_method() const { return "from_GrandparentB"; }
            };

            struct ParentA : property_owner<ParentA, GrandparentA> {
                std::string pa_method() const { return "from_ParentA"; }
            };

            struct ParentB : property_owner<ParentB, GrandparentB> {
                std::string pb_method() const { return "from_ParentB"; }
            };

            struct DeepChild : property_owner<DeepChild, ParentA, ParentB> {
                std::string child_method() const { return "from_Child"; }
            };

            auto eng = engine::make();

            // Register in dependency order (grandparents first)
            dynamic_binder<GrandparentA>(*eng, "GrandparentA")
                .constructor<>()
                .method("gpa_method", &GrandparentA::gpa_method)
                .build();

            dynamic_binder<GrandparentB>(*eng, "GrandparentB")
                .constructor<>()
                .method("gpb_method", &GrandparentB::gpb_method)
                .build();

            dynamic_binder<ParentA>(*eng, "ParentA")
                .auto_bind()
                .constructor<>()
                .method("pa_method", &ParentA::pa_method)
                .build();

            dynamic_binder<ParentB>(*eng, "ParentB")
                .auto_bind()
                .constructor<>()
                .method("pb_method", &ParentB::pb_method)
                .build();

            dynamic_binder<DeepChild>(*eng, "DeepChild")
                .auto_bind()
                .constructor<>()
                .method("child_method", &DeepChild::child_method)
                .build();

            // Create instance
            eng->execute("auto c = DeepChild();");

            // Test all methods are accessible (including grandparent methods)
            check_eq(eng->execute("c.child_method()").as<std::string>(), "from_Child", "Direct method");
            check_eq(eng->execute("c.pa_method()").as<std::string>(), "from_ParentA", "Inherited from ParentA");
            check_eq(eng->execute("c.pb_method()").as<std::string>(), "from_ParentB", "Inherited from ParentB");
            check_eq(eng->execute("c.gpa_method()").as<std::string>(), "from_GrandparentA", "Inherited from GrandparentA via ParentA");
            check_eq(eng->execute("c.gpb_method()").as<std::string>(), "from_GrandparentB", "Inherited from GrandparentB via ParentB");
        });

        test("auto_bind_operator_equality", [this]() {
            // Test that auto_bind detects and registers operator== and operator!=
            struct EqualityTest {
                int value;
                EqualityTest() : value(0) {}
                EqualityTest(int v) : value(v) {}
                bool operator==(const EqualityTest& other) const { return value == other.value; }
            };

            auto eng = engine::make();

            dynamic_binder<EqualityTest>(*eng, "EqualityTest")
                .auto_bind()  // Should auto-detect operator== and register == and !=
                .constructor<int>()
                .property("value", &EqualityTest::value);

            // Test that == was auto-registered
            check(eng->execute("auto a = EqualityTest(5); auto b = EqualityTest(5); a == b").as<bool>(),
                  "Same values should be equal");
            check(!eng->execute("auto a = EqualityTest(5); auto b = EqualityTest(10); a == b").as<bool>(),
                  "Different values should not be equal");

            // Test that != was auto-registered (derived from ==)
            check(!eng->execute("auto a = EqualityTest(5); auto b = EqualityTest(5); a != b").as<bool>(),
                  "Same values should not be not-equal");
            check(eng->execute("auto a = EqualityTest(5); auto b = EqualityTest(10); a != b").as<bool>(),
                  "Different values should be not-equal");
        });

        test("jai_property_auto_bind_read", [this]() {
            // Test that JAI_PROPERTY macros work with auto_bind for reading properties
            auto eng = engine::make();

            dynamic_binder<PropertyOwnerTest>(*eng, "PropertyOwnerTest")
                .auto_bind()  // Should auto-detect JAI_PROPERTY macros and bind them
                .constructor<>()
                .method("get_health_cpp", &PropertyOwnerTest::get_health_cpp)
                .build();

            // Test reading default property values from script
            eng->execute("auto obj = PropertyOwnerTest();");
            check_eq(eng->execute("obj.health").as<int>(), 100, "Default health value");
            check_eq(eng->execute("obj.speed").as<float>(), 1.5f, "Default speed value");
            check_eq(eng->execute("obj.name").as<std::string>(), "Player", "Default name value");
            check_eq(eng->execute("obj.active").as<bool>(), true, "Default active value");
        });

        test("jai_property_auto_bind_write", [this]() {
            // Test that JAI_PROPERTY macros work with auto_bind for writing properties
            // This is the critical test: script modifications must update the C++ property
            auto eng = engine::make();

            dynamic_binder<PropertyOwnerTest>(*eng, "PropertyOwnerTest")
                .auto_bind()
                .constructor<>()
                .method("get_health_cpp", &PropertyOwnerTest::get_health_cpp)
                .build();

            // Create a C++ object and expose it to script using make_object
            auto cpp_obj = std::make_shared<PropertyOwnerTest>();
            check_eq(cpp_obj->health.get(), 100, "Initial C++ health is 100");

            eng->add_global("test_obj", eng->make_object(cpp_obj));

            // Modify property from script
            eng->execute("test_obj.health = 50;");

            // Verify C++ side sees the change
            check_eq(cpp_obj->health.get(), 50, "C++ health updated to 50 after script assignment");

            // Verify script can read the updated value
            check_eq(eng->execute("test_obj.health").as<int>(), 50, "Script reads updated health");

            // Test other property types
            eng->execute("test_obj.speed = 3.0;");
            check_eq(cpp_obj->speed.get(), 3.0f, "C++ speed updated after script assignment");

            eng->execute("test_obj.name = \"Hero\";");
            check_eq(cpp_obj->name.get(), "Hero", "C++ name updated after script assignment");

            eng->execute("test_obj.active = false;");
            check_eq(cpp_obj->active.get(), false, "C++ active updated after script assignment");
        });

        test("jai_property_auto_bind_compound_assignment", [this]() {
            // Test that compound assignment operators work with JAI_PROPERTY
            auto eng = engine::make();

            dynamic_binder<PropertyOwnerTest>(*eng, "PropertyOwnerTest")
                .auto_bind()
                .constructor<>()
                .build();

            auto cpp_obj = std::make_shared<PropertyOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            // Test += on int property
            eng->execute("obj.health += 25;");
            check_eq(cpp_obj->health.get(), 125, "health += 25 works");

            // Test -= on int property
            eng->execute("obj.health -= 50;");
            check_eq(cpp_obj->health.get(), 75, "health -= 50 works");

            // Test *= on float property
            eng->execute("obj.speed *= 2;");
            check_eq(cpp_obj->speed.get(), 3.0f, "speed *= 2 works");

            // Test /= on float property
            eng->execute("obj.speed /= 3;");
            check_eq(cpp_obj->speed.get(), 1.0f, "speed /= 3 works");
        });

        test("jai_property_cpp_to_script_sync", [this]() {
            // Test that C++ modifications to properties are visible in script
            auto eng = engine::make();

            dynamic_binder<PropertyOwnerTest>(*eng, "PropertyOwnerTest")
                .auto_bind()
                .constructor<>()
                .method("set_health_cpp", &PropertyOwnerTest::set_health_cpp)
                .build();

            auto cpp_obj = std::make_shared<PropertyOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            // Modify from C++
            cpp_obj->health = 999;

            // Script should see the C++ change
            check_eq(eng->execute("obj.health").as<int>(), 999, "Script sees C++ modification");

            // Modify via bound C++ method
            eng->execute("obj.set_health_cpp(42);");
            check_eq(eng->execute("obj.health").as<int>(), 42, "Script sees method modification");
            check_eq(cpp_obj->health.get(), 42, "C++ sees method modification");
        });

        test("observable_property_auto_bind", [this]() {
            // Test that JAI_OBSERVABLE_PROPERTY macros work with auto_bind
            auto eng = engine::make();

            dynamic_binder<ObservableOwnerTest>(*eng, "ObservableOwnerTest")
                .auto_bind()
                .constructor<>()
                .method("set_score_cpp", &ObservableOwnerTest::set_score_cpp)
                .build();

            // Test reading and writing observable properties
            auto cpp_obj = std::make_shared<ObservableOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            check_eq(eng->execute("obj.score").as<int>(), 0, "Default score is 0");
            eng->execute("obj.score = 42;");
            check_eq(cpp_obj->score.get(), 42, "C++ sees script modification");
            check_eq(eng->execute("obj.score").as<int>(), 42, "Script sees its own modification");

            // Test string observable property
            check_eq(eng->execute("obj.status").as<std::string>(), "idle", "Default status is 'idle'");
            eng->execute("obj.status = \"running\";");
            check_eq(cpp_obj->status.get(), "running", "C++ sees string modification");
        });

        test("observable_property_script_callback", [this]() {
            // Test that script can register callbacks for observable property changes
            auto eng = engine::make();

            dynamic_binder<ObservableOwnerTest>(*eng, "ObservableOwnerTest")
                .auto_bind()
                .constructor<>()
                .method("set_score_cpp", &ObservableOwnerTest::set_score_cpp)
                .build();

            auto cpp_obj = std::make_shared<ObservableOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            // Create a script variable to track callback invocations
            eng->execute("var callback_count = 0;");
            eng->execute("var last_old_value = -1;");
            eng->execute("var last_new_value = -1;");

            // Register a callback on score changes using JaiScript lambda syntax
            eng->execute(R"(
                obj.on_score_change([&](auto old_val, auto new_val) {
                    callback_count += 1;
                    last_old_value = old_val;
                    last_new_value = new_val;
                });
            )");

            // Initially no callbacks should have fired
            check_eq(eng->execute("callback_count").as<int>(), 0, "No callbacks yet");

            // Modify from script - should trigger callback
            eng->execute("obj.score = 10;");
            check_eq(eng->execute("callback_count").as<int>(), 1, "Callback fired once");
            check_eq(eng->execute("last_old_value").as<int>(), 0, "Old value was 0");
            check_eq(eng->execute("last_new_value").as<int>(), 10, "New value is 10");

            // Modify from C++ - should also trigger callback
            cpp_obj->score = 25;
            check_eq(eng->execute("callback_count").as<int>(), 2, "Callback fired again from C++");
            check_eq(eng->execute("last_old_value").as<int>(), 10, "Old value was 10");
            check_eq(eng->execute("last_new_value").as<int>(), 25, "New value is 25");

            // Setting to same value should NOT trigger callback (observable_property checks for change)
            eng->execute("obj.score = 25;");
            check_eq(eng->execute("callback_count").as<int>(), 2, "No callback for same value");
        });

        test("observable_property_multiple_callbacks", [this]() {
            // Test that multiple script callbacks can be registered
            auto eng = engine::make();

            dynamic_binder<ObservableOwnerTest>(*eng, "ObservableOwnerTest")
                .auto_bind()
                .constructor<>()
                .build();

            auto cpp_obj = std::make_shared<ObservableOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            eng->execute("var sum = 0;");

            // Register multiple callbacks using JaiScript lambda syntax
            eng->execute("obj.on_score_change([&](auto old_val, auto new_val) { sum += new_val; });");
            eng->execute("obj.on_score_change([&](auto old_val, auto new_val) { sum += new_val * 2; });");

            // Trigger change - both callbacks should fire
            eng->execute("obj.score = 10;");

            // sum should be 10 + 20 = 30
            check_eq(eng->execute("sum").as<int>(), 30, "Both callbacks fired (10 + 20)");
        });

        test("observable_property_on_change_syntax", [this]() {
            // Test the new player.score.on_change(callback) API syntax
            // This uses transparent wrappers to allow accessing on_change on the property itself
            auto eng = engine::make();

            dynamic_binder<ObservableOwnerTest>(*eng, "ObservableOwnerTest")
                .auto_bind()
                .constructor<>()
                .build();

            auto cpp_obj = std::make_shared<ObservableOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            // Create tracking variables
            eng->execute("var change_count = 0;");
            eng->execute("var last_old = -1;");
            eng->execute("var last_new = -1;");

            // Register callback using new syntax: obj.score.on_change(callback)
            eng->execute(R"(
                obj.score.on_change([&](auto old_val, auto new_val) {
                    change_count += 1;
                    last_old = old_val;
                    last_new = new_val;
                });
            )");

            // Initially no callbacks
            check_eq(eng->execute("change_count").as<int>(), 0, "No callbacks initially");

            // Test that obj.score still works as an integer (transparent wrapper)
            check_eq(eng->execute("obj.score").as<int>(), 0, "obj.score returns int value");
            check_eq(eng->execute("obj.score + 5").as<int>(), 5, "obj.score + 5 works (transparent unwrap)");

            // Modify from script - should trigger callback
            eng->execute("obj.score = 42;");
            check_eq(eng->execute("change_count").as<int>(), 1, "Callback fired");
            check_eq(eng->execute("last_old").as<int>(), 0, "Old value was 0");
            check_eq(eng->execute("last_new").as<int>(), 42, "New value is 42");

            // Verify arithmetic still works on the property
            check_eq(eng->execute("obj.score * 2").as<int>(), 84, "obj.score * 2 works");
            check_eq(eng->execute("obj.score - 2").as<int>(), 40, "obj.score - 2 works");

            // Modify from C++ - should also trigger callback
            cpp_obj->score = 100;
            check_eq(eng->execute("change_count").as<int>(), 2, "Callback fired from C++ change");
            check_eq(eng->execute("last_new").as<int>(), 100, "New value is 100");
        });

        test("observable_property_transparent_comparison", [this]() {
            // Test that observable properties work in comparisons (transparent wrapper)
            auto eng = engine::make();

            dynamic_binder<ObservableOwnerTest>(*eng, "ObservableOwnerTest")
                .auto_bind()
                .constructor<>()
                .build();

            auto cpp_obj = std::make_shared<ObservableOwnerTest>();
            eng->add_global("obj", eng->make_object(cpp_obj));

            cpp_obj->score = 50;

            // Test comparisons with the transparent wrapper
            check_eq(eng->execute("obj.score == 50").as<bool>(), true, "obj.score == 50");
            check_eq(eng->execute("obj.score != 50").as<bool>(), false, "obj.score != 50 is false");
            check_eq(eng->execute("obj.score > 40").as<bool>(), true, "obj.score > 40");
            check_eq(eng->execute("obj.score < 60").as<bool>(), true, "obj.score < 60");
            check_eq(eng->execute("obj.score >= 50").as<bool>(), true, "obj.score >= 50");
            check_eq(eng->execute("obj.score <= 50").as<bool>(), true, "obj.score <= 50");
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::dynamic_binder_tests)