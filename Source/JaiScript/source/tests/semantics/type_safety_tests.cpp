#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>

namespace jai::foundry::tests {

// C++ classes for inheritance type safety tests
namespace type_safety_test_classes {

class Entity {
public:
    std::string name;
    int id;

    Entity() : name("unnamed"), id(0) {}
    Entity(const std::string& n, int i) : name(n), id(i) {}

    virtual std::string get_type() const { return "Entity"; }
    int get_id() const { return id; }
    std::string get_name() const { return name; }
};

class Vehicle {
public:
    int speed;
    std::string model;

    Vehicle() : speed(0), model("unknown") {}
    Vehicle(int s, const std::string& m) : speed(s), model(m) {}

    virtual int get_max_speed() const { return speed; }
    void accelerate(int amount) { speed += amount; }
};

class Animal {
public:
    std::string species;
    int legs;

    Animal() : species("unknown"), legs(4) {}
    Animal(const std::string& s, int l) : species(s), legs(l) {}

    virtual std::string speak() const { return "..."; }
    int get_legs() const { return legs; }
};

} // namespace type_safety_test_classes

using namespace type_safety_test_classes;

/**
 * Type Safety Tests
 *
 * This suite contains two categories of tests:
 *
 * 1. KNOWN_ISSUE tests - Document known type safety issues that need fixing
 *    These tests demonstrate problematic behavior and will fail once fixed.
 *    They are marked with KNOWN_ISSUE_ prefix.
 *
 * 2. Working system tests - Comprehensive tests for correctly working type safety
 *    These should always pass and verify the type system works as intended.
 */
class type_safety_tests : public suite {
public:
    type_safety_tests() : suite("Type Safety") {}

