#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class execution_budget_tests : public suite {
public:
	execution_budget_tests() : suite("Execution Budget") {}

	void forge_tests() override {
		test("default_budget_is_one_second", [this]() {
			auto e = engine::make();
			check_near(1.0, e->execution_budget(), 1e-9);
		});

		test("budget_stops_infinite_while", [this]() {
			auto e = engine::make();
			e->execution_budget(0.05);
			check_throws([&]() { e->execute("while (true) { }"); });
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_stops_infinite_fast_path_for", [this]() {
			auto e = engine::make();
			e->execution_budget(0.05);
			// Literal zero step keeps the optimized native counting loop spinning forever
			check_throws([&]() { e->execute("for (int i = 0; i < 2; i += 0) { }"); });
		});

		test("zero_budget_disables_the_limit", [this]() {
			auto e = engine::make();
			e->execution_budget(0.0);
			auto r = e->execute("var x = 0; while (x < 200000) { x += 1; } x;");
			check_eq((int64_t)200000, r.as_int());
		});

		test("budget_error_is_script_catchable", [this]() {
			auto e = engine::make();
			e->execution_budget(0.05);
			auto r = e->execute("var caught = 0; try { while (true) { } } catch (e) { caught = 1; } caught;");
			check_eq((int64_t)1, r.as_int());
		});

		test("budget_rearms_per_execute", [this]() {
			auto e = engine::make();
			e->execution_budget(0.05);
			check_throws([&]() { e->execute("while (true) { }"); });
			// A fresh execute gets a fresh deadline (not the already-expired one).
			// Generous budget: the loop is trivial but CI machines stall under load.
			e->execution_budget(5.0);
			auto r = e->execute("var x = 0; while (x < 10000) { x += 1; } x;");
			check_eq((int64_t)10000, r.as_int());
		});
	}
};

} // namespace jai::foundry::tests

using execution_budget_tests = jai::foundry::tests::execution_budget_tests;
FOUNDRY_REGISTER(execution_budget_tests)
