// A typed load (docs/site/guide/06-functions.html "Type arguments", docs/site/embedding/17-host-api.html "Records") over data
// that carries archive records (docs/site/embedding/08-serialization.html): from_json<T> and jai::make_record refuse a saved
// value its field cannot take whether or not the object that holds it is written with a $type
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/stdlib/json.hpp>
#include <jaiscript/detail/engine_access.hpp>
#include <jaiscript/detail/type_value.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <string>
#include <vector>

namespace jai::foundry::tests {

class typed_load_tests : public suite {
public:
	typed_load_tests() : suite("Typed Loads") {}

	static std::string outcome(engine& eng, const std::string& code) {
		try {
			return eng.execute(code).to_string();
		} catch (const std::exception& e) {
			return std::string("ERR: ") + e.what();
		}
	}

	static std::string quoted(const std::string& text) {
		std::string out = "\"";
		for (const char c : text) {
			if (c == '"' || c == '\\') { out += '\\'; }
			out += c;
		}
		return out + "\"";
	}

	static const char* classes() {
		return "class Cell<T> { T held; } class Other { string held = \"o\"; } class Base { int b = 1; } class Derived : Base { int d = 2; }"
		       " class Outer { Cell<int> cell; Base base; array<Cell<int>> items; map<string, Cell<int>> keyed; shared_ptr<Cell<int>> h;"
		       " weak_ptr<Cell<int>> w; var any; function cb; int n = 3; bool flag = false; float f = 1.5; string s = \"d\"; }"
		       " Cell<int> ci; Cell<string> cs; Cell<Cell<int>> cc;";
	}

	static std::string tagged(const std::string& members) { return "{\"$type\":\"Outer\",\"$val\":" + members + "}"; }