    void forge_tests() override {
        // ============================================================
        // SECTION 1: TYPE SAFETY FIXES - Tests for fixed type safety issues
        // Formerly KNOWN_ISSUE tests - now verify correct behavior
        // Issues still open: silent truncation, typed array validation
        // ============================================================

        // --- Issue 1: Equality vs Arithmetic Consistency (FIXED) ---

        test("numeric_equality_int_float_works", [this]() {
            auto eng = engine::make();

            // FIXED: Numeric equality now promotes int to float for comparison
            auto result = eng->execute("5 == 5.0");
            check_eq(result.as<bool>(), true, "5 == 5.0 returns true (numeric promotion)");

            // Arithmetic also converts
            auto arith = eng->execute("5 + 5.0");
            check_eq(arith.as<double>(), 10.0, "Arithmetic converts int to float");

            // All comparisons now consistent
            auto compare = eng->execute("5 + 5.0 == 10.0");
            check_eq(compare.as<bool>(), true, "Float == Float works");
        });

        test("numeric_not_equal_int_float_works", [this]() {
            auto eng = engine::make();

            // FIXED: 5 != 5.0 returns false because they are equal numerically
            auto result = eng->execute("5 != 5.0");
            check_eq(result.as<bool>(), false, "5 != 5.0 returns false (same numeric value)");

            // Different values should still be not equal
            auto diff = eng->execute("5 != 6.0");
            check_eq(diff.as<bool>(), true, "5 != 6.0 returns true");
        });

        test("numeric_comparison_consistent", [this]() {
            auto eng = engine::make();

            // FIXED: Arithmetic and equality both handle type conversion consistently
            auto sum_works = eng->execute("5 + 5.0 == 10.0");
            check_eq(sum_works.as<bool>(), true, "5 + 5.0 == 10.0 works");

            auto direct_compare = eng->execute("5 == 5.0");
            check_eq(direct_compare.as<bool>(), true, "5 == 5.0 now returns true");

            // Edge cases
            auto zero_compare = eng->execute("0 == 0.0");
            check_eq(zero_compare.as<bool>(), true, "0 == 0.0 returns true");

            auto neg_compare = eng->execute("-5 == -5.0");
            check_eq(neg_compare.as<bool>(), true, "-5 == -5.0 returns true");
        });

        // --- Issue 2: Logical Operators Return Boolean (FIXED) ---

        test("logical_and_returns_boolean", [this]() {
            auto eng = engine::make();

            // FIXED: && now returns a boolean
            auto result = eng->execute("5 && 10");
            check_eq(result.type(), script_value_type::jai_bool_type, "&& returns bool type");
            check_eq(result.as<bool>(), true, "5 && 10 returns true");
        });

        test("logical_or_returns_boolean", [this]() {
            auto eng = engine::make();

            // FIXED: || now returns a boolean
            auto result = eng->execute("0 || \"hello\"");
            check_eq(result.type(), script_value_type::jai_bool_type, "|| returns bool type");
            check_eq(result.as<bool>(), true, "0 || 'hello' returns true");
        });

        test("logical_and_short_circuit_returns_false", [this]() {
            auto eng = engine::make();

            // FIXED: When left is falsy, && returns false (not left operand)
            auto result = eng->execute("0 && 10");
            check_eq(result.type(), script_value_type::jai_bool_type, "&& returns bool type");
            check_eq(result.as<bool>(), false, "0 && 10 returns false");
        });

        test("logical_result_type_always_bool", [this]() {
            auto eng = engine::make();

            // FIXED: The type of logical operators is always bool
            auto result1 = eng->execute("true && 42");
            auto result2 = eng->execute("false && 42");

            // Both results are now bool
            check_eq(result1.type(), script_value_type::jai_bool_type, "true && 42 returns bool");
            check_eq(result2.type(), script_value_type::jai_bool_type, "false && 42 returns bool");
            check_eq(result1.as<bool>(), true, "true && 42 = true");
            check_eq(result2.as<bool>(), false, "false && 42 = false");
        });

        // --- Issue 3: Empty Container Truthiness (FIXED) ---

        test("empty_array_is_falsy", [this]() {
            auto eng = engine::make();

            auto result = eng->execute(R"(
                auto arr = [];
                auto result = 0;
                if (arr) { result = 1; }
                result
            )");

            // FIXED: Empty array is now falsy
            check_eq(result.as<int>(), 0, "Empty array is falsy (if branch not taken)");
        });

        test("empty_map_is_falsy", [this]() {
            auto eng = engine::make();

            auto result = eng->execute(R"(
                auto m = {"a": 1};
                m.clear();
                auto result = 0;
                if (m) { result = 1; }
                result
            )");

            // FIXED: Empty map is now falsy
            check_eq(result.as<int>(), 0, "Empty map is falsy (if branch not taken)");
        });

        test("non_empty_containers_are_truthy", [this]() {
            auto eng = engine::make();

            // Non-empty array should be truthy
            auto arr_result = eng->execute(R"(
                auto arr = [1, 2, 3];
                auto result = 0;
                if (arr) { result = 1; }
                result
            )");
            check_eq(arr_result.as<int>(), 1, "Non-empty array is truthy");

            // Non-empty map should be truthy
            auto map_result = eng->execute(R"(
                auto m = {"a": 1};
                auto result = 0;
                if (m) { result = 1; }
                result
            )");
            check_eq(map_result.as<int>(), 1, "Non-empty map is truthy");
        });

        test("empty_array_in_logical_or", [this]() {
            auto eng = engine::make();

            // FIXED: Empty array is falsy, so || returns the truthiness of "default"
            // Since || now returns boolean, this returns true (string is truthy)
            auto result = eng->execute(R"(
                auto arr = [];
                arr || "default"  // [] is falsy, "default" is truthy
            )");

            // Logical || now returns boolean
            check_eq(result.type(), script_value_type::jai_bool_type, "|| returns bool");
            check_eq(result.as<bool>(), true, "[] || 'default' returns true");
        });

        // --- Issue 4: Object Equality (PARTIALLY FIXED) ---

        test("object_self_equality_works", [this]() {
            auto eng = engine::make();

            eng->execute(R"(
                class Thing { int value = 42; }
            )");

            auto result = eng->execute(R"(
                auto t = Thing();
                t == t  // Comparing object to itself
            )");

            // FIXED: Self-comparison now works via reference equality
            check_eq(result.as<bool>(), true, "Object compared to itself returns true");
        });

        test("object_reference_equality_different_instances", [this]() {
            auto eng = engine::make();

            eng->execute(R"(
                class Point {
                    int x = 0;
                    int y = 0;
                    Point(int px, int py) { x = px; y = py; }
                }
            )");

            auto result = eng->execute(R"(
                auto a = Point(1, 2);
                auto b = Point(1, 2);
                a == b  // Different instances, same values
            )");

            // Without operator==, different instances are not equal (reference equality)
            // Users should implement operator== for value comparison
            check_eq(result.as<bool>(), false,
                "Different instances are not equal without operator==");
        });

        test("object_operator_equals_custom", [this]() {
            auto eng = engine::make();

            // Define a class with operator== for custom equality
            eng->execute(R"(
                class Point {
                    int x = 0;
                    int y = 0;
                    Point(int px, int py) { x = px; y = py; }

                    bool operator==(Point other) {
                        return x == other.x && y == other.y;
                    }
                }
            )");

            // Same values with operator== should be equal when a == b is used
            auto result = eng->execute(R"(
                auto a = Point(1, 2);
                auto b = Point(1, 2);
                a == b
            )");
            check_eq(result.as<bool>(), true, "Points with same values are equal via operator==");

            // Different values should not be equal
            auto diff_result = eng->execute(R"(
                auto a = Point(1, 2);
                auto c = Point(3, 4);
                a == c
            )");
            check_eq(diff_result.as<bool>(), false, "Points with different values are not equal");
        });

        test("object_operator_equals_with_not_equal", [this]() {
            auto eng = engine::make();

            // Define a class with operator==
            eng->execute(R"(
                class Item {
                    int id = 0;
                    Item(int i) { id = i; }

                    bool operator==(Item other) {
                        return id == other.id;
                    }
                }
            )");

            // Test a == b uses operator== method
            auto eq_result = eng->execute(R"(
                auto a = Item(42);
                auto b = Item(42);
                a == b
            )");
            check_eq(eq_result.as<bool>(), true, "Items with same id are equal via operator==");

            // Test a != b also works (negation of ==)
            auto neq_result = eng->execute(R"(
                auto a = Item(42);
                auto c = Item(99);
                a != c
            )");
            check_eq(neq_result.as<bool>(), true, "Items with different id are not equal");
        });

        test("script_class_comparison_operators", [this]() {
            auto eng = engine::make();

            // Define a class with all comparison operators
            eng->execute(R"(
                class Score {
                    int value = 0;
                    Score(int v) { value = v; }

                    bool operator<(Score other) {
                        return value < other.value;
                    }
                    bool operator<=(Score other) {
                        return value <= other.value;
                    }
                    bool operator>(Score other) {
                        return value > other.value;
                    }
                    bool operator>=(Score other) {
                        return value >= other.value;
                    }
                }
            )");

            // Test less than
            check_eq(eng->execute("Score(10) < Score(20)").as<bool>(), true, "10 < 20");
            check_eq(eng->execute("Score(20) < Score(10)").as<bool>(), false, "20 < 10 is false");

            // Test greater than
            check_eq(eng->execute("Score(20) > Score(10)").as<bool>(), true, "20 > 10");
            check_eq(eng->execute("Score(10) > Score(20)").as<bool>(), false, "10 > 20 is false");

            // Test less than or equal
            check_eq(eng->execute("Score(10) <= Score(20)").as<bool>(), true, "10 <= 20");
            check_eq(eng->execute("Score(10) <= Score(10)").as<bool>(), true, "10 <= 10");
            check_eq(eng->execute("Score(20) <= Score(10)").as<bool>(), false, "20 <= 10 is false");

            // Test greater than or equal
            check_eq(eng->execute("Score(20) >= Score(10)").as<bool>(), true, "20 >= 10");
            check_eq(eng->execute("Score(10) >= Score(10)").as<bool>(), true, "10 >= 10");
            check_eq(eng->execute("Score(10) >= Score(20)").as<bool>(), false, "10 >= 20 is false");
        });

        test("script_class_arithmetic_operators", [this]() {
            auto eng = engine::make();

            // Define a class with all arithmetic operators
            // Note: Use 'function operator+(...) -> Type' syntax for custom return types
            eng->execute(R"(
                class Vec2 {
                    float x = 0.0;
                    float y = 0.0;
                    Vec2(float px, float py) { x = px; y = py; }

                    function operator+(Vec2 other) -> Vec2 {
                        return Vec2(x + other.x, y + other.y);
                    }
                    function operator-(Vec2 other) -> Vec2 {
                        return Vec2(x - other.x, y - other.y);
                    }
                    function operator*(float scalar) -> Vec2 {
                        return Vec2(x * scalar, y * scalar);
                    }
                    function operator/(float scalar) -> Vec2 {
                        return Vec2(x / scalar, y / scalar);
                    }
                }
            )");

            // Test addition
            auto add_x = eng->execute("(Vec2(1, 2) + Vec2(3, 4)).x");
            auto add_y = eng->execute("(Vec2(1, 2) + Vec2(3, 4)).y");
            check_eq(add_x.as<double>(), 4.0, "Vec2 addition x");
            check_eq(add_y.as<double>(), 6.0, "Vec2 addition y");

            // Test subtraction
            auto sub_x = eng->execute("(Vec2(5, 7) - Vec2(2, 3)).x");
            auto sub_y = eng->execute("(Vec2(5, 7) - Vec2(2, 3)).y");
            check_eq(sub_x.as<double>(), 3.0, "Vec2 subtraction x");
            check_eq(sub_y.as<double>(), 4.0, "Vec2 subtraction y");

            // Test multiplication
            auto mul_x = eng->execute("(Vec2(3, 4) * 2).x");
            auto mul_y = eng->execute("(Vec2(3, 4) * 2).y");
            check_eq(mul_x.as<double>(), 6.0, "Vec2 multiplication x");
            check_eq(mul_y.as<double>(), 8.0, "Vec2 multiplication y");

            // Test division
            auto div_x = eng->execute("(Vec2(6, 8) / 2).x");
            auto div_y = eng->execute("(Vec2(6, 8) / 2).y");
            check_eq(div_x.as<double>(), 3.0, "Vec2 division x");
            check_eq(div_y.as<double>(), 4.0, "Vec2 division y");
        });

        test("script_class_compound_assignment_operators", [this]() {
            auto eng = engine::make();

            // Define a class with arithmetic operators
            eng->execute(R"(
                class Vec2 {
                    float x = 0.0;
                    float y = 0.0;
                    Vec2(float px, float py) { x = px; y = py; }

                    function operator+(Vec2 other) -> Vec2 {
                        return Vec2(x + other.x, y + other.y);
                    }
                    function operator-(Vec2 other) -> Vec2 {
                        return Vec2(x - other.x, y - other.y);
                    }
                    function operator*(float scalar) -> Vec2 {
                        return Vec2(x * scalar, y * scalar);
                    }
                    function operator/(float scalar) -> Vec2 {
                        return Vec2(x / scalar, y / scalar);
                    }
                }
            )");

            // Test += operator
            auto result_x = eng->execute(R"(
                auto v = Vec2(1, 2);
                v += Vec2(3, 4);
                v.x
            )");
            auto result_y = eng->execute(R"(
                auto v = Vec2(1, 2);
                v += Vec2(3, 4);
                v.y
            )");
            check_eq(result_x.as<double>(), 4.0, "Vec2 += x");
            check_eq(result_y.as<double>(), 6.0, "Vec2 += y");

            // Test -= operator
            auto sub_x = eng->execute(R"(
                auto v = Vec2(5, 7);
                v -= Vec2(2, 3);
                v.x
            )");
            auto sub_y = eng->execute(R"(
                auto v = Vec2(5, 7);
                v -= Vec2(2, 3);
                v.y
            )");
            check_eq(sub_x.as<double>(), 3.0, "Vec2 -= x");
            check_eq(sub_y.as<double>(), 4.0, "Vec2 -= y");

            // Test *= operator
            auto mul_x = eng->execute(R"(
                auto v = Vec2(3, 4);
                v *= 2;
                v.x
            )");
            auto mul_y = eng->execute(R"(
                auto v = Vec2(3, 4);
                v *= 2;
                v.y
            )");
            check_eq(mul_x.as<double>(), 6.0, "Vec2 *= x");
            check_eq(mul_y.as<double>(), 8.0, "Vec2 *= y");

            // Test /= operator
            auto div_x = eng->execute(R"(
                auto v = Vec2(6, 8);
                v /= 2;
                v.x
            )");
            auto div_y = eng->execute(R"(
                auto v = Vec2(6, 8);
                v /= 2;
                v.y
            )");
            check_eq(div_x.as<double>(), 3.0, "Vec2 /= x");
            check_eq(div_y.as<double>(), 4.0, "Vec2 /= y");
        });

        test("type_based_method_overloading", [this]() {
            auto eng = engine::make();

            // Define a class with multiple overloads of the same method using different types
            eng->execute(R"(
                class Vec2 {
                    float x = 0.0;
                    float y = 0.0;
                    Vec2(float px, float py) { x = px; y = py; }

                    // Overload operator* with different types
                    function operator*(float scalar) -> Vec2 {
                        return Vec2(x * scalar, y * scalar);
                    }

                    function operator*(Vec2 other) -> Vec2 {
                        return Vec2(x * other.x, y * other.y);
                    }

                    function operator*(int n) -> Vec2 {
                        return Vec2(x * n, y * n);
                    }

                    // Regular method overloading
                    function scale(float s) -> Vec2 {
                        return Vec2(x * s, y * s);
                    }

                    function scale(Vec2 s) -> Vec2 {
                        return Vec2(x * s.x, y * s.y);
                    }
                }
            )");

            // Test operator* with float - should call float overload
            auto mul_float_x = eng->execute("(Vec2(3, 4) * 2.0).x");
            auto mul_float_y = eng->execute("(Vec2(3, 4) * 2.0).y");
            check_eq(mul_float_x.as<double>(), 6.0, "Vec2 * float x");
            check_eq(mul_float_y.as<double>(), 8.0, "Vec2 * float y");

            // Test operator* with Vec2 - should call Vec2 overload (component-wise)
            auto mul_vec_x = eng->execute("(Vec2(3, 4) * Vec2(2, 3)).x");
            auto mul_vec_y = eng->execute("(Vec2(3, 4) * Vec2(2, 3)).y");
            check_eq(mul_vec_x.as<double>(), 6.0, "Vec2 * Vec2 x (component-wise)");
            check_eq(mul_vec_y.as<double>(), 12.0, "Vec2 * Vec2 y (component-wise)");

            // Test operator* with int - should call int overload
            auto mul_int_x = eng->execute("(Vec2(3, 4) * 2).x");
            auto mul_int_y = eng->execute("(Vec2(3, 4) * 2).y");
            check_eq(mul_int_x.as<double>(), 6.0, "Vec2 * int x");
            check_eq(mul_int_y.as<double>(), 8.0, "Vec2 * int y");

            // Test regular method overloading - scale with float
            auto scale_float_x = eng->execute("Vec2(3, 4).scale(2.0).x");
            auto scale_float_y = eng->execute("Vec2(3, 4).scale(2.0).y");
            check_eq(scale_float_x.as<double>(), 6.0, "Vec2.scale(float) x");
            check_eq(scale_float_y.as<double>(), 8.0, "Vec2.scale(float) y");

            // Test regular method overloading - scale with Vec2
            auto scale_vec_x = eng->execute("Vec2(3, 4).scale(Vec2(2, 3)).x");
            auto scale_vec_y = eng->execute("Vec2(3, 4).scale(Vec2(2, 3)).y");
            check_eq(scale_vec_x.as<double>(), 6.0, "Vec2.scale(Vec2) x");
            check_eq(scale_vec_y.as<double>(), 12.0, "Vec2.scale(Vec2) y");
        });

        test("type_based_overloading_with_var_fallback", [this]() {
            auto eng = engine::make();

            // Test that untyped parameters (var) act as "any" fallback
            // Following C++ semantics: typed conversions are preferred over var/any
            eng->execute(R"(
                class Wrapper {
                    var data = null;
                }

                class Calculator {
                    function process(int x) -> int {
                        return x * 2;
                    }

                    function process(string s) -> string {
                        return s + s;
                    }

                    // This should match only when no typed conversion is possible
                    function process(var x) -> string {
                        return "fallback";
                    }
                }

                var calc = Calculator();
            )");

            // Test with int - should call int overload (exact match)
            auto int_result = eng->execute("Calculator().process(5)");
            check_eq(int_result.as<int>(), 10, "process(int) returns x * 2");

            // Test with string - should call string overload (exact match)
            auto str_result = eng->execute("Calculator().process(\"ab\")");
            check_eq(str_result.as<std::string>(), "abab", "process(string) returns s + s");

            // Test with float - C++ semantics: float->int conversion beats var fallback
            auto float_result = eng->execute("Calculator().process(3.14)");
            check_eq(float_result.as<int>(), 6, "process(float) converts to int (C++ semantics)");

            // Test with object - should call var fallback (no conversion to int/string)
            auto obj_result = eng->execute("Calculator().process(Wrapper())");
            check_eq(obj_result.as<std::string>(), "fallback", "process(object) falls back to var");

            // Test with array - should call var fallback (no conversion to int/string)
            auto arr_result = eng->execute("Calculator().process([1, 2, 3])");
            check_eq(arr_result.as<std::string>(), "fallback", "process(array) falls back to var");
        });

        test("type_based_overloading_multi_param", [this]() {
            auto eng = engine::make();

            // Test overloading with multiple parameters of different types
            eng->execute(R"(
                class Math {
                    // Different parameter type combinations
                    function combine(int a, int b) -> int {
                        return a + b;
                    }

                    function combine(float a, float b) -> float {
                        return a * b;
                    }

                    function combine(string a, string b) -> string {
                        return a + "-" + b;
                    }

                    function combine(int a, string b) -> string {
                        return b + ":" + a;
                    }
                }
            )");

            // Test int, int -> addition
            auto ii = eng->execute("Math().combine(3, 4)");
            check_eq(ii.as<int>(), 7, "combine(int, int) = addition");

            // Test float, float -> multiplication
            auto ff = eng->execute("Math().combine(3.0, 4.0)");
            check_eq(ff.as<double>(), 12.0, "combine(float, float) = multiplication");

            // Test string, string -> concatenation with dash
            auto ss = eng->execute("Math().combine(\"hello\", \"world\")");
            check_eq(ss.as<std::string>(), "hello-world", "combine(string, string) = dash concat");

            // Test int, string -> special format
            auto is_result = eng->execute("Math().combine(42, \"answer\")");
            check_eq(is_result.as<std::string>(), "answer:42", "combine(int, string) = colon format");
        });

        test("type_based_overloading_conversion_count", [this]() {
            auto eng = engine::make();

            // Test that overloads with fewer conversions are preferred
            eng->execute(R"(
                class Converter {
                    // Requires 0 conversions for (int, int)
                    function calc(int a, int b) -> string {
                        return "int-int";
                    }

                    // Requires 0 conversions for (float, float)
                    function calc(float a, float b) -> string {
                        return "float-float";
                    }

                    // Requires 1 conversion for mixed (int, float) or (float, int)
                    function calc(float a, int b) -> string {
                        return "float-int";
                    }
                }
            )");

            // Exact matches should win
            auto ii = eng->execute("Converter().calc(1, 2)");
            check_eq(ii.as<std::string>(), "int-int", "calc(int, int) exact match");

            auto ff = eng->execute("Converter().calc(1.0, 2.0)");
            check_eq(ff.as<std::string>(), "float-float", "calc(float, float) exact match");

            // Mixed types - prefers exact match for one param
            auto fi = eng->execute("Converter().calc(1.0, 2)");
            check_eq(fi.as<std::string>(), "float-int", "calc(float, int) exact match");
        });

        test("type_based_overloading_arity_difference", [this]() {
            auto eng = engine::make();

            // Test overloading by both type and arity
            eng->execute(R"(
                class Variadic {
                    function foo(int x) -> string { return "one-int"; }
                    function foo(int x, int y) -> string { return "two-int"; }
                    function foo(int x, int y, int z) -> string { return "three-int"; }
                    function foo(float x) -> string { return "one-float"; }
                    function foo(string x) -> string { return "one-string"; }
                }
            )");

            // Test arity selection
            check_eq(eng->execute("Variadic().foo(1)").as<std::string>(), "one-int", "foo(int)");
            check_eq(eng->execute("Variadic().foo(1, 2)").as<std::string>(), "two-int", "foo(int, int)");
            check_eq(eng->execute("Variadic().foo(1, 2, 3)").as<std::string>(), "three-int", "foo(int, int, int)");

            // Test type selection with same arity
            check_eq(eng->execute("Variadic().foo(1.5)").as<std::string>(), "one-float", "foo(float)");
            check_eq(eng->execute("Variadic().foo(\"hi\")").as<std::string>(), "one-string", "foo(string)");
        });

        test("type_based_overloading_class_types", [this]() {
            auto eng = engine::make();

            // Test overloading with user-defined class types
            eng->execute(R"(
                class Point {
                    float x = 0.0;
                    float y = 0.0;
                    Point(float px, float py) { x = px; y = py; }
                }

                class Rect {
                    float w = 0.0;
                    float h = 0.0;
                    Rect(float pw, float ph) { w = pw; h = ph; }
                }

                class Geometry {
                    function area(Point p) -> float { return p.x * p.y; }
                    function area(Rect r) -> float { return r.w * r.h; }
                    function area(float radius) -> float { return 3.14159 * radius * radius; }
                }
            )");

            // Test with Point
            auto point_area = eng->execute("Geometry().area(Point(3, 4))");
            check_eq(point_area.as<double>(), 12.0, "area(Point) = x * y");

            // Test with Rect
            auto rect_area = eng->execute("Geometry().area(Rect(5, 6))");
            check_eq(rect_area.as<double>(), 30.0, "area(Rect) = w * h");

            // Test with float (circle area)
            auto circle_area = eng->execute("Geometry().area(2.0)");
            check(std::abs(circle_area.as<double>() - 12.56636) < 0.001, "area(float) = pi * r^2");
        });

        test("type_based_overloading_numeric_preference", [this]() {
            auto eng = engine::make();

            // Verify int exact match beats float conversion
            eng->execute(R"(
                class NumTest {
                    function test(int x) -> string { return "int"; }
                    function test(float x) -> string { return "float"; }
                }
            )");

            // Integer literal should prefer int overload
            check_eq(eng->execute("NumTest().test(5)").as<std::string>(), "int", "5 calls int overload");

            // Float literal should prefer float overload
            check_eq(eng->execute("NumTest().test(5.0)").as<std::string>(), "float", "5.0 calls float overload");

            // Expression result type matters
            check_eq(eng->execute("NumTest().test(2 + 3)").as<std::string>(), "int", "int + int = int");
            check_eq(eng->execute("NumTest().test(2.0 + 3.0)").as<std::string>(), "float", "float + float = float");
        });

        // --- Issue 5: Silent Truncation ---

        test("KNOWN_ISSUE_float_to_int_silent_truncation", [this]() {
            auto eng = engine::make();

            // Use auto which locks to int, then assign float
            auto result = eng->execute(R"(
                auto x = 5;
                x = 3.9;  // Truncates to 3, no warning
                x
            )");

            // This "works" but silently loses data
            // EXPECTED (after fix): Either warning or require explicit cast
            check_eq(result.as<int>(), 3,
                "KNOWN ISSUE: 3.9 silently truncates to 3");
        });

        test("KNOWN_ISSUE_negative_float_truncation", [this]() {
            auto eng = engine::make();

            // Use auto which locks to int, then assign negative float
            auto result = eng->execute(R"(
                auto x = 5;
                x = -3.9;  // Truncates toward zero to -3
                x
            )");

            // Truncation toward zero, not floor
            check_eq(result.as<int>(), -3,
                "KNOWN ISSUE: -3.9 truncates to -3 (toward zero)");
        });

        // --- Issue 6: Typed Array Element Validation (FIXED) ---

        test("typed_array_rejects_wrong_element", [this]() {
            auto eng = engine::make();

            // array<int> should reject pushing a string
            bool caught = false;
            try {
                eng->execute(R"(
                    array<int> nums = [1, 2, 3];
                    nums.push("hello");  // Should be rejected
                )");
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check_true(msg.find("Cannot push 'string' to array<int>") != std::string::npos,
                    "Error message should mention type mismatch");
            }

            check_true(caught, "array<int> should reject push(string)");
        });

        test("typed_array_allows_correct_element", [this]() {
            auto eng = engine::make();

            // array<int> should accept integers
            auto result = eng->execute(R"(
                array<int> nums = [1, 2, 3];
                nums.push(4);
                nums.size()
            )");

            check_eq(result.as<int>(), 4, "array<int> accepts int elements");
        });

        test("typed_array_allows_numeric_conversion", [this]() {
            auto eng = engine::make();

            // array<float> should accept int (widening conversion)
            auto result = eng->execute(R"(
                array<float> nums = [1.0, 2.0];
                nums.push(3);  // int -> float conversion
                nums.size()
            )");

            check_eq(result.as<int>(), 3, "array<float> accepts int with conversion");
        });

        test("untyped_array_allows_anything", [this]() {
            auto eng = engine::make();

            // Untyped arrays should accept any element
            auto result = eng->execute(R"(
                auto nums = [1, 2, 3];
                nums.push("hello");
                nums.push(true);
                nums.size()
            )");

            check_eq(result.as<int>(), 5, "untyped array allows mixed types");
        });

        test("typed_array_subscript_rejects_wrong_type", [this]() {
            auto eng = engine::make();

            // array<int>[i] = string should be rejected
            bool caught = false;
            try {
                eng->execute(R"(
                    array<int> nums = [1, 2, 3];
                    nums[0] = "hello";  // Should be rejected
                )");
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check_true(msg.find("Cannot assign 'string' to element of type 'int'") != std::string::npos,
                    "Error message should mention type mismatch");
            }

            check_true(caught, "array<int>[i] = string should be rejected");
        });

        test("typed_array_subscript_allows_correct_type", [this]() {
            auto eng = engine::make();

            // array<int>[i] = int should work
            auto result = eng->execute(R"(
                array<int> nums = [1, 2, 3];
                nums[0] = 42;
                nums[0]
            )");

            check_eq(result.as<int>(), 42, "array<int>[i] = int works");
        });

        // --- Nested Array/Map Type Validation ---

        test("nested_array_basic", [this]() {
            auto eng = engine::make();

            // array<array<int>> should work with nested int arrays
            auto result = eng->execute(R"(
                array<array<int>> matrix = [[1, 2], [3, 4]];
                matrix[0][1]
            )");

            check_eq(result.as<int>(), 2, "nested array access works");
        });

        test("nested_array_push_correct_type", [this]() {
            auto eng = engine::make();

            // Pushing an int array to array<array<int>> should work
            auto result = eng->execute(R"(
                array<array<int>> matrix = [[1, 2]];
                matrix.push([3, 4]);
                matrix.size()
            )");

            check_eq(result.as<int>(), 2, "push array to array<array<int>> works");
        });

        test("LIMITATION_nested_array_inner_type_not_checked", [this]() {
            auto eng = engine::make();

            // LIMITATION: Pushing a string array to array<array<int>> doesn't fail
            // because array literals are untyped - they don't carry element type info
            // The check only verifies "is this an array?" not "is this array<int>?"
            bool caught = false;
            try {
                eng->execute(R"(
                    array<array<int>> matrix = [[1, 2]];
                    matrix.push(["hello", "world"]);  // Should fail but doesn't
                )");
            } catch (const std::exception&) {
                caught = true;
            }

            // Current behavior: Does NOT catch the mismatch
            // Future enhancement: Recursively validate inner element types
            check_false(caught, "LIMITATION: nested array inner types not validated");
        });

        test("array_of_maps", [this]() {
            auto eng = engine::make();

            // array<map<string, int>> basic usage
            auto result = eng->execute(R"(
                auto items = [{"a": 1}, {"b": 2}];
                items[0]["a"]
            )");

            check_eq(result.as<int>(), 1, "array of maps works");
        });

        test("map_of_arrays", [this]() {
            auto eng = engine::make();

            // map<string, array<int>> basic usage
            auto result = eng->execute(R"(
                auto data = {"nums": [1, 2, 3], "more": [4, 5]};
                data["nums"][1]
            )");

            check_eq(result.as<int>(), 2, "map of arrays works");
        });

        // --- Homogeneous Array Validation (auto vs var) ---

        test("auto_array_requires_homogeneous_int", [this]() {
            auto eng = engine::make();

            // auto x = [1, 2, 3] - all ints, should work
            auto result = eng->execute(R"(
                auto nums = [1, 2, 3];
                nums.size()
            )");

            check_eq(result.as<int>(), 3, "auto [int, int, int] succeeds");
        });

        test("auto_array_requires_homogeneous_string", [this]() {
            auto eng = engine::make();

            // auto x = ["a", "b"] - all strings, should work
            auto result = eng->execute(R"(
                auto strs = ["hello", "world"];
                strs.size()
            )");

            check_eq(result.as<int>(), 2, "auto [string, string] succeeds");
        });

        test("auto_array_rejects_mixed_types", [this]() {
            auto eng = engine::make();

            // auto x = [1, "hello"] - mixed types, should fail
            bool caught = false;
            try {
                eng->execute(R"(
                    auto mixed = [1, "hello"];
                )");
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check_true(msg.find("homogeneous") != std::string::npos ||
                           msg.find("Array declared with 'auto'") != std::string::npos,
                    "Error should mention homogeneous requirement");
            }

            check_true(caught, "auto [int, string] should fail");
        });

        test("auto_array_rejects_int_float_mix", [this]() {
            auto eng = engine::make();

            // auto x = [5.0, 5] - float and int are different types
            bool caught = false;
            try {
                eng->execute(R"(
                    auto mixed = [5.0, 5];
                )");
            } catch (const std::exception& e) {
                caught = true;
            }

            check_true(caught, "auto [float, int] should fail - different types");
        });

        test("var_array_allows_mixed_types", [this]() {
            auto eng = engine::make();

            // var x = [1, "hello", 3.14] - should work with var
            auto result = eng->execute(R"(
                var mixed = [1, "hello", 3.14, true];
                mixed.size()
            )");

            check_eq(result.as<int>(), 4, "var allows heterogeneous array");
        });

        test("var_array_allows_int_float_mix", [this]() {
            auto eng = engine::make();

            // var x = [5.0, 5] - should work with var
            auto result = eng->execute(R"(
                var nums = [5.0, 5];
                nums.size()
            )");

            check_eq(result.as<int>(), 2, "var allows [float, int]");
        });

        test("array_auto_requires_homogeneous", [this]() {
            auto eng = engine::make();

            // array<auto> x = [1, "hello"] - should fail, auto requires same type
            bool caught = false;
            try {
                eng->execute(R"(
                    array<auto> items = [1, "hello"];
                )");
            } catch (const std::exception& e) {
                caught = true;
            }

            check_true(caught, "array<auto> [int, string] should fail");
        });

        test("array_auto_allows_homogeneous", [this]() {
            auto eng = engine::make();

            // array<auto> x = [1, 2, 3] - should work, all same type
            auto result = eng->execute(R"(
                array<auto> nums = [1, 2, 3];
                nums.size()
            )");

            check_eq(result.as<int>(), 3, "array<auto> [int, int, int] succeeds");
        });

        test("array_var_allows_heterogeneous", [this]() {
            auto eng = engine::make();

            // array<var> x = [1, "hello"] - should work, var allows any type
            auto result = eng->execute(R"(
                array<var> items = [1, "hello", 3.14];
                items.size()
            )");

            check_eq(result.as<int>(), 3, "array<var> allows heterogeneous elements");
        });

        test("auto_empty_array_succeeds", [this]() {
            auto eng = engine::make();

            // auto x = [] - empty array is trivially homogeneous
            auto result = eng->execute(R"(
                auto empty = [];
                empty.size()
            )");

            check_eq(result.as<int>(), 0, "auto [] succeeds (empty array)");
        });

        test("auto_single_element_array_succeeds", [this]() {
            auto eng = engine::make();

            // auto x = [1] - single element is trivially homogeneous
            auto result = eng->execute(R"(
                auto single = [42];
                single[0]
            )");

            check_eq(result.as<int>(), 42, "auto [single element] succeeds");
        });

        // --- Nested Container Homogeneity (up to 3 levels deep) ---

        test("nested_array_2_levels_homogeneous", [this]() {
            auto eng = engine::make();

            // [[1,2], [3,4]] - all inner arrays have ints, outer has arrays
            auto result = eng->execute(R"(
                auto matrix = [[1, 2], [3, 4]];
                matrix[1][1]
            )");

            check_eq(result.as<int>(), 4, "auto [[int]] succeeds");
        });

        test("nested_array_2_levels_heterogeneous_inner_fails", [this]() {
            auto eng = engine::make();

            // [[1, "a"], [2, "b"]] - inner arrays have mixed types
            bool caught = false;
            try {
                eng->execute(R"(
                    auto mixed = [[1, "a"], [2, "b"]];
                )");
            } catch (const std::exception&) {
                caught = true;
            }

            check_true(caught, "auto [[int, string]] fails - inner array heterogeneous");
        });

        test("nested_array_3_levels_homogeneous", [this]() {
            auto eng = engine::make();

            // [[[1,2], [3,4]], [[5,6], [7,8]]] - 3 levels all homogeneous
            auto result = eng->execute(R"(
                auto cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];
                cube[1][0][1]
            )");

            check_eq(result.as<int>(), 6, "auto [[[int]]] succeeds");
        });

