#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

// The proofs of a program that imports or includes at run time (internal/type_proofs_design.md, "Programs that load at run time"): every
// program runs on {interpreter, vm} x {proofs on, proofs off} and must give one answer
namespace jai::foundry::tests {

class include_proof_gate_tests : public suite {
public:
	include_proof_gate_tests() : suite("Include Proof Gates") {}

	struct step {
		std::vector<std::pair<std::string, std::string>> files;   // written before the step runs (a second write of a name is an edit)
		std::string source;                                       // executed as a string
		std::string file;                                         // or executed as this file of the directory
		bool fresh = false;                                       // a new engine runs this step and the ones after it, in the same directory
		std::vector<std::string> removed;                         // files deleted before the step runs
	};
	struct outcome {
		std::string text[4];
		std::string listing;   // the vm's, proofs on
	};

	static std::filesystem::path directory(const std::string& name, int way) {
		std::filesystem::path dir = jai::foundry::test_temp_directory() / "include_proof_gates" / (name + "_" + std::to_string(way));
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		return dir;
	}
	static void write(const std::filesystem::path& path, const std::string& text) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}

	// the steps on one engine per way; a step's answer is its value, or its error's first line
	static outcome run(const std::string& name, const std::vector<step>& steps, bool bite = false) {
		outcome result;
		for (int way = 0; way < 4; ++way) {
			const std::filesystem::path dir = directory(name, way);
			const auto engine_of_the_way = [&result, &dir, way]() {
				auto e = jai::foundry::make_default_engine();
				e->set_backend(way < 2 ? jai::backend_type::interpreter : jai::backend_type::vm);
				e->type_proofs(way % 2 == 0);
				e->execution_budget(0);
				e->import_recheck_interval(0);
				e->jaibite_cache(true);
				jai::stdlib::register_all(e);
				e->add_include_path(dir.string());
				e->add_global("DIR", e->make_value(dir.generic_string()));
				// the program's own edit of one of its files, for a reload in the middle of a loop
				e->add_function("edit_file", [dir](const std::string& file, const std::string& text) { write(dir / file, text); });
				e->add_function("copy_file", [dir](const std::string& to, const std::string& from) {
					std::ifstream in(dir / from, std::ios::binary);
					write(dir / to, std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
				});
				if (way == 2) { e->disassembly_sink([&result](const std::string& listing, const std::string&) { result.listing += listing; result.listing += '\n'; }); }
				return e;
			};
			auto e = engine_of_the_way();
			std::string& text = result.text[way];
			for (const step& s : steps) {
				if (s.fresh) { e = engine_of_the_way(); }
				for (const std::string& gone : s.removed) { std::filesystem::remove(dir / gone); }
				for (const auto& [file, body] : s.files) { write(dir / file, body); }
				if (!text.empty()) { text += " / "; }
				try {
					if (!s.file.empty()) { text += e->execute_file((dir / s.file).string()).to_string(); }
					else if (bite) { auto b = e->jaibite(s.source); text += b.execute().to_string(); }
					else { text += e->execute(s.source).to_string(); }
				} catch (const std::exception& ex) {
					const std::string what = ex.what();
					text += "ERROR: " + what.substr(0, what.find('\n'));
				}
			}
		}
		return result;
	}

	void agree(const outcome& r, const std::string& what) {
		check_eq(r.text[0], r.text[1], what + ": interpreter, proofs on and off");
		check_eq(r.text[2], r.text[3], what + ": vm, proofs on and off");
		check_eq(r.text[0], r.text[2], what + ": interpreter and vm");
	}
	void expect(const outcome& r, const std::string& expected, const std::string& what) {
		agree(r, what);
		check_eq(expected, r.text[2], what);
	}
	static size_t count(const std::string& text, const std::string& piece) {
		size_t n = 0;
		for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + 1)) { ++n; }
		return n;
	}

	void forge_tests() override {
		const std::string leaves =
			"array<float> XS = [1.0, 2.0, 3.0, 4.0];\nfloat SCALE = 2.0;\nint N = 4;\nint IDX = 0;\n"
			"float scale(float v, float s) { return v * s; }\n"
			"int two(int a, int b) { return a + b; }\n";
		const std::string sum_all = "float sum_all() { float acc = 0.0; for (int i = 0; i < N; i++) { acc += scale(XS[i], SCALE); } return acc; }\n";

		test("a_computed_import_keeps_the_proofs_of_arguments_that_are_globals", [this, leaves, sum_all]() {
			const std::string main = leaves + sum_all + "string DEV = \"dev.jai\";\nvoid tick() { import(DEV); }\ntick();\nfloat total = 0.0;\n"
				"for (int k = 0; k < 50; k++) { total += sum_all(); }\nto_string(total) + \"|\" + to_string(DEV_LOADS);\n";
			const outcome r = run("keeps_proofs", {{{{"dev.jai", "int DEV_LOADS = 1;\n"}, {"main.jai", main}}, "", "main.jai"}});
			expect(r, "1000.0|1", "the program's value");
			check(count(r.listing, "-- inlined scale") > 0, "the call that takes a global element and a global grafts: " + r.listing.substr(0, 4000));
			check_eq(size_t(0), count(r.listing, "an unproven argument"), "no call is kept for an argument's proof");
		});

		test("a_scoped_runtime_include_keeps_program_names_unproven_in_its_unit", [this, leaves]() {
			// include(expr) inside a function declares into that function's environment, which the function and what it calls read ahead of the program's names
			const std::string main = leaves + "string P = \"shadow.jai\";\nfunction reads() { return two(IDX, 1); }\n"
				"function scoped() { include(P); return to_string(reads()) + \",\" + reflect::type_of(IDX); }\n"
				"string r = to_string(reads());\nstring s = \"\";\ntry { s = scoped(); } catch (e) { s = to_string(e); }\nr + \"|\" + s + \"|\" + to_string(reads());\n";
			const outcome r = run("scoped_include", {{{{"shadow.jai", "string IDX = \"seven\";\n"}, {"main.jai", main}}, "", "main.jai"}});
			agree(r, "a function under the including function");
			check_eq(size_t(0), count(r.listing, "-- inlined two"), "the unit's calls keep their global arguments unproven: " + r.listing.substr(0, 4000));
		});

		test("a_declaration_that_retypes_a_global_in_place_is_seen_by_the_next_graft", [this, leaves]() {
			const std::string uses = leaves + "array<int> arr = [1, 2, 3];\nint at = 0;\nfunction f() { return two(IDX, arr[at]); }\nstring r = to_string(f());\n";
			const std::string retype = "r += \"|\" + to_string(f());\nstring IDX = \"s\";\nat = 10;\ntry { r += \"|\" + to_string(f()); } catch (e) { r += \"|\" + to_string(e); }\n";
			const std::string tail = "try { r += \"|\" + to_string(f()); } catch (e) { r += \"|\" + to_string(e); }\nr;\n";
			const std::string expected = "1|1|Array index 10 out of bounds for array of size 3|Array index 10 out of bounds for array of size 3";
			expect(run("retype_literal", {{{{"retype.jai", retype}, {"main.jai", uses + "include \"retype.jai\";\n" + tail}}, "", "main.jai"}}), expected, "a literal include");
			expect(run("retype_computed", {{{{"retype.jai", retype}, {"main.jai", uses + "string P = \"retype.jai\";\ninclude(P);\n" + tail}}, "", "main.jai"}}), expected, "a computed include");
		});

		test("a_runtime_unit_declares_and_redeclares_globals", [this, leaves]() {
			// a name only the runtime unit declares, used before and after; a typed global it declares again is its own cell
			const std::string main = "[[warn_mode]];\n" + leaves + "string DEV = \"late.jai\";\nfunction uses() { return two(LATE, IDX); }\nfunction own() { return two(IDX, N); }\n"
				"string r = \"\";\ntry { r += to_string(uses()); } catch (e) { r += to_string(e); }\nr += \"|\" + to_string(own());\nimport(DEV);\n"
				"r += \"|\" + to_string(uses()) + \"|\" + to_string(own()) + \"|\" + late_idx() + \"|\" + reflect::type_of(IDX);\nr;\n";
			const std::string late = "int LATE = 40;\nstring IDX = \"theirs\";\nfloat N = 0.5;\nfunction late_idx() { return IDX + \",\" + to_string(N); }\n";
			expect(run("declares", {{{{"late.jai", late}, {"main.jai", main}}, "", "main.jai"}}), "Undefined variable 'LATE'|4|40|4|theirs,0.5|int", "the root keeps its cells");
		});

		test("a_reload_through_a_computed_import_retypes_a_global_another_unit_reads", [this, leaves, sum_all]() {
			const std::string main = "import \"state.jai\";\narray<float> XS = [1.0, 2.0, 3.0, 4.0];\nint N = 4;\nfloat scale(float v, float s) { return v * s; }\n" + sum_all +
				"array<string> MODS = [\"state.jai\"];\nfunction tick() { try { return import(MODS) ? \"ran\" : \"same\"; } catch (e) { return \"rejected\"; } }\n"
				"string r = to_string(sum_all());\nfor (int k = 0; k < 20; k++) { sum_all(); }\nr;\n";
			const std::string poll = "tick() + \",\" + to_string(sum_all()) + \",\" + reflect::type_of(SCALE)";
			const outcome r = run("reload_retype", {
				{{{"state.jai", "float SCALE = 2.0;\n"}, {"main.jai", main}}, "", "main.jai"},
				{{{"state.jai", "int SCALE = 3;   // an int now\n"}}, poll, ""},
				{{{"state.jai", "string SCALE = \"a text the float parameter refuses\";\n"}}, poll, ""},
				{{{"state.jai", "float SCALE = 0.5;   // and a float again, another size\n"}}, poll, ""},
			});
			agree(r, "each reload");
			check(r.text[2].rfind("20.0 / ran,30.0,int / ", 0) == 0, "the int reload converts at the call: " + r.text[2]);
			check(r.text[2].find(" / ran,5.0,float") != std::string::npos, "the last reload lands: " + r.text[2]);
		});

		test("a_reload_redefines_an_inlined_function_while_its_callers_loop_runs", [this]() {
			// the module's file changes inside the loop, and the loop's own tick imports it: another body, an untyped parameter, a parameter
			// that refuses the argument, another return type, a result of another kind, each from the iteration after the import
			const std::string main = "import \"leaf.jai\";\narray<float> XS = [1.0, 2.0, 3.0, 4.0];\nfloat BIAS = 0.5;\narray<string> MODS = [\"leaf.jai\"];\n"
				"function tick() { try { import(MODS); } catch (e) { return 1; } return 0; }\n"
				"function drive(string text, int at) {\n\tstring seen = \"\";\n\tint rejected = 0;\n\tfor (int i = 0; i < 8; i++) {\n"
				"\t\tif (i == at) { edit_file(\"leaf.jai\", text); rejected += tick(); }\n"
				"\t\ttry { seen += to_string(leaf(XS[i % 4], BIAS)) + \",\"; } catch (e) { seen += \"E,\"; }\n\t}\n\treturn seen + to_string(rejected);\n}\n"
				"string r = drive(\"float leaf(float a, float b) { return a * 3.0 + b; }   // 1\\n\", 3);\n"
				"r += \"|\" + drive(\"float leaf(var a, float b) { return a * 10.0 + b; }   // 22\\n\", 2);\n"
				"r += \"|\" + drive(\"float leaf(int a, float b) { return a * 10.0 + b; }   // 333\\n\", 4);\n"
				"r += \"|\" + drive(\"int leaf(float a, float b) { return 7; }   // 4444\\n\", 5);\n"
				"r += \"|\" + drive(\"string leaf(float a, float b) { return `<${a}>`; }   // 55555\\n\", 1);\nr;\n";
			const outcome r = run("reload_inlined", {{{{"leaf.jai", "float leaf(float a, float b) { return a * 2.0 + b; }\n"}, {"main.jai", main}}, "", "main.jai"}});
			expect(r, "2.5,4.5,6.5,12.5,3.5,6.5,9.5,12.5,0|3.5,6.5,30.5,40.5,10.5,20.5,30.5,40.5,0|10.5,20.5,30.5,40.5,E,E,E,E,0|E,E,E,E,E,7,7,7,0|7,<2.0>,<3.0>,<4.0>,<1.0>,<2.0>,<3.0>,<4.0>,0",
				"each redefinition takes effect at the call after its import");
			// the same in a loop of typed arithmetic alone, the reload behind a call the loop makes
			const std::string block = "import \"leaf.jai\";\narray<float> XS = [1.0, 2.0, 3.0, 4.0];\nfloat BIAS = 0.5;\narray<string> MODS = [\"leaf.jai\"];\nstring NEXT = \"\";\n"
				"function swap(int i, int at) { if (i == at) { edit_file(\"leaf.jai\", NEXT); import(MODS); } return 0; }\n"
				"float drive(int at) {\n\tfloat acc = 0.0;\n\tfor (int i = 0; i < 12; i++) {\n\t\tacc += leaf(XS[i % 4], BIAS);\n\t\tacc += swap(i, at);\n\t}\n\treturn acc;\n}\n"
				"string r = to_string(drive(99));\nNEXT = \"float leaf(float a, float b) { return a * 3.0 + b; }   // 1\\n\";\nr += \"|\" + to_string(drive(5));\n"
				"NEXT = \"float leaf(var a, float b) { return a + b + 100.0; }   // 22\\n\";\nr += \"|\" + to_string(drive(2));\n"
				"NEXT = \"int leaf(float a, float b) { return 7; }   // 333\\n\";\nr += \"|\" + to_string(drive(7));\nr;\n";
			const outcome b = run("reload_inlined_block", {{{{"leaf.jai", "float leaf(float a, float b) { return a * 2.0 + b; }\n"}, {"main.jai", block}}, "", "main.jai"}});
			expect(b, "66.0|83.0|948.0|852.0", "a loop of typed arithmetic takes each redefinition at the call after the reload");
			check(count(b.listing, "-- inlined leaf") > 0, "the loop's call grafts: " + b.listing.substr(0, 3000));
		});

		test("a_runtime_unit_assigns_the_globals_a_block_reads", [this]() {
			// the loop reads a typed global and a var global as cells; the module's function, called in the loop, stores into both
			const std::string main = "float GAIN = 1.5;\nvar LOOSE = 2;\nstring DEV = \"poke.jai\";\nimport(DEV);\n"
				"function run() {\n\tfloat acc = 0.0;\n\tstring kinds = \"\";\n\tfor (int i = 0; i < 6; i++) {\n\t\tacc += GAIN * i;\n\t\tif (i == 2) { poke(i); }\n"
				"\t\tif (i == 4) { poke(i); }\n\t\tkinds += reflect::type_of(LOOSE) + \",\";\n\t}\n\treturn to_string(acc) + \"|\" + kinds + to_string(GAIN);\n}\nrun();\n";
			const std::string poke = "function poke(int i) { GAIN = i; if (i == 2) { LOOSE = \"text\"; } else { LOOSE = 0.25; } }\n";
			expect(run("assigns", {{{{"poke.jai", poke}, {"main.jai", "[[warn_mode]];\n" + main}}, "", "main.jai"}}), "38.5|int,int,string,string,float,float,4.0", "the stores convert and the block reads them");
		});

		test("a_reload_redefines_a_class_whose_field_a_loop_reads", [this]() {
			const std::string main = "import \"body.jai\";\narray<string> MODS = [\"body.jai\"];\nfloat lift(float v, float k) { return v + k; }\nfloat K = 0.25;\n"
				"function tick() { try { import(MODS); } catch (e) { return 1; } return 0; }\n"
				"function drive(string text) {\n\tBody b = Body();\n\tstring seen = \"\";\n\tfor (int i = 0; i < 6; i++) {\n\t\tif (i == 3) { edit_file(\"body.jai\", text); tick(); }\n"
				"\t\tseen += to_string(lift(b.w, K)) + \",\";\n\t}\n\treturn seen + reflect::type_of(b.w);\n}\n"
				"string r = drive(\"class Body { int w = 4;      }\\n\");\nr += \"|\" + drive(\"class Body { float w = 8.5; int extra = 1;   }\\n\");\nr;\n";
			expect(run("reload_class", {{{{"body.jai", "class Body { float w = 1.5; }\n"}, {"main.jai", main}}, "", "main.jai"}}),
				"1.75,1.75,1.75,1.25,1.25,1.25,int|4.25,4.25,4.25,4.25,4.25,4.25,float", "a live instance migrates and the call converts its field");
		});

		test("an_import_reached_from_a_method_a_destructor_and_a_parallel_body", [this, leaves]() {
			const std::string main = "[[warn_mode]];\n" + leaves + "array<string> MODS = [\"unit.jai\"];\nint LOADS = 0;\nfunction f() { return two(IDX, N); }\n"
				"class Loader { int n = 0; function load() { n += import(MODS) ? 1 : 0; return f(); } ~Loader() { import(MODS); LOADS += f(); } }\n"
				"string r = to_string(f());\n{ Loader l = Loader(); r += \"|\" + to_string(l.load()) + \",\" + to_string(l.load()) + \",\" + to_string(l.n); }\n"
				"r += \"|\" + to_string(LOADS) + \"|\" + to_string(UNIT_RUNS);\n"
				"array<int> cells = [1, 2, 3, 4];\ntry { parallel_for (auto& c : cells) { import(MODS); c = c + f(); } r += \"|\" + to_string(cells[0] + cells[3]); } catch (e) { r += \"|refused\"; }\nr;\n";
			const outcome r = run("reached_from", {{{{"unit.jai", "inline int UNIT_RUNS;\nUNIT_RUNS += 1;\n"}, {"main.jai", main}}, "", "main.jai"}});
			agree(r, "an import in a method, a destructor and a parallel body");
			check_eq(std::string("4|4,4,1|4|1|refused"), r.text[2], "the module runs once, every caller reads the root's globals, and a parallel body takes no import");
		});

		test("a_program_built_as_a_bite_runs_with_and_without_its_module", [this, leaves, sum_all]() {
			const std::string source = "[[warn_mode]];\n" + leaves + sum_all + "string DEV = DIR + \"/live.jai\";\n"
				"function tick() { try { import(DEV); return \"live\"; } catch (e) { return \"absent\"; } }\n"
				"string r = tick() + \",\" + to_string(sum_all());\nfor (int k = 0; k < 20; k++) { sum_all(); }\nr + \",\" + to_string(SCALE);\n";
			expect(run("bite_absent", {{{}, source, ""}}, true), "absent,20.0,2.0", "the module is not there");
			expect(run("bite_present", {{{{"live.jai", "SCALE = 4.0;\n"}}, source, ""}}, true), "live,40.0,4.0", "the module stores into the program's global");
			// a file's cached bite: the first engine writes it beside the file, the next two load it, one with the module gone, one with it back
			const outcome cached = run("bite_cache", {
				{{{"live.jai", "SCALE = 4.0;\n"}, {"main.jai", source}}, "", "main.jai"},
				{{}, "", "main.jai", true, {"live.jai"}},
				{{{"live.jai", "SCALE = 8.0;\n"}}, "", "main.jai", true},
			});
			expect(cached, "live,40.0,4.0 / absent,20.0,2.0 / live,80.0,8.0", "one file, three engines");
		});

		test("a_unit_with_imports_keeps_the_grafts_and_field_reads_of_its_methods", [this]() {
			const std::string main = "import \"helper.jai\";\nstring DEV = \"dev.jai\";\nfloat GAIN = 1.5;\nclass Body {\n	float w = 2.0;\n	int n = 0;\n	float lift(float k) { return w * k + GAIN; }\n	float run(int count) { float acc = 0.0; for (int i = 0; i < count; i++) { acc += lift(i * 0.5); n += 1; } return acc; }\n}\nimport(DEV);\nBody b = Body();\nto_string(b.run(8)) + \"|\" + to_string(b.n) + \"|\" + to_string(helper_half(3));\n";
			const outcome r = run("method_proofs", {{{{"helper.jai", "float helper_half(int i) { return i * 0.5; }\n"}, {"dev.jai", "int DEV_LOADS = 1;\n"}, {"main.jai", main}}, "", "main.jai"}});
			expect(r, "40.0|8|1.5", "the method's loop");
			check(count(r.listing, "-- inlined lift") > 0, "a sibling's call grafts in a unit that imports: " + r.listing.substr(0, 4000));
			check_eq(size_t(0), count(r.listing, "call kept lift"), "the sibling stays no call");
		});

		test("a_reload_redefines_a_class_under_its_running_method", [this]() {
			// the method's loop reloads its own class: a field retyped and a sibling's body changed, a field of a program name's spelling added,
			// a subclass that overrides the sibling; the frame on the stack keeps its body and reads what the instance now holds
			const std::string v1 = "import \"helper.jai\";\nclass Body {\n	float w = 2.0;\n	int n = 0;\n	float lift(float k) { return w * k + GAIN; }\n	string run(string next, int at) {\n		string seen = \"\";\n		for (int i = 0; i < 6; i++) {\n			if (i == at) { copy_file(\"shape.jai\", next); reload(); }\n			seen += to_string(lift(1.0)) + \":\" + to_string(w) + \":\" + to_string(GAIN) + \",\";\n			n += 1;\n		}\n		return seen + to_string(n) + to_string(helper_half(0));\n	}\n}\n";
			const std::string v2 = "import \"helper.jai\";\nclass Body {\n	int w = 5;\n	int n = 0;\n	float lift(float k) { return w * k * 10.0 + GAIN; }\n	string run(string next, int at) {\n		string seen = \"\";\n		for (int i = 0; i < 6; i++) {\n			if (i == at) { copy_file(\"shape.jai\", next); reload(); }\n			seen += to_string(lift(1.0)) + \":\" + to_string(w) + \":\" + to_string(GAIN) + \",\";\n			n += 1;\n		}\n		return seen + to_string(n) + to_string(helper_half(0));\n	}\n}\n";
			const std::string v3 = "import \"helper.jai\";\nclass Body {\n	float w = 1.0;\n	int n = 0;\n	float GAIN = 100.0;\n	float lift(float k) { return w * k + GAIN; }\n	string run(string next, int at) {\n		string seen = \"\";\n		for (int i = 0; i < 6; i++) {\n			if (i == at) { copy_file(\"shape.jai\", next); reload(); }\n			seen += to_string(lift(1.0)) + \":\" + to_string(w) + \":\" + to_string(GAIN) + \",\";\n			n += 1;\n		}\n		return seen + to_string(n) + to_string(helper_half(0));\n	}\n}\n";
			const std::string v4 = "import \"helper.jai\";\nclass Body {\n	float w = 2.0;\n	int n = 0;\n	float lift(float k) { return w * k + GAIN; }\n	string run(string next, int at) {\n		string seen = \"\";\n		for (int i = 0; i < 6; i++) {\n			if (i == at) { copy_file(\"shape.jai\", next); reload(); }\n			seen += to_string(lift(1.0)) + \":\" + to_string(w) + \":\" + to_string(GAIN) + \",\";\n			n += 1;\n		}\n		return seen + to_string(n) + to_string(helper_half(0));\n	}\n}\nclass Heavy : Body { float lift(float k) override { return 1000.0 + k; } }\n";
			const std::string main = "float GAIN = 0.5;\nimport \"shape.jai\";\narray<string> MODS = [\"shape.jai\"];\nfunction reload() { try { import(MODS); } catch (e) { return 1; } return 0; }\nBody b = Body();\nstring r = b.run(\"shape_v2.jai\", 2);\nr += \"|\" + b.run(\"shape_v3.jai\", 3);\nr += \"|\" + b.run(\"shape_v4.jai\", 1);\ncopy_file(\"shape.jai\", \"shape_v4.jai\");\nreload();\nHeavy h = Heavy();\nr += \"|\" + h.run(\"shape_v1.jai\", 4);\nr;\n";
			const outcome r = run("reload_method", {{{{"helper.jai", "float helper_half(int i) { return i * 0.5; }\n"}, {"shape.jai", v1}, {"shape_v1.jai", v1}, {"shape_v2.jai", v2},
				{"shape_v3.jai", v3}, {"shape_v4.jai", v4}, {"main.jai", main}}, "", "main.jai"}});
			expect(r, "2.5:2.0:0.5,2.5:2.0:0.5,20.5:2:0.5,20.5:2:0.5,20.5:2:0.5,20.5:2:0.5,60.0|20.5:2:0.5,20.5:2:0.5,20.5:2:0.5,102.0:2.0:100.0,102.0:2.0:100.0,102.0:2.0:100.0,120.0|"
				"102.0:2.0:100.0,2.5:2.0:0.5,2.5:2.0:0.5,2.5:2.0:0.5,2.5:2.0:0.5,2.5:2.0:0.5,180.0|1001.0:2.0:0.5,1001.0:2.0:0.5,1001.0:2.0:0.5,1001.0:2.0:0.5,1001.0:2.0:0.5,1001.0:2.0:0.5,60.0",
				"a class reloaded under its own method");
			check(count(r.listing, "-- inlined lift") > 0, "the sibling grafts in the module, a unit that imports: " + r.listing.substr(0, 3000));
		});

		test("an_include_inside_a_method_keeps_its_units_member_proofs_closed", [this]() {
			const std::string main = "int GAIN = 7;\nclass C { int hp = 3; function m(string p) { include(p); return reflect::type_of(hp) + \":\" + to_string(hp) + \":\" + to_string(twice()) + \":\" + to_string(GAIN); } function twice() { return hp * 2; } }\nC().m(\"shadow.jai\") + \"|\" + C().m(\"empty.jai\");\n";
			const outcome r = run("scoped_method", {{{{"shadow.jai", "string hp = \"shadow\";\nstring GAIN = \"theirs\";\n"}, {"empty.jai", "int unused_here = 1;\n"}, {"main.jai", main}}, "", "main.jai"}});
			agree(r, "a name an include declares in a method's scope");
			check_eq(size_t(0), count(r.listing, "-- inlined twice"), "the unit's sibling calls stay calls: " + r.listing.substr(0, 3000));
		});
	}
};

} // namespace jai::foundry::tests

using include_proof_gate_tests = jai::foundry::tests::include_proof_gate_tests;
FOUNDRY_REGISTER(include_proof_gate_tests)
