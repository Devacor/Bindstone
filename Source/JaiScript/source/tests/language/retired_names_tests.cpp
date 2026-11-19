// Library functions with a new spelling: the new name works, the old one reports what to write, and a script's or a host's
// own declaration of the old name keeps working (docs/site/reference/errors.html "retired-names").

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/detail/retired_names.hpp>
#include "../../../tools/jaiscript/file_io.hpp"

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

std::shared_ptr<engine> library_engine() {
	auto e = make_engine();
	stdlib::register_all(*e);
	jai_runner::register_file_io(*e);
	return e;
}

std::string run_output(const std::shared_ptr<engine>& e, const std::string& source) {
	auto out = std::make_shared<std::ostringstream>();
	e->set_output_stream(out);
	e->execute(source);
	return out->str();
}

std::string run_output(const std::string& source) { return run_output(library_engine(), source); }

std::string load_errors(const std::shared_ptr<engine>& e, const std::string& source) {
	const auto result = e->check(source);
	return result.has_errors() ? result.format(source) : std::string();
}

std::string load_errors(const std::string& source) { return load_errors(library_engine(), source); }

std::string script_text(const std::filesystem::path& path) { return "\"" + path.generic_string() + "\""; }

} // namespace

class retired_names_tests : public suite {
public:
	retired_names_tests() : suite("Retired Names") {}

	void forge_tests() override {
		test("every_retired_name_says_what_to_write", [this]() {
			for (const detail::retired_name& retired : detail::k_retired_names) {
				const std::string source = "var r = " + std::string(retired.name) + "();";
				const std::string errors = load_errors(source);
				check_true(errors.find(retired.text) != std::string::npos, source + " -> " + errors);
			}
		});

		test("file_names_report_their_replacements", [this]() {
			const std::pair<const char*, const char*> samples[] = {
				{"file_data(\"p\");", "JaiScript has no 'file_data': write file::data(path)"},
				{"read_file(\"p\");", "JaiScript has no 'read_file': write file::read(path)"},
				{"write_file(\"p\", \"t\");", "JaiScript has no 'write_file': write file::write(path, text)"},
				{"file_exists(\"p\");", "JaiScript has no 'file_exists': write file::exists(path)"},
				{"delete_file(\"p\");", "JaiScript has no 'delete_file': write file::remove(path)"},
				{"move_file(\"a\", \"b\");", "JaiScript has no 'move_file': write file::move(from, to)"},
				{"dir_exists(\"p\");", "JaiScript has no 'dir_exists': write file::directory_exists(path)"},
				{"make_dirs(\"p\");", "JaiScript has no 'make_dirs': write file::make_directories(path)"},
				{"list_dir(\"p\");", "JaiScript has no 'list_dir': write file::list_directory(path)"},
				{"writable_dir(\"p\");", "JaiScript has no 'writable_dir': write file::ensure_writable_directory(path)"},
			};
			for (const auto& [source, text] : samples) {
				const std::string errors = load_errors(source);
				check_true(errors.find(text) != std::string::npos, std::string(source) + " -> " + errors);
			}
		});

		test("file_namespace_does_what_the_old_functions_did", [this]() {
			const auto dir = test_temp_directory() / "retired_file_namespace";
			std::filesystem::remove_all(dir);
			const std::string root = script_text(dir);
			check_eq(std::string("true true true\n"), run_output(
				"string root = " + root + ";\n"
				"print(file::ensure_writable_directory(root), \" \", file::make_directories(root + \"/sub\"), \" \", file::directory_exists(root + \"/sub\"));\n"));
			check_eq(std::string("true hello true 5 false\n"), run_output(
				"string a = " + root + " + \"/a.txt\";\n"
				"print(file::write(a, \"hello\"), \" \", file::read(a), \" \", file::exists(a), \" \", file::data(a)[\"size\"], \" \", file::data(a)[\"is_directory\"]);\n"));
			check_eq(std::string("true false true [\"b.txt\", \"sub\"]\n"), run_output(
				"string root = " + root + ";\n"
				"print(file::move(root + \"/a.txt\", root + \"/b.txt\"), \" \", file::exists(root + \"/a.txt\"), \" \", file::exists(root + \"/b.txt\"), \" \", file::list_directory(root));\n"));
			check_eq(std::string("true false true\n"), run_output(
				"string b = " + root + " + \"/b.txt\";\n"
				"print(file::remove(b), \" \", file::exists(b), \" \", file::data(b) == null);\n"));
			std::filesystem::remove_all(dir);
		});

		test("file_functions_keep_the_filesystem_permission", [this]() {
			auto e = library_engine();
			e->disable(permissions::filesystem);
			for (const char* call : {"file::data(\"p\")", "file::read(\"p\")", "file::write(\"p\", \"t\")", "file::exists(\"p\")", "file::remove(\"p\")",
			                         "file::move(\"a\", \"b\")", "file::directory_exists(\"p\")", "file::make_directories(\"p\")",
			                         "file::list_directory(\"p\")", "file::ensure_writable_directory(\"p\")"}) {
				const std::string caught = e->execute("var m = \"\"; try { " + std::string(call) + "; } catch (error) { m = error; } m;").to_string();
				check_eq(std::string("Insufficient permission: filesystem."), caught, call);
			}
		});

		test("file_calls_are_checked_like_the_functions_they_replace", [this]() {
			check_true(!load_errors("file::read(1);").empty(), "a number is no path");
			check_true(!load_errors("file::read();").empty(), "a path is required");
			check_true(!load_errors("int n = file::read(\"p\");").empty(), "file::read gives a string");
			check_true(load_errors("string s = file::read(\"p\"); bool b = file::exists(\"p\");").empty(), "declared results");
		});

		test("a_script_may_name_its_own_variable_file", [this]() {
			const auto dir = test_temp_directory() / "retired_file_variable";
			std::filesystem::create_directories(dir);
			const std::string path = script_text(dir / "note.txt");
			check_eq(std::string("true\nnote\nnote\nnote\nnote\nnote\n"), run_output(
				"string file = " + path + ";\n"
				"print(file::write(file, \"note\"));\n"
				"print(file::read(file));\n"
				"string by_parameter(string file) { return file::read(file); }\n"
				"print(by_parameter(file));\n"
				"for (auto file : [" + path + "]) { print(file::read(file)); }\n"
				"auto by_lambda = [](string file) { return file::read(file); };\n"
				"print(by_lambda(file));\n"
				"class Holder { string file = " + path + "; string read() { return file::read(file); } }\n"
				"print(Holder().read());\n"));
			std::filesystem::remove_all(dir);
		});

		test("a_declaration_of_an_old_name_keeps_working", [this]() {
			check_eq(std::string("mine:p\n"), run_output("string read_file(string p) { return \"mine:\" + p; }\nprint(read_file(\"p\"));\n"));
			check_eq(std::string("held:p\n"), run_output("var read_file = [](string p) { return \"held:\" + p; };\nprint(read_file(\"p\"));\n"));
			check_eq(std::string("arg:p\n"), run_output("string call(var list_dir) { return list_dir(\"p\"); }\nprint(call([](string p) { return \"arg:\" + p; }));\n"));
			check_eq(std::string("method:p\n"), run_output("class C { string dir_exists(string p) { return \"method:\" + p; } string go() { return dir_exists(\"p\"); } }\nprint(C().go());\n"));
			check_eq(std::string("space:p\n"), run_output("namespace n { string make_dirs(string p) { return \"space:\" + p; } string go() { return make_dirs(\"p\"); } }\nprint(n::go());\n"));
			auto e = library_engine();
			e->add_function("delete_file", [](const std::string& path) -> std::string { return "host:" + path; });
			check_eq(std::string("host:p\n"), run_output(e, "print(delete_file(\"p\"));\n"));
		});

		test("archive_base64_is_two_calls", [this]() {
			check_true(load_errors("var t = to_base64([1, 2]);").find("JaiScript has no 'to_base64': write base64_encode(to_binary(value))") != std::string::npos, "to_base64");
			check_true(load_errors("var v = from_base64(\"AA==\");").find("JaiScript has no 'from_base64': write from_binary(base64_decode(text))") != std::string::npos, "from_base64");
			check_eq(std::string("true {\"a\": [1, 2]} true\n"), run_output(
				"map<string, array<int>> m = {\"a\": [1, 2]};\n"
				"string text = base64_encode(to_binary(m));\n"
				"var back = from_binary(base64_decode(text));\n"
				"print(base64_decode(text) == to_binary(m), \" \", back, \" \", back == m);\n"));
			check_eq(std::string("mine [1]\n"), run_output("string to_base64(var v) { return \"mine \" + to_string(v); }\nprint(to_base64([1]));\n"));
		});

		test("the_engine_clock_has_one_name", [this]() {
			const std::string errors = load_errors("float t = time::now();");
			check_true(errors.find("JaiScript has no 'time::now': write time::engine_elapsed()") != std::string::npos, errors);
			check_eq(std::string("true\n"), run_output("float a = time::engine_elapsed(); float b = time::engine_elapsed(); print(b >= a && a >= 0.0);\n"));
			check_eq(std::string("mine\n"), run_output("namespace time { string now() { return \"mine\"; } }\nprint(time::now());\n"));
		});

		test("untrusted_templates_name_what_they_render", [this]() {
			const std::pair<const char*, const char*> samples[] = {
				{"var s = scoped_template(\"card.spider\", {});", "JaiScript has no 'scoped_template': write untrusted_template(path, map)"},
				{"var s = scoped_template_text(\"x\", {});", "JaiScript has no 'scoped_template_text': write untrusted_template_text(text, map)"},
				{"var d = reflect::scoped_template_dirs();", "JaiScript has no 'reflect::scoped_template_dirs': write reflect::untrusted_template_dirs()"},
				{"var n = reflect::scoped_template_output_limit();", "JaiScript has no 'reflect::scoped_template_output_limit': write reflect::untrusted_template_output_limit()"},
				{"var p = jai::permissions::scoped_template;", "JaiScript has no 'jai::permissions::scoped_template': write jai::permissions::untrusted_template"},
			};
			for (const auto& [source, text] : samples) {
				const std::string errors = load_errors(source);
				check_true(errors.find(text) != std::string::npos, std::string(source) + " -> " + errors);
			}
			check_eq(std::string("Hi Ada true true\n"), run_output(R"(
				print(untrusted_template_text("Hi ${args[\"name\"]}", {"name": "Ada"}), " ",
				      jai::permissions::is_enabled(jai::permissions::untrusted_template), " ", reflect::untrusted_template_output_limit() > 0);
			)"));
			check_true(jai::permissions::untrusted_template != jai::permissions::untrusted, "two permissions");
			auto e = library_engine();
			e->disable(permissions::untrusted_template);
			check_eq(std::string("Insufficient permission: untrusted_template."),
				e->execute("var m = \"\"; try { untrusted_template_text(\"x\", {}); } catch (error) { m = error; } m;").to_string());
			check_eq(std::string("mine x\n"), run_output("string scoped_template_text(string t, var m) { return \"mine \" + t; }\nprint(scoped_template_text(\"x\", {}));\n"));
		});

		test("constant_time_comparison_says_so", [this]() {
			const std::string errors = load_errors("bool same = crypto::equals(\"a\", \"a\");");
			check_true(errors.find("JaiScript has no 'crypto::equals': write crypto::constant_time_equals(a, b)") != std::string::npos, errors);
			check_eq(std::string("true false false\n"), run_output(
				"print(crypto::constant_time_equals(\"abc\", \"abc\"), \" \", crypto::constant_time_equals(\"abc\", \"abd\"), \" \", crypto::constant_time_equals(\"ab\", \"abc\"));\n"));
			check_true(load_errors("crypto::constant_time_equals(\"a\");").find("'crypto::constant_time_equals' expects 2 argument(s), got 1") != std::string::npos, "arity");
		});

		test("identity_is_same_object", [this]() {
			const char* text = "JaiScript has no 'same_as' method: write same_object(a, b)";
			for (const char* source : {
				"class C { }\nshared_ptr<C> a = new C();\nbool b = a.same_as(a);",
				"class C { }\nC a = C();\nbool b = a.same_as(a);",
				"bool b = 5.same_as(5);",
				"array<int> xs = [1];\nbool b = xs.same_as(xs);",
				"string s = \"x\";\nbool b = s.same_as(s);",
			}) {
				const std::string errors = load_errors(source);
				check_true(errors.find(text) != std::string::npos, std::string(source) + " -> " + errors);
			}
			check_eq(std::string("true false true true\n"), run_output(R"(
				class C { }
				shared_ptr<C> a = new C();
				shared_ptr<C> other = new C();
				weak_ptr<C> w = a;
				print(same_object(a, a), " ", same_object(a, other), " ", same_object(a, w), " ", same_object(w, a));
			)"));
			check_eq(std::string("mine mine\n"), run_output(R"(
				class C { string same_as(var other) { return "mine"; } }
				shared_ptr<C> a = new C();
				C v = C();
				print(a.same_as(a), " ", v.same_as(v));
			)"));
			std::string raised;
			try { library_engine()->execute("[[warn_mode]];\nclass C { }\nvar a = new C();\na.same_as(a);"); } catch (const std::exception& error) { raised = error.what(); }
			check_true(raised.find("has no member 'same_as': write same_object(a, b)") != std::string::npos, "a var receiver raises at run time with the advice: " + raised);
		});

		test("an_engine_without_the_replacement_reports_the_plain_undeclared_name", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			const std::string missing = load_errors(e, "read_file(\"p\");");
			check_true(missing.find("use of undeclared identifier 'read_file'") != std::string::npos, missing);
			const std::string present = load_errors(e, "file_data(\"p\");");
			check_true(present.find("write file::data(path)") != std::string::npos, present);
		});
	}
};

}   // namespace jai::foundry::tests

using retired_names_tests = jai::foundry::tests::retired_names_tests;
FOUNDRY_REGISTER(retired_names_tests)
