// Regressions surfaced by the demoreel dogfooding project (examples/demoreel, README
// findings). Each test runs on BOTH backends and asserts the intended behavior; the
// finding numbers refer to the demoreel README's "Bugs worked around" list.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

std::shared_ptr<jai::engine> demoreel_engine(bool use_vm) {
	auto e = jai::engine::make();
	if (use_vm) { e->set_backend(jai::backend_type::vm); }
	jai::stdlib::register_all(e);
	return e;
}

std::string backend_tag(bool use_vm) { return use_vm ? "[vm] " : "[interp] "; }

} // namespace

class demoreel_regression_tests : public suite {
public:
	demoreel_regression_tests() : suite("Demoreel Regressions") {}

	void forge_tests() override {

		// Finding 1: calling a FREE function (or lambda) inside a class method must not
		// poison the method's implicit-self scope for the rest of the method.
		test("free_call_in_method_keeps_implicit_self", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					function mk() -> var { return [1]; }
					class R { var g = null; int ok = 0; R() { g = mk(); ok = g.size(); } }
					var r = R();
					r.ok
				)");
				check_eq((int64_t)1, r.as_int(), backend_tag(use_vm) + "field access after free call in ctor");
			}
		});

		test("free_call_in_method_keeps_field_reads", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					int helper() { return 1; }
					class P { int x = 5; int m() { helper(); return x; } }
					var p = P();
					p.m();
				)");
				check_eq((int64_t)5, r.as_int(), backend_tag(use_vm) + "unqualified field read after free call");
			}
		});

		test("free_call_in_method_keeps_this", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					int helper() { return 1; }
					class Q { int x = 5; auto m() { helper(); return this.x; } }
					var q = Q();
					q.m();
				)");
				check_eq((int64_t)5, r.as_int(), backend_tag(use_vm) + "this.x after free call");
			}
		});

		test("lambda_call_in_method_keeps_implicit_self", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class L {
						int a = 7;
						int m() {
							var f = []() { return 3; };
							int b = f();
							return a + b;
						}
					}
					var l = L();
					l.m();
				)");
				check_eq((int64_t)10, r.as_int(), backend_tag(use_vm) + "field read after lambda call in method");
			}
		});

		// Finding 2 (VM): a loop-body local assigned from a nested scope must not corrupt
		// the enclosing loop variable.
		test("vm_nested_for_assign_does_not_corrupt_outer_loop", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class G { int t(int w, int h) {
						int rows = 0;
						for (int y = 0; y < h; ++y) {
							int prev = -1;
							for (int x = 0; x < w; ++x) { prev = x; }
							rows = rows + 1;
						}
						return rows;
					} }
					var g = G();
					g.t(3, 5)
				)");
				check_eq((int64_t)5, r.as_int(), backend_tag(use_vm) + "outer loop count with inner-for assigning body local");
			}
		});

		test("vm_if_assign_does_not_corrupt_outer_loop", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class G2 { int t(int h) {
						int rows = 0;
						for (int y = 0; y < h; ++y) {
							int clamp = 0;
							if (y > 1) { clamp = y; }
							rows = rows + 1;
						}
						return rows;
					} }
					var g = G2();
					g.t(6)
				)");
				check_eq((int64_t)6, r.as_int(), backend_tag(use_vm) + "outer loop count with plain-if assigning body local");
			}
		});

		test("vm_sibling_scope_locals_do_not_alias", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class G3 { int t(int flag) {
						int acc = 0;
						if (flag == 1) { int v = 10; acc = acc + v; }
						else { int v = 20; acc = acc + v; }
						if (flag == 2) { int v = 100; acc = acc + v; }
						else { int v = 200; acc = acc + v; }
						return acc;
					} }
					var g = G3();
					g.t(1) * 1000 + g.t(0)
				)");
				check_eq((int64_t)210220, r.as_int(), backend_tag(use_vm) + "sibling-scope same-named locals");
			}
		});

		// Finding 2 tail (parser): the flat name->slot map leaked inner-scope names to
		// the enclosing scope — shadowed locals hijacked the outer name after the block
		// closed ("sibling scopes' same-named locals alias" family; also hit by jai_rogue).
		test("inner_scope_locals_do_not_leak_to_outer_reads", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					function shadow1() -> int {
						int v = 1;
						{ int v = 2; }
						return v;
					}
					function shadow2() -> int {
						int v = 1;
						for (int i = 0; i < 3; ++i) { int v = 10 + i; }
						return v;
					}
					function shadow3() -> int {
						int v = 1;
						for (auto v : [7, 8, 9]) { }
						return v;
					}
					shadow1() * 10000 + shadow2() * 100 + shadow3()
				)");
				check_eq((int64_t)10101, r.as_int(), backend_tag(use_vm) + "block/for-body/range-for shadowing restores the outer local");
			}
		});

		// Finding 3 (interpreter): ++x on an enclosing-scope local inside a for body.
		test("preincrement_outer_local_in_for_body", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					function f() -> int {
						int x = 0;
						for (int i = 0; i < 3; ++i) { ++x; }
						return x;
					}
					f();
				)");
				check_eq((int64_t)3, r.as_int(), backend_tag(use_vm) + "++outer local in for body (function)");
			}
		});

		test("preincrement_outer_local_in_for_body_method", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class C3 { int m() {
						int x = 0;
						for (int i = 0; i < 4; ++i) { ++x; }
						return x;
					} }
					var c = C3();
					c.m();
				)");
				check_eq((int64_t)4, r.as_int(), backend_tag(use_vm) + "++outer local in for body (method)");
			}
		});

		// Finding 4: typed locals and fields convert their initializer/assignment like
		// typed parameters do (float->int truncates toward zero, int->float widens).
		test("typed_local_initializer_converts", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute("int d = 4.7; d");
				check_true(r.is_int(), backend_tag(use_vm) + "int local from float literal is int");
				check_eq((int64_t)4, r.as_int(), backend_tag(use_vm) + "int local from 4.7 truncates");
				auto f = e->execute("float fv = 3; fv");
				check_true(f.is_float(), backend_tag(use_vm) + "float local from int literal is float");
				check_near(3.0, f.as<double>(), 1e-12);
			}
		});

		test("typed_local_assignment_converts", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute("int d = 1; d = 6.9; d");
				check_true(r.is_int(), backend_tag(use_vm) + "int local stays int after float assign");
				check_eq((int64_t)6, r.as_int(), backend_tag(use_vm) + "int local assign from 6.9 truncates");
			}
		});

		// PINNED (pending Dev ruling): field declared types are DISCARDED at runtime — the
		// class stores only initializer ASTs, so a typed field behaves dynamically on
		// assignment (an int field silently holds 4.7). Both backends agree. Enforcing
		// would need per-field declared-type storage in class_definition (+ hot-reload
		// migration); until then this pins the parity of the current behavior.
		test("typed_field_assignment_stays_dynamic_parity", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class F { int a = 0; }
					var f = F();
					f.a = 4.7;
					f.a
				)");
				check_true(r.is_float(), backend_tag(use_vm) + "int field currently holds the raw float (dynamic fields)");
				check_near(4.7, r.as<double>(), 1e-12);
			}
		});

		// Finding 5: raw array-element / nested-field arguments must resolve against
		// typed parameters (converting), same as unpacked locals do.
		test("element_args_resolve_typed_params", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class P5 { int add(int r, int g, int b) { return r + g + b; } }
					var p = P5();
					var c = [1.5, 2.5, 3.5];
					p.add(c[0], c[1], c[2])
				)");
				check_eq((int64_t)6, r.as_int(), backend_tag(use_vm) + "float array elements into int params");
			}
		});

		test("nested_field_arg_resolves_string_param", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class Inner { string s = "hi"; }
					class Holder { var inner = null; Holder() { inner = Inner(); } }
					class Echo { string echo(string v) { return v; } }
					class Outer {
						var h = null;
						var e = null;
						Outer() { h = Holder(); e = Echo(); }
						string m() { return e.echo(h.inner.s); }
					}
					var o = Outer();
					o.m()
				)");
				check_eq(std::string("hi"), r.to_string(), backend_tag(use_vm) + "nested field arg into string param");
			}
		});

		test("element_arg_into_host_function", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				e->add_function("hosted_double", [](jai::script_int a) -> jai::script_int { return a * 2; });
				auto r = e->execute("var c = [2.5]; hosted_double(c[0])");
				check_eq((int64_t)4, r.as_int(), backend_tag(use_vm) + "float element into host int param truncates");
			}
		});

		// Finding 6: var fields stay dynamic — reassigning across script classes is legal
		// on both backends (interpreter used to class-lock).
		test("var_field_retypes_across_script_classes", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class A6 { int tag = 1; }
					class B6 { int tag = 2; }
					class H6 { var cur = null; }
					var h = H6();
					h.cur = A6();
					h.cur = B6();
					h.cur.tag
				)");
				check_eq((int64_t)2, r.as_int(), backend_tag(use_vm) + "var field reassigned across classes");
				auto r2 = e->execute(R"(
					class A6b { int tag = 1; }
					class B6b { int tag = 2; }
					class H6b {
						var cur = null;
						var nxt = null;
						void init() { cur = A6b(); nxt = B6b(); }
						void swap() { cur = nxt; nxt = null; }
					}
					var hb = H6b();
					hb.init();
					hb.swap();
					hb.cur.tag
				)");
				check_eq((int64_t)2, r2.as_int(), backend_tag(use_vm) + "unqualified in-method var field cross-class handover");
			}
		});

		// Finding 7: destroying a script-class instance mid-run (field handover drops the
		// last reference) must not crash. Mirrors the reel's transition handover shape.
		test("instance_destroy_midrun_field_handover", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				auto r = e->execute(R"(
					class Pal7 { var full = []; int add(int v) { full.push(v); return full.size() - 1; } }
					class Scene7 {
						var pal = null;
						var g = null;
						int n = 0;
						void setup(int k) {
							pal = Pal7();
							g = [];
							for (int i = 0; i < 32; ++i) { g.push(i * k); }
							pal.add(k);
							n = k;
						}
						int update() { g[0] = g[0] + 1; return g[0]; }
					}
					class Reel7 {
						var cur = null;
						var nxt = null;
						int idx = 0;
						void start(int k) {
							cur = null;
							cur = Scene7();
							cur.setup(k);
						}
						void trans() {
							nxt = Scene7();
							nxt.setup(idx + 100);
						}
						void complete() {
							cur = null;
							cur = nxt;
							nxt = null;
						}
						int frame() { return cur.update(); }
					}
					var r = Reel7();
					r.start(1);
					int total = 0;
					for (int i = 0; i < 60; ++i) {
						total = total + r.frame();
						if (i % 7 == 3) { r.idx = r.idx + 1; r.trans(); }
						if (i % 7 == 5) { r.complete(); }
					}
					total > 0 ? 1 : 0
				)");
				check_eq((int64_t)1, r.as_int(), backend_tag(use_vm) + "mid-run instance handover survives");
			}
		});

		// Finding 9: a coroutine created inside a host-invoked callable must not poison
		// later host-invoked callables on the same engine.
		test("coroutine_in_host_callable_no_state_leak", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = demoreel_engine(use_vm);
				e->execute(R"(
					coroutine int gen9(int n) {
						for (int i = 0; i < n; ++i) { yield i; }
						return -1;
					}
					function kick9() -> int {
						var co = gen9(3);
						int a = co.resume();
						int b = co.resume();
						return a * 10 + b;
					}
					function later9(int v) -> int { return v * 2; }
				)");
				auto kick = e->get_variable("kick9");
				auto r1 = kick.as_function()(std::vector<jai::script_value>{});
				check_true(r1.has_value(), backend_tag(use_vm) + "kick9 call succeeded");
				check_eq((int64_t)1, r1.value().as_int(), backend_tag(use_vm) + "coroutine yields inside host callable");
				auto later = e->get_variable("later9");
				auto r2 = later.as_function()(std::vector<jai::script_value>{ jai::script_value((int64_t)21, e.get()) });
				check_true(r2.has_value(), backend_tag(use_vm) + "later host callable still succeeds");
				check_true(!r2.value().is_null(), backend_tag(use_vm) + "later host callable result not null");
				check_eq((int64_t)42, r2.value().as_int(), backend_tag(use_vm) + "later host callable returns real value");
			}
		});
	}
};

} // namespace jai::foundry::tests

using demoreel_regression_tests = jai::foundry::tests::demoreel_regression_tests;
FOUNDRY_REGISTER(demoreel_regression_tests)
