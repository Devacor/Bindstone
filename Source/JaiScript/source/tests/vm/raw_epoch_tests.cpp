// A raw call's caller holds what it read before the call: environment values in its cells, the callee its site's plan names, the
// leaves it inlined. A raw-entered callee that runs general code (a call through its door, to script or to the host) can change each of
// them, so the caller reads them again when the callee returns. Every shape runs the same on the interpreter, the vm and the vm-reference
// backend, with type proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <functional>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class raw_epoch_tests : public suite {
public:
	raw_epoch_tests() : suite("Raw Call Epoch") {}

	enum class which { interp, vm, vm_reference };
	using setup_fn = std::function<void(engine&)>;

	static std::string run(which w, bool proofs, const std::string& script, const setup_fn& setup = {}) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(w == which::interp ? backend_type::interpreter : backend_type::vm);
		if (w == which::vm_reference) { e->vm_reference_mode(true); }
		if (!proofs) { e->type_proofs(false); }
		e->execution_budget(0);
		stdlib::register_all(*e);
		if (setup) { setup(*e); }
		try { return e->execute(script).to_string(); }
		catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void all(const std::string& script, const std::string& expected, const std::string& label, const setup_fn& setup = {}) {
		check_eq(expected, run(which::interp, true, script, setup), label + ": interpreter");
		check_eq(expected, run(which::vm, true, script, setup), label + ": vm");
		check_eq(expected, run(which::vm_reference, true, script, setup), label + ": vm-reference");
		check_eq(expected, run(which::interp, false, script, setup), label + ": interpreter, no proofs");
		check_eq(expected, run(which::vm, false, script, setup), label + ": vm, no proofs");
	}

	void forge_tests() override {
		test("a_global_a_raw_callees_callee_writes_is_read_again_by_the_raw_caller", [this]() {
			all(R"(
				int G = 0;
				void bump(int x) { switch (x) { default: G = G + x; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { bump(i); s += i; } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(4); t += G; } return t; }
				run(5);
			)", "120", "an int global");
			all(R"(
				float F = 0.5;
				bool B = false;
				void bump(int x) { switch (x) { default: F = F + x * 0.25; B = !B; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { bump(i); s += i; } return s; }
				function run(int n) -> float { float t = 0.0; for (int k = 0; k < n; ++k) { t = t + work(3); t = t + F; if (B) { t = t + 100.0; } } return t; }
				run(5);
			)", "328.75", "a float and a bool global");
		});

		test("the_raw_caller_reads_again_whatever_its_own_shape_and_the_callees", [this]() {
			// the call after the callee's loop, a callee that is one call, a caller that is no frame program, a raw chain two deep
			all(R"(
				int G = 0;
				void bump(int x) { switch (x) { default: G = G + x; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += i; } bump(n); return s; }
				int once(int n) { bump(n); return n + 1; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(3); t += G; t += once(k); t += G; } return t; }
				run(5);
			)", "150", "a call after the loop, a callee of one call");
			all(R"(
				int G = 0;
				void bump(int x) { switch (x) { default: G = G + x; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { bump(i); s += i; } return s; }
				function run(int n, string tag) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(4); t += G; } return t + tag.size(); }
				run(5, "");
			)", "120", "a caller with a string parameter");
			all(R"(
				int G = 0;
				void bump(int x) { switch (x) { default: G = G + x; } }
				int inner(int n) { int s = 0; for (int i = 0; i < n; ++i) { bump(i); s += i; } return s; }
				int middle(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += inner(i); } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += middle(4); t += G; } return t; }
				run(5);
			)", "80", "a raw chain two deep");
			all(R"(
				int G = 0;
				array<int> LOG = [];
				void note(array<int>& log, int x) { log.push(x); G = log.size(); }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { note(LOG, i); s += i; s += i * 2; s += 1; } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(4); t += G; } return t; }
				run(5);
			)", "170", "a callee's call that binds a reference");
		});

		test("a_raw_callee_rebound_by_its_own_callee_is_the_new_function_at_the_next_call", [this]() {
			all(R"(
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { swap(i); s += i; } return s; }
				int other(int n) { return n * 1000; }
				void swap(int x) { switch (x) { default: if (x == 2) { work = other; } } }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(k); } return t; }
				run(6);
			)", "9004", "the raw callee's name");
		});

		test("an_inlined_leaf_a_raw_callees_callee_redefines_is_called_as_redefined", [this]() {
			all(R"(
				int leaf(int x) { return x + 1; }
				int other(int x) { return x * 1000; }
				void redefine(int x) { switch (x) { default: if (x == 3) { leaf = other; } } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { redefine(n); s += i; } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += leaf(k); t += work(k); t += leaf(k); } return t; }
				run(6);
			)", "21036", "an inlined leaf");
		});

		test("a_host_function_at_a_raw_callees_door_that_writes_a_global", [this]() {
			const setup_fn setup = [](engine& eng) {
				engine* raw = &eng;
				eng.add_function("poke", [raw](script_int x) -> script_int {
					raw->execute("G = G + " + std::to_string(x) + ";");
					return x;
				});
			};
			all(R"(
				int G = 0;
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += poke(i); } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(4); t += G; } return t; }
				run(5);
			)", "120", "a host callee", setup);
		});

		test("what_a_raw_caller_reads_through_its_bindings_stays_right", [this]() {
			// a string global, a container the callee's callee replaces or empties, a var global that changes kind
			all(R"(
				string S = "a";
				void grow(int x) { switch (x) { default: S = S + "bc"; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { grow(i); s += i; } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += work(2); if (S == "abcbc") { t += 1000; } if (S < "abcbcbcbcb") { t += 1; } } return t; }
				run(5);
			)", "1007", "a string global");
			all(R"(
				array<int> A = [1, 2, 3, 4, 5, 6, 7, 8];
				array<int> build(int x) { array<int> fresh = []; for (int j = 0; j < 8; ++j) { fresh.push(x * 100 + j); } return fresh; }
				void swap_out(int x) { switch (x) { default: A = build(x); } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { swap_out(n); s += i; } return s; }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += A[k]; t += work(k); t += A[k]; } return t; }
				run(6);
			)", "2553", "a replaced array");
			all(R"(
				array<int> A = [1, 2, 3, 4, 5, 6, 7, 8];
				void empty_out(int x) { switch (x) { default: if (x == 3) { A = []; } } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { empty_out(n); s += i; } return s; }
				function run(int n) -> string { int t = 0; string out = "ok"; try { for (int k = 0; k < n; ++k) { t += A[k]; t += work(k); t += A[k]; } } catch (e) { out = e; } return to_string(t) + " " + out; }
				run(6);
			)", "20 Array index 3 out of bounds for array of size 0", "an emptied array");
			all(R"(
				var G = 1;
				void flip(int x) { switch (x) { default: if (x == 2) { G = 2.5; } } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { flip(n); s += i; } return s; }
				function run(int n) -> string { string out = ""; for (int k = 0; k < n; ++k) { int w = work(k); out += to_string(G + w) + " "; } return out; }
				run(5);
			)", "1 1 3.5 5.5 8.5 ", "a var global that changes kind");
		});
	}
};

}

using raw_epoch_tests = jai::foundry::tests::raw_epoch_tests;
FOUNDRY_REGISTER(raw_epoch_tests)
