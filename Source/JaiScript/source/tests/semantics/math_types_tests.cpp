// The native math kinds (docs/math_types_design.md): math::vec2, math::vec3 (binary32 components) and math::ivec2,
// math::ivec3 (int32 components), inline in the value. Every row runs on both backends with proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/math/types.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class math_types_tests : public suite {
public:
    math_types_tests() : suite("Math Types") {}

    void forge_tests() override {
        auto run = [](const std::string& src, bool use_vm, bool proofs) {
            auto e = jai::engine::make();
            if (use_vm) { e->set_backend(jai::backend_type::vm); }
            e->type_proofs(proofs);
            e->execution_budget(0);
            jai::stdlib::register_all(e);
            try { return e->execute(src).to_string(); }
            catch (const std::exception& ex) { return std::string("ERROR: ") + ex.what(); }
        };
        auto expect = [this, run](const std::string& src, const std::string& expected, const char* what) {
            for (bool proofs : {true, false}) {
                for (bool use_vm : {false, true}) {
                    check_eq(expected, run(src, use_vm, proofs), std::string(what) + (use_vm ? " (vm" : " (interpreter") + (proofs ? ")" : ", proofs off)"));
                }
            }
        };
        // the text a statement raises, caught in script
        auto raised = [](const std::string& stmt) { return "string m = \"\"; try { " + stmt + " } catch (e) { m = e; } m"; };

        test("construction_and_text", [expect]() {
            expect("to_string(math::vec3(1, 2.5, 3))", "(1.0, 2.5, 3.0)", "three components");
            expect("to_string(math::vec3(1, 2))", "(1.0, 2.0, 0.0)", "z defaults to 0");
            expect("to_string(math::vec3())", "(0.0, 0.0, 0.0)", "zero");
            expect("to_string(math::ivec3(4, -2))", "(4, -2, 0)", "int components");
            expect("to_string(math::vec2(0.1, 1e30))", "(0.1, 1e+30)", "shortest binary32 text");
            expect("to_string(math::vec3(math::vec2(1, 2), 3))", "(1.0, 2.0, 3.0)", "a vec2 and z");
            expect("to_string(math::ivec3(math::vec3(1.9, -1.9, 2)))", "(1, -1, 2)", "vec to ivec truncates");
            expect("to_string(math::vec2(math::ivec2(3, -4)))", "(3.0, -4.0)", "ivec to vec");
            expect("type_of(math::vec3()) + \" \" + type_of(math::ivec2())", "math::vec3 math::ivec2", "type_of");
            expect("math::vec3 v; to_string(v)", "(0.0, 0.0, 0.0)", "a declaration default-constructs");
            expect("math::ivec2 p(3, 4); to_string(p)", "(3, 4)", "direct initialization");
            expect("math::vec3 v{1, 2, 3}; to_string(v)", "(1.0, 2.0, 3.0)", "brace initialization");
            expect("to_string(math::ivec2(true, 2))", "(1, 2)", "a bool component");
            expect("to_string(math::vec4(1, 2.5, 3, 4)) + type_of(math::vec4())", "(1.0, 2.5, 3.0, 4.0)math::vec4", "vec4");
            expect("to_string(math::vec4(math::vec3(1, 2, 3), 4)) + to_string(math::vec4(math::ivec2(1, 2), 3, 4))",
                   "(1.0, 2.0, 3.0, 4.0)(1.0, 2.0, 3.0, 4.0)", "a narrower vector completed by scalars");
        });

        test("constructor_errors", [expect, raised]() {
            expect(raised("math::vec3(1, 2, 3, 4);"), "math::vec3 expects 0 to 3 arguments", "arity");
            expect(raised("math::vec2(\"a\", 1);"), "math::vec2 expects numeric components", "a string component");
            expect(raised("math::vec3(5);"), "math::vec3 expects numeric components", "one scalar is not a vector");
            expect(raised("math::vec3(math::vec2(1, 2));"), "math::vec3 expects numeric components", "another dimension");
            expect(raised("math::ivec2(3000000000, 0);"), "Integer overflow: the value does not fit an int32 component", "int32 range");
            expect(raised("math::ivec2(1e100, 0);"), "Cannot convert float to integer: non-finite or out of range", "a float out of range");
            expect(raised("math::vec4(1, 2, 3, 4, 5);"), "math::vec4 expects 0 to 4 arguments", "vec4 arity");
            expect(raised("math::vec4(math::vec2(1, 2), 3);"), "math::vec4 expects numeric components", "the scalars must complete the count");
        });

        test("components_and_aliases", [expect, raised]() {
            expect("math::vec3 v = math::vec3(1, 2, 3); to_string(v.x + v.y * 10 + v.z * 100)", "321.0", "x y z");
            expect("math::vec3 v = math::vec3(1, 2, 3); to_string(v.width + v.height * 10 + v.depth * 100)", "321.0", "width height depth");
            expect("math::vec2 w = math::vec2(4, 5); to_string(w.width + w.y)", "9.0", "vec2 aliases");
            expect("type_of(math::ivec3(1, 2, 3).x) + \" \" + type_of(math::vec3(1, 2, 3).x)", "int float", "component kinds");
            expect(raised("var w = math::vec2(); var z = w.z;"), "math::vec2 has no member 'z'", "vec2 has no z");
            expect(raised("var v = math::vec3(); var q = v.w;"), "math::vec3 has no member 'w'", "no w");
            expect(raised("var v = math::vec3(); v.length();"), "math::vec3 has no member 'length'", "no methods yet");
            expect("to_string(math::vec2(0.1, 0).x)", "0.10000000149011612", "a component widens exactly");
            expect("to_string(math::vec2(0.1, 0).x == 0.1)", "false", "binary32 is not binary64");
            expect("math::vec4 q = math::vec4(1, 2, 3, 4); q.w += 1; to_string(q.w + q.depth * 10)", "35.0", "vec4's w");
        });

        test("component_stores_land_in_named_storage", [expect, raised]() {
            expect("math::vec3 v; v.x = 5; v.depth = 7; to_string(v)", "(5.0, 0.0, 7.0)", "a local");
            expect("math::vec3 v = math::vec3(1, 1, 1); v.y += 2.5; v.x *= 3; v.z -= 4; to_string(v)", "(3.0, 3.5, -3.0)", "compound");
            expect("math::ivec2 p; p.x++; ++p.y; p.y++; to_string(p)", "(1, 2)", "increments");
            expect("math::vec3 a = math::vec3(1, 2, 3); math::vec3 b = a; b.x = 9; to_string(a) + to_string(b)", "(1.0, 2.0, 3.0)(9.0, 2.0, 3.0)", "a copy is a value");
            expect("class Body { math::vec3 pos; } var b = Body(); b.pos.x = 4; b.pos.y += 1; to_string(b.pos)", "(4.0, 1.0, 0.0)", "a field");
            expect("class C { math::vec3 pos; void mv() { pos.x += 1; this.pos.y = 5; } } var c = C(); c.mv(); c.mv(); to_string(c.pos)", "(2.0, 5.0, 0.0)", "a bare field and this.field in a method");
            expect("var arr = [math::vec3(1, 2, 3)]; arr[0].z = 9; to_string(arr[0])", "(1.0, 2.0, 9.0)", "an element");
            expect("var arr = [math::vec3(1, 2, 3), math::vec3()]; int i = 1; arr[i].x += 2; to_string(arr[1])", "(2.0, 0.0, 0.0)", "an element by index variable");
            expect("var m = {\"a\": math::ivec2(1, 2)}; m[\"a\"].x = 5; to_string(m[\"a\"])", "(5, 2)", "a map value");
            expect("math::vec3 v; var& r = v; r.x = 3; to_string(v)", "(3.0, 0.0, 0.0)", "through a reference");
            expect("function bump(math::vec3& p) { p.x += 1; } math::vec3 v; bump(v); bump(v); to_string(v)", "(2.0, 0.0, 0.0)", "a reference parameter");
            expect("function set(math::vec3 p) { p.x = 9; return p.x; } math::vec3 v; var r = set(v); to_string(v) + to_string(r)", "(0.0, 0.0, 0.0)9.0", "a value parameter is a copy");
            expect("math::vec3 G; function f() { G.x = 2; } f(); to_string(G)", "(2.0, 0.0, 0.0)", "a global from a function");
            expect("class S { static math::vec3 s; } S::s.x = 4; S::s.y += 2; S::s.z++; to_string(S::s)", "(4.0, 2.0, 1.0)", "a static member");
            expect("class T { static math::vec2 g; static void bump() { T::g.x += 1; g.y += 2; } } T::bump(); T::bump(); to_string(T::g)", "(2.0, 4.0)", "a static from a static method");
            expect("namespace ns { math::ivec2 v = math::ivec2(1, 1); void f() { v.y *= 5; } } ns::v.x = 7; ns::v.y -= 3; ns::f(); to_string(ns::v)", "(7, -10)", "a namespace variable");
            expect("var rows = [[math::ivec2(1, 1)], [math::ivec2(2, 2)]]; int i = 1; rows[i][0].x = 9; rows[i - 1][0].y += 4; to_string(rows[1][0]) + to_string(rows[0][0])", "(9, 2)(1, 5)", "nested and computed subscripts");
            expect("class Box { math::vec2 p; } var boxes = [Box(), Box()]; int i = 1; boxes[i].p.y = 6; boxes[1].p.x += 1; to_string(boxes[1].p)", "(1.0, 6.0)", "a field of an element");
            expect("math::vec3 v; var add = [&]() { v.y = 4; }; add(); to_string(v)", "(0.0, 4.0, 0.0)", "a [&] capture");
            expect(raised("function mk() { return math::vec3(1, 2, 3); } mk().x = 5;"), "Cannot assign to a member of a temporary", "a temporary");
            expect(raised("var v = math::vec3(); var s = \"a\"; v.x = s;"), "Cannot store a non-numeric value in a math::vec3 component", "a string component");
            expect(raised("var v = math::vec2(); v.z = 1;"), "math::vec2 has no member 'z'", "a store to a missing component");
        });

        test("every_copy_is_its_own_value", [expect]() {
            expect("math::vec3 a = math::vec3(1, 2, 3); var b = a; var c = [a, a]; c[1].y = 7; b.z = 9; to_string(a) + to_string(b) + to_string(c[0]) + to_string(c[1])",
                   "(1.0, 2.0, 3.0)(1.0, 2.0, 9.0)(1.0, 2.0, 3.0)(1.0, 7.0, 3.0)", "locals and elements");
            expect("math::ivec3 v = math::ivec3(1, 1, 1); var& r = v; r.y = 5; var copy = v; copy.x = 9; to_string(v) + to_string(copy)",
                   "(1, 5, 1)(9, 5, 1)", "a reference writes the named value, a copy does not");
            expect("function f() { math::vec3 v = math::vec3(1, 2, 3); var keep = [=]() { return v; }; v.x = 5; return to_string(keep()) + to_string(v); } f()",
                   "(1.0, 2.0, 3.0)(5.0, 2.0, 3.0)", "a [=] capture keeps its copy");
            expect("var m = {\"a\": math::vec3(1, 2, 3)}; var n = m; n[\"a\"].x = 9; to_string(m[\"a\"]) + to_string(n[\"a\"])",
                   "(1.0, 2.0, 3.0)(9.0, 2.0, 3.0)", "a map copy");
            expect("class P { math::ivec3 at; } var p = P(); p.at.x = 4; var q = p; q.at.x += 1; to_string(p.at) + to_string(q.at)",
                   "(4, 0, 0)(5, 0, 0)", "an instance copy");
            expect("var total = 0.0; for (int i = 0; i < 3000; i++) { math::vec3 p = math::vec3(i, 1, 2); var q = p; q.x += 1; total += q.x - p.x; } to_string(total)",
                   "3000.0", "node churn");
        });

        test("the_assignment_value_is_the_stored_component", [expect]() {
            expect("math::ivec2 p; var r = (p.x = 7.9); to_string(r)", "7", "int32 component");
            expect("math::vec3 v; var r = (v.x = 0.1); to_string(r)", "0.10000000149011612", "binary32 component");
            expect("math::ivec2 p; var r = p.y++; to_string(r) + to_string(p)", "0(0, 1)", "postfix yields the old value");
        });

        test("narrowing_and_overflow", [expect, raised]() {
            expect("math::vec2 v; v.x = 1e300; to_string(v.x)", "inf", "binary32 overflow is infinity");
            expect("math::vec2 v; v.y = -1e300; to_string(v.y)", "-inf", "negative infinity");
            expect(raised("math::ivec2 p; p.x = 3000000000;"), "Integer overflow: the value does not fit an int32 component", "an int32 store");
            expect(raised("math::ivec2 p = math::ivec2(2147483647, 0); p.x += 1;"), "Integer overflow: the value does not fit an int32 component", "a compound narrows the int64 sum");
            expect(raised("var q = math::ivec2(2147483647, 0) + 1;"), "Integer overflow in '+'", "vector arithmetic overflows");
            expect(raised("var q = -math::ivec2(-2147483647 - 1, 0);"), "Integer overflow in unary '-'", "negating INT32_MIN");
            expect(raised("var q = math::ivec2(1, 2) * 3000000000;"), "Integer overflow: the value does not fit an int32 component", "a scalar outside int32");
            expect(raised("math::vec2 v; v.x = 1e300; math::ivec2 p = v;"), "Cannot convert float to integer: non-finite or out of range", "an infinite component does not convert");
            expect(raised("math::ivec2 p = math::vec2(3e9, 0);"), "Integer overflow: the value does not fit an int32 component", "a finite component beyond int32");
            expect("math::ivec3 p = math::vec3(-2.9, 2.9, -0.0); to_string(p)", "(-2, 2, 0)", "float to int32 truncates toward zero");
            expect("math::vec2 v; v.x = 1e300; v.x -= v.x; to_string(v == v) + to_string(v != v) + to_string(!v)", "falsetruefalse", "NaN is unequal and nonzero");
        });

        test("operators", [expect, raised]() {
            expect("to_string(math::vec3(1, 2, 3) + math::vec3(0.5, 0.5, 0.5))", "(1.5, 2.5, 3.5)", "vec + vec");
            expect("to_string(math::vec3(1, 2, 3) - math::vec3(1, 1, 1))", "(0.0, 1.0, 2.0)", "vec - vec");
            expect("to_string(math::vec3(1, 2, 3) * 2) + to_string(2 * math::vec3(1, 2, 3))", "(2.0, 4.0, 6.0)(2.0, 4.0, 6.0)", "scalar on either side");
            expect("to_string(math::vec2(3, 6) / 2) + to_string(6 / math::vec2(3, 6))", "(1.5, 3.0)(2.0, 1.0)", "division both ways");
            expect("to_string(math::vec2(1, 2) * math::vec2(3, 4))", "(3.0, 8.0)", "componentwise product");
            expect("to_string(-math::vec3(1, -2, 0))", "(-1.0, 2.0, -0.0)", "negation");
            expect("to_string(math::ivec2(7, -7) / 2) + to_string(math::ivec2(7, -7) % 4)", "(3, -3)(3, -3)", "int division truncates, modulo keeps the sign");
            expect("to_string(math::ivec3(1, 2, 3) * 2 + 1)", "(3, 5, 7)", "ivec arithmetic");
            expect("to_string(math::vec4(1, 2, 3, 4) * 2 - math::vec4(1, 1, 1, 1))", "(1.0, 3.0, 5.0, 7.0)", "vec4 arithmetic");
            expect("to_string(math::vec2(0.1, 0) + 0.2)", "(0.3, 0.2)", "binary32 arithmetic");
            expect("math::vec3 v = math::vec3(1, 1, 1); v += math::vec3(1, 2, 3); v *= 2; to_string(v)", "(4.0, 6.0, 8.0)", "compound on a vector");
            expect("class Body { math::vec3 pos; } var b = Body(); b.pos += math::vec3(1, 2, 3); b.pos *= 2; to_string(b.pos)", "(2.0, 4.0, 6.0)", "compound on a vector field");
            expect("\"p=\" + math::ivec2(1, 2)", "p=(1, 2)", "string concatenation");
            expect(raised("var q = math::ivec2(1, 2) * 1.5;"), "Invalid operands for * operator", "ivec and float");
            expect(raised("var q = math::vec3(1, 2, 3) + math::ivec3(1, 2, 3);"), "Invalid operands for + operator", "mixed kinds");
            expect(raised("var q = math::vec2(1, 2) + math::vec3(1, 2, 3);"), "Invalid operands for + operator", "mixed dimensions");
            expect(raised("var q = math::vec2(1, 2) % 2;"), "Invalid operands for % operator", "vec modulo");
            expect(raised("var q = math::vec2(1, 1) / 0;"), "Division by zero in float operation", "float division by zero");
            expect(raised("var q = math::ivec2(1, 1) / math::ivec2(1, 0);"), "Division by zero in integer operation", "int division by zero");
            expect(raised("var q = math::vec2(1, 1) < math::vec2(2, 2);"), "math::vec2 has no ordering", "no ordering");
            expect(raised("var q = math::ivec3(1, 1, 1) <=> math::ivec3(1, 1, 1);"), "math::ivec3 has no ordering", "no spaceship");
        });

        test("component_wise_library", [expect, raised]() {
            expect("to_string(math::mix(math::vec2(0, 10), math::vec2(10, 20), 0.5))", "(5.0, 15.0)", "mix, float t");
            expect("to_string(math::lerp(math::vec3(0, 0, 0), math::vec3(10, 20, 40), math::vec3(0.5, 0.25, 1)))", "(5.0, 5.0, 40.0)", "lerp, per-component t");
            expect("to_string(math::mix(math::vec4(0, 0, 0, 0), math::vec4(1, 2, 3, 4), 0.5))", "(0.5, 1.0, 1.5, 2.0)", "vec4");
            expect("var m = math::mix(math::ivec2(0, 0), math::ivec2(1, 3), 0.5); to_string(m) + type_of(m)", "(0.5, 1.5)math::vec2", "ivec inputs give a vec");
            expect("to_string(math::unmix(math::vec2(0, 10), math::vec2(10, 20), math::vec2(5, 20)))", "(0.5, 1.0)", "unmix");
            expect("to_string(math::unmix(math::vec2(1, 1), math::vec2(1, 3), math::vec2(1, 2)))", "(0.0, 0.5)", "unmix over an empty range is 0");
            expect("to_string(math::mix_in(math::vec2(0, 0), math::vec2(8, 8), 0.5, 2.0)) + to_string(math::mix_out(math::vec2(0, 0), math::vec2(8, 8), 0.5, 2.0))",
                   "(2.0, 2.0)(6.0, 6.0)", "eased mixes");
            expect("to_string(math::mix_in_out(math::vec2(0, 0), math::vec2(8, 8), 0.25, 2.0)) + to_string(math::mix_out_in(math::vec2(0, 0), math::vec2(8, 8), 0.75, 2.0))",
                   "(1.0, 1.0)(5.0, 5.0)", "in-out and out-in");
            expect("to_string(math::unmix_in(math::vec2(0, 0), math::vec2(4, 4), math::vec2(1, 4), 2.0))", "(0.5, 1.0)", "unmix_in");
            expect("to_string(math::unmix_out(0.0, 4.0, 3.0, 2.0)) + to_string(math::unmix_in_out(0.0, 8.0, 2.0, 2.0)) + to_string(math::unmix_out_in(0.0, 8.0, 6.0, 2.0))",
                   "0.50.35355339059327380.8535533905932737", "the float eased unmixes");
            expect("to_string(math::remap(5.0, 0.0, 10.0, 100.0, 200.0)) + to_string(math::remap(math::vec2(0, 10), 0.0, 10.0, math::vec2(0, 0), math::vec2(1, 2)))",
                   "150.0(0.0, 2.0)", "remap");
            expect("to_string(math::remap(3.0, 1.0, 1.0, 7.0, 9.0))", "7.0", "remap over an empty range is the low end");
            expect("to_string(math::round(2.5)) + to_string(math::round(-2.5)) + to_string(math::round(math::vec2(1.4, 1.6)))", "3.0-3.0(1.0, 2.0)", "round (half away from zero)");
            expect("to_string(math::floor(math::vec3(1.5, -1.5, 2))) + to_string(math::ceil(math::vec2(1.2, -1.2)))", "(1.0, -2.0, 2.0)(2.0, -1.0)", "floor, ceil");
            expect("var f = math::floor(math::ivec2(3, 4)); to_string(f) + type_of(f)", "(3.0, 4.0)math::vec2", "floor of an ivec gives a vec");
            expect("to_string(math::abs(math::vec2(-1.5, 2))) + to_string(math::abs(math::ivec2(-3, 4)))", "(1.5, 2.0)(3, 4)", "abs keeps ivec exact");
            expect("to_string(math::min(math::vec2(1, 5), math::vec2(3, 2))) + to_string(math::max(math::ivec2(1, 5), 3))", "(1.0, 2.0)(3, 5)", "min, max, scalar broadcast");
            expect("to_string(math::clamp(math::vec3(-1, 0.5, 2), 0.0, 1.0)) + to_string(math::clamp(math::ivec2(-5, 9), math::ivec2(0, 0), math::ivec2(4, 4)))",
                   "(0.0, 0.5, 1.0)(0, 4)", "clamp");
            expect("var c = math::clamp(math::ivec2(-5, 9), 0.0, 4.0); to_string(c) + type_of(c)", "(0.0, 4.0)math::vec2", "a float bound gives a vec");
            expect("to_string(math::mix(0.0, 10.0, math::vec2(0.1, 0.9)))", "(1.0, 9.0)", "scalar ends, vector t");
            expect(raised("var q = math::mix(math::vec2(0, 0), math::vec3(1, 1, 1), 0.5);"), "math::mix expects vectors of the same dimensions", "dimensions must agree");
            expect(raised("var q = math::sqrt(math::vec2(4, 9));"), "math::sqrt expects a numeric argument", "sqrt stays scalar");
            expect(raised("var q = math::abs(math::ivec2(-2147483647 - 1, 0));"), "Integer overflow: the value does not fit an int32 component", "abs of INT32_MIN");
            expect("function f(math::vec3 a, math::vec3 b) -> math::vec3 { math::vec3 m = math::mix(a, b, 0.5); return math::floor(m); } to_string(f(math::vec3(0, 1, 2), math::vec3(3, 4, 6)))",
                   "(1.0, 2.0, 4.0)", "typed slots take the library's vectors");
        });

        test("equality_and_truthiness", [expect]() {
            expect("to_string(math::vec3(1, 2, 3) == math::vec3(1, 2, 3))", "true", "equal");
            expect("to_string(math::vec3(1, 2, 3) != math::vec3(1, 2, 4))", "true", "not equal");
            expect("to_string(math::vec3(1, 2, 3) == math::ivec3(1, 2, 3))", "true", "a vec equals an ivec of the same values");
            expect("to_string(math::vec2(1, 2) == math::vec3(1, 2, 0))", "false", "different dimensions");
            expect("to_string(math::vec4(1, 2, 3, 4) == math::vec4(1, 2, 3, 4)) + to_string(math::vec3(1, 2, 3) == math::vec4(1, 2, 3, 0))", "truefalse", "vec4 equality");
            expect("to_string(math::vec2(1, 2) == 1)", "false", "a scalar");
            expect("to_string(math::vec2(0, 0) == null)", "false", "null");
            expect("string s = \"\"; if (math::vec2()) { s = \"t\"; } else { s = \"f\"; } if (math::ivec2(0, 1)) { s += \"t\"; } s", "ft", "not all zero is truthy");
            expect("to_string(!math::vec3())", "true", "not");
            expect("bool b = math::ivec3(0, 0, 2); to_string(b)", "true", "a bool slot takes truthiness");
        });

        test("typed_slots", [expect, raised]() {
            expect("math::vec3 v = math::ivec3(1, 2, 3); to_string(v)", "(1.0, 2.0, 3.0)", "ivec into a vec slot");
            expect("math::ivec2 p = math::vec2(1.5, -2.5); to_string(p)", "(1, -2)", "vec into an ivec slot");
            expect("function f(math::vec3 p) { return p.x; } to_string(f(math::ivec3(7, 2, 3)))", "7.0", "a parameter converts");
            expect("math::ivec2 g() { return math::vec2(2.9, 1.1); } to_string(g())", "(2, 1)", "a return converts");
            expect("class H { math::ivec2 cell; } var h = H(); h.cell = math::vec2(3.5, 4.5); to_string(h.cell)", "(3, 4)", "a field converts");
            expect("string s = math::vec2(1, 2); s", "(1.0, 2.0)", "a string slot takes the text");
            expect("auto a = math::ivec2(1, 2); a = math::vec2(3.7, 4.2); to_string(a)", "(3, 4)", "auto infers the kind");
            expect("var a = math::ivec2(1, 2); a = 5; to_string(a)", "5", "var stays dynamic");
            expect("var five = 5; string m = \"\"; try { math::vec3 v = five; } catch (e) { m = e; } to_string(m.contains(\"declared math::vec3\"))", "true", "an int rejects, naming the declared type");
            expect("var w = math::vec2(1, 2); string m = \"\"; try { math::vec3 v = w; } catch (e) { m = e; } to_string(m.contains(\"declared math::vec3\"))", "true", "another dimension rejects");
        });

        test("containers_and_keys", [expect]() {
            expect("array<math::vec3> a = [math::vec3(1, 2, 3)]; a[0].x = 5; a.push(math::vec3()); to_string(a.size()) + to_string(a[0])", "2(5.0, 2.0, 3.0)", "a typed array");
            expect("array<math::ivec2> b = [math::vec2(1.5, 2.5)]; to_string(b[0])", "(1, 2)", "the element boundary converts");
            expect("map<math::ivec2, int> grid; grid[math::ivec2(1, 2)] = 5; grid[math::ivec2(1, 2)] += 1; to_string(grid[math::ivec2(1, 2)]) + to_string(grid.size())", "61", "a typed map keyed by ivec2");
            expect("var m = {}; m[math::ivec2(1, 2)] = \"a\"; m[math::ivec2(1, 2)]", "a", "a dynamic map key");
            expect("var m = {}; m[math::vec2(0.0, 1)] = 1; m[math::vec2(-0.0, 1)] = 2; to_string(m.size())", "2", "keys order floats totally, as float keys do");
            expect("var m = {}; m[math::vec2(1, 2)] = 1; m[math::vec3(1, 2, 0)] = 2; m[math::ivec2(1, 2)] = 3; to_string(m.size()) + to_string(m[math::vec3(1, 2, 0)])",
                   "32", "keys of different kinds are different keys");
            expect("var s = 0.0; for (auto v : [math::vec2(1, 2), math::vec2(3, 4)]) { s += v.x + v.y; } to_string(s)", "10.0", "range-for");
        });

        test("json", [expect]() {
            expect("to_json(math::vec3(1, 0.5, -2))", "{\"$type\":\"math::vec3\",\"$val\":{\"x\":1.0,\"y\":0.5,\"z\":-2.0}}", "to_json");
            expect("to_string(from_json(to_json(math::ivec3(1, -2, 3))))", "(1, -2, 3)", "round trip ivec3");
            expect("var v = from_json(to_json(math::vec2(0.1, 1e30))); to_string(v) + type_of(v)", "(0.1, 1e+30)math::vec2", "round trip binary32 exactly");
            expect("var a = from_json(to_json([math::ivec2(1, 2), 3])); to_string(a[0]) + to_string(a[1])", "(1, 2)3", "nested in an array");
            expect("var v = from_json(to_json(math::vec4(1, -2.5, 0.1, 4))); to_string(v) + type_of(v)", "(1.0, -2.5, 0.1, 4.0)math::vec4", "round trip vec4");
        });

        test("binary_archive", [expect]() {
            expect("var v = from_base64(to_base64(math::vec3(0.1, -2, 1e30))); to_string(v) + type_of(v)", "(0.1, -2.0, 1e+30)math::vec3", "binary32 bits round trip");
            expect("var p = from_base64(to_base64(math::ivec2(-2147483647 - 1, 7))); to_string(p)", "(-2147483648, 7)", "int32 bits round trip");
            expect("var q = from_base64(to_base64(math::vec4(0.1, 2, 3, -4))); to_string(q) + type_of(q)", "(0.1, 2.0, 3.0, -4.0)math::vec4", "vec4 round trip");
        });

        test("static_checker", [this]() {
            auto e = jai::engine::make();
            jai::stdlib::register_all(e);
            auto errors = [&](const std::string& src) {
                std::string all;
                for (const auto& d : e->check(src).diagnostics) {
                    if (d.severity == check_diagnostic::level::error) { all += d.message + "|"; }
                }
                return all;
            };
            check_eq(std::string(""), errors("math::vec3 v = math::vec3(1, 2, 3); float f = v.x + v.depth; math::ivec3 c = v; v.y = 2;"), "clean program");
            check_eq(std::string("math::vec3 has no member 'w'|"), errors("math::vec3 v; var q = v.w;"), "an unknown component");
            check_eq(std::string("math::vec2 has no member 'z'|"), errors("math::vec2 v; v.z = 1;"), "a missing component store");
            check(errors("math::vec3 v = 5;").find("math::vec3") != std::string::npos, "an int into a vec3 slot is a load error naming the type");
            check(errors("math::vec3 v = math::vec2(1, 2);").find("math::vec3") != std::string::npos, "another dimension is a load error");
            check_eq(std::string(""), errors("var d = math::vec2(1, 2); math::vec3 v = d;"), "a var source stays dynamic");
            check_eq(std::string(""), errors("math::vec3 a; math::vec3 b; math::vec3 m = math::mix(a, b, 0.5); math::ivec2 c = math::min(math::ivec2(1, 2), 3); float f = math::mix(1.0, 2.0, 0.5);"),
                     "the library types its vector results");
            check(errors("math::vec3 a; float f = math::floor(a);").find("float") != std::string::npos, "a vector result is not a float");
            check_eq(std::string(""), errors("var v = math::vec2(1, 2); var w = math::floor(v); math::vec2 x = w;"), "a var argument leaves the result dynamic");
        });
    }
};

} // namespace jai::foundry::tests

using math_types_tests = jai::foundry::tests::math_types_tests;
FOUNDRY_REGISTER(math_types_tests)
