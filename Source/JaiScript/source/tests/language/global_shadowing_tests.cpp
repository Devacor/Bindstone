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
