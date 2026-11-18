// Type arguments (docs/site/guide/06-functions.html "Type arguments") and records (docs/site/embedding/17-host-api.html
// "Records"): callee<T>(args) passes T as a reflect::type value only a host takes, a host registration types the result in
// terms of T, and jai::make_record / record_fields build and read script class instances from named values
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

namespace jai::foundry::tests {

namespace type_argument_probe {
	struct store {
		std::string last;
	};
}

class type_argument_tests : public suite {
public:
	type_argument_tests() : suite("Type Arguments") {}

	static std::string failure(engine& eng, const std::string& code) {
		try {
			eng.execute(code);
		} catch (const std::exception& e) {
			return e.what();
		}
		return "(no error)";
	}

	static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

	// the host surface the tests call: a free function, a typed-result family and a bound class
	static void bind_probe(engine& eng) {
		stdlib::register_all(eng);
		engine* e = &eng;
		eng.add_function("type_name_of", [](const script_type& t) { return t.name(); });
		eng.add_function("type_and_value", [](const script_type& t, script_int n) { return t.name() + ":" + std::to_string(n); });
		eng.add_function("two_types", [](const script_type& a, const script_type& b) { return a.name() + "," + b.name(); });
		eng.add_function("class_of", [](const script_type& t) { return t.class_name(); });
		eng.add_function("one_record", [e](const script_type& t, const std::string& handle) {
			auto made = make_record(*e, t, record_field_list{{"handle", script_value(handle, e)}}, "one_record");
			if (!made) { throw std::runtime_error(e->format_error(made)); }
			return std::move(made.value());
		}, returns_type_argument);
		eng.add_function("some_records", [e](const script_type& t) {
			script_value rows = script_value::make_array(t.info(), e);
			for (const char* handle : {"rex", "kit"}) {
				auto made = make_record(*e, t, record_field_list{{"handle", script_value(std::string(handle), e)}}, "some_records");
				if (!made) { throw std::runtime_error(e->format_error(made)); }
				rows.unchecked_get_array_storage()->push(std::move(made.value()));
			}
			return rows;
		}, returns_array_of_type_argument);
		eng.add_function("first_record", [e](const script_type& t) {
			auto made = make_shared_record(*e, t, record_field_list{{"handle", script_value(std::string("rex"), e)}}, "first_record");
			if (!made) { throw std::runtime_error(e->format_error(made)); }
			return std::move(made.value());
		}, returns_shared_ptr_of_type_argument);
		dynamic_binder<type_argument_probe::store>(eng, "ProbeStore")
			.constructor<>()
			.method("fetch", [e](type_argument_probe::store& self, const script_type& t, const std::string& sql) {
				self.last = t.name() + " " + sql;
				return e->constant_argument_admits("ProbeStore.fetch", 0, sql) ? self.last : std::string("refused");
			})
			.method("last", [](const type_argument_probe::store& self) { return self.last; })
			.method("rows", [e](type_argument_probe::store&, const script_type& t) {
				script_value rows = script_value::make_array(t.info(), e);
				auto made = make_record(*e, t, record_field_list{{"handle", script_value(std::string("kit"), e)}}, "ProbeStore.rows");
				if (!made) { throw std::runtime_error(e->format_error(made)); }
				rows.unchecked_get_array_storage()->push(std::move(made.value()));
				return rows;
			}, returns_array_of_type_argument)
			.build();
		eng.constant_argument("ProbeStore.fetch", 0, "a constant, please");
	}

	void forge_tests() override {
		test("a_type_argument_reaches_a_host_function_as_a_script_type", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			e->execute("class User { int id; } namespace shop { class Product { string title; } }");
			check_eq(std::string("User"), e->execute("type_name_of<User>()").as_string());
			check_eq(std::string("array<User>"), e->execute("type_name_of<array<User>>()").as_string());
			check_eq(std::string("shared_ptr<shop::Product>"), e->execute("type_name_of<shared_ptr<shop::Product>>()").as_string());
			check_eq(std::string("map<string,array<int>>"), e->execute("type_name_of<map<string, array<int>>>()").as_string());
			check_eq(std::string("int:4"), e->execute("type_and_value<int>(4)").as_string());
			check_eq(std::string("User,float"), e->execute("two_types<User, float>()").as_string());
			check_eq(std::string("shop::Product"), e->execute("class_of<shared_ptr<shop::Product>>()").as_string());
			check_eq(std::string(""), e->execute("class_of<array<User>>()").as_string());
		});

		test("the_type_value_is_reflect_type_with_name_and_to_string", [this]() {
			auto e = make_engine();
			e->add_function("pass_type", [](const script_value& v) { return v; });
			check_eq(std::string("array<int> array<int>"), e->execute("var t = pass_type<array<int>>(); t.name() + \" \" + t.to_string();").as_string());
		});

