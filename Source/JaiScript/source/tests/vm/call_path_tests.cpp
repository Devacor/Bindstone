// The call path's exits: a frame pops what it opened (a try, a range-for, a counted loop, a parked callee) on every way out (a return,
// a throw through it, a depth limit, a spent budget, a tail call's splice, a coroutine's end), and a destructor that runs while a frame
// pops finds the stacks as its own calls need them. Every shape runs the same on the interpreter, the vm and the vm-reference backend.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class call_path_tests : public suite {
public:
	call_path_tests() : suite("Call Path") {}

	enum class which { interp, vm, vm_reference };

	static std::shared_ptr<engine> make_for(which w) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(w == which::interp ? backend_type::interpreter : backend_type::vm);
		if (w == which::vm_reference) { e->vm_reference_mode(true); }
		e->execution_budget(0);
		stdlib::register_all(*e);
		return e;
	}

	static std::string run(which w, const std::string& script) {
		auto e = make_for(w);
		try { return e->execute(script).to_string(); }
		catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void all(const std::string& script, const std::string& expected, const std::string& label) {
		check_eq(expected, run(which::interp, script), label + ": interpreter");
		check_eq(expected, run(which::vm, script), label + ": vm");
		check_eq(expected, run(which::vm_reference, script), label + ": vm-reference");
	}

	void forge_tests() override {
		test("a_throw_through_a_counted_loop_leaves_the_callers_loop", [this]() {
			all(R"(
				function thrower(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += i; if (i == 3) { throw "boom"; } } return s; }
				function outer() -> string {
					string log = "";
					for (int k = 0; k < 4; ++k) {
						try { log += to_string(thrower(k + 2)); } catch (e) { log += "!" + e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "1,3,!boom,!boom,", "counted loop");
		});

		test("a_throw_through_a_range_for_leaves_the_callers_iteration", [this]() {
			all(R"(
				function walk(array<int> xs, int stop) -> int { int s = 0; for (int x : xs) { if (x == stop) { throw "stop" + to_string(x); } s += x; } return s; }
				function outer() -> string {
					string log = "";
					array<int> stops = [9, 2, 9, 3];
					for (int stop : stops) {
						try { log += to_string(walk([1, 2, 3, 4], stop)); } catch (e) { log += e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "10,stop2,10,stop3,", "range-for");
		});

		test("a_rethrow_through_a_try_and_parked_callees_unwinds_in_order", [this]() {
			all(R"(
				function inner(int n) -> int { if (n > 2) { throw "deep" + to_string(n); } return n; }
				function middle(int n) -> int {
					int r = 0;
					try { r = inner(n); } catch (e) { throw "re:" + e; }
					return r + 10;
				}
				function add(int a, int b) -> int { return a + b; }
				function outer() -> string {
					string log = "";
					for (int k = 1; k < 5; ++k) {
						try { log += to_string(add(middle(k), add(k, middle(k + 1)))); } catch (e) { log += e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "24,re:deep3,re:deep3,re:deep4,", "try and parked callees");
		});

		test("a_leaf_method_that_throws_leaves_its_callers_loop_and_try", [this]() {
			all(R"(
				class Acc {
					int total = 0;
					int leaf(int x) { if (x == 3) { throw "leaf3"; } return x * 2; }
					int pick(int a, int b) { return a + b; }
					int run(int n) {
						for (int i = 0; i < n; ++i) {
							try { total += pick(leaf(i), leaf(i + 1)); } catch (e) { total += 1000; }
						}
						return total;
					}
				}
				shared_ptr<Acc> a = new Acc();
				string out = to_string(a.run(5));
				out + "," + to_string(a.run(2));
			)", "2026,2034", "leaf method");
		});

		test("a_depth_limit_at_each_entry_unwinds_to_a_catch_inside_loops", [this]() {
			// a raw typed frame, a lean method frame and a plain frame each run into the cap under a range-for, a counted loop and a try
			const std::string script = R"(
				int grow(int n) { return grow(n + 1) + 1; }
				class R { int deep(int n) { return this.deep(n + 1) + 1; } int ok() { return 7; } }
				function deepfn(n) -> auto { return deepfn(n + 1) + 1; }
				function probe() -> string {
					string log = "";
					shared_ptr<R> r = new R();
					array<int> order = [0, 1, 2, 0, 1, 2];
					for (int which : order) {
						for (int i = 0; i < 2; ++i) {
							try {
								if (which == 0) { log += to_string(grow(0)); }
								if (which == 1) { log += to_string(r.deep(0)); }
								if (which == 2) { log += to_string(deepfn(0)); }
							} catch (e) { log += "E"; }
						}
						log += to_string(r.ok());
					}
					return log;
				}
				probe();
			)";
			for (const which w : {which::interp, which::vm, which::vm_reference}) {
				auto e = make_for(w);
				e->max_call_depth(backend_type::interpreter, 200);
				e->max_call_depth(backend_type::vm, 200);
				check_eq(std::string("EE7EE7EE7EE7EE7EE7"), e->execute(script).to_string(), "depth limit under loops");
			}
		});

		test("a_spent_budget_inside_nested_loops_leaves_the_engine_usable", [this]() {
			const std::string spin = R"(
				class Spinner { int n = 0; int turn(int k) { for (int i = 0; i < k; ++i) { n += i; } return n; } }
				function forever() -> int {
					shared_ptr<Spinner> s = new Spinner();
					int total = 0;
					for (int v : [1, 2, 3]) {
						try { while (true) { total += s.turn(v); } } catch (e) { total = -1; }
					}
					return total;
				}
				forever();
			)";
			const std::string after = R"(
				function thrower(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += i; if (i == 3) { throw "boom"; } } return s; }
				string log = "";
				for (int k = 0; k < 4; ++k) { try { log += to_string(thrower(k + 2)); } catch (e) { log += "!" + e; } log += ","; }
				log;
			)";
			for (const which w : {which::interp, which::vm, which::vm_reference}) {
				auto e = make_for(w);
				e->execution_budget(0.05);
				check_throws([&]() { e->execute(spin); }, "the budget stops the loop");
				e->execution_budget(0);
				check_eq(std::string("1,3,!boom,!boom,"), e->execute(after).to_string(), "the next run starts clean");
			}
		});

		test("a_coroutine_frame_keeps_its_own_loops_and_tries", [this]() {
			all(R"(
				coroutine int gen(int n) {
					for (int i = 0; i < n; ++i) {
						try { if (i == 2) { throw "g2"; } yield i; } catch (e) { yield 100; }
					}
				}
				function sum_gen(int n) -> int { int s = 0; for (int v : gen(n)) { for (int k = 0; k < 2; ++k) { s += v; } } return s; }
				coroutine int bad(int n) { for (int i = 0; i < n; ++i) { if (i == 1) { throw "bad1"; } yield i; } }
				function drain() -> string {
					string log = "";
					for (int round = 0; round < 3; ++round) {
						try { for (int v : bad(3)) { log += to_string(v); } } catch (e) { log += e; }
						log += "|" + to_string(sum_gen(4)) + ";";
					}
					return log;
				}
				drain();
			)", "0bad1|208;0bad1|208;0bad1|208;", "coroutine frames");
		});

		test("a_destructor_that_loops_and_catches_while_a_frame_pops", [this]() {
			// the last handle dies in a popping frame (a local) and at a statement's end (a temporary receiver); the destructor opens a range-for,
			// counted loops and a try of its own
			all(R"(
				string log = "";
				function note(int k) -> int { int s = 0; for (int i = 0; i < k; ++i) { s += i; } try { if (k == 3) { throw "n3"; } } catch (e) { s += 100; } return s; }
				class D {
					int k = 0;
					D(int n) { k = n; }
					~D() { for (int v : [1, 2]) { log += "d" + to_string(k) + ":" + to_string(note(k + v)) + " "; } }
					int get() { return k; }
				}
				function make(int n) -> shared_ptr<D> { return new D(n); }
				function use(int n) -> int { shared_ptr<D> local = new D(n); int s = 0; for (int i = 0; i < 2; ++i) { s += local.get(); } return s; }
				function outer() -> int {
					int s = 0;
					for (int i = 0; i < 3; ++i) { s += make(i).get(); s += use(i + 10); }
					return s;
				}
				to_string(outer()) + " " + log;
			)", "69 d0:0 d0:1 d10:55 d10:66 d1:1 d1:103 d11:66 d11:78 d2:103 d2:6 d12:78 d12:91 ", "destructor during a pop");
		});

		test("a_tail_call_after_a_loop_splices_a_clean_frame", [this]() {
			all(R"(
				function countdown(int n, int acc) -> int {
					for (int i = 0; i < 2; ++i) { acc += 1; }
					if (n == 0) { return acc; }
					return countdown(n - 1, acc);
				}
				function countdown2(int n, int acc) -> int {
					if (n == 0) { return acc; }
					return countdown2(n - 1, acc + 2);
				}
				function both() -> string { string s = ""; for (int k = 0; k < 2; ++k) { s += to_string(countdown(60, 0)) + "," + to_string(countdown2(60, 1)) + ";"; } return s; }
				both();
			)", "122,121;122,121;", "tail call");
		});
	}
};

}

using call_path_tests = jai::foundry::tests::call_path_tests;
FOUNDRY_REGISTER(call_path_tests)
