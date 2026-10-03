// Spec oracles for the robustness pass: every expected value comes from the specification (CLAUDE.md, docs/, C++ where
// the docs name it), never from a backend. Every row runs on the interpreter and the VM, proofs on and off, VM passes off,
// the copy elisions off, and with injected cell misses when the build carries the lever.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace robustness {

struct run_config {
	bool vm = false;
	bool proofs = true;
	bool passes = true;
	bool alias = true;
	uint32_t miss = 0;
	const char* label = "";
};

inline std::vector<run_config> configs() {
	std::vector<run_config> out = {
		{false, true, true, true, 0, "interp"},
		{false, false, true, true, 0, "interp proofs off"},
		{false, true, true, false, 0, "interp alias off"},
		{true, true, true, true, 0, "vm"},
		{true, false, true, true, 0, "vm proofs off"},
		{true, true, false, true, 0, "vm passes off"},
		{true, true, true, false, 0, "vm alias off"},
	};
	if (engine::miss_injection_available()) {
		out.push_back({true, true, true, true, 1, "vm miss 1"});
		out.push_back({true, true, true, true, 3, "vm miss 3"});
	}
	return out;
}

inline std::shared_ptr<engine> make(const run_config& c, check_mode mode) {
	auto e = engine::make();
	if (c.vm) { e->set_backend(backend_type::vm); }
	stdlib::register_all(*e);
	e->static_checking(mode);
	if (!c.proofs) { e->type_proofs(false); }
	if (!c.passes) { e->ir_passes(false); }
	if (!c.alias) { e->parameter_alias(false); e->local_alias(false); }
	if (c.miss) { e->cells_miss_every(c.miss); }
	return e;
}

inline std::string render(const script_value& v) {
	if (v.is_string()) { return v.as_string(); }
	if (v.is_null()) { return "<null>"; }
	return "<" + v.to_string() + ">";
}

} // namespace robustness

class robustness_oracle_tests : public suite {
public:
	robustness_oracle_tests() : suite("Robustness Oracles") {}

	void expect(const std::string& label, const std::string& src, const std::string& expected, check_mode mode = check_mode::strict) {
		for (const auto& c : robustness::configs()) {
			auto e = robustness::make(c, mode);
			std::string got;
			try { got = robustness::render(e->execute(src)); }
			catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
			check_eq(expected, got, label + " [" + c.label + "]");
		}
	}

	void forge_tests() override {
		destructors();
		destruction_order();
		sorting();
		method_conversions();
		uncaught_texts();
		map_transforms();
		second_audit();
		spaceship_rewriting();
		local_alias_soundness();
	}

private:
	// strict load verdict: "" when the program loads, else the static check's text
	static std::string strict_load_error(const std::string& src) {
		auto e = robustness::make(robustness::configs()[0], check_mode::strict);
		try { e->execute(src); } catch (const std::exception& ex) { return ex.what(); }
		return "";
	}

