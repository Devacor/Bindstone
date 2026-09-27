// Spec conformance: every expected value below is derived from the specification (Source/JaiScript/CLAUDE.md
// "Language features", docs/*.md, the guide, and C++ where the docs say C++ semantics), never from running
// a backend. docs/spec_conformance.md is the ledger: each claim, its source, the tests that pin it.
// Every row runs on the interpreter and the VM, with proofs on and off and the VM's passes off (and, in a
// build with the miss-injection lever, with injected cell misses), against one expected string.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
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
#ifdef JAISCRIPT_SPEC_PENDING_CALL_ARGS
		// Dev ruling (being implemented): by-value call arguments capture their value at the call site, left to right
		pending_call_argument_capture();
#endif
#ifdef JAISCRIPT_SPEC_PENDING_RULINGS
		// open questions for Dev (docs/spec_conformance.md): the expected values follow the likeliest reading and wait for the ruling
		pending_rulings();
#endif
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
							try { throw 7; } catch (e) { o += to_string(e + 1); }
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
				coroutine int g() { try { throw 3; } catch (e) { yield e; var f = [=]() { return e * 2; }; yield f(); yield e + 1; } }
				function mk() { var f = null; try { throw 5; } catch (e) { f = [&]() { return e * 2; }; } return f; }
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
		test("a_qualified_namespace_variable_is_not_an_lvalue_to_a_reference", [this]() {
			expect("ns ref", R"(
				namespace n5 { var x = 1; }
				function take(var& r) { r = 9; }
				function run() -> string {
					string s = "";
					try { take(n5::x); s += "bound"; } catch (e) { s += e; }
					return s + "|" + n5::x;
				}
				run();
			)", "Cannot pass non-lvalue to reference parameter|1");
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
			// whether a script catch sees this text is an open question (docs/spec_conformance.md); the host always does
			every_config("rethrow nothing", "No exception to re-throw", [](engine& e) {
				try { e.execute("function f() { throw; } f();"); } catch (const std::exception& ex) { return std::string(ex.what()).substr(0, 24); }
				return std::string("no error");
			});
		});
		test("a_finished_coroutine_restores_the_resumers_exception", [this]() {
			expect("resumer trio", R"(
				coroutine int g() { yield 1; }
				var r = "";
				try { throw "outer"; } catch (e) { var h = g(); h.resume(); try { throw; } catch (x) { r = x; } }
				r;
			)", "outer");
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

	void pending_rulings() {
		test("bitwise_compound_assignments_on_every_lvalue", [this]() {
			// CLAUDE.md lists "& | ^ << >> ... and compound assigns"; the bitwise compound forms do not parse today
			expect("bitwise compound", R"(
				class C { int f = 5; }
				function run() -> string {
					int x = 6; x &= 3; x |= 8; x ^= 1; x <<= 2; x >>= 1;
					int y = 1; string err = "";
					try { y <<= 64; } catch (e) { err = e; }
					array<int> a = [12]; a[0] &= 10;
					C c = C(); c.f ^= 3;
					return "" + x + "|" + err + "|" + y + "|" + a[0] + "|" + c.f;
				}
				run();
			)", "22|Shift amount must be between 0 and 63|1|8|6");
		});
	}

	void known_bugs() {
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
		test("rethrow_after_a_nested_try_inside_a_catch_rethrows_the_outer_exception", [this]() {
			// docs/EXCEPTION_DESIGN.md addendum: a nested try inside a catch keeps the caught exception live for throw; (C++)
			expect("rethrow", R"(
				function run() -> string { string r = ""; try { try { throw "x"; } catch (e) { try { throw "y"; } catch (e2) { r += e2; } throw; } } catch (z) { r += z; } return r; }
				run();
			)", "yx");
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
			)", "No exception to re-throw|outer");
		});
		test("a_callee_of_a_top_level_block_sees_the_outer_binding", [this]() {
			// docs/global_shadowing.md: a function called from inside the block still sees the outer binding
			expect("top block", R"(
				var G = 1;
				function g() { return G; }
				var rb = 0; var ri = 0; var rf = 0;
				{ var G = 5; rb = g(); }
				if (true) { var G = 9; ri = g(); }
				for (int i = 0; i < 1; i++) { var G = 11; rf = g(); }
				"" + rb + ri + rf + G;
			)", "1111");
		});
	}

	// Dev ruling (being implemented by another change): a by-value argument is captured at the call site, left to right
	void pending_call_argument_capture() {
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
