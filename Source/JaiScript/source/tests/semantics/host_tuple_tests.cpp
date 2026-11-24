#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace jai::foundry::tests {

namespace host_tuple_fixtures {

struct Ledger {
	std::pair<std::string, int64_t> best {"none", 0};
	std::pair<std::string, int64_t> top() const { return best; }
	void record(const std::pair<std::string, int64_t>& a_entry) {
		if (a_entry.second > best.second) {
			best = a_entry;
		}
	}
};

}

class host_tuple_tests : public suite {
public:
	host_tuple_tests() : suite("Host Tuples") {}

	static std::shared_ptr<engine> make_tuple_engine() {
		auto result = make_engine();
		stdlib::register_all(*result);
		result->add_function("parse", [](const std::string& a_text) { return std::tuple<int64_t, std::string>{static_cast<int64_t>(a_text.size()), a_text + "!"}; });
		result->add_function("halves", [](double a_value) { return std::pair<double, bool>{a_value / 2.0, a_value > 1.0}; });
		result->add_function("describe", [](std::tuple<int64_t, std::string, double> a_row) {
			return std::get<1>(a_row) + ":" + std::to_string(std::get<0>(a_row)) + ":" + std::to_string(std::get<2>(a_row) * 2.0);
		});
		result->add_function("pair_sum", [](const std::pair<int, int>& a_pair) { return a_pair.first + a_pair.second; });
		result->add_function("bump", [](std::pair<int64_t, int64_t>& a_pair) {
			++a_pair.first;
			a_pair.second *= 10;
		});
		result->add_function("tagged", [](const std::string& a_tag) { return std::tuple<std::vector<int64_t>, std::string>{{1, 2, 3}, a_tag}; });
		result->add_function("tagged_total", [](const std::tuple<std::vector<int64_t>, std::string>& a_tagged) {
			int64_t total = 0;
			for (int64_t value : std::get<0>(a_tagged)) { total += value; }
			return std::get<1>(a_tagged) + "=" + std::to_string(total);
		});
		result->add_function("corners", []() { return std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>{{0, 1}, {2, 3}}; });
		result->add_function("stock", []() { return std::vector<std::pair<std::string, int64_t>>{{"ore", 4}, {"gem", 1}}; });
		result->add_function("stock_count", [](const std::vector<std::pair<std::string, int64_t>>& a_items) {
			int64_t result = 0;
			for (const auto& [name, count] : a_items) { result += count; }
			return result;
		});
		result->add_function("ranges", []() { return std::map<std::string, std::pair<int64_t, int64_t>>{{"a", {1, 2}}, {"b", {3, 4}}}; });
		result->add_function("range_width", [](const std::map<std::string, std::pair<int64_t, int64_t>>& a_ranges) {
			int64_t result = 0;
			for (const auto& [key, range] : a_ranges) { result += range.second - range.first; }
			return result;
		});
		result->add_function("empty_tuple", []() { return std::tuple<>{}; });
		return result;
	}

	static std::string run(engine& a_engine, const std::string& a_source) {
		return a_engine.execute(a_source).as<std::string>();
	}

	static std::string caught(engine& a_engine, const std::string& a_call) {
		return run(a_engine, "string r = \"none\";\ntry { " + a_call + "; } catch (e) { r = to_string(e); }\nr;");
	}

