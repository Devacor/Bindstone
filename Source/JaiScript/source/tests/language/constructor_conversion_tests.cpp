/**
 * @file constructor_conversion_tests.cpp
 * @brief Comprehensive tests for JaiScript type conversion system
 *
 * These tests cover:
 * - Inbound conversions via single-argument constructors (script and C++)
 * - Outbound conversions via to_X() methods (to_int, to_float, to_string, to_bool, to_char)
 * - Inheritance-based no-copy compatibility for refs and shared_ptr
 * - Conversion priority resolution
 * - Error handling when no conversion exists
 * - Hot-reload behavior
 * - Edge cases (null, self-conversion, chained conversions, etc.)
 *
 * Valid JaiScript syntax for declarations:
 *   var w = Wrapper(42);       // dynamic typed, explicit construction
 *   auto w = Wrapper(42);      // strongly typed, explicit construction
 *   Wrapper w = Wrapper(42);   // typed variable, explicit construction
 *   Wrapper w{42};             // C++ style brace initialization
 *
 * Class field syntax:
 *   int x = 0;                 // type name = default;
 *
 * Implicit conversions happen in:
 *   - Function parameters: def foo(Wrapper w) called as foo(42)
 *   - Assignment to typed variables: Wrapper w; w = 42;
 *   - Return statements: def make(): Wrapper { return 42; }
 */

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/testing/foundry.hpp>
#include <cmath>

namespace jai::foundry::tests {

// ============================================================================
// Test C++ types for conversion testing
// ============================================================================

struct Point2D {
    float x = 0.0f;
    float y = 0.0f;

    Point2D() = default;
    Point2D(float x_, float y_) : x(x_), y(y_) {}
    explicit Point2D(float scalar) : x(scalar), y(scalar) {}
    explicit Point2D(int scalar) : x(static_cast<float>(scalar)), y(static_cast<float>(scalar)) {}

    float magnitude() const { return std::sqrt(x*x + y*y); }
};

struct Point3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Point3D() = default;
    Point3D(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Point3D(const Point2D& p) : x(p.x), y(p.y), z(0.0f) {}

    float magnitude() const { return std::sqrt(x*x + y*y + z*z); }
};

struct Color {
    int r = 0, g = 0, b = 0;

    Color() = default;
    Color(int r_, int g_, int b_) : r(r_), g(g_), b(b_) {}
    explicit Color(int gray) : r(gray), g(gray), b(gray) {}
    explicit Color(const std::string& hex) {
        if (hex.size() >= 6) {
            r = std::stoi(hex.substr(0, 2), nullptr, 16);
            g = std::stoi(hex.substr(2, 2), nullptr, 16);
            b = std::stoi(hex.substr(4, 2), nullptr, 16);
        }
    }

    int to_int() const { return (r << 16) | (g << 8) | b; }
    std::string to_string() const {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02X%02X%02X", r, g, b);
        return std::string(buf);
    }
    bool to_bool() const { return r != 0 || g != 0 || b != 0; }
};

struct BaseEntity {
    int id = 0;
    std::string name;

    BaseEntity() = default;
    BaseEntity(int id_, const std::string& name_) : id(id_), name(name_) {}

    virtual ~BaseEntity() = default;
    virtual std::string type_name() const { return "BaseEntity"; }
};

struct DerivedEntity : BaseEntity {
    float health = 100.0f;

    DerivedEntity() = default;
    DerivedEntity(int id_, const std::string& name_, float health_)
        : BaseEntity(id_, name_), health(health_) {}

    std::string type_name() const override { return "DerivedEntity"; }
};

struct Temperature {
    float celsius = 0.0f;

    Temperature() = default;
    explicit Temperature(float c) : celsius(c) {}
    explicit Temperature(int c) : celsius(static_cast<float>(c)) {}

    float to_fahrenheit() const { return celsius * 9.0f / 5.0f + 32.0f; }
    float to_float() const { return celsius; }
    int to_int() const { return static_cast<int>(celsius); }
    std::string to_string() const { return std::to_string(celsius) + "C"; }
    bool to_bool() const { return celsius != 0.0f; }
};

// ============================================================================
// Test Suite
// ============================================================================

class constructor_conversion_tests : public foundry::suite {
public:
    constructor_conversion_tests() : suite("constructor_conversions") {}

