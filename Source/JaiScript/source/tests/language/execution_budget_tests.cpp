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
			auto e = make_engine();
			check_near(1.0, e->execution_budget(), 1e-9);
		});

		test("budget_stops_infinite_while", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() { e->execute("while (true) { }"); });
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_stops_infinite_fast_path_for", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			// Literal zero step keeps the optimized native counting loop spinning forever
			check_throws([&]() { e->execute("for (int i = 0; i < 2; i += 0) { }"); });
		});

		test("zero_budget_disables_the_limit", [this]() {
			auto e = make_engine();
			e->execution_budget(0.0);
			auto r = e->execute("var x = 0; while (x < 200000) { x += 1; } x;");
			check_eq((int64_t)200000, r.as_int());
		});

		// Dev ruling 2026-07: budget/timeout errors are TERMINAL - no script catch may
		// swallow them (the old catchable semantics enabled the FZ-BUDGET-LIVELOCK
		// denial-of-service: catch-and-retry loops kept execute() from ever returning).
		test("budget_error_is_terminal_not_catchable", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			bool threw = false;
			std::string msg;
			try {
				e->execute("var caught = 0; try { while (true) { } } catch (e) { caught = 1; } caught;");
			} catch (const std::exception& ex) {
				threw = true;
				msg = ex.what();
			}
			check_true(threw);
			check_true(msg.find("execution budget") != std::string::npos);
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		// The fuzzer's livelock shape (seeds 436/3507/8285/1213/1462/1788/8356/8391):
		// catch-and-retry around infinite work must now RETURN from execute() with the
		// budget error instead of re-raising and re-catching forever.
		test("budget_livelock_catch_retry_loop_terminates", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("while (true) { try { while (true) { } } catch (e) { } }");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_terminal_skips_nested_catches", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("try { try { while (true) { } } catch (a) { } } catch (b) { }");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_terminal_skips_catch_inside_function", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("void spin() { try { while (true) { } } catch (e) { } } spin();");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_terminal_skips_catch_inside_lambda", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("auto f = []() -> auto { try { while (true) { } } catch (e) { return 1; } return 2; }; f();");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_terminal_skips_catch_inside_coroutine", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute(R"(
					coroutine int spin() {
						try { while (true) { } } catch (e) { yield 1; }
						return 0;
					}
					auto c = spin();
					c.resume();
				)");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		// A terminal error in a REENTRANT execute (host callback calling execute()
		// mid-script) fails the inner execute AND propagates through the boundary:
		// the outer script's catch is skipped too, all the way to the host.
		test("budget_terminal_crosses_reentrant_execute_boundary", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			engine* raw = e.get();
			e->add_function("inner_spin", [raw]() -> script_int {
				raw->execute("while (true) { }");
				return 1;
			});
			check_throws([&]() {
				e->execute("var got = 0; try { got = inner_spin(); } catch (err) { got = -1; } got;");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_rearms_per_execute", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() { e->execute("while (true) { }"); });
			// A fresh execute gets a fresh deadline (not the already-expired one).
			// Generous budget: the loop is trivial but CI machines stall under load.
			e->execution_budget(5.0);
			auto r = e->execute("var x = 0; while (x < 10000) { x += 1; } x;");
			check_eq((int64_t)10000, r.as_int());
		});

		// ===== engine::memory_cap (approximate high-water accounting) =====

		test("memory_cap_default_is_unlimited", [this]() {
			auto e = make_engine();
			check_eq((size_t)0, e->memory_cap());
			e->memory_cap(65536);
			check_eq((size_t)65536, e->memory_cap());
			e->memory_cap(0);
			check_eq((size_t)0, e->memory_cap());
		});

		test("memory_cap_zero_is_unlimited", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(0);
			auto r = e->execute(R"(
				var a = [];
				var n = 0;
				while (n < 20000) { a.push("xxxxxxxxxx"); n += 1; }
				a.size();
			)");
			check_eq((int64_t)20000, r.as_int());
		});

		// The legit use case: the FIRST raise per execute is an ordinary catchable
		// error (the allocation was denied; the counter re-arms so the script can
		// free its caches and keep working).
		test("memory_cap_first_raise_is_catchable_and_denies", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(256 * 1024);
			auto r = e->execute(R"(
				var a = [];
				var caught = 0;
				try { while (true) { a.push("xxxxxxxxxx"); } } catch (err) { caught = 1; a = []; }
				var n = 0;
				while (n < 1000) { n += 1; }
				caught;
			)");
			check_eq((int64_t)1, r.as_int());
		});

		test("memory_cap_error_text_names_the_cap", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(65536);
			auto r = e->execute(R"(
				var a = [];
				var msg = "";
				try { while (true) { a.push("xxxxxxxxxx"); } } catch (err) { msg = err; }
				msg;
			)");
			check_eq(std::string("Script memory cap exceeded (65536 bytes) - raise engine::memory_cap or break up the work"),
				r.as<std::string>());
		});

		// Dev ruling: ONE script catch per execute; the SECOND raise in the same
		// execute is TERMINAL (Part-1 machinery - every catch skipped, execute fails).
		test("memory_cap_second_raise_is_terminal", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(256 * 1024);
			bool threw = false;
			std::string msg;
			try {
				e->execute(R"(
					var a = [];
					var caught = 0;
					try { while (true) { a.push("xxxxxxxxxx"); } } catch (err) { caught = 1; a = []; }
					var b = [];
					try { while (true) { b.push("xxxxxxxxxx"); } } catch (err2) { caught = 9; }
					caught;
				)");
			} catch (const std::exception& ex) {
				threw = true;
				msg = ex.what();
			}
			check_true(threw);
			check_true(msg.find("memory cap exceeded") != std::string::npos);
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		// Map growth via subscript auto-insert (no builtin method involved) also
		// counts; the raise fires at the loop back-edge.
		test("memory_cap_map_growth_bomb_trips", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(128 * 1024);
			auto r = e->execute(R"(
				var m = {};
				var i = 0;
				var caught = 0;
				try { while (true) { m[i] = i; i += 1; } } catch (err) { caught = 1; }
				caught;
			)");
			check_eq((int64_t)1, r.as_int());
		});

		// Environment storage growth (defines of new names in fresh scopes) counts
		// toward the allowance too (Dev explicitly wants env size counted).
		test("memory_cap_env_growth_bomb_trips", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(128 * 1024);
			auto r = e->execute(R"(
				var caught = 0;
				try {
					while (true) { var x = [1]; }
				} catch (err) { caught = 1; }
				caught;
			)");
			check_eq((int64_t)1, r.as_int());
		});

		test("memory_cap_resets_per_execute", [this]() {
			auto e = make_engine();
			e->execution_budget(5.0);
			e->memory_cap(256 * 1024);
			check_throws([&]() {
				e->execute(R"(
					var a = [];
					try { while (true) { a.push("xxxxxxxxxx"); } } catch (e1) { }
					var b = [];
					try { while (true) { b.push("xxxxxxxxxx"); } } catch (e2) { }
				)");
			});
			// Fresh execute: fresh allowance and raise count
			auto r = e->execute(R"(
				var a = [];
				var caught = 0;
				try { while (true) { a.push("xxxxxxxxxx"); } } catch (err) { caught = 1; }
				caught;
			)");
			check_eq((int64_t)1, r.as_int());
		});
	}
};

} // namespace jai::foundry::tests

using execution_budget_tests = jai::foundry::tests::execution_budget_tests;
FOUNDRY_REGISTER(execution_budget_tests)