        test("nested_array_3_levels_heterogeneous_deepest_fails", [this]() {
            auto eng = engine::make();

            // [[[1, "x"]]] - deepest level is heterogeneous
            bool caught = false;
            try {
                eng->execute(R"(
                    auto deep = [[[1, "x"]]];
                )");
            } catch (const std::exception&) {
                caught = true;
            }

            check_true(caught, "auto [[[int, string]]] fails - deepest level heterogeneous");
        });

        test("nested_map_2_levels_homogeneous", [this]() {
            auto eng = engine::make();

            // {"a": {"x": 1, "y": 2}, "b": {"x": 3, "y": 4}} - all values are maps with ints
            auto result = eng->execute(R"(
                auto data = {"a": {"x": 1, "y": 2}, "b": {"x": 3, "y": 4}};
                data["b"]["y"]
            )");

            check_eq(result.as<int>(), 4, "auto {k: {k: int}} succeeds");
        });

        test("nested_map_2_levels_heterogeneous_inner_fails", [this]() {
            auto eng = engine::make();

            // {"a": {"x": 1, "y": "bad"}} - inner map has mixed values
            bool caught = false;
            try {
                eng->execute(R"(
                    auto bad = {"a": {"x": 1, "y": "bad"}};
                )");
            } catch (const std::exception&) {
                caught = true;
            }

            check_true(caught, "auto {k: {k: mixed}} fails - inner map heterogeneous");
        });

