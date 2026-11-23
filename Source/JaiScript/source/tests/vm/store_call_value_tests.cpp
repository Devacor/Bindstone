// `a[i] = f(x);` lands the call's result in a compiler temp, so a block's door lands it in a cell and the store runs in the
// block. The general form keeps its order: the call runs before the store's container and index are read, and a result that
// is no int or float goes back to the stack and is stored from there, as a temporary. Every case holds both backends to one
// text, proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class store_call_value_tests : public suite {
public:
	store_call_value_tests() : suite("Store Call Value") {}

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

	// the door's callee: a body the inliner keeps a call (its own loop over a local array)
	static constexpr const char* k_door = R"JAI(
function weigh(int a, int b) -> int {
	array<int> parts = [a, b, a + b];
	int s = 0;
	for (auto v : parts) { s += v; }
	return s & 255;
}
)JAI";

	void forge_tests() override {
		test("a_call_as_an_element_stores_value_runs_in_the_block", [this]() {
			const outcome vm = both(std::string(k_door) + R"JAI(
class Wash {
public:
	array<array<int>> remaps = [];
	array<int> levels = [];
	Wash() {
		for (int k = 0; k < 6; ++k) { remaps.push([]); }
		for (int k = 0; k < 64; ++k) { remaps[1].push(-1); remaps[2].push(-1); }
		for (int k = 0; k < 400; ++k) { levels.push(1 + (k & 1)); }
	}
	int remapped(int level, int p) {
		auto& table = remaps[level];
		int m = table[p];
		if (m >= 0) { return m; }
		m = (p * 7 + level) & 63;
		table[p] = m;
		return m;
	}
	void pass(array<int>& pix) {
		for (int k = 0; k < levels.size(); ++k) { pix[k] = remapped(levels[k], pix[k]); }
	}
}
function fill(array<int>& pix, array<float>& shade) {
	for (int i = 0; i < 400; ++i) { pix[i] = weigh(pix[i], i); }
	for (int i = 0; i < 400; ++i) { shade[i] = weigh(i, 3); }
}
function run() -> string {
	array<int> pix = [];
	for (int i = 0; i < 400; ++i) { pix.push(i & 63); }
	array<float> shade = [];
	for (int i = 0; i < 400; ++i) { shade.push(0.0); }
	Wash w = Wash();
	for (int r = 0; r < 3; ++r) { w.pass(pix); }
	fill(pix, shade);
	int s = 0; float fs = 0.0;
	for (int i = 0; i < 400; ++i) { s = s + pix[i] * (i + 1); fs = fs + shade[i]; }
	return to_string(s) + " " + to_string(fs);
}
run();
)JAI", "", "stores of a call's result");
			check(vm.counters.entries > 0, "stores of a call's result: the loops enter their blocks");
			check_eq(0ull, vm.counters.boundaries, "stores of a call's result: the store is no boundary statement");
			for (const char* fn : { "pass", "fill" }) {
				const std::string part = listing_of(vm.listing, fn);
				check(part.find("-- cell loop") != std::string::npos && part.find("-- open loop") == std::string::npos, std::string("stores of a call's result: the loops of ") + fn + " close");
			}
		});

		test("a_callee_entered_through_the_lean_door_stores_its_result", [this]() {
			// by-value scalars as they stand and a T& over a local the block holds no cell for enter the callee's frame at the arguments
			// instruction (the lean door); an int for a float parameter converts in the binder (the general door): each result lands in
			// the store's temp and the store runs in the block
			const outcome vm = both(R"JAI(
int g3(int x, int y, int z) { switch (x) { default: return (x * 3 + y + z) & 255; } }
float gf(float a) { switch (1) { default: return a + 0.5; } }
int at(array<int>& src, int i) { switch (i) { default: return src[i & 3] + i; } }
function run(int n) -> string {
	array<int> a = []; array<float> f = []; array<int> src = [1, 2, 3, 4];
	for (int i = 0; i < 64; ++i) { a.push(i); f.push(0.0); }
	for (int i = 0; i < n; ++i) { int k = i & 63; a[k] = g3(i, a[k], 2); }
	for (int i = 0; i < n; ++i) { int k = i & 63; f[k] = gf(i); }
	for (int i = 0; i < n; ++i) { int k = (i + 1) & 63; a[k] = at(src, i); }
	int s = 0; float fs = 0.0;
	for (int i = 0; i < 64; ++i) { s = s + a[i] * (i + 1); fs = fs + f[i]; }
	return to_string(s) + " " + to_string(fs);
}
run(200);
)JAI", "", "stores of a lean callee's result");
			check(vm.counters.lean_calls >= 380 && vm.counters.lean_calls <= 400, "stores of a lean callee's result: the two by-value and reference loops enter lean (" + std::to_string(vm.counters.lean_calls) + ")");
			check_eq(0ull, vm.counters.boundaries, "stores of a lean callee's result: the store is no boundary statement");
			const std::string part = listing_of(vm.listing, "run");
			check(part.find("-- open loop") == std::string::npos, "stores of a lean callee's result: every loop closes");
		});

		test("the_call_runs_before_the_container_and_the_index_are_read", [this]() {
			// through the door (weigh's shape keeps the callee a call) and through a graft (a one-line leaf): a callee that grows, shrinks or
			// replaces the container, or writes the index, is seen by the store
			for (const char* shape : { "array<int> scratch = [n, n]; for (auto v : scratch) { n = n + v - v; }", "" }) {
				const std::string defs = std::string(R"JAI(
array<int> A = [];
int I = 0;
function grow(int n) -> int { )JAI") + shape + R"JAI( A.push(n); return n * 2; }
function shrink(int n) -> int { )JAI" + shape + R"JAI( A.pop(); return n; }
function swap_in(int n) -> int { )JAI" + shape + R"JAI( A = [n, n, n, n]; return 9; }
function bump(int n) -> int { )JAI" + shape + R"JAI( I = I + 1; return n + 100; }
function shift(int& k, int n) -> int { )JAI" + shape + R"JAI( k = k + 1; return n + 200; }
function show() -> string { string out = ""; for (int i = 0; i < A.size(); ++i) { out += to_string(A[i]) + ","; } return out + " I=" + to_string(I); }
)JAI";
				const std::string what = *shape ? "through the door" : "through a graft";
				both(defs + R"JAI(
function run() -> string {
	A = [1, 2, 3];
	string out = "";
	for (int i = 3; i < 6; ++i) { A[i] = grow(i); }
	out += show() + "|";
	try { for (int i = 0; i < 4; ++i) { A[A.size() - 1] = shrink(i); } } catch (err) { out += err + "|"; }
	out += show() + "|";
	A = [5, 6];
	for (int i = 0; i < 3; ++i) { A[i + 1] = swap_in(i); }
	out += show() + "|";
	A = [0, 0, 0, 0, 0, 0];
	I = 0;
	for (int i = 0; i < 4; ++i) { A[I] = bump(i); }
	out += show() + "|";
	int j = 0;
	for (int i = 0; i < 4; ++i) { A[j] = shift(j, i); }
	return out + show() + " j=" + to_string(j);
}
run();
)JAI", "", "a callee that changes the target, " + what);
			}
		});

		test("a_result_that_is_no_number_is_stored_as_the_temporary_it_is", [this]() {
			both(std::string(k_door) + R"JAI(
int COPIES = 0;
class Tile {
public:
	int v = 0;
	Tile(int v_) { v = v_; }
	Tile(Tile& other) { v = other.v; COPIES = COPIES + 1; }
}
function make(int n) -> Tile { Tile t = Tile(n + weigh(n, 1) * 0); return t; }
function name(int n) -> string { return "n" + to_string(n + weigh(n, 1) * 0); }
function half(int n) -> float { return (n + weigh(n, 1) * 0) * 0.5; }
function list(int n) -> array<int> { array<int> xs = [n, n + 1]; return xs; }
function any_of(int n) -> var {
	if ((n & 3) == 0) { return n * 2; }
	if ((n & 3) == 1) { return "s" + to_string(n); }
	if ((n & 3) == 2) { Tile t = Tile(n); return t; }
	return [n, n];
}
class Maker {
public:
	int made = 0;
	Tile tile(int n) { made = made + 1; Tile t = Tile(n + made); return t; }
	int count(int n) { made = made + 1; return n + made; }
}
function mixed() -> string {
	array<var> any = [];
	for (int i = 0; i < 12; ++i) { any.push(0); }
	for (int i = 0; i < 12; ++i) { any[i] = any_of(i); }
	Maker maker = Maker();
	array<Tile> tiles = [Tile(0), Tile(0), Tile(0)];
	array<int> counts = [0, 0, 0];
	for (int i = 0; i < 3; ++i) { tiles[i] = maker.tile(i); counts[i] = maker.count(i); }
	string out = to_string(any[0]) + any[1] + to_string(any[2].v) + to_string(any[3][0]) + to_string(any[4]) + any[9] + to_string(any[10].v) + to_string(any[11][1]);
	return out + " " + to_string(tiles[0].v + tiles[1].v + tiles[2].v) + " " + to_string(counts[0] + counts[1] + counts[2]) + " copies " + to_string(COPIES);
}
function run() -> string {
	string out = mixed() + "|";
	array<Tile> tiles = [Tile(0), Tile(0), Tile(0)];
	for (int i = 0; i < 3; ++i) { tiles[i] = make(i + 4); }
	out += to_string(tiles[0].v + tiles[1].v + tiles[2].v) + " copies " + to_string(COPIES) + "|";
	array<string> names = ["", "", ""];
	for (int i = 0; i < 3; ++i) { names[i] = name(i); }
	out += names[0] + names[1] + names[2] + "|";
	array<var> any = [0, 0, 0, 0];
	any[0] = make(7); any[1] = name(8); any[2] = half(9); any[3] = list(3);
	out += to_string(any[0].v) + " " + any[1] + " " + to_string(any[2]) + " " + to_string(any[3][1]) + " copies " + to_string(COPIES) + "|";
	array<int> ints = [0, 0, 0];
	try { for (int i = 0; i < 3; ++i) { ints[i] = half(i + 1); } } catch (err) { out += err + "|"; }
	array<float> floats = [0.0, 0.0, 0.0];
	for (int i = 0; i < 3; ++i) { floats[i] = weigh(i, 2); }
	out += to_string(floats[0] + floats[1] + floats[2]) + "|";
	map<string, int> m = {};
	for (int i = 0; i < 3; ++i) { m["k" + to_string(i)] = weigh(i, i); }
	out += to_string(m["k0"] + m["k1"] + m["k2"]);
	return out;
}
run();
)JAI", "", "objects, strings, floats, arrays and maps");
		});

		test("a_result_that_is_used_and_a_reference_result", [this]() {
			both(std::string(k_door) + R"JAI(
array<int> POOL = [10, 20, 30];
function pick(int i) -> int& { return POOL[i]; }
function run() -> string {
	array<int> a = [0, 0, 0, 0];
	int last = 0;
	for (int i = 0; i < 3; ++i) { last = (a[i] = weigh(i, 5)); }
	for (int i = 0; i < 3; ++i) { a[i + 1] = pick(i); }
	POOL[0] = 99;
	a[0] += weigh(1, 1);
	return to_string(a[0]) + "," + to_string(a[1]) + "," + to_string(a[2]) + "," + to_string(a[3]) + " " + to_string(last);
}
run();
)JAI", "", "a used store, a reference result, a compound");
		});
	}
};

}
using store_call_value_tests = jai::foundry::tests::store_call_value_tests;
FOUNDRY_REGISTER(store_call_value_tests)
