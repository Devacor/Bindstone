#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include "../case_table.hpp"

#include <string>
#include <utility>

using namespace jai::foundry;

namespace jai::foundry::tests {

// A throw out of an operator, a conversion or a call an expression runs ends the statement there: the thrown value is what
// the catch receives, whatever the statement would have written keeps its value, and nothing reaches the script error
// handler. Every expected text is the site's rule (the right side before every part of the target), never a backend's output.
class operator_unwind_tests : public suite {
public:
	operator_unwind_tests() : suite("Operator Unwind") {}

	static constexpr const char* k_prelude = R"jai(
		string LOG = "";
		class Token { string name = ""; Token(string a) { name = a; } ~Token() { LOG += name; } }
		int deep(int o) { if (o > 0) { return deep(o - 1); } throw "deep"; }
		class C {
			int n = 7;
			C() {}
			C(int a) { n = a; }
			auto operator+(int o) { throw "boom"; }
			auto operator+(C o) { throw "boom2"; }
			auto operator-(int o) { return deep(o); }
			auto operator*(int o) { Token t = Token("t"); throw "star"; }
			auto operator/(int o) { array<int> xs; return xs[o]; }
			auto operator%(int o) { throw "mod"; }
			auto operator<(int o) { throw "less"; }
			auto operator==(int o) { throw "eq"; }
			auto operator-() { throw "neg"; }
			auto operator[](int i) { throw "index"; }
			auto operator()(int a) { throw "call"; }
		}
		class D { int k = 1; }
		class T { string to_string() { throw "ts"; } }
		class B { bool to_bool() { throw "tobool"; } }
		class P { int n = 7; P() {} P(int a) { n = a; } auto operator+=(int o) { throw "pluseq"; } auto operator++() { throw "inc"; } auto operator++(int) { throw "postinc"; } }
		class K { int n = 1; K() {} K(int a) { throw "conv"; } }
		class Q { int n = 0; Q(int a) { n = a; } bool operator<(Q o) { throw "qless"; } }
		int thrower(int i) { if (i == 1) { throw "T"; } return i + 10; }
		float fthrower(int i) { if (i == 1) { throw "T"; } return i + 0.5; }
	)jai";

	// Each {code, expected} runs after the prelude as the runner's backend builds it, with proofs off, and with proofs and passes
	// off: expected is what it prints, or "ERROR <text>" for the error that leaves execute
	template<typename Cases>
	void expect_printed(const Cases& a_cases) {
		check_each(a_cases, [](const auto& c) {
			for (int variant = 0; variant < 3; ++variant) {
				auto eng = make_engine();
				jai::stdlib::register_all(eng);
				if (variant >= 1) { eng->type_proofs(false); }
				if (variant == 2) { eng->ir_passes(false); }
				std::string reported;
				eng->set_script_error_handler([&reported](const std::string& text) { reported += text; });
				const char* label = variant == 0 ? "as built" : variant == 1 ? "proofs off" : "proofs and passes off";
				check_eq(std::string(c.second), printed_output(*eng, std::string(k_prelude) + c.first), label);
				check_eq(std::string(), reported, label);
			}
		});
	}