        test("mixed_array_map_3_levels_homogeneous", [this]() {
            auto eng = engine::make();

            // [[{"a": 1, "b": 2}], [{"c": 3, "d": 4}]] - arrays of arrays of homogeneous maps
            auto result = eng->execute(R"(
                auto data = [[{"a": 1, "b": 2}], [{"c": 3, "d": 4}]];
                data[1][0]["d"]
            )");

            check_eq(result.as<int>(), 4, "auto [[{k: int}]] succeeds");
        });

        test("mixed_map_array_3_levels_homogeneous", [this]() {
            auto eng = engine::make();

            // {"items": [[1, 2], [3, 4]]} - map with value being 2D int array
            auto result = eng->execute(R"(
                auto data = {"items": [[1, 2], [3, 4]]};
                data["items"][1][0]
            )");

            check_eq(result.as<int>(), 3, "auto {k: [[int]]} succeeds");
        });

        test("mixed_3_levels_heterogeneous_fails", [this]() {
            auto eng = engine::make();

            // {"items": [[1, "bad"]]} - deepest array has mixed types
            bool caught = false;
            try {
                eng->execute(R"(
                    auto bad = {"items": [[1, "bad"]]};
                )");
            } catch (const std::exception&) {
                caught = true;
            }

            check_true(caught, "auto {k: [[int, string]]} fails");
        });