	// CLAUDE.md "One conversion table for every typed slot": a by-value parameter runs the assignment table; a lone method
	// converts like a lone free function, among overloads the scorer takes the table's rows and a string never makes a bool
	// parameter viable (Dev ruling 2026-10-02); the strict checker agrees at load
	void method_conversions() {
		test("a_lone_method_converts_its_arguments_like_a_free_function", [this]() {
			expect("lone method", R"(
				class C {
					int f(int v) { return v; }
					string h(string s) { return s; }
					float g(float x) { return x; }
					bool b(bool x) { return x; }
					static int sf(int v) { return v + 100; }
				}
				int free_f(int v) { return v; }
				C c;
				"" + c.f(true) + "," + c.h(5) + "," + c.g(true) + "," + c.b("x") + "," + c.b(0) + "," + C::sf(true) + "," + free_f(true);
			)", "1,5,1.0,true,false,101,1");
		});
		test("overloaded_methods_score_the_conversion_table", [this]() {
			expect("method overload table", R"(
				class P { int v = 0; P(int x) { v = x; } }
				class D {
					string k(int v) { return "int"; }
					string k(string s) { return "string"; }
					string m(bool b) { return "bool"; }
					string m(int a, int b2) { return "pair"; }
					string n(float x) { return "float"; }
					string n(string s) { return "string"; }
					string w(P p) { return "P" + p.v; }
					string w(string s, int z) { return "s"; }
				}
				D d;
				d.k(true) + "," + d.k(2.5) + "," + d.k('c') + "," + d.n(true) + "," + d.n(3) + "," + d.w(5) + "," + d.m(true);
			)", "int,int,string,float,float,P5,bool");
		});
		test("a_string_never_makes_a_bool_parameter_viable_among_overloads", [this]() {
			const std::string rivals = R"(
				class D { string m(bool b) { return "bool"; } string m(int a) { return "int"; } }
				class E { E(bool b) { } E(int a) { } }
				class F { int f(int v) { return v; } }
				string run() {
					string out = "";
					D d; F ff;
					try { out += d.m("s"); } catch (e) { out += e; }
					out += "|";
					try { E x = E("s"); out += "built"; } catch (e) { out += e; }
					out += "|";
					try { out += ff.f("s"); } catch (e) { out += e; }
					return out;
				}
				run();
			)";
			expect("string to bool among rivals", rivals,
			       "No overload of 'm' takes (string)|No constructor of 'E' takes (string)|Cannot pass string to parameter 'v' of 'f' (declared int)",
			       check_mode::off);
			const std::string verdict = strict_load_error(rivals);
			check_true(verdict.find("no overload of 'm' matches this call") != std::string::npos, "strict rejects m(string): " + verdict);
			check_true(verdict.find("no overload of 'E' matches this call") != std::string::npos, "strict rejects E(string): " + verdict);
			check_true(verdict.find("cannot pass 'string' to 'v' declared 'int'") != std::string::npos, "strict rejects f(string): " + verdict);
			// the one overload the argument count admits binds and converts, at load as at run time
			expect("the arity's one overload converts", R"(
				class D { string m(bool b) { return "bool:" + b; } string m(int a, int b2) { return "pair"; } }
				class E { bool v = false; E(bool b) { v = b; } E(int a, int b) { } }
				D d; E x = E("s");
				d.m("s") + "," + x.v;
			)", "bool:true,true");
		});
		test("a_cached_arity_answer_never_lets_a_string_reach_a_bool_overload", [this]() {
			expect("arity cache", R"(
				class D { string m(bool b) { return "bool"; } string m(int a) { return "int"; } string m(int a, int b) { return "pair"; } }
				string run() {
					D d; string out = d.m(true) + d.m(false) + d.m(1, 2) + d.m(1, 2);
					var s = "s";
					try { out += d.m(s); } catch (e) { out += "|" + e; }
					return out;
				}
				run();
			)", "boolboolpairpair|No overload of 'm' takes (string)", check_mode::off);
		});
		test("a_var_parameter_takes_what_only_a_loose_row_would_convert", [this]() {
			expect("var ahead of loose rows", R"(
				class W { int k = 0; }
				class C {
					string p(int x) { return "int"; }
					string p(string s) { return "string"; }
					string p(var v) { return "var"; }
				}
				C c;
				c.p(5) + "," + c.p("a") + "," + c.p(2.5) + "," + c.p(true) + "," + c.p(W()) + "," + c.p([1]);
			)", "int,string,int,int,var,var");
		});
	}

	// C++ [class.dtor]: a class without a user destructor has an implicit one that destroys its bases
	void destructors() {
		test("a_derived_class_without_a_destructor_runs_its_bases_destructors", [this]() {
			expect("implicit destructor", R"(
				string log = "";
				class A { ~A() { log += "~A"; } }
				class B : A { }
				class C : A { ~C() { log += "~C"; } }
				class E : A { E() { } }
				class F : B { int k = 0; }
				{ A a; } log += "|";
				{ B b; } log += "|";
				{ B b = B(); } log += "|";
				{ C c; } log += "|";
				{ E e; } log += "|";
				{ F f; } log += "|";
				{ auto p = new B(); } log += "|";
				function scoped() { B b; B copy = b; } scoped(); log += "|";
				log;
			)", "~A|~A|~A|~C~A|~A|~A|~A|~A~A|");
		});
	}

	// C++ [stmt.dcl], [class.dtor]: locals die in reverse declaration order when their frame ends (a return, a throw), a
	// vector's elements first to last when it dies, an object's members after its destructor body in reverse order
	void destruction_order() {
		test("every_copy_runs_its_destructor_once_whatever_the_elisions", [this]() {
			expect("copy destructor counts", R"(
				int gone = 0;
				class T { int v; T(int a) { v = a; } ~T() { gone++; } }
				T id(T t) { return t; }
				void take(T t) { }
				int read(T t) { return t.v; }
				function run() {
					T a = T(1);
					T b = a;
					take(a);
					T c = id(a);
					array<T> arr = [a, b];
					arr.push(c);
					T d = arr[0];
					T e = d;
					int r = read(e) + read(arr[1]);
				}
				run();
				"" + gone;
			)", "11");
		});
		test("frame_locals_die_in_reverse_declaration_order", [this]() {
			expect("frame order", R"(
				string log = "";
				class Token { string n; Token(string s) { n = s; } ~Token() { log += n; } }
				function f() { Token a = Token("a"); Token b = Token("b"); array<Token> l = [Token("x"), Token("y")]; Token c = Token("c"); }
				function g() { Token a = Token("a"); Token b = Token("b"); throw "boom"; }
				function h(int k) -> int { Token a = Token("a"); if (k > 0) { Token b = Token("b"); return k; } Token c = Token("c"); return 0; }
				class M { void run() { Token a = Token("a"); Token b = Token("b"); } }
				class Holder { Token first = Token("1"); Token second = Token("2"); ~Holder() { log += "H"; } }
				f(); log += "|";
				try { g(); } catch (e) { log += "!"; } log += "|";
				h(1); log += ","; h(0); log += "|";
				M m; m.run(); log += "|";
				auto lam = () => { Token a = Token("a"); Token b = Token("b"); };
				lam(); log += "|";
				{ Holder hh; } log += "|";
				{ Token a = Token("a"); Token b = Token("b"); { Token i = Token("i"); Token j = Token("j"); } log += "."; }
				log;
			)", "cxyba|ba!|ba,ca|ba|ba|H21|ji.ba");
		});
	}

	// docs/diagnostics.md: an uncaught error's what() is its text alone; the category message stands in only for an error
	// that carries no text
	void uncaught_texts() {
		test("an_uncaught_error_reads_its_text_without_a_category_suffix", [this]() {
			for (const auto& c : robustness::configs()) {
				auto uncaught = [&](const std::string& src) {
					auto e = robustness::make(c, check_mode::off);
					try { e->execute(src); } catch (const std::exception& ex) { return std::string(ex.what()); }
					return std::string("no error");
				};
				const std::string label = std::string(" [") + c.label + "]";
				check_eq(std::string("No overload of 'add' takes (bool)"),
				         uncaught("class C { int add(int a, int b) { return a + b; } int add(string a, string b, string c) { return 0; } } C k; k.add(true);"),
				         "overload miss" + label);
				check_eq(std::string("Cannot resume a completed coroutine"),
				         uncaught("coroutine int g() { yield 1; } auto h = g(); h.resume(); h.resume(); h.resume();"), "resume" + label);
				check_eq(std::string("Division by zero"), uncaught("int z = 0; int q = 5 / z;"), "division" + label);
				check_eq(std::string("boom: Type mismatch"), uncaught("throw \"boom: Type mismatch\";"), "a thrown text stays whole" + label);
			}
		});
	}

	// Dev ruling 2026-10-03: map.filter(pred) keeps the accepted entries in the receiver and returns it, map::filter(m, pred)
	// returns them as a new map and leaves m alone (the array dot / namespace pairs)
	void map_transforms() {
		test("map_dot_filter_mutates_and_map_namespace_filter_copies", [this]() {
			expect("map filter", R"(
				string run() {
					map<string, int> m = {"a": 1, "b": 2, "c": 3, "d": 4};
					m.filter((k, v) => v != 2).filter((k, v) => v != 4);
					string out = "" + m.size() + "," + m.contains("b") + "," + m.contains("d") + ",";
					map<string, int> m2 = {"a": 1, "b": 2};
					map<string, int> c = map::filter(m2, (k, v) => v == 1);
					out += "" + m2.size() + "," + c.size() + "," + c.contains("a") + ",";
					map<string, int> m3 = {"x": 1, "y": 2, "z": 3};
					m3.filter((k, v) => { if (k == "x") { m3.remove("z"); } return v != 2; });
					out += "" + m3.size() + "," + m3.contains("x");
					return out;
				}
				run();
			)", "2,false,false,2,1,true,1,true");
		});
		test("map_remove_if_whose_predicate_writes_the_map_visits_a_snapshot", [this]() {
			expect("map remove_if writes", R"(
				string run() {
					map<int, int> m = {};
					for (int i = 0; i < 64; i++) { m[i] = i; }
					int removed = m.remove_if((k, v) => { if (k % 8 == 0) { m.remove(k + 1); m[1000 + k] = k; } return k % 2 == 0; });
					return "" + removed + "," + m.size();
				}
				run();
			)", "32,32");
		});
	}

	// Second docs audit (2026-10-03): extend identity, thrown objects' text, parse texts that name the fix, top-level call
	// order under strict, host overload misses
	void second_audit() {
		test("extend_reclasses_in_place_so_its_result_is_the_same_object", [this]() {
			expect("extend identity", R"(
				class A { int x = 1; } class B : A { int d = 0; }
				var v = A(); var w = extend(v, B); w.d = 3;
				var p = new A(); var q = extend(p, B); q.d = 4;
				"" + same_object(v, w) + "," + v.d + "," + reflect::type_name(v) + "," + same_object(p, q) + "," + p.d;
			)", "true,3,B,true,4");
		});
		test("a_thrown_object_reads_as_its_to_string", [this]() {
			expect("thrown object text", R"(
				class Oops { int code = 7; string to_string() { return "oops " + code; } }
				class Bad { string to_string() { throw "inner"; } }
				string out = "";
				try { throw Oops(); } catch (e) { out += e + "|" + e.message + "|" + e.code; }
				try { throw Bad(); } catch (e) { out += "|" + e; }
				out;
			)", "oops 7|oops 7|7|inner");
			for (const auto& c : robustness::configs()) {
				auto e = robustness::make(c, check_mode::strict);
				std::string what;
				try { e->execute("class Oops { string to_string() { return \"oops\"; } } throw Oops();"); } catch (const std::exception& ex) { what = ex.what(); }
				check_eq(std::string("oops"), what, std::string("uncaught [") + c.label + "]");
			}
		});
		test("parse_errors_name_the_fix", [this]() {
			auto parse_text = [](const std::string& src) {
				auto e = engine::make();
				try { e->execute(src); } catch (const std::exception& ex) { return std::string(ex.what()); }
				return std::string("no error");
			};
			const std::string en = parse_text("enum F { a, b = 5 }");
			check_true(en.find("Enum values take no initializer: 'b' is 1, its position in the enum") != std::string::npos, en);
			const std::string sp = parse_text("array<int> a = [1, 2]; auto b = [...a, 3];");
			check_true(sp.find("JaiScript has no spread '...': join arrays with array::concat(a, b)") != std::string::npos, sp);
			const std::string sc = parse_text("int f(int x, int y) { return x + y; } array<int> a = [1, 2]; f(...a);");
			check_true(sc.find("JaiScript has no spread '...'") != std::string::npos, sc);
		});
		test("a_namespace_declares_coroutines", [this]() {
			expect("namespace coroutine", R"(
				namespace ns {
					coroutine int co(int n) { for (int i = 0; i < n; i++) { yield i; } }
					coroutine function names() { yield "a"; yield "b"; }
					int plain() { return 1; }
				}
				string s = "";
				for (auto v : ns::co(3)) { s += "" + v; }
				auto h = ns::names(); s += h.resume(); s += h.resume();
				s + ns::plain();
			)", "012ab1");
		});
		test("a_top_level_call_before_the_declaration_is_a_strict_load_error", [this]() {
			const std::string verdict = strict_load_error("print(later(2));\nint later(int x) { return x; }\n");
			check_true(verdict.find("'later' is called before its declaration (line 2): top-level code runs in order") != std::string::npos, verdict);
			check_eq(std::string(""), strict_load_error("int f() { return later(1); }\nint later(int x) { return x; }\nf();\n"), "a body may call further down");
			check_eq(std::string(""), strict_load_error("int later(int x) { return x; }\nlater(2);\n"), "declared first");
			expect("warn keeps the run-time text", "string out = \"\"; try { later(2); } catch (e) { out = e; } int later(int x) { return x; } out;",
			       "Undefined variable 'later'", check_mode::warn);
		});
		test("a_host_overload_miss_names_the_function_and_the_arguments", [this]() {
			for (const auto& c : robustness::configs()) {
				auto e = robustness::make(c, check_mode::off);
				e->add_function("h", [](double a) -> double { return a; });
				e->add_function("h", [](double a, double b) -> double { return a + b; });
				std::string got;
				try { got = robustness::render(e->execute("string m = \"\"; var s = \"x\"; try { h(s); } catch (err) { m = err; } m")); }
				catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_eq(std::string("No overload of 'h' takes (string)"), got, std::string("host miss [") + c.label + "]");
			}
		});
	}

	// C++20 [over.match.oper]: with no operator< in the chain a < b is (a <=> b) < 0, reversed 5 < v is 0 < (v <=> 5), == and !=
	// come from (a <=> b) == 0 when no operator== is declared; each comparison calls <=> once, operands evaluated once, left first;
	// sort, contains and index_of use the same operators (metamorphic: sorted order agrees with pairwise <)
	void spaceship_rewriting() {
		test("spaceship_drives_every_comparison_once_per_use", [this]() {
			expect("spaceship rewriting", R"(
				int calls = 0;
				string order = "";
				class V {
					int x;
					V(int a) { x = a; }
					int operator<=>(V o) { calls++; return x - o.x; }
					int operator<=>(int o) { calls++; return x - o; }
				}
				V mk(string tag, int v) { order += tag; return V(v); }
				string run() {
					V a = V(1); V b = V(2); V c = V(2);
					string out = "" + (a < b) + (b <= c) + (b > a) + (c >= b) + (a > b) + (b < c) + calls + ",";
					calls = 0;
					out += "" + (b == c) + (a == b) + (b != c) + (a != b) + calls + ",";
					calls = 0;
					out += "" + (5 < a) + (0 < a) + (1 <= a) + (2 > a) + calls + ",";
					calls = 0;
					out += "" + (5 <=> a) + (a <=> 5) + (1 <=> a) + (a == 1) + (1 == a) + (a != 1) + (2 != a) + calls + ",";
					calls = 0;
					bool lt = mk("L", 1) < mk("R", 2);
					out += "" + lt + order + calls + ",";
					array<V> arr = [V(3), V(1), V(2), V(1), V(5), V(0)];
					arr.sort();
					bool sorted = true;
					for (int i = 1; i < arr.size(); i++) { if (arr[i] < arr[i - 1]) { sorted = false; } }
					string xs = ""; for (auto& v : arr) { xs += "" + v.x; }
					out += xs + sorted + arr.contains(V(2)) + arr.contains(V(9)) + arr.index_of(V(2));
					return out;
				}
				run();
			)", "truetruetruetruefalsefalse6,truefalsefalsetrue4,falsetruetruetrue4,1-40truetruefalsetrue7,trueLR1,011235truetruefalse3");
		});
	}

	// docs/parallel_design.md section 15: a read-only by-value local binds its source only when nothing after it can tell; every
	// route that writes the source after the copy (operators, conversions, accessors, callbacks, coroutines, reflection,
	// references) leaves the copy unchanged, elisions on or off (C++: T x = lvalue copies)
	void local_alias_soundness() {
		test("a_by_value_local_never_sees_a_later_write_of_its_source", [this]() {
			const std::string prelude = R"(
				class T { int v = 1; array<int> a = [1]; T() {} T(int k) { v = k; } }
				T G;
				array<T> GA = [T(1)];
				class Evil {
					int k = 0;
					string to_string() { G.v = 7; return "evil"; }
					int to_int() { G.v = 7; return 3; }
					bool operator==(Evil o) { G.v = 7; return true; }
					int operator<=>(Evil o) { G.v = 7; return 0; }
					Evil operator+(Evil o) { G.v = 7; return o; }
					int _get_q() { G.v = 7; return 1; }
				}
				class Conv { int z = 0; Conv(int i) { G.v = 7; z = i; } }
				int takeconv(Conv c) { return c.z; }
				void wr(T& t) { t.v = 7; }
				void wri(int& i) { i = 7; }
				coroutine int co() { G.v = 7; yield 1; }
			)";
			struct row { const char* name; const char* setup; const char* src; const char* attack; };
			const row rows[] = {
				{"to_string", "", "G", "Evil e; string s = to_string(e);"},
				{"print", "", "G", "Evil e; string s = \"\" + e;"},
				{"to_int", "", "G", "Evil e; int i = e;"},
				{"operator==", "", "G", "Evil e; Evil e2; bool b = e == e2;"},
				{"spaceship", "", "G", "Evil e; Evil e2; bool b = e < e2;"},
				{"operator+", "", "G", "Evil e; Evil e2; Evil e3 = e + e2;"},
				{"accessor", "", "G", "Evil e; int q = e.q;"},
				{"sort", "", "G", "array<Evil> es = [Evil(), Evil()]; es.sort();"},
				{"converting ctor", "", "G", "int r = takeconv(5);"},
				{"reference parameter", "", "G", "wr(G);"},
				{"field reference", "", "G", "wri(G.v);"},
				{"reference before", "auto& r = G;", "G", "r.v = 7;"},
				{"lambda", "auto fn = [&]() { G.v = 7; };", "G", "fn();"},
				{"callback", "", "G", "array<int> xs = [1]; xs.for_each((k) => { G.v = 7; });"},
				{"coroutine", "", "G", "auto c = co(); c.resume();"},
				{"reflect", "", "G", "reflect::set(G, \"v\", 7);"},
				{"element", "", "GA[0]", "for (auto& t : GA) { t.v = 7; }"},
				{"local swap", "T s = T(1); T t2 = T(7);", "s", "T tmp = s; s = t2; t2 = tmp;"},
				{"local lambda", "T s = T(1); auto fn = () => { s.v = 7; };", "s", "fn();"},
				{"local element", "array<T> la = [T(1)];", "la[0]", "la[0].a.push(5); la[0].v = la[0].a.size() + 5;"},
			};
			for (const auto& r : rows) {
				const std::string src = prelude + "string f() {\n" + r.setup + "\nT x = " + r.src + ";\n" + r.attack +
					"\nreturn \"\" + x.v + x.a.size();\n}\nf();";
				expect(std::string("alias ") + r.name, src, "11", check_mode::warn);
			}
		});
	}

	// docs/site/reference/api.html: sort(predicate) orders by the predicate, equal items may be reordered; a predicate that
	// answers inconsistently still leaves a permutation of the elements (never a crash or a lost element)
	void sorting() {
		test("a_predicate_writing_the_elements_it_compares_leaves_a_permutation", [this]() {
			expect("writing predicate", R"(
				class T { int v = 0; T(int k) { v = k; } }
				function run() -> string {
					array<T> la = [];
					for (int i = 0; i < 40; i++) { la.push(T(i % 9)); }
					int calls = 0;
					la.sort((a, b) => { calls++; la[calls % 40].v = (calls * 7) % 13; return a.v < b.v; });
					return "" + la.size();
				}
				function run2() -> string {
					array<T> la = [T(1), T(2)];
					la.sort((a, b) => { la[0].v = 7; return a.v < b.v; });
					return "" + la.size();
				}
				run() + "|" + run2();
			)", "40|2");
		});
		test("a_stateful_predicate_leaves_the_same_multiset", [this]() {
			expect("stateful predicate", R"(
				function run() -> string {
					array<int> xs = [];
					for (int i = 0; i < 300; i++) { xs.push((i * 37) % 17); }
					int sum = 0; for (auto x : xs) { sum += x; }
					int n = 0;
					xs.sort((a, b) => { n++; return (n * 7919) % 5 < 2; });
					int after = 0; for (auto x : xs) { after += x; }
					array<int> ys = array::sort(xs, (a, b) => { n++; return n % 3 == 0; });
					int copied = 0; for (auto y : ys) { copied += y; }
					return "" + xs.size() + "," + (sum == after) + "," + ys.size() + "," + (sum == copied);
				}
				run();
			)", "300,true,300,true");
		});
	}
};

} // namespace jai::foundry::tests

using robustness_oracle_tests = jai::foundry::tests::robustness_oracle_tests;
FOUNDRY_REGISTER(robustness_oracle_tests)