	void forge_tests() override {
		test("a_tuple_result_is_an_array_the_script_destructures", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("4|ruby!|2"), run(*eng, "auto [n, s] = parse(\"ruby\");\nauto all = parse(\"ruby\");\nto_string(n) + \"|\" + s + \"|\" + to_string(all.size());"));
			check_eq(std::string("1.5 true"), run(*eng, "auto [half, big] = halves(3.0);\nformat(\"{} {}\", half, big);"));
			check_eq(std::string("0"), run(*eng, "to_string(empty_tuple().size());"));
		});

		test("a_tuple_result_is_an_array_of_var", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("4|rubies"), run(*eng, "auto row = parse(\"ruby\");\nrow[1] = \"rubies\";\nto_string(row[0]) + \"|\" + row[1];"));
			check_eq(std::string("[4, \"ruby!\"]"), run(*eng, "to_string(parse(\"ruby\"));"));
		});

		test("a_tuple_parameter_takes_an_array_element_by_element", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("gem:3:5.000000"), run(*eng, "describe([3, \"gem\", 2.5]);"));
			check_eq(std::string("gem:3:4.000000"), run(*eng, "describe([3, \"gem\", 2]);"));
			check_eq(std::string("7"), run(*eng, "to_string(pair_sum([3, 4]));"));
			check_eq(std::string("11"), run(*eng, "array<int> xs = [5, 6];\nto_string(pair_sum(xs));"));
			check_eq(std::string("9"), run(*eng, "var row = [4, 5];\nto_string(pair_sum(row));"));
		});

		test("a_tuple_reference_parameter_writes_back", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("[2, 20]"), run(*eng, "auto p = [1, 2];\nbump(p);\nto_string(p);"));
		});

		test("a_tuple_nests_containers_and_tuples", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("3|hp|hp=6"), run(*eng, "auto [values, tag] = tagged(\"hp\");\nto_string(values.size()) + \"|\" + tag + \"|\" + tagged_total(tagged(\"hp\"));"));
			check_eq(std::string("mp=15"), run(*eng, "tagged_total([[4, 5, 6], \"mp\"]);"));
			check_eq(std::string("0 1 2 3"), run(*eng, "auto [a, b] = corners();\nformat(\"{} {} {} {}\", a[0], a[1], b[0], b[1]);"));
			check_eq(std::string("ore=4 gem=1 5"), run(*eng, "string r = \"\";\nfor (auto row : stock()) { auto [name, count] = row; r += name + \"=\" + to_string(count) + \" \"; }\nr + to_string(stock_count([[\"a\", 2], [\"b\", 3]]));"));
			check_eq(std::string("1 3 2"), run(*eng, "auto spans = ranges();\nformat(\"{} {} {}\", spans[\"a\"][0], spans[\"b\"][0], range_width(spans));"));
		});

		test("a_bound_method_takes_and_returns_a_pair", [this]() {
			auto eng = make_tuple_engine();
			dynamic_binder<host_tuple_fixtures::Ledger>(*eng, "Ledger")
				.constructor<>()
				.method("top", &host_tuple_fixtures::Ledger::top)
				.method("record", &host_tuple_fixtures::Ledger::record)
				.build();
			check_eq(std::string("gem 9"), run(*eng, "auto ledger = new Ledger();\nledger.record([\"ore\", 4]);\nledger.record([\"gem\", 9]);\nledger.record([\"rock\", 2]);\nauto [name, count] = ledger.top();\nformat(\"{} {}\", name, count);"));
			check_eq(std::string("Cannot convert an array of 1 element to a tuple of 2 elements"), caught(*eng, "auto ledger = new Ledger();\nledger.record([\"ore\"])"));
		});

		test("a_tuple_parameter_refuses_another_length", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("Cannot convert an array of 1 element to a tuple of 2 elements"), caught(*eng, "pair_sum([1])"));
			check_eq(std::string("Cannot convert an array of 3 elements to a tuple of 2 elements"), caught(*eng, "pair_sum([1, 2, 3])"));
			check_eq(std::string("Cannot convert an array of 0 elements to a tuple of 3 elements"), caught(*eng, "describe([])"));
			check_eq(std::string("[1, 2]"), run(*eng, "auto p = [1, 2];\ntry { bump([1]); } catch (e) {}\nto_string(p);"));
		});

		test("a_tuple_parameter_refuses_what_its_elements_refuse", [this]() {
			auto eng = make_tuple_engine();
			check_eq(std::string("Cannot convert string to int"), caught(*eng, "pair_sum([\"x\", 2])"));
			check_eq(std::string("Cannot convert int to string"), caught(*eng, "describe([1, 2, 3.0])"));
			check_eq(std::string("No overload of 'pair_sum' takes (int)"), caught(*eng, "var five = 5;\npair_sum(five)"));
			check_eq(std::string("No overload of 'pair_sum' takes (null)"), caught(*eng, "var nothing = null;\npair_sum(nothing)"));
			check_eq(std::string("Cannot convert int to array"), caught(*eng, "stock_count([[\"a\", 2], 5])"));
			check_eq(std::string("No overload of 'pair_sum' takes (map)"), caught(*eng, "var stock = {\"a\": 1};\npair_sum(stock)"));
			check_eq(std::string("Cannot convert string to array"), caught(*eng, "tagged_total([\"x\", \"mp\"])"));
			check_eq(std::string("Cannot convert an array of 1 element to a tuple of 2 elements"), caught(*eng, "stock_count([[\"a\", 2], [\"b\"]])"));
		});

		test("a_tuple_parameter_refuses_a_declared_non_array_at_load", [this]() {
			auto eng = make_tuple_engine();
			std::string text;
			try { (void)eng->execute("pair_sum(5);"); } catch (const static_check_error& e) { text = e.what(); }
			check(text.find("no overload of 'pair_sum' accepts these argument types") != std::string::npos, text);
		});
	}
};

}

using host_tuple_tests = jai::foundry::tests::host_tuple_tests;
FOUNDRY_REGISTER(host_tuple_tests)