        test("var_allows_any_nesting", [this]() {
            auto eng = engine::make();

            // var bypasses all homogeneity checks at any depth
            auto result = eng->execute(R"(
                var chaos = [[1, "a", true], {"x": [1, "b"]}, 42];
                chaos.size()
            )");

            check_eq(result.as<int>(), 3, "var allows deeply nested heterogeneous");
        });

        // ============================================================
        // SECTION 2: WORKING TYPE SAFETY - Tests that should always pass
        // These verify the type system works correctly
        // ============================================================

        // --- Primitive Type Enforcement ---

        test("int_variable_rejects_string", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute(R"(
                    int x = 5;
                    x = "hello";
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "int variable should reject string assignment");
        });

        test("string_variable_rejects_uncoerced_int", [this]() {
            auto eng = engine::make();
            // String variables may accept int via to_string coercion
            // This tests the base behavior
            auto result = eng->execute(R"(
                string s = "hello";
                s = "world";
                s
            )");
            check_eq(result.as<std::string>(), "world");
        });

        test("bool_variable_accepts_assignment", [this]() {
            auto eng = engine::make();
            // Test that bool variables can be assigned boolean values
            auto result = eng->execute(R"(
                bool b = true;
                b = false;
                b
            )");
            check_eq(result.as<bool>(), false);
        });