	void forge_tests() override {
		test("a_tagged_record_refuses_the_member_its_untagged_form_refuses", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute(classes());
			const std::string wrong = "{\"$type\":\"Cell<string>\",\"$val\":{\"held\":\"x\"}}";
			const std::vector<std::pair<std::string, std::string>> cases = {
				{"{\"cell\":" + wrong + "}", "from_json: field 'cell' of 'Outer' is Cell<int>, not Cell<string>"},
				{"{\"cell\":{\"$type\":\"Other\",\"$val\":{\"held\":\"x\"}}}", "from_json: field 'cell' of 'Outer' is Cell<int>, not Other"},
				{"{\"items\":[" + wrong + "]}", "from_json: field 'items' of 'Outer' is array<Cell<int>>, not array"},
				{"{\"keyed\":{\"k\":" + wrong + "}}", "from_json: field 'keyed' of 'Outer' is map<string,Cell<int>>, not map"},
				{"{\"h\":{\"$id\":1,\"$type\":\"Cell<string>\",\"$val\":{\"held\":\"x\"}}}", "from_json: field 'h' of 'Outer' is shared_ptr<Cell<int>>, not Cell<string>"},
				{"{\"n\":\"x\"}", "from_json: field 'n' of 'Outer' is int, not string"},
				{"{\"s\":[1]}", "from_json: field 's' of 'Outer' is string, not array"},
				{"{\"n\":null}", "from_json: field 'n' of 'Outer' is int and cannot be null"},
				{"{\"f\":null}", "from_json: field 'f' of 'Outer' is float and cannot be null"},
				{"{\"cell\":{\"held\":\"x\"}}", "from_json: field 'held' of 'Cell<int>' is int, not string"},
			};
			for (const auto& [members, refusal] : cases) {
				check_eq("ERR: " + refusal, outcome(*e, "to_json(from_json<Outer>(" + quoted(members) + "));"), "untagged " + members);
				check_eq("ERR: " + refusal, outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged(members)) + "));"), "tagged " + members);
			}
			// the record the test was written for: an instance of a generic under an instance of the same generic
			const std::string nested = "{\"held\":" + wrong + "}";
			const std::string refusal = "ERR: from_json: field 'held' of 'Cell<Cell<int>>' is Cell<int>, not Cell<string>";
			check_eq(refusal, outcome(*e, "to_json(from_json<Cell<Cell<int>>>(" + quoted(nested) + "));"), "untagged");
			check_eq(refusal, outcome(*e, "to_json(from_json<Cell<Cell<int>>>(" + quoted("{\"$type\":\"Cell<Cell<int>>\",\"$val\":" + nested + "}") + "));"), "tagged");
		});

		test("a_tagged_record_anywhere_in_a_typed_load_refuses_a_member_that_does_not_fit", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute(classes());
			const std::string unfit = "{\"$type\":\"Cell<int>\",\"$val\":{\"held\":\"x\"}}";
			const std::string refusal = "ERR: from_json: field 'held' of 'Cell<int>' is int, not string";
			check_eq(refusal, outcome(*e, "to_json(from_json<Cell<int>>(" + quoted(unfit) + "));"), "the root");
			check_eq(refusal, outcome(*e, "to_json(from_json<array<Cell<int>>>(" + quoted("[" + unfit + "]") + "));"), "an element of the root");
			check_eq(refusal, outcome(*e, "to_json(from_json<map<string, Cell<int>>>(" + quoted("{\"k\":" + unfit + "}") + "));"), "a value of the root");
			check_eq(refusal, outcome(*e, "to_json(from_json<shared_ptr<Cell<int>>>(" + quoted("{\"$id\":1,\"$type\":\"Cell<int>\",\"$val\":{\"held\":\"x\"}}") + "));"), "a handle");
			check_eq(refusal, outcome(*e, "to_json(from_json<Outer>(" + quoted("{\"any\":" + unfit + "}") + "));"), "a var field of an untagged record");
			check_eq(refusal, outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged("{\"any\":" + unfit + "}")) + "));"), "a var field of a tagged record");
			check_eq(refusal, outcome(*e, "to_json(from_json<var>(" + quoted("{\"deep\":[{\"k\":" + unfit + "}]}") + "));"), "under plain data");
		});

		test("a_tagged_record_takes_the_member_its_untagged_form_takes", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute(classes());
			const std::vector<std::string> cases = {
				"{\"cell\":{\"held\":5}}",
				"{\"items\":[{\"held\":5},{\"$type\":\"Cell<int>\",\"$val\":{\"held\":6}}]}",
				"{\"keyed\":{\"k\":{\"held\":5}}}",
				"{\"h\":{\"held\":5}}",
				"{\"cell\":\"{\\\"held\\\":9}\"}",
				"{\"flag\":1,\"f\":2}",
				"{\"base\":{\"$type\":\"Derived\",\"$val\":{\"b\":7,\"d\":8}}}",
				"{\"cb\":null,\"h\":null,\"any\":null}",
				"{\"nobody\":1,\"n\":4}",
			};
			for (const std::string& members : cases) {
				const std::string plain = outcome(*e, "to_json(from_json<Outer>(" + quoted(members) + "));");
				check_true(plain.rfind("ERR: ", 0) != 0, "the untagged form loads: " + members + " -> " + plain);
				check_eq(plain, outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged(members)) + "));"), "tagged " + members);
			}
			// what an archive converts stays converted under a tag: a float truncates into an int field as cast<int> does
			check_eq(std::string("7"), outcome(*e, "from_json<Outer>(" + quoted(tagged("{\"n\":7.9}")) + ").n;"));
		});

		test("an_archive_record_that_names_no_class_is_refused_where_a_class_belongs", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute(classes());
			const std::string stray = "{\"$type\":\"Cel<int>\",\"$val\":{\"held\":5}}";
			check_eq(std::string("ERR: from_json: the JSON is Cel<int>, not Cell<int>"), outcome(*e, "to_json(from_json<Cell<int>>(" + quoted(stray) + "));"), "the root");
			check_eq(std::string("ERR: from_json: the JSON is Nope, not Cell<int>"),
			         outcome(*e, "to_json(from_json<Cell<int>>(" + quoted("{\"$type\":\"Nope\",\"$val\":{\"held\":5}}") + "));"), "a name nothing declares");
			check_eq(std::string("ERR: from_json: field 'cell' of 'Outer' is Cell<int>, not Cel<int>"),
			         outcome(*e, "to_json(from_json<Outer>(" + quoted("{\"cell\":" + stray + "}") + "));"), "a field of an untagged record");
			check_eq(std::string("ERR: from_json: field 'cell' of 'Outer' is Cell<int>, not Cel<int>"),
			         outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged("{\"cell\":" + stray + "}")) + "));"), "a field of a tagged record");
			check_eq(std::string("ERR: from_json: field 'h' of 'Outer' is shared_ptr<Cell<int>>, not Cel<int>"),
			         outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged("{\"h\":" + stray + "}")) + "));"), "a handle field");
			check_eq(std::string("ERR: from_json: field 'items' of 'Outer' is array<Cell<int>>, not array"),
			         outcome(*e, "to_json(from_json<Outer>(" + quoted(tagged("{\"items\":[" + stray + "]}")) + "));"), "an element");
			// where no class belongs the record stays the data it is
			check_eq(std::string("Cel<int>"), outcome(*e, "from_json<Outer>(" + quoted(tagged("{\"any\":" + stray + "}")) + ").any[\"$type\"];"), "a var field");
			check_eq(std::string("Cel<int>"), outcome(*e, "from_json<map<string, var>>(" + quoted(stray) + ")[\"$type\"];"), "a map");
		});

		test("an_untyped_load_converts_or_keeps_the_constructed_value_and_records_it", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute(classes());
			e->hot_reload_reporting(true);
			const std::string members = "{\"cell\":{\"$type\":\"Cell<string>\",\"$val\":{\"held\":\"x\"}},\"n\":\"x\",\"f\":2}";
			check_eq(std::string("0|3|2.0"), outcome(*e, "var o = from_json(" + quoted(tagged(members)) + "); to_string(o.cell.held) + \"|\" + to_string(o.n) + \"|\" + to_string(o.f);"));
			std::string rows;
			for (const auto& row : e->hot_reload_reports()) {
				if (row.outcome == field_migration_outcome::reset) { rows += row.class_name + "." + row.field_name + ";"; }
			}
			check_eq(std::string("Outer.cell;Outer.n;"), rows, "the values the load did not keep");
		});

		test("a_load_inside_a_typed_load_follows_its_own_rule", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute("class Inner { int n = 3; } class Config { int level = 1; }"
			           " class Holder { int seen = 0; Inner inner; Holder() { var c = from_json(\"{\\\"$type\\\":\\\"Config\\\",\\\"$val\\\":{\\\"level\\\":\\\"high\\\"}}\"); seen = c.level; } }"
			           " class Strict { int seen = 0; Strict() { try { from_json<Config>(\"{\\\"$type\\\":\\\"Config\\\",\\\"$val\\\":{\\\"level\\\":\\\"high\\\"}}\"); } catch (e) { seen = 1; } } }");
			check_eq(std::string("1|8"), outcome(*e, "auto h = from_json<Holder>(\"{\\\"$type\\\":\\\"Holder\\\",\\\"$val\\\":{\\\"inner\\\":{\\\"n\\\":8}}}\"); to_string(h.seen) + \"|\" + to_string(h.inner.n);"),
			         "an untyped load a constructor runs during a typed load keeps the archive's rule");
			check_eq(std::string("ERR: from_json: field 'n' of 'Inner' is int, not string"),
			         outcome(*e, "to_json(from_json<Holder>(\"{\\\"$type\\\":\\\"Holder\\\",\\\"$val\\\":{\\\"inner\\\":{\\\"$type\\\":\\\"Inner\\\",\\\"$val\\\":{\\\"n\\\":\\\"x\\\"}}}}\"));"),
			         "and the typed load's rule holds again after it");
			check_eq(std::string("1|3"), outcome(*e, "var s = from_json(\"{\\\"$type\\\":\\\"Strict\\\",\\\"$val\\\":{\\\"seen\\\":\\\"x\\\"}}\"); var i = from_json(\"{\\\"$type\\\":\\\"Inner\\\",\\\"$val\\\":{\\\"n\\\":\\\"x\\\"}}\"); to_string(s.seen) + \"|\" + to_string(i.n);"),
			         "a typed load a constructor runs during an untyped load does not make the untyped one strict");
		});

		test("a_record_a_host_builds_refuses_the_same_members", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			engine* raw = e.get();
			e->add_function("row", [raw](const script_type& type, const std::string& column) {
				auto made = make_record(*raw, type, record_field_list{{"cell", script_value(column, raw)}}, "row");
				if (!made) { throw std::runtime_error(raw->format_error(made)); }
				return std::move(made.value());
			}, returns<type_arg>);
			e->execute("class Cell<T> { T held; } class Outer { Cell<Cell<int>> cell; } Cell<int> ci; Cell<string> cs; Cell<Cell<int>> cc;");
			const std::string wrong = "{\"$type\":\"Cell<Cell<int>>\",\"$val\":{\"held\":{\"$type\":\"Cell<string>\",\"$val\":{\"held\":\"x\"}}}}";
			check_eq(std::string("ERR: row: field 'held' of 'Cell<Cell<int>>' is Cell<int>, not Cell<string>"), outcome(*e, "to_json(row<Outer>(" + quoted(wrong) + "));"));
			check_eq(std::string("4"), outcome(*e, "row<Outer>(" + quoted("{\"$type\":\"Cell<Cell<int>>\",\"$val\":{\"held\":{\"held\":4}}}") + ").cell.held.held;"));
			check_eq(std::string("ERR: row: text for 'Cell<Cell<int>>' is not JSON: Expected '\"' in JSON"), outcome(*e, "to_json(row<Outer>(\"{\"));"), "text that is no JSON keeps its own error");
		});

		test("a_binary_archive_under_a_typed_load_refuses_the_same_members", [this]() {
			// what another version of the program saved: each class held a Cell<string> there
			auto writer = make_engine();
			stdlib::register_all(*writer);
			writer->execute("class Cell<T> { T held; } class Field { Cell<string> cell; } class Items { array<Cell<string>> items; } class Keyed { map<string, Cell<string>> keyed; }"
			                " class Handle { shared_ptr<Cell<string>> h; } class Fits { Cell<int> cell; }"
			                " Field field; field.cell.held = \"x\"; Items items; items.items.push(Cell<string>()); Keyed keyed; keyed.keyed[\"k\"] = Cell<string>();"
			                " Handle handle; handle.h = new Cell<string>(); Fits fits; fits.cell.held = 6;");
			auto e = make_engine();
			stdlib::register_all(*e);
			std::string payload;
			e->add_function("payload", [&payload]() { return payload; });
			e->execute("class Cell<T> { T held; } class Field { Cell<int> cell; } class Items { array<Cell<int>> items; } class Keyed { map<string, Cell<int>> keyed; }"
			           " class Handle { shared_ptr<Cell<int>> h; } class Fits { Cell<int> cell; } Cell<string> cs;");
			// from_binary<T> through the script for the typed load, from_binary for the archive's own rule
			const auto load = [&](const char* name, const char* cls, std::string_view who) -> std::string {
				payload = writer->execute(std::string("to_binary(") + name + ");").as_string();
				return outcome(*e, who.empty() ? std::string("from_binary(payload()); \"loaded\";") : "from_binary<" + std::string(cls) + ">(payload()); \"loaded\";");
			};
			check_eq(std::string("ERR: from_binary: field 'cell' of 'Field' is Cell<int>, not Cell<string>"), load("field", "Field", "from_binary"));
			check_eq(std::string("ERR: from_binary: field 'items' of 'Items' is array<Cell<int>>, not array"), load("items", "Items", "from_binary"));
			check_eq(std::string("ERR: from_binary: field 'keyed' of 'Keyed' is map<string,Cell<int>>, not map"), load("keyed", "Keyed", "from_binary"));
			check_eq(std::string("ERR: from_binary: field 'h' of 'Handle' is shared_ptr<Cell<int>>, not Cell<string>"), load("handle", "Handle", "from_binary"));
			check_eq(std::string("loaded"), load("fits", "Fits", "from_binary"), "a record whose members fit");
			check_eq(std::string("loaded"), load("field", "Field", {}), "the archive's own rule keeps the constructed value");
		});
	}
};

}

using typed_load_tests = jai::foundry::tests::typed_load_tests;
FOUNDRY_REGISTER(typed_load_tests)