		test("only_a_less_than_that_closes_before_a_paren_opens_type_arguments", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			check_true(e->execute("int a = 1; int b = 2; a < b;").as_bool());
			check_true(e->execute("int a = 1; int b = 2; int c = 0; a < b && b > (c);").as_bool());
			check_false(e->execute("int a = 1; int b = 2; int c = 3; a < b >> (c);").as_bool());
			check_eq(std::string("array<array<int>>"), e->execute("type_name_of<array<array<int>>>()").as_string());
		});

		test("a_script_callee_takes_no_type_arguments", [this]() {
			auto e = make_engine();
			for (const char* code : {"int f(int x) { return x; } f<int>(1);",
			                         "class C { int m() { return 1; } } C c; c.m<int>();",
			                         "class C { } C<int>();",
			                         "void g() { auto l = [](int x) { return x; }; l<int>(2); }"}) {
				check_true(contains(failure(*e, code), "takes no type arguments: a type argument goes to a host function"), code);
			}
		});

		test("an_unknown_type_argument_is_the_declarations_error", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			check_true(contains(failure(*e, "type_name_of<Nope>();"), "unknown type 'Nope'"));
		});

		test("a_registration_types_the_result_in_terms_of_the_type_argument", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			e->execute("class User { int id = 3; string handle; }");
			check_eq(std::string("rex3"), e->execute("auto u = one_record<User>(\"rex\"); u.handle + to_string(u.id);").as_string());
			check_eq(std::string("kit"), e->execute("auto all = some_records<User>(); all[1].handle;").as_string());
			check_eq(std::string("rex"), e->execute("auto one = first_record<User>(); one->handle;").as_string());
			check_eq(std::string("rex"), e->execute("shared_ptr<User> h = first_record<User>(); h->handle;").as_string());
			check_eq(2, static_cast<int>(e->type_argument_result("ProbeStore.rows")));
			check_eq(3, static_cast<int>(e->type_argument_result("first_record")));
			check_eq(0, static_cast<int>(e->type_argument_result("type_name_of")));
			check_eq(std::string("kit"), e->execute("ProbeStore s = ProbeStore(); auto rows = s.rows<User>(); rows[0].handle;").as_string());
			for (const char* code : {"auto u = one_record<Member>(\"rex\"); u.Handel;", "auto all = some_records<Member>(); all[0].Handel;",
			                         "auto one = first_record<Member>(); one->Handel;", "ProbeStore s = ProbeStore(); auto rows = s.rows<Member>(); rows[0].Handel;"}) {
				const std::string unit = std::string("class Member { string handle; } ") + code;
				check_true(contains(e->check(unit).format(), "no member 'Handel' in class 'Member'"), code);
			}
		});

		test("a_constant_argument_index_counts_the_value_arguments", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			e->execute("class User { int id; }");
			check_eq(std::string("User SELECT 1"), e->execute("ProbeStore s = ProbeStore(); s.fetch<User>(\"SELECT 1\");").as_string());
			check_true(contains(e->check("ProbeStore s = ProbeStore(); string t = \"x\"; s.fetch<User>(t);").format(),
				"argument 1 of 'fetch' is not a constant: a constant, please"));
		});

		test("type_arguments_survive_a_jaibite_round_trip", [this]() {
			auto e = make_engine();
			bind_probe(*e);
			jai::jaibite bite = e->jaibite("class User { string handle; } auto all = some_records<User>(); type_name_of<array<User>>() + all[1].handle;");
			const std::vector<uint8_t> bytes = bite.save_bytes();
			auto loaded = make_engine();
			bind_probe(*loaded);
			jai::jaibite again = loaded->jaibite_load_bytes(bytes);
			check_eq(std::string("array<User>kit"), std::string(loaded->execute(again).as_string()));
		});

		test("make_record_builds_by_the_store_rules_and_widens_what_a_row_holds", [this]() {
			auto e = make_engine();
			bind_records(*e);
			e->execute("class Item { string name; int qty = 1; } "
			           "class Base { int id; } "
			           "class Cart : Base { bool open; float total; array<Item> items; map<string, int> notes; Item best; shared_ptr<Item> spare; "
			           "string kept = \"init\"; }");
			e->execute("Cart c = record_from<Cart>({\"id\": 7, \"open\": 1, \"total\": 3, \"items\": \"[{\\\"name\\\": \\\"pin\\\"}, {\\\"name\\\": \\\"hat\\\", \\\"qty\\\": 2}]\", "
			           "\"notes\": \"{\\\"a\\\": 1}\", \"best\": {\"name\": \"mask\"}, \"spare\": {\"name\": \"tail\"}, \"unknown\": 5});");
			check_eq(std::string("7 true 3.0 2 hat 2 1 1 mask tail init"), e->execute(
				"to_string(c.id) + \" \" + to_string(c.open) + \" \" + to_string(c.total) + \" \" + to_string(c.items.size()) + \" \" + c.items[1].name + "
				"\" \" + to_string(c.items[1].qty) + \" \" + to_string(c.items[0].qty) + \" \" + to_string(c.notes[\"a\"]) + \" \" + c.best.name + \" \" + "
				"c.spare->name + \" \" + c.kept;").as_string());
			check_eq(std::string("rex"), e->execute("auto h = shared_record_from<Item>({\"name\": \"rex\"}); h->name;").as_string());
		});

		test("make_record_refuses_with_the_class_the_field_and_both_types", [this]() {
			auto e = make_engine();
			bind_records(*e);
			e->execute("class User { int id; string handle; } class Needs { int id; Needs(int i) { id = i; } }");
			check_eq(std::string("record_from: field 'id' of 'User' is int, not string"), failure(*e, "record_from<User>({\"id\": \"one\"});"));
			check_eq(std::string("record_from: field 'id' of 'User' is int and cannot be null"), failure(*e, "record_from<User>({\"id\": null});"));
			check_eq(std::string("record_from: field 'id' of 'User' is int, not float"), failure(*e, "record_from<User>({\"id\": 1.5});"));
			check_eq(std::string("record_from: 'Needs' has no constructor taking no arguments"), failure(*e, "record_from<Needs>({\"id\": 1});"));
			check_eq(std::string("record_from: a record is an object of a script class, not array<User>"), failure(*e, "record_from<array<User>>({});"));
		});

		test("record_fields_reads_bases_first_and_stores_containers_as_json", [this]() {
			auto e = make_engine();
			bind_records(*e);
			e->execute("class Base { int id = 4; } class Tagged : Base { string name = \"rex\"; array<string> tags = [\"a\"]; bool on = true; }");
			check_eq(std::string("id=4;name=rex;tags=[\"a\"];on=true;"), e->execute("fields_text(Tagged());").as_string());
			check_eq(std::string("a=1;b=[1,2];"), e->execute("fields_text({\"b\": [1, 2], \"a\": 1});").as_string());
			check_eq(std::string("fields_text: a record is an object of a script class or a map, not int"), failure(*e, "fields_text(3);"));
		});

		test("from_json_with_a_type_argument_builds_that_type", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execute("class Item { string name; int qty = 1; } class Order { int id; array<Item> items; shared_ptr<Item> first; }");
			check_eq(std::string("2 hat 1 pin"), e->execute("Order o = from_json<Order>(\"{\\\"id\\\": 2, \\\"items\\\": [{\\\"name\\\": \\\"hat\\\"}], "
				"\\\"first\\\": {\\\"name\\\": \\\"pin\\\"}}\"); to_string(o.id) + \" \" + o.items[0].name + \" \" + to_string(o.items[0].qty) + \" \" + o.first->name;").as_string());
			check_eq(std::string("b"), e->execute("auto all = from_json<array<Item>>(\"[{\\\"name\\\": \\\"a\\\"}, {\\\"name\\\": \\\"b\\\"}]\"); all[1].name;").as_string());
			check_eq(script_int(6), e->execute("from_json<int>(\"5\") + 1;").as_int());
			check_eq(std::string("from_json: the JSON is map<string,var>, not array<int>"), failure(*e, "from_json<array<int>>(\"{}\");"));
			check_true(contains(e->check("class Receipt { int id; } auto r = from_json<Receipt>(\"{}\"); r.Idd;").format(), "no member 'Idd' in class 'Receipt'"));
			check_eq(std::string("x"), e->execute("var plain = from_json(\"{\\\"a\\\": \\\"x\\\"}\"); plain[\"a\"];").as_string());
		});
	}

	static void bind_records(engine& eng) {
		stdlib::register_all(eng);
		engine* e = &eng;
		eng.add_function("record_from", [e](const script_type& t, const script_value& fields) {
			auto made = make_record(*e, t, fields, "record_from");
			if (!made) { throw std::runtime_error(e->format_error(made)); }
			return std::move(made.value());
		}, returns_type_argument);
		eng.add_function("shared_record_from", [e](const script_type& t, const script_value& fields) {
			record_field_list list;
			for (const auto& [key, value] : fields.deref().as_map()) { list.emplace_back(key.as_string(), value); }
			auto made = make_shared_record(*e, t, list, "shared_record_from");
			if (!made) { throw std::runtime_error(e->format_error(made)); }
			return std::move(made.value());
		}, returns_shared_ptr_of_type_argument);
		eng.add_function("fields_text", [e](const script_value& record) {
			auto fields = record_fields(*e, record, "fields_text");
			if (!fields) { throw std::runtime_error(e->format_error(fields)); }
			std::string out;
			for (const auto& [name, value] : fields.value()) { out += name + "=" + value.to_string() + ";"; }
			return out;
		});
	}
};

} // namespace jai::foundry::tests

using type_argument_tests = jai::foundry::tests::type_argument_tests;
FOUNDRY_REGISTER(type_argument_tests)
