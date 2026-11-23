// A program-scope name read bare, a const above all: what shadows it (a local, a parameter, a field, a capture, a namespace member, a
// derived class's field, a field the class gains mid-run), what a reload does to it (a new value, a new kind, no const any more, a
// frame still running, a function the new source dropped) and what another unit reads. Every row runs both backends with proofs on
// and off against one expected string: a read's operand is resolved where it compiles, and what it resolves to is decided as it runs.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class program_const_tests : public suite {
public:
	program_const_tests() : suite("Program Consts") {}

	void every_backend(const std::string& label, const std::string& expected, const std::function<std::string(engine&)>& body) {
		for (bool vm : { false, true }) {
			for (bool proofs : { true, false }) {
				auto e = jai::foundry::make_default_engine();
				e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				std::string got;
				try { got = body(*e); }
				catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_eq(expected, got, label + (vm ? " vm" : " interp") + (proofs ? "" : " proofs off"));
			}
		}
	}

	static std::string text(engine& e, const std::string& source) { return e.execute(source).to_string(); }

	void forge_tests() override {
		test("what_shadows_a_program_const", [this]() {
			every_backend("shadows", "1 10 4 8 3 2|99 20 297 100|7 14|8 7 6|6 8|1 2 2 42|3 7 1|2.5 s true 7", [](engine& e) {
				return text(e, R"JS(
					const int K = 1;
					const float F = 2.5;
					const string S = "s";
					const bool B = true;
					const int D = K | 6;
					string out = "";
					class Base {
						int n = 0;
						int get() { return K; }
						int cmp() { if (K == 1) { return 10; } return 20; }
						int local() { int K = 4; return K; }
						int param(int K) { return K; }
						int sum() { int t = 0; for (int i = 0; i < 3; ++i) { t += K; } return t; }
						int step() { n = K; ++n; return n; }
					}
					class Derived : Base { int K = 99; }
					class Holder { int K = 7; int get() { return K; } int twice() { return K + K; } }
					Base b; Derived d; Holder h;
					out += to_string(b.get()) + " " + to_string(b.cmp()) + " " + to_string(b.local()) + " " + to_string(b.param(8)) + " " + to_string(b.sum()) + " " + to_string(b.step());
					out += "|" + to_string(d.get()) + " " + to_string(d.cmp()) + " " + to_string(d.sum()) + " " + to_string(d.step());
					out += "|" + to_string(h.get()) + " " + to_string(h.twice());
					int f() { return K + D; }
					int g(int K) { return K; }
					int l() { int K = 5; return K + 1; }
					out += "|" + to_string(f()) + " " + to_string(g(7)) + " " + to_string(l());
					{ int K = 6; out += "|" + to_string(K) + " " + to_string(f()); }
					auto lam = [K]() { return K; };
					auto lam2 = [=]() { return K + 1; };
					auto lam3 = [x = K]() { return x + K; };
					auto lam4 = (int K) => K * 2;
					out += "|" + to_string(lam()) + " " + to_string(lam2()) + " " + to_string(lam3()) + " " + to_string(lam4(21));
					namespace ns { const int K = 3; int h() { return K; } int far() { return D; } }
					out += "|" + to_string(ns::h()) + " " + to_string(ns::far()) + " " + to_string(::K);
					out += "|" + to_string(F) + " " + S + " " + to_string(B) + " " + to_string(D);
					out;
				)JS");
			});
		});

		// one receiver class after another through one method body: each frame decides the name for its own receiver
		test("a_base_method_reads_the_const_or_the_receivers_field", [this]() {
			every_backend("receivers", "1 99 1 99 1|3 101 3 101|4 400 4", [](engine& e) {
				return text(e, R"JS(
					const int K = 1;
					const int LIMIT = 3;
					class Base {
						int get() { return K; }
						int count() { int t = 0; for (int i = 0; i < LIMIT; ++i) { t += 1; } if (K > 50) { t += K - 1; } return t; }
						int pick() { int v = K; if (v == 1) { return 4; } return v + 300; }
					}
					class Derived : Base { int K = 99; }
					class Other : Base { int K = 100; }
					array<shared_ptr<Base>> all = [new Base(), new Derived(), new Base(), new Derived(), new Base()];
					string out = "";
					for (int i = 0; i < 5; ++i) { out += (i == 0 ? "" : " ") + to_string(all[i].get()); }
					out += "|";
					for (int i = 0; i < 4; ++i) { out += (i == 0 ? "" : " ") + to_string(all[i].count()); }
					shared_ptr<Base> o = new Other();
					out += "|" + to_string(all[0].pick()) + " " + to_string(o.pick()) + " " + to_string(all[2].pick());
					out;
				)JS");
			});
		});

		test("a_reload_reaches_code_the_new_source_dropped", [this]() {
			every_backend("reload", "1111 1111|2 12 2 2|2.5 2.5 2.5|6 6 6|k k k", [](engine& e) {
				e.disable(jai::permissions::filesystem);
				const std::string path = "D:/virtual/program_const/unit_a.jai";
				const std::string read = "to_string(stale()) + \" \" + to_string(held()) + \" \" + to_string(obj.m())";
				e.execute_file_source(path, "const int K = 1; var keep = () => K; function stale() { return K; } function fresh() { return K; }"
				                            " class C { function m() { if (K == 1) { return 1; } return K; } } C c;");
				std::string out = text(e, "to_string(stale()) + to_string(fresh()) + to_string(keep()) + to_string(c.m())");
				out += " " + text(e, "to_string(stale()) + to_string(fresh()) + to_string(keep()) + to_string(c.m())");
				e.execute("var held = keep; var obj = c;");
				e.execute_file_source(path, "const int K = 2; function fresh() { return K + 10; }");
				out += "|" + text(e, "to_string(stale()) + \" \" + to_string(fresh()) + \" \" + to_string(held()) + \" \" + to_string(obj.m())");
				e.execute_file_source(path, "const float K = 2.5;");
				out += "|" + text(e, read);
				e.execute_file_source(path, "int K = 5; void bump() { K += 1; }");
				e.execute("bump();");
				out += "|" + text(e, read);
				e.execute_file_source(path, "const string K = \"k\";");
				out += "|" + text(e, read);
				return out;
			});
		});

		test("a_running_frame_reads_the_reloaded_value", [this]() {
			every_backend("running frame", "22 242", [](engine& e) {
				e.disable(jai::permissions::filesystem);
				const std::string path = "D:/virtual/program_const/unit_b.jai";
				engine* raw = &e;
				e.add_function("reload", [raw, path](script_int k) {
					raw->execute_file_source(path, "const int K = " + std::to_string(k) + "; int run() { return -1; }");
					return k;
				});
				e.execute_file_source(path, R"JS(
					const int K = 1;
					int run() { int t = 0; for (int i = 0; i < 4; i++) { t += K; if (i == 1) { reload(10); } } return t; }
					class C { int run() { int t = 0; for (int i = 0; i < 4; i++) { if (K > 5) { t += 100; } t += K; if (i == 1) { reload(20); } } return t; } }
				)JS");
				std::string out = text(e, "run()");
				e.execute("C c;");
				e.execute_file_source(path, "const int K = 1;");
				out += " " + text(e, "c.run()");
				return out;
			});
		});

		test("a_use_ahead_of_the_declaration_raises", [this]() {
			every_backend("forward use", "raised 5 raised 6", [](engine& e) {
				return text(e, R"JS(
					int f() { return K; }
					class C { int m() { if (K == 5) { return K + 1; } return 0; } }
					C c;
					string first = "";
					try { first = to_string(f()); } catch (err) { first = "raised"; }
					string second = "";
					try { second = to_string(c.m()); } catch (err) { second = "raised"; }
					const int K = 5;
					first + " " + to_string(f()) + " " + second + " " + to_string(c.m());
				)JS");
			});
		});

		test("another_unit_reads_the_const_it_saw_and_declares_its_own", [this]() {
			every_backend("another unit", "11|1122|1122", [](engine& e) {
				e.execute("const int K = 1; int own() { return K; }");
				e.execute("int other() { return K; }");
				std::string out = text(e, "to_string(own()) + to_string(other())");
				e.execute("const int K = 2; int mine() { return K; }");
				out += "|" + text(e, "to_string(own()) + to_string(other()) + to_string(mine()) + to_string(K)");
				e.execute("int late() { return K; }");
				out += "|" + text(e, "to_string(own()) + to_string(other()) + to_string(mine()) + to_string(late())");
				return out;
			});
		});

		test("an_imported_units_const", [this]() {
			every_backend("import", "7 7 14|9 9 18", [](engine& e) {
				const auto dir = jai::foundry::test_temp_directory() / "program_const_import";
				std::filesystem::create_directories(dir);
				const auto file = dir / "limits.jai";
				{ std::ofstream(file, std::ios::trunc) << "const int LIMIT = 7; int limit() { return LIMIT; }\n"; }
				e.add_include_path(dir.string());
				e.import_recheck_interval(0);
				const std::string user = "import \"limits.jai\"; class U { int twice() { return LIMIT + LIMIT; } } int direct() { return LIMIT; }"
				                         " U u; to_string(limit()) + \" \" + to_string(direct()) + \" \" + to_string(u.twice())";
				std::string out = text(e, user);
				{ std::ofstream(file, std::ios::trunc) << "const int LIMIT = 9; int limit() { return LIMIT; }  // again\n"; }
				out += "|" + text(e, user);
				return out;
			});
		});

		test("a_class_redefined_mid_run_takes_the_name_as_a_field", [this]() {
			every_backend("class redefined mid-run", "1 102 50 1", [](engine& e) {
				engine* raw = &e;
				e.add_function("redefine", [raw](script_int) {
					raw->execute("class P { int K = 50; int a = 0; int get() { return K; } int loop() { return -1; } }");
					return script_int(0);
				});
				e.execute(R"JS(
					const int K = 1;
					class P { int a = 0; int get() { return K; } int loop() { int t = 0; for (int i = 0; i < 4; i++) { t += K; if (i == 1) { redefine(0); } } return t; } }
					P p;
				)JS");
				std::string out = text(e, "to_string(p.get()) + \" \" + to_string(p.loop())");
				out += " " + text(e, "to_string(p.get()) + \" \" + to_string(K)");
				return out;
			});
		});

		// a bare field stepped, compared and bounding a counted loop in its method, on the class and on a derived one that lays the
		// fields out alike, then on a class that renames the layout
		test("a_bare_field_steps_and_bounds_a_loop_in_its_method", [this]() {
			every_backend("fields", "3 6 2.5|3 6 2.5|13 4", [](engine& e) {
				return text(e, R"JS(
					class Counter {
						int n = 0;
						int limit = 3;
						float f = 0.5;
						int run() { for (int i = 0; i < limit; ++i) { ++n; } return n; }
						int again() { n++; ++n; n += 1; return n; }
						float half() { ++f; f++; return f; }
					}
					class Wide : Counter { int extra = 1; }
					class Renamed { int limit = 2; int n = 10; int run() { for (int i = 0; i < limit; ++i) { ++n; } n++; return n; } int lim() { limit++; ++limit; return limit; } }
					Counter c; Wide w; Renamed r;
					string out = to_string(c.run()) + " " + to_string(c.again()) + " " + to_string(c.half());
					out += "|" + to_string(w.run()) + " " + to_string(w.again()) + " " + to_string(w.half());
					out += "|" + to_string(r.run()) + " " + to_string(r.lim());
					out;
				)JS");
			});
		});
	}
};

} // namespace jai::foundry::tests

using program_const_tests = jai::foundry::tests::program_const_tests;
FOUNDRY_REGISTER(program_const_tests)
