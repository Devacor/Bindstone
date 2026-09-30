// Spec conformance: every expected value below is derived from the specification (Source/JaiScript/CLAUDE.md
// "Language features", docs/*.md, the guide, and C++ where the docs say C++ semantics), never from running
// a backend. docs/spec_conformance.md is the ledger: each claim, its source, the tests that pin it.
// Every row runs on the interpreter and the VM, with proofs on and off and the VM's passes off (and, in a
// build with the miss-injection lever, with injected cell misses), against one expected string.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace spec {

// A host class whose accessors, methods and operators throw (docs/EXCEPTION_DESIGN.md: a C++ exception a bound
// function, getter or setter raises becomes the script's exception)
struct boundary_probe {
	int v = 1;
	int get_v() const { return v; }
	void set_v(int x) { if (x < 0) { throw std::invalid_argument("negative v"); } v = x; }
	int get_bad() const { throw std::runtime_error("getter failed"); }
	int poke(int x) { if (x == 0) { throw std::invalid_argument("poke zero"); } return x; }
	boundary_probe operator+(const boundary_probe& o) const { boundary_probe r; r.v = v + o.v; return r; }
	boundary_probe operator*(int k) const { if (k < 0) { throw std::domain_error("negative scale"); } boundary_probe r; r.v = v * k; return r; }
};

inline void bind_boundary_probe(engine& e) {
	dynamic_binder<boundary_probe>(e, "Probe")
		.constructor<>()
		.property("v", &boundary_probe::get_v, &boundary_probe::set_v)
		.property("bad", &boundary_probe::get_bad, nullptr)
		.property("lv", [](boundary_probe& p) { return p.v; }, [](boundary_probe& p, int x) { if (x > 100) { throw std::out_of_range("lv too big"); } p.v = x; })
		.method("poke", &boundary_probe::poke)
		.method("+", &boundary_probe::operator+)
		.method("*", &boundary_probe::operator*)
		.build();
}

struct run_config {
	bool vm = false;
	bool proofs = true;
	bool passes = true;
	uint32_t miss = 0;
	const char* label = "";
};

inline std::vector<run_config> configs() {
	std::vector<run_config> out = {
		{false, true, true, 0, "interp"},
		{false, false, true, 0, "interp proofs off"},
		{true, true, true, 0, "vm"},
		{true, false, true, 0, "vm proofs off"},
		{true, true, false, 0, "vm passes off"},
	};
	if (engine::miss_injection_available()) {
		out.push_back({true, true, true, 1, "vm miss 1"});
		out.push_back({true, true, true, 3, "vm miss 3"});
	}
	return out;
}

inline std::shared_ptr<engine> make(const run_config& c, check_mode mode = check_mode::strict) {
	auto e = engine::make();
	if (c.vm) { e->set_backend(backend_type::vm); }
	stdlib::register_all(*e);
	e->static_checking(mode);
	if (!c.proofs) { e->type_proofs(false); }
	if (!c.passes) { e->ir_passes(false); }
	if (c.miss) { e->cells_miss_every(c.miss); }
	return e;
}

inline std::string render(const script_value& v) {
	if (v.is_string()) { return v.as_string(); }
	if (v.is_null()) { return "<null>"; }
	return "<" + v.to_string() + ">";
}

// "i:3" / "f:3.9" / "s:x" - the host-side view of an execute result, type included
inline std::string typed_render(const script_value& v) {
	if (v.is_int()) { return "i:" + std::to_string(v.as_int()); }
	if (v.is_float()) { std::ostringstream os; os << v.as_float(); return "f:" + os.str(); }
	if (v.is_bool()) { return std::string("b:") + (v.as_bool() ? "true" : "false"); }
	if (v.is_string()) { return "s:" + v.as_string(); }
	if (v.is_null()) { return "null"; }
	return "?:" + v.to_string();
}

} // namespace spec

class spec_conformance_tests : public suite {
public:
	spec_conformance_tests() : suite("Spec Conformance") {}

	void every_config(const std::string& label, const std::string& expected, const std::function<std::string(engine&)>& body,
	                  check_mode mode = check_mode::strict) {
		for (const auto& c : spec::configs()) {
			auto e = spec::make(c, mode);
			std::string got;
			try { got = body(*e); }
			catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
			check_eq(expected, got, label + " [" + c.label + "]");
		}
	}

	void expect(const std::string& label, const std::string& src, const std::string& expected, check_mode mode = check_mode::strict) {
		every_config(label, expected, [&](engine& e) { return spec::render(e.execute(src)); }, mode);
	}

	void forge_tests() override {
		assignment_expression_values();
		declarations_and_constructors();
		by_value_returns_and_copies();
		memberwise_equality();
		map_keys();
		casts();
		catch_variables_and_switch();
		namespaces();
		utf8_caches();
		typed_containers();
		increments();
		parallel_locals();
		grammar_and_operators();
		chars();
		map_key_edges();
		scopes_and_units();
		references();
		namespace_forms();
		exceptions_more();
		closures();
		diagnostic_texts();
		callback_errors();
		user_assignment_operator();
		textless_errors();
		checker_verdicts();
		instance_variable_overlays();
		file_locations();
		privilege_order();
		guide_examples();
		parallel_contracts();
		native_exceptions();
		access_control();
		lambda_globals_and_init_captures();
		compound_member_order();
		compound_subscript_order();
		auto_static_type();
		call_argument_capture();
		simplification_audit_fixes();
		rulings_2026_09_28();
#ifdef JAISCRIPT_SPEC_KNOWN_BUGS
		known_bugs();
#endif
	}

private:
	// CLAUDE.md "An assignment expression's value is the destination's stored value" (Dev ruling 2026-09-08)
	void assignment_expression_values() {
		test("assignment_value_is_the_stored_value_for_every_destination_kind", [this]() {
			expect("stored value", R"(
				class A { int raw = 0; int _get_x() { return raw * 10; } void _set_x(int v) { raw = v; } }
				class C { int n = 0; static int s = 0; int set_bare() { return (n = 7.9); } int set_this() { return (this.n = 8.9); } }
				function run() -> string {
					int t = 0; C c = C(); A a = A();
					array<int> xs = [0]; map<string, int> m = {"k": 0};
					var r1 = (t = 3.9);
					var r2 = (c.n = 4.9);
					var r3 = (C::s = 5.9);
					var r4 = (a.x = 4);
					var r5 = (xs[0] = 2.9);
					var r6 = (m["k"] = 1.5);
					var r7 = c.set_bare();
					var r8 = c.set_this();
					return "" + r1 + "," + r2 + "," + r3 + "," + r4 + "," + r5 + "," + r6 + "," + r7 + "," + r8 + "|" + type_of(r1) + type_of(r5) + type_of(r6);
				}
				run();
			)", "3,4,5,40,2,1,7,8|intintint");
		});
		test("compound_and_postfix_assignment_values_follow_the_destination", [this]() {
			// compound x op= y is x = T(x op y); postfix yields the old stored value
			expect("compound/postfix", R"(
				class C { int n = 1; }
				function run() -> string {
					int t = 1; C c = C(); array<int> xs = [5]; map<string, int> m = {"k": 5};
					var r1 = (t += 2.5);
					var r2 = (c.n *= 2.9);
					var r3 = xs[0]++;
					var r4 = m["k"]--;
					var r5 = ++xs[0];
					var r6 = (xs[0] -= 0.5);
					return "" + r1 + "," + r2 + "," + r3 + "," + r4 + "," + r5 + "," + r6 + "|" + t + "," + c.n + "," + xs[0] + "," + m["k"];
				}
				run();
			)", "3,2,5,5,7,6|3,2,6,4");
		});
		test("setter_only_member_assignment_yields_its_right_hand_side", [this]() {
			// "A setter-only member (no field, no getter in the chain) yields its right-hand side"; the int parameter truncates
			expect("setter only", R"(
				class W { var got = null; void _set_v(int x) { got = x; } }
				function run() -> string { W w = W(); var r = (w.v = 2.5); return "" + r + "|" + w.got + "|" + type_of(r); }
				run();
			)", "2.5|2|float");
		});
		test("top_level_implicit_result_is_never_read_back", [this]() {
			// a typed local or plain static store converts before producing anything; an explicit field, accessor, element or
			// map store hands the host its right-hand side
			struct row { const char* src; const char* expected; };
			const row rows[] = {
				{"int n = 0; n = 3.9;", "i:3"},
				{"class S { static int s = 0; } S::s = 2.5;", "i:2"},
				{"class C { int n = 0; } C c = C(); c.n = 3.9;", "f:3.9"},
				{"array<int> a = [0]; a[0] = 2.5;", "f:2.5"},
				{"map<string, int> m = {}; m[\"k\"] = 1.5;", "f:1.5"},
				{"class A { int raw = 0; int _get_x() { return raw * 10; } void _set_x(int v) { raw = v; } } A a = A(); a.x = 4.5;", "f:4.5"},
			};
			for (const auto& r : rows) {
				every_config(r.src, r.expected, [&](engine& e) { return spec::typed_render(e.execute(r.src)); });
			}
		});
		test("assignment_value_inside_a_ternary_in_a_leaf_called_from_a_counted_loop", [this]() {
			// element, typed local and field destinations, each read back as the stored int
			expect("ternary leaf", R"(
				array<int> xs = [0, 0, 0, 0, 0, 0];
				class Acc { int v = 0; }
				Acc A = Acc();
				function leaf(int i) -> int { return i % 2 == 0 ? (xs[i] = i * 1.5) : (xs[i] = i - 0.5); }
				function leaf2(int i) -> int { int t = 0; return i > 2 ? (t = i * 1.9) : (t = -i * 1.9); }
				function leaf3(int i) -> int { return (A.v = A.v + i * 0.75); }
				function run() -> string {
					int acc = 0; int acc2 = 0; int acc3 = 0;
					for (int i = 0; i < 6; i++) { acc += leaf(i); acc2 += leaf2(i); acc3 += leaf3(i); }
					string s = "";
					for (int i = 0; i < 6; i++) { s += "" + xs[i] + ","; }
					return "" + acc + "|" + s + "|" + acc2 + "|" + acc3 + "," + A.v;
				}
				run();
			)", "15|0,0,3,2,6,4,|17|19,9");
		});
	}

	// CLAUDE.md "Declarations without an initializer" and "Constructor exits" (Dev rulings 2026-09-26), crossed with
	// coroutines and delegating constructors
	void declarations_and_constructors() {
		test("defaulted_declarations_inside_a_coroutine_re_default_on_every_execution", [this]() {
			expect("coroutine decls", R"(
				class P { int x = 7; P() { x = 9; } }
				coroutine int gen() {
					for (int i = 0; i < 3; i++) { int k; string s; k += i * 10; s += "ab"; yield k + s.size(); }
					P p; p.x += 1; yield p.x;
					array<int> xs; xs.push(4); yield xs.size() + xs[0];
					int q{5}; yield q;
				}
				function run() -> string { string out = ""; for (auto v : gen()) { out += "" + v + ","; } return out; }
				run();
			)", "2,12,22,10,5,5,");
		});
		test("defaulted_class_fields_take_their_type_defaults", [this]() {
			expect("field defaults", R"(
				class Q { int a; string b; array<int> c; bool d; float f; var v; }
				function run() -> string { Q q = Q(); return "" + q.a + "|" + q.b + "|" + q.c.size() + "|" + q.d + "|" + q.f + "|" + type_of(q.v); }
				run();
			)", "0||0|false|0.0|null");
		});
		test("bare_return_in_a_delegated_constructor_ends_only_that_body", [this]() {
			// C++: a delegating constructor runs the whole target constructor (its base initializer included), then its own body
			expect("delegation", R"(
				class B { int b = 0; B(int x) { b = x; } }
				class D : B {
					int d = 0; string path = "";
					D(int x) : super(x) { path += "I"; if (x < 0) { return; } d = x * 10; path += "J"; }
					D() : this(-1) { path += "K"; d = 5; }
					D(string s) : this(7) { if (s == "x") { return; } path += "L"; }
				}
				function show(D v) -> string { return "" + v.b + "," + v.d + "," + v.path; }
				show(D()) + "|" + show(D(3)) + "|" + show(D("x")) + "|" + show(D("y"));
			)", "-1,5,IK|3,30,IJ|7,70,IJ|7,70,IJL");
		});
		test("a_delegation_chain_runs_every_target_whole_and_a_cycle_raises", [this]() {
			// C++: D() -> D(int) -> D(int, int) : super(a + b); field initializers run once, in the innermost target;
			// a delegation cycle is ill-formed in C++ and raises the recursion text instead of recursing without end
			expect("delegation chain", R"(
				class B { int b = 0; string log = ""; B(int x) { b = x; log += "B"; } }
				class D : B {
					int d = 1;
					D(int x, int y) : super(x + y) { log += "2"; d += 10; }
					D(int x) : this(x, 100) { log += "1"; d += 100; }
					D() : this(5) { log += "0"; d += 1000; }
				}
				class Loop { int v = 0; Loop() : this(1) { } Loop(int x) : this() { } }
				function run() -> string {
					D v = D();
					string err = "";
					try { var l = Loop(); } catch (e) { err = e; }
					return "" + v.b + "," + v.d + "," + v.log + "|" + err;
				}
				run();
			)", "105,1111,B210|Maximum recursion depth exceeded - possible infinite recursion");
		});
		test("a_throw_in_a_delegated_constructor_keeps_the_target_and_runs_no_destructor", [this]() {
			expect("delegated throw", R"(
				var LOG = [];
				class R { int id = 0; R(int i) { id = i; if (i == 2) { throw "boom"; } } R() : this(2) { id = 99; } ~R() { LOG.push(id); } }
				function run() -> string { R keep = R(1); string err = ""; try { keep = R(); } catch (e) { err = e; } return "" + keep.id + "|" + err + "|" + to_json(LOG); }
				run();
			)", "1|boom|[]");
		});
	}

	// CLAUDE.md "By-value returns copy, own locals move" (docs/script_extensible.md 6.5) and "Aggregate copies are C++ copies"
	void by_value_returns_and_copies() {
		test("by_value_returns_never_alias_what_the_callee_could_name", [this]() {
			expect("returns", R"(
				var G = [1, 2, 3];
				class H { array<int> items = [1]; array<int> get() { return items; } }
				var rows = [[1], [2]];
				function fg() { return G; }
				function frow(int i) { return rows[i]; }
				function fref() -> auto& { return G; }
				function run() -> string {
					H h = H();
					fg().push(4); h.get().push(2); frow(0).push(9);
					fref().push(5);
					var loc = [7];
					var cl = [&]() { return loc; };
					cl().push(8);
					return "" + G.size() + "," + h.items.size() + "," + rows[0].size() + "," + loc.size();
				}
				run();
			)", "4,1,1,1");
		});
		test("a_by_value_return_of_an_aggregate_shares_its_shared_ptr_members", [this]() {
			expect("aggregate return", R"(
				class In { int x = 1; }
				class Out { shared_ptr<In> p = new In(); array<int> v = [1]; In w; }
				Out O = Out();
				function get() -> Out { return O; }
				function run() -> string {
					Out c = get(); c.p.x = 42; c.v.push(2); c.w.x = 5;
					get().p.x += 1; get().v.push(3);
					return "" + O.p.x + "," + O.v.size() + "," + O.w.x + "," + to_string(same_object(c.p, O.p)) + "," + to_string(c.p == O.p);
				}
				run();
			)", "43,1,1,true,true");
		});
		test("returning_an_own_local_moves_it_without_an_extra_destructor", [this]() {
			expect("nrvo", R"(
				var TRACE = "";
				class T { int id = 0; T(int i) { id = i; } ~T() { TRACE += "d" + to_string(id); } }
				function make() -> T { T t(1); return t; }
				function make2() -> T { return T(2); }
				function run1() { T got = make(); TRACE += "|"; }
				function run2() { T got = make2(); TRACE += "|"; }
				run1(); run2();
				TRACE;
			)", "|d1|d2");
		});
		test("aggregate_copies_copy_values_and_share_handles", [this]() {
			expect("aggregate copy", R"(
				class In { int x = 1; }
				class Box { shared_ptr<In> h = new In(); In v; array<shared_ptr<In>> hs = [new In()]; array<In> vs = [In()]; }
				function run() -> string {
					Box a = Box(); Box b = a;
					b.h.x = 5; b.v.x = 9; b.hs[0].x = 6; b.vs[0].x = 7;
					array<shared_ptr<In>> xs = [new In()]; var ys = xs; ys[0].x = 3;
					array<In> zs = [In()]; array<In> ws = zs; ws[0].x = 4;
					map<string, shared_ptr<In>> m = {"a": new In()}; var m2 = m; m2["a"].x = 8;
					return "" + a.h.x + "," + a.v.x + "," + a.hs[0].x + "," + a.vs[0].x + "|" + xs[0].x + "," + zs[0].x + "," + m["a"].x;
				}
				run();
			)", "5,1,6,1|3,1,8");
		});
	}

	// CLAUDE.md "== is memberwise on heavy values" (Dev ruling 2026-09-05)
	void memberwise_equality() {
		test("memberwise_equality_with_user_operators_at_every_depth", [this]() {
			expect("memberwise", R"(
				class P { int x = 0; int y = 0; P(int a, int b) { x = a; y = b; } }
				class Q { int x = 0; int y = 0; Q(int a, int b) { x = a; y = b; } }
				class E { int v = 0; E(int a) { v = a; } bool operator==(E o) { return v % 10 == o.v % 10; } }
				class HasE { E e = E(0); array<E> es = []; }
				function run() -> string {
					string s = "";
					s += to_string([1, 2, 3] == [1, 2, 3]) + ",";
					s += to_string([1, 2] == [1, 2, 3]) + ",";
					s += to_string({"a": 1, "b": 2} == {"b": 2, "a": 1}) + ",";
					s += to_string({"a": 1} == {"a": 2}) + ",";
					s += to_string(P(1, 2) == P(1, 2)) + ",";
					s += to_string(P(1, 2) == P(1, 3)) + ",";
					var p = P(1, 2); var q = Q(1, 2);
					s += to_string(p == q) + ",";
					s += to_string([E(1), E(22)] == [E(11), E(2)]) + ",";
					HasE a = HasE(); HasE b = HasE(); a.e = E(3); b.e = E(13);
					s += to_string(a == b) + ",";
					a.es.push(E(4)); b.es.push(E(15));
					s += to_string(a == b) + ",";
					s += to_string({"k": [E(5)]} == {"k": [E(25)]});
					return s;
				}
				run();
			)", "true,false,true,false,true,false,false,true,true,false,true");
		});
		test("handles_compare_by_pointer_and_same_object_is_identity", [this]() {
			expect("handles", R"(
				class In { int x = 1; }
				function run() -> string {
					shared_ptr<In> p = new In(); shared_ptr<In> q = new In(); shared_ptr<In> r = p;
					var a = [1]; var b = a; var& c = a;
					return to_string(p == q) + "," + to_string(p == r) + "," + to_string([p] == [q]) + "," + to_string([p] == [r]) + "," +
						to_string(same_object(a, b)) + "," + to_string(same_object(a, c)) + "," + to_string(same_object(1, 1)) + "," +
						to_string(same_object("s", "s")) + "," + to_string(a == b);
				}
				run();
			)", "false,true,false,true,false,true,true,true,true");
		});
		test("ordering_stays_off_containers_and_instances", [this]() {
			expect("ordering", R"(
				class P { int x = 0; }
				function run() -> string {
					string s = ""; var a = [1]; var b = [2]; var p = P(); var q = P();
					try { var r = a < b; s += "ordered,"; } catch (e) { s += "raised,"; }
					try { var r = p < q; s += "ordered"; } catch (e) { s += "raised"; }
					return s;
				}
				run();
			)", "raised,raised");
		});
		test("memberwise_equality_depth_cap", [this]() {
			expect("depth cap", R"(
				function nest(int n) { var a = 0; for (int i = 0; i < n; i++) { a = [a]; } return a; }
				function run() -> string {
					string s = to_string(nest(150) == nest(150)) + ",";
					try { var r = nest(260) == nest(260); s += to_string(r); } catch (e) { s += e; }
					return s;
				}
				run();
			)", "true,Cannot compare: the values nest deeper than 200 levels");
		});
	}