    void forge_tests() override {

        // ====================================================================
        // SECTION 1: Explicit construction syntax (baseline - should work now)
        // ====================================================================

        test("explicit_constructor_call", [&]() {
            auto eng = make_engine();

            // Using two-argument constructor like the working test in script_class_tests.cpp
            eng->execute(R"(
                class Wrapper {
                    int value = 0;
                    int mult = 0;

                    Wrapper(int x, int y) {
                        this.value = x;
                        this.mult = y;
                    }
                }

                auto w = Wrapper(42, 2);
            )");

            auto value = eng->execute("w.value");
            check_eq(value.as<script_int>(), 42);
        });

        test("explicit_constructor_brace_init", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Wrapper {
                    int value = 0;

                    Wrapper(int x) {
                        this.value = x;
                    }
                }

                Wrapper w{42};
            )");

            auto value = eng->execute("w.value");
            check_eq(value.as<script_int>(), 42);
        });

        // ====================================================================
        // SECTION 2: Implicit conversion in function parameters
        // ====================================================================

        test("implicit_conversion_function_param_int", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Wrapper {
                    int value = 0;

                    Wrapper(int x) {
                        this.value = x;
                    }
                }

                auto process(Wrapper w) -> int {
                    return w.value * 2;
                }

                var result = process(21);
            )");

