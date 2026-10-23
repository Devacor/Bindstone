// docs/site/guide/03-value-semantics.html "A var holding a handle" and "A var holding an object": '=' into a var asks the object
// it holds first. A handle on the right rebinds; a value of the held class or a subclass assigns into the shared object (its
// operator= when one takes the value); any other value rebinds. A var holding an object by value runs the class's operator= when
// one of its overloads takes the value, else takes the new value. Every place a var lives answers alike.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <string>

namespace jai::foundry::tests {

class var_assignment_tests : public suite {
public:
	var_assignment_tests() : suite("Var Assignment") {}

	static constexpr const char* k_classes = R"(
		class P { int x = 1; }
		class D : P { int z = 3; }
		class B { int y = 2; }
		class Q { int x = 1; void operator=(int v) { x = v; } }
		class R { int x = 1; void operator=(R o) { x = o.x + 100; } }
		class Holder { var m = null; var v = null; void bare() { v = 7; m = "s"; } }
		string run(function f) { string out = ""; try { out = f(); } catch (e) { out = "ERR " + e; } return out; }
	)";

	void expect(const std::string& a_what, const std::string& a_body, const std::string& a_expected) {
		auto eng = make_engine();
		jai::stdlib::register_all(eng);
		eng->execute(k_classes);
		auto value = eng->execute("run([]() { " + a_body + " })");
		check_eq(a_expected, value.is_null() ? std::string("<null>") : value.as_string(), a_what + ": " + a_body);
	}