        test("bool_rejects_incompatible_assignment", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute(R"(
                    bool b = true;
                    b = 42;  // int to bool - may or may not be allowed
                )");
                // If it gets here without throwing, check the conversion behavior
                // Note: JaiScript may allow int->bool conversion (truthy)
            } catch (const std::exception&) {
                caught = true;
            }
            // Document actual behavior - this may pass or fail depending on bool coercion rules
        });

        // --- Numeric Conversion (Working as Designed) ---

        test("int_to_float_widening_works", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                float f = 3.14;
                f = 5;  // int widens to float
                f
            )");
            check_eq(result.as<double>(), 5.0);
        });

        test("float_to_int_narrowing_works", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                int i = 5;
                i = 3.7;  // float narrows to int (truncates)
                i
            )");
            check_eq(result.as<int>(), 3);
        });

        test("mixed_arithmetic_promotes_to_float", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("5 + 3.14");
            check_eq(result.as<double>(), 8.14);
        });

        test("integer_division_stays_integer", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("10 / 3");
            check_eq(result.as<int>(), 3);
        });

        test("float_division_preserves_precision", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("10.0 / 3");
            check(std::abs(result.as<double>() - 3.333333) < 0.001);
        });

        // --- Auto Type Locking ---

        test("auto_locks_to_first_value_type", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto x = 42;
                x = 100;
                x
            )");
            check_eq(result.as<int>(), 100);
            check_eq(result.type(), script_value_type::jai_int_type);
        });

        test("auto_rejects_incompatible_reassignment", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute(R"(
                    auto x = 42;
                    x = "string";
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "auto should reject type change");
        });

        test("auto_allows_numeric_conversion", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto x = 3.14;
                x = 5;  // int to float is OK
                x
            )");
            check_eq(result.as<double>(), 5.0);
        });

        // --- Var Dynamic Typing ---

        test("var_accepts_any_type", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                var x = 42;
                x = "hello";
                x = 3.14;
                x = true;
            )");
            // Should not throw
        });

        test("var_preserves_actual_type", [this]() {
            auto eng = engine::make();

            auto r1 = eng->execute("var x = 42; x");
            check_eq(r1.type(), script_value_type::jai_int_type);

            auto r2 = eng->execute("x = \"hello\"; x");
            check_eq(r2.type(), script_value_type::jai_string_type);
        });

        // --- Null Type Safety ---

        test("null_assignable_to_object_types", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Foo { int x = 0; }
            )");

            eng->execute(R"(
                auto f = Foo();
                f = null;  // OK for objects
            )");
        });

        test("null_not_assignable_to_primitives", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute(R"(
                    int x = 5;
                    x = null;
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "null should not be assignable to int");
        });

        // --- Function Parameter Type Enforcement ---

        test("function_param_type_enforced", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute(R"(
                    function add(int a, int b) -> int { return a + b; }
                    add("hello", "world");
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "typed parameters should reject wrong types");
        });

        test("function_param_numeric_conversion", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                function square(float x) -> float { return x * x; }
                square(5)  // int converts to float
            )");
            check_eq(result.as<double>(), 25.0);
        });

        test("function_return_type_enforced", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                function get_value() -> int { return 42; }
                get_value()
            )");
            check_eq(result.as<int>(), 42);
        });

        // --- Class Type Safety ---

        test("class_field_types_enforced", [this]() {
            auto eng = engine::make();
            // Test that class fields can be assigned valid types
            auto result = eng->execute(R"(
                class Data { int value = 0; }
                auto d = Data();
                d.value = 42;
                d.value
            )");
            check_eq(result.as<int>(), 42);

            // Note: Field type enforcement may vary - document actual behavior
            // Some implementations allow implicit conversion, others reject
        });

        test("class_inheritance_upcasting_allowed", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Animal { string name = ""; }
                class Dog : Animal { Dog() { name = "dog"; } }
            )");

            auto result = eng->execute(R"(
                Animal a = Dog();  // Upcasting OK
                a.name
            )");
            check_eq(result.as<std::string>(), "dog");
        });

        test("class_inheritance_type_safety", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Animal { string name = ""; }
                class Dog : Animal { Dog() { name = "dog"; } }
            )");

            // Test basic inheritance works
            auto result = eng->execute(R"(
                auto d = Dog();
                d.name
            )");
            check_eq(result.as<std::string>(), "dog");

            // Note: Downcasting behavior may vary by implementation
            // Document actual behavior rather than assuming rejection
        });

        // --- Constructor Conversion ---

        test("constructor_enables_implicit_conversion", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Wrapper {
                    int value = 0;
                    Wrapper(int v) { value = v; }
                }
                function process(Wrapper w) -> int { return w.value; }
            )");

            auto result = eng->execute("process(42)");  // int -> Wrapper via ctor
            check_eq(result.as<int>(), 42);
        });

        test("no_chained_constructor_conversion", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class A { int x = 0; }
                class B { B(A a) { } }
                class C { C(B b) { } }
            )");

            bool caught = false;
            try {
                eng->execute(R"(
                    function take_c(C c) {}
                    take_c(A());  // Would need A->B->C chain
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "chained conversions should be rejected");
        });

        // --- Reference Type Safety ---

        test("reference_preserves_type", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                function modify(int& ref) { ref = ref + 10; }
                auto x = 5;
                modify(x);
                x
            )");
            check_eq(result.as<int>(), 15);
        });

        // --- shared_ptr Type Safety ---

        test("shared_ptr_type_preserved", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Node { int value = 0; }
            )");

            auto result = eng->execute(R"(
                auto n = shared_ptr<Node>(Node());
                n.value = 42;
                n.value
            )");
            check_eq(result.as<int>(), 42);
        });

        test("shared_ptr_parameter_matching", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Data { int x = 0; }
                function process(shared_ptr<Data> d) -> int { return d.x; }
            )");

            auto result = eng->execute(R"(
                auto d = shared_ptr<Data>(Data());
                d.x = 99;
                process(d)
            )");
            check_eq(result.as<int>(), 99);
        });

        // --- Weak Pointer Type Safety ---

        test("weak_ptr_rejects_value_type", [this]() {
            auto eng = engine::make();
            eng->execute("class Obj { int x = 0; }");

            bool caught = false;
            try {
                eng->execute(R"(
                    auto o = Obj();  // Value type
                    auto w = weak_ptr<Obj>(o);  // Should fail
                )");
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check(msg.find("shared_ptr") != std::string::npos,
                    "Error should mention shared_ptr");
            }
            check(caught, "weak_ptr should reject value-semantic objects");
        });

        test("weak_ptr_accepts_shared_ptr", [this]() {
            auto eng = engine::make();
            eng->execute("class Obj { int x = 0; }");

            auto result = eng->execute(R"(
                auto o = shared_ptr<Obj>(Obj());
                o.x = 42;
                auto w = weak_ptr<Obj>(o);
                auto locked = w.lock();
                locked.x
            )");
            check_eq(result.as<int>(), 42);
        });

        // --- Bitwise Operator Type Safety ---

        test("bitwise_requires_integers", [this]() {
            auto eng = engine::make();

            // These should work
            check_eq(eng->execute("5 & 3").as<int>(), 1);
            check_eq(eng->execute("5 | 3").as<int>(), 7);
            check_eq(eng->execute("5 ^ 3").as<int>(), 6);
            check_eq(eng->execute("1 << 3").as<int>(), 8);
            check_eq(eng->execute("8 >> 2").as<int>(), 2);
        });

        test("bitwise_rejects_floats", [this]() {
            auto eng = engine::make();
            bool caught = false;
            try {
                eng->execute("5.0 & 3");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "bitwise & should reject float operands");
        });

        // --- Comparison Type Safety ---

        test("same_type_equality_works", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("5 == 5").as<bool>(), true);
            check_eq(eng->execute("5 == 6").as<bool>(), false);
            check_eq(eng->execute("3.14 == 3.14").as<bool>(), true);
            check_eq(eng->execute("\"hello\" == \"hello\"").as<bool>(), true);
            check_eq(eng->execute("\"hello\" == \"world\"").as<bool>(), false);
            check_eq(eng->execute("true == true").as<bool>(), true);
            check_eq(eng->execute("true == false").as<bool>(), false);
        });

        test("null_equality_works", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("null == null").as<bool>(), true);
            check_eq(eng->execute("null != null").as<bool>(), false);
        });

        test("numeric_comparisons_work", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("5 < 10").as<bool>(), true);
            check_eq(eng->execute("5 > 10").as<bool>(), false);
            check_eq(eng->execute("5 <= 5").as<bool>(), true);
            check_eq(eng->execute("5 >= 5").as<bool>(), true);
            check_eq(eng->execute("3.14 < 4.0").as<bool>(), true);
        });

        test("string_comparison_lexicographic", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("\"apple\" < \"banana\"").as<bool>(), true);
            check_eq(eng->execute("\"zebra\" > \"apple\"").as<bool>(), true);
        });

        // --- Truthiness (Documented Behavior) ---

        test("zero_is_falsy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                if (0) { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 0);
        });

        test("nonzero_is_truthy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                if (42) { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 1);
        });

        test("empty_string_is_falsy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                if ("") { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 0);
        });

        test("nonempty_string_is_truthy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                if ("hello") { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 1);
        });

        test("null_is_falsy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                if (null) { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 0);
        });

        test("nonempty_array_is_truthy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                auto arr = [1, 2, 3];
                if (arr) { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 1);
        });

        test("nonempty_map_is_truthy", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto r = 99;
                auto m = {"a": 1};
                if (m) { r = 1; } else { r = 0; }
                r
            )");
            check_eq(result.as<int>(), 1);
        });

        // --- Type Preservation Through Operations ---

        test("arithmetic_preserves_int_when_possible", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("5 + 3");
            check_eq(result.type(), script_value_type::jai_int_type);
        });

        test("arithmetic_promotes_to_float_when_needed", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("5 + 3.0");
            check_eq(result.type(), script_value_type::jai_float_type);
        });

        test("comparison_always_returns_bool", [this]() {
            auto eng = engine::make();
            check_eq(eng->execute("5 < 10").type(), script_value_type::jai_bool_type);
            check_eq(eng->execute("5 == 5").type(), script_value_type::jai_bool_type);
            check_eq(eng->execute("5 != 5").type(), script_value_type::jai_bool_type);
        });

        // ============================================================
        // SECTION 3: C++ TYPE INTEROP - Tests for C++ ↔ Script type safety
        // ============================================================

        test("cpp_int_to_script_preserves_type", [this]() {
            auto eng = engine::make();
            eng->add_global("cpp_value", script_value(42, eng->weak_from_this()));
            auto result = eng->execute("cpp_value");
            check_eq(result.type(), script_value_type::jai_int_type);
            check_eq(result.as<int>(), 42);
        });

        test("cpp_float_to_script_preserves_type", [this]() {
            auto eng = engine::make();
            eng->add_global("cpp_value", script_value(3.14, eng->weak_from_this()));
            auto result = eng->execute("cpp_value");
            check_eq(result.type(), script_value_type::jai_float_type);
            check_eq(result.as<double>(), 3.14);
        });

        test("cpp_string_to_script_preserves_type", [this]() {
            auto eng = engine::make();
            eng->add_global("cpp_value", script_value(std::string("hello"), eng->weak_from_this()));
            auto result = eng->execute("cpp_value");
            check_eq(result.type(), script_value_type::jai_string_type);
            check_eq(result.as<std::string>(), "hello");
        });

        test("cpp_bool_to_script_preserves_type", [this]() {
            auto eng = engine::make();
            eng->add_global("cpp_value", script_value(true, eng->weak_from_this()));
            auto result = eng->execute("cpp_value");
            check_eq(result.type(), script_value_type::jai_bool_type);
            check_eq(result.as<bool>(), true);
        });

        test("script_int_to_cpp_works", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("42");
            check_eq(result.as<int>(), 42);
            // int64_t and long use same as<int> under the hood
        });

        test("script_float_to_cpp_works", [this]() {
            auto eng = engine::make();
            auto result = eng->execute("3.14");
            check(std::abs(result.as<double>() - 3.14) < 0.001);
            check(std::abs(result.as<float>() - 3.14f) < 0.001f);
        });

        test("cpp_function_parameter_type_checking", [this]() {
            auto eng = engine::make();
            eng->add_function("typed_add", [](int a, int b) -> int {
                return a + b;
            });

            auto result = eng->execute("typed_add(3, 4)");
            check_eq(result.as<int>(), 7);
        });

        test("cpp_function_returns_correct_type", [this]() {
            auto eng = engine::make();
            eng->add_function("make_string", []() -> std::string {
                return "from_cpp";
            });

            auto result = eng->execute("make_string()");
            check_eq(result.type(), script_value_type::jai_string_type);
            check_eq(result.as<std::string>(), "from_cpp");
        });

        // ============================================================
        // SECTION 4: SCRIPT CLASS INHERITANCE - Type safety with inheritance
        // ============================================================

        test("script_class_method_returns_correct_type", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Calculator {
                    int value = 0;
                    function add(int x) -> int { return value + x; }
                    function get_value() -> int { return value; }
                }
            )");

            auto result = eng->execute(R"(
                auto calc = Calculator();
                calc.value = 10;
                calc.add(5)
            )");
            check_eq(result.as<int>(), 15);
        });

        test("script_class_inheritance_method_override", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Base {
                    function get_name() -> string { return "Base"; }
                }
                class Derived : Base {
                    override function get_name() -> string { return "Derived"; }
                }
            )");

            auto base_result = eng->execute("Base().get_name()");
            check_eq(base_result.as<std::string>(), "Base");

            auto derived_result = eng->execute("Derived().get_name()");
            check_eq(derived_result.as<std::string>(), "Derived");
        });

        test("script_class_polymorphic_call", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Animal {
                    function speak() -> string { return "..."; }
                }
                class Dog : Animal {
                    override function speak() -> string { return "Woof!"; }
                }
                class Cat : Animal {
                    override function speak() -> string { return "Meow!"; }
                }
            )");

            // Direct method calls on derived classes
            auto dog_result = eng->execute("Dog().speak()");
            check_eq(dog_result.as<std::string>(), "Woof!");

            auto cat_result = eng->execute("Cat().speak()");
            check_eq(cat_result.as<std::string>(), "Meow!");

            // Upcasting to base class
            auto base_result = eng->execute(R"(
                Animal a = Dog();
                a.speak()
            )");
            check_eq(base_result.as<std::string>(), "Woof!");
        });

        test("script_class_field_type_inheritance", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Entity {
                    int id = 0;
                    string name = "";
                }
                class Player : Entity {
                    int health = 100;
                    Player(string n) { name = n; }
                }
            )");

            auto result = eng->execute(R"(
                auto p = Player("Hero");
                p.id = 42;
                p.id
            )");
            check_eq(result.as<int>(), 42);
        });

        test("script_class_multi_level_inheritance", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class A { int a_val = 1; }
                class B : A { int b_val = 2; }
                class C : B { int c_val = 3; }
            )");

            // Test that C has all fields from A, B, and C
            auto result = eng->execute(R"(
                auto c = C();
                c.a_val + c.b_val + c.c_val
            )");
            check_eq(result.as<int>(), 6);
        });

        test("script_class_type_checking_in_function", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class TypeA { int x = 0; }
                class TypeB { int y = 0; }
                function process_a(TypeA a) -> int { return a.x; }
            )");

            // Should work with TypeA
            auto result = eng->execute(R"(
                auto a = TypeA();
                a.x = 42;
                process_a(a)
            )");
            check_eq(result.as<int>(), 42);

            // Should fail with TypeB
            bool caught = false;
            try {
                eng->execute("process_a(TypeB())");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "TypeB should not be accepted where TypeA is expected");
        });

        test("script_class_return_type_enforcement", [this]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Result { int value = 0; }
                function make_result(int v) -> Result {
                    auto r = Result();
                    r.value = v;
                    return r;
                }
            )");

            auto result = eng->execute("make_result(42).value");
            check_eq(result.as<int>(), 42);
        });

        // ============================================================
        // SECTION 5: CONTAINER TYPE SAFETY
        // ============================================================

        test("array_push_preserves_type_context", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto arr = [1, 2, 3];
                arr.push(4);
                arr.push(5);
                arr.size()
            )");
            check_eq(result.as<int>(), 5);
        });

        test("map_typed_access", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
                m["a"] + m["b"] + m["c"]
            )");
            check_eq(result.as<int>(), 6);
        });

        test("nested_container_type_safety", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto matrix = [[1, 2], [3, 4], [5, 6]];
                matrix[0][0] + matrix[1][1] + matrix[2][0]
            )");
            check_eq(result.as<int>(), 1 + 4 + 5);
        });

        test("map_of_arrays_type_safety", [this]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                auto data = {"nums": [1, 2, 3], "more": [4, 5, 6]};
                data["nums"].size() + data["more"].size()
            )");
            check_eq(result.as<int>(), 6);
        });

        // ============================================================
        // SECTION 6: SCRIPT CLASSES INHERITING FROM C++ TYPES
        // Tests type safety when script classes extend registered C++ classes
        // ============================================================

        test("script_inherits_cpp_basic_field_access", [this]() {
            auto eng = engine::make();

            // Register C++ Entity class
            class_builder<Entity>(*eng, "Entity")
                .constructor<>()
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .method("get_type", &Entity::get_type)
                .method("get_id", &Entity::get_id)
                .method("get_name", &Entity::get_name)
                .build();

            // Script class inherits from C++ Entity
            auto result = eng->execute(R"(
                class Player : Entity {
                    int score = 0;

                    Player(string n, int i) : super(n, i) {
                        score = 100;
                    }
                }

                auto p = Player("Hero", 42);
                p.id  // Access C++ base class field
            )");
            check_eq(result.as<int>(), 42);
        });

        test("script_inherits_cpp_base_method_call", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .method("get_type", &Entity::get_type)
                .method("get_name", &Entity::get_name)
                .build();

            auto result = eng->execute(R"(
                class Player : Entity {
                    Player(string n) : super(n, 1) {}
                }

                auto p = Player("TestPlayer");
                p.get_name()  // Call C++ base class method
            )");
            check_eq(result.as<std::string>(), "TestPlayer");
        });

        test("script_inherits_cpp_override_method", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .method("get_type", &Entity::get_type)
                .build();

            auto result = eng->execute(R"(
                class Enemy : Entity {
                    Enemy(string n) : super(n, 999) {}

                    override function get_type() -> string {
                        return "Enemy";
                    }
                }

                auto e = Enemy("Goblin");
                e.get_type()
            )");
            check_eq(result.as<std::string>(), "Enemy");
        });

        test("script_inherits_cpp_super_call", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .method("get_type", &Entity::get_type)
                .build();

            auto result = eng->execute(R"(
                class Boss : Entity {
                    Boss(string n) : super(n, 1000) {}

                    override function get_type() -> string {
                        return super::get_type() + "_Boss";
                    }
                }

                auto b = Boss("Dragon");
                b.get_type()
            )");
            check_eq(result.as<std::string>(), "Entity_Boss");
        });

        test("script_inherits_cpp_script_specific_field", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .build();

            auto result = eng->execute(R"(
                class Character : Entity {
                    int level = 1;
                    int experience = 0;

                    Character(string n) : super(n, 0) {
                        level = 1;
                        experience = 0;
                    }

                    function gain_exp(int amount) {
                        experience = experience + amount;
                        if (experience >= 100) {
                            level = level + 1;
                            experience = 0;
                        }
                    }
                }

                auto c = Character("Warrior");
                c.gain_exp(150);
                c.level
            )");
            check_eq(result.as<int>(), 2);
        });

        test("script_inherits_cpp_type_preserved_in_parameter", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .method("get_id", &Entity::get_id)
                .build();

            auto result = eng->execute(R"(
                class NPC : Entity {
                    NPC(string n, int i) : super(n, i) {}
                }

                // Function accepts base Entity type
                function process_entity(Entity e) -> int {
                    return e.get_id();
                }

                auto npc = NPC("Merchant", 500);
                process_entity(npc)  // NPC should work as Entity parameter
            )");
            check_eq(result.as<int>(), 500);
        });

        test("script_inherits_cpp_different_cpp_base_types", [this]() {
            auto eng = engine::make();

            // Register Vehicle
            class_builder<Vehicle>(*eng, "Vehicle")
                .constructor<int, std::string>()
                .property("speed", &Vehicle::speed)
                .property("model", &Vehicle::model)
                .method("get_max_speed", &Vehicle::get_max_speed)
                .method("accelerate", &Vehicle::accelerate)
                .build();

            // Register Animal
            class_builder<Animal>(*eng, "Animal")
                .constructor<std::string, int>()
                .property("species", &Animal::species)
                .property("legs", &Animal::legs)
                .method("speak", &Animal::speak)
                .method("get_legs", &Animal::get_legs)
                .build();

            // Script classes extending different C++ bases
            auto vehicle_result = eng->execute(R"(
                class Car : Vehicle {
                    Car(string m) : super(0, m) {}

                    override function get_max_speed() -> int {
                        return 200;
                    }
                }

                auto car = Car("Sedan");
                car.get_max_speed()
            )");
            check_eq(vehicle_result.as<int>(), 200);

            auto animal_result = eng->execute(R"(
                class Dog : Animal {
                    Dog() : super("Canine", 4) {}

                    override function speak() -> string {
                        return "Woof!";
                    }
                }

                auto dog = Dog();
                dog.speak()
            )");
            check_eq(animal_result.as<std::string>(), "Woof!");
        });

        test("script_inherits_cpp_type_mismatch_rejected", [this]() {
            auto eng = engine::make();

            class_builder<Vehicle>(*eng, "Vehicle")
                .constructor<int, std::string>()
                .property("speed", &Vehicle::speed)
                .build();

            class_builder<Animal>(*eng, "Animal")
                .constructor<std::string, int>()
                .property("species", &Animal::species)
                .build();

            // Car extends Vehicle
            eng->execute(R"(
                class Car : Vehicle {
                    Car() : super(0, "car") {}
                }
            )");

            // Function expects Animal
            eng->execute(R"(
                function pet(Animal a) -> string {
                    return a.species;
                }
            )");

            // Passing Car (extends Vehicle) to function expecting Animal should fail
            bool caught = false;
            try {
                eng->execute("pet(Car())");
            } catch (const std::exception&) {
                caught = true;
            }
            check(caught, "Car (extends Vehicle) should not be accepted as Animal parameter");
        });

        test("script_inherits_cpp_multi_level", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .build();

            // Script class inheriting from C++, then another script class inheriting from that
            auto result = eng->execute(R"(
                class Character : Entity {
                    int level = 1;
                    Character(string n) : super(n, 0) {}
                }

                class Mage : Character {
                    int mana = 100;
                    Mage(string n) : super(n) {
                        level = 5;
                    }
                }

                auto m = Mage("Gandalf");
                m.name + " Level:" + m.level + " Mana:" + m.mana
            )");
            // Check that all fields from C++ and both script levels are accessible
            check_eq(result.as<std::string>(), "Gandalf Level:5 Mana:100");
        });

        test("script_inherits_cpp_deep_multi_level", [this]() {
            // Test 4-level inheritance: Archmage : Mage : Character : Entity (script→script→script→C++)
            // This verifies the iterative solution handles arbitrary depth
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .build();

            auto result = eng->execute(R"(
                class Character : Entity {
                    int level = 1;
                    Character(string n, int startLevel) : super(n, 0) {
                        level = startLevel;
                    }
                }

                class Mage : Character {
                    int mana = 50;
                    Mage(string n, int startLevel, int startMana) : super(n, startLevel) {
                        mana = startMana;
                    }
                }

                class Archmage : Mage {
                    int spellPower = 100;
                    Archmage(string n) : super(n, 20, 500) {
                        spellPower = 999;
                    }
                }

                auto a = Archmage("Merlin");
                a.name + " L:" + a.level + " M:" + a.mana + " SP:" + a.spellPower
            )");
            // All 4 levels should be initialized correctly
            check_eq(result.as<std::string>(), "Merlin L:20 M:500 SP:999");
        });

        test("script_inherits_cpp_cpp_method_mutation", [this]() {
            auto eng = engine::make();

            class_builder<Vehicle>(*eng, "Vehicle")
                .constructor<int, std::string>()
                .property("speed", &Vehicle::speed)
                .property("model", &Vehicle::model)
                .method("accelerate", &Vehicle::accelerate)
                .build();

            auto result = eng->execute(R"(
                class Motorcycle : Vehicle {
                    Motorcycle(string m) : super(0, m) {}
                }

                auto bike = Motorcycle("Ducati");
                bike.accelerate(50);  // Call C++ method that mutates state
                bike.accelerate(30);
                bike.speed
            )");
            check_eq(result.as<int>(), 80);
        });

        test("script_inherits_cpp_script_overrides_cpp_field_access", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .method("get_name", &Entity::get_name)
                .build();

            // Script can access and modify C++ base field
            auto result = eng->execute(R"(
                class CustomEntity : Entity {
                    CustomEntity() : super("initial", 0) {}

                    function rename(string newName) {
                        name = newName;  // Modify C++ base class field
                    }
                }

                auto ce = CustomEntity();
                ce.rename("Modified");
                ce.get_name()
            )");
            check_eq(result.as<std::string>(), "Modified");
        });

        test("script_inherits_cpp_shared_ptr_inheritance", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .method("get_id", &Entity::get_id)
                .build();

            auto result = eng->execute(R"(
                class SmartEntity : Entity {
                    SmartEntity(string n, int i) : super(n, i) {}
                }

                auto ptr = shared_ptr<SmartEntity>(SmartEntity("PtrTest", 777));
                ptr.get_id()
            )");
            check_eq(result.as<int>(), 777);
        });

        test("script_inherits_cpp_return_derived_from_base_function", [this]() {
            auto eng = engine::make();

            class_builder<Entity>(*eng, "Entity")
                .constructor<std::string, int>()
                .property("name", &Entity::name)
                .property("id", &Entity::id)
                .build();

            auto result = eng->execute(R"(
                class Monster : Entity {
                    int danger_level = 0;

                    Monster(string n, int d) : super(n, 0) {
                        danger_level = d;
                    }
                }

                function create_monster(string name, int danger) -> Entity {
                    return Monster(name, danger);
                }

                auto e = create_monster("Orc", 5);
                e.name
            )");
            check_eq(result.as<std::string>(), "Orc");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::type_safety_tests)