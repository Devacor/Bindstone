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
	}
};

}

using reload_verdict_tests = jai::foundry::tests::reload_verdict_tests;
FOUNDRY_REGISTER(reload_verdict_tests)
