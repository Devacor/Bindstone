// A host function whose parameters and result are ints, floats, bools and strings has a scalar entry (engine::host_scalar_entry): a block
// calls it on its argument cells' words and lands the result word, and the general form's direct lane calls it on its arguments' payloads.
// Every case holds both backends to one result; the vm's blocks take the entry where the case says so (cell_counters::host_calls).
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <functional>
#include <stdexcept>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

enum class host_scalar_shade { red, green, blue };

class host_scalar_entry_tests : public suite {
public:
	host_scalar_entry_tests() : suite("Host Scalar Entries") {}

	struct outcome { std::string text; cell_counters counters; };
	using bind_fn = std::function<void(engine&)>;

	static void bind_scalars(engine& e) {
		e.add_function("add_one", [](script_int x) -> script_int { return x + 1; });
		e.add_function("add_two", [](script_int x, script_int y) -> script_int { return x + y; });
		e.add_function("half", [](double x) -> double { return x * 0.5; });
		e.add_function("half32", [](float x) -> float { return x * 0.5f; });
		e.add_function("narrow", [](int x) -> int { return x + 1; });
		e.add_function("big", [](script_int x) -> uint64_t { return static_cast<uint64_t>(x); });
		e.add_function("is_even", [](script_int x) -> bool { return (x & 1) == 0; });
		e.add_function("either", [](bool a, bool b) -> bool { return a || b; });
		e.add_function("text_len", [](const std::string& s) -> script_int { return static_cast<script_int>(s.size()); });
		e.add_function("text_at", [](std::string s, script_int i) -> script_int { return i >= 0 && static_cast<size_t>(i) < s.size() ? s[static_cast<size_t>(i)] : -1; });
		e.add_function("next_shade", [](host_scalar_shade s) -> host_scalar_shade { return static_cast<host_scalar_shade>((static_cast<int>(s) + 1) % 3); });
		e.add_function("nine", [](script_int a, script_int b, script_int c, script_int d, script_int f, script_int g, script_int h, script_int i, script_int j) -> script_int {
			return a + b + c + d + f + g + h + i + j;
		});
	}