	// CLAUDE.md "Map key types" (Dev ruling 2026-09-02); texts from docs/diagnostics.md "element / key boundary"
	void map_keys() {
		test("map_key_types_are_enforced_and_auto_keys_infer", [this]() {
			expect("keys", R"(
				function run() -> string {
					string s = "";
					map<string, int> m = {}; var k = 5;
					try { m[k] = 1; s += "stored"; } catch (e) { s += e; }
					s += "|";
					map<var, int> mv = {}; mv[5] = 1; mv["5"] = 2;
					s += "" + mv.size() + "|";
					map<auto, int> ma = {5: 1}; var ks = "x";
					try { ma[ks] = 2; s += "stored"; } catch (e) { s += e; }
					s += "|";
					map<auto, int> me = {}; me[1] = 1; me["a"] = 2;
					s += "" + me.size();
					return s;
				}
				run();
			)", "Cannot use 'int' as a key of type 'string'|2|Cannot use 'string' as a key of type 'int'|2");
		});
		test("an_inferred_key_type_rejection_mid_loop_keeps_the_entries_stored_before_it", [this]() {
			expect("hot loop", R"(
				function run() -> string {
					map<auto, int> m = {0: 0};
					array<var> keys = [1, 2, 3, "four", 5];
					string err = ""; int i = 0;
					try { while (i < 5) { m[keys[i]] = i * i; i++; } } catch (e) { err = e; }
					int total = 0;
					for (auto kv : m) { total += kv.first * 100 + kv.second; }
					return "" + m.size() + "|" + i + "|" + err + "|" + total;
				}
				run();
			)", "4|3|Cannot use 'string' as a key of type 'int'|605");
		});
		test("inferred_int_keys_accumulate_in_a_counted_loop", [this]() {
			std::string expected;
			for (int k = 0; k < 7; ++k) {
				int64_t sum = 0;
				for (int j = 0; j < 1000; ++j) { if (j % 7 == k) { sum += j; } }
				expected += std::to_string(sum) + ",";
			}
			expect("accumulate", R"(
				function run() -> string {
					map<auto, int> h = {0: 0, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0};
					for (int j = 0; j < 1000; j++) { h[j % 7] += j; }
					string s = "";
					for (int k = 0; k < 7; k++) { s += "" + h[k] + ","; }
					return s;
				}
				run();
			)", expected);
		});
	}

	// CLAUDE.md "cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e)"
	void casts() {
		test("cast_reference_member_stores_through_a_base_typed_receiver", [this]() {
			expect("cast ref", R"(
				class Base { int b = 1; }
				class Der : Base { int t = 0; }
				class Holder { Base p = Der(); }
				function run() -> string {
					Holder h = Holder();
					cast<Der&>(h.p).t = 5;
					cast<Der&>(h.p).t += 2;
					var copy = cast<Der>(h.p); copy.t = 99;
					shared_ptr<Base> sp = new Der();
					cast<Der&>(sp).t = 7;
					shared_ptr<Der> sd = cast<shared_ptr<Der>>(sp); sd.t += 1;
					string err = "";
					Base plain = Base();
					try { cast<Der&>(plain).t = 1; } catch (e) { err = e; }
					int before = cast<Der&>(h.p).t;
					for (int i = 0; i < 100; i++) { cast<Der&>(h.p).t += i; }
					return "" + before + "," + cast<Der&>(sp).t + "," + err + "," + cast<Der&>(h.p).t;
				}
				run();
			)", "7,8,Cannot cast 'Base' to 'Der&',4957");
		});
		test("null_passes_the_value_and_handle_casts_and_fails_the_reference_cast", [this]() {
			expect("cast null", R"(
				class Base { int b = 1; }
				class Der : Base { int t = 0; }
				function run() -> string {
					var n = null; string s = "";
					s += to_string(cast<Der>(n) == null) + "," + to_string(cast<shared_ptr<Der>>(n) == null) + ",";
					try { cast<Der&>(n).t = 1; s += "bound"; } catch (e) { s += "raised"; }
					return s;
				}
				run();
			)", "true,true,raised");
		});
	}

	// docs/EXCEPTION_DESIGN.md addendum (the catch variable is a block-scoped local; coroutines park the exception trio)
	// and CLAUDE.md "switch/case/default (break-by-default; explicit fallthrough;)"
	void catch_variables_and_switch() {
		test("catch_variables_inside_switch_cases_with_fallthrough", [this]() {
			expect("switch catch", R"(
				function f(int k) -> string {
					string o = "";
					switch (k) {
						case 0:
							try { throw "A"; } catch (e) { o += e; }
							fallthrough;
						case 1:
							try { throw 7; } catch (e) { o += to_string(e.value + 1); }
							fallthrough;
						case 2:
							o += "c";
						default:
							o += "d";
					}
					return o;
				}
				f(0) + "|" + f(1) + "|" + f(2) + "|" + f(3);
			)", "A8c|8c|c|d");
		});
		test("a_catch_variable_is_invisible_after_its_block_and_to_callees", [this]() {
			expect("catch scope", R"(
				var e = "outer";
				function peek() -> string { return e; }
				function run() -> string {
					string o = "";
					try { throw "inner"; } catch (e) { o += e + "," + peek() + ","; }
					o += e;
					return o;
				}
				run();
			)", "inner,outer,outer");
		});
		test("a_catch_variable_read_after_its_block_raises_undefined_at_run_time", [this]() {
			// the strict checker rejects the read at load (C++ scope); warn mode reaches the runtime text
			expect("catch gone", R"(
				function gone() -> string { try { throw 1; } catch (err) { } return to_string(err); }
				string o = "";
				try { gone(); } catch (x) { o = x; }
				o;
			)", "Undefined variable 'err'", check_mode::warn);
		});
		test("a_catch_variable_is_captured_and_survives_yields", [this]() {
			expect("catch capture", R"(
				coroutine int g() { try { throw 3; } catch (e) { yield e.value; var f = [=]() { return e.value * 2; }; yield f(); yield e.value + 1; } }
				function mk() { var f = null; try { throw 5; } catch (e) { f = [&]() { return e.value * 2; }; } return f; }
				function run() -> string { string o = ""; for (auto v : g()) { o += "" + v + ","; } var f = mk(); o += "" + f(); return o; }
				run();
			)", "3,6,4,10");
		});
	}

	// docs/namespaces.md
	void namespaces() {
		test("namespace_document_example", [this]() {
			expect("geometry", R"(
				namespace geometry {
					var scale = 2;
					int calls = 0;
					function area(w, h) { calls += 1; return w * h * scale; }
					class Rect { int w = 1; int h = 1; }
				}
				var a1 = geometry::area(3, 4);
				geometry::scale = 10;
				var a2 = geometry::area(3, 4);
				"" + a1 + "|" + a2 + "|" + geometry::calls + "|" + geometry::Rect().w + Rect().h;
			)", "24|120|2|11");
		});
		test("namespace_functions_write_the_namespace_cell_by_bare_name", [this]() {
			expect("bare name", R"(
				namespace ns { var x = 1; function set(v) { x = v; } function get() { return x; } }
				namespace ns2 { var x = 3; var y = x + 1; }
				var x = 100;
				ns::set(5);
				namespace ns { var x = 6; }
				"" + ns::x + "|" + x + "|" + ns2::y + "|" + ns::get();
			)", "6|100|4|6");
		});
		test("namespace_typed_variables_store_through_the_assignment_table", [this]() {
			expect("typed ns", R"(
				namespace n4 { int y = 0; }
				function run() -> string {
					string s = ""; var str = "str";
					try { n4::y = str; } catch (e) { s += e + "|"; }
					var r = (n4::y = 2.9);
					n4::y += 1.5; n4::y++;
					return s + r + "|" + n4::y;
				}
				run();
			)", "Cannot assign string to 'y' (declared int)|2|4");
		});
		test("a_qualified_namespace_variable_binds_a_reference", [this]() {
			expect("ns ref", R"(
				namespace n5 { var x = 1; }
				function take(var& r) { r = 9; }
				function run() -> string {
					string s = "";
					try { take(n5::x); s += "bound"; } catch (e) { s += e; }
					return s + "|" + n5::x;
				}
				run();
			)", "bound|9");
		});
		test("a_namespace_function_does_not_see_the_callers_locals", [this]() {
			expect("ns env", R"(
				namespace n6 { function f() { return q; } }
				var q = "global";
				function g() { var q = "local"; return n6::f(); }
				g();
			)", "global");
		});
		test("a_units_program_scope_variable_never_touches_the_namespace_cell", [this]() {
			every_config("ns units", "7|50|7", [](engine& e) {
				e.execute("namespace n7 { var x = 1; function get() { return x; } }");
				e.execute("var x = 50; n7::x = 7;");
				return e.execute("\"\" + n7::x + \"|\" + x + \"|\" + n7::get()").as_string();
			});
		});
	}

	// CLAUDE.md "utf8_string": caches clear on every write
	void utf8_caches() {
		test("utf8_caches_follow_writes_through_references_parameters_fields_and_copies", [this]() {
			// e = e-acute, j = the two CJK characters, o = omega, u = u-umlaut
			const std::string E = "\xC3\xA9", J1 = "\xE6\x97\xA5", J2 = "\xE6\x9C\xAC", J3 = "\xE8\xAA\x9E", O = "\xCE\xA9", U = "\xC3\xBC";
			const std::string src =
				"function ed(utf8_string& t) { t.erase(0, 1); }\n"
				"class H { utf8_string t = \"" + U + "\"; }\n"
				"function run() -> string {\n"
				"  utf8_string u = \"a" + E + J1 + "b\";\n"
				"  int s1 = u.size(); var c2 = u[2];\n"
				"  auto& r = u; r.insert(1, \"" + O + "\");\n"
				"  string o = \"\" + s1 + \",\" + c2 + \",\" + u.size() + \",\" + u[2] + \",\" + u[3];\n"
				"  ed(u);\n"
				"  o += \"|\" + u.size() + \",\" + u[0] + \",\" + u.find(\"" + J1 + "\");\n"
				"  H h = H(); int hs = h.t.size(); h.t += \"" + J1 + J2 + "\"; o += \"|\" + hs + \",\" + h.t.size() + \",\" + h.t[2];\n"
				"  utf8_string a = \"" + J1 + J2 + J3 + "\"; var b = a; int as = a.size(); b.insert(0, \"x\");\n"
				"  o += \"|\" + as + \",\" + a.size() + \",\" + b.size() + \",\" + a[0] + \",\" + b[1];\n"
				"  return o;\n"
				"}\n"
				"run();\n";
			expect("utf8 caches", src, "4," + J1 + ",5," + E + "," + J1 + "|4," + O + ",2|1,3," + J2 + "|3,3,4," + J1 + "," + J1);
		});
		test("utf8_breadcrumbs_follow_an_insert_through_a_reference", [this]() {
			const std::string E = "\xC3\xA9", J1 = "\xE6\x97\xA5";
			const std::string src =
				"function run() -> string {\n"
				"  string s = \"\"; for (int i = 0; i < 100; i++) { s += \"a" + E + J1 + "\"; }\n"
				"  utf8_string u = s; var before = u[200]; var last = u[299]; int n = u.size();\n"
				"  var& r = u; r.insert(0, \"" + E + "\");\n"
				"  return \"\" + n + \",\" + u.size() + \",\" + to_string(u[201] == before) + \",\" + to_string(u[300] == last) + \",\" + to_string(u[0] == \"" + E + "\") + \",\" + to_string(u[1] == \"a\");\n"
				"}\n"
				"run();\n";
			expect("breadcrumbs", src, "300,301,true,true,true,true");
		});
	}

	// CLAUDE.md "Array representation follows the declared type", "Handle-typed elements take null", "Element conversions"
	void typed_containers() {
		test("element_boundaries_on_every_typed_container", [this]() {
			expect("elements", R"(
				class In { int x = 1; }
				function run() -> string {
					string s = "";
					array<array<int>> g = [[1], [2]]; var row = [2.9]; g.push(row); s += "" + g[2][0] + ",";
					map<string, array<int>> m = {}; m["a"] = [1.9, 2]; s += "" + m["a"][0] + ",";
					array<int> ai = []; var str = "x";
					try { ai.push(str); } catch (e) { s += e + ","; }
					array<string> as = []; var one = 1;
					try { as.push(one); } catch (e) { s += e + ","; }
					array<bool> ab = []; ab.push(one); ai.push(true); s += to_string(ab[0]) + "," + ai[0] + ",";
					array<shared_ptr<In>> hs = [null]; hs.push(null); s += "" + hs.size() + ",";
					array<In> vs = []; var n = null;
					try { array<In> lit = [In(), n]; } catch (e) { s += e + ","; }
					try { vs.push(n); } catch (e) { s += e + ","; }
					var box = In();
					try { ai.push(box); } catch (e) { s += e; }
					return s;
				}
				run();
			)", "2,1,Cannot push 'string' to array<int>,Cannot push 'int' to array<string>,true,1,2,Cannot store null in array<In>,Cannot push 'null' to array<In>,Cannot push 'In' to array<int>");
		});
	}

	// CLAUDE.md "++/-- work on every lvalue"
	void increments() {
		test("increments_on_members_elements_map_entries_and_nested_lvalues", [this]() {
			expect("incdec", R"(
				class C { int n = 1; array<int> a = [1, 2]; map<string, int> m = {"k": 3}; void bump() { this.n++; ++a[1]; m["k"]--; } }
				function run() -> string {
					C c = C(); c.bump(); var r = c.a[0]++;
					var g = [[1, 2], [3]]; g[1][0]++; ++g[0][1];
					return "" + c.n + c.a[0] + c.a[1] + c.m["k"] + r + "|" + (g[0][1] + g[1][0]);
				}
				run();
			)", "22321|7");
		});
	}

	// CLAUDE.md "Parallel bodies see the enclosing function's locals" (docs/parallel_design.md section 13)
	void parallel_locals() {
		test("a_parallel_body_reads_the_enclosing_frames_local_over_a_same_named_global", [this]() {
			expect("parallel locals", R"(
				var k = 100;
				function leaf(int a) -> int { return a + 1; }
				function f() -> string {
					int k = 3; array<int> xs = [1, 2, 3, 4]; array<int> ys = [1, 2, 3, 4];
					parallel_for (auto& x : xs) { x = x * k; }
					parallel_for (auto& y : ys) { y = leaf(y) * k; }
					return "" + xs[0] + "," + xs[1] + "," + xs[2] + "," + xs[3] + "|" + ys[0] + "," + ys[1] + "," + ys[2] + "," + ys[3];
				}
				f();
			)", "3,6,9,12|6,9,12,15");
		});
	}

	// docs/grammar.md forStatement, CLAUDE.md operators ("and compound assigns"), docs/integer_semantics.md
	void grammar_and_operators() {
		test("a_for_initializer_may_be_an_expression", [this]() {
			// docs/grammar.md: forStatement = "for" "(" (variableDeclaration | expression)? ";" ...
			expect("for init", R"(
				class P { int x = 0; P(int v) { x = v; } }
				function run() -> string {
					int i = 0; int s = 0;
					for (i = 0; i < 3; i++) { s += i; }
					var o = [0];
					for (o[0] = 1; o[0] < 3; o[0]++) { s += 10; }
					int j = 5;
					for (j += 1; j < 8; j++) { s += 100; }
					for (++j; j < 11; j++) { s += 1000; }
					for (P p = P(2); p.x < 4; p.x++) { s += 10000; }
					return "" + i + "," + s + "," + o[0] + "," + j;
				}
				run();
			)", "3,22223,3,11");
		});
		test("binary_minus_overflow_follows_the_build_policy", [this]() {
			auto probe = engine::make();
			const std::string expected = probe->throw_on_overflow() ? "Integer overflow in '-'" : "9223372036854775807";
			expect("minus overflow", R"(
				var a = -9223372036854775807 - 1; var b = 1; string m = "";
				try { var r = a - b; m = "" + r; } catch (e) { m = e; }
				m;
			)", expected);
		});
		test("a_literal_without_an_element_type_held_by_var_is_the_dynamic_form", [this]() {
			expect("var literal", "var v = [1, 2]; v.push(\"s\"); \"\" + v.size();", "3");
		});
	}

	// docs/char_semantics.md
	void chars() {
		test("char_promotion_in_division_bitwise_and_map_keys", [this]() {
			expect("chars", R"(
				class C { string got = ""; C operator+(var x) { got = type_of(x); return this; } }
				function run() -> string {
					string s = "";
					s += "" + ('d' / 2) + "," + ('a' / 2.0) + ",";
					s += "" + ('a' | 1) + "," + ('a' ^ ' ') + "," + ('b' >> 1) + ",";
					var m = {}; m['a'] = 1; m['b'] = 2;
					s += "" + (m['a'] + m.size()) + "," + to_string(m.has('a')) + ",";
					var u = {}; u['\xC3'] = 1; u['a'] = 2;
					s += to_string(u.keys()[0] == 'a') + ",";
					var c = C(); var r = c + 'a';
					s += c.got + ",";
					var k = {}; k[98] = 1; k['a'] = 2; k[1] = 3;
					string order = ""; for (auto kv : k) { order += type_of(kv.first) + " "; }
					s += to_string(order == "int int char " || order == "char int int ");
					return s;
				}
				run();
			)", "50,48.5,97,65,49,3,true,true,char,true");
		});
		test("math_intrinsics_are_numeric_only_and_char_is_not_an_int_slot", [this]() {
			expect("char edges", R"(
				function run() -> string {
					string s = "";
					try { var r = math::abs('a'); s += "ok,"; } catch (e) { s += "raised,"; }
					try { var r = math::wrap_add('a', 1); s += "ok,"; } catch (e) { s += "raised,"; }
					var v = 'c';
					try { int x = v; s += "ok"; } catch (e) { s += e; }
					return s;
				}
				run();
			)", "raised,raised,Cannot initialize 'x' (declared int) with char");
		});
	}

	// CLAUDE.md code guidelines: <=> is a strict weak ordering incl. NaN; complex keys order by held-pointer identity
	void map_key_edges() {
		test("a_nan_key_is_storable_and_findable", [this]() {
			every_config("nan key", "12", [](engine& e) {
				e.add_global("nan", script_value(std::numeric_limits<double>::quiet_NaN(), &e));
				return e.execute("var m = {}; m[nan] = 1; m[nan] = 2; \"\" + (m.size() * 10 + m[nan]);").as_string();
			});
		});
		test("equal_content_arrays_are_distinct_keys_and_cpp_equality_is_identity", [this]() {
			every_config("identity keys", "2|false|true", [](engine& e) {
				std::string out = e.execute("var a = [1]; var b = [1]; var m = {}; m[a] = 1; m[b] = 2; \"\" + m.size();").as_string();
				const script_value x = e.execute("[1];");
				const script_value y = e.execute("[1];");
				out += std::string("|") + (x == y ? "true" : "false");
				out += std::string("|") + (e.execute("[1] == [1];").as_bool() ? "true" : "false");
				return out;
			});
		});
	}

