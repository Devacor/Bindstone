// `r.size()` on a receiver whose kind the run picks (a reference local, a var) is a size site of a block when the entry finds an
// array there: the loop it bounds closes. Any other receiver (a string, a map, an instance with its own size, null) refuses the
// entry and the general form makes the call. Every case holds both backends to one text, proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class open_size_tests : public suite {
public:
	open_size_tests() : suite("Open Receiver Size") {}

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
		test("a_loop_bounded_by_a_reference_locals_size_closes", [this]() {
			const outcome vm = both(R"JAI(
array<int> PIX = [];
class Feel {
public:
	array<int> mask = [];
	void wash(int base) {
		auto& pix = PIX;
		for (int i = 0; i < pix.size(); ++i) {
			int level = mask[i] + base;
			if (level > 0) { pix[i] = pix[i] + level; }
		}
	}
}
function typed(int base) -> int {
	array<int>& pix = PIX;
	int s = 0;
	for (int i = 0; i < pix.size(); ++i) { s = s + pix[i] * base; }
	return s;
}
function open(int base) -> int {
	var& pix = PIX;
	int s = 0;
	for (int i = 0; i < pix.size(); ++i) { s = s + pix[i] + base; }
	return s;
}
function held(int base) -> int {
	var pix = PIX;
	int s = 0;
	int i = 0;
	while (i < pix.size()) { s = s + pix[i] - base; i = i + 1; }
	return s;
}
function run() -> string {
	Feel f = Feel();
	for (int i = 0; i < 300; ++i) { PIX.push(i & 31); f.mask.push((i & 3) - 1); }
	f.wash(1);
	return to_string(typed(3)) + " " + to_string(open(2)) + " " + to_string(held(1));
}
run();
)JAI", "", "a reference local's size");
			check(vm.counters.entries > 0, "a reference local's size: the loops enter their blocks");
			for (const char* fn : { "wash", "typed", "open", "held" }) {
				const std::string part = listing_of(vm.listing, fn);
				check(part.find("-- cell loop") != std::string::npos && part.find("-- open loop") == std::string::npos, std::string("a reference local's size: the loop of ") + fn + " closes");
			}
		});

		test("the_size_is_read_live", [this]() {
			both(R"JAI(
array<int> DATA = [1, 2, 3];
function grow() -> string {
	auto& d = DATA;
	int steps = 0;
	for (int i = 0; i < d.size(); ++i) {
		if (d.size() < 40) { d.push(d[i] + i); }
		steps = steps + 1;
	}
	return to_string(steps) + ":" + to_string(d.size());
}
function shrink() -> string {
	var& d = DATA;
	int steps = 0;
	for (int i = 0; i < d.size(); ++i) {
		if ((i & 1) == 0 && d.size() > 3) { d.pop(); }
		steps = steps + d[i];
	}
	return to_string(steps) + ":" + to_string(d.size());
}
function run() -> string { return grow() + " " + shrink(); }
run();
)JAI", "", "a size the body changes");
		});

		test("a_receiver_that_is_no_array_runs_the_general_call", [this]() {
			both(R"JAI(
int CALLS = 0;
class Bag {
public:
	int n = 0;
	Bag(int n_) { n = n_; }
	int size() { CALLS = CALLS + 1; n = n - 1; return n; }
}
function count(var& r) -> int {
	int steps = 0;
	for (int i = 0; i < r.size(); ++i) { steps = steps + 1; }
	return steps;
}
function length_of(var& r) -> int {
	int steps = 0;
	for (int i = 0; i < r.length(); ++i) { steps = steps + 1; }
	return steps;
}
function run() -> string {
	string out = "";
	string text = "hello there";
	map<string, int> m = {"a": 1, "b": 2, "c": 3};
	array<int> xs = [4, 5, 6, 7];
	Bag bag = Bag(9);
	out += to_string(count(text)) + "," + to_string(count(m)) + "," + to_string(count(xs)) + "," + to_string(count(bag)) + " calls " + to_string(CALLS) + "|";
	out += to_string(length_of(text)) + "|";
	try { out += to_string(length_of(xs)); } catch (err) { out += err; }
	out += "|";
	var nothing = null;
	try { out += to_string(count(nothing)); } catch (err) { out += err; }
	out += "|";
	var number = 7;
	try { out += to_string(count(number)); } catch (err) { out += err; }
	return out;
}
run();
)JAI", "", "strings, maps, instances, null and numbers as the receiver");
		});

		test("a_receiver_the_body_rebinds_or_declares", [this]() {
			both(R"JAI(
function make(int n) -> array<int> { array<int> xs = []; for (int i = 0; i < n; ++i) { xs.push(i); } return xs; }
function rebound() -> int {
	var cur = make(6);
	int steps = 0;
	for (int i = 0; i < cur.size(); ++i) {
		if (i == 2) { cur = make(12); }
		if (i == 9) { cur = "four"; }
		steps = steps + 1;
	}
	return steps;
}
rebound();
)JAI", "", "a receiver the body rebinds");
			// a receiver a boundary statement of the body declares (an array from a call, a var from a literal) is no site's, and a var a
			// call landed in is a cell: its size is the general form's, and the block enters
			for (const char* declared : { "array<int> held = make(i)", "var held = [i, i, i]", "var held = make(i)" }) {
				const outcome arm = both(std::string(R"JAI(
function make(int n) -> array<int> { array<int> xs = []; xs.push(n); xs.push(n); xs.push(n); return xs; }
function arm(int n) -> int {
	int total = 0;
	for (int i = 0; i < n; ++i) {
		if ((i % 9) == 4) {
			)JAI") + declared + R"JAI(;
			total = total + 2;
			total = total + held.size();
			total = total + 3;
			continue;
		}
		total = total + 1;
	}
	return total;
}
arm(200);
)JAI", "", std::string("a receiver a branch declares, ") + declared);
				const std::string part = listing_of(arm.listing, "arm");
				check(part.find("-- cell loop") != std::string::npos && part.find("-- open loop") == std::string::npos, std::string("a receiver a branch declares, ") + declared + ": the loop closes");
				check(arm.counters.entries > 0, std::string("a receiver a branch declares, ") + declared + ": the block enters");
			}
		});
	}
};

}
using open_size_tests = jai::foundry::tests::open_size_tests;
FOUNDRY_REGISTER(open_size_tests)
