#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <chrono>
#include <thread>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class execution_budget_tests : public suite {
public:
	execution_budget_tests() : suite("Execution Budget") {}

	void forge_tests() override {
		test("default_budget_is_five_seconds", [this]() {
			auto e = make_engine();
			check_near(5.0, e->execution_budget(), 1e-9);
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

		// fuzz seed 33294: the fast counting loop's fall-through continuation
		// (taken when the counter's storage kind changes mid-loop - here a one-time
		// reference binding boxes it) had no back-edge limit check, so the loop ran
		// unbudgeted forever on the interpreter (the vm checks every back-edge)
		test("budget_stops_fast_path_for_after_counter_is_boxed", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("function bump(int& x) { x += 1; } "
				           "for (int i = 0; i < 4; i++) { if (i != 0) { i = i - 1; } else { bump(i); i = 0; } }");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("budget_stops_fast_path_for_after_counter_is_captured", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("for (int i = 0; i < 4; i++) { if (i == 0) { auto f = [&]() { i = 0; }; f(); } i = i - 1; }");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("a_direct_host_call_runs_under_the_budget_set_before_it", [this]() {
			// a host calling a script function with no script running is a host entry: the deadline is armed from the
			// budget current at the call (not the last execute's), the entry's latch resets, and the clock sample is written
			for (bool use_vm : {false, true}) {
				const std::string be = use_vm ? " (vm)" : " (interpreter)";
				auto e = engine::make();
				e->set_backend(use_vm ? backend_type::vm : backend_type::interpreter);
				e->add_function("nap", []() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); });
				e->execution_budget(1000.0);
				e->execute("function run(int n) -> int { nap(); int acc = 0; for (int i = 0; i < n; i++) { acc += 1; } return acc; }\n"
				           "function idle() -> int { return 1; }");
				const auto run = e->get_variable("run").as_function();
				const std::vector<script_value> n{ script_value(static_cast<script_int>(20000), e.get()) };
				const uint64_t samples = e->clock_sample_count();
				check_true(e->get_variable("idle").as_function()({}).has_value(), "the idle call runs" + be);
				check_true(e->clock_sample_count() > samples, "a direct call's entry writes the clock sample" + be);
				e->execution_budget(0.0005);
				auto tripped = run(n);
				check_true(!tripped.has_value() && std::string(tripped.message()).find("Script execution budget exceeded") != std::string::npos,
				           "the 3 ms nap outlives the 0.5 ms budget set before the call" + be);
				e->execution_budget(1000.0);
				auto fresh = run(n);
				check_true(fresh.has_value() && fresh.value().as_int() == 20000, "the next call arms its own deadline" + be);
			}
		});

		test("zero_budget_disables_the_limit", [this]() {
			auto e = make_engine();
			e->execution_budget(0.0);
			auto r = e->execute("var x = 0; while (x < 200000) { x += 1; } x;");
			check_eq((int64_t)200000, r.as_int());
		});

		// Budget/timeout errors are TERMINAL - no script catch may
		// swallow them (catchable semantics would enable the FZ-BUDGET-LIVELOCK
		// denial-of-service: catch-and-retry loops keep execute() from ever returning).
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
					c.next();
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

		// ONE script catch per execute; the SECOND raise in the same
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
		// toward the allowance too (env size is counted deliberately).
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

		// Every builtin that grows a value past its inputs is a chokepoint: the growth is denied before it exists
		test("memory_cap_charges_every_growing_builtin", [this]() {
			const std::string cap_text = "Script memory cap exceeded (1048576 bytes) - raise engine::memory_cap or break up the work";
			const char* growths[] = {
				"string s = \"x\"; s.pad_left(64000000);",
				"string s = \"x\"; s.pad_right(64000000, \"ab\");",
				"string s = \"x\"; s.pad_center(64000000);",
				"utf8_string u = utf8_string(\"x\"); u.pad_left(64000000);",
				"string s = string::pad_left(\"x\", 64000000);",
				"string s = string::repeat(\"x\", 64000000);",
				"string s = \"abcd\"; s.repeat(4611686018427387905);",
				"string s = \"x\".repeat(500000); s.append(s, s, s);",
				"string s = \"x\".repeat(500000); string t = string::append(s, s, s);",
				"var a = [1]; for (int i = 0; i < 30; i++) { a.concat(a); }",
				"var a = [1]; for (int i = 0; i < 30; i++) { a.append(a); }",
				"var a = [\"a\", \"b\", \"c\"]; string sep = \"x\".repeat(500000); a.join(sep);",
				"var a = [\"a\", \"b\", \"c\"]; string sep = \"x\".repeat(500000); string::join(a, sep);",
				"string s = \"x\".repeat(500000); s.replace_all(\"x\", \"yyyy\");",
				"string s = \"x\".repeat(500000); string::replace_all(s, \"x\", \"yyyy\");",
				"string s = \"x\".repeat(500000); var parts = s.split(\"\");",
				"string s = \"x\".repeat(500000); var parts = s.utf8_chars();",
				"string s = \"x\\n\".repeat(300000); var parts = s.lines();",
				"var w = binary::writer(); w.text(\"x\", 64000000);",
				"var w = binary::writer(); for (int i = 0; i < 1000000; i++) { w.int64(i); }",
				"var deep = [[[[[[1]]]]]]; to_json(deep, 10000000);",
			};
			for (const char* growth : growths) {
				auto e = make_engine();
				stdlib::register_all(*e);
				e->execution_budget(30.0);
				e->memory_cap(1024 * 1024);
				std::string caught;
				try {
					auto r = e->execute(std::string("string msg = \"none\"; try { ") + growth + " } catch (err) { msg = err; } msg");
					caught = r.as<std::string>();
				} catch (const std::exception& ex) {
					caught = std::string("escaped: ") + ex.what();
				}
				check_eq(cap_text, caught, growth);
				check_eq((int64_t)4, e->execute("2 + 2").as_int(), std::string("engine usable after ") + growth);
			}
		});

		// A repeat whose byte count passes size_t reports like any oversized text instead of reserving the wrapped size and
		// appending forever
		test("an_overflowing_repeat_reports_at_once", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execution_budget(10.0);
			const auto start = std::chrono::steady_clock::now();
			for (const char* source : {"string s = \"abcd\"; s.repeat(4611686018427387905);", "string s = string::repeat(\"abcd\", 4611686018427387905);"}) {
				check_throws([&]() { e->execute(source); }, source);
			}
			check_true(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "no step appends the wrapped size");
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		// replace_all builds its result in one pass (replacing in place shifted the tail at every hit: two million hits ran
		// for minutes, out of the budget's reach inside one builtin)
		test("replace_all_is_linear_in_the_hits", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			e->execution_budget(60.0);
			const auto start = std::chrono::steady_clock::now();
			check_eq((int64_t)4000000, e->execute("string s = \"x\".repeat(2000000); s.replace_all(\"x\", \"yy\"); s.size()").as_int());
			check_eq((int64_t)4000000, e->execute("string s = \"x\".repeat(2000000); string::replace_all(s, \"x\", \"yy\").size()").as_int());
			check_eq(std::string("a-b-c"), e->execute("string s = \"a, b, c\"; s.replace_all(\", \", \"-\")").as<std::string>());
			check_eq(std::string("xxyxxy"), e->execute("string s = \"xyxy\"; s.replace_all(\"x\", \"xx\")").as<std::string>());
			check_eq(std::string("q"), e->execute("string s = \"aaaa\"; s.replace_all(\"aa\", \"\"); s + \"q\"").as<std::string>());
			check_true(std::chrono::steady_clock::now() - start < std::chrono::seconds(10), "two million hits finish at once");
		});
	}
};

} // namespace jai::foundry::tests

using execution_budget_tests = jai::foundry::tests::execution_budget_tests;
FOUNDRY_REGISTER(execution_budget_tests)
