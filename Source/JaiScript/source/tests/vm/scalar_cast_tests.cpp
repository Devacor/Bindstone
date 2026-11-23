// cast<int>(e), cast<float>(e) and cast<bool>(e) land in a compiler temp on the general form (op_cast_temp) and run as a cell
// operation in a block (op_c_cast). Every case holds both backends to one text, proofs on and off, and the cases that name a
// block check that the vm entered it with no boundary statement.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class scalar_cast_tests : public suite {
public:
	scalar_cast_tests() : suite("Scalar Casts") {}

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

	// one text on both backends, proofs on and off (and under injected misses where the build carries the lever); the vm's proven run is returned
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

	void closes(const outcome& vm, const std::string& what) {
		check(vm.counters.entries > 0, what + ": the loop enters its block");
		check_eq(0ull, vm.counters.boundaries, what + ": no boundary statement");
		check(vm.listing.find("C_CAST") != std::string::npos, what + ": the cast is a cell operation");
		check(vm.listing.find("-- open loop") == std::string::npos, what + ": no loop stays open");
	}

	void forge_tests() override {
		test("every_scalar_pair_converts_as_the_cast_does", [this]() {
			both(R"JAI(
function show() -> string {
	float f = -2.75; float g = 2.75; float zero = 0.0; float nzero = -0.0; float nan = zero / zero;
	int i = 7; int z = 0; int big = 9007199254740993; int neg = -3;
	bool t = true; bool n = false;
	string out = "";
	out += to_string(cast<int>(f)) + " " + to_string(cast<int>(g)) + " " + to_string(cast<int>(i)) + " " + to_string(cast<int>(t)) + " " + to_string(cast<int>(n)) + "|";
	out += to_string(cast<float>(i)) + " " + to_string(cast<float>(big) == 9007199254740992.0) + " " + to_string(cast<float>(f)) + " " + to_string(cast<float>(t)) + " " + to_string(cast<float>(n)) + "|";
	out += to_string(cast<bool>(i)) + " " + to_string(cast<bool>(z)) + " " + to_string(cast<bool>(neg)) + " " + to_string(cast<bool>(f)) + " " + to_string(cast<bool>(zero)) + " " + to_string(cast<bool>(nzero)) + " " + to_string(cast<bool>(nan)) + " " + to_string(cast<bool>(t)) + " " + to_string(cast<bool>(n)) + "|";
	out += to_string(cast<int>(-0.99)) + " " + to_string(cast<int>(9.2e18)) + " " + to_string(cast<int>(-9.2e18)) + " " + to_string(cast<float>(3)) + " " + to_string(cast<bool>(0.5));
	return out;
}
show();
)JAI", "", "the nine pairs");
		});

		test("casts_in_a_loop_run_in_its_block", [this]() {
			const outcome vm = both(R"JAI(
function run(int n) -> string {
	array<int> pix = [];
	for (int k = 0; k < 64; ++k) { pix.push(0); }
	float px = 3.25; float step = 0.37;
	int total = 0; float ftotal = 0.0; int odd = 0;
	for (int y = 0; y < n; ++y) {
		float fx = px + step * y;
		int ix = cast<int>(fx);
		int u = cast<int>((fx - ix) * 16.0);
		float back = cast<float>(ix) * 0.5;
		bool on = cast<bool>(y & 1);
		if (on) { odd = odd + cast<int>(on); }
		pix[(ix * 4 + u) & 63] = cast<int>(back) + u;
		total = total + ix * 16 + u;
		ftotal = ftotal + back;
	}
	int sum = 0;
	for (int k = 0; k < 64; ++k) { sum = sum + pix[k] * (k + 1); }
	return to_string(total) + " " + to_string(ftotal) + " " + to_string(odd) + " " + to_string(sum);
}
run(200);
)JAI", "", "a pixel loop's casts");
			closes(vm, "a pixel loop's casts");
			check_eq(0ull, vm.counters.misses, "a pixel loop's casts: no miss");
		});

		test("a_float_no_int_holds_raises_the_casts_text_from_a_block", [this]() {
			const outcome vm = both(R"JAI(
function run(int n) -> string {
	float f = 1.5; int last = 0; int seen = 0;
	string out = "";
	try {
		for (int i = 0; i < n; ++i) {
			last = cast<int>(f);
			seen = i;
			f = f * 1000000.0;
		}
	} catch (err) { out = " " + err; }
	return to_string(last) + " " + to_string(seen) + out;
}
run(40);
)JAI", "", "an overflowing cast in a loop");
			check(vm.counters.entries > 0, "an overflowing cast in a loop: the block entered");
			check(vm.counters.misses > 0, "an overflowing cast in a loop: the cast missed onto the general form");
			check(vm.text.find("Cannot convert float to integer") != std::string::npos, "an overflowing cast in a loop: the checked conversion's text");
			both(R"JAI(
function one(float f) -> string { try { return to_string(cast<int>(f)); } catch (err) { return err; } }
float zero = 0.0;
one(1e300) + "|" + one(-1e300) + "|" + one(zero / zero) + "|" + one(1.0 / zero) + "|" + one(9223372036854775807.0) + "|" + one(-9223372036854775808.0);
)JAI", "", "the range's edges");
		});

		test("operands_that_are_no_scalar_keep_the_casts_own_rows", [this]() {
			both(R"JAI(
class Meter {
	int n = 0;
	Meter(int n_) { n = n_; }
	int to_int() { return n * 2; }
	float to_float() { return n * 0.5; }
	bool to_bool() { return n > 3; }
}
function show() -> string {
	string out = "";
	string s = "12"; string empty = ""; char c = 'a';
	Meter m = Meter(5); Meter small = Meter(1);
	try { out += to_string(cast<int>(s)); } catch (err) { out += err; }
	out += "|";
	try { out += to_string(cast<float>(s)); } catch (err) { out += err; }
	out += "|" + to_string(cast<bool>(s)) + " " + to_string(cast<bool>(empty));
	out += "|" + to_string(cast<int>(c)) + " " + to_string(cast<float>(c)) + " " + to_string(cast<bool>(c));
	out += "|" + to_string(cast<int>(m)) + " " + to_string(cast<float>(m)) + " " + to_string(cast<bool>(m)) + " " + to_string(cast<bool>(small));
	var v = null;
	try { out += "|" + to_string(cast<int>(v)); } catch (err) { out += "|" + err; }
	out += "|" + to_string(cast<bool>(v));
	return out;
}
show();
)JAI", "", "strings, chars, objects and null");
		});

		test("a_var_operand_of_any_kind_casts_in_a_loop", [this]() {
			const outcome vm = both(R"JAI(
function run() -> string {
	array<var> xs = [1, 2.75, true, -3.5, false, 40, "x", 6.5];
	string out = "";
	for (int i = 0; i < xs.size(); ++i) {
		var v = xs[i];
		try {
			int a = cast<int>(v);
			float b = cast<float>(v);
			bool c = cast<bool>(v);
			out += to_string(a) + "," + to_string(b) + "," + to_string(c) + ";";
		} catch (err) { out += err + ";"; }
	}
	return out;
}
run();
)JAI", "", "var operands");
			(void)vm;
			both(R"JAI(
function run(int n) -> string {
	var v = 0.5; int total = 0; float ft = 0.0;
	for (int i = 0; i < n; ++i) {
		total = total + cast<int>(v) + cast<int>(cast<bool>(v));
		ft = ft + cast<float>(total & 3);
		v = v + 0.75;
	}
	return to_string(total) + " " + to_string(ft);
}
run(100);
)JAI", "", "a var local's cast");
		});

		test("reference_operands_read_once", [this]() {
			both(R"JAI(
class Gauge {
	float level = 2.5;
	int reads = 0;
}
function through(float& r, int& k) -> int {
	int total = 0;
	for (int i = 0; i < 20; ++i) {
		total = total + cast<int>(r) + cast<int>(cast<float>(k) * 0.5);
		r = r + 0.5;
		k = k + 1;
	}
	return total;
}
function run() -> string {
	Gauge g = Gauge();
	array<float> fs = [1.5, 7.25];
	array<int> ks = [3, 9];
	int a = through(g.level, g.reads);
	int b = through(fs[1], ks[0]);
	return to_string(a) + " " + to_string(b) + " " + to_string(g.level) + " " + to_string(g.reads) + " " + to_string(fs[1]) + " " + to_string(ks[0]);
}
run();
)JAI", "", "casts through references");
		});

		test("a_cast_keeps_its_place_among_the_operands", [this]() {
			both(R"JAI(
function first(array<int>& a, float f, int i) -> string {
	try { return to_string(a[i] + cast<int>(f)); } catch (err) { return err; }
}
function second(array<int>& a, float f, int i) -> string {
	try { return to_string(cast<int>(f) + a[i]); } catch (err) { return err; }
}
function third(float f, int d) -> string {
	try { return to_string(cast<int>(f) / d + cast<int>(f * 1e300)); } catch (err) { return err; }
}
array<int> a = [4, 5];
first(a, 1e300, 9) + "|" + second(a, 1e300, 9) + "|" + first(a, 2.5, 1) + "|" + second(a, 1e300, 1) + "|" + third(8.5, 0) + "|" + third(8.5, 2);
)JAI", "", "which error comes first");
		});

		test("casts_as_arguments_conditions_indexes_and_arms", [this]() {
			const std::string weigh = R"JAI(
function weigh(int a, float b, bool c) -> int {
	if (c) { return a * 3 + cast<int>(b); }
	array<int> spread = [a, a + 1, a + 2];
	int s = 0;
	for (auto v : spread) { s += v; }
	return s;
}
)JAI";
			const outcome vm = both(weigh + R"JAI(
function run(int n) -> string {
	array<int> table = [5, 6, 7, 8];
	float f = 0.25; float g = 1.5; int total = 0; int hits = 0; bool on = false;
	for (int i = 0; i < n; ++i) {
		total = total + weigh(cast<int>(f), g, on);
		if (cast<int>(f) > 3) { hits = hits + 1; on = !on; }
		total = total + table[cast<int>(f) & 3];
		int pick = i > 4 ? cast<int>(f * 2.0) : cast<int>(f);
		total = total + pick;
		f = f + 0.45;
		g = g + cast<float>(pick) * 0.25;
	}
	return to_string(total) + " " + to_string(hits);
}
run(60);
)JAI", "", "casts in every position");
			closes(vm, "casts in every position");
			both(weigh + R"JAI(
function run(int n) -> string {
	float f = 0.25; int total = 0; int hits = 0;
	for (int i = 0; i < n; ++i) {
		total = total + weigh(cast<int>(f), cast<float>(i), cast<bool>(hits & 2));
		hits = hits + cast<int>(f > 3.0);
		f = f + 0.45;
	}
	return to_string(total) + " " + to_string(hits);
}
run(60);
)JAI", "", "every argument a cast");
		});

		test("stamps_and_other_targets_are_the_casts", [this]() {
			both(R"JAI(
enum Tone { low, mid, high }
function show() -> string {
	string out = "";
	auto a = cast<int>(2.75);
	try { a = 3.5; } catch (err) { out += err; }
	out += "|" + to_string(a);
	var v = cast<float>(4);
	out += "|" + reflect::type_of(v) + " " + reflect::type_of(cast<bool>(1)) + " " + reflect::type_of(cast<int>(true));
	try { out += "|" + to_string(cast<Tone>(7) == Tone.mid); } catch (err) { out += "|" + err; }
	out += "|" + to_string(cast<int>(Tone.high)) + " " + to_string(cast<Tone>(1) == Tone.mid);
	out += "|" + to_string(int(7.9)) + " " + to_string(float(3)) + " " + to_string(bool(0.0));
	return out;
}
show();
)JAI", "", "the result's type");
		});
	}
};

}
using scalar_cast_tests = jai::foundry::tests::scalar_cast_tests;
FOUNDRY_REGISTER(scalar_cast_tests)