	void forge_tests() override {
		test("a_throwing_operator_leaves_the_assigned_variable_untouched", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); try { e = e + 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { var e = C(5); try { e = e + 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(C e = C(5); try { e = e + 3; } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(var e = C(5); try { e = e + 3; } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(function f() { C e = C(5); C o = C(1); try { e = e + o; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom2 5"},
				{R"jai(function f() { C e = C(5); try { e = e + C(3); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom2 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e + 3; } catch (x) { print(x, " ", r); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); int r = 4; try { r = e + 3; } catch (x) { print(x, " ", r); } } f();)jai", "boom 4"},
				{R"jai(function f() { C e = C(5); D d; try { d = e + 3; } catch (x) { print(x, " ", d.k); } } f();)jai", "boom 1"},
				{R"jai(C e = C(5); D d; try { d = e + 3; } catch (x) { print(x, " ", d.k); })jai", "boom 1"},
				{R"jai(C e = C(5); var r = 5; try { r = e + 3; } catch (x) { print(x, " ", r); })jai", "boom 5"},
				{R"jai(function f() { C e = C(5); int i = 0; try { while (i < 3) { e = e + i; i += 1; } } catch (x) { print(x, " ", e.n, " ", i); } } f();)jai", "boom 5 0"},
				{R"jai(function f() { C e = C(5); try { for (int i = 0; i < 3; ++i) { e = e + i; } } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); int s = 0; for (int i = 0; i < 4; ++i) { try { s = e + i; } catch (x) { s += 1; } } print(s); } f();)jai", "4"},
			};
			expect_printed(cases);
		});

		test("a_throwing_operator_leaves_a_field_an_element_and_a_referent_untouched", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(class H { C c = C(5); void go() { try { c = c + 3; } catch (x) { print(x, " ", c.n); } } } H h; h.go();)jai", "boom 5"},
				{R"jai(class H { var c = C(5); void go() { try { c = c + 3; } catch (x) { print(x, " ", c.n); } } } H h; h.go();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); void go(C e) { try { c = e + 3; } catch (x) { print(x, " ", c.n); } } } H h; h.go(C(9));)jai", "boom 5"},
				{R"jai(function f() { array<C> a = [C(5)]; try { for (auto& e : a) { e = e + 3; } } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(array<C> a = [C(5)]; try { for (auto& e : a) { e = e + 3; } } catch (x) { print(x, " ", a[0].n); })jai", "boom 5"},
				{R"jai(function f() { var a = [C(5)]; try { for (auto& e : a) { e = e + 3; } } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); C& r = e; try { r = r + 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(void bump(C& r) { r = r + 3; } function f() { C e = C(5); try { bump(e); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); var g = [&]() { e = e + 3; }; try { g(); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { array<C> a = [C(5)]; try { a[0] = a[0] + 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { array<C> a = [C(5)]; C e = C(9); try { a[0] = e + 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { var a = [C(5)]; C e = C(9); int i = 0; try { a[i] = e + 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { map<string, C> m = {"k": C(5)}; C e = C(9); try { m["k"] = e + 3; } catch (x) { print(x, " ", m["k"].n); } } f();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); var v = C(5); } function f() { H h; C e = C(9); try { h.c = e + 3; } catch (x) { print(x, " ", h.c.n); } } f();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); var v = C(5); } function f() { H h; try { h.v = h.v + 3; } catch (x) { print(x, " ", h.v.n); } } f();)jai", "boom 5"},
				{R"jai(class S { static C s = C(5); } function f() { try { S::s = S::s + 3; } catch (x) { print(x, " ", S::s.n); } } f();)jai", "boom 5"},
				{R"jai(namespace ns { C v = C(5); } function f() { try { ns::v = ns::v + 3; } catch (x) { print(x, " ", ns::v.n); } } f();)jai", "boom 5"},
			};
			expect_printed(cases);
		});

		test("a_throwing_operator_declares_nothing", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); try { C r = e + 3; print("stored ", r.n); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); try { int r = e + 3; print("stored ", r); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); try { D r = e + 3; print("stored ", r.k); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); try { var r = e + 3; print("stored ", r); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); try { auto r = e - 3; print("stored ", r); } catch (x) { print(x, " ", e.n); } } f();)jai", "deep 5"},
				{R"jai(C e = C(5); try { C r = e + 3; print("stored ", r.n); } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(C e = C(5); try { int r = e + 3; print("stored ", r); } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(function f() { C e = C(5); try { bool b = e == 3; print("stored ", b); } catch (x) { print(x); } } f();)jai", "eq"},
				{R"jai(function f() { C e = C(5); try { bool b = e < 3; print("stored ", b); } catch (x) { print(x); } } f();)jai", "less"},
				{R"jai(function f() { C e = C(5); try { var b = e == 3; print("stored ", b); } catch (x) { print(x); } } f();)jai", "eq"},
				{R"jai(function f() { T t; try { string s = "b" + t; print("stored ", s); } catch (x) { print(x); } } f();)jai", "ts"},
				{R"jai(function f() { T t; try { var s = "b" + t; print("stored ", s); } catch (x) { print(x); } } f();)jai", "ts"},
			};
			expect_printed(cases);
		});

		test("an_uncaught_operator_throw_is_the_error_that_leaves_execute", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); e = e + 3; } f();)jai", "ERROR boom"},
				{R"jai(C e = C(5); e = e + 3;)jai", "ERROR boom"},
				{R"jai(C e = C(5); D d; d = e + 3;)jai", "ERROR boom"},
				{R"jai(function f() { C e = C(5); C r = e + 3; } f();)jai", "ERROR boom"},
				{R"jai(C e = C(5); int r = e + 3;)jai", "ERROR boom"},
				{R"jai(function f() { C e = C(5); e += 3; } f();)jai", "ERROR boom"},
				{R"jai(function f() { array<C> a = [C(9)]; for (auto& e : a) { e = e + 3; } print(a[0].n); } f();)jai", "ERROR boom"},
			};
			expect_printed(cases);
		});

		// the operator's own locals and the try block's die before the catch runs, and a throw from a call the operator makes is the operator's
		test("a_throw_from_a_nested_call_and_past_a_destructor_reaches_the_catch", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); try { e = e - 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "deep 5"},
				{R"jai(C e = C(5); try { e = e - 3; } catch (x) { print(x, " ", e.n); })jai", "deep 5"},
				{R"jai(function f() { C e = C(5); try { e = e * 3; } catch (x) { print(x, " ", e.n, " ", LOG); } } f();)jai", "star 5 t"},
				{R"jai(function f() { C e = C(5); try { C r = e * 3; print("stored ", r.n); } catch (x) { print(x, " ", e.n, " ", LOG); } } f();)jai", "star 5 t"},
				{R"jai(function f() { C e = C(5); try { Token k = Token("k"); e = e + 3; } catch (x) { print(x, " ", e.n, " ", LOG); } } f();)jai", "boom 5 k"},
				{R"jai(function f() { C e = C(5); try { e = e % 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "mod 5"},
				{R"jai(function f() { C e = C(5); try { e *= 3; } catch (x) { print(x, " ", e.n, " ", LOG); } } f();)jai", "star 5 t"},
				{R"jai(function f() { C e = C(5); try { e -= 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "deep 5"},
			};
			expect_printed(cases);
		});

		// a comparison's or a text join's result is the right side too: no bool and no partial text is stored
		test("a_throwing_comparison_or_text_conversion_stores_nothing", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); bool b = true; try { b = e < 3; } catch (x) { print(x, " ", b); } } f();)jai", "less true"},
				{R"jai(function f() { C e = C(5); var b = 5; try { b = e < 3; } catch (x) { print(x, " ", b); } } f();)jai", "less 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { b = e == 3; } catch (x) { print(x, " ", b); } } f();)jai", "eq 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { b = e != 3; } catch (x) { print(x, " ", b); } } f();)jai", "eq 5"},
				{R"jai(function f() { C e = C(5); bool b = true; try { b = e == 3; } catch (x) { print(x, " ", b); } } f();)jai", "eq true"},
				{R"jai(C e = C(5); var b = 5; try { b = e == 3; } catch (x) { print(x, " ", b); })jai", "eq 5"},
				{R"jai(function f() { T t; string s = "a"; try { s = "b" + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; var s = "a"; try { s = "b" + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(T t; var s = "a"; try { s = "b" + t; } catch (x) { print(x, " ", s); })jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = s + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; var s = "a"; try { s = s + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = `${t}`; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = s + t + "c"; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = s + "c" + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = s + "c" + t + "d"; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; var s = "a"; try { s = s + "c" + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(T t; string s = "a"; try { s = s + "c" + t; } catch (x) { print(x, " ", s); })jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s = "x" + t + "c"; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s += "c" + t; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { T t; string s = "a"; try { s += "c" + t + "d"; } catch (x) { print(x, " ", s); } } f();)jai", "ts a"},
				{R"jai(function f() { C e = C(5); string s = "a"; try { s = s + "c" + (e + 3); } catch (x) { print(x, " ", s); } } f();)jai", "boom a"},
				{R"jai(function f() { T t; try { print("a" + t + "c"); } catch (x) { print(x); } } f();)jai", "ts"},
				{R"jai(function f() { T t; try { print(t); } catch (x) { print(x); } } f();)jai", "ts"},
				{R"jai(function f() { B t; var b = 5; try { b = !t; } catch (x) { print(x, " ", b); } } f();)jai", "tobool 5"},
				{R"jai(function f() { B t; bool b = true; try { b = !t; } catch (x) { print(x, " ", b); } } f();)jai", "tobool true"},
				{R"jai(function f() { B t; var b = 5; try { if (t) { b = 6; } else { b = 7; } } catch (x) { print(x, " ", b); } } f();)jai", "tobool 5"},
				{R"jai(function f() { B t; var b = 5; try { b = t ? 6 : 7; } catch (x) { print(x, " ", b); } } f();)jai", "tobool 5"},
			};
			expect_printed(cases);
		});

		// an operand the expression reads through a throwing operator[] ends it before the operator it feeds runs
		test("a_throwing_operand_ends_the_expression_it_feeds", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e[0] + 3; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); int r = 5; try { r = 3 + e[0]; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); try { int r = e[0] + 3; print("stored ", r); } catch (x) { print(x); } } f();)jai", "index"},
				{R"jai(function f() { C e = C(5); int i = 0; var r = 5; try { r = e[i] * 2; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(C e = C(5); var r = 5; try { r = e[0] + 3; } catch (x) { print(x, " ", r); })jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { if (e[0] < 3) { r = 6; } r = 7; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { while (e[0] < 3) { r = 6; break; } r = 7; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); array<int> a = [1, 2]; try { a[e[0] + 1] = 9; } catch (x) { print(x, " ", a); } } f();)jai", "index [1, 2]"},
				{R"jai(function f() { C e = C(5); array<int> a = [1, 2]; var r = 5; try { r = a[e[0] + 1]; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); array<int> a = [1, 2]; var r = 5; try { r = a[(e + 3) + 1]; } catch (x) { print(x, " ", r); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); array<int> a = [1, 2]; try { a[e[0]] = 9; } catch (x) { print(x, " ", a); } } f();)jai", "index [1, 2]"},
				{R"jai(function f() { C e = C(5); array<int> a = [1, 2]; try { a[e[0]] += 9; } catch (x) { print(x, " ", a); } } f();)jai", "index [1, 2]"},
				{R"jai(function f() { C e = C(5); int n = 2; try { n += e[0] + 1; } catch (x) { print(x, " ", n); } } f();)jai", "index 2"},
				{R"jai(function f() { C e = C(5); int n = 2; try { n += e[0]; } catch (x) { print(x, " ", n); } } f();)jai", "index 2"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e[0] + thrower(0); } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = math::abs(e[0]); } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = math::max(e[0], 1); } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = math::max(1, e[0]); } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = (-e[0]) * 2; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = -e[0]; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(class H { var v = 5; } function f() { C e = C(5); H h; try { h.v = e[0]; } catch (x) { print(x, " ", h.v); } } f();)jai", "index 5"},
				{R"jai(class H { var v = 5; } function f() { C e = C(5); H h; try { h.v = e[0] + 1; } catch (x) { print(x, " ", h.v); } } f();)jai", "index 5"},
				{R"jai(class H { int n = 5; } function f() { C e = C(5); array<H> hs = [H()]; try { hs[0].n = e[0] + 1; } catch (x) { print(x, " ", hs[0].n); } } f();)jai", "index 5"},
				{R"jai(function g() { C e = C(5); return e[0] + 1; } try { g(); } catch (x) { print(x); })jai", "index"},
				{R"jai(function take(a, int b) { return b; } function f() { C e = C(5); int r = 1; try { r = take(e[0], 2); } catch (x) { print(x, " ", r); } } f();)jai", "index 1"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e[0]; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); try { int r = e[0]; print("stored ", r); } catch (x) { print(x); } } f();)jai", "index"},
				{R"jai(function f() { var e = C(5); try { e[0] = 9; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); int i = 0; try { e[i] = 9; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(var e = C(5); try { e[0] = 9; } catch (x) { print(x, " ", e.n); })jai", "index 5"},
				{R"jai(function f() { var e = C(5); try { e[0] += 9; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); int i = 0; try { e[i] += 9; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); try { e[0]++; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); try { e[0].n = 9; } catch (x) { print(x, " ", e.n); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); var r = 5; try { r = e[0].n; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); var r = 5; try { r = e[0].n + 1; } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { var e = C(5); var a = [1]; try { a.push(e[0]); } catch (x) { print(x, " ", a.size()); } } f();)jai", "index 1"},
				{R"jai(function f() { var e = C(5); var r = 5; try { r = cast<int>(e[0]); } catch (x) { print(x, " ", r); } } f();)jai", "index 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e(1); } catch (x) { print(x, " ", r); } } f();)jai", "call 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = e(1) + 1; } catch (x) { print(x, " ", r); } } f();)jai", "call 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = (e + 3) + 1; } catch (x) { print(x, " ", r); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); var r = 5; try { r = 1 + (e + 3); } catch (x) { print(x, " ", r); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); C o = C(6); var r = 5; try { r = (e + 3) + (o + 4); } catch (x) { print(x, " ", r); } } f();)jai", "boom 5"},
				{R"jai(function f() { int r = 5; try { r = thrower(1) + 1; } catch (x) { print(x, " ", r); } } f();)jai", "T 5"},
				{R"jai(function f() { int r = 5; try { r = r + thrower(1); } catch (x) { print(x, " ", r); } } f();)jai", "T 5"},
				{R"jai(function f() { try { int r = thrower(1) + thrower(0); print("stored ", r); } catch (x) { print(x); } } f();)jai", "T"},
			};
			expect_printed(cases);
		});

		// `a op= b` without an in-place operator is `a = a op b` through the binary operator: its throw is the right side's
		test("a_compound_assignment_through_a_throwing_binary_operator_stores_nothing", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); try { e += 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { var e = C(5); try { e += 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(C e = C(5); try { e += 3; } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(var e = C(5); try { e += 3; } catch (x) { print(x, " ", e.n); })jai", "boom 5"},
				{R"jai(function f() { array<C> a = [C(5)]; try { a[0] += 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { var a = [C(5)]; try { a[0] += 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(function f() { map<string, C> m = {"k": C(5)}; try { m["k"] += 3; } catch (x) { print(x, " ", m["k"].n); } } f();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); var v = C(5); } function f() { H h; try { h.c += 3; } catch (x) { print(x, " ", h.c.n); } } f();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); var v = C(5); } function f() { H h; try { h.v += 3; } catch (x) { print(x, " ", h.v.n); } } f();)jai", "boom 5"},
				{R"jai(class H { C c = C(5); void go() { try { c += 3; } catch (x) { print(x, " ", c.n); } } } H h; h.go();)jai", "boom 5"},
				{R"jai(class H { var v = C(5); void go() { try { v += 3; } catch (x) { print(x, " ", v.n); } } } H h; h.go();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); C& r = e; try { r += 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(void bump(C& r) { r += 3; } function f() { C e = C(5); try { bump(e); } catch (x) { print(x, " ", e.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { array<C> a = [C(5)]; try { for (auto& e : a) { e += 3; } } catch (x) { print(x, " ", a[0].n); } } f();)jai", "boom 5"},
				{R"jai(class S { static C s = C(5); } function f() { try { S::s += 3; } catch (x) { print(x, " ", S::s.n); } } f();)jai", "boom 5"},
				{R"jai(namespace ns { C v = C(5); } function f() { try { ns::v += 3; } catch (x) { print(x, " ", ns::v.n); } } f();)jai", "boom 5"},
				{R"jai(function f() { C e = C(5); var r = 1; try { r = (e += 3); } catch (x) { print(x, " ", r, " ", e.n); } } f();)jai", "boom 1 5"},
				{R"jai(function f() { C e = C(5); int n = 2; try { n += e + 3; } catch (x) { print(x, " ", n); } } f();)jai", "boom 2"},
			};
			expect_printed(cases);
		});

		// an in-place operator, an increment, a converting constructor and a builtin's comparison that throw
		test("a_throwing_in_place_operator_conversion_or_builtin_comparison_reaches_the_catch", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { P p = P(5); try { p += 3; } catch (x) { print(x, " ", p.n); } } f();)jai", "pluseq 5"},
				{R"jai(function f() { P p = P(5); try { ++p; } catch (x) { print(x, " ", p.n); } } f();)jai", "inc 5"},
				{R"jai(function f() { P p = P(5); try { p++; } catch (x) { print(x, " ", p.n); } } f();)jai", "postinc 5"},
				{R"jai(function f() { array<P> a = [P(5)]; try { a[0] += 3; } catch (x) { print(x, " ", a[0].n); } } f();)jai", "pluseq 5"},
				{R"jai(function f() { K k; try { k = 3; } catch (x) { print(x, " ", k.n); } } f();)jai", "conv 1"},
				{R"jai(function f() { try { K k = 3; print("stored"); } catch (x) { print(x); } } f();)jai", "conv"},
				{R"jai(class HK { K k; } function f() { HK h; try { h.k = 3; } catch (x) { print(x, " ", h.k.n); } } f();)jai", "conv 1"},
				{R"jai(function g() -> K { return 3; } function f() { K k; try { k = g(); } catch (x) { print(x, " ", k.n); } } f();)jai", "conv 1"},
				{R"jai(function take(K k) { return 1; } function f() { int r = 0; try { r = take(3); } catch (x) { print(x, " ", r); } } f();)jai", "conv 0"},
				{R"jai(function f() { var a = [C(5)]; var r = 5; try { r = a.has(3); } catch (x) { print(x, " ", r); } } f();)jai", "eq 5"},
				{R"jai(function f() { var a = [Q(2), Q(1)]; try { a.sort(); } catch (x) { print(x, " ", a[0].n + a[1].n); } } f();)jai", "qless 3"},
			};
			expect_printed(cases);
		});

		// the statement forms whose consumer never sees the value of a throwing operand
		test("a_throwing_operator_ends_a_return_an_argument_a_condition_and_a_literal", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function f() { C e = C(5); try { e = e / 3; } catch (x) { print(x, " ", e.n); } } f();)jai", "Array index 3 out of bounds for array of size 0 5"},
				{R"jai(function g(C e) -> C { return e + 3; } try { g(C(5)); } catch (x) { print(x); })jai", "boom"},
				{R"jai(function g(e) { try { return e + 3; } catch (x) { return "caught " + x; } } print(g(C(5)));)jai", "caught boom"},
				{R"jai(function f() { C e = C(5); try { e = -e; } catch (x) { print(x, " ", e.n); } } f();)jai", "neg 5"},
				{R"jai(function f() { C e = C(5); string s = "a"; try { s = s + (e + 3); } catch (x) { print(x, " ", s); } } f();)jai", "boom a"},
				{R"jai(function f() { C e = C(5); string s = "a"; try { s = `${e + 3}`; } catch (x) { print(x, " ", s); } } f();)jai", "boom a"},
				{R"jai(function f() { C e = C(5); var r = 1; try { r = (e + 3) ?? 2; } catch (x) { print(x, " ", r); } } f();)jai", "boom 1"},
				{R"jai(function take(C a, int b) { return b; } function f() { C e = C(5); int r = 1; try { r = take(e + 3, 2); } catch (x) { print(x, " ", r); } } f();)jai", "boom 1"},
				{R"jai(function f() { C e = C(5); var r = 1; try { r = [e + 3]; } catch (x) { print(x, " ", r); } } f();)jai", "boom 1"},
				{R"jai(function f() { C e = C(5); var r = 1; try { r = {"k": e + 3}; } catch (x) { print(x, " ", r); } } f();)jai", "boom 1"},
				{R"jai(function f() { C e = C(5); int r = 1; try { r = (e < 3) ? 2 : 3; } catch (x) { print(x, " ", r); } } f();)jai", "less 1"},
				{R"jai(function f() { C e = C(5); var b = 5; try { if (e == 3) { b = 6; } else { b = 7; } } catch (x) { print(x, " ", b); } } f();)jai", "eq 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { if (e == 3 && b == 5) { b = 6; } else { b = 7; } } catch (x) { print(x, " ", b); } } f();)jai", "eq 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { while (e < 3) { b = 6; break; } b = 7; } catch (x) { print(x, " ", b); } } f();)jai", "less 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { for (int i = 0; e < 3; ++i) { b = 6; } b = 7; } catch (x) { print(x, " ", b); } } f();)jai", "less 5"},
				{R"jai(function f() { C e = C(5); var b = 5; try { switch (e) { case 3: b = 6; break; default: b = 7; } } catch (x) { print(x, " ", b); } } f();)jai", "eq 5"},
			};
			expect_printed(cases);
		});

		// a call that throws inside a counted loop leaves the loop's typed locals as the last finished statement wrote them
		test("a_throwing_call_in_a_counted_loop_leaves_its_locals_as_written", [this]() {
			const std::pair<const char*, const char*> cases[] = {
				{R"jai(function g() { int s = 0; try { for (int i = 0; i < 4; ++i) { s = s + thrower(i); } } catch (x) { print(x, " ", s); } } g();)jai", "T 10"},
				{R"jai(function g() { int s = 0; try { for (int i = 0; i < 4; ++i) { s += thrower(i); } } catch (x) { print(x, " ", s); } } g();)jai", "T 10"},
				{R"jai(function g() { int s = 0; try { for (int i = 0; i < 4; ++i) { int t = thrower(i) + s; s = t; } } catch (x) { print(x, " ", s); } } g();)jai", "T 10"},
				{R"jai(function g() { array<int> a = [1, 2, 3, 4]; try { for (int i = 0; i < 4; ++i) { a[i] = thrower(i); } } catch (x) { print(x, " ", a); } } g();)jai", "T [10, 2, 3, 4]"},
				{R"jai(function g() { array<int> a = [1, 2, 3, 4]; try { for (int i = 0; i < 4; ++i) { a[i] = a[i] + thrower(i); } } catch (x) { print(x, " ", a); } } g();)jai", "T [11, 2, 3, 4]"},
				{R"jai(function g() { array<int> a = [1, 2, 3, 4]; try { for (int i = 0; i < 4; ++i) { a[i] += thrower(i); } } catch (x) { print(x, " ", a); } } g();)jai", "T [11, 2, 3, 4]"},
				{R"jai(function g() { float s = 0.5; try { for (int i = 0; i < 4; ++i) { s = s * fthrower(i); } } catch (x) { print(x, " ", s); } } g();)jai", "T 0.25"},
				{R"jai(function g() { int s = 0; try { int i = 0; while (i < 4) { s = thrower(i); i += 1; } } catch (x) { print(x, " ", s); } } g();)jai", "T 10"},
				{R"jai(function g() { var s = 0; try { for (int i = 0; i < 4; ++i) { s = thrower(i); } } catch (x) { print(x, " ", s); } } g();)jai", "T 10"},
				{R"jai(function g() { int s = 0; try { for (int i = 0; i < 4; ++i) { s = thrower(i) < 5 ? 1 : 2; } } catch (x) { print(x, " ", s); } } g();)jai", "T 2"},
				{R"jai(function g() { int s = 0; for (int i = 0; i < 4; ++i) { try { s = s + thrower(i); } catch (x) { s += 100; } } print(s); } g();)jai", "135"},
				{R"jai(function g() { C e = C(5); int s = 0; for (int i = 0; i < 4; ++i) { try { s = s + (e + i); } catch (x) { s += 1; } } print(s); } g();)jai", "4"},
				{R"jai(class Acc { int total = 0; void run() { try { for (int i = 0; i < 4; ++i) { total = total + thrower(i); } } catch (x) { print(x, " ", total); } } } Acc a; a.run();)jai", "T 10"},
				{R"jai(class Acc { int total = 0; void run() { try { for (int i = 0; i < 4; ++i) { total += thrower(i); } } catch (x) { print(x, " ", total); } } } Acc a; a.run();)jai", "T 10"},
			};
			expect_printed(cases);
		});
	}
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::operator_unwind_tests)
