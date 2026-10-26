// What a constructor level's initializer-list arguments and field initializers see: `this`, the constructor's parameters by name
// (as bound at the level: the call's bindings at the class constructed, the raw initializer arguments at a base), then the
// definition environment. The vm runs them in one frame per level with the parameters in slots, the interpreter in the level's
// environment; every case holds both backends to one result, and to the stated value where it is fixed.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class construction_scope_tests : public suite {
public:
	construction_scope_tests() : suite("Construction Scope") {}

	static std::string run(bool vm, const std::string& program) {
		auto e = engine::make();
		e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
		stdlib::register_all(*e);
		try { return e->execute(program).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void both(const std::string& program, const std::string& expected, const std::string& what) {
		const std::string interp = run(false, program);
		const std::string vm = run(true, program);
		check_eq(interp, vm, what + ": both backends");
		if (!expected.empty()) { check_eq(expected, vm, what); }
		const std::string again = run(true, program + "\n");   // a second engine compiles the chunks afresh
		check_eq(vm, again, what + ": repeatable");
	}

	void forge_tests() override {
		test("a_field_initializer_reads_a_constructor_parameter_by_name", [this]() {
			both("class A { int x = n + 1; A(int n) {} } A(5).x;", "6", "a parameter");
			both("int v = 100; class B { int v = 1; int w = v; B(int v) {} } B(7).w;", "", "a parameter over a member and a global");
			both("class C { int x = 0; C(int n) { x = n; } } class D : C { int y = n * 2; D(int n) : super(n + 1) {} } auto d = D(4); to_string(d.x) + \" \" + to_string(d.y);",
			     "5 8", "the class's parameters at its own level, the argument at the base");
		});

		test("an_omitted_parameter_is_absent_as_in_the_environment", [this]() {
			both("int n = 40; class C { int x = n; C(int n = 5) {} } to_string(C().x) + \" \" + to_string(C(9).x);", "", "a defaulted parameter");
		});

		test("a_base_level_sees_the_argument_as_it_was_passed", [this]() {
			both("class Base { var f = x * 2; Base(float x) {} } class D : Base { D() : super(3) {} } to_string(D().f);", "", "an int argument to a float parameter");
			both("class P { int a = 0; P(int x) { a = x; } } class Q : P { int b = k; Q(int k) : super(k * 10) {} } auto q = Q(2); to_string(q.a) + \" \" + to_string(q.b);",
			     "20 2", "each level's own parameters");
		});

		test("delegation_and_further_bases_take_their_arguments_in_the_level", [this]() {
			both("class E { int a = 1; int b = a + k; E(int k) {} E() : this(10) {} } E().b;", "11", "a delegated-to constructor's parameter");
			both("class L { int l = 0; L(int v) { l = v; } } class R { int r = 0; R(int v) { r = v; } } "
			     "class M : L, R { int m = p + q; M(int p, int q) : L(p), R(q * 2) {} } auto x = M(3, 4); to_string(x.l) + \" \" + to_string(x.r) + \" \" + to_string(x.m);",
			     "3 8 7", "two bases named in the list");
		});

		test("an_initializer_runs_against_the_class_under_construction", [this]() {
			both("class G { int v = who(); int who() { return 1; } } class H : G { int who() override { return 2; } H() : super() {} } H().v;", "1",
			     "a virtual call in a base's field initializer");
			both("class I { int a = 3; int b = a * 2; I(int z) {} } I(0).b;", "6", "a field read in a later initializer");
		});

		test("an_initializer_that_raises_raises_as_before", [this]() {
			both("class T { int x = [1, 2][n]; T(int n) {} } string r = \"made\"; try { T(7); } catch (err) { r = to_string(err); } r;", "", "an index past the end in a field initializer");
			both("function fail(int n) -> int { throw \"no \" + to_string(n); } class U { int x = 0; U(int v) { x = v; } } class V : U { V(int n) : super(fail(n)) {} } "
			     "string r = \"made\"; try { V(4); } catch (err) { r = to_string(err); } r;", "no 4", "a throw in an initializer-list argument");
			both("class W { int x = q; W(int n) {} } W(1).x;", "", "an unknown name in a field initializer");
		});

		test("a_closure_in_an_initializer_keeps_the_level_environment", [this]() {
			both("class F { var g = [n](int q) { return q + n; }; F(int n) {} } F(5).g(1);", "6", "a lambda capturing a parameter");
		});

		test("an_argument_is_captured_where_the_level_takes_it", [this]() {
			both("class P { array<int> a = []; P(array<int> x) { x.push(1); a = x; } } class Q : P { array<int> keep = []; Q(array<int> y) : super(y) { keep = y; } } "
			     "auto q = Q([0]); to_string(q.a.size()) + \" \" + to_string(q.keep.size());", "", "a by-value argument to the base");
		});

		test("constructions_repeat_with_their_own_parameters", [this]() {
			both("class S { int s = k * k; S(int k) {} } int t = 0; for (int i = 0; i < 200; ++i) { t += S(i).s; } t;", "2646700", "two hundred constructions");
		});
	}
};

}

using construction_scope_tests = jai::foundry::tests::construction_scope_tests;
FOUNDRY_REGISTER(construction_scope_tests)