	// docs/global_shadowing.md, CLAUDE.md "Program-scope redeclarations shadow by unit" and "{} blocks scope as in C++"
	void scopes_and_units() {
		test("block_scopes_in_loops_tries_and_bare_blocks", [this]() {
			expect("blocks", R"(
				var x = 1;
				for (int i = 0; i < 2; i++) { var x = 10 + i; }
				function f() -> int { int x = 1; for (int i = 0; i < 2; i++) { int x = 10 + i; } return x; }
				var r = 0;
				try { var x = 2; throw 1; } catch (e) { r = x; }
				var G = 1;
				function g() { return G; }
				function h() -> int { var rg = 0; { var G = 5; rg = g(); } return rg; }
				var e = "g"; string rc = "";
				try { throw "x"; } catch (e) { rc = ::e + e; }
				"" + x + f() + "|" + r + x + "|" + h() + G + "|" + rc;
			)", "11|11|11|gx");
		});
		test("default_arguments_constructors_and_namespace_functions_resolve_in_their_declaring_unit", [this]() {
			every_config("declaring unit", "1|1|1|5", [](engine& e) {
				e.execute("var D = 1; function f(int a = D) { return a; } class C { int v = 0; C() { v = D; } } namespace ns { function h() { return D; } } function k() { return D; }");
				e.execute("int D = 5;");
				e.execute("function k() { return D; }");
				return e.execute("\"\" + f() + \"|\" + C().v + \"|\" + ns::h() + \"|\" + k();").as_string();
			});
		});
		test("a_host_callback_and_a_parallel_region_resolve_as_the_code_that_runs_them", [this]() {
			every_config("callback unit", "1|1", [](engine& e) {
				engine* raw = &e;
				e.add_function("peek", [raw]() -> script_value { return raw->get_variable("G"); });
				e.execute("var G = 1; function f() { return peek(); } function fill(var& a) { parallel_for (auto& x : a) { x = G; } }");
				e.execute("int G = 5;");
				return e.execute("var A = [0, 0]; fill(A); \"\" + f() + \"|\" + A[0];").as_string();
			});
		});
		test("an_import_is_its_own_unit_and_an_include_is_the_includers", [this]() {
			const auto dir = jai::foundry::test_temp_directory() / "spec_conformance_units";
			std::filesystem::create_directories(dir);
			std::ofstream(dir / "lib.jai") << "var X = 1; function getx() { return X; }\n";
			every_config("import unit", "1|2", [&](engine& e) {
				e.add_include_path(dir.string());
				return e.execute("import \"lib.jai\"; var X = 2; \"\" + getx() + \"|\" + X;").as_string();
			});
			every_config("include unit", "2|2", [&](engine& e) {
				e.add_include_path(dir.string());
				return e.execute("include \"lib.jai\"; var X = 2; \"\" + getx() + \"|\" + X;").as_string();
			});
		});
		test("engines_share_no_script_state", [this]() {
			every_config("two engines", "Undefined variable 'g'", [](engine& e) {
				e.execute("var g = 1;");
				auto other = engine::make();
				if (e.get_backend_type() == backend_type::vm) { other->set_backend(backend_type::vm); }
				other->static_checking(check_mode::off);
				return other->execute("var r = \"\"; try { r = \"\" + g; } catch (x) { r = x; } r;").as_string();
			});
		});
	}

	// docs/reference_model.md, docs/script_extensible.md 6.5
	void references() {
		test("reference_model_boundaries", [this]() {
			expect("refs", R"(
				int G = 1;
				function g() -> int& { return G; }
				function f() -> int& { return g(); }
				var log = "";
				function i1() -> int { log += "a"; return 0; }
				function i2() -> int { log += "b"; return 1; }
				function two(int& p, int& q) { }
				function typed_name() -> string { int x = 1; int& r = x; var s = "s"; string m = ""; try { x = s; } catch (e) { m = e; } x = 2.7; return m + "|" + to_string(x); }
				function run() -> string {
					auto& r = f(); r = 5;
					var A = [1, 2]; two(A[i1()], A[i2()]);
					int a = 1; auto& ra = a; int x = 0; x = ra; x = 5;
					var arr = [1]; auto& re = arr[0]; var m = {}; m["k"] = re; m["k"] = 9; re = 3;
					map<string, int> mm = {"k": 1}; var s = "x"; string d = ""; string o = "";
					try { mm["k"] = s; } catch (e) { d = e; }
					var& rm = mm["k"];
					try { rm = s; } catch (e) { o = e; }
					return "" + G + "|" + log + "|" + a + "|" + arr[0] + m["k"] + "|" + to_string(o == d && o != "") + mm["k"] + "|" + typed_name();
				}
				run();
			)", "5|ab|1|39|true1|Cannot assign string to 'x' (declared int)|2");
		});
		test("a_reference_held_across_yields_follows_its_element", [this]() {
			expect("ref across yield", R"(
				array<int> A = [1];
				coroutine int co() { auto& r = A[0]; yield 0; r = 7; yield 1; }
				var h = co(); h.resume(); A.push(2); h.resume();
				"" + A[0];
			)", "7");
		});
		test("coroutine_handles_returned_by_value_share", [this]() {
			expect("handle return", R"(
				coroutine int co() { yield 1; yield 2; }
				var H = co();
				function get() { return H; }
				get().resume();
				"" + H.resume();
			)", "2");
		});
		test("a_reference_to_a_by_value_call_result_is_an_error", [this]() {
			expect("ref to rvalue", R"(
				function fv() { return [1]; }
				string r = "";
				try { var& p = fv(); r = "bound"; } catch (e) { r = e.starts_with("Cannot take reference of non-lvalue expression") ? "raised" : e; }
				r;
			)", "raised", check_mode::warn);
		});
	}

	// docs/namespaces.md variable forms
	void namespace_forms() {
		test("namespace_variable_spellings_and_captures", [this]() {
			expect("ns forms", R"(
				class T { int v = 1; }
				namespace a { namespace b { var x = 1; } }
				namespace a::b { function get() { return x; } }
				namespace hp { shared_ptr<T> p = new T(); weak_ptr<T> w = p; }
				namespace dyn { var x = 1; }
				var G = 1;
				var x = 100;
				namespace cap { var x = 1; function mk() { return [&]() { x += 1; return x; }; } }
				namespace bump { var x = 1; function go() { var& r = x; r += 1; return x; } }
				var v = 1;
				namespace nf { function f() { return v; } }
				class Cm { int v = 7; int m() { return nf::f(); } }
				hp::p.v = 5;
				dyn::x = "s";
				G = 5;
				var fc = cap::mk(); fc(); fc();
				bump::go();
				"" + a::b::get() + "|" + hp::w.lock().v + "|" + dyn::x + "|" + G + "|" + cap::x + "," + x + "|" + bump::x + "|" + Cm().m();
			)", "1|5|s|5|3,100|2|1");
		});
		test("a_namespace_variable_may_be_reference_declared", [this]() {
			// docs/namespaces.md Variables: a namespace variable takes the spellings a program-scope variable takes, a reference
			// included, and a reference-declared variable stores through to what it refers to
			expect("ns ref", R"(
				var G = 1;
				namespace rf { var& r = G; int& get() { return r; } }
				rf::r = 5;
				rf::r += 1;
				auto& q = rf::get(); q = 9;
				"" + G + "|" + rf::r;
			)", "9|9");
		});
		test("a_namespace_redeclared_by_another_unit_writes_in_place", [this]() {
			every_config("ns other unit", "2", [](engine& e) {
				e.execute("namespace ns { var x = 1; function get() { return x; } }");
				e.execute("namespace ns { var x = 2; }");
				return e.execute("\"\" + ns::get();").as_string();
			});
		});
		test("a_typed_namespace_store_is_checked_at_run_time_under_strict", [this]() {
			expect("ns strict", R"(
				namespace ns { int y = 0; }
				string m = "";
				try { ns::y = "s"; } catch (e) { m = e; }
				m;
			)", "Cannot assign string to 'y' (declared int)");
		});
	}

	// docs/EXCEPTION_DESIGN.md
	void exceptions_more() {
		test("a_bare_rethrow_outside_any_catch_raises", [this]() {
			every_config("rethrow nothing", "Bare throw with no active exception thrown.", [](engine& e) {
				try { e.execute("function f() { throw; } f();"); } catch (const std::exception& ex) { return std::string(ex.what()).substr(0, 43); }
				return std::string("no error");
			});
		});
		test("rethrow_after_a_nested_try_inside_a_catch_rethrows_the_outer_exception", [this]() {
			// docs/EXCEPTION_DESIGN.md addendum: a nested try inside a catch keeps the caught exception live for throw;
			// (C++: a completed inner handler hands the exception back to the enclosing one)
			expect("rethrow", R"(
				function run() -> string {
					string r = "";
					try { try { throw "x"; } catch (e) { try { throw "y"; } catch (e2) { r += e2; } throw; } } catch (z) { r += z; }
					try { try { throw "p"; } catch { try { throw "q"; } catch (e3) { r += e3; } throw; } } catch (z2) { r += z2; }
					try { try { throw 1; } catch (e) { try { try { throw 2; } catch (e4) { r += "" + e4; } throw; } catch (e5) { r += "" + e5; } throw; } } catch (z3) { r += "" + z3; }
					return r;
				}
				run();
			)", "yxqp211");
		});
		test("a_fresh_coroutine_starts_with_no_active_exception", [this]() {
			// docs/EXCEPTION_DESIGN.md addendum: a bare throw; in a fresh coroutine raises even when the resumer sits inside a catch
			expect("coroutine rethrow", R"(
				coroutine int c() { throw; yield 1; }
				function run() -> string {
					string r = "";
					try { throw "outer"; } catch (e) { try { var h = c(); h.resume(); } catch (e2) { r = e2; } r += "|" + e; }
					return r;
				}
				run();
			)", "Bare throw with no active exception thrown.|outer");
		});
		test("an_error_out_of_a_typed_callee_leaves_the_callers_frame_intact", [this]() {
			// the caller's own typed return still converts against the caller's declaration
			expect("frame pop", R"(
				function f() -> int { throw; }
				function a() -> string { string r = ""; try { f(); } catch (e) { r = "caught:" + e; } return r; }
				a();
			)", "caught:Bare throw with no active exception thrown.");
		});
		test("a_finished_coroutine_restores_the_resumers_exception", [this]() {
			expect("resumer trio", R"(
				coroutine int g() { yield 1; }
				var r = "";
				try { throw "outer"; } catch (e) { var h = g(); h.resume(); try { throw; } catch (x) { r = x; } }
				r;
			)", "outer");
		});
		test("a_throw_expression_composes_inside_a_ternary", [this]() {
			// docs/grammar.md throwExpression: throw is an expression, so it composes (x > 0 ? x : throw "neg")
			expect("throw in ternary", R"(
				function g(int x) { return x > 0 ? x : throw "neg"; }
				function k(int x) -> int { int y = x > 0 ? x : throw "neg3"; return y; }
				string r = "";
				try { g(-1); } catch (e) { r += e; }
				try { k(-1); } catch (e) { r += "|" + e; }
				try { var q = true ? throw "neg4" : 1; } catch (e) { r += "|" + e; }
				try { int s = 1 + (false ? 1 : throw "neg5"); } catch (e) { r += "|" + e; }
				r + "|" + g(2);
			)", "neg|neg3|neg4|neg5|2", check_mode::warn);
		});
		test("a_native_exception_in_a_call_argument_converts_in_the_calling_frame", [this]() {
			every_config("native arg", "own:n", [](engine& e) {
				e.add_function("bad", []() -> script_int { throw std::runtime_error("n"); });
				return e.execute(R"(
					function pass(a, b) { return a; }
					function probe() { try { return pass(1, bad()); } catch (e) { return "own:" + e; } }
					probe();
				)").as_string();
			});
		});
	}

	// docs/closures_and_captures.md
	void closures() {
		test("value_captures_copy_objects_and_share_handles_and_loop_variables_snapshot", [this]() {
			expect("closures", R"(
				class T { int v = 1; }
				function sh() -> int { shared_ptr<T> s = new T(); auto l = [=]() { s.v = 5; }; l(); return s.v; }
				function ob() -> int { T p = T(); auto l = [=]() { return p.v; }; p.v = 9; return l(); }
				function mk() -> string { var fs = []; for (int i = 0; i < 3; i++) { fs.push([=]() { return i; }); } string s = ""; for (auto f : fs) { s += to_string(f()); } return s; }
				"" + sh() + "|" + ob() + "|" + mk();
			)", "5|1|012");
		});
		test("utf8_erase_counts_characters", [this]() {
			const std::string src = "utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u.erase(1, 2); string s = u; s;";
			expect("utf8 erase", src, "ab");
		});
	}

	// docs/diagnostics.md: the texts a script catch sees, one per row; `setup` runs first, the failing statement in a try
	struct diag_row { const char* label; const char* setup; const char* failing; const char* expected; check_mode mode; };
	void expect_texts(const std::vector<diag_row>& rows) {
		for (const auto& r : rows) {
			const std::string src = std::string(r.setup) + "\nstring __r = \"no error\";\ntry { " + r.failing + " } catch (__e) { __r = __e; }\n__r;";
			expect(r.label, src, r.expected, r.mode);
		}
	}

	void diagnostic_texts() {
		test("family_a_null_receiver_texts", [this]() {
			expect_texts({
				{"call result access", "function f() { return null; }", "var z = f().x;", "Cannot access 'x' on null ('f(...)' returned null)", check_mode::off},
				{"call result call", "function f() { return null; }", "f().go();", "Cannot call 'go' on null ('f(...)' returned null)", check_mode::off},
				{"unnamed access", "var a = [null];", "var z = a[0].x;", "Cannot access 'x' on null", check_mode::off},
				{"unnamed ternary access", "var a = [null];", "var z = (true ? a[0] : a[0]).x;", "Cannot access 'x' on null", check_mode::off},
				{"unnamed call", "var a = [null];", "a[0].go();", "Cannot call 'go' on null", check_mode::off},
				{"named call", "var v8 = null;", "v8.push(1);", "Cannot call 'push' on null ('v8' is null)", check_mode::off},
				{"named access", "var v8 = null;", "var z = v8.x;", "Cannot access 'x' on null ('v8' is null)", check_mode::off},
			});
		});
		test("family_b_no_member_and_not_a_function_texts", [this]() {
			expect_texts({
				{"coroutine handle", "coroutine int g() { yield 1; } var h = g();", "var z = h.nosuch;", "coroutine_handle has no member 'nosuch'", check_mode::off},
				{"string", "var s = \"s\";", "s.foo();", "string has no member 'foo'", check_mode::off},
				{"array", "var a = [1];", "a.foo();", "array has no member 'foo'", check_mode::off},
				{"map", "var m = {\"a\": 1};", "m.foo();", "map has no member or key 'foo'", check_mode::off},
				{"object", "class Vec2 { int y = 0; } var v = Vec2();", "v.nosuch();", "Vec2 has no member 'nosuch'", check_mode::off},
				{"int", "var i = 1;", "var z = i.nosuch;", "int has no member 'nosuch'", check_mode::off},
				{"super", "class Base { int b = 1; } class D : Base { var f() { return super::x(); } }", "var z = D().f();", "parent class Base has no member 'x'", check_mode::off},
				{"static", "class C { static int s = 0; }", "var z = C::nope;", "class C has no static member 'nope'", check_mode::off},
				{"call an instance", "class Vec2 { int y = 0; } var v = Vec2();", "v();", "Cannot call 'v': it is a Vec2, not a function", check_mode::off},
				{"call a string", "var s = \"x\";", "s();", "Cannot call 's': it is a string, not a function", check_mode::off},
				{"call an int member", "class H { int x = 1; } var h = H();", "h.x(1);", "Cannot call 'x': it is an int, not a function", check_mode::off},
				{"call null", "var f = null;", "f();", "Cannot call 'f': it is null, not a function", check_mode::off},
				{"call a computed null", "var fs = [null];", "fs[0]();", "Cannot call null: it is not a function", check_mode::off},
				{"call a computed int", "var fs = [5];", "fs[0](1);", "Cannot call a value of type int: it is not a function", check_mode::off},
			});
		});
		test("family_c_arity_and_overload_texts", [this]() {
			expect_texts({
				{"method arity", "class V { int dot(int a) { return a; } }", "V().dot();", "'dot' expects 1 argument, got 0", check_mode::off},
				{"function arity", "function fib(int n) { return n; }", "fib(1, 2);", "'fib' expects 1 argument, got 2", check_mode::off},
				{"lambda arity", "var l = [](int a) { return a; };", "l();", "lambda expects 1 argument, got 0", check_mode::off},
				{"overload list", "class V { int dot(int a) { return a; } int dot(int a, int b) { return a + b; } }", "V().dot(V(), null, [1], {\"k\": 1});", "No overload of 'dot' takes (V, null, array, map)", check_mode::off},
				{"overload list cap", "class M { int m(int a) { return a; } int m(int a, int b) { return b; } }", "M().m(1, 2, 3, 4, 5, 6, 7);", "No overload of 'm' takes (int, int, int, int, int, int, ...)", check_mode::off},
				{"namespace overloads", "namespace ns { function f(int a, int b) { return 1; } function f(string s) { return 2; } }", "ns::f(1, 2, 3);", "No overload of 'f' takes (int, int, int)", check_mode::off},
				{"constructor overloads", "class Vec2 { Vec2(int x) {} Vec2(int x, int y) {} }", "Vec2(\"s\", 1, 2);", "No constructor of 'Vec2' takes (string, int, int)", check_mode::off},
				{"direct super", "class Base { Base(int x) {} } class D : Base { D() : super(\"s\", 2) {} }", "D();", "No matching parent constructor found for super()", check_mode::off},
				{"ancestor initializer", "class A { A(int x) {} } class B : A { B(int y) : super(\"s\", 1) {} } class C : B { C() : super(1) {} }", "C();", "No constructor of 'A' takes (string, int)", check_mode::off},
				{"delegation", "class Vec2 { Vec2(int x) {} Vec2() : this(\"s\", 1) {} }", "Vec2();", "No constructor of 'Vec2' takes (string, int)", check_mode::off},
				{"builtin push", "var a = [];", "a.push();", "push() takes exactly one argument", check_mode::off},
				{"builtin size", "var a = [];", "a.size(1);", "size() takes no arguments", check_mode::off},
				{"math pow", "", "math::pow(2);", "math::pow expects 2 arguments", check_mode::off},
			});
		});
		test("family_d_typed_boundary_texts", [this]() {
			expect_texts({
				{"lambda parameter", "var l = [](int n) { return n; }; var s = \"x\";", "l(s);", "Cannot pass string to parameter 'n' of lambda (declared int)", check_mode::strict},
				{"function parameter", "function fib(int n) { return n; } var s = \"x\";", "fib(s);", "Cannot pass string to parameter 'n' of 'fib' (declared int)", check_mode::strict},
				{"typed return", "function f() -> int { var s = \"x\"; return s; }", "f();", "Cannot return string from 'f' (declared -> int)", check_mode::strict},
				{"assignment", "int x = 0; var s = \"s\";", "x = s;", "Cannot assign string to 'x' (declared int)", check_mode::strict},
				{"declaration", "var s = \"s\";", "int x = s;", "Cannot initialize 'x' (declared int) with string", check_mode::strict},
				{"field", "class Vec2 { int s = 0; } var v = Vec2(); var str = \"q\";", "v.s = str;", "Cannot assign string to field 's' of Vec2 (declared int)", check_mode::strict},
				{"static", "class C { static int s = 0; } var str = \"q\";", "C::s = str;", "Cannot assign string to static 's' of C (declared int)", check_mode::strict},
				{"class into class", "class A { int a = 0; } class B { int b = 0; } A x = A(); var b = B();", "x = b;", "Cannot assign B to 'x' (declared A)", check_mode::strict},
				{"weak handle declaration", "class Foo { int v = 0; } var f = Foo();", "weak_ptr<Foo> w = f;", "Cannot initialize 'w' (declared weak_ptr<Foo>) with Foo", check_mode::off},
				{"weak handle assignment", "class Foo { int v = 0; } var f = Foo(); weak_ptr<Foo> w;", "w = f;", "Cannot assign Foo to 'w' (declared weak_ptr<Foo>)", check_mode::off},
				{"shared handle assignment", "class Foo { int v = 0; } shared_ptr<Foo> p = new Foo(); var s = \"x\";", "p = s;", "Cannot assign string to 'p' (declared shared_ptr<Foo>)", check_mode::off},
				{"class conversion", "class A { A(int x) {} } class B { int b = 0; } function f(A a) { return 1; } var b = B();", "f(b);", "Cannot convert B to A (no suitable single-argument constructor)", check_mode::off},
			});
		});
		test("checked_float_to_int_texts_leave_the_destination_unwritten", [this]() {
			const std::string text = "Cannot convert float to integer: non-finite or out of range";
			expect_texts({
				{"assignment", "var f = 1e100; int x = 0;", "x = f;", text.c_str(), check_mode::strict},
				{"itrunc", "var f = 1e100;", "var z = math::itrunc(f);", text.c_str(), check_mode::strict},
				{"ifloor", "var f = -1e100;", "var z = math::ifloor(f);", text.c_str(), check_mode::strict},
				{"cast", "var f = 1e100;", "var z = cast<int>(f);", text.c_str(), check_mode::strict},
			});
			expect("unwritten", "var f = 1e100; int x = 7; try { x = f; } catch (e) { } \"\" + x;", "7");
		});
		test("untouched_family_texts", [this]() {
			expect_texts({
				{"class not found", "", "var z = Nope::x;", "Class 'Nope' not found", check_mode::off},
				{"integer overflow in division", "var a = -9223372036854775807 - 1; var b = -1;", "var z = a / b;", "Integer overflow in '/'", check_mode::off},
				{"invalid plus", "var m = {}; var a = [1];", "var z = a + m;", "Invalid operands for + operator", check_mode::off},
				{"unary minus", "var s = \"x\";", "var z = -s;", "Unary minus requires numeric operand", check_mode::off},
				{"array index type", "var a = [1];", "var z = a[\"k\"];", "Array index must be an integer", check_mode::off},
				{"subscript on int", "var n = 5;", "var z = n[0];", "Subscript can only be used on arrays, maps, or types with [] operator", check_mode::off},
				{"pop empty", "var a = [];", "a.pop();", "Cannot pop from empty array", check_mode::off},
				{"front empty", "var a = [];", "var z = a.front();", "Cannot get front of empty array", check_mode::off},
				{"sort comparator", "var a = [2, 1];", "a.sort(5);", "sort() comparator must be a function", check_mode::off},
				{"pad_left fill", "var s = \"x\";", "var z = s.pad_left(3, \"\");", "pad_left() padding character cannot be empty", check_mode::off},
				{"to_int base", "var s = \"1\";", "var z = s.to_int(0, 1);", "to_int() base must be 0 or between 2 and 36", check_mode::off},
				{"code", "var s = \"\";", "var z = s.code();", "code() needs a string of exactly one UTF-8 character", check_mode::off},
				{"partition", "var s = \"a\";", "var z = s.partition(\"\");", "partition() separator cannot be empty", check_mode::off},
				{"split count", "var s = \"a\";", "var z = s.split(\",\", \"x\");", "split() second argument must be an integer", check_mode::off},
				{"from_code", "", "var z = string::from_code(-1);", "string::from_code expects a Unicode code point (0 to 0x10FFFF, no surrogate)", check_mode::off},
				{"join", "", "var z = string::join([1]);", "string::join expects an array of strings", check_mode::off},
				{"repeat", "", "var z = string::repeat(\"a\", -1);", "string::repeat count cannot be negative", check_mode::off},
				{"namespace pad_left", "", "var z = string::pad_left(\"a\", 3, \"\");", "string::pad_left fill must be a char or a non-empty string", check_mode::off},
				{"unknown namespace member", "", "var z = string::zzz_nope;", "Undefined variable 'string::zzz_nope'", check_mode::off},
				{"super without base", "class A { A() : super() {} }", "var z = A();", "Cannot call super() - class has no base class", check_mode::off},
				{"iterate int", "", "for (auto x : 5) { }", "Cannot iterate int: not an array, map, string or coroutine", check_mode::off},
				{"iterate null", "var n = null;", "for (auto x : n) { }", "Cannot iterate null: not an array, map, string or coroutine", check_mode::off},
				{"auto array homogeneity", "var s = \"x\";", "auto a = [1, 2, 3, s];", "Array 'auto' requires homogeneous elements - type mismatch at index 3", check_mode::off},
				{"reference uninitialized", "", "int& r;", "Reference variable must be initialized", check_mode::off},
				{"this at top level", "", "var z = this;", "'this' can only be used inside methods", check_mode::off},
			});
		});
		test("host_configuration_and_uncaught_suffix_texts", [this]() {
			every_config("host texts", "max_call_depth must be between 1 and INT_MAX|Maximum recursion depth exceeded - possible infinite recursion: Maximum environment recursion depth exceeded|Cannot access 'x' on null ('v8' is null): Member access on null value", [](engine& e) {
				std::string out;
				try { e.max_call_depth(backend_type::interpreter, 0); out += "accepted"; } catch (const std::exception& ex) { out += ex.what(); }
				out += "|";
				e.static_checking(check_mode::off);
				// not a tail call: a VM self tail call reuses its frame and runs into the budget instead (docs/diagnostics.md)
				e.execution_budget(0);
				try { e.execute("function r() { return r() + 1; } r();"); out += "no error"; } catch (const std::exception& ex) { out += ex.what(); }
				out += "|";
				try { e.execute("var v8 = null; v8.x;"); out += "no error"; } catch (const std::exception& ex) { out += ex.what(); }
				return out;
			});
		});
	}

