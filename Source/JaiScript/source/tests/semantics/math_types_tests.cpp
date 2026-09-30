// The native math kinds (docs/math_types_design.md): math::vec2, math::vec3 (binary32 components) and math::ivec2,
// math::ivec3 (int32 components), inline in the value. Every row runs on both backends with proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/math/types.hpp>
#include <jaiscript/detail/aggregate_lowering.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class math_types_tests : public suite {
public:
    math_types_tests() : suite("Math Types") {}

    void forge_tests() override {
        auto run = [](const std::string& src, bool use_vm, bool proofs, bool lowering) {
            auto e = jai::engine::make();
            if (use_vm) { e->set_backend(jai::backend_type::vm); }
            e->type_proofs(proofs);
            e->aggregate_lowering(lowering);
            e->execution_budget(0);
            jai::stdlib::register_all(e);
            try { return e->execute(src).to_string(); }
            catch (const std::exception& ex) { return std::string("ERROR: ") + ex.what(); }
        };
        // every row on both backends, proofs on and off, typed vectors lowered and boxed
        auto expect = [this, run](const std::string& src, const std::string& expected, const char* what) {
            for (bool lowering : {true, false}) {
                for (bool proofs : {true, false}) {
                    for (bool use_vm : {false, true}) {
                        check_eq(expected, run(src, use_vm, proofs, lowering), std::string(what) + (use_vm ? " (vm" : " (interpreter") + (proofs ? "" : ", proofs off") + (lowering ? ")" : ", boxed)"));
                    }
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
            expect(raised("var v = math::vec3(); v.magnitude();"), "math::vec3 has no member 'magnitude'", "no such method");
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

        test("color", [expect, raised]() {
            expect("to_string(math::color()) + to_string(math::color(1, 0, 0)) + to_string(math::color(0.5, 0.25, 0, 0.5)) + type_of(math::color())",
                   "(1.0, 1.0, 1.0, 1.0)(1.0, 0.0, 0.0, 1.0)(0.5, 0.25, 0.0, 0.5)math::color", "construction: opaque white by default, alpha 1 when omitted");
            expect("math::color c = math::color(0.25, 0.5, 0.75, 0.5); c.g = 0.125; c.a *= 2; to_string(c.r + c.g * 10 + c.b * 100 + c.a * 1000)",
                   "1076.5", "the r g b a channels");
            expect(raised("var c = math::color(); var q = c.x;"), "math::color has no member 'x'", "no x on a color");
            expect("function f(math::color p) -> math::color { p.r = p.r + 1; p.a -= 0.5; return p; } to_string(f(math::color()))", "(2.0, 1.0, 1.0, 0.5)", "a channel store on a parameter lands in its slot");
            expect(raised("var c = math::color(1, 2);"), "math::color expects 0, 1, 3 or 4 arguments", "arity");
            expect("to_string(math::color(0.25, 0.5, 0.5, 1) * 2)", "(0.5, 1.0, 1.0, 2.0)", "arithmetic over all four channels");
            expect(raised("var q = math::color() + math::vec4(1, 1, 1, 1);"), "Invalid operands for + operator", "a color and a vec4 do not add");
            expect("to_string(math::color() == math::color(1, 1, 1, 1)) + to_string(math::color() == math::vec4(1, 1, 1, 1))", "truefalse", "a color never equals a vec4");
            expect("var c = math::color(); string m = \"\"; try { math::vec4 v = c; } catch (e) { m = e; } to_string(m.contains(\"declared math::vec4\"))", "true", "a color does not convert to a vec4");
            expect("to_json(math::color(1, 0.5, 0, 1))", "{\"$type\":\"math::color\",\"$val\":{\"r\":1.0,\"g\":0.5,\"b\":0.0,\"a\":1.0}}", "JSON channels");
            expect("var c = from_json(to_json(math::color(0.1, 0.2, 0.3, 0.4))); to_string(c) + type_of(c)", "(0.1, 0.2, 0.3, 0.4)math::color", "JSON round trip");
            expect("var c = from_base64(to_base64(math::color(0.1, 0.2, 0.3, 0.4))); to_string(c)", "(0.1, 0.2, 0.3, 0.4)", "binary round trip");
            // OKLab (reference values from an independent implementation of Ottosson's transform)
            expect("var m = math::mix(math::color(1, 0, 0), math::color(0, 0, 1), 0.5); "
                   "to_string(math::abs(m.r - 0.55044109) < 1e-5 && math::abs(m.g - 0.32562068) < 1e-5 && math::abs(m.b - 0.63650066) < 1e-5 && m.a == 1.0)",
                   "true", "mix(red, blue, 0.5) in OKLab");
            expect("var m = math::mix(math::color(1, 1, 1), math::color(0, 0, 0), 0.5); "
                   "to_string(math::abs(m.r - 0.38857287) < 1e-5 && math::abs(m.g - m.r) < 1e-6 && math::abs(m.b - m.r) < 1e-6)",
                   "true", "mix(white, black, 0.5) is the perceptual mid-gray");
            expect("function near(math::color a, math::color b) -> bool { return math::abs(a.r - b.r) < 1e-5 && math::abs(a.g - b.g) < 1e-5 && math::abs(a.b - b.b) < 1e-5; } "
                   "bool ok = true; for (auto c : [math::color(1, 0, 0), math::color(0, 1, 0), math::color(0, 0, 1), math::color(0.2, 0.4, 0.6)]) { ok = ok && near(math::mix(c, c, 0.3), c); } to_string(ok)",
                   "true", "primaries round trip within 1e-5");
            expect("var r = math::color(1, 0, 0); var b = math::color(0, 0, 1); to_string(math::mix_in(r, b, 0.5, 2.0) == math::mix(r, b, 0.25)) + to_string(math::lerp(r, b, 0.5) == math::mix(r, b, 0.5))",
                   "truetrue", "the eased mixes weight OKLab");
            expect("to_string(math::mix(math::color(0, 0, 0, 0), math::color(0, 0, 0, 1), 0.25).a)", "0.25", "alpha blends linearly");
            expect("to_string(math::mix_rgb(math::color(1, 0, 0), math::color(0, 0, 1), 0.5))", "(0.5, 0.0, 0.5, 1.0)", "mix_rgb is per channel");
            expect("var m = math::mix_hsv(math::color(1, 0, 0.16666667), math::color(1, 0.16666667, 0), 0.5); to_string(math::abs(m.r - 1.0) < 1e-5 && m.g < 1e-5 && m.b < 1e-5)",
                   "true", "mix_hsv takes the shortest arc across 0/360");
            expect("var m = math::mix_hsv(math::color(0.5, 0.5, 0.5), math::color(1, 0, 0), 0.5); to_string(math::abs(m.r - 0.75) < 1e-5 && math::abs(m.g - 0.375) < 1e-5 && math::abs(m.b - 0.375) < 1e-5)",
                   "true", "an achromatic endpoint takes the other's hue");
            expect("to_string(math::mix_hsv(math::color(1, 0, 0), math::color(0, 0, 1), 0.5))", "(1.0, 0.0, 1.0, 1.0)", "red to blue through magenta");
            expect(raised("var q = math::mix(math::color(), math::color(), math::vec4(0.5, 0.5, 0.5, 0.5));"), "math::mix on colors expects (math::color, math::color, float)", "t is a float for colors");
            expect(raised("var q = math::mix(math::color(), math::vec4(), 0.5);"), "math::mix on colors expects (math::color, math::color, float)", "a color and a vector do not mix");
            expect(raised("var q = math::min(math::color(), math::vec4());"), "a math::color combines only with colors and scalars", "nor combine per channel");
            expect("var c = math::clamp(math::color(2, -1, 0.5, 1), 0.0, 1.0); to_string(c) + type_of(c)", "(1.0, 0.0, 0.5, 1.0)math::color", "per-channel rows keep the color");
        });

        test("geometry", [expect, raised]() {
            expect("to_string(math::dot(math::vec3(1, 2, 3), math::vec3(4, 5, 6))) + to_string(math::vec3(1, 2, 3).dot(math::vec3(4, 5, 6)))", "32.032.0", "dot, free and method");
            expect("var d = math::dot(math::ivec2(2, 3), math::ivec2(4, 5)); to_string(d) + type_of(d)", "23int", "dot of ivecs is an exact int");
            expect("to_string(math::dot(math::vec2(0.5, 1), math::ivec2(2, 3)))", "4.0", "a vec and an ivec dot as floats");
            expect("to_string(math::cross(math::vec3(1, 0, 0), math::vec3(0, 1, 0))) + to_string(math::vec3(0, 1, 0).cross(math::vec3(1, 0, 0)))", "(0.0, 0.0, 1.0)(0.0, 0.0, -1.0)", "cross");
            expect("var c = math::cross(math::ivec3(1, 2, 3), math::ivec3(4, 5, 6)); to_string(c) + type_of(c)", "(-3, 6, -3)math::ivec3", "cross of ivec3 stays exact");
            expect("to_string(math::cross(math::vec2(1, 2), math::vec2(3, 4))) + to_string(math::cross(math::ivec2(1, 2), math::ivec2(3, 4)))", "-2.0-2", "the 2D cross is a scalar");
            expect("var v = math::vec3(3, 4, 0); to_string(v.length()) + to_string(math::length_squared(v)) + to_string(math::length(v))", "5.025.05.0", "length");
            expect("var p = math::ivec2(3, 4); to_string(p.length_squared()) + type_of(p.length_squared()) + to_string(p.length())", "25int5.0", "an ivec's length_squared is exact, its length a float");
            expect("to_string(math::normalize(math::vec2(3, 4))) + to_string(math::vec2(0, -2).normalized())", "(0.6, 0.8)(0.0, -1.0)", "normalize");
            expect("to_string(math::normalize(math::vec3())) + to_string(math::vec3().normalized())", "(0.0, 0.0, 0.0)(0.0, 0.0, 0.0)", "the zero vector normalizes to zero");
            expect("var n = math::ivec2(0, 5).normalized(); to_string(n) + type_of(n)", "(0.0, 1.0)math::vec2", "normalizing an ivec gives a vec");
            expect("to_string(math::distance(math::vec2(0, 0), math::vec2(3, 4))) + to_string(math::distance_squared(math::vec2(1, 1), math::vec2(4, 5))) + to_string(math::vec2(0, 0).distance_to(math::vec2(3, 4)))",
                   "5.025.05.0", "distance over vectors");
            expect("var d = math::distance_squared(math::ivec3(1, 1, 1), math::ivec3(4, 5, 1)); to_string(d) + type_of(d) + to_string(math::distance(0, 0, 3, 4))", "25int5.0", "exact ivec distance_squared; the scalar form stays");
            expect("to_string(math::reflect(math::vec2(1, -1), math::vec2(0, 1)))", "(1.0, 1.0)", "reflect");
            expect("to_string(math::project(math::vec2(2, 3), math::vec2(4, 0))) + to_string(math::project(math::vec2(2, 3), math::vec2()))", "(2.0, 0.0)(0.0, 0.0)", "project, onto zero is zero");
            expect("to_string(math::angle_between(math::vec2(1, 0), math::vec2(0, 1))) + to_string(math::angle_between(math::vec2(1, 0), math::vec2()))", "1.57079632679489660.0", "angle_between in radians; a zero vector gives 0");
            expect("to_string(math::angle_between(math::vec3(1, 1, 1), math::vec3(2, 2, 2)) < 1e-7) + to_string(math::abs(math::angle_between(math::vec2(1, 0), math::vec2(-1, 0)) - 3.141592653589793) < 1e-12)",
                   "truetrue", "the cosine is clamped");
            expect("var p = math::perpendicular(math::ivec2(1, 2)); to_string(math::perpendicular(math::vec2(1, 2))) + to_string(p) + type_of(p)", "(-2.0, 1.0)(-2, 1)math::ivec2", "perpendicular");
            expect("var r = math::rotate(math::vec2(1, 0), 1.5707963267948966); var s = math::vec2(1, 0).rotated(3.141592653589793); "
                   "to_string(math::abs(r.x) < 1e-7 && math::abs(r.y - 1) < 1e-7 && math::abs(s.x + 1) < 1e-7 && math::abs(s.y) < 1e-7)", "true", "rotate a vec2");
            expect("var r = math::rotate(math::vec3(1, 0, 0), math::vec3(0, 0, 2), 1.5707963267948966); var s = math::vec3(0, 1, 0).rotated(math::vec3(1, 0, 0), 3.141592653589793); "
                   "to_string(math::abs(r.x) < 1e-7 && math::abs(r.y - 1) < 1e-7 && math::abs(r.z) < 1e-7 && math::abs(s.y + 1) < 1e-7 && math::abs(s.z) < 1e-6)", "true", "rotate a vec3 about an axis (Rodrigues; the axis is normalized)");
            expect("to_string(math::rotate(math::vec3(1, 2, 3), math::vec3(), 1.0)) + type_of(math::rotate(math::ivec2(1, 0), 0.5))", "(1.0, 2.0, 3.0)math::vec2", "a zero axis leaves the vector; an ivec rotates into a vec");
            expect("var f = math::vec3(3, 4, 0).length; to_string(f())", "5.0", "a method read is a function value");
            expect("function speed(math::vec3 v) -> float { return v.length(); } math::vec3 n = math::vec3(0, 3, 4).normalized(); float d = n.dot(math::vec3(0, 1, 0)); to_string(speed(math::vec3(0, 3, 4))) + to_string(d)",
                   "5.00.6000000238418579", "typed slots take the results");
            expect(raised("var q = math::dot(math::vec2(), math::vec3());"), "math::dot expects two vectors of the same dimensions", "dimensions must agree");
            expect(raised("var q = math::dot(math::color(), math::color());"), "math::dot expects two vectors of the same dimensions", "a color has no geometry");
            expect(raised("var q = math::cross(math::vec4(), math::vec4());"), "math::cross expects two vec2 or two vec3 vectors", "no 4D cross");
            expect(raised("var q = math::perpendicular(math::vec3());"), "math::perpendicular expects a vec2", "perpendicular is 2D");
            expect(raised("var q = math::rotate(math::vec3(1, 0, 0), 1.0);"), "math::rotate expects (vec2, angle) or (vec3, axis, angle)", "a vec3 rotates about an axis");
            expect(raised("var q = math::length(5);"), "math::length expects a vector", "a scalar has no length here");
            expect(raised("var q = math::distance(1, 2, 3);"), "math::distance expects 2 vectors or 4 arguments (x1, y1, x2, y2)", "distance arity");
            expect(raised("var v = math::vec3(); var q = v.dot();"), "'dot' expects 1 argument", "method arity");
            expect(raised("var v = math::vec3(); var q = v.rotated(1.0);"), "'rotated' expects 2 arguments (axis, angle)", "a vec3 rotates about an axis, as a method too");
            expect(raised("var v = math::vec3(); var q = v.foo();"), "math::vec3 has no member 'foo'", "an unknown method");
            expect(raised("var c = math::color(); var q = c.length();"), "math::color has no member 'length'", "no geometry methods on a color");
            expect(raised("var q = math::length_squared(math::ivec3(-2147483647 - 1, -2147483647 - 1, 0));"), "Integer overflow in '+'", "an exact sum follows the integer policy");
        });

        // the boxed interpreter is the oracle: every other configuration prints the same text
        auto agree = [this, run](const std::string& src, const char* what) {
            const std::string oracle = run(src, false, true, false);
            check(oracle.rfind("ERROR: static check", 0) != 0, std::string(what) + ": the program checks (" + oracle + ")");
            for (bool lowering : {true, false}) {
                for (bool proofs : {true, false}) {
                    for (bool use_vm : {false, true}) {
                        check_eq(oracle, run(src, use_vm, proofs, lowering), std::string(what) + (use_vm ? " (vm" : " (interpreter") + (proofs ? "" : ", proofs off") + (lowering ? ")" : ", boxed)"));
                    }
                }
            }
        };

        test("lowering", [expect, agree, raised, this]() {
            // statements over typed locals in a function body
            agree("function f() -> string { math::vec3 p = math::vec3(1, 2, 3); math::vec3 v = math::vec3(0.5, 0.25, 2); float dt = 0.1; "
                  "for (int i = 0; i < 5; i++) { p += v * dt; v *= 0.99; p.y -= 0.01; } return to_string(p) + to_string(v) + to_string(p.x + v.z); } f()", "an integrator");
            agree("function f() -> string { math::vec2 p = math::vec2(1.5, -2); p = math::vec2(p.y, p.x); p = -p + p * 2; p /= 4; return to_string(p); } f()", "a value that reads its target");
            agree("function f() -> string { math::ivec3 a = math::ivec3(7, -9, 4); a.x += 5; a.y *= -3; a.z /= 3; a = a % 4; a = a - math::ivec3(1, 1, 1); a = 2 * a; return to_string(a) + to_string(-a); } f()", "ivec rows");
            agree("function f() -> string { math::vec3 v = math::vec3(1.9, -1.9, 1e10); math::ivec3 c = math::ivec3(v.x, v.y, 0); math::vec3 w = c; math::vec3 u = math::vec3(16777217, 0.1, true); return to_string(c) + to_string(w) + to_string(u); } f()", "conversions and narrowing");
            agree("function f() -> string { math::vec4 q = math::vec4(math::vec3(1, 2, 3), 4); math::vec3 e = math::vec3(math::vec2(5, 6), 7); math::ivec2 g = math::ivec2(math::vec2(2.5, -3.5)); return to_string(q) + to_string(e) + to_string(g); } f()", "extension and conversion constructors");
            agree("function f() -> string { math::vec2 a = math::vec2(0, 10); math::vec2 b = math::vec2(10, 20); math::vec2 m = math::mix(a, b, 0.25); math::vec2 l = math::lerp(a, b, math::vec2(0.5, 0.1)); "
                  "math::vec2 c = math::clamp(m, 1.0, 12.0); math::vec2 r = math::floor(l + 0.5); math::ivec2 i = math::min(math::ivec2(3, 9), math::ivec2(5, 2)); math::vec2 e = math::mix_in_out(a, b, 0.3, 2.0); "
                  "math::vec2 u = math::remap(m, 0.0, 10.0, a, b); return to_string(m) + to_string(l) + to_string(c) + to_string(r) + to_string(i) + to_string(e) + to_string(u) + to_string(math::abs(-m)); } f()", "the component-wise family");
            agree("function f() -> string { math::vec3 p = math::vec3(3, 0, 4); math::vec3 n = p.normalized(); math::vec3 z = math::vec3().normalized(); math::vec3 c = math::cross(p, math::vec3(0, 1, 0)); "
                  "float d = math::dot(p, n) + p.length() + p.length_squared() + p.distance_to(n) + math::distance_squared(p, c); return to_string(n) + to_string(z) + to_string(c) + to_string(d) + to_string(p.cross(n)); } f()", "the geometry rows");
            agree("function f() -> string { math::ivec3 a = math::ivec3(2, 3, 4); math::ivec3 b = math::ivec3(-1, 5, 2); math::ivec2 h = math::ivec2(3, -7); var d = math::dot(a, b); var c = math::cross(a, b); "
                  "var p = math::perpendicular(h); var l = a.length_squared(); var s = math::distance_squared(a, b); return to_string(d) + type_of(d) + to_string(c) + to_string(p) + to_string(l) + to_string(s) + to_string(math::cross(h, math::ivec2(1, 1))); } f()", "exact ivec geometry");
            agree("function f() -> string { math::vec3 v = math::vec3(1, -2, 0.5); math::vec3 n = math::vec3(0, 1, 0); math::vec3 axis = math::vec3(1, 1, 0.25); math::vec2 p = math::vec2(3, -1); math::ivec3 iv = math::ivec3(4, -5, 6); float t = 0.7; "
                  "math::vec3 r = math::reflect(v, n); math::vec3 j = math::project(v, axis); math::vec3 z = math::project(v, math::vec3()); math::vec2 q = math::rotate(p, t); math::vec2 q2 = p.rotated(2); "
                  "math::vec3 w = math::rotate(v, axis, t); math::vec3 w2 = v.rotated(math::vec3(), t); math::vec3 w3 = math::rotate(iv, axis, 1.25); float a = math::angle_between(v, axis) + math::angle_between(v, math::vec3()) + math::angle_between(iv, v); "
                  "return to_string(r) + to_string(j) + to_string(z) + to_string(q) + to_string(q2) + to_string(w) + to_string(w2) + to_string(w3) + to_string(a) + to_string(math::reflect(iv, n)); } f()", "reflect, project, rotate and angle_between");
            agree("function f() -> string { math::ivec3 big = math::ivec3(2000000000, 2000000000, 2000000000); math::vec3 n = big.normalized(); return to_string(big.length()) + to_string(n) + to_string(math::distance(big, -big)); } f()", "ivec components widen where the kernel reads doubles");
            agree("function f() -> string { math::color c = math::color(0.25, 0.5, 0.75, 0.5); c.g = 0.125; c.a *= 2; math::color m = math::mix(c, math::color(1, 0, 0), 0.5); math::color k = math::clamp(c * 3, 0.0, 1.0); "
                  "math::color r = math::mix_rgb(c, m, 0.5); return to_string(c) + to_string(m) + to_string(k) + to_string(r) + to_string(c == m); } f()", "colors lowered, OKLab boxed");
            agree("function g(math::vec3 q) -> float { float s = 0.0; for (int i = 0; i < 4; i++) { q.x += 1; s += q.x; } return s + q.length(); } to_string(g(math::vec3(1, 2, 3)))", "a parameter unpacks at entry");
            agree("function f() -> string { math::vec2 p = math::vec2(1, 2); string s = \"\"; if (p.x > 0.5) p += math::vec2(1, 1); else p.x = 0; while (p.y < 10.0) { p.y *= 2; } "
                  "switch (1) { case 1: math::vec2 k = p * 2; s += to_string(k); break; default: break; } if (p == math::vec2(2, 12)) { s += \"eq\"; } if (p != p) { s += \"ne\"; } if (p) { s += \"t\"; } return s + to_string(p); } f()", "statement forms");
            agree("function f() -> string { math::vec3 p = math::vec3(1, 2, 3); var fn = p.length; auto t = [&]() { p.x = 7; }; t(); var arr = [p, p + p]; return to_string(fn()) + to_string(p) + to_string(arr[1]); } f()", "escapes stay boxed");
            agree("function h(math::vec3& r) -> void { r.y = 9; } function f() -> string { math::vec3 p = math::vec3(1, 2, 3); h(p); math::vec3 q = p; q.z = 5; return to_string(p) + to_string(q); } f()", "a reference argument");
            agree("function f() -> string { math::vec2 a = math::vec2(1e30, 1); math::vec2 b = a * 1e30; math::vec2 z = math::vec2(0, 0) * -1.0; return to_string(b) + to_string(z) + to_string(b - b); } f()", "infinities, NaN and negative zero");
            // raising statements leave their target as it was, with the boxed texts
            expect("function f() -> string { math::ivec2 a = math::ivec2(2147483647, 5); string m = \"\"; try { a = a + math::ivec2(1, 0); } catch (e) { m = e; } return m + to_string(a); } f()",
                   "Integer overflow in '+'(2147483647, 5)", "an ivec overflow");
            expect("function f() -> string { math::ivec2 a = math::ivec2(4, 5); string m = \"\"; try { a = a / math::ivec2(2, 0); } catch (e) { m = e; } return m + to_string(a); } f()",
                   "Division by zero in integer operation(4, 5)", "an ivec division by zero");
            expect("function f() -> string { math::vec2 v = math::vec2(4, 5); string m = \"\"; try { v = v / math::vec2(2, 0); } catch (e) { m = e; } return m + to_string(v); } f()",
                   "Division by zero in float operation(4.0, 5.0)", "a vec division by zero");
            expect("function f() -> string { math::vec3 v = math::vec3(1, 2, 3); string m = \"\"; try { v.y = \"a\"; } catch (e) { m = e; } return m + to_string(v); } f()",
                   "Cannot store a non-numeric value in a math::vec3 component(1.0, 2.0, 3.0)", "a component store's text");
            expect("[[warn_mode]] function f() -> string { math::vec3 v = math::vec3(1, 2, 3); string m = \"\"; try { var w = v.w; } catch (e) { m = e; } try { v.w = 1; } catch (e) { m += \"|\" + e; } return m; } f()",
                   "math::vec3 has no member 'w'|math::vec3 has no member 'w'", "an unknown component");
            expect("function f() -> string { math::vec3 v = math::vec3(1, 2, 3); var s = \"x\"; string m = \"\"; try { v = s; } catch (e) { m = e; } return to_string(m.contains(\"declared math::vec3\")) + to_string(v); } f()",
                   "true(1.0, 2.0, 3.0)", "a typed store's rejection names the local");
            expect("function f() -> string { math::ivec2 a = math::ivec2(1, 2); var big = 3000000000; string m = \"\"; try { a.x = big; } catch (e) { m = e; } try { a = math::ivec2(1e100, 0); } catch (e) { m += \"|\" + e; } return m + to_string(a); } f()",
                   "Integer overflow: the value does not fit an int32 component|Cannot convert float to integer: non-finite or out of range(1, 2)", "int32 narrowing texts");
            // the VM runs component slots
            auto e = jai::engine::make();
            e->set_backend(jai::backend_type::vm);
            e->execution_budget(0);
            std::string listings;
            e->disassembly_sink([&](const std::string& listing, const std::string&) { listings += listing; });
            e->execute("function f() -> float { math::vec3 p = math::vec3(1, 2, 3); for (int i = 0; i < 3; i++) { p += math::vec3(1, 1, 1); } return p.x; } f();");
            // the debugger's locals: component slots fold back into the source-level vector, temporaries drop
            std::vector<std::pair<std::string, script_value>> locals = {{"p", script_value(std::monostate{}, e.get())}, {"n", script_value(script_int(4), e.get())},
                {"p$x", script_value(1.0, e.get())}, {"p$y", script_value(2.5, e.get())}, {"p$z", script_value(3.0, e.get())}, {"$t0", script_value(9.0, e.get())},
                {"c$x", script_value(script_int(7), e.get())}, {"c$y", script_value(script_int(-1), e.get())}};
            jai::detail::fold_lowered_locals(locals, e.get());
            std::string shown;
            for (const auto& [name, value] : locals) { shown += name + "=" + value.to_string() + " "; }
            check_eq(std::string("p=(1.0, 2.5, 3.0) n=4 c=(7, -1) "), shown, "lowered locals fold for the debugger");
            check(listings.find("T_ADD_FF") != std::string::npos && listings.find("C_MATH") != std::string::npos, "the lowered local runs as float slots in a cell block: " + listings.substr(0, 2000));
            listings.clear();
            e->execute("function g() -> float { math::vec3 v = math::vec3(1, 2, 3); math::vec3 w = math::rotate(v, math::vec3(0, 0, 1), 0.5); float a = math::angle_between(v, w); return w.x + a; } g();");
            const std::string rotate_call = "MATH " + std::to_string(static_cast<int>(jai::detail::math_fn::rotate_)) + " ";
            const std::string angle_call = "MATH " + std::to_string(static_cast<int>(jai::detail::math_fn::angle_between_)) + " ";
            check(listings.find("T_MUL_FF") != std::string::npos && listings.find(rotate_call) == std::string::npos && listings.find(angle_call) == std::string::npos,
                  "rotate and angle_between run as component arithmetic, no boxed call");
            (void)raised;
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
            check_eq(std::string(""), errors("math::color c = math::mix(math::color(), math::color(0, 0, 0), 0.5); math::color d = math::mix_hsv(c, c, 0.5); float r = c.r;"),
                     "colors type through the library");
            check(errors("math::vec4 v = math::color();").find("math::vec4") != std::string::npos, "a color into a vec4 slot is a load error");
            check_eq(std::string("math::color has no member 'x'|"), errors("math::color c; var q = c.x;"), "a vector name on a color");
            check_eq(std::string(""), errors("var v = math::vec2(1, 2); var w = math::floor(v); math::vec2 x = w;"), "a var argument leaves the result dynamic");
            check_eq(std::string(""), errors("math::vec3 a; math::vec3 b; float d = a.dot(b); math::vec3 n = a.normalized(); math::vec3 c = math::cross(a, b); int e = math::ivec2(1, 2).length_squared(); "
                                             "math::vec2 r = math::ivec2(1, 0).rotated(0.5); float l = math::length(a) + a.distance_to(b) + math::angle_between(a, b); int k = math::dot(math::ivec3(), math::ivec3());"),
                     "the geometry library types its results");
            check_eq(std::string("math::vec3 has no member 'foo'|"), errors("math::vec3 v; var q = v.foo();"), "an unknown method");
            check_eq(std::string("'dot' expects 1 argument|"), errors("math::vec3 v; var q = v.dot();"), "a method's arity");        });
    }
};

} // namespace jai::foundry::tests

using math_types_tests = jai::foundry::tests::math_types_tests;
FOUNDRY_REGISTER(math_types_tests)
