// A program-scope variable redeclared by a later execute shadows the earlier declaration the way a
// nested block shadows in C++: code that ran under the earlier unit keeps its binding, code that runs
// under a later unit sees the new one; the same unit (a reload, a re-run hook) writes in place;
// functions and classes replace as before (docs/global_shadowing.md). Every row runs both backends
// with proofs on and off against one expected string.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <functional>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class global_shadowing_tests : public suite {
public:
	global_shadowing_tests() : suite("Global Shadowing") {}

	void every_backend(const std::string& label, const std::string& expected, const std::function<std::string(engine&)>& body) {
		for (bool vm : { false, true }) {
			for (bool proofs : { true, false }) {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				std::string got;
				try { got = body(*e); }
				catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_eq(expected, got, label + (vm ? " vm" : " interp") + (proofs ? "" : " proofs off"));
			}
		}
	}

	void forge_tests() override {
		test("code_compiled_before_a_redeclaration_keeps_its_binding_and_code_after_sees_the_new_one", [this]() {
			// the last reader is the first reader's text executed again: the same unit, resolving as of its current run
			every_backend("before and after", "1 5 5 5|7 5 5|host=9|7 5 5 9", [](engine& e) {
				e.execute("var G = 1; function f() { return G; } function set(v) { G = v; }");
				e.execute("int G = 5; function g() { return G; }");
				e.execute("function h() { return G; }");
				std::string out = e.execute("to_string(f()) + \" \" + to_string(g()) + \" \" + to_string(h()) + \" \" + to_string(G)").as_string();
				e.execute("set(7);");
				out += "|" + e.execute("to_string(f()) + \" \" + to_string(G) + \" \" + to_string(g())").as_string();
				e.execute("var G = 9;");
				auto host = e.get_global_environment()->get("G");
				out += "|host=" + std::to_string(host ? host.value().as_int() : -1);
				out += "|" + e.execute("to_string(f()) + \" \" + to_string(g()) + \" \" + to_string(h()) + \" \" + to_string(G)").as_string();
				return out;
			});
		});
		test("the_same_unit_redeclares_in_place_and_a_forward_reference_keeps_late_binding", [this]() {
			every_backend("same unit", "2 2|3 2 3", [](engine& e) {
				const std::string second = "var G = 2; function g2() { return G; }";
				e.execute("extern var G; function f() { return G; }");
				e.execute(second);
				std::string out = e.execute("to_string(f()) + \" \" + to_string(g2())").as_string();
				e.execute(second);
				e.execute("var G = 3;");
				out += "|" + e.execute("to_string(f()) + \" \" + to_string(g2()) + \" \" + to_string(G)").as_string();
				return out;
			});
		});
		test("compound_stores_and_increments_go_through_the_old_binding", [this]() {
			every_backend("compound", "11 12 5", [](engine& e) {
				e.execute("var G = 1; function bump() { G += 10; return G; } function inc() { ++G; return G; }");
				e.execute("int G = 5;");
				return e.execute("to_string(bump()) + \" \" + to_string(inc()) + \" \" + to_string(G)").as_string();
			});
		});
		test("a_running_loop_keeps_its_cell_when_a_host_callback_redeclares_the_name", [this]() {
			// the reader's run began before the callback declared its G, so the reader's own read is the old cell; a later run sees 100
			every_backend("host shadow", "4 1 3|100 3", [](engine& e) {
				engine* raw = &e;
				e.add_function("host_shadow", [raw]() { raw->execute("var G = 100;"); });
				e.execute("var G = 1;"
				          " function loop(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + G; if (i == 1) { host_shadow(); } } return acc; }"
				          " function sum(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + G; } return acc; }");
				const std::string during = e.execute("to_string(loop(4)) + \" \" + to_string(G) + \" \" + to_string(sum(3))").as_string();
				return during + "|" + e.execute("to_string(G) + \" \" + to_string(sum(3))").as_string();
			});
		});
		test("the_global_qualifier_resolves_the_unit_binding_and_the_host_reads_and_assigns_the_newest", [this]() {
			every_backend("host api", "1|5|1 9", [](engine& e) {
				e.execute("var G = 1; function f() { return ::G; }");
				e.execute("int G = 5;");
				std::string out = e.execute("to_string(f())").as_string();
				auto host = e.get_global_environment()->get("G");
				out += "|" + std::to_string(host ? host.value().as_int() : -1);
				e.add_global("G", script_value(static_cast<int64_t>(9), &e));
				out += "|" + e.execute("to_string(f()) + \" \" + to_string(G)").as_string();
				return out;
			});
		});
		test("classes_lambdas_and_coroutines_resolve_in_the_unit_that_declared_them", [this]() {
			every_backend("declaring unit", "1 1 1 1 5 1 1", [](engine& e) {
				e.execute("var G = 1; class C { var x = G; function get() { return G; } function qualified() { return ::G; } static function stat() { return G; } } var lam = function() { return G; }; coroutine co() { yield G; }");
				e.execute("int G = 5; class D { var x = G; } var c = C(); var d = D(); var h = co();");
				return e.execute("to_string(c.x) + \" \" + to_string(c.get()) + \" \" + to_string(c.qualified()) + \" \" + to_string(C::stat()) + \" \" + to_string(d.x) + \" \" + to_string(lam()) + \" \" + to_string(h.resume())").as_string();
			});
		});
		test("a_reference_bound_before_the_redeclaration_pins_the_old_cell", [this]() {
			every_backend("reference", "7 7 5", [](engine& e) {
				e.execute("var G = 1; function f() { return G; }");
				e.execute("var& R = G;");
				e.execute("int G = 5;");
				return e.execute("R = 7; to_string(f()) + \" \" + to_string(R) + \" \" + to_string(G)").as_string();
			});
		});
		test("an_extern_declaration_shares_the_visible_global_or_declares_it_when_absent", [this]() {
			// Dev (2026-09-12): extern is how a unit shares a global instead of owning one; when no unit has declared the name,
			// the extern brings the shared global into being (unit 0); the first unit that declares the name adopts that cell in
			// place, so the externing units, the declaring unit and the host keep sharing it
			every_backend("extern", "5 5|9 9|7 7 7", [](engine& e) {
				e.execute("extern var G; G = 5; function f() { return G; }");
				std::string out = e.execute("extern var G; to_string(G) + \" \" + to_string(f())").as_string();
				e.execute("var G = 9;");
				out += "|" + e.execute("to_string(f()) + \" \" + to_string(G)").as_string();
				e.execute("extern int H; H = 7;");
				auto host = e.get_global_environment()->get("H");
				out += "|" + e.execute("extern int H; to_string(H)").as_string() + " " + std::to_string(host ? host.value().as_int() : -1);
				out += " " + e.execute("function h() { return H; } h();").to_string();
				return out;
			});
		});
		test("a_reference_argument_resolves_in_the_callers_unit", [this]() {
			// row 123.6: a by-reference argument is the caller's code, so it binds the caller's unit's cell; the call enters the
			// callee's unit ahead of the binding and the binding resolves under the caller's (the sol2 precompiled BST benchmark's
			// abort: a literal and a jaibite of the same text both declaring ref_root)
			every_backend("ref args", "2 1 11 1", [](engine& e) {
				e.execute("class T { int v = 0; T left = null; T right = null; T(int x) { v = x; } }"
				          " function ins(T& n, int v) { if (n == null) { n = T(v); return; } if (v < n.v) { ins(n.left, v); } else { ins(n.right, v); } }"
				          " function cnt(T& n) -> int { if (n == null) { return 0; } return 1 + cnt(n.left) + cnt(n.right); }"
				          " function bump(int& x) { x += 1; }"
				          " var root = null; ins(root, 8); ins(root, 4); var g = 1;"
				          " function a_cnt() { return cnt(root); } function a_g() { return g; }");
				std::string b = e.execute("var root = null; ins(root, 1); var g = 10; bump(g); to_string(cnt(root)) + \" \" + to_string(g);").as_string();
				return e.execute("to_string(a_cnt()) + \" " + b + " \" + to_string(a_g());").as_string();
			});
		});
		test("a_jaibite_of_the_same_text_is_another_unit_for_reference_arguments", [this]() {
			every_backend("jaibite refs", "2 2 2 2", [](engine& e) {
				e.execute("class T { int v = 0; T left = null; T right = null; T(int x) { v = x; } }"
				          " function ins(T& n, int v) { if (n == null) { n = T(v); return; } if (v < n.v) { ins(n.left, v); } else { ins(n.right, v); } }"
				          " function cnt(T& n) -> int { if (n == null) { return 0; } return 1 + cnt(n.left) + cnt(n.right); }");
				const char* text = "var root = null; ins(root, 8); ins(root, 4); cnt(root);";
				const auto lit = e.execute(text).as_int();
				auto bite = e.jaibite(text);
				const auto j1 = e.execute(bite).as_int();
				const auto j2 = e.execute(bite).as_int();
				const auto lit2 = e.execute(text).as_int();
				return std::to_string(lit) + " " + std::to_string(j1) + " " + std::to_string(j2) + " " + std::to_string(lit2);
			});
		});
		test("the_first_shadow_from_a_host_callback_leaves_the_active_frame_on_its_unit", [this]() {
			// row 123.7: the ambient stamp is written only once a name has versions; a frame active when the first shadow appears
			// (a host callback declaring another unit's global) keeps resolving as its own unit's code
			every_backend("first shadow under a frame", "11 11 22", [](engine& e) {
				engine* raw = &e;
				e.add_function("host_declare", [raw]() { raw->execute("var g = 22;"); });
				e.execute("var g = 11; function probe() { var before = g; host_declare(); return to_string(before) + \" \" + to_string(g); }");
				const std::string inside = e.execute("probe();").as_string();
				return inside + " " + e.execute("to_string(g)").as_string();
			});
		});
		test("a_lambda_minted_before_the_first_shadow_keeps_its_functions_unit", [this]() {
			// the unit of a definition is derived from the executing frame, never from an ambient that is not yet maintained
			every_backend("stamp before shadow", "1 1", [](engine& e) {
				e.execute("var g = 1; function mk() { return function() { return g; }; }");
				e.execute("var f = mk();");
				e.execute("var g = 2;");
				return e.execute("to_string(f()) + \" \" + to_string(mk()());").as_string();
			});
		});
		test("a_definition_made_by_a_function_called_from_another_unit_is_the_functions_units", [this]() {
			// a lambda, a nested function and a nested coroutine minted while another unit's top-level code is the caller and no
			// name has versions yet carry the function's unit (derived from its frame), not the caller's run: the caller declared g
			// and would read its own 2, the function's unit never declared it and reads the newest
			every_backend("nested definitions", "3 3 3", [](engine& e) {
				e.execute("[[warn_mode]] function mk() { return function() { return g; }; }"   // g is declared by no unit yet
				          " function mkf() { function inner() { return g; } return inner; }"
				          " function mkc() { coroutine co() { yield g; } return co; }");
				e.execute("var g = 2; var f = mk(); var h = mkf(); var c = mkc();");
				e.execute("var g = 3;");
				return e.execute("to_string(f()) + \" \" + to_string(h()) + \" \" + to_string(c().resume())").as_string();
			});
		});
		test("a_block_is_a_scope_inside_a_unit", [this]() {
			// Dev (2026-09-12): scope is also identified by {}. A block declaration shadows for the block and vanishes after it, at
			// program scope and in a function; a callee still sees the outer binding; a block in another unit shadows only inside
			// the block and is not that unit's declaration of the name
			every_backend("blocks", "2 1 2 1 1 5 3 1 20 1", [](engine& e) {
				e.execute("var G = 1; string out = \"\";"
				          " { var G = 2; out = out + to_string(G); } out = out + \" \" + to_string(G);"
				          " function g() { return G; }"
				          " function f() { var x = 1; { var x = 2; out = out + \" \" + to_string(x); } out = out + \" \" + to_string(x);"
				          "   { var G = 5; out = out + \" \" + to_string(g()) + \" \" + to_string(G); } }"
				          " f();"
				          " if (true) { var G = 3; out = out + \" \" + to_string(G); } out = out + \" \" + to_string(G);");
				return e.execute("{ var G = 20; out = out + \" \" + to_string(G); } out = out + \" \" + to_string(G); out;").as_string();
			});
		});
		test("a_file_reloaded_at_its_path_is_the_same_unit_and_writes_in_place", [this]() {
			every_backend("reload", "102 5", [](engine& e) {
				e.disable(jai::filesystem);
				const std::string path = "D:/virtual/shadowing/unit_a.jai";
				e.execute_file_source(path, "var G = 1; function f() { return G; }");
				e.execute("int G = 5;");
				e.execute_file_source(path, "var G = 2; function f() { return G + 100; }");
				return e.execute("to_string(f()) + \" \" + to_string(G)").as_string();
			});
		});
	}
};

} // namespace jai::foundry::tests

using global_shadowing_tests = jai::foundry::tests::global_shadowing_tests;
FOUNDRY_REGISTER(global_shadowing_tests)
