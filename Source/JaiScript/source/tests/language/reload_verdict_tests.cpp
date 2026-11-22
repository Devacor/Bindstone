// What a reload decides (docs/site/embedding/07-hot-reload.html "The contract", "Callers in other files"): a rejected reload
// leaves nothing and waits for a file it involved to change, and a generic's instance gets the verdict the plain function or
// class in its place gets (docs/site/guide/21-generics.html "Declaring it again"), in every arrangement of the files
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace jai::foundry::tests {

class reload_verdict_tests : public suite {
public:
	reload_verdict_tests() : suite("Reload Verdicts") {}

	// one program in a folder of its own: files written with a stamp that always moves, an engine that checks every reach
	struct program {
		std::filesystem::path dir;
		std::shared_ptr<engine> eng;
		int tick = 0;

		explicit program(const std::string& name) : dir(test_temp_directory() / ("rv_" + name)) {
			std::error_code ec;
			std::filesystem::remove_all(dir, ec);
			std::filesystem::create_directories(dir, ec);
			eng = make_engine();
			stdlib::register_all(*eng);
			eng->add_include_path(dir.string());
			eng->import_recheck_interval(0);
			eng->jaibite_cache(false);
		}
		~program() {
			eng.reset();
			std::error_code ec;
			std::filesystem::remove_all(dir, ec);
		}
		std::string path(const std::string& file) const { return (dir / file).generic_string(); }
		void write(const std::string& file, const std::string& text) {
			const auto p = dir / file;
			{ std::ofstream(p, std::ios::binary | std::ios::trunc) << text; }
			std::error_code ec;
			std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick + 4000), ec);
		}
		std::string run(const std::string& code) {
			try {
				return eng->execute(code).to_string();
			} catch (const std::exception& e) {
				return std::string("ERR: ") + e.what();
			}
		}
		std::string run_file(const std::string& file) {
			try {
				return eng->execute_file(path(file)).to_string();
			} catch (const std::exception& e) {
				return std::string("ERR: ") + e.what();
			}
		}
	};

	static bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
	static bool failed(const std::string& text) { return text.rfind("ERR: ", 0) == 0; }

	// one generic or plain declaration of the name, in the forms the same program is written in
	struct family {
		std::string name;
		std::string declare;   // %s: the letter its body answers
		std::string call;
	};
	static std::string declared(const family& f, const std::string& letter) {
		std::string text = f.declare;
		for (size_t at = text.find("%s"); at != std::string::npos; at = text.find("%s")) { text.replace(at, 2, letter); }
		return text;
	}
	static std::vector<family> families() {
		return {
			{"a plain function", "string f(int x) { return \"%s\"; }\n", "f(1)"},
			{"a generic function", "string f<T>(T x) { return \"%s\"; }\n", "f(1)"},
			{"a generic function called with its type argument", "string f<T>(T x) { return \"%s\"; }\n", "f<int>(1)"},
			{"a generic class", "class F<T> { string f(T x) { return \"%s\"; } }\n", "F<int>().f(1)"},
			{"a class in a namespace", "namespace n { class F { string f(int x) { return \"%s\"; } } }\n", "n::F().f(1)"},
			{"a generic class in a namespace", "namespace n { class F<T> { string f(T x) { return \"%s\"; } } }\n", "n::F<int>().f(1)"},
			{"a class with a plain method", "class K { string f(int x) { return \"%s\"; } }\n", "K().f(1)"},
			{"a class with a generic method", "class K { string f<T>(T x) { return \"%s\"; } }\n", "K().f(1)"},
		};
	}

	void forge_tests() override {
		test("a_rejected_reload_leaves_none_of_its_declarations", [this]() {
			// the reload is reached from a file's function, or from an executed string whose own text names the import
			for (const bool through_string : {false, true}) {
				const std::string how = through_string ? " (reached from an executed string)" : " (reached from a file's function)";
				program p(through_string ? "a_string" : "a_file");
				p.write("lib.jai", "int size_of(int x) { return x; }\n");
				p.write("root.jai", "import \"lib.jai\";\nint use() { return size_of(1); }\nint tick() { import(\"lib.jai\"); return use(); }\n");
				check_eq(std::string("null"), p.run_file("root.jai"));
				p.write("lib.jai", "int size_of(string x) { return 9; }\nint fresh() { return 7; }\nclass Fresh { int n = 4; }\nint FRESH = 5;\n");
				const std::string rejected = p.run(through_string ? "import(\"lib.jai\");" : "tick();");
				check_true(has(rejected, "root.jai:2:29: error: argument 1 of 'size_of': cannot pass 'int' to 'x' declared 'string'"), "the caller rejects the reload" + how + ": " + rejected);
				check_true(has(p.run("fresh();"), "<script>:1:1: error: use of undeclared identifier 'fresh'"), "a function of the rejected text is not declared" + how + ": " + p.run("fresh();"));
				check_true(has(p.run("Fresh().n;"), "<script>:1:1: error: use of undeclared identifier 'Fresh'"), "nor a class" + how);
				check_true(has(p.run("FRESH;"), "<script>:1:1: error: use of undeclared identifier 'FRESH'"), "nor a variable" + how);
				check_true(has(p.run("int q() { return fresh(); }\nq();"), "error: use of undeclared identifier 'fresh'"), "nor inside a function a later text declares" + how);
				check_eq(std::string("1"), p.run("size_of(1);"), "the function keeps the signature it runs with" + how);
				check_true(has(p.run("size_of(\"a\");"), "<script>:1:12: error: argument 1 of 'size_of': cannot pass 'string' to 'x' declared 'int'"), "and a call the rejected text would take is refused" + how + ": " + p.run("size_of(\"a\");"));
				check_eq(std::string("1"), p.run("use();"), "the old code runs" + how);
				// the fix applies, and what it declares is declared
				p.write("lib.jai", "int size_of(int x) { return x + 1; }\nint fresh() { return 7; }\n");
				check_eq(std::string("2"), p.run(through_string ? "import(\"lib.jai\"); use();" : "tick();"), "the fix reloads" + how);
				check_eq(std::string("7"), p.run("fresh();"), "and brings its declarations" + how);
			}
			// a root file run again whose closure holds a rejected edit: the same nothing
			{
				program p("a_root");
				p.write("lib.jai", "int size_of(int x) { return x; }\n");
				p.write("user.jai", "int use() { return size_of(1); }\n");
				p.write("root.jai", "import \"lib.jai\";\nimport \"user.jai\";\n");
				check_eq(std::string("null"), p.run_file("root.jai"));
				p.write("lib.jai", "int size_of(string x) { return 9; }\nint fresh() { return 7; }\n");
				const std::string rejected = p.run_file("root.jai");
				check_true(has(rejected, "user.jai:1:29: error: argument 1 of 'size_of'"), "the root's own run again is rejected for the caller: " + rejected);
				check_true(has(p.run("fresh();"), "error: use of undeclared identifier 'fresh'"), "and leaves no declaration of the rejected text: " + p.run("fresh();"));
				check_eq(std::string("1"), p.run("size_of(1);"));
				check_eq(std::string("1"), p.run("use();"));
			}
			// the string's own declarations ran before its import was reached: they stay, as any code that ran does
			{
				program p("a_beside");
				p.write("lib.jai", "int size_of(int x) { return x; }\n");
				p.write("root.jai", "import \"lib.jai\";\nint use() { return size_of(1); }\n");
				check_eq(std::string("null"), p.run_file("root.jai"));
				p.write("lib.jai", "int size_of(string x) { return 9; }\nint fresh() { return 7; }\n");
				check_true(failed(p.run("int beside() { return 3; }\nimport \"lib.jai\";\nbeside();")), "the import's rejection is the string's error");
				check_eq(std::string("3"), p.run("beside();"), "what the string declared before the import stays declared");
				check_true(has(p.run("fresh();"), "error: use of undeclared identifier 'fresh'"), "the module's rejected text does not");
			}
		});

		test("a_module_rejected_for_a_file_in_its_closure_raises_once_and_waits", [this]() {
			struct closure {
				std::string name;
				std::string part;          // the file in the module's closure; empty: the module's own text is what breaks
				std::string good, broken, fixed;
				std::string outer;         // the module the import reaches
				std::string error;         // what the rejection names
			};
			const std::vector<closure> closures = {
				{"an imported module", "part.jai", "int part() { return 1; }\n", "int part() { return \"broken\"; }\n", "int part() { return 2; }\n",
				 "import \"part.jai\";\nint outer() { return part() + 10; }\n", "part.jai:1:14: error: cannot return 'string' from 'part'"},
				{"a file included at the top level", "part.jai", "int part() { return 1; }\n", "int part() { return \"broken\"; }\n", "int part() { return 2; }\n",
				 "include \"part.jai\";\nint outer() { return part() + 10; }\n", "part.jai:1:14: error: cannot return 'string' from 'part'"},
				{"a file spliced into a function", "part.jai", "int n = 1;\n", "int n = \"broken\";\n", "int n = 2;\n",
				 "int outer() { include \"part.jai\"; return n + 10; }\n", "error: cannot initialize 'n' declared 'int' with 'string'"},
				{"a generic's file whose edit breaks an instance", "part.jai", "class Box<T> { T item; int size() { return 1; } }\n",
				 "class Box<T> { T item; int size() { return item.size(); } }\n", "class Box<T> { T item; int size() { return 2; } }\n",
				 "import \"part.jai\";\nint outer() { Box<int> b; return b.size() + 10; }\n", "part.jai:1:49: error: int has no member 'size'"},
				{"a generic's file that no longer parses", "part.jai", "class Box<T> { T item; int size() { return 1; } }\n",
				 "class Box<T> { T item; int size() { return 5 +; } }\n", "class Box<T> { T item; int size() { return 2; } }\n",
				 "import \"part.jai\";\nint outer() { Box<int> b; return b.size() + 10; }\n", "part.jai:1:47: error: expected expression"},
				{"the module's own text", "", "int outer() { return 11; }\n", "int outer() { return \"broken\"; }\n", "int outer() { return 12; }\n",
				 "", "outer.jai:1:15: error: cannot return 'string' from 'outer'"},
			};
			for (const closure& c : closures) {
				program p("d_" + std::to_string(&c - closures.data()));
				const std::string edited = c.part.empty() ? "outer.jai" : c.part;
				p.write("outer.jai", c.outer);
				p.write(edited, c.good);
				p.write("root.jai", "import \"outer.jai\";\nint tick() { import(\"outer.jai\"); return outer(); }\n");
				check_eq(std::string("null"), p.run_file("root.jai"), c.name);
				check_eq(std::string("11"), p.run("tick();"), c.name + ": before the edit");
				p.write(edited, c.broken);
				const std::string first = p.run("tick();");
				check_true(failed(first) && has(first, c.error), c.name + ": the first reach raises the rejection: " + first);
				for (int reach = 1; reach <= 3; ++reach) {
					check_eq(std::string("11"), p.run("tick();"), c.name + ": reach " + std::to_string(reach) + " after it runs the code the program had");
				}
				check_eq(std::string("false"), p.run("string path = \"outer\" + \".jai\"; to_string(import(path));"), c.name + ": an import of it answers false while it waits");
				p.write(edited, c.fixed);
				check_eq(std::string("12"), p.run("tick();"), c.name + ": the fix reloads");
				// another broken edit is another rejection, raised once
				p.write(edited, c.broken);
				check_true(failed(p.run("tick();")), c.name + ": a second broken edit raises again");
				check_eq(std::string("12"), p.run("tick();"), c.name + ": and waits again");
			}
		});

		test("a_generic_a_file_drops_stays_as_a_function_or_a_class_it_drops_does", [this]() {
			struct dropped {
				std::string name;
				std::string before;
				std::string use;
				std::string fresh;   // a use of it no unit made before the drop (another instance, for a generic)
			};
			const std::vector<dropped> kinds = {
				{"a plain function", "int twice(int x) { return x + x; }\n", "twice(4)", "twice(5)"},
				{"a generic function", "T twice<T>(T x) { return x + x; }\n", "twice<int>(4)", "twice<float>(2.5)"},
				{"a generic function called by deduction", "T twice<T>(T x) { return x + x; }\n", "twice(4)", "twice(\"ab\").size()"},
				{"a plain class", "class Box { int item = 3; int size() { return 8; } }\n", "Box().size()", "Box().item"},
				{"a generic class", "class Box<T> { T item; int size() { return 8; } }\n", "Box<int>().size()", "Box<string>().size()"},
				{"a generic function in a namespace", "namespace n { T twice<T>(T x) { return x + x; } }\n", "n::twice<int>(4)", "n::twice<float>(2.5)"},
			};
			for (const bool module_user : {false, true}) {
				for (const dropped& k : kinds) {
					const std::string who = k.name + (module_user ? ", used by a module" : ", used by the root file");
					program p("e_" + std::to_string(&k - kinds.data()) + (module_user ? "m" : "r"));
					p.write("lib.jai", k.before);
					if (module_user) {
						p.write("user.jai", "import \"lib.jai\";\nvar use() { return " + k.use + "; }\n");
						p.write("root.jai", "import \"user.jai\";\nvar tick() { import(\"lib.jai\", \"user.jai\"); return use(); }\n");
					} else {
						p.write("root.jai", "import \"lib.jai\";\nvar use() { return " + k.use + "; }\nvar tick() { import(\"lib.jai\"); return use(); }\n");
					}
					check_eq(std::string("null"), p.run_file("root.jai"), who);
					const std::string value = p.run("tick();");
					check_true(!failed(value), who + ": before the drop: " + value);
					p.write("lib.jai", "int other() { return 1; }\n");
					check_eq(value, p.run("tick();"), who + ": the reload that drops it is accepted and its users run as they did");
					check_eq(value, p.run("tick();"), who + ": at every reach");
					check_eq(std::string("1"), p.run("other();"), who + ": the file's new text is applied");
					check_eq(value, p.run("var later() { return " + k.use + "; }\nlater();"), who + ": a later text uses it");
					const std::string newer = p.run("var newer() { return " + k.fresh + "; }\nnewer();");
					check_true(!failed(newer), who + ": and makes of it what no unit made before: " + newer);
					if (module_user) {
						p.write("user.jai", "import \"lib.jai\";\nvar use() { return " + k.use + "; }\nint edited() { return 1; }\n");
						check_eq(value, p.run("tick();"), who + ": its user's own edit reloads");
						check_eq(std::string("1"), p.run("edited();"), who);
					}
					// the name declared again is the declaration from there on
					p.write("lib.jai", k.before + "int other() { return 2; }\n");
					check_eq(value, p.run("tick();"), who + ": declared again");
					check_eq(std::string("2"), p.run("other();"), who);
				}
			}
			// a name the file gives to something else is declared again, not dropped: what cannot be made from that rejects the reload
			struct renamed {
				std::string name;
				std::string before, after;
				std::string use;
			};
			const std::vector<renamed> renames = {
				{"a generic function declared with other parameters", "T twice<T>(T x) { return x + x; }\n", "T twice<T>(T x, T y) { return x + y; }\n", "twice<int>(4)"},
				{"a generic function's name given to a plain function of another type", "T twice<T>(T x) { return x + x; }\n", "string twice(string x) { return x; }\n", "twice<int>(4)"},
				{"a generic class's name given to a plain class", "class Box<T> { T item; int size() { return 8; } }\n", "class Box { int size() { return 9; } }\n", "Box<int>().size()"},
				{"a generic class declared with another count of parameters", "class Box<T> { T item; int size() { return 8; } }\n", "class Box<K, V> { K item; int size() { return 9; } }\n", "Box<int>().size()"},
			};
			for (const renamed& r : renames) {
				program p("e_renamed_" + std::to_string(&r - renames.data()));
				p.write("lib.jai", r.before);
				p.write("root.jai", "import \"lib.jai\";\nvar use() { return " + r.use + "; }\nvar tick() { import(\"lib.jai\"); return use(); }\n");
				check_eq(std::string("null"), p.run_file("root.jai"), r.name);
				const std::string value = p.run("tick();");
				p.write("lib.jai", r.after);
				const std::string rejected = p.run("tick();");
				check_true(failed(rejected) && has(rejected, "root.jai:2:"), r.name + ": the reload is rejected at the caller: " + rejected);
				check_eq(value, p.run("tick();"), r.name + ": and the old code runs");
			}
		});

		test("the_same_declaration_in_two_files_gets_one_verdict_whatever_it_declares", [this]() {
			// every arrangement of two files that declare the name (a answers "a", b answers "b"), a third that only calls it and a
			// root that may declare its own: the later declaration to run is the one every caller reaches
			struct arrangement {
				std::string name;
				std::string root;     // ${own}: the root's own declaration; ${call}: the call
				std::string expected;
			};
			const std::vector<arrangement> arrangements = {
				{"a then b", "import \"fa.jai\";\nimport \"fb.jai\";\n${call} + use_a() + use_b();", "bbb"},
				{"b then a", "import \"fb.jai\";\nimport \"fa.jai\";\n${call} + use_a() + use_b();", "aaa"},
				{"a, b, then a file that only calls", "import \"fa.jai\";\nimport \"fb.jai\";\nimport \"fc.jai\";\n${call} + use_a() + use_b() + use_c();", "bbbb"},
				{"the calling file first", "import \"fc.jai\";\nimport \"fa.jai\";\nimport \"fb.jai\";\n${call} + use_a() + use_b() + use_c();", "bbbb"},
				{"b, a, then the calling file", "import \"fb.jai\";\nimport \"fa.jai\";\nimport \"fc.jai\";\n${call} + use_a() + use_b() + use_c();", "aaaa"},
				{"a call between the imports", "string out = \"\";\nimport \"fa.jai\";\nout += ${call} + use_a();\nimport \"fb.jai\";\nout += \"|\" + ${call} + use_a() + use_b();\nout;", "aa|bbb"},
				{"the root's own declaration, then b", "${own}string out = ${call};\nimport \"fb.jai\";\nout += \"|\" + ${call} + use_b();\nout;", "r|bb"},
			};
			const auto fill = [](std::string text, const std::string& own, const std::string& call) {
				for (size_t at = text.find("${own}"); at != std::string::npos; at = text.find("${own}")) { text.replace(at, 6, own); }
				for (size_t at = text.find("${call}"); at != std::string::npos; at = text.find("${call}")) { text.replace(at, 7, call); }
				return text;
			};
			const std::vector<family> all = families();
			for (const family& f : all) {
				for (const arrangement& a : arrangements) {
					program p("g1_" + std::to_string(&f - all.data()) + "_" + std::to_string(&a - arrangements.data()));
					p.write("fa.jai", declared(f, "a") + "string use_a() { return " + f.call + "; }\n");
					p.write("fb.jai", declared(f, "b") + "string use_b() { return " + f.call + "; }\n");
					p.write("fc.jai", "string use_c() { return " + f.call + "; }\n");
					p.write("root.jai", fill(a.root, declared(f, "r"), f.call));
					check_eq(a.expected, p.run_file("root.jai"), f.name + ", " + a.name);
				}
			}
			// a reload that declares it again: the file that ran last is the one every caller reaches, at once
			for (const family& f : all) {
				program p("g1_reload_" + std::to_string(&f - all.data()));
				const auto file = [&](const char* letter, const char* user) { return declared(f, letter) + "string " + user + "() { return " + f.call + "; }\n"; };
				p.write("fa.jai", file("a", "use_a"));
				p.write("fb.jai", file("b", "use_b"));
				p.write("fc.jai", "string use_c() { return " + f.call + "; }\n");
				p.write("root.jai", "import \"fa.jai\";\nimport \"fb.jai\";\nimport \"fc.jai\";\nstring all() { import(\"fa.jai\", \"fb.jai\", \"fc.jai\"); return " + f.call + " + use_a() + use_b() + use_c(); }\n");
				std::string trace = p.run_file("root.jai");
				const auto step = [&](const std::string& code) { trace += " "; trace += p.run(code); };
				step("all();");
				p.write("fa.jai", file("A", "use_a"));
				step("all();");
				p.write("fb.jai", file("B", "use_b"));
				step("all();");
				p.write("fc.jai", "string use_c() { return " + f.call + " + \"!\"; }\n");
				step("all();");
				p.write("fa.jai", file("Z", "use_a"));
				step("all();");
				step(f.call + ";");
				check_eq(std::string("null bbbb AAAA BBBB BBBB! ZZZZ! Z"), trace, f.name + ": across reloads");
			}
		});

		test("generic_functions_that_differ_in_shape_stay_overloads_across_files", [this]() {
			program p("g1_shapes");
			p.write("fa.jai", "string f<T>(T x) { return \"one\"; }\nstring g<T>(array<T> xs) { return \"array\"; }\n");
			p.write("fb.jai", "string f<T>(T x, T y) { return \"two\"; }\nstring g<K, V>(map<K, V> m) { return \"map\"; }\n");
			p.write("root.jai", "import \"fa.jai\";\nimport \"fb.jai\";\nf(1) + \" \" + f(1, 2) + \" \" + g([1]) + \" \" + g({\"k\": 1});");
			check_eq(std::string("one two array map"), p.run_file("root.jai"));
			// an instance only a calling file names follows the declaration that ran last, as every other does
			program q("g1_third");
			q.write("fa.jai", "string f<T>(T x) { return \"a\"; }\n");
			q.write("fb.jai", "string f<T>(T x) { return \"b\"; }\n");
			q.write("fc.jai", "string use_c() { return f(\"text\") + f(2.5); }\n");
			q.write("root.jai", "import \"fa.jai\";\nimport \"fb.jai\";\nimport \"fc.jai\";\nstring all() { import(\"fa.jai\", \"fb.jai\", \"fc.jai\"); return use_c(); }\n");
			check_eq(std::string("null"), q.run_file("root.jai"));
			check_eq(std::string("bb"), q.run("all();"));
			q.write("fa.jai", "string f<T>(T x) { return \"A\"; }\n");
			check_eq(std::string("AA"), q.run("all();"), "the file declared again makes every instance the engine holds again");
		});

		test("a_namespaces_generic_function_collides_across_files_as_its_plain_function_does", [this]() {
			// a namespace's function another unit declares with the same parameters raises where the second declaration runs,
			// unless that declaration is written `override`: the generic gets the plain function's verdict, text included
			struct form {
				std::string name;
				std::string declare;   // %s: the letter its body answers; %o: where `override` stands
				std::string call;
			};
			const std::vector<form> forms = {
				{"a plain function", "namespace n { string f(int x)%o { return \"%s\"; } }\n", "n::f(1)"},
				{"a generic function", "namespace n { string f<T>(T x)%o { return \"%s\"; } }\n", "n::f(1)"},
				{"a generic function called with its type argument", "namespace n { string f<T>(T x)%o { return \"%s\"; } }\n", "n::f<int>(1)"},
			};
			const auto text = [](const form& f, const std::string& letter, bool overrides) {
				std::string out = f.declare;
				out.replace(out.find("%o"), 2, overrides ? " override" : "");
				out.replace(out.find("%s"), 2, letter);
				return out;
			};
			const std::string collision = "Function 'f' already exists in namespace 'n'. Use 'override' keyword to replace it.";
			for (const form& f : forms) {
				const auto files = [&](program& p, bool override_a, bool override_b) {
					p.write("fa.jai", text(f, "a", override_a) + "string use_a() { return " + f.call + "; }\n");
					p.write("fb.jai", text(f, "b", override_b) + "string use_b() { return " + f.call + "; }\n");
					p.write("fc.jai", "string use_c() { return " + f.call + "; }\n");
				};
				const std::string second = "string out = \"\";\nimport \"fa.jai\";\nout += " + f.call + " + use_a();\n"
				                           "try { import(\"fb.jai\"); out += \"|loaded\"; } catch (e) { out += \"|\" + e; }\nout += \"|\" + " + f.call + " + use_a();\nout;";
				{
					program p("n1_plain_" + std::to_string(&f - forms.data()));
					files(p, false, false);
					p.write("root.jai", second);
					check_eq("aa|" + collision + "|aa", p.run_file("root.jai"), f.name + ": the second file's declaration collides and the first stands");
					check_eq(std::string("a"), p.run(f.call + ";"), f.name + ": later text reaches the declaration that stands");
				}
				{
					program p("n1_first_" + std::to_string(&f - forms.data()));
					files(p, true, false);
					p.write("root.jai", second);
					check_eq("aa|" + collision + "|aa", p.run_file("root.jai"), f.name + ": override on the first only does not let the second in");
				}
				{
					program p("n1_second_" + std::to_string(&f - forms.data()));
					files(p, false, true);
					p.write("root.jai", "import \"fa.jai\";\nimport \"fb.jai\";\nimport \"fc.jai\";\n"
					                    "string all() { import(\"fa.jai\", \"fb.jai\", \"fc.jai\"); return " + f.call + " + use_a() + use_b() + use_c(); }\n");
					check_eq(std::string("null"), p.run_file("root.jai"), f.name);
					check_eq(std::string("bbbb"), p.run("all();"), f.name + ": override on the second replaces, for every caller");
					// a reload of either file: the overriding file replaces again, the other collides with what stands and waits
					p.write("fb.jai", text(f, "B", true) + "string use_b() { return " + f.call + "; }\n");
					check_eq(std::string("BBBB"), p.run("all();"), f.name + ": the overriding file declared again");
					p.write("fa.jai", text(f, "A", false) + "string use_a() { return " + f.call + "; }\n");
					check_eq("ERR: " + collision, p.run("all();").substr(0, 5 + collision.size()), f.name + ": the first file declared again collides with the declaration that stands");
					check_eq(std::string("BBBB"), p.run("all();"), f.name + ": and the program runs on");
				}
				{
					program p("n1_same_" + std::to_string(&f - forms.data()));
					files(p, false, false);
					p.write("root.jai", "import \"fc.jai\";\nimport \"fa.jai\";\nstring all() { import(\"fa.jai\", \"fc.jai\"); return " + f.call + " + use_a() + use_c(); }\n");
					check_eq(std::string("null"), p.run_file("root.jai"), f.name + ": a file that only calls it, imported first");
					check_eq(std::string("aaa"), p.run("all();"), f.name);
					p.write("fa.jai", text(f, "A", false) + "string use_a() { return " + f.call + "; }\n");
					check_eq(std::string("AAA"), p.run("all();"), f.name + ": the same file run again replaces its own");
				}
			}
			// a class in a namespace is replaced by the later file, a generic method with it
			for (const char* method : {"string f(int x)", "string f<T>(T x)"}) {
				program p(std::string("n1_method_") + (method[8] == '<' ? "g" : "p"));
				p.write("fa.jai", std::string("namespace n { class K { ") + method + " { return \"a\"; } } }\nstring use_a() { return n::K().f(1); }\n");
				p.write("fb.jai", std::string("namespace n { class K { ") + method + " { return \"b\"; } } }\nstring use_b() { return n::K().f(1); }\n");
				p.write("root.jai", "import \"fa.jai\";\nimport \"fb.jai\";\nn::K().f(1) + use_a() + use_b();");
				check_eq(std::string("bbb"), p.run_file("root.jai"), method);
			}
		});
	}
};

}

using reload_verdict_tests = jai::foundry::tests::reload_verdict_tests;
FOUNDRY_REGISTER(reload_verdict_tests)
