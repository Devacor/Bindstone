#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <memory>
#include <string>

using namespace jai;
using namespace jai::foundry;

// A bound class's operators answer wherever the language compares or combines its values, and an operator counts for an
// operand only when one of its overloads takes it, as a script class's does
namespace bound_operator_hosts {
	struct money {
		int64_t cents = 0;
		money() = default;
		explicit money(int64_t a_cents) : cents(a_cents) {}
	};
	struct tag {
		int64_t id = 0;
		tag() = default;
		explicit tag(int64_t a_id) : id(a_id) {}
	};
}

namespace jai::foundry::tests {

class bound_operator_tests : public suite {
public:
	bound_operator_tests() : suite("Bound Operators") {}

	static std::shared_ptr<engine> make_bound_engine() {
		using bound_operator_hosts::money;
		using bound_operator_hosts::tag;
		auto eng = make_engine();
		stdlib::register_all(eng);
		dynamic_binder<money>(*eng, "Money")
			.constructor<int64_t>()
			.property("cents", &money::cents)
			.method("==", [](const money& a_self, const money& a_other) { return a_self.cents == a_other.cents; })
			.method("<", [](const money& a_self, const money& a_other) { return a_self.cents < a_other.cents; })
			.method("<=>", [](const money& a_self, int64_t a_other) -> int64_t { return a_self.cents < a_other ? -1 : a_self.cents > a_other ? 1 : 0; })
			.method("+", [](const money& a_self, const money& a_other) { return money(a_self.cents + a_other.cents); })
			.method("to_string", [](const money& a_self) { return std::to_string(a_self.cents) + "c"; })
			.build();
		dynamic_binder<tag>(*eng, "Tag")
			.constructor<int64_t>()
			.property("id", &tag::id)
			.method("==", [](const tag& a_self, const tag& a_other) { return a_self.id == a_other.id; })
			.method("<=>", [](const tag& a_self, const tag& a_other) -> int64_t { return a_self.id < a_other.id ? -1 : a_self.id > a_other.id ? 1 : 0; })
			.build();
		return eng;
	}

	void forge_tests() override {
		test("bound_operators_answer_wherever_values_compare", [this]() {
			auto eng = make_bound_engine();
			eng->execute(R"(
				Money a = Money(1); Money b = Money(2); Money c = Money(1);
				class Wallet { Money m = Money(0); Wallet(Money a_m) { m = a_m; } }
				array<Money> sorted = [Money(3), Money(1), Money(2)];
				sorted.sort();
			)");
			check_eq(true, eng->execute("a == c && a != b && a < b").as_bool());
			check_eq(true, eng->execute("[a, b] == [c, b] && [a, b].contains(c) && [b, a].index_of(c) == 1").as_bool());
			check_eq(true, eng->execute("{1: a} == {1: c} && Wallet(a) == Wallet(c) && Wallet(a) != Wallet(b)").as_bool());
			check_eq((int64_t)123, eng->execute("sorted[0].cents * 100 + sorted[1].cents * 10 + sorted[2].cents").as_int());
		});

		test("an_explicit_operator_that_cannot_take_the_operand_yields_to_the_spaceship", [this]() {
			auto eng = make_bound_engine();
			eng->execute("Money a = Money(1);");
			check_eq(true, eng->execute("a < 5").as_bool());
			check_eq(false, eng->execute("a > 5").as_bool());
			check_eq(true, eng->execute("5 > a").as_bool());
			check_eq(true, eng->execute("a < Money(2)").as_bool());
			auto script = make_engine();
			script->execute(R"(
				class V { int v = 0; V(int a_v) { v = a_v; } bool operator<(V o) { return v < o.v; } int operator<=>(int o) { return v - o; } }
				V w = V(1);
			)");
			check_eq(true, script->execute("w < 5").as_bool());
			check_eq(true, script->execute("w < V(2)").as_bool());
		});

		test("a_bound_operator_that_cannot_take_the_operand_leaves_the_builtin_meaning", [this]() {
			auto eng = make_bound_engine();
			eng->execute("Money a = Money(7);");
			check_eq(std::string("7c!"), eng->execute("a + \"!\"").as<std::string>());
			check_eq((int64_t)9, eng->execute("(a + Money(2)).cents").as_int());
		});

		test("equality_without_a_spaceship_answers_only_values_of_the_class", [this]() {
			auto eng = make_bound_engine();
			eng->execute("Money a = Money(1); Tag t = Tag(1);");
			check_eq(false, eng->execute("a == t").as_bool());
			check_eq(true, eng->execute("a != t").as_bool());
			auto script = make_engine();
			script->execute(R"(
				class P { int v = 0; P() {} bool operator==(P o) { return v == o.v; } }
				class Q { int v = 0; Q() {} }
				P p = P();
			)");
			check_eq(false, script->execute("p == Q()").as_bool());
			check_eq(true, script->execute("p != Q()").as_bool());
			check_eq(true, script->execute("p == P()").as_bool());
		});

		test("a_script_operator_that_cannot_take_the_operand_yields_to_the_bound_base", [this]() {
			auto eng = make_bound_engine();
			eng->execute(R"(
				class Badge : Tag { Badge(int a_id) : Tag(a_id) {} override bool operator==(Badge o) { return false; } }
				var badge = Badge(4);
				var plain = Tag(4);
			)");
			check_eq(false, eng->execute("badge == Badge(4)").as_bool());
			check_eq(true, eng->execute("badge == plain").as_bool());
			check_eq(true, eng->execute("plain == badge").as_bool());
			check_eq(true, eng->execute("badge < Tag(5)").as_bool());
		});
	}
};

}

using bound_operator_tests = jai::foundry::tests::bound_operator_tests;
FOUNDRY_REGISTER(bound_operator_tests)