	// An error raised by script a builtin or an operator runs on the program's behalf (a comparator, a predicate, a user
	// operator, to_bool(), to_string(), an archive hook) is that script's error: it propagates with its own text, catchable,
	// and the builtin stops calling back after it (docs/spec_conformance.md "Callback errors"; docs/diagnostics.md)
	void callback_errors() {
		test("a_sort_comparator_error_propagates_stops_the_calls_and_leaves_the_array", [this]() {
			expect("sort error", R"(
				int calls = 0;
				function inner() { int z = 0; calls += 1; int q = 5 / z; }
				array<int> v = [3, 1, 2];
				string r = "no error";
				try { v.sort([](int a, int b) { inner(); return a < b; }); } catch (e) { r = "caught:" + e; }
				r + "|calls=" + calls + "|" + v[0] + v[1] + v[2];
			)", "caught:Division by zero|calls=1|312");
			expect("sort throw", R"(
				array<int> v = [3, 1, 2];
				string r = "no error";
				try { v.sort([](int a, int b) -> bool { if (a > -100) { throw "cmp"; } return a < b; }); } catch (e) { r = "caught:" + e; }
				r + "|" + v[0] + v[1] + v[2];
			)", "caught:cmp|312");
		});
		test("an_uncaught_comparator_error_ends_the_program_with_the_callbacks_trace", [this]() {
			every_config("sort uncaught", "Division by zero|at inner|at <lambda>", [](engine& e) {
				try {
					e.execute(R"(
						int calls = 0;
						void inner() { int z = 0; calls += 1; int q = 5 / z; }
						array<int> v = [3, 1, 2];
						v.sort([](int a, int b) { inner(); return a < b; });
					)");
				} catch (const std::exception& ex) {
					const std::string w = ex.what();
					const std::string trace = e.format_stack_trace();
					std::string out = w.find("Division by zero") != std::string::npos ? "Division by zero" : w;
					out += trace.find("at inner") != std::string::npos ? "|at inner" : "|no inner frame: " + trace;
					out += trace.find("at <lambda>") != std::string::npos ? "|at <lambda>" : "|no lambda frame";
					return out;
				}
				return std::string("no error");
			});
		});
		test("user_operator_errors_propagate_with_their_own_text", [this]() {
			const char* K = R"(
				class K {
					int x = 1;
					K(int v) { x = v; }
					function operator+(int n) -> K { int z = 0; return K(x / z); }
					function operator<(K o) -> bool { int z = 0; return x / z < 1; }
					function operator==(K o) -> bool { int z = 0; return x / z == 1; }
				}
				K k = K(4);
				K k2 = K(5);
			)";
			const char* div = "Division by zero";
			const std::string KH = std::string(K) + "class H { K m = K(2); } H h = H();";
			expect_texts({
				{"operator+", K, "K k3 = k + 1;", div, check_mode::strict},
				{"operator<", K, "bool b = k < k2;", div, check_mode::strict},
				{"operator==", K, "bool b = k == k2;", div, check_mode::strict},
				{"operator!= through ==", K, "bool b = k != k2;", div, check_mode::strict},
				{"compound on a global", K, "k += 1;", div, check_mode::strict},
				{"compound on a local", K, "function f() { K a = K(4); a += 1; } f();", div, check_mode::strict},
				{"compound on a field", KH.c_str(), "h.m += 1;", div, check_mode::strict},
			});
		});
		test("to_bool_and_to_string_errors_propagate", [this]() {
			const char* Q = R"(
				class Q { bool to_bool() { int z = 0; return 1 / z > 0; } }
				class TS { string to_string() { int z = 0; return "t" + (1 / z); } }
				auto q = Q();
				auto ts = TS();
			)";
			const char* div = "Division by zero";
			expect_texts({
				{"if (q)", Q, "if (q) { int w = 1; }", div, check_mode::strict},
				{"!q", Q, "bool b = !q;", div, check_mode::strict},
				{"q ? :", Q, "int w = q ? 1 : 2;", div, check_mode::strict},
				{"bool slot", Q, "bool b = q;", div, check_mode::strict},
				{"string + object", Q, "string s = \"x\" + ts;", div, check_mode::strict},
				{"object + string", Q, "string s = ts + \"x\";", div, check_mode::strict},
			});
		});
		test("a_user_operator_assign_error_propagates_with_its_own_text", [this]() {
			const char* A2 = "class A2 { int v = 1; function operator=(int x) -> A2 { int z = 0; v = x / z; return this; } } A2 g = A2();";
			expect_texts({
				{"operator= on a global", A2, "g = 5;", "Division by zero", check_mode::strict},
				{"operator= on a local", A2, "function f() { A2 a = A2(); a = 5; } f();", "Division by zero", check_mode::strict},
			});
		});
		test("archive_hooks_the_script_declares_propagate_their_errors", [this]() {
			const char* H = R"(
				class P { int v = 1; int _get_v() { int z = 0; return v / z; } }
				class PT { int v = 1; int _get_v() { if (v > 0) { throw "getthrow"; } return v; } }
				class L { int v = 1; void post_load(int version) { int z = 0; v = v / z; } }
				class S { int v = 1; void _set_v(int x) { int z = 0; v = x / z; } }
			)";
			const char* div = "Division by zero";
			expect_texts({
				{"to_json getter", H, "string s = to_json(P());", div, check_mode::strict},
				{"to_json getter throw", H, "string s = to_json(PT());", "getthrow", check_mode::strict},
				{"to_binary getter", H, "string s = to_binary(P());", div, check_mode::strict},
				{"from_json post_load", H, "var o = from_json(\"{\\\"_type_\\\":\\\"L\\\",\\\"v\\\":3}\");", div, check_mode::strict},
				{"from_json setter", H, "var o = from_json(\"{\\\"_type_\\\":\\\"S\\\",\\\"v\\\":3}\");", div, check_mode::strict},
			});
			expect("hooks that succeed", R"(
				class L2 { int v = 1; void post_load(int version) { v = v + 10 * version; } }
				class S2 { int v = 1; void _set_v(int x) { v = x * 2; } }
				var a = from_json("{\"_type_\":\"L2\",\"v\":3}");
				var b = from_json("{\"_type_\":\"S2\",\"v\":3}");
				"" + a.v + "|" + b.v;
			)", "13|6");
		});
		test("a_default_sort_of_instances_orders_by_the_languages_less_than", [this]() {
			// CLAUDE.md: ordering stays off containers and instances - an instance sorts by its user operator< (C++ std::sort's
			// operator<), and an element with no order raises the < operator's rejection instead of sorting by address
			expect("default sort", R"(
				class K { int v = 0; K(int x) { v = x; } }
				class L { int v = 0; L(int x) { v = x; } bool operator<(L o) { return v < o.v; } }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { array<L> ls = [L(3), L(1), L(2)]; ls.sort(); return "" + ls[0].v + ls[1].v + ls[2].v; }) + "|" +
				t([]() { var ls = [L(3), L(1), L(2)]; ls.sort(); return "" + ls[0].v + ls[1].v + ls[2].v; }) + "|" +
				t([]() { array<K> ks = [K(3), K(1)]; ks.sort(); return "sorted"; }) + "|" +
				t([]() { var a = [[3], [1]]; a.sort(); return "sorted"; }) + "|" +
				t([]() { var a = [3, "a", 1.5, true]; a.sort(); return to_json(a); });
			)", "123|123|Invalid operands for < operator|Invalid operands for < operator|[1.5,3,\"a\",true]", check_mode::warn);
		});
		test("predicate_and_parallel_body_errors_propagate", [this]() {
			const char* div = "Division by zero";
			expect_texts({
				{"array filter", "", "var b = [1, 2].filter([](var x) { int z = 0; return x / z > 0; });", div, check_mode::warn},
				{"array remove_if", "var a = [1, 2];", "a.remove_if([](var x) { int z = 0; return x / z > 0; });", div, check_mode::warn},
				{"map filter", "var m = {\"a\": 1};", "var r = m.filter([](var k, var v) { int z = 0; return v / z > 0; });", div, check_mode::warn},
				{"map remove_if", "var m = {\"a\": 1};", "m.remove_if([](var k, var v) { int z = 0; return v / z > 0; });", div, check_mode::warn},
				{"parallel_for body", "array<int> xs = [1, 2, 3, 4];", "parallel_for (auto& x : xs) { int z = 0; x = x / z; }", div, check_mode::strict},
			});
		});
	}

	// docs/static_checking.md rule table: on plain assignment an object slot takes a source through a user operator= of its
	// class (C++ assignment: the operator runs on the held instance, exact match ahead of a converting constructor); the
	// expression's value is the destination's (CLAUDE.md assignment-expression values); container elements need the exact
	// type (CLAUDE.md element conversions), so an element store never dispatches it
	void user_assignment_operator() {
		const char* OK = R"(
			class OK2 { int v = 1; function operator=(int x) -> OK2 { v = x * 10; return this; } }
			class H { OK2 m = OK2(); static OK2 s = OK2(); void set_bare() { m = 7; } void set_this() { this.m = 8; } }
			namespace ns { OK2 n = OK2(); }
			OK2 g = OK2();
		)";
		test("operator_assign_runs_at_every_plain_store", [this, OK]() {
			expect("destinations", std::string(OK) + R"(
				function local() -> int { OK2 a = OK2(); a = 5; return a.v; }
				function auto_local() -> int { auto a = OK2(); a = 4; return a.v; }
				function field() -> int { H h = H(); h.m = 5; return h.m.v; }
				function bare() -> int { H h = H(); h.set_bare(); return h.m.v; }
				function this_field() -> int { H h = H(); h.set_this(); return h.m.v; }
				g = 3;
				H::s = 2;
				ns::n = 6;
				"" + local() + "|" + auto_local() + "|" + field() + "|" + bare() + "|" + this_field() + "|" + g.v + "|" + H::s.v + "|" + ns::n.v;
			)", "50|40|50|70|80|30|20|60");
		});
		test("operator_assign_value_is_the_destination_and_a_copy", [this, OK]() {
			expect("expression value", std::string(OK) + R"(
				function local() -> string { OK2 a = OK2(); var r = (a = 6); r.v = 1; return type_of(r) + ":" + a.v; }
				function global() -> string { var r = (g = 9); r.v = 2; return type_of(r) + ":" + g.v; }
				local() + "|" + global();
			)", "OK2:60|OK2:90");
		});
		test("same_class_and_subclass_sources_copy_and_exact_operator_beats_the_converting_constructor", [this]() {
			expect("copy and precedence", R"(
				class B2 { int v = 1; B2() {} B2(int x) { v = x; } function operator=(int x) -> B2 { v = x * 100; return this; } }
				class D2 : B2 { D2() { v = 42; } }
				function run() -> string {
					B2 a = B2(); B2 b = B2(); b.v = 7; a = b;
					B2 c = B2(); c = D2();
					B2 d = B2(); d = 3;
					B2 e = 4;
					return "" + a.v + "|" + c.v + "|" + d.v + "|" + e.v;
				}
				run();
			)", "7|42|300|4");
		});
		test("an_element_store_needs_the_exact_type", [this, OK]() {
			expect_texts({
				{"array element", OK, "array<OK2> xs = [OK2()]; xs[0] = 5;", "Cannot assign 'int' to element of type 'OK2'", check_mode::warn},
			});
		});
	}

	// docs/diagnostics.md: every runtime error a catch sees has a text; one raised without its own reads as its code's
	// message, the text the host boundary shows (`what()` of the uncaught error)
	void textless_errors() {
		test("a_compound_error_on_a_variable_has_a_text", [this]() {
			expect_texts({
				{"local /= 0", "", "function f() { int x = 5; int z = 0; x /= z; } f();", "Division by zero", check_mode::warn},
				{"global /= 0", "int gx = 5; int gz = 0;", "gx /= gz;", "Division by zero", check_mode::warn},
				{"local -= string", "", "function f() { int x = 5; var s = \"s\"; x -= s; } f();", "Cannot apply '-=' to int and string", check_mode::warn},
				{"string += int", "", "function f() { string s = \"a\"; s += 5; } f();", "Cannot apply '+=' to string and int", check_mode::warn},
				{"bool += int", "", "function f() { bool b = true; b += 1; } f();", "Cannot apply '+=' to bool and int", check_mode::warn},
			});
		});
		test("the_catch_text_matches_the_uncaught_text", [this]() {
			every_config("host text", "Cannot apply '+=' to string and int|Cannot apply '+=' to string and int", [](engine& e) {
				e.static_checking(check_mode::warn);
				const std::string caught = e.execute("string r = \"\"; function f() { string s = \"a\"; s += 5; } try { f(); } catch (x) { r = x; } r;").as_string();
				std::string uncaught;
				try { e.execute("f();"); } catch (const std::exception& ex) { uncaught = ex.what(); }
				return caught + "|" + uncaught.substr(0, caught.size());
			}, check_mode::warn);
		});
	}

	// docs/static_checking.md rule table and precision map: load verdicts (engine::check reports, nothing runs)
	void checker_verdicts() {
		test("checker_verdicts_follow_the_rule_table", [this]() {
			struct row { const char* label; const char* src; size_t errors; };
			const row rows[] = {
				{"map into int", "int x = {\"k\": 1};", 1},
				{"typed array into string array", "array<int> a = [1]; array<string> b = a;", 1},
				{"typed array into float array", "array<int> a = [1]; array<float> c = a;", 0},
				{"containers and callables into a class slot", "class C { int v = 0; } C a = [1]; C b = {\"k\": 1}; C d = [](){ return 1; };", 3},
				{"auto from null stays dynamic", "auto x = null; x = 1; x = \"s\";", 0},
				{"a typed reference parameter binds only its type", "function f(int& r) { } string s = \"x\"; f(s);", 1},
				{"itrunc is int", "char c = math::itrunc(1.5);", 1},
				{"sin is float", "char c = math::sin(1.0);", 1},
				{"sin into int converts", "int i = math::sin(1.0);", 0},
				{"abs and max follow their operands", "char a = math::abs(1); char b = math::abs(1.5); char c = math::max(1, 2.5);", 2},
				{"random_seed is unknown", "char c = math::random_seed(1);", 0},
				{"math arity is a runtime error", "var z = math::pow(2);", 0},
				{"namespace paths are lenient", "namespace a::b { int c = 1; } var x = a::b::zzz;", 0},
				{"handle builtins on any object", "class C { int v = 0; } C c = C(); int n = c.use_count(); bool u = c.unique(); c.reset();", 0},
				{"a class method wins over the handle builtin", "class D { string use_count() { return \"m\"; } } int n = D().use_count();", 1},
				{"builtin results stay lenient", "map<string, int> m = {\"a\": 1}; var k = m.keys()[0].to_upper();", 0},
				{"element stores through a range reference are runtime", "array<int> a = [1]; for (auto& x : a) { x = \"s\"; }", 0},
				{"out of bounds is runtime", "array<int> a = [1]; int x = a[5];", 0},
				{"namespace variables are dynamic reads", "namespace ns { int x = 1; } char c = ns::x;", 0},
				{"missing map key is runtime", "map<string, int> m = {\"a\": 1}; int x = m[\"missing\"];", 0},
			};
			for (const auto& r : rows) {
				every_config(r.label, std::to_string(r.errors), [&](engine& e) { return std::to_string(e.check(r.src).error_count()); });
			}
		});
		test("a_bare_return_in_a_typed_function_is_a_warning_not_an_error", [this]() {
			every_config("bare return", "0|1", [](engine& e) {
				const auto rep = e.check("function f() -> int { return; }");
				size_t warnings = 0;
				for (const auto& d : rep.diagnostics) { if (d.severity != check_diagnostic::level::error) { ++warnings; } }
				return std::to_string(rep.error_count()) + "|" + std::to_string(warnings > 0 ? 1 : 0);
			});
		});
		test("a_host_global_types_its_uses", [this]() {
			every_config("host global", "1", [](engine& e) {
				e.add_global("g", script_value(script_int(5), &e));
				return std::to_string(e.check("char c = g;").error_count());
			});
		});
		test("strict_loads_value_dependent_failures_and_the_runtime_raises_them", [this]() {
			expect("runtime", R"(
				map<string, int> m = {"a": 1};
				string r = "";
				try { int x = m["missing"]; } catch (e) { r += e + "|"; }
				array<int> a = [1];
				try { for (auto& x : a) { x = "s"; } } catch (e) { r += "raised|"; }
				try { int y = a[5]; } catch (e) { r += e; }
				r;
			)", "Cannot initialize 'x' (declared int) with null|raised|Array index 5 out of bounds for array of size 1");
		});
		test("bitwise_compound_assignments_are_x_op_equals_y_with_x_evaluated_once", [this]() {
			// Dev ruling 2026-09-28: &= |= ^= <<= >>= on every lvalue (a local, a field, this's field, a static, a namespace
			// variable, an element, a map entry), the stored value as the expression's value, the operand text
			expect("bitwise compound", R"(
				class Flags { int f = 1; static int s = 1; void bump() { f |= 8; } }
				namespace ns { int m = 12; }
				int calls = 0;
				function at() -> int { calls += 1; return 0; }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { int x = 6; x &= 3; x |= 8; x ^= 1; x <<= 2; x >>= 1; return x; }) + "|" +
				t([]() { Flags o; o.f |= 6; o.bump(); Flags::s <<= 4; ns::m &= 4; return "" + o.f + " " + Flags::s + " " + ns::m; }) + "|" +
				t([]() { array<int> a = [5, 3]; var v = [12]; map<string, int> m; a[at()] &= 4; v[0] >>= 2; m["k"] |= 3; return "" + a[0] + " " + v[0] + " " + m["k"] + " " + calls; }) + "|" +
				t([]() { int x = 5; int y = (x ^= 3); return y; }) + "|" +
				t([]() { var s = 1.5; s &= 1; return s; }) + "|" +
				t([]() { int x = 1; x <<= 64; return x; }) + "|" +
				t([]() { var w = [1.5]; w[0] |= 1; return w[0]; });
			)", "22|15 16 4|4 3 3 1|6|Cannot apply '&=' to float and int|Shift amount must be between 0 and 63|Cannot apply '|=' to float and int",
			   check_mode::warn);
			every_config("checker", "1|0", [](engine& e) {
				auto bad = e.check("float f = 1.5; f &= 1;");
				auto ok = e.check("int x = 6; x &= 3; char c = 'a'; int y = 1; y |= c;");
				return std::to_string(bad.error_count()) + "|" + std::to_string(ok.error_count());
			});
		});
	}

	// docs/LOCAL_VARIABLES_BEHAVIOR.md: instance variables are an overlay for one execution, not dynamic scope
	void instance_variable_overlays() {
		test("an_instance_variable_overlay_lives_for_one_execution", [this]() {
			every_config("overlay", "5|1|raised|raised|5", [](engine& e) {
				e.static_checking(check_mode::off);
				e.add_global("G", script_value(script_int(1), &e));
				instance_variables five{{"G", script_value(script_int(5), &e)}};
				std::string out = std::to_string(e.execute("G", five).as_int());
				out += "|" + std::to_string(e.execute("G").as_int());
				instance_variables n0{{"n", script_value(script_int(0), &e)}};
				e.execute("var temp = 1;", n0);
				try { e.execute("temp"); out += "|visible"; } catch (const std::exception&) { out += "|raised"; }
				try { e.execute("n"); out += "|visible"; } catch (const std::exception&) { out += "|raised"; }
				instance_variables n5{{"n", script_value(script_int(5), &e)}};
				out += "|" + std::to_string(e.execute("function f8() { return n; } f8()", n5).as_int());
				return out;
			});
		});
	}

	// CLAUDE.md "Files and locations"
	void file_locations() {
		test("file_name_is_lexical_and_parent_dir_canonicalizes", [this]() {
			expect("locations", R"(
				string d = working_dir();
				"" + file_name("no/such/dir/x.jai") + "|" + to_string(parent_dir(d + "/x.jai") == parent_dir(d + "/sub/../x.jai"));
			)", "x.jai|true");
		});
		test("a_bare_engine_has_no_host_file_functions", [this]() {
			every_config("bare engine", "Undefined variable 'read_file'", [](engine&) {
				auto bare = engine::make();
				bare->static_checking(check_mode::off);
				return bare->execute("var r = \"\"; try { read_file(\"x\"); } catch (e) { r = e; } r;").as_string();
			});
		});
		test("a_spliced_include_keeps_its_declarations_in_the_function_and_a_computed_include_sees_globals", [this]() {
			const auto dir = jai::foundry::test_temp_directory() / "spec_conformance_includes";
			std::filesystem::create_directories(dir);
			std::ofstream(dir / "twice.jai") << "function twice(int v) -> int { return v * 2; }\n";
			std::ofstream(dir / "reads_g.jai") << "G * 2;\n";
			every_config("includes", "2|Undefined variable 'twice'|8", [&](engine& e) {
				e.add_include_path(dir.string());
				e.static_checking(check_mode::off);
				std::string out = std::to_string(e.execute("function run(int x) { include \"twice.jai\"; return twice(x); } run(1);").as_int());
				out += "|" + e.execute("var r = \"\"; try { twice(3); } catch (err) { r = err; } r;").as_string();
				out += "|" + std::to_string(e.execute("var G = 4; function run2() { var p = \"reads_g.jai\"; return include(p); } run2();").as_int());
				return out;
			});
		});
	}

	// CLAUDE.md "Custom permissions": the first missing built-in, then custom names in registration order
	void privilege_order() {
		test("a_denial_names_the_first_missing_permission_in_registration_order", [this]() {
			every_config("denial order", "Insufficient privilege: app.b.|Insufficient privilege: filesystem.", [](engine& e) {
				e.register_privilege("app.b");
				e.register_privilege("app.a");
				e.add_function("f", []() -> script_int { return 1; }, {"app.a", "app.b"});
				e.add_function("g2", []() -> script_int { return 2; }, {"filesystem", "app.a"});
				e.disable(jai::filesystem);
				std::string out = e.execute("var r = \"\"; try { f(); } catch (x) { r = x; } r;").as_string();
				out += "|" + e.execute("var r2 = \"\"; try { g2(); } catch (x) { r2 = x; } r2;").as_string();
				return out;
			});
		});
	}

	// docs/site/guide examples with their stated outputs
	void guide_examples() {
		test("guide_operator_precedence_and_associativity", [this]() {
			// docs/grammar.md precedence (lowest to highest): assignment (right), ternary (right), ||, &&, |, ^, &, equality, relational, shift, additive
			expect("precedence", R"(
				var a = 0; var b = 0; a = b = 5;
				"" + a + b + "|" + to_string(true || false && false) + "|" + (1 | 2 ^ 3) + "|" + to_string(1 < 2 == true) + "|" + (1 << 2 + 1) + "|" +
				(-2 * 3) + "|" + to_string(!false == true) + "|" + (2 | 1 ^ 3 & 1) + "|" + (8 >> 1 + 1) + "|" + (true ? 1 : false ? 2 : 3) + (false ? 1 : true ? 2 : 3);
			)", "55|true|1|true|8|-6|true|2|2|12");
		});
		test("guide_function_and_lambda_examples", [this]() {
			expect("functions", R"(
				var mul4(var a, var b) { return a * b; }
				var pick(bool flag) { return flag ? 42 : "none"; }
				auto ops = {"double": [](x) { return x * 2; }, "square": [](x) { return x * x; }};
				function g(int x) { return x > 0 ? x : throw "neg"; }
				function f() -> int { int a = 1; int m = 10; auto h = [&, m]() { a = 7; }; h(); return a * 100 + m; }
				string e = ""; try { g(-1); } catch (x) { e = x; }
				"" + mul4(2.5, 2) + "|" + pick(true) + "|" + pick(false) + "|" + ops["square"](7) + "|" + g(2) + "|" + e + "|" + f();
			)", "5.0|42|none|49|2|neg|710");
		});
		test("guide_class_examples", [this]() {
			expect("classes", R"(
				class Cat { int a = 0; } class Tiger : Cat { int stripes = 12; }
				int g = 5;
				class C { int m() { int a = g; int g = 9; return a * 10 + g; } }
				class S { int f = 3; static int s = 4; int use() { return f + s; } static int st() { return s; } }
				class A { int v = 1; };
				class Vec { float x = 0.0; Vec(float a) { x = a; } Vec operator+(Vec o) { return Vec(x + o.x); } }
				string err = ""; try { var t = cast<Tiger&>(Cat()).stripes; } catch (x) { err = x; }
				"" + err + "|" + C().m() + "|" + S().use() + " " + S::st() + "|" + A().v + "|" + (Vec(1.0) + Vec{3.0}).x;
			)", "Cannot cast 'Cat' to 'Tiger&'|59|7 4|1|4.0");
		});
		test("guide_container_and_string_examples", [this]() {
			expect("containers", R"(
				auto g = [1, 2, 3];
				string c = "";
				try { g.push("s"); } catch (e) { } try { g.push("t"); } catch (e) { c = e; }
				map<var, int> anything = {}; anything[5] = 1; anything["five"] = 5;
				var s = "Jai\x21"; var word = (s[0] + 0) | ((s[1] + 0) << 8);
				var e = ""; try { var z = 10 / 0; } catch (x) { e = x; }
				c + "|" + to_json(g) + "|" + to_json(anything) + "|" + "b".compare("a") + "|" + word + "|" + e + "|" + `[${"ab":^5}] [${7:*^4}]`;
			)", "Cannot push 'string' to array<int>|[1,2,3]|{\"$map\":[[5,1],[\"five\",5]]}|1|24906|Division by zero|[ ab  ] [*7**]");
		});
		test("guide_parallel_transform_rows", [this]() {
			expect("transform", R"(
				var rows = [[1, 10], [2, 20]];
				var f(var p) { return [p[1], p[0] * 2]; }
				to_json(parallel_transform(rows, f));
			)", "[[10,2],[20,4]]");
		});
	}

	// docs/parallel_design.md contracts
	void parallel_contracts() {
		test("a_helper_that_writes_a_global_hits_the_parallel_write_wall", [this]() {
			// docs/parallel_design.md section 1: a callee that mutates a global hits the same wall; the source is unchanged
			expect("transitive write", R"(
				var g = 0;
				function helper(int v) -> int { g = v; return v; }
				array<int> a = [1, 2, 3];
				string r = "ok";
				try { parallel_for (auto& x : a) { x = helper(x); } } catch (e) { r = e; }
				to_string(r.contains("enclosing state 'g'")) + "|" + to_json(a) + "|" + g;
			)", "true|[1,2,3]|0");
		});
		test("cells_in_a_parallel_worker_match_the_serial_rows", [this]() {
			expect("worker cells", R"(
				array<int> G = [3, 1, 4, 1, 5, 9, 2, 6];
				function f(int row) -> int { int acc = 0; for (int k = 0; k < 8; ++k) { acc = acc + G[(row + k) % 8] * k; } return acc; }
				var rows = []; for (int i = 0; i < 64; i++) { rows.push(i); }
				var par = parallel_transform(rows, f);
				int bad = 0; for (int i = 0; i < 64; i++) { if (par[i] != f(i)) { bad++; } }
				"" + bad + "|" + par[0];
			)", "0|131");
		});
		test("parallel_for_iterates_arrays_only", [this]() {
			expect("map range", R"(
				var m = {"a": 1};
				string r = "ran";
				try { parallel_for (auto& kv : m) { } } catch (e) { r = "raised"; }
				r;
			)", "raised");
		});
	}

	void native_exceptions() {
		test("a_bound_accessor_method_or_operator_that_throws_is_catchable_where_it_is_called", [this]() {
			// every host exception at the boundary is the catchable runtime error with its text, at top level and inside a
			// function, and an operand the operator cannot take raises the overload text instead of escaping
			const std::string src = R"(
				function t(function f) -> string { string r = "none"; try { f(); } catch (e) { r = e; } return r; }
				string top = "none";
				try { Probe q = Probe(); q.v = -1; } catch (e) { top = e; }
				string in_fn() { Probe p = Probe(); string r = "none"; try { p.v = -2; } catch (e) { r = e; } return r + ":" + p.v; }
				Probe p = Probe();
				top + "|" + in_fn() + "|" +
				t([]() { Probe a = Probe(); a.v = -3; }) + "|" +
				t([]() { Probe a = Probe(); a.lv = 500; }) + "|" +
				t([]() { Probe a = Probe(); var x = a.bad; }) + "|" +
				t([]() { Probe a = Probe(); a.poke(0); }) + "|" +
				t([]() { Probe a = Probe(); a.v += -9; }) + "|" +
				t([]() { Probe a = Probe(); var b = a * -1; }) + "|" +
				(t([]() { Probe a = Probe(); var b = a + 5; }) != "none" ? "operand raised" : "operand accepted") + "|" + p.v;
			)";
			every_config("native boundary", "negative v|negative v:1|negative v|lv too big|getter failed|poke zero|negative v|negative scale|operand raised|1",
				[&](engine& e) { spec::bind_boundary_probe(e); return spec::render(e.execute(src)); }, check_mode::off);
		});
		test("a_host_script_exception_is_catchable_with_its_message", [this]() {
			every_config("host script_exception", "Random failure!", [](engine& e) {
				e.add_function("raise_it", []() -> script_int { throw jai::script_exception("Random failure!"); });
				return e.execute("var r = \"\"; try { raise_it(); } catch (x) { r = x; } r;").as_string();
			});
		});
	}

	// Access control (Dev ruling 2026-09-27, two levels as simple as possible): public: = everyone; private: = the declaring
	// class and every class derived from it, by any spelling and through any receiver of the family; protected: is a synonym
	// of private: (one message). Outside the family every read, write, call, compound and ++/-- raises
	// "Cannot access private member 'x' of class 'C'"; the strict checker reports it at load when the receiver class is
	// statically known (a warning in warn mode), with the same text
	void access_control() {
		const char* W = R"(
			class Wallet {
			private:
				int secret = 42;
				int hidden() { return 7; }
				static int vault = 5;
			protected:
				int prot = 3;
			public:
				int own_other(Wallet w) { return w.secret; }
				int peek_sub() { return tip; }
			}
			class Tipper : Wallet { private: int tip = 9; }
			class Clip : Wallet { }
			function bump(int& r) { r = r + 1; }
			class Purse : Wallet {
				int peek_secret() { return secret; }
				int poke_secret() { secret = 9; return secret; }
				int compound_secret() { secret += 1; return secret; }
				int inc_secret() { secret++; return secret; }
				int call_hidden() { return hidden() + super::hidden(); }
				int ref_secret() { auto& r = secret; r = 50; return secret; }
				int pass_ref() { bump(secret); return secret; }
				int lambda_secret() { auto l = [=]() { return secret; }; return l(); }
				int binary_secret() { return secret + 1; }
				int this_secret() { return this.secret; }
				int other(Wallet w) { return w.secret; }
				int sibling(Clip c) { return c.secret; }
				int statics() { return Wallet::vault; }
				int prot_synonym() { prot += 1; return prot; }
			}
			class Thief { int steal(Wallet w) { return w.secret; } }
			function t(function f) -> string {
				string r = "";
				try { r = "" + f(); }
				catch (e) {
					r = e == "Cannot access private member 'secret' of class 'Wallet'" ? "S"
					  : e == "Cannot access private member 'hidden' of class 'Wallet'" ? "H"
					  : e == "Cannot access private member 'vault' of class 'Wallet'" ? "V"
					  : e == "Cannot access private member 'prot' of class 'Wallet'" ? "P" : e;
				}
				return r + ",";
			}
		)";
		test("the_declaring_class_and_every_derived_class_reach_a_private_member_by_any_spelling", [this, W]() {
			expect("family", std::string(W) + R"(
				t([]() { return Purse().peek_secret(); }) + t([]() { return Purse().poke_secret(); }) +
				t([]() { return Purse().compound_secret(); }) + t([]() { return Purse().inc_secret(); }) +
				t([]() { return Purse().call_hidden(); }) + t([]() { return Purse().ref_secret(); }) +
				t([]() { return Purse().pass_ref(); }) + t([]() { return Purse().lambda_secret(); }) +
				t([]() { return Purse().binary_secret(); }) + t([]() { return Purse().this_secret(); }) +
				t([]() { return Purse().other(Wallet()); }) + t([]() { return Purse().sibling(Clip()); }) +
				t([]() { return Purse().statics(); }) + t([]() { return Purse().prot_synonym(); }) +
				t([]() { return Wallet().own_other(Purse()); });
			)", "42,9,43,43,14,50,43,42,43,42,42,42,5,4,42,", check_mode::off);
		});
		test("outside_the_family_every_spelling_raises_the_one_private_text", [this, W]() {
			expect("outside", std::string(W) + R"(
				t([]() { return Wallet().secret; }) + t([]() { Wallet w = Wallet(); w.secret = 1; return "w"; }) +
				t([]() { Wallet w = Wallet(); w.secret += 1; return "c"; }) + t([]() { Wallet w = Wallet(); w.secret++; return "i"; }) +
				t([]() { return Wallet().hidden(); }) + t([]() { return Wallet::vault; }) + t([]() { return Wallet().prot; }) +
				t([]() { return Thief().steal(Wallet()); }) + t([]() { var v = Purse(); return v.secret; }) +
				t([]() { return Tipper().peek_sub(); });
			)", "S,S,S,S,H,V,P,S,S,Cannot access private member 'tip' of class 'Tipper',", check_mode::off);
		});
		test("the_strict_checker_reports_an_outside_access_at_load_with_the_runtime_text", [this]() {
			every_config("checker", "1:Cannot access private member 'secret' of class 'Wallet'|0|0|0|1:Cannot access private member 'prot' of class 'Wallet'|"
			             "1:Cannot access private member 'secret' of class 'Wallet'|1:Cannot access private member 'vault' of class 'Wallet'|0|0", [](engine& e) {
				const std::string base = "class Wallet { private: int secret = 42; int hidden() { return 7; } static int vault = 5; protected: int prot = 3; } ";
				auto one = [&](const std::string& src) {
					const auto rep = e.check(base + src);
					std::string first;
					for (const auto& d : rep.diagnostics) { if (d.severity == check_diagnostic::level::error) { first = d.message; break; } }
					return std::to_string(rep.error_count()) + (first.empty() ? "" : ":" + first);
				};
				return one("int peek(Wallet w) { return w.secret; }") + "|" +
				       one("class Purse : Wallet { int peek() { return secret + hidden() + prot; } }") + "|" +
				       one("class Purse : Wallet { int peek(Wallet w) { return w.secret + this.secret + Wallet::vault; } }") + "|" +
				       one("class Purse : Wallet { } int f() { return 1; }") + "|" +
				       one("int peek(Wallet w) { return w.prot; }") + "|" +
				       one("class Thief { int steal(Wallet w) { return w.secret; } }") + "|" +
				       one("int f() { return Wallet::vault; }") + "|" +
				       one("int f() { var v = Wallet(); return v.secret; }") + "|" +
				       one("class Other { int n = 1; } int g(Other o) { return o.n; }");
			});
		});
		test("a_strict_load_with_an_outside_access_runs_nothing_and_warn_mode_runs_to_the_raise", [this]() {
			every_config("modes", "raised|0|S", [](engine& e) {
				e.add_global("ran", script_value(script_int(0), &e));
				const std::string src = "class Wallet { private: int secret = 42; } ran = 1; int peek(Wallet w) { return w.secret; } ";
				std::string out;
				try { e.execute(src); out = "loaded"; } catch (const std::exception&) { out = "raised"; }
				out += "|" + std::to_string(e.execute("ran").as_int());
				e.static_checking(check_mode::warn);
				out += "|" + e.execute(src + "string r = \"\"; try { peek(Wallet()); } catch (x) { r = x == \"Cannot access private member 'secret' of class 'Wallet'\" ? \"S\" : x; } r;").as_string();
				return out;
			});
		});
	}

	// Dev ruling 2026-09-27 (docs/closures_and_captures.md): a program-scope variable is never captured by default - [=],
	// [&] and bare [] read it when the lambda runs, [&g] is the same as not listing it, an explicit [g] copies it at creation;
	// an init-capture [x = e] copies e, evaluated at creation in the enclosing scope, into the lambda's own x. Locals and
	// parameters keep their contract (bare [] and [=] copy, [&] shares the cell)
	void lambda_globals_and_init_captures() {
		test("a_global_resolves_when_the_lambda_runs_unless_an_explicit_capture_copies_it", [this]() {
			expect("globals", R"(
				var g = 1;
				function t1() -> int { auto l = [=]() { return g; }; g = 5; return l(); }
				function t2() -> int { auto l = []() { return g; }; g = 6; return l(); }
				function t3() -> int { auto l = [g]() { return g; }; g = 7; return l(); }
				function t4() -> int { auto l = [&g]() { return g; }; g = 8; return l(); }
				function t5() -> int { auto l = [x = g]() { return x; }; g = 9; return l(); }
				function t6() -> int { auto l = [&]() { g = 10; }; l(); return g; }
				function t7() -> int { auto l = [=]() { g = 11; }; l(); return g; }
				function t8() -> int { int k = 1; auto l = [=]() { return k; }; k = 2; return l(); }
				function t9() -> int { int k = 1; auto l = [&]() { return k; }; k = 2; return l(); }
				"" + t1() + "," + t2() + "," + t3() + "," + t4() + "," + t5() + "," + t6() + "," + t7() + "," + t8() + "," + t9();
			)", "5,6,6,8,8,10,11,1,2");
		});
		test("a_top_level_lambda_reads_a_global_when_it_runs", [this]() {
			expect("top level", R"(
				var h = 1; auto lh = [=]() { return h; }; h = 2;
				var h2 = 1; auto lc = [h2]() { return h2; }; h2 = 2;
				function make() { return [=]() { return h; }; }
				auto late = make(); h = 3;
				"" + lh() + "," + lc() + "," + late();
			)", "3,1,3");
		});
		test("an_init_capture_copies_its_expression_at_creation", [this]() {
			expect("init captures", R"(
				function run() -> string {
					int x = 1; int y = 2;
					auto a = [x = y * 10]() { return x; };
					auto b = [z = x + y, &y]() { y = 100; return z; };
					int bz = b();
					string s = "q";
					auto c = [s2 = s + "!"]() { return s2; };
					s = "changed";
					array<int> arr = [1];
					auto d = [cp = arr]() { return cp.size(); };
					arr.push(2);
					auto e = [=, w = 5]() { return w + x; };
					return "" + a() + "," + bz + "," + y + "," + c() + "," + x + "," + d() + "," + e();
				}
				run();
			)", "20,3,100,q!,1,1,6");
		});
		test("a_reference_capture_takes_no_initializer", [this]() {
			every_config("ref init", "parse error", [](engine& e) {
				try { e.execute("int x = 1; auto l = [&r = x]() { return r; };"); return std::string("accepted"); }
				catch (const std::exception& ex) {
					const std::string w = ex.what();
					return std::string(w.find("A reference capture takes no initializer") != std::string::npos ? "parse error" : w);
				}
			});
		});
	}

	// Dev ruling 2026-09-27, C++17 [expr.ass]: in E1 op= E2 every value computation and side effect of E2 is sequenced before
	// E1 - the right side runs first, then the target's receiver, its read (a user getter included), the arithmetic and the store
	void compound_member_order() {
		test("a_member_compound_evaluates_its_right_side_before_the_target", [this]() {
			expect("member order", R"(
				string LOG = ""; int K = 0;
				class O { int f = 1; static int s = 1; int self_add() { this.f += (f = 10); return f; } }
				namespace ns { int x = 1; }
				O G = O();
				function obj() -> O { LOG += "t" + K; return G; }
				class H { int f = 1; int _get_g() { LOG += "g" + K; return f; } void _set_g(int v) { LOG += "s" + K; f = v; } }
				H hh = H();
				function t(function f) -> string { LOG = ""; K = 0; f(); return LOG; }
				function field() -> int { O o = O(); o.f += (o.f = 10); return o.f; }
				t([]() { obj().f += ++K; }) + "|" + t([]() { hh.g += ++K; }) + "|" +
				t([]() { O::s += (K = 3); LOG += "" + O::s; }) + "|" + t([]() { ns::x += (K = 4); LOG += "" + ns::x; }) + "|" +
				field() + "|" + O().self_add();
			)", "t1|g1s1|4|5|20|20", check_mode::warn);
		});
		test("a_right_side_error_comes_before_the_targets", [this]() {
			expect("member error order", R"(
				function bad() -> int { throw "rhs"; return 1; }
				namespace ns { int x = 1; }
				function t(function f) -> string { string r = "none"; try { f(); } catch (e) { r = e; } return r; }
				t([]() { var nv = null; nv.f += bad(); }) + "|" + t([]() { ns::missing += bad(); }) + "|" + t([]() { var nv = null; nv.f += 1; });
			)", "rhs|rhs|Cannot access 'f' on null ('nv' is null)", check_mode::off);
		});
	}

	// Dev ruling 2026-09-27, C++17 [expr.ass]: a[j] op= e evaluates e before a and j (the plain store already did)
	void compound_subscript_order() {
		test("a_subscript_compound_evaluates_its_right_side_before_the_container_and_index", [this]() {
			expect("subscript order", R"(
				string LOG = ""; int K = 0;
				array<int> A = [1, 2, 3];
				map<string, int> M = {"k": 1};
				function idx() -> int { LOG += "j" + K; return 1; }
				function key() -> string { LOG += "k" + K; return "k"; }
				function t(function f) -> string { LOG = ""; K = 0; f(); return LOG; }
				function self_write() -> int { array<int> a = [1, 2, 3]; a[1] += (a[1] = 10); return a[1]; }
				function index_write() -> string { array<int> a = [1, 2, 3]; int i = 0; a[i] += (i = 2); return "" + a[0] + a[2]; }
				t([]() { A[idx()] += ++K; }) + "|" + t([]() { A[idx()] = ++K; }) + "|" + t([]() { M[key()] += ++K; }) + "|" +
				self_write() + "|" + index_write();
			)", "j1|j1|k1|20|15", check_mode::warn);
		});
		test("a_right_side_error_comes_before_the_subscripts", [this]() {
			expect("subscript error order", R"(
				string LOG = "";
				function bad() -> int { throw "rhs"; return 1; }
				function idx() -> int { LOG += "j"; return 1; }
				array<int> A = [1, 2, 3];
				function t(function f) -> string { string r = "none"; LOG = ""; try { f(); } catch (e) { r = e; } return r + LOG; }
				t([]() { var na = null; na[idx()] += bad(); }) + "|" + t([]() { A[5] += bad(); }) + "|" + t([]() { A[5] += 1; });
			)", "rhs|rhs|Array index 5 out of bounds for array of size 3", check_mode::off);
		});
	}

	void auto_static_type() {
		test("an_auto_from_a_var_or_unknown_static_type_is_a_var", [this]() {
			// Dev ruling 2026-09-27: `auto x = e` takes e's static type; a var element, a var local, a var-returning call and a
			// mixed-branch ternary other than int with float have none, so x is a var (a later store of another type lands as is)
			const char* src = R"(
				var arr = [1, 2];
				array<var> av = [1];
				var v = 5;
				function f() -> var { return 5; }
				bool c = true;
				auto x = arr[0]; x = "s";
				auto y = av[0]; y = 2.5;
				auto z = v; z = "t";
				auto w = f(); w = 1.5;
				auto m = c ? 1 : "q"; m = 2.5;
				type_of(x) + ":" + x + "|" + type_of(y) + ":" + y + "|" + type_of(z) + ":" + z + "|" + type_of(w) + ":" + w + "|" + type_of(m) + ":" + m;
			)";
			const std::string want = "string:s|float:2.5|string:t|float:1.5|float:2.5";
			expect("auto from var", src, want, check_mode::warn);
			expect("auto from var (strict)", src, want, check_mode::strict);
			expect("auto from var (checks off)", src, want, check_mode::off);
		});
		test("an_auto_from_a_known_static_type_keeps_that_type", [this]() {
			const char* src = R"(
				array<int> ai = [1, 2];
				class P { int n = 0; }
				auto n = 5; n = 1.5;
				auto e = ai[0]; e = 2.9;
				auto s = "a"; s = "b" + 5;
				auto p = P();
				function t(function g) -> string { string r = "none"; try { g(); } catch (err) { r = err; } return r; }
				type_of(n) + ":" + n + "|" + type_of(e) + ":" + e + "|" + s + "|" + t([]() { auto q = P(); var o = 5; q = o; });
			)";
			expect("auto from known", src, "int:1|int:2|b5|Cannot assign int to 'q' (declared P)", check_mode::warn);
			expect("auto from known (checks off)", src, "int:1|int:2|b5|Cannot assign int to 'q' (declared P)", check_mode::off);
		});
		test("an_auto_in_a_counted_loop_follows_the_static_type_on_every_lane", [this]() {
			expect("auto in loops", R"(
				function from_var() -> string {
					var arr = [1, 2, 3]; string s = "";
					for (int i = 0; i < 3; i++) { auto x = arr[i]; x = x + 0.5; s += "" + x + " "; }
					return s;
				}
				function from_ints() -> string {
					array<int> arr = [1, 2, 3]; string s = "";
					for (int i = 0; i < 3; i++) { auto x = arr[i]; x = x + 0.5; s += "" + x + " "; }
					return s;
				}
				function sum_var() -> float {
					array<var> arr = [1, 2, 3]; float total = 0.0;
					for (int i = 0; i < 3; i++) { auto x = arr[i]; x += 0.25; total += x; }
					return total;
				}
				from_var() + "|" + from_ints() + "|" + sum_var();
			)", "1.5 2.5 3.5 |1 2 3 |6.75", check_mode::warn);
		});
		test("a_jaibite_keeps_the_auto_spelling_and_types_it_again_at_load", [this]() {
			every_config("auto through jaibite", "string:s|int:1", [](engine& e) {
				auto bite = e.jaibite("var arr = [1]; auto x = arr[0]; x = \"s\"; auto n = 5; n = 1.5; type_of(x) + \":\" + x + \"|\" + type_of(n) + \":\" + n;");
				std::string first = spec::render(e.execute(bite));
				auto loaded = e.jaibite_load_bytes(bite.save_bytes());
				std::string second = spec::render(e.execute(loaded));
				return first == second ? first : first + " vs " + second;
			}, check_mode::warn);
		});
	}

	void known_bugs_simplification_audit() {
		test("f2_a_weak_store_yields_the_stored_weak_handle", [this]() {
			// the vm hands back the right-hand side (the shared handle) of a consumed store into a weak_ptr
			expect("weak store value", R"(
				class Foo { int v = 1; }
				shared_ptr<Foo> sp = Foo();
				weak_ptr<Foo> w = sp;
				auto h = (w = sp);
				type_of(h);
			)", "weak_ptr", check_mode::warn);
		});
		test("f3_a_store_takes_its_right_sides_value_before_the_target", [this]() {
			// Dev ruling 2026-09-27 (C++17): the right operand of every assignment operator, its value included, before the target
			expect("store value timing", R"(
				array<int> a = [0, 0];
				array<int> b = [1, 2];
				function g() -> int { b[1] = 99; return 0; }
				a[g()] = b[1];
				string s = "" + a[0];
				b[1] = 2; a[0] = 0;
				a[g()] += b[1];
				s + " " + a[0];
			)", "2 2", check_mode::warn);
		});
		test("f4_a_container_literal_reads_its_elements_left_to_right", [this]() {
			expect("literal order", R"(
				var A = [1];
				class O { int operator+(int x) { A[0] = 100; return 0; } }
				var o = O();
				function f() { var r = [A[0], o + 1]; return r[0]; }
				"" + f();
			)", "1", check_mode::warn);
		});
		test("f5_a_user_operator_assign_runs_at_every_store_into_a_bare_field", [this]() {
			expect("operator= in a method loop", R"(
				var assigns = 0;
				class V { int v = 0; function operator=(int x) -> V { assigns = assigns + 1; v = x; return this; } }
				class H { V f = V(); void set_many() { for (int i = 0; i < 4; i = i + 1) { f = i; } } }
				var h = H();
				h.set_many();
				"" + assigns + " " + h.f.v;
			)", "4 3", check_mode::warn);
		});
		test("f7_an_argument_ahead_of_a_class_operator_takes_its_value_when_evaluated", [this]() {
			// invariant 13: a later argument that runs user code does not change an earlier by-value argument
			expect("capture ahead of operator", R"(
				var G = [1, 2];
				class O { int operator+(int x) { G.push(9); return 0; } }
				var o = O();
				function sz(var a, int b) { return a.size() + b; }
				"" + sz(G, o + 1);
			)", "2", check_mode::warn);
		});
	}

	// docs/SIMPLIFICATION_AUDIT.md: the audit's divergences, each pinned here by the change that fixes it
	void simplification_audit_fixes() {
		test("f1_a_nested_assignment_yields_the_destinations_stored_value", [this]() {
			// Dev ruling 2026-09-08: the value of an assignment is the destination's stored value, consumed anywhere
			expect("nested stored value", R"(
				class C { int n = 0; static int s = 0; }
				function id(var v) { return v; }
				C c = C(); array<int> a = [0];
				var r1 = 0; r1 = (c.n = 3.9);
				var r2 = 0; r2 = (a[0] = 3.9);
				string r3 = to_string(a[0] = 4.9);
				var r4 = id(C::s = 5.9);
				type_of(r1) + ":" + r1 + " " + type_of(r2) + ":" + r2 + " " + r3 + " " + type_of(r4) + ":" + r4;
			)", "int:3 int:3 4 int:5", check_mode::warn);
		});
		test("f2_a_handle_store_yields_a_value", [this]() {
			expect("handle store value", R"(
				class Foo { int v = 1; }
				shared_ptr<Foo> sp = Foo();
				weak_ptr<Foo> w = sp;
				auto r1 = (w = null);
				auto r2 = (w = sp);
				shared_ptr<Foo> s2 = Foo();
				auto r3 = (s2 = null);
				"" + (r1 == null) + " " + (r2 != null) + " " + (r3 == null);
			)", "true true true", check_mode::warn);
		});
		test("f6_a_coroutine_whose_body_threw_is_completed", [this]() {
			// a native exception escaping the body fails the handle like a script throw; the resumer's state comes back
			expect("coroutine after a throw", R"(
				coroutine g() { throw; yield 1; }
				coroutine k() { throw "boom"; yield 1; }
				coroutine m() { yield 1; throw; yield 2; }
				function t(var h) -> string { string r = ""; try { h.resume(); } catch (e) { r = e; } try { h.resume(); } catch (e) { r = r + "|" + e; } return r; }
				function u(var h) -> string { h.resume(); return t(h); }
				string out = t(g()) + " / " + t(k()) + " / " + u(m());
				try { throw "outer"; } catch (e) { out += " / " + e; }
				out;
			)", "Bare throw with no active exception thrown.|Cannot resume a completed coroutine / boom|Cannot resume a completed coroutine / "
			    "Bare throw with no active exception thrown.|Cannot resume a completed coroutine / outer", check_mode::warn);
		});
		test("f8_a_by_value_parameter_does_not_see_user_code_write_its_argument", [this]() {
			// a by-value parameter holds a copy: user code the callee runs (a method on another local, a getter of the
			// declared class or of a subclass the argument is, an operator, a conversion) never writes through it
			expect("parameter alias proof", R"(
				class Box { int n = 1; }
				Box G = Box();
				class P { void poke() { G.n = 5; } }
				P GQ = P();
				function f(Box p) -> int { var q = GQ; q.poke(); return p.n; }
				function f2(Box p) -> int { P q = GQ; q.poke(); return p.n; }
				class B { int x = 1; int y = 1; int _get_x() { GB.y = 5; return 3; } }
				B GB = B();
				function g(B p) -> int { int a = p.x; return a * 10 + p.y; }
				class C { int x = 1; int y = 1; }
				class D : C { int _get_x() { GC.y = 5; return 3; } }
				C GC = D();
				function h(C p) -> int { int a = p.x; return a * 10 + p.y; }
				class K { int v = 0; int operator+(int k) { G.n = 9; return k; } bool to_bool() { G.n = 8; return true; } }
				K GK = K();
				function i1(Box p) -> int { K k = GK; int t = k + 1; return p.n; }
				function i2(Box p) -> int { K k = GK; if (k) { return p.n; } return 0; }
				string s = "" + f(G);
				G.n = 1; s += " " + f2(G);
				s += " " + g(GB);
				s += " " + h(GC);
				G.n = 1; s += " " + i1(G);
				G.n = 1; s += " " + i2(G);
				s;
			)", "1 1 31 31 1 1", check_mode::warn);
		});
	}

	void known_bugs() {
		known_bugs_simplification_audit();
	}

	// Dev's rulings of 2026-09-28 (docs/spec_conformance.md section 9)
	void rulings_2026_09_28() {
		test("a_bare_throw_with_nothing_active_is_a_catchable_runtime_error", [this]() {
			expect("bare throw", R"(
				string r = "none"; try { throw; } catch (e) { r = "top:" + e; }
				function f() -> string { string s = "none"; try { throw; } catch (e) { s = "own:" + e; } return s; }
				function g() -> int { throw; return 1; }
				function h() -> string { string s = "none"; try { g(); } catch (e) { s = "caller:" + e; } return s; }
				r + "|" + f() + "|" + h();
			)", "top:Bare throw with no active exception thrown.|own:Bare throw with no active exception thrown.|caller:Bare throw with no active exception thrown.", check_mode::warn);
		});
		test("a_native_exception_is_caught_by_the_running_functions_own_try", [this]() {
			// docs/EXCEPTION_DESIGN.md: a store through a property reference whose host setter refuses raises natively inside the
			// statement; the function's own try catches it, else it unwinds to the caller's
			const std::string src = R"(
				function f() -> string { Probe a = Probe(); var& r = a.lv; string s = "none"; try { r = 500; } catch (e) { s = "own:" + e; } return s + ":" + a.lv; }
				Probe b = Probe(); var& rb = b.lv; string top = "none"; try { rb = 700; } catch (e) { top = "top:" + e; }
				function g() -> string { Probe a = Probe(); var& r = a.lv; r = 900; return "unreached"; }
				function h() -> string { string s = "none"; try { g(); } catch (e) { s = "caller:" + e; } return s; }
				f() + "|" + top + "|" + h();
			)";
			every_config("own try", "own:lv too big:1|top:lv too big|caller:lv too big",
				[&](engine& e) { spec::bind_boundary_probe(e); return spec::render(e.execute(src)); }, check_mode::off);
		});
		ruling_switch_equality();
		ruling_membership_equality();
		ruling_compound_operand_text();
		ruling_this_capture();
		ruling_to_string_builtin();
		ruling_host_callbacks();
		ruling_signed_zero_keys();
		ruling_typed_slice();
		ruling_missing_key_compound();
		ruling_constructorless_call_text();
		ruling_typed_reference_parameters();
		static_members_bind_references();
		ruling_reference_store_types();
		ruling_typed_range_for();
		ruling_auto_fields();
		ruling_numeric_ternary_auto();
		ruling_from_json_constructor_errors();
		ruling_json_tagged_values();
		ruling_base_slots_keep_the_class();
		ruling_block_callees();
		ruling_var_typed_arrays();
		constructor_overloads_rank_like_calls();
		compound_operators_have_one_table();
		script_class_operator_overloads();
		builtin_methods_as_values();
	}

	void builtin_methods_as_values() {
		test("a_builtin_method_taken_as_a_value_is_bound_to_its_receiver", [this]() {
			// Dev ruling 2026-09-30 (R-q5): arr.push, m.clear, co.resume, s.size taken as values bind their receiver exactly as a user
			// method taken as a value does: the bound value holds the receiver's object (an element's, a copy's), keeps it alive, and
			// a later reassignment of the variable leaves the bound one on the object it was taken from
			expect("builtin bound", R"(
				class C { int n = 0; void bump() { n += 1; } }
				C c; var b = c.bump; b(); b();
				var a = [1, 2]; var f = a.push; f(3);
				var copy = a; var fc = copy.push; fc(7);
				map<string, int> m = {"x": 1}; var mc = m.clear; mc();
				var inner = [[1], [2]]; var ip = inner[0].push; ip(9);
				C c2; var b2 = c2.bump; c2 = C(); b2();
				var a2 = [1]; var p = a2.push; a2 = [5, 6]; p(0);
				coroutine gen() { yield 1; yield 2; }
				var co = gen(); var r = co.resume; var dn = co.done;
				string s = "abc"; var sz = s.size;
				"" + c.n + " " + a.size() + " " + copy.size() + " " + m.size() + " " + inner[0].size() + " " + c2.n + " " + a2.size() + " " +
				    r() + " " + dn() + " " + sz();
			)", "2 3 4 0 2 0 2 1 false 3");
		});
	}

	void script_class_operator_overloads() {
		test("a_script_class_overloads_every_cpp_overloadable_operator", [this]() {
			// Dev ruling 2026-09-30: unary - + ! ~ and binary % & | ^ << >> <=> join the declarable operators (unary vs binary by
			// the parameter count, like C++ member operators); a compound form is x = x op y through the class's operator; the
			// class chain resolves them; a builtin operand keeps its builtin meaning
			const std::string src = R"(
				class V {
					int n = 0;
					V(int x) { n = x; }
					V operator%(V o) { return V(n % o.n); }
					V operator&(V o) { return V(n & o.n); }
					V operator|(V o) { return V(n | o.n); }
					V operator^(int k) { return V(n ^ k); }
					V operator<<(int k) { return V(n << k); }
					V operator>>(int k) { return V(n >> k); }
					int operator<=>(V o) { return n < o.n ? -1 : n > o.n ? 1 : 0; }
					V operator-() { return V(0 - n); }
					V operator+() { return V(n + 100); }
					bool operator!() { return n == 0; }
					V operator~() { return V(~n); }
					V operator-(V o) { return V(n - o.n); }
				}
				class W : V { W(int x) : super(x) {} }
				class P { int n = 0; P(int x) { n = x; } int operator%(Probe p) { return n % p.v; } }
				V a = V(12); V b = V(5);
				string direct = "" + (a % b).n + " " + (a & b).n + " " + (a | b).n + " " + (a ^ 1).n + " " + (a << 2).n + " " + (a >> 2).n + " " +
				    (a <=> b) + " " + (-a).n + " " + (+a).n + " " + !a + " " + !V(0) + " " + (~a).n + " " + (a - b).n;
				V c = V(12);
				c %= b; string comp = "" + c.n; c &= V(3); comp += " " + c.n; c |= V(4); comp += " " + c.n; c ^= 3; comp += " " + c.n;
				c <<= 1; comp += " " + c.n; c >>= 2; comp += " " + c.n;
				W w = W(12);
				string inherited = "" + (w % b).n + " " + (-w).n + " " + (~w).n + " " + !W(0);
				Probe pr = Probe(); pr.v = 5;
				string mixed = "" + (P(12) % pr);
				string builtin = "" + (7 % 3) + " " + (6 & 3) + " " + (-4) + " " + (+4) + " " + (+2.5) + " " + !0 + " " + (~0);
				direct + "|" + comp + "|" + inherited + "|" + mixed + "|" + builtin;
			)";
			every_config("script operators", "2 4 13 13 48 3 1 -12 112 false true -13 7|2 2 6 5 10 2|2 -12 -13 true|2|1 2 -4 4 2.5 true -1",
				[&](engine& e) { spec::bind_boundary_probe(e); return spec::render(e.execute(src)); }, check_mode::strict);
		});
		test("an_operator_the_class_does_not_declare_keeps_the_builtin_text", [this]() {
			// a unary operator the class lacks (only the binary form declared) raises the builtin operand text; && || stay short-circuit
			expect("missing operators", R"(
				class V { int n = 1; V operator-(V o) { return V(); } }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { V v; var w = -v; return 1; }) + "|" + t([]() { V v; var w = ~v; return 1; }) + "|" + t([]() { V v; var w = v % 2; return 1; });
			)", "Unary minus requires numeric operand|Bitwise NOT requires integer operand|Invalid operands for % operator", check_mode::off);
		});
		test("a_class_operator_bang_decides_every_context_a_condition_reads", [this]() {
			// `!v` runs operator!() as a value and in a condition alike; a value instance's truthiness is the negation of its
			// operator!() (before to_bool()), so if (v), while, ?:, && and a bool slot read one answer; a handle's ! is its null test
			const std::string src = R"(
				class Z { int n = 0; Z(int x) { n = x; } bool operator!() { return n == 0; } }
				class Q { int operator!() { return 0; } }
				class B { bool operator!() { return true; } bool to_bool() { return true; } }
				Z z0 = Z(0); Z z1 = Z(1);
				string r = "";
				if (!z0) { r += "a"; }
				if (!z1) { r += "b"; }
				if (z1) { r += "c"; }
				if (z0) { r += "d"; }
				r += !z0 ? "e" : "f";
				bool b0 = z0; bool b1 = z1;
				r += " " + b0 + " " + b1;
				int k = 0;
				while (!Z(k)) { k += 1; }
				r += " " + k + " " + (z1 && true) + " " + (z0 || false);
				shared_ptr<Z> sp = new Z(0);
				r += " " + !sp;
				Q q;
				r += " " + (!q) + (q ? "T" : "F");
				B bb;
				r += " " + (bb ? "T" : "F") + (!bb ? "T" : "F");
				r;
			)";
			every_config("operator! contexts", "ace false true 1 true false false 0T FT",
				[&](engine& e) { return spec::render(e.execute(src)); }, check_mode::strict);
		});
		test("a_class_with_operator_call_is_callable_wherever_a_function_is_called", [this]() {
			// Dev ruling 2026-09-30: a call whose callee holds a class instance runs its operator() (any arity, the usual
			// overload scorer, the class chain), through a local, a global, a field, an element, a computed callee and a handle
			const std::string src = R"(
				class Adder {
					int base = 0;
					int calls = 0;
					Adder(int b) { base = b; }
					int operator()(int x) { calls += 1; return base + x; }
					int operator()(int x, int y) { calls += 1; return base + x + y; }
					string operator()(string s) { calls += 1; return s + "!"; }
				}
				class Twice : Adder { Twice() : super(1) {} }
				class Holder { Adder f = Adder(100); }
				Adder g = Adder(10);
				function make() -> Adder { return Adder(1000); }
				function run() -> string {
					Adder a = Adder(5);
					Holder h;
					array<Adder> arr = [Adder(20)];
					shared_ptr<Adder> sp = new Adder(7);
					Twice t;
					string r = "" + a(1) + " " + a(1, 2) + " " + a("hi") + " " + a.calls + " " + g(1) + " " + h.f(3) + " " + arr[0](2) + " " +
					    (make())(4) + " " + sp(1) + " " + sp.calls + " " + t(1);
					return r;
				}
				run();
			)";
			every_config("callable objects", "6 8 hi! 3 11 103 22 1004 8 1 2",
				[&](engine& e) { return spec::render(e.execute(src)); }, check_mode::strict);
		});
		test("a_class_without_operator_call_keeps_the_not_callable_text", [this]() {
			// a function slot takes a function or null (the one conversion table): a callable object passes through a lambda
			expect("not callable", R"(
				class Plain { int n = 1; }
				class F { int operator()(int x) { return x * 2; } }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				function apply(function f, int x) -> int { return f(x); }
				t([]() { Plain p; return p(1); }) + "|" + t([]() { F f; return apply([f](int x) { return f(x); }, 3); });
			)", "Cannot call 'p': it is a Plain, not a function|6", check_mode::strict);
			// the table's function row takes a function or null: the strict checker refuses an instance at a function declaration
			every_config("function slot", "rejected", [&](engine& e) {
				try {
					e.execute(R"(
						class F { int operator()(int x) { return x * 2; } }
						F f;
						function g = f;
					)");
					return std::string("loaded");
				} catch (const std::exception& ex) {
					const std::string what = ex.what();
					return what.find("cannot initialize 'g' declared 'function' with 'F'") != std::string::npos ? std::string("rejected") : what;
				}
			}, check_mode::strict);
		});
	}

	void compound_operators_have_one_table() {
		test("a_compound_operator_has_one_table_at_every_destination", [this]() {
			// the simplification audit's F9-F12 (Dev: one semantics per compound operator, one zero-divisor text per operator family):
			// member '/=' divides like the local's, '+=' appends text only to a text target at every destination (a local, a field,
			// this's field, an element, a map entry, a reference), '/' and '/=' raise 'Division by zero', '%' and '%=' 'Modulo by zero'
			expect("one compound table", R"(
				class T { string s = "a"; var n = 7; int big = 9007199254740993; float f = 1.0; void half() { n /= 2; } void addc() { s += 'c'; } }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { T o; o.half(); return type_of(o.n) + ":" + o.n; }) + "|" +
				t([]() { T o; o.n /= 2; return o.n; }) + "|" +
				t([]() { T o; o.big /= 1; return o.big; }) + "|" +
				t([]() { T o; o.s += 'c'; o.addc(); return o.s; }) + "|" +
				t([]() { string gs = "a"; gs += 'c'; return gs; }) + "|" +
				t([]() { map<string, string> m = {"k": "v"}; m["k"] += 'z'; return m["k"]; }) + "|" +
				t([]() { array<string> as = ["x"]; as[0] += 5; return as[0]; }) + "|" +
				t([]() { var vs = "x"; vs += 5; return vs; }) + "|" +
				t([]() { T o; o.s += 5; return o.s; }) + "|" +
				t([]() { var a = [5]; a[0] += "s"; return a[0]; }) + "|" +
				t([]() { array<string> ss = ["a"]; auto& e1 = ss[0]; e1 += 5; return e1; }) + "|" +
				t([]() { int x = 5; x %= 0; return x; }) + "|" +
				t([]() { int x = 5; int z = 0; return x % z; }) + "|" +
				t([]() { int x = 5; x /= 0; return x; }) + "|" +
				t([]() { int x = 5; int z = 0; return x / z; }) + "|" +
				t([]() { float x = 5.0; x %= 0.0; return x; }) + "|" +
				t([]() { float x = 5.0; float z = 0.0; return x / z; }) + "|" +
				t([]() { T o; o.f /= 0.0; return o.f; }) + "|" +
				t([]() { array<int> a = [4]; a[0] %= 0; return 1; }) + "|" +
				t([]() { array<int> ai = [3]; auto& e0 = ai[0]; e0 %= 0; return 1; });
			)", "int:3|3|9007199254740993|acc|ac|vz|"
			    "Cannot apply '+=' to string and int|Cannot apply '+=' to string and int|Cannot apply '+=' to string and int|"
			    "Cannot apply '+=' to int and string|Cannot apply '+=' to string and int|"
			    "Modulo by zero|Modulo by zero|Division by zero|Division by zero|Modulo by zero|Division by zero|Division by zero|"
			    "Modulo by zero|Modulo by zero", check_mode::warn);
		});
	}

	void constructor_overloads_rank_like_calls() {
		test("a_constructor_overload_set_ranks_like_a_call", [this]() {
			// the one script-overload scorer picks a constructor (a call, : this(...) and : super(...)): typed exact > conversion > var;
			// a typed parameter the argument does not match ranks below every candidate it matches
			expect("constructor ranking", R"(
				class R { int got = 0; R(var& a, string tag) { got = -1; } R(var a, int tag) { got = a[0]; } }
				class F { string which = ""; F(var x) { which = "var"; } F(float x) { which = "float"; } }
				class D { string which = ""; D() : this(1) {} D(var x) { which = "var"; } D(int x) { which = "int"; } }
				class B { string which = ""; B(var x) { which = "var"; } B(int x) { which = "int"; } }
				class C : B { C() : super(5) {} }
				var a = [7];
				R r = R(a, 0); F f = F(2); D d = D(); C c = C(); B v = B("s");
				"" + r.got + " " + f.which + " " + d.which + " " + c.which + " " + v.which;
			)", "7 float int int var", check_mode::warn);
		});
	}

	void ruling_var_typed_arrays() {
		test("a_var_holding_a_copy_of_a_typed_array_keeps_the_typed_representation", [this]() {
			// the array keeps its element type (every element boundary), the variable stays a var ('=' takes any value)
			expect("var typed array", R"(
				array<int> a = [1];
				var G = a;
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { var v = a; v.push("s"); return v.size(); }) + "|" +
				t([]() { var v = a; v.push(2); v[0] = 2.9; return "" + v.size() + " " + a.size() + " " + type_of(v[0]) + ":" + v[0]; }) + "|" +
				t([]() { var v = a; v = "hello"; v = 5; return type_of(v) + ":" + v; }) + "|" +
				t([]() { var v = 1; v = a; v.push("s"); return 1; }) + "|" +
				t([]() { G.push("s"); return 1; }) + "|" +
				t([]() { G = "g"; G = a; array<int> c = G; c.push(3); return "" + c.size() + " " + G.size(); }) + "|" +
				t([]() { var e = []; e.push("x"); e.push(1); return e.size(); }) + "|" +
				t([]() { var v = a; var& r = v; r = "via ref"; return r; });
			)", "Cannot push 'string' to array<int>|2 1 int:2|int:5|Cannot push 'string' to array<int>|Cannot push 'string' to array<int>|2 1|2|via ref",
			   check_mode::warn);
		});
	}

	void ruling_block_callees() {
		test("a_callee_of_a_top_level_block_sees_the_outer_binding", [this]() {
			// docs/global_shadowing.md: a function called from inside a block sees the binding of the scope it was declared in
			// (a {} block, an if body, a for body, a range-for body, a catch body, a nested block); a function declared inside
			// the block keeps the block's names
			expect("top block", R"(
				var G = 1;
				function g() { return G; }
				var rb = 0; var ri = 0; var rf = 0; var rr = 0; var rc = 0; var rn = 0; var rd = 0;
				{ var G = 5; rb = g(); }
				if (true) { var G = 9; ri = g(); }
				for (int i = 0; i < 1; i++) { var G = 11; rf = g(); }
				for (auto v : [1]) { var G = 12; rr = g(); }
				try { throw 1; } catch (e) { var G = 13; rc = g(); }
				{ var G = 14; { var G = 15; rn = g(); } }
				{ var H = 3; function h() { return H; } rd = h(); }
				"" + rb + ri + rf + rr + rc + rn + " " + rd + " " + G;
			)", "111111 3 1");
		});
	}

	void ruling_base_slots_keep_the_class() {
		test("a_derived_value_in_a_base_typed_slot_keeps_its_class", [this]() {
			// a deliberate deviation from C++ slicing: the slot's static type is the base, the value stays whole
			expect("no slicing", R"(
				class B { int a = 1; } class D : B { int d = 2; }
				B b = D(); B c = b; array<B> xs = [D()];
				function by_value(B p) -> string { return type_of(p); }
				type_of(b) + " " + type_of(c) + " " + type_of(cast<B>(D())) + " " + cast<D&>(b).d + " " + type_of(xs[0]) + " " + by_value(D());
			)", "D D D 2 D D", check_mode::warn);
		});
	}

	void ruling_json_tagged_values() {
		test("a_json_round_trip_keeps_chars_and_non_string_map_keys", [this]() {
			// {"$char": n} and {"$map": [[k, v], ...]} carry what a JSON object cannot; any other shape of those keys stays a map
			expect("json tags", R"(
				class Inv { char grade = 'a'; map<int, string> slots; map<char, int> counts; map<int, string> none; var loose = {1: "x", "k": 2}; }
				Inv a; a.grade = 'z'; a.slots[3] = "sword"; a.slots[10] = "bow"; a.counts['q'] = 2;
				var b = from_json(to_json(a));
				to_json('z') + "|" + to_json({1: "a", 2: "b"}) + "|" + to_json({"s": 'c'}) + "|" +
				type_of(b.grade) + ":" + b.grade + "|" + b.slots[3] + "," + b.slots[10] + "|" + b.counts['q'] + "|" + b.none.size() + "|" +
				b.loose[1] + b.loose["k"] + "|" + type_of(from_json("{\"$char\": 300}")) + "|" + type_of(from_json("{\"$map\": [1]}")) + "|" +
				type_of(from_json("{\"$map\": [[1, 2]], \"x\": 1}"));
			)", "{\"$char\":122}|{\"$map\":[[1,\"a\"],[2,\"b\"]]}|{\"s\":{\"$char\":99}}|char:z|sword,bow|2|0|x2|map|map|map", check_mode::warn);
		});
	}

	void ruling_auto_fields() {
		test("an_auto_field_takes_its_initializers_static_type", [this]() {
			// ruling 4 (auto = static type) for class fields: an int-initialized auto field is an int field, an auto without a static
			// type is a var
			expect("auto fields", R"(
				var FLAG = true;
				class C { auto n = 5; auto v = FLAG ? 1 : "s"; auto f = FLAG ? 1 : 2.5; }
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { C c; c.n = 2.9; return type_of(c.n) + ":" + c.n; }) + "|" +
				t([]() { C c; var s = "s"; c.n = s; return c.n; }) + "|" +
				t([]() { C c; c.v = 2.5; c.v = "t"; return type_of(c.v) + ":" + c.v; }) + "|" +
				t([]() { C c; return type_of(c.f) + ":" + c.f; });
			)", "int:2|Cannot assign string to field 'n' of C (declared int)|string:t|float:1.0", check_mode::warn);
		});
	}

	void ruling_numeric_ternary_auto() {
		test("an_auto_initialized_from_an_int_float_ternary_is_float", [this]() {
			// C++'s common type of int and float is float: the chosen arm converts; any other mixed ternary has no static type
			expect("numeric ternary", R"(
				var FLAG = true;
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { auto x = FLAG ? 1 : 2.5; return type_of(x) + ":" + x; }) + "|" +
				t([]() { auto x = FLAG ? 2.5 : 1; x = 3; return type_of(x) + ":" + x; }) + "|" +
				t([]() { auto x = FLAG ? 1 : 2.5; var s = "s"; x = s; return x; }) + "|" +
				t([]() { auto y = FLAG ? 1 : "s"; y = 2.5; return type_of(y); });
			)", "float:1.0|float:3.0|Cannot assign string to 'x' (declared float)|float", check_mode::warn);
			every_config("no warning for the numeric pair", "0|1", [](engine& e) {
				auto numeric = e.check("var F = true; auto x = F ? 1 : 2.5;");
				auto mixed = e.check("var F = true; auto y = F ? 1 : \"s\";");
				auto ternary = [](const auto& r) { size_t n = 0; for (const auto& d : r.diagnostics) { if (d.message.find("ternary") != std::string::npos) { ++n; } } return std::to_string(n); };
				return ternary(numeric) + "|" + ternary(mixed);
			});
		});
	}

	void ruling_from_json_constructor_errors() {
		test("from_json_propagates_a_raising_constructor", [this]() {
			// the load constructs the class as Name() would; that call's throw or error is the load's
			expect("from_json ctor", R"(
				class R { int v = 0; R() { throw "no R"; } }
				class Ok { int v = 0; }
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { var x = from_json("{\"_type_\": \"R\", \"v\": 3}"); return "loaded"; }) + "|" +
				t([]() { var x = from_json("{\"_type_\": \"Ok\", \"v\": 3}"); return type_of(x) + ":" + x.v; });
			)", "no R|Ok:3", check_mode::warn);
		});
	}

	void ruling_typed_range_for() {
		test("a_typed_range_for_variable_takes_each_element_as_its_declaration_would", [this]() {
			// by value: T x = element; by reference: an element that holds a T; auto over elements without a static type is a var
			expect("typed range-for", R"(
				class A { int v = 1; } class B : A { }
				function t(function f) -> string { string r = ""; try { r = "" + f(); } catch (e) { r = e; } return r; }
				t([]() { var a = [1.5, 2.5]; string s = ""; for (int v : a) { s += type_of(v) + ":" + v + " "; } return s; }) + "|" +
				t([]() { var a = [1, 2]; string s = ""; for (string v : a) { s += type_of(v) + ":" + v + " "; } return s; }) + "|" +
				t([]() { var a = ["x"]; string s = ""; for (int v : a) { s += type_of(v); } return s; }) + "|" +
				t([]() { var a = [1, 2.5]; int n = 0; for (int& v : a) { v = v + 1; n += 1; } return n; }) + "|" +
				t([]() { var a = [B(), A()]; int n = 0; for (A& r : a) { r.v = 7; n += r.v; } return n; }) + "|" +
				t([]() { var a = [1, 2]; string s = ""; for (auto x : a) { x = "s"; s += x; } return s; }) + "|" +
				t([]() { array<int> b = [1]; string s = ""; for (auto x : b) { x = 2.9; s += type_of(x) + ":" + x; } return s; });
			)", "int:1 int:2 |string:1 string:2 |Cannot initialize 'v' (declared int) with string|Cannot initialize 'v' (declared int) with float|"
			    "14|ss|int:2", check_mode::warn);
			every_config("checker", "1|0", [](engine& e) {
				auto bad = e.check("array<float> a = [1.5]; for (int& v : a) { }");
				auto ok = e.check("array<int> a = [1]; for (int& v : a) { } array<float> f = [1.5]; for (int v : f) { }");
				return std::to_string(bad.error_count()) + "|" + std::to_string(ok.error_count());
			});
		});
	}

	void ruling_reference_store_types() {
		test("a_store_through_a_reference_obeys_the_variables_declared_type", [this]() {
			expect("ref store", R"(
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = e; } return r; }
				function set37(int& a) { a = 3.7; }
				t([]() { int x = 1; var& r = x; var s = "s"; r = s; return type_of(x); }) + "|" +
				t([]() { int x = 1; var& r = x; r = 2.9; return type_of(x) + ":" + x; }) + "|" +
				t([]() { string x = "a"; var& r = x; r = 5; return type_of(x) + ":" + x; }) + "|" +
				t([]() { var x = 1; var& r = x; r = "free"; return type_of(x); }) + "|" +
				t([]() { int u = 1; set37(u); return type_of(u) + ":" + u; });
			)", "Cannot assign string to a variable declared int|int:2|string:5|string|int:3", check_mode::warn);
		});
		test("a_store_through_a_reference_copies_like_the_variables_own_store", [this]() {
			// value semantics hold through the reference: the typed variable takes a copy, never the source's node
			expect("ref store copies", R"(
				array<int> src = [1, 2]; array<int> dst = [9, 9];
				function fill(var& r) { r = src; r[0] = 5; }
				fill(dst);
				"" + src[0] + " " + dst[0] + " " + dst.size();
			)", "1 5 2", check_mode::warn);
		});
	}

	void static_members_bind_references() {
		test("a_static_member_and_a_namespace_variable_bind_a_reference_parameter", [this]() {
			// C++: S::s and ns::x are lvalues; a reference parameter or a reference declaration binds their storage
			expect("static ref", R"(
				class S { static int s = 1; static string name = "a"; }
				namespace ns { int x = 1; }
				function bump(int& r) { r = r + 10; }
				function rename(string& r) { r += "b"; }
				function run() -> string { bump(S::s); bump(ns::x); rename(S::name); bump(S::s); return "" + S::s + " " + ns::x + " " + S::name; }
				string first = run();
				S::s = 5; ns::x = 6; bump(S::s); bump(ns::x);
				string second = "" + S::s + " " + ns::x;
				function decls() -> string { var& a = S::s; int& b = ns::x; a = 40; b = 41; return "" + S::s + " " + ns::x; }
				first + "|" + second + "|" + decls();
			)", "21 11 ab|15 16|40 41", check_mode::warn);
		});
	}

	void ruling_typed_reference_parameters() {
		test("a_typed_reference_parameter_binds_only_its_own_type", [this]() {
			expect("int& binding", R"(
				class A { int v = 1; } class B : A { }
				function set(int& a) { a = 7; }
				function seta(A& r) { r.v = 9; }
				function any(var& r) { r = "s"; }
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = e; } return r; }
				var vi = 1; var vf = 1.5;
				t([]() { set(vf); return vf; }) + "|" + t([]() { set(vi); return vi; }) + "|" + t([]() { B b = B(); seta(b); return b.v; }) + "|" +
				t([]() { var x = 5; any(x); return x; }) + "|" + t([]() { var s = "x"; seta(s); return 1; });
			)", "Cannot pass float to parameter 'a' of 'set' (declared int)|7|9|s|Cannot pass string to parameter 'r' of 'seta' (declared A)", check_mode::warn);
			every_config("checker", "1|0", [](engine& e) {
				auto bad = e.check("function set(int& a) { a = 7; } float u = 1.5; set(u);");
				auto ok = e.check("function set(int& a) { a = 7; } int n = 1; set(n); var v = 2.5; set(v);");
				return std::to_string(bad.error_count()) + "|" + std::to_string(ok.error_count());
			});
		});
	}

	void ruling_constructorless_call_text() {
		test("a_constructorless_class_called_with_arguments_has_one_text", [this]() {
			expect("runtime text", R"(
				class Bare { int v = 1; }
				function t(function f) -> string { string r = "none"; try { f(); } catch (e) { r = e; } return r; }
				var one = 1; var two = "s";
				t([]() { var b = Bare(one, two); });
			)", "No constructor of 'Bare' takes (int, string)", check_mode::warn);
			every_config("checker text", "No constructor of 'Bare' takes (int, string)", [](engine& e) {
				auto r = e.check("class Bare { int v = 1; } var b = Bare(1, \"s\");");
				for (const auto& d : r.diagnostics) { if (d.message.find("constructor") != std::string::npos) { return d.message; } }
				return std::string("no diagnostic");
			});
		});
	}

	void ruling_typed_slice() {
		test("a_slice_of_a_typed_array_keeps_its_element_type", [this]() {
			expect("typed slice", R"(
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = "raised:" + e; } return r; }
				array<int> a = [1, 2, 3];
				array<string> s = ["x", "y"];
				var v = [1, "two", 3.0];
				t([]() { a.slice(0, 2).push("x"); return 1; }) + "|" + t([]() { array<int> sl = a.slice(1, 3); return sl.size(); }) + "|" +
				t([]() { s.slice(0, 1).push(5); return 1; }) + "|" + t([]() { var sl = v.slice(0, 2); sl.push("ok"); return sl.size(); });
			)", "raised:Cannot push 'string' to array<int>|2|raised:Cannot push 'int' to array<string>|3", check_mode::warn);
		});
	}

	void ruling_missing_key_compound() {
		test("a_compound_on_a_missing_key_starts_from_the_value_types_default", [this]() {
			// C++ operator[] value-initializes the entry; a var-valued map has no default, so its entry meets null
			expect("missing key compound", R"(
				function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = "raised:" + e; } return r; }
				map<string, int> mi = {}; mi["k"] += 5; mi["j"]++; mi["q"] -= 2;
				map<string, float> mf = {}; mf["k"] += 1.5;
				map<string, string> ms = {}; ms["k"] += "s";
				map<int, int> counts = {}; for (int x : [3, 1, 3, 3]) { counts[x] += 1; }
				"" + mi["k"] + mi["j"] + mi["q"] + mi.size() + "|" + mf["k"] + "|" + ms["k"] + "|" + counts[3] + counts[1] + counts.size() + "|" +
				t([]() { var m = {}; m["k"] += 1; return m.size(); }) + "|" + t([]() { map<string, int> m = {}; m[5] += 1; return m.size(); });
			)", "51-23|1.5|s|312|raised:Cannot apply '+=' to null and int|raised:Cannot use 'int' as a key of type 'string'", check_mode::warn);
		});
	}

	void ruling_signed_zero_keys() {
		test("zero_and_negative_zero_are_one_map_key", [this]() {
			expect("signed zero", R"(
				var m = {}; m[0.0] = 1; m[-0.0] = 2;
				map<float, int> t = {}; t[-0.0] = 5; t[0.0] += 1;
				float nan = INF - INF; var n = {}; n[nan] = 1; n[nan] = 2;
				var pz = {}; pz[0.0] = 1; var nz = {}; nz[-0.0] = 1;
				"" + m.size() + m[0.0] + m[-0.0] + "|" + t.size() + t[0.0] + "|" + n.size() + "|" + m.has(-0.0) + (pz == nz);
			)", "122|16|1|truetrue", check_mode::warn);
		});
	}

	void ruling_to_string_builtin() {
		test("the_to_string_builtin_uses_a_classs_own_to_string", [this]() {
			expect("to_string(x)", R"(
				class T { int n = 3; string to_string() { return "t" + n; } }
				class U { int n = 1; }
				class Bad { string to_string() { int z = 0; return "" + (1 / z); } }
				class Thrower { string to_string() { throw "nope"; return ""; } }
				function t(function f) -> string { string r = "none"; try { r = f(); } catch (e) { r = "raised:" + e; } return r; }
				to_string(T()) + "|" + (to_string(T()) == ("" + T()) ? "same" : "differs") + "|" + t([]() { return to_string(Bad()); }) + "|" +
				t([]() { return to_string(Thrower()); }) + "|" + to_string(5) + to_string("s");
			)", "t3|same|raised:Division by zero|raised:nope|5s", check_mode::warn);
		});
	}

	void ruling_host_callbacks() {
		test("a_script_callback_behind_a_host_function_propagates_its_error", [this]() {
			every_config("host callback", "raised:Division by zero|raised:boom|7", [](engine& e) {
				e.add_function("apply", [](script_value cb) -> script_int { return convert_script_function<script_int(script_int)>(cb)(3); });
				return spec::render(e.execute(R"(
					function t(function f) -> string { string r = "none"; try { r = "" + f(); } catch (e) { r = "raised:" + e; } return r; }
					t([]() { return apply([](int x) { int z = 0; return x / z; }); }) + "|" +
					t([]() { return apply([](int x) { throw "boom"; return x; }); }) + "|" + apply([](int x) { return x + 4; });
				)"));
			}, check_mode::warn);
		});
		test("a_host_calling_a_script_callback_sees_the_error_as_an_exception", [this]() {
			every_config("host sees", "threw:Division by zero", [](engine& e) {
				e.execute("var cb = [](int x) { int z = 0; return x / z; };");
				auto f = convert_script_function<int(int)>(e.get_variable("cb"));
				try { (void)f(3); return std::string("no throw"); }
				catch (const std::exception& ex) { return std::string("threw:") + ex.what(); }
			}, check_mode::warn);
		});
	}

	void ruling_this_capture() {
		test("a_this_capture_is_the_instance_itself", [this]() {
			expect("[this]", R"(
				class C {
					int v = 1;
					int by_this() { auto l = [this]() { this.v = 8; }; l(); return v; }
					int bare() { auto l = [this]() { v = 9; }; l(); return v; }
					int later() { auto l = [this]() { return v; }; v = 4; return l(); }
					function keep() { return [this]() { v = v + 1; return v; }; }
				}
				C c = C();
				var inc = c.keep(); inc(); inc();
				"" + C().by_this() + C().bare() + C().later() + c.v;
			)", "8943", check_mode::warn);
		});
	}

	void ruling_compound_operand_text() {
		test("a_compound_operand_error_has_one_text_at_every_destination", [this]() {
			// docs/diagnostics.md compound assignment: Cannot apply 'op=' to <target> and <operand>, a variable, a field, a static,
			// an element and a map entry alike
			expect("compound text", R"(
				class F { int n = 5; static int s = 5; }
				function t(function f) -> string { string r = "none"; try { f(); } catch (e) { r = e; } return r; }
				var s = "s";
				t([]() { int x = 5; x -= s; }) + "|" + t([]() { F f = F(); f.n -= s; }) + "|" + t([]() { F::s *= s; }) + "|" +
				t([]() { array<int> a = [5]; a[0] -= s; }) + "|" + t([]() { var a = [5]; a[0] %= s; }) + "|" +
				t([]() { map<string, int> m = {"k": 1}; m["k"] /= s; }) + "|" + t([]() { var m = {"k": 1.5}; m["k"] -= s; });
			)", "Cannot apply '-=' to int and string|Cannot apply '-=' to int and string|Cannot apply '*=' to int and string|"
			    "Cannot apply '-=' to int and string|Cannot apply '%=' to int and string|Cannot apply '/=' to int and string|"
			    "Cannot apply '-=' to float and string", check_mode::warn);
		});
	}

	void ruling_membership_equality() {
		test("contains_index_of_and_has_use_the_languages_equality", [this]() {
			expect("membership ==", R"(
				class E { int v = 0; E(int x) { v = x; } bool operator==(E o) { return v == o.v; } }
				class P { int a = 1; int b = 2; }
				class B { bool operator==(B o) { throw "cmp"; return true; } }
				var es = [E(1), E(2)];
				var ps = [P()];
				var nums = [1, 2.5, "x", [1, 2]];
				array<int> ints = [3, 4];
				string r = "" + es.contains(E(2)) + es.index_of(E(2)) + es.has(E(3)) + "|" + ps.contains(P()) + "|" +
				           nums.index_of(2.5) + nums.contains(1.0) + nums.index_of([1, 2]) + ints.index_of(4.0) + ints.contains(5);
				string t = "none"; try { [B()].contains(B()); } catch (e) { t = "raised:" + e; }
				r + "|" + t;
			)", "true1false|true|1true31false|raised:cmp", check_mode::warn);
		});
	}

	void ruling_switch_equality() {
		test("a_switch_case_matches_when_the_languages_equality_holds", [this]() {
			expect("switch ==", R"(
				class E { int v = 0; E(int x) { v = x; } bool operator==(E o) { return v == o.v; } }
				class P { int a = 1; int b = 2; }
				class B { bool operator==(B o) { throw "cmp"; return true; } }
				function pick(var x) -> string { switch (x) { case 1.0: return "one"; case "s": return "s"; default: return "none"; } }
				function inst(var x) -> string { switch (x) { case E(2): return "E2"; default: return "none"; } }
				function memberwise(var x) -> string { switch (x) { case P(): return "P"; default: return "none"; } }
				string r = pick(1) + pick(1.0) + pick("s") + pick(2) + "|" + inst(E(2)) + inst(E(3)) + memberwise(P());
				string t = "none"; try { switch (B()) { case B(): t = "hit"; } } catch (e) { t = "raised:" + e; }
				r + "|" + t;
			)", "oneonesnone|E2noneP|raised:cmp", check_mode::warn);
		});
	}

	// Dev ruling 2026-09-27: a by-value argument takes its value when it is evaluated, left to right
	void call_argument_capture() {
		test("by_value_arguments_capture_their_values_left_to_right", [this]() {
			expect("call args", R"(
				var GA = [1, 2];
				var M = {"k": 2};
				var GG = [[2]];
				function f() { GA[1] = 100; return 1; }
				function fm() { M["k"] = 100; return 1; }
				function fg() { GG[0][0] = 100; return 1; }
				function add2(a, b) { return a + b; }
				function add2t(int a, int b) -> int { return a + b; }
				class C { int m(int a, int b) { return a + b; } }
				class P { int a = 0; int b = 0; P(int x, int y) { a = x; b = y; } }
				function run() -> string {
					string s = "";
					GA[1] = 2; s += "" + add2(GA[1], f()) + ",";
					GA[1] = 2; s += "" + add2t(GA[1], f()) + ",";
					GA[1] = 2; C c = C(); s += "" + c.m(GA[1], f()) + ",";
					int t = 0; for (int i = 0; i < 3; i++) { GA[1] = 2; t += add2t(GA[1], f()); } s += "" + t + ",";
					GA[1] = 2; P p = P(GA[1], f()); s += "" + (p.a + p.b) + ",";
					s += "" + add2(M["k"], fm()) + "," + add2(GG[0][0], fg());
					return s;
				}
				run();
			)", "3,3,3,9,3,3,3");
		});
	}
};

} // namespace jai::foundry::tests

using spec_conformance_tests = jai::foundry::tests::spec_conformance_tests;
FOUNDRY_REGISTER(spec_conformance_tests)