	static outcome run_on(bool vm, const std::string& script, const bind_fn& bind, double budget = 0.0) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
		stdlib::register_all(*e);
		bind_scalars(*e);
		if (bind) { bind(*e); }
		if (budget > 0.0) { e->execution_budget(budget); }
		outcome result;
		try { result.text = e->execute(script).to_string(); } catch (const std::exception& ex) { result.text = std::string("throw:") + ex.what(); }
		result.counters = detail::engine_access::cells_counters(*e);
		return result;
	}

	// both backends agree; the vm's blocks made at least min_host_calls calls through a scalar entry
	void both(const std::string& script, const std::string& expected, unsigned long long min_host_calls, const std::string& what, const bind_fn& bind = {}) {
		const outcome interp = run_on(false, script, bind), vm = run_on(true, script, bind);
		check_eq(interp.text, vm.text, what + ": both backends");
		if (!expected.empty()) { check_eq(expected, vm.text, what + ": the value"); }
		check(vm.counters.host_calls >= min_host_calls, what + ": the blocks call the scalar entry (" + std::to_string(vm.counters.host_calls) + " calls)");
		check_eq(0ull, interp.counters.host_calls, what + ": the interpreter has no blocks");
	}

	void forge_tests() override {
		test("a_typed_loop_calls_a_scalar_entry_for_every_consumer", [this]() {
			both(R"JAI(
int G = 0;
int stored() { int c = 0; for (int i = 1; i <= 2000; ++i) { c = add_one(c); } return c; }
int declared() { int s = 0; for (int i = 0; i < 2000; ++i) { int r = add_one(i); s += r; } return s; }
int dropped() { int n = 0; for (int i = 0; i < 2000; ++i) { add_one(i); n += 1; } return n; }
int branched() { int evens = 0; for (int i = 0; i < 2000; ++i) { if (is_even(i)) { evens += 1; } } return evens; }
int global_target() { for (int i = 0; i < 2000; ++i) { G = add_one(G); } return G; }
float widened_result() { float s = 0.0; for (int i = 0; i < 2000; ++i) { float f = add_one(i); s += f; } return s; }
int var_target() { var v = 0; for (int i = 0; i < 2000; ++i) { v = add_one(v); } return v; }
int two() { int c = 0; for (int i = 0; i < 2000; ++i) { c = add_two(c, i); } return c; }
int constant() { int c = 0; for (int i = 0; i < 2000; ++i) { c = add_two(c, 3); } return c; }
int while_loop() { int c = 0; int i = 0; while (i < 2000) { c = add_one(c); i += 1; } return c; }
to_string(stored()) + " " + to_string(declared()) + " " + to_string(dropped()) + " " + to_string(branched()) + " " + to_string(global_target()) + " " +
	to_string(widened_result()) + " " + to_string(var_target()) + " " + to_string(two()) + " " + to_string(constant()) + " " + to_string(while_loop())
)JAI", "2000 2001000 2000 1000 2000 2001000.0 2000 1999000 6000 2000", 17990, "consumers");   // an environment name as an argument loads at the door
		});

		test("a_program_scope_loop_calls_a_scalar_entry", [this]() {
			both(R"JAI(
int total = 0;
for (int i = 0; i < 3000; ++i) { total = add_one(total); }
float x = 1.0;
for (int k = 0; k < 3000; ++k) { x = half(x) + 1.0; }
to_string(total) + " " + to_string(x)
)JAI", "3000 2.0", 0, "program scope");
		});

		test("each_argument_kind_reaches_its_parameter", [this]() {
			both(R"JAI(
float floats() { float x = 1.0; for (int i = 0; i < 2000; ++i) { x = half(x) + 1.0; } return x; }
float ints_widen() { float s = 0.0; for (int i = 0; i < 2000; ++i) { float h = half(i); s += h; } return s; }
float single() { float s = 0.0; float x = 0.5; for (int i = 0; i < 2000; ++i) { float h = half32(x); s += h; } return s; }
float single_from_int() { float s = 0.0; for (int i = 0; i < 2000; ++i) { float h = half32(i); s += h; } return s; }
int bools() { int n = 0; bool flip = false; bool no = false; for (int i = 0; i < 2000; ++i) { if (either(flip, no)) { n += 1; } flip = is_even(i); } return n; }
int strings() { string s = "hello"; int n = 0; for (int i = 0; i < 2000; ++i) { n += text_len(s); n += text_len("ab"); n += text_at(s, i % 7); } return n; }
int narrows() { int c = 0; for (int i = 0; i < 2000; ++i) { c = narrow(c); } return c; }
int bigs() { int c = 0; for (int i = 0; i < 2000; ++i) { c = big(i - 1000); } return c; }
int shades() { int s = 0; int total = 0; for (int i = 0; i < 2000; ++i) { s = next_shade(s); total += s; } return total; }
int many() { int c = 0; for (int i = 0; i < 500; ++i) { c = nine(c, 1, 1, 1, 1, 1, 1, 1, 1); } return c; }
to_string(floats()) + " " + to_string(ints_widen()) + " " + to_string(single()) + " " + to_string(single_from_int()) + " " + to_string(bools()) + " " +
	to_string(strings()) + " " + to_string(narrows()) + " " + to_string(bigs()) + " " + to_string(shades()) + " " + to_string(many())
)JAI", "2.0 999500.0 500.0 999500.0 1000 165582 2000 999 2001 4000", 21980, "argument kinds");   // an int into a float parameter and nine parameters take the door
		});

		test("an_argument_its_parameter_cannot_hold_raises_the_general_calls_error", [this]() {
			both(R"JAI(
string out_of_range() {
	int c = 0;
	try { for (int i = 0; i < 2000; ++i) { c = narrow(i * 3000000); } } catch (e) { return to_string(c) + ": " + e; }
	return "none";
}
int float_to_int() { int c = 0; float f = 1.5; for (int i = 0; i < 2000; ++i) { c += add_one(f); } return c; }
out_of_range() + " | " + to_string(float_to_int())
)JAI", "2145000001: Integer value out of range for int | 4000", 700, "conversions the entry declines");
		});

		test("a_computed_argument_runs_between_the_probe_and_the_call", [this]() {
			// the probe leaves the callee unparked across the argument's own operations; one that misses (an overflow) parks it for the general form
			both(R"JAI(
int computed() { int c = 0; for (int i = 0; i < 2000; ++i) { c = add_two(c, i * 2); } return c; }
float computed_float() { float x = 0.0; for (int i = 0; i < 2000; ++i) { x = half(x + i * 0.5); } return x; }
string overflows() {
	int c = 0;
	int big = 9223372036854775807 / 1500;
	try { for (int i = 0; i < 2000; ++i) { c = add_two(c % 1000, i * big); } } catch (e) { return to_string(c) + ": " + e; }
	return "none";
}
to_string(computed()) + " " + to_string(computed_float()) + " " + overflows()
)JAI", "", 5400, "computed arguments");
		});

		test("a_scalar_entry_that_throws_raises_at_its_call", [this]() {
			const bind_fn bind = [](engine& e) {
				e.add_function("fails_past", [](script_int x, script_int limit) -> script_int { if (x > limit) { throw std::runtime_error("past the limit"); } return x; });
			};
			both(R"JAI(
string in_function() {
	int c = 0;
	try { for (int i = 0; i < 2000; ++i) { c = c + fails_past(i, 1500); } } catch (e) { return to_string(c) + ": " + e; }
	return "none";
}
int G = 0;
string at_program_scope = "none";
try { for (int i = 0; i < 2000; ++i) { G = G + fails_past(i, 900); } } catch (e) { at_program_scope = to_string(G) + ": " + e; }
int caught_inside() { int c = 0; for (int i = 0; i < 300; ++i) { try { c += fails_past(i, 100); } catch (e) { c += 1000; } } return c; }
in_function() + " | " + at_program_scope + " | " + to_string(caught_inside())
)JAI", "1125750: past the limit | 405450: past the limit | 204050", 1490, "a throwing entry", bind);
		});

		test("rebinding_a_host_name_mid_loop_calls_the_new_binding", [this]() {
			const bind_fn bind = [](engine& e) {
				engine* raw = &e;
				// a scalar entry that registers its own name again while it runs
				e.add_function("swap_self", [raw](script_int x) -> script_int {
					raw->add_function("swap_self", [](script_int y) -> script_int { return y * 100; });
					return x;
				});
				// a door call (a script_value parameter has no scalar entry) that registers another function's name again
				e.add_function("grow", [](script_int x) -> script_int { return x + 1; });
				e.add_function("regrow", [raw](const script_value&) -> script_int {
					raw->add_function("grow", [](script_int x) -> script_int { return x + 10; });
					return 0;
				});
			};
			both(R"JAI(
var op = add_one;
function swap_op() { op = narrow; return 0; }
function script_op(int x) { return x + 1000; }
function to_script() { op = script_op; return 0; }
int through_a_variable() {
	int c = 0;
	for (int i = 0; i < 3000; ++i) { c = op(c); if (i == 1000) { swap_op(); } if (i == 2000) { to_script(); } }
	return c;
}
int itself() { int c = 0; for (int i = 1; i <= 2000; ++i) { c += swap_self(i); } return c; }
int from_a_door() { int c = 0; for (int i = 0; i < 2000; ++i) { c = grow(c); if (i == 999) { regrow(i); } } return c; }
to_string(through_a_variable()) + " " + to_string(itself()) + " " + to_string(from_a_door())
)JAI", "1001001 200099901 11000", 3990, "rebinding", bind);
		});

		test("a_budget_stop_inside_a_host_call_loop_ends_the_run", [this]() {
			const char* const loops[] = {
				"int spin() { int c = 0; for (int i = 0; i < 2; i += 0) { c = add_one(c); } return c; } spin();",
				"int spin() { int c = 0; while (true) { c = add_one(c); } return c; } spin();",
				"int c = 0; while (true) { c = add_one(c); }",
			};
			for (const char* loop : loops) {
				for (bool vm : { false, true }) {
					auto e = jai::foundry::make_default_engine();
					e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
					bind_scalars(*e);
					e->execution_budget(0.05);
					std::string text;
					try { e->execute(loop); } catch (const std::exception& ex) { text = ex.what(); }
					check(text.find("execution budget") != std::string::npos, std::string(loop) + (vm ? " (vm)" : " (interpreter)") + ": the budget ends it (" + text + ")");
					e->execution_budget(0.0);
					check_eq((int64_t)8, e->execute("add_one(7)").as_int(), "the engine runs after the stop");
				}
			}
		});

		test("an_overloaded_host_name_takes_the_general_call", [this]() {
			const bind_fn bind = [](engine& e) {
				e.add_function("pick", [](script_int x) -> script_int { return x + 1; });
				e.add_function("pick", [](double x) -> double { return x + 0.5; });
			};
			const std::string script = R"JAI(
string run() { int a = 0; float b = 0.0; for (int i = 0; i < 2000; ++i) { a = pick(a); b = pick(b); } return to_string(a) + " " + to_string(b); }
run()
)JAI";
			both(script, "2000 1000.0", 0, "an overload set", bind);
			check_eq(0ull, run_on(true, script, bind).counters.host_calls, "an overload set has no scalar entry");
		});

		test("a_guarded_or_uncounted_binding_takes_the_general_call", [this]() {
			const bind_fn bind = [](engine& e) {
				engine* raw = &e;
				e.add_function("guarded", [](script_int x) -> script_int { return x + 1; }, jai::permissions::filesystem);
				e.add_function("uncounted", [](script_int x) -> script_int { return x + 1; }, jai::exclude_execution_time);
				e.add_function("revoke", [raw]() { raw->disable(jai::permissions::filesystem); });
			};
			const std::string script = R"JAI(
string run() {
	int a = 0; int b = 0;
	try { for (int i = 0; i < 2000; ++i) { a = guarded(a); b = uncounted(b); if (i == 1200) { revoke(); } } } catch (e) { return to_string(a) + " " + to_string(b) + ": " + e; }
	return "none";
}
run()
)JAI";
			const outcome interp = run_on(false, script, bind), vm = run_on(true, script, bind);
			check_eq(interp.text, vm.text, "a guarded binding: both backends");
			check(vm.text.rfind("1201 1201: ", 0) == 0, "the guard stops the first call after the permission goes (" + vm.text + ")");
		});

		test("a_scalar_entry_that_enters_the_engine_lands_through_the_door", [this]() {
			const bind_fn bind = [](engine& e) {
				engine* raw = &e;
				// each of these reaches script state from a function of words: the block re-reads what it holds when the entry returns
				e.add_function("relay", [raw](script_int x) -> script_int { return raw->execute("on_relay(" + std::to_string(x) + ")").as_int(); });
				e.add_function("redefine", [raw](script_int x) -> script_int { raw->add_global("K", script_value(x * 2, raw)); return x; });
			};
			both(R"JAI(
int G = 0;
int K = 1;
array<int> table = [1, 2, 3, 4];
function on_relay(int x) { G = G + x; if (x % 50 == 0) { table = [x, x + 1, x + 2, x + 3]; } return G; }
int reads_a_global() { int s = 0; for (int i = 0; i < 400; ++i) { G = G + 1; s += relay(i) % 7 + G % 5; } return s; }
int reads_a_table() { int s = 0; for (int i = 0; i < 400; ++i) { s += table[i & 3] + relay(i) % 3 + table[(i + 1) & 3]; } return s; }
int reads_a_define() { int s = 0; for (int i = 0; i < 400; ++i) { s += K; s += redefine(i) % 2; s += K; } return s; }
to_string(reads_a_global()) + " " + to_string(reads_a_table()) + " " + to_string(reads_a_define())
)JAI", "", 0, "an entry that reaches the engine", bind);
		});

		test("a_parallel_body_calls_a_scalar_entry_through_the_door", [this]() {
			for (size_t workers : { size_t(1), size_t(4) }) {
				std::string text[2];
				for (bool vm : { false, true }) {
					auto e = jai::foundry::make_default_engine();
					e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
					stdlib::register_all(*e);
					bind_scalars(*e);
					e->parallel_thread_count(workers);
					try {
						text[vm ? 1 : 0] = e->execute(R"JAI(
function climb(int start) -> int { int c = start; for (int i = 0; i < 200; ++i) { c = add_one(c); } return c; }
array<int> starts = [];
for (int i = 0; i < 64; ++i) { starts.push(i * 3); }
var ends = parallel_transform(starts, climb);
int sum = 0;
for (auto v : ends) { sum += v; }
sum
)JAI").to_string();
					} catch (const std::exception& ex) { text[vm ? 1 : 0] = std::string("throw:") + ex.what(); }
				}
				check_eq(text[0], text[1], "a parallel body with " + std::to_string(workers) + " workers: both backends");
				check_eq(std::string("18848"), text[1], "a parallel body with " + std::to_string(workers) + " workers: the value");
			}
		});

		test("the_general_form_calls_a_scalar_entry_on_its_arguments_payloads", [this]() {
			// an open loop (a try inside it) and plain statements: the direct lane's scalar entry, its declines and its throw
			const bind_fn bind = [](engine& e) {
				e.add_function("fails_past", [](script_int x, script_int limit) -> script_int { if (x > limit) { throw std::runtime_error("past the limit"); } return x; });
				e.add_function("stamp", [](script_int) {});
			};
			both(R"JAI(
string log = to_string(add_one(41)) + " " + to_string(half(5)) + " " + to_string(half(3.0)) + " " + to_string(half32(3)) + " " + to_string(is_even(4)) + " " +
	to_string(text_len("four")) + " " + to_string(narrow(-8)) + " " + to_string(next_shade(2)) + " " + to_string(add_one(2.5));
try { narrow(3000000000); } catch (e) { log += " | " + e; }
log += " " + to_string(big(-1));
try { fails_past(9, 3); } catch (e) { log += " | " + e; }
var boxed = 5;
var text = "boxed";
stamp(1);
log += " | " + to_string(add_one(boxed)) + " " + to_string(text_len(text));
int open_loop() { int c = 0; for (int i = 0; i < 500; ++i) { try { c = add_two(c, i); c = c + fails_past(i, 250); } catch (e) { c = c - 1; } } return c; }
log + " | " + to_string(open_loop())
)JAI", "", 0, "the general form", bind);
		});
	}
};

}

using host_scalar_entry_tests = jai::foundry::tests::host_scalar_entry_tests;
FOUNDRY_REGISTER(host_scalar_entry_tests)
