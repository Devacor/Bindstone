// A local no cell holds (a var, a container, an object) that a branch of a loop declares ends at the branch's close or at a
// continue: the loop still closes as a block, and the arm leaves the block at the kill (a bail), where the general form
// releases the value. A site over such a container is the general form's too (no block entry can resolve a container the
// body declares), so the block enters. Every case holds both backends to one text, proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class branch_local_kill_tests : public suite {
public:
	branch_local_kill_tests() : suite("Branch Local Kills") {}

	struct outcome { std::string text; cell_counters counters; std::string listing; };

	static outcome run_on(bool vm, const std::string& script, bool proofs, uint32_t miss_every = 0) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
		stdlib::register_all(*e);
		if (!proofs) { e->type_proofs(false); }
		if (miss_every) { e->cells_miss_every(miss_every); }
		outcome result;
		if (vm) { e->disassembly_sink([&result](const std::string& listing, const std::string&) { result.listing += listing; result.listing += "\n"; }); }
		try { result.text = e->execute(script).to_string(); }
		catch (const std::exception& ex) { result.text = std::string("throw:") + ex.what(); }
		result.counters = detail::engine_access::cells_counters(*e);
		return result;
	}

	// one function's part of a listing
	static std::string listing_of(const std::string& listing, const std::string& function) {
		const size_t at = listing.find("== " + function + " (");
		if (at == std::string::npos) { return {}; }
		const size_t end = listing.find("\n== ", at);
		return listing.substr(at, end == std::string::npos ? std::string::npos : end - at);
	}

	outcome both(const std::string& script, const std::string& expected, const std::string& what) {
		outcome proven;
		for (bool proofs : { true, false }) {
			const outcome interp = run_on(false, script, proofs);
			const outcome vm = run_on(true, script, proofs);
			const std::string tag = what + (proofs ? "" : " (proofs off)");
			check_eq(interp.text, vm.text, tag + ": both backends");
			if (!expected.empty()) { check_eq(expected, vm.text, tag + ": the value"); }
			if (proofs) { proven = vm; }
			if (engine::miss_injection_available()) {
				for (uint32_t every : { 1u, 2u, 3u, 5u }) { check_eq(interp.text, run_on(true, script, proofs, every).text, tag + ": a miss every " + std::to_string(every)); }
			}
		}
		return proven;
	}

	void forge_tests() override {
		test("a_continue_past_a_branchs_container_local_bails", [this]() {
			const outcome vm = both(R"JAI(
function run(int n) -> string {
	int total = 0; int taken = 0;
	for (int i = 0; i < n; ++i) {
		int a = i * 3;
		if ((i & 7) == 5) {
			var pair = [a, i];
			total = total + pair[0] + pair[1];
			taken = taken + 1;
			continue;
		}
		total = total + a;
	}
	return to_string(total) + " " + to_string(taken);
}
run(200);
)JAI", "", "a continue past a var local");
			check(vm.counters.entries > 0, "a continue past a var local: the loop enters its block");
			check(vm.counters.misses > 0, "a continue past a var local: the arm bails at the kill");
			check(vm.listing.find("-- open loop") == std::string::npos, "a continue past a var local: the loop closes");
		});

		test("a_row_builders_rare_arm_with_a_state_array", [this]() {
			// the reel presenter's shape: a string row built cell by cell, one arm in many declaring a var array a callee writes through
			const outcome vm = both(R"JAI(
array<int> LUM = [];
array<string> FG = [];
function mark(int cell, var& state) -> string {
	state[0] = cell & 7;
	state[1] = (cell >> 3) & 7;
	return "<" + to_string(cell) + ">";
}
function row(int cy, int cols) -> string {
	string out = "";
	int fg = -1; int bg = -1;
	int cell = cy * cols;
	for (int cx = 0; cx < cols; ++cx) {
		int p0 = (cx * 7 + cy) & 15;
		int l0 = LUM[p0];
		if ((cell % 11) == 3) {
			var state = [fg, bg];
			out += mark(cell, state);
			fg = state[0];
			bg = state[1];
			++cell;
			continue;
		}
		if (p0 != fg) { out += FG[p0]; fg = p0; }
		if (l0 > 8 && p0 != bg) { bg = p0; }
		out += "#";
		++cell;
	}
	return out + to_string(fg) + to_string(bg);
}
function run() -> string {
	for (int i = 0; i < 16; ++i) { LUM.push((i * 5) & 15); FG.push("f" + to_string(i)); }
	string all = "";
	for (int cy = 0; cy < 6; ++cy) { all += row(cy, 40) + "|"; }
	return to_string(all.size()) + " " + all.substr(0, 60);
}
run();
)JAI", "", "a row builder's rare arm");
			check(vm.counters.entries > 0, "a row builder's rare arm: the loop enters its block");
			const std::string row = listing_of(vm.listing, "row");
			check(row.find("-- cell loop") != std::string::npos && row.find("-- open loop") == std::string::npos, "a row builder's rare arm: the loop closes");
		});

		test("a_branchs_close_ends_its_local_on_the_general_form", [this]() {
			const outcome vm = both(R"JAI(
function run(int n) -> string {
	int total = 0; string seen = "";
	for (int i = 0; i < n; ++i) {
		int a = i + 1;
		if ((i % 9) == 4) {
			array<int> xs = [a, a * 2];
			string s = to_string(a);
			total = total + xs[1];
			seen = seen + s;
		}
		total = total + a;
	}
	return to_string(total) + " " + seen;
}
run(120);
)JAI", "", "a branch's close");
			check(vm.counters.entries > 0, "a branch's close: the loop enters its block");
			check(vm.listing.find("-- open loop") == std::string::npos, "a branch's close: the loop closes");
		});

		test("a_destructor_runs_at_the_kill_and_the_loop_reads_what_it_wrote", [this]() {
			both(R"JAI(
int G = 0;
string LOG = "";
class Tick {
	int by = 1;
	Tick(int b) { by = b; }
	~Tick() { G = G + by; if (by > 90) { LOG += "!"; } }
}
function run(int n) -> string {
	int total = 0;
	for (int i = 0; i < n; ++i) {
		if ((i % 5) == 2) {
			Tick t = Tick(i);
			total = total + G;
			continue;
		}
		if ((i % 7) == 3) {
			Tick u = Tick(100);
			total = total - 1;
		}
		total = total + G + i;
	}
	return to_string(total) + " " + to_string(G) + " " + LOG;
}
run(80);
)JAI", "", "destructors at a continue and at a close");
		});

		test("an_arm_taken_every_iteration_and_nested_arms", [this]() {
			both(R"JAI(
function run(int n) -> string {
	int total = 0;
	for (int i = 0; i < n; ++i) {
		int a = i * 2;
		if (a >= 0) {
			var held = [a];
			if ((i & 3) == 1) {
				var inner = [held[0], 7];
				total = total + inner[1];
				continue;
			}
			total = total + held[0];
		}
		total = total + 1;
	}
	int j = 0;
	while (j < n) {
		j = j + 1;
		if ((j & 1) == 0) { map<string, int> m = {"k": j}; total = total + m["k"]; continue; }
		total = total - 1;
	}
	return to_string(total);
}
run(90);
)JAI", "", "an arm every iteration, nested arms, a while");
		});

		test("an_arms_boundary_ahead_of_its_bail_is_the_bail", [this]() {
			// an arm whose first statements are a boundary and which then leaves at a bail (its local's kill) leaves at the boundary's
			// place: one miss, no park and resume ahead of it
			const outcome arm = both(R"JAI(
function run(int n) -> string {
	int s = 0; int fg = 0;
	for (int i = 0; i < n; ++i) {
		int p0 = (i * 7) & 15;
		if ((i & 7) == 1) {
			var state = [fg, p0];
			state[1] = i;
			fg = state[0] + 1;
			s = s + 1;
			continue;
		}
		if (p0 != fg) { fg = p0; s = s + 1; }
		s = s + (p0 >> 1);
	}
	return to_string(s) + ":" + to_string(fg);
}
run(400);
)JAI", "", "a boundary then a kill in one arm");
			check(arm.counters.entries > 0 && arm.counters.misses > 0, "a boundary then a kill in one arm: the loop enters and the arm bails");
			check_eq(0ull, arm.counters.boundaries, "a boundary then a kill in one arm: no boundary runs");
			check(arm.listing.find("C_BAIL") != std::string::npos && arm.listing.find("C_BOUNDARY") == std::string::npos && arm.listing.find("-- open loop") == std::string::npos, "a boundary then a kill in one arm: the listing");
			// two boundaries apart in the arm: the arm leaves at the first
			const outcome two = both(R"JAI(
function run(int n) -> string {
	int s = 0; int t = 0;
	for (int i = 0; i < n; ++i) {
		if ((i % 9) == 2) {
			var a = [i, i + 1];
			s = s + 3;
			t = t + a[1];
			s = s + t;
			var b = [t];
			t = t + 1;
			s = s + b[0];
			t = t + 2;
			continue;
		}
		s = s + (i & 3);
	}
	return to_string(s) + ":" + to_string(t);
}
run(300);
)JAI", "", "two boundaries then a kill in one arm");
			check_eq(0ull, two.counters.boundaries, "two boundaries then a kill in one arm: no boundary runs");
			check(two.counters.entries > 0 && two.listing.find("-- open loop") == std::string::npos, "two boundaries then a kill in one arm: the loop closes and enters");
			// a boundary on the main path ahead of the arm, and one in an arm the bail's own arm encloses, keep their place
			const outcome kept = both(R"JAI(
function run(int n) -> string {
	int s = 0; string log = "";
	array<string> names = ["a", "b", "c", "d"];
	for (int i = 0; i < n; ++i) {
		string name = names[i & 3];
		s = s + name.size() + (i & 5);
		if ((i % 11) == 3) {
			if ((i & 1) == 1) { log = log + name; }
			var held = [i];
			s = s + held[0];
			s = s + 1;
			continue;
		}
		s = s + 2;
	}
	return to_string(s) + ":" + log;
}
run(300);
)JAI", "", "a boundary on the main path and one in an inner arm");
			check(kept.counters.entries > 0 && kept.listing.find("-- open loop") == std::string::npos, "a boundary on the main path and one in an inner arm: the loop closes and enters");
		});
	}
};

}
using branch_local_kill_tests = jai::foundry::tests::branch_local_kill_tests;
FOUNDRY_REGISTER(branch_local_kill_tests)
