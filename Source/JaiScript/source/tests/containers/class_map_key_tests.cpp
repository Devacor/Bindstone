#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/detail/type_checker.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

// A class value keys a map by its class: through < or <=>, else hash() with ==, else by identity with a warning
namespace class_map_key_hosts {
	struct cell {
		int64_t x = 0;
		cell() = default;
		explicit cell(int64_t a_x) : x(a_x) {}
	};
}

namespace jai::foundry::tests {

class class_map_key_tests : public suite {
public:
	class_map_key_tests() : suite("Class Map Keys") {}

	static constexpr const char* classes = R"(
		class P { int v = 0; P(int a) { v = a; } int operator<=>(P o) { return v - o.v; } }
		class H { int v = 0; H(int a) { v = a; } int hash() { return v % 3; } bool operator==(H o) { return v == o.v; } }
		class I { int v = 0; I(int a) { v = a; } }
	)";

	void forge_tests() override {
		test("an_ordered_class_keys_by_its_order", [this]() {
			auto e = make_engine();
			stdlib::register_all(e);
			e->execute(classes);
			e->execute(R"(
				var m = {};
				m[P(3)] = "c"; m[P(1)] = "a"; m[P(2)] = "b"; m[P(1)] = "A";
				string walk = "";
				for (auto& [k, v] : m) { walk += to_string(k.v) + v; }
			)");
			check_eq((int64_t)3, e->execute("m.size()").as_int());
			check_eq(std::string("A"), e->execute("m[P(1)]").as<std::string>());
			check_eq(true, e->execute("m.contains(P(2)) && !m.contains(P(4))").as_bool());
			check_eq(std::string("1A2b3c"), e->execute("walk").as<std::string>());
			check_eq(true, e->execute("m.remove(P(2)) && m.size() == 2").as_bool());
			check_eq(true, e->execute("var x = {}; x[P(1)] = 1; var y = {}; y[P(1)] = 1; var z = {}; z[P(2)] = 1; x == y && x != z").as_bool());
			check_eq(true, e->execute("var c = m; c[P(9)] = \"z\"; c.size() == 3 && m.size() == 2 && c[P(1)] == \"A\"").as_bool());
		});

		test("a_hashed_class_keys_by_hash_and_equality", [this]() {
			auto e = make_engine();
			stdlib::register_all(e);
			e->execute(classes);
			e->execute(R"(
				var m = {};
				m[H(1)] = "one"; m[H(4)] = "four"; m[H(2)] = "two"; m[H(1)] = "uno";
				string walk = "";
				for (auto& [k, v] : m) { walk += to_string(k.v); }
			)");
			check_eq((int64_t)3, e->execute("m.size()").as_int());
			check_eq(std::string("uno"), e->execute("m[H(1)]").as<std::string>());
			check_eq(std::string("four"), e->execute("m[H(4)]").as<std::string>());
			check_eq(true, e->execute("walk == \"142\" || walk == \"412\"").as_bool());
		});

		test("a_bound_class_with_less_keys_by_it", [this]() {
			using class_map_key_hosts::cell;
			auto e = make_engine();
			stdlib::register_all(e);
			dynamic_binder<cell>(*e, "Cell")
				.constructor<int64_t>()
				.property("x", &cell::x)
				.method("<", [](const cell& a_self, const cell& a_other) { return a_self.x < a_other.x; })
				.build();
			e->execute("var m = {}; m[Cell(2)] = 1; m[Cell(1)] = 2; m[Cell(2)] = 3;");
			check_eq((int64_t)2, e->execute("m.size()").as_int());
			check_eq((int64_t)3, e->execute("m[Cell(2)]").as_int());
			check_eq((int64_t)1, e->execute("int first = 0; for (auto& [k, v] : m) { first = k.x; break; } first").as_int());
		});

		test("a_class_with_neither_keys_by_identity_and_warns_once", [this]() {
			auto e = make_engine();
			stdlib::register_all(e);
			std::vector<std::string> warnings;
			e->set_script_warning_handler([&](const std::string& a_message) { warnings.push_back(a_message); });
			e->execute(classes);
			e->execute("var m = {}; I a = I(1); m[a] = 1; m[I(1)] = 2; m[I(1)] = 3; bool found = m.contains(a);");
			check_eq((int64_t)3, e->execute("m.size()").as_int());
			check_eq(true, e->execute("found").as_bool());
			check_eq((size_t)1, warnings.size());
			check_true(!warnings.empty() && warnings[0].find("map keys of class 'I' compare by identity") != std::string::npos, warnings.empty() ? std::string("none") : warnings[0]);
			auto report = e->check(std::string(classes) + "map<I, int> n = {}; n[I(1)] = 1;");
			bool warned = false;
			for (const auto& d : report.diagnostics) { warned = warned || d.message.find("map keys of class 'I' compare by identity") != std::string::npos; }
			std::string all;
			for (const auto& d : report.diagnostics) { all += d.message + "|"; }
			check_true(warned, all);
		});

		test("a_key_operator_that_throws_leaves_the_map_unchanged", [this]() {
			auto e = make_engine();
			stdlib::register_all(e);
			e->execute(R"(
				class T { int v = 0; T(int a) { v = a; } bool operator<(T o) { if (o.v == 7 || v == 7) { throw "boom"; } return v < o.v; } }
				var m = {};
				m[T(1)] = 1;
				string caught = "";
				try { m[T(7)] = 2; } catch (err) { caught = err; }
			)");
			check_eq(std::string("boom"), e->execute("caught").as<std::string>());
			check_eq((int64_t)1, e->execute("m.size()").as_int());
		});

		test("erasing_the_current_class_key_inside_a_range_for_continues_after_it", [this]() {
			auto e = make_engine();
			stdlib::register_all(e);
			e->execute(classes);
			e->execute(R"(
				var m = {};
				for (int i = 0; i < 5; i++) { m[P(i)] = i; }
				string seen = "";
				for (auto& [k, v] : m) { seen += to_string(k.v); if (k.v == 2) { m.remove(P(2)); } }
			)");
			check_eq(std::string("01234"), e->execute("seen").as<std::string>());
			check_eq((int64_t)4, e->execute("m.size()").as_int());
		});
	}
};

}

using class_map_key_tests = jai::foundry::tests::class_map_key_tests;
FOUNDRY_REGISTER(class_map_key_tests)