	void forge_tests() override {
		test("a_local_var_holding_a_handle_follows_the_site_rules", [this]() {
			expect("a primitive rebinds", R"(var a = new P(); var b = a; a = "s"; return type_of(a) + " " + to_string(b.x);)", "string 1");
			expect("an int rebinds", R"(var a = new P(); a = 5; return type_of(a);)", "int");
			expect("a compatible value assigns into the shared object", R"(var a = new P(); var b = a; P n = P(); n.x = 7; a = n; return to_string(b.x) + " " + to_string(same_object(a, b));)", "7 true");
			expect("a subclass value assigns into it, the object taking the subclass", R"(var a = new P(); var b = a; D n = D(); n.x = 8; a = n; return to_string(b.x) + " " + type_of(b);)", "8 D");
			expect("an unrelated value rebinds", R"(var a = new P(); var b = a; a = B(); return type_of(a) + " " + to_string(b.x);)", "B 1");
			expect("a handle rebinds", R"(var a = new P(); var b = a; a = new P(); return to_string(same_object(a, b));)", "false");
			expect("a primitive rebinds even when operator= takes it", R"(var a = new Q(); var b = a; a = 5; return type_of(a) + " " + to_string(b.x);)", "int 1");
			expect("a compatible value runs the class's operator=", R"(var a = new R(); var b = a; R n = R(); n.x = 2; a = n; return to_string(b.x) + " " + to_string(same_object(a, b));)", "102 true");
			expect("a var that took a handle later rebinds", R"(var a = 1; a = new P(); a = "s"; return type_of(a);)", "string");
			expect("in a loop", R"(var a = new P(); var b = a; for (int i = 0; i < 3; ++i) { a = i; } return type_of(a) + " " + to_string(b.x);)", "int 1");
			expect("a var parameter", R"(auto f = [](var p) { p = "s"; return type_of(p); }; return f(new P());)", "string");
			expect("a var loop variable", R"(array<shared_ptr<P>> xs = [new P()]; string o = ""; for (var x : xs) { x = 5; o = type_of(x); } return o + " " + to_string(xs[0].x);)", "int 1");
			expect("a var from a call returning a handle", R"(auto mk = []() -> shared_ptr<P> { return new P(); }; var a = mk(); a = 3; return type_of(a);)", "int");
			expect("through a var&", R"(var a = new P(); var b = a; var& r = a; r = "s"; return type_of(a) + " " + to_string(b.x);)", "string 1");
		});

		test("a_typed_handle_local_assigns_a_value_into_its_object", [this]() {
			expect("shared_ptr<R> local", R"(shared_ptr<R> h = new R(); shared_ptr<R> h2 = h; R n = R(); n.x = 2; h = n; return to_string(h2.x);)", "102");
			expect("shared_ptr<P> local takes a subclass value", R"(shared_ptr<P> h = new P(); shared_ptr<P> h2 = h; D n = D(); n.x = 4; h = n; return to_string(h2.x) + " " + type_of(h2);)", "4 D");
			expect("an unrelated value stays an error", R"(shared_ptr<P> h = new P(); h = B(); return "stored";)", "ERR Cannot assign B to 'h' (declared shared_ptr<P>)");
		});

		test("a_var_holding_an_object_runs_an_operator_that_takes_the_value", [this]() {
			expect("operator=(int) takes an int", R"(var a = Q(); a = 5; return type_of(a) + " " + to_string(a.x);)", "Q 5");
			expect("no overload takes a string: the var takes it", R"(var a = Q(); a = "s"; return type_of(a);)", "string");
			expect("operator=(R) takes an R", R"(var a = R(); R n = R(); n.x = 2; a = n; return to_string(a.x);)", "102");
			expect("operator=(R) does not take an int", R"(var a = R(); a = 3; return type_of(a);)", "int");
			expect("no operator=", R"(var a = P(); a = 3; return type_of(a);)", "int");
			expect("a handle on the right rebinds", R"(var a = R(); var h = new R(); a = h; return to_string(a.x) + " " + to_string(same_object(a, h));)", "1 true");
			expect("in a loop", R"(var a = Q(); for (int i = 0; i < 3; ++i) { a = i + 10; } return type_of(a) + " " + to_string(a.x);)", "Q 12");
			expect("a computed value", R"(var a = Q(); int k = 2; a = k + 3; return to_string(a.x);)", "5");
			expect("through a var&", R"(var a = Q(); var& r = a; r = 4; return to_string(a.x);)", "4");
			expect("a [&] capture", R"(var a = Q(); auto g = [&]() { a = 3; }; g(); return type_of(a) + " " + to_string(a.x);)", "Q 3");
			expect("a var parameter", R"(auto f = [](var p) { p = 5; return to_string(p.x); }; return f(Q());)", "5");
		});

		test("var_fields_elements_and_map_values_answer_alike", [this]() {
			expect("a field holding an object", R"(Holder h = Holder(); h.v = Q(); h.v = 6; return type_of(h.v) + " " + to_string(h.v.x);)", "Q 6");
			expect("a field holding an object takes what its operator does not", R"(Holder h = Holder(); h.v = Q(); h.v = "s"; return type_of(h.v);)", "string");
			expect("bare field stores in a method", R"(Holder h = Holder(); h.v = Q(); h.m = new P(); var mm = h.m; h.bare(); return to_string(h.v.x) + " " + type_of(h.m) + " " + to_string(mm.x);)", "7 string 1");
			expect("a field holding a handle", R"(Holder h = Holder(); h.m = new P(); var mm = h.m; P n = P(); n.x = 9; h.m = n; return to_string(mm.x) + " " + to_string(same_object(mm, h.m));)", "9 true");
			expect("a field in a loop", R"(Holder h = Holder(); h.v = Q(); for (int i = 0; i < 3; ++i) { h.v = i + 40; } return to_string(h.v.x);)", "42");
			expect("an element holding an object", R"(array<var> xs = [Q(), Q()]; for (int i = 0; i < 2; ++i) { xs[i] = i + 20; } return type_of(xs[1]) + " " + to_string(xs[1].x);)", "Q 21");
			expect("an untyped array's element", R"(var xs = [Q()]; xs[0] = 5; return to_string(xs[0].x);)", "5");
			expect("an element holding a handle", R"(array<var> xs = [new P()]; var e0 = xs[0]; P n = P(); n.x = 4; xs[0] = n; return to_string(e0.x) + " " + to_string(same_object(e0, xs[0]));)", "4 true");
			expect("an element holding a handle rebinds to a primitive", R"(array<var> xs = [new P()]; xs[0] = "s"; return type_of(xs[0]);)", "string");
			expect("a map value holding an object", R"(map<string, var> m = {"k": Q()}; m["k"] = 5; return to_string(m["k"].x);)", "5");
			expect("a map value in a loop", R"(map<int, var> m = {0: Q()}; for (int i = 0; i < 3; ++i) { m[0] = i + 30; } return to_string(m[0].x);)", "32");
			expect("a map value holding a handle", R"(map<string, var> m = {"k": new P()}; var m0 = m["k"]; P n = P(); n.x = 7; m["k"] = n; return to_string(m0.x) + " " + to_string(same_object(m0, m["k"]));)", "7 true");
		});

		test("a_global_var_answers_like_a_local", [this]() {
			auto eng = make_engine();
			jai::stdlib::register_all(eng);
			eng->execute(k_classes);
			check_eq(std::string("string 1|Q 5|102 true"), eng->execute(R"(
				var ga = new P(); var gb = ga; ga = "s";
				var gq = Q(); gq = 5;
				var gr = new R(); var gr2 = gr; R gn = R(); gn.x = 2; gr = gn;
				type_of(ga) + " " + to_string(gb.x) + "|" + type_of(gq) + " " + to_string(gq.x) + "|" + to_string(gr2.x) + " " + to_string(same_object(gr, gr2))
			)").as_string(), "globals");
		});
	}
};

}

using var_assignment_tests = jai::foundry::tests::var_assignment_tests;
FOUNDRY_REGISTER(var_assignment_tests)