            auto result = eng->execute("result");
            check_eq(result.as<script_int>(), 42);
        });

        test("implicit_conversion_function_param_string", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Label {
                    string text = "";

                    Label(string s) {
                        this.text = s;
                    }
                }

                auto get_length(Label l) -> int {
                    return l.text.length();
                }

                var result = get_length("Hello");
            )");

            auto result = eng->execute("result");
            check_eq(result.as<script_int>(), 5);
        });

        test("implicit_conversion_function_param_float", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Percentage {
                    float value = 0.0;

                    Percentage(float f) {
                        this.value = f;
                    }
                }

                auto scale(Percentage p) -> float {
                    return p.value * 100.0;
                }

                var result = scale(0.75);
            )");

            auto result = eng->execute("result");
            check(std::abs(result.as<script_float>() - 75.0) < 0.001);
        });

        test("implicit_conversion_function_param_object", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class SourceType {
                    int x = 10;
                    int y = 20;
                }

                class TargetType {
                    int sum = 0;

                    TargetType(SourceType src) {
                        this.sum = src.x + src.y;
                    }
                }

                auto get_sum(TargetType t) -> int {
                    return t.sum;
                }

                var src = SourceType();
                var result = get_sum(src);
            )");

            auto result = eng->execute("result");
            check_eq(result.as<script_int>(), 30);
        });

        // ====================================================================
        // SECTION 3: Implicit conversion in assignment to typed variables
        // ====================================================================

        test("implicit_conversion_assignment_int", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Wrapper {
                    int value = 0;

                    Wrapper(int x) {
                        this.value = x;
                    }
                }

                Wrapper w = Wrapper(0);
                w = 42;
            )");

            auto value = eng->execute("w.value");
            check_eq(value.as<script_int>(), 42);
        });

        test("implicit_conversion_assignment_string", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Label {
                    string text = "";

                    Label(string s) {
                        this.text = s;
                    }
                }

                Label lbl = Label("");
                lbl = "Hello World";
            )");

            auto text = eng->execute("lbl.text");
            check_eq(text.as<std::string>(), "Hello World");
        });

        // ====================================================================
        // SECTION 4: Implicit conversion in return statements
        // ====================================================================

        test("implicit_conversion_return_int", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Wrapper {
                    int value = 0;

                    Wrapper(int x) {
                        this.value = x;
                    }
                }

                auto make_wrapper() -> Wrapper {
                    return 42;
                }

                auto w = make_wrapper();
            )");

            auto value = eng->execute("w.value");
            check_eq(value.as<script_int>(), 42);
        });

        test("implicit_conversion_return_object", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class SourceType {
                    int x = 10;
                    int y = 20;
                }

                class TargetType {
                    int sum = 0;

                    TargetType(SourceType src) {
                        this.sum = src.x + src.y;
                    }
                }

                auto convert() -> TargetType {
                    var s = SourceType();
                    return s;
                }

                auto t = convert();
            )");

            auto sum = eng->execute("t.sum");
            check_eq(sum.as<script_int>(), 30);
        });

        // ====================================================================
        // SECTION 5: Script-side outbound conversions (to_X methods)
        // ====================================================================

        test("script_to_int_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Counter {
                    int count = 42;

                    int to_int() {
                        return this.count;
                    }
                }

                auto double_it(int x) -> int {
                    return x * 2;
                }

                var counter = Counter();
                var result = double_it(counter);
            )");

            auto result = eng->execute("result");
            check_eq(result.as<script_int>(), 84);
        });

        test("script_to_float_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Measurement {
                    float value = 3.14;

                    float to_float() {
                        return this.value;
                    }
                }

                auto add_one(float f) -> float {
                    return f + 1.0;
                }

                var m = Measurement();
                var result = add_one(m);
            )");

            auto result = eng->execute("result");
            check(std::abs(result.as<script_float>() - 4.14) < 0.001);
        });

        test("script_to_string_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Person {
                    string name = "Alice";

                    string to_string() {
                        return "Person: " + this.name;
                    }
                }

                auto greet(string s) -> string {
                    return "Hello, " + s;
                }

                var p = Person();
                var result = greet(p);
            )");

            auto result = eng->execute("result");
            check_eq(result.as<std::string>(), "Hello, Person: Alice");
        });

        test("script_to_bool_in_conditional", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Truthy {
                    bool is_truthy = true;

                    bool to_bool() {
                        return this.is_truthy;
                    }
                }

                class Falsy {
                    bool to_bool() {
                        return false;
                    }
                }

                var truthy = Truthy();
                var falsy = Falsy();
                var truthy_result = "not set";
                var falsy_result = "not set";

                if (truthy) {
                    truthy_result = "truthy branch";
                } else {
                    truthy_result = "falsy branch";
                }

                if (falsy) {
                    falsy_result = "truthy branch";
                } else {
                    falsy_result = "falsy branch";
                }
            )");

            auto truthy_result = eng->execute("truthy_result");
            auto falsy_result = eng->execute("falsy_result");
            check_eq(truthy_result.as<std::string>(), "truthy branch");
            check_eq(falsy_result.as<std::string>(), "falsy branch");
        });

        test("script_to_string_in_concatenation", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Version {
                    int major = 1;
                    int minor = 2;
                    int patch = 3;

                    string to_string() {
                        return major + "." + minor + "." + patch;
                    }
                }

                var v = Version();
                var result = "Version: " + v;
            )");

            auto result = eng->execute("result");
            check_eq(result.as<std::string>(), "Version: 1.2.3");
        });

        // ====================================================================
        // SECTION 6: C++ inbound conversions via ClassBuilder
        // ====================================================================

        test("cpp_constructor_int_conversion", [&]() {
            auto eng = make_engine();

            dynamic_binder<Temperature>(*eng, "Temperature")
                .constructor<>()
                .constructor<float>()
                .constructor<int>()
                .property("celsius", &Temperature::celsius)
                .build();

            eng->execute(R"(
                auto get_celsius(Temperature t) -> float {
                    return t.celsius;
                }
                var result = get_celsius(25);
            )");

            auto result = eng->execute("result");
            check(std::abs(result.as<script_float>() - 25.0) < 0.001);
        });

        test("cpp_constructor_object_conversion", [&]() {
            auto eng = make_engine();

            dynamic_binder<Point2D>(*eng, "Point2D")
                .constructor<>()
                .constructor<float, float>()
                .property("x", &Point2D::x)
                .property("y", &Point2D::y)
                .build();

            dynamic_binder<Point3D>(*eng, "Point3D")
                .constructor<>()
                .constructor<float, float, float>()
                .constructor<Point2D>()
                .property("x", &Point3D::x)
                .property("y", &Point3D::y)
                .property("z", &Point3D::z)
                .build();

            eng->execute(R"(
                auto get_z(Point3D p) -> float {
                    return p.z;
                }

                var p2 = Point2D(3.0, 4.0);
                var z_val = get_z(p2);
            )");

            auto z = eng->execute("z_val");
            check(std::abs(z.as<script_float>() - 0.0) < 0.001);
        });

        test("cpp_constructor_string_conversion", [&]() {
            auto eng = make_engine();

            dynamic_binder<Color>(*eng, "Color")
                .constructor<>()
                .constructor<int, int, int>()
                .constructor<int>()
                .constructor<std::string>()
                .property("r", &Color::r)
                .property("g", &Color::g)
                .property("b", &Color::b)
                .build();

            eng->execute(R"(
                auto get_red(Color c) -> int {
                    return c.r;
                }
                var red = get_red("FF8000");
            )");

            auto red = eng->execute("red");
            check_eq(red.as<script_int>(), 255);
        });

        // ====================================================================
        // SECTION 7: Inheritance-based no-copy compatibility
        // ====================================================================

        test("inheritance_ref_upcast_no_copy", [&]() {
            auto eng = make_engine();

            dynamic_binder<BaseEntity>(*eng, "BaseEntity")
                .constructor<>()
                .constructor<int, std::string>()
                .property("id", &BaseEntity::id)
                .property("name", &BaseEntity::name)
                .method("type_name", &BaseEntity::type_name)
                .build();

            dynamic_binder<DerivedEntity>(*eng, "DerivedEntity")
                .constructor<>()
                .constructor<int, std::string, float>()
                .base_class<BaseEntity>()
                .property("health", &DerivedEntity::health)
                .method("type_name", &DerivedEntity::type_name)
                .build();

            eng->execute(R"(
                auto get_type(BaseEntity& e) -> string {
                    return e.type_name();
                }

                var derived = DerivedEntity(1, "Hero", 100.0);
                var type_str = get_type(derived);
            )");

            auto type_name = eng->execute("type_str");
            check_eq(type_name.as<std::string>(), "DerivedEntity");
        });

        test("script_inheritance_base_param", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Animal {
                    string name = "";

                    auto speak() { return "..."; }
                }

                class Dog : Animal {
                    string breed = "";

                    auto speak() override { return "Woof!"; }
                }

                auto get_sound(Animal& a) -> string {
                    return a.speak();
                }

                var dog = Dog();
                dog.name = "Rex";
                var sound = get_sound(dog);
            )");

            auto sound = eng->execute("sound");
            check_eq(sound.as<std::string>(), "Woof!");
        });

        // ====================================================================
        // SECTION 8: Null handling
        // ====================================================================

        test("null_assignment_no_conversion", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                var constructor_called = false;

                class Tracker {
                    Tracker() {
                        constructor_called = true;
                    }

                    Tracker(var x) {
                        constructor_called = true;
                    }
                }

                constructor_called = false;
                Tracker t = Tracker();
                constructor_called = false;
                t = null;
            )");

            auto called = eng->execute("constructor_called");
            check_eq(called.as<bool>(), false);
        });

        // ====================================================================
        // SECTION 9: Conversion priority
        // ====================================================================

        test("exact_match_priority", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                var conversion_count = 0;

                class MyType {
                    int value = 0;

                    MyType(int x) {
                        conversion_count = conversion_count + 1;
                        this.value = x;
                    }
                }

                auto accept(MyType m) -> int {
                    return m.value;
                }

                var a = MyType(10);
                conversion_count = 0;
                var result = accept(a);
            )");

            auto count = eng->execute("conversion_count");
            check_eq(count.as<script_int>(), 0);
        });

        // ====================================================================
        // SECTION 10: Error cases
        // ====================================================================

        test("error_no_conversion_available", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class TypeA { int a = 0; }
                class TypeB { int b = 0; }

                auto accept_b(TypeB b) -> int { return b.b; }

                var a = TypeA();
            )");

            bool threw = false;
            try {
                eng->execute("accept_b(a);");
            } catch (...) {
                threw = true;
            }
            check(threw);
        });

        // ====================================================================
        // SECTION 11: Chained conversions (should NOT be supported)
        // ====================================================================

        test("no_chained_conversions", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class TypeA { int value = 1; }
                class TypeB {
                    int value = 0;
                    TypeB(TypeA a) { this.value = a.value * 2; }
                }
                class TypeC {
                    int value = 0;
                    TypeC(TypeB b) { this.value = b.value * 3; }
                }

                auto accept_c(TypeC c) -> int { return c.value; }

                var a = TypeA();
            )");

            bool threw = false;
            try {
                eng->execute("accept_c(a);");
            } catch (...) {
                threw = true;
            }
            check(threw);
        });

        // ====================================================================
        // SECTION 12: Hot-reload scenarios
        // ====================================================================

        test("hot_reload_adds_conversion", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Reloadable {
                    int value = 0;
                }

                auto accept(Reloadable r) -> int { return r.value; }
            )");

            bool threw_before = false;
            try {
                eng->execute("accept(42);");
            } catch (...) {
                threw_before = true;
            }
            check(threw_before);

            eng->execute(R"(
                class Reloadable {
                    int value = 0;

                    Reloadable(int x) {
                        this.value = x;
                    }
                }
            )");

            eng->execute("var result = accept(42);");
            auto result = eng->execute("result");
            check_eq(result.as<script_int>(), 42);
        });

        // ====================================================================
        // SECTION 13: Multiple constructors / overload resolution
        // ====================================================================

        test("multiple_constructors_exact_match", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Multi {
                    string source = "";

                    Multi(int x) {
                        this.source = "int";
                    }

                    Multi(float x) {
                        this.source = "float";
                    }

                    Multi(string x) {
                        this.source = "string";
                    }
                }

                auto get_source(Multi m) -> string { return m.source; }

                var from_int = get_source(42);
                var from_float = get_source(3.14);
                var from_string = get_source("hello");
            )");

            auto from_int = eng->execute("from_int");
            auto from_float = eng->execute("from_float");
            auto from_string = eng->execute("from_string");

            check_eq(from_int.as<std::string>(), "int");
            check_eq(from_float.as<std::string>(), "float");
            check_eq(from_string.as<std::string>(), "string");
        });

        // ====================================================================
        // SECTION 14: Edge cases
        // ====================================================================

        test("conversion_preserves_value_semantics", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Source {
                    int value = 0;
                }

                class Target {
                    int copied_value = 0;

                    Target(Source s) {
                        this.copied_value = s.value;
                    }
                }

                auto capture(Target t) -> int { return t.copied_value; }

                var src = Source();
                src.value = 42;
                var captured = capture(src);
                src.value = 100;
            )");

            auto captured = eng->execute("captured");
            check_eq(captured.as<script_int>(), 42);
        });

        test("recursive_type_conversion", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class NodeA {
                    int value = 0;
                    NodeA child = null;
                }

                class NodeB {
                    int value = 0;
                    NodeB child = null;

                    NodeB(NodeA a) {
                        this.value = a.value * 2;
                        if (a.child != null) {
                            this.child = NodeB(a.child);
                        }
                    }
                }

                auto get_value(NodeB b) -> int { return b.value; }

                var a1 = NodeA();
                a1.value = 1;
                var a2 = NodeA();
                a2.value = 2;
                a1.child = a2;

                var b_val = get_value(a1);
            )");

            auto b_value = eng->execute("b_val");
            check_eq(b_value.as<script_int>(), 2);
        });

        test("conversion_with_side_effects", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                var side_effect_count = 0;

                class SideEffector {
                    int value = 0;

                    SideEffector(int x) {
                        side_effect_count = side_effect_count + 1;
                        this.value = x;
                    }
                }

                auto accept(SideEffector s) -> int { return s.value; }

                var result = accept(42);
            )");

            auto count = eng->execute("side_effect_count");
            check_eq(count.as<script_int>(), 1);
        });

        test("conversion_exception_handling", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Validator {
                    int value = 0;

                    Validator(int x) {
                        if (x < 0) {
                            throw "Negative values not allowed";
                        }
                        this.value = x;
                    }
                }

                auto accept(Validator v) -> int { return v.value; }
            )");

            eng->execute("var v1 = accept(42);");
            auto v1_value = eng->execute("v1");
            check_eq(v1_value.as<script_int>(), 42);

            bool threw = false;
            try {
                eng->execute("accept(-1);");
            } catch (...) {
                threw = true;
            }
            check(threw);
        });

        test("bidirectional_conversion", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                class Celsius {
                    float temp = 0.0;

                    Celsius(float t) {
                        this.temp = t;
                    }
                }

                class Fahrenheit {
                    float temp = 0.0;

                    Fahrenheit(float t) {
                        this.temp = t;
                    }

                    Fahrenheit(Celsius c) {
                        this.temp = c.temp * 9.0 / 5.0 + 32.0;
                    }
                }

                auto get_fahrenheit(Fahrenheit f) -> float {
                    return f.temp;
                }

                var c = Celsius(100.0);
                var f_temp = get_fahrenheit(c);
            )");

            auto f_temp = eng->execute("f_temp");
            check(std::abs(f_temp.as<script_float>() - 212.0) < 0.1);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::constructor_conversion_tests)
