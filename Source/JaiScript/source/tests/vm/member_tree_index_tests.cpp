// `o.arr[tree]`: an element of a member container at an arithmetic tree. The member's value lands in a temp ahead of the tree, the
// order the stack form reads them in, and in a block the member is the read's own container site, so the statement is no
// boundary. Every case holds both backends to one text, proofs on and off, and under injected misses where the build has them.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class member_tree_index_tests : public suite {
public:
	member_tree_index_tests() : suite("Member Tree Index") {}

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

	static constexpr const char* k_art = R"JAI(
class Art {
public:
	int w = 8; int h = 6;
	array<int> pix = [];
	Art() { for (int i = 0; i < 48; ++i) { pix.push((i * 7) & 15); } }
}
)JAI";

	void forge_tests() override {
		test("the_read_is_a_site_of_the_block", [this]() {
			// GLOOM's view model loop: a handle's and a value instance's array at `row + (x - x0) * o.w / pw`
			const outcome vm = both(std::string(k_art) + R"JAI(
function draw(shared_ptr<Art> art, array<int>& out, int rounds) -> int {
	int pw = 16; int x0 = 3; int s = 0;
	for (int r = 0; r < rounds; ++r) {
		int row = (r % 6) * art.w;
		for (int x = 3; x < 19; ++x) {
			int code = art.pix[row + (x - x0) * art.w / pw];
			if (code > 0) { out[x] = code; s = s + code; }
		}
	}
	return s;
}
function by_value(int rounds) -> int {
	Art art = Art();
	int s = 0;
	for (int r = 0; r < rounds; ++r) {
		for (int x = 0; x < 8; ++x) { int code = art.pix[(r % 6) * art.w + x]; s = s + code * (x + 1); }
	}
	return s;
}
function run() -> string {
	shared_ptr<Art> art = new Art();
	array<int> out = [];
	for (int i = 0; i < 24; ++i) { out.push(0); }
	return to_string(draw(art, out, 40)) + " " + to_string(by_value(40)) + " " + to_string(out[5]);
}
run();
)JAI", "", "a member array at an arithmetic tree");
			check(vm.counters.entries > 0, "a member array at an arithmetic tree: the loops enter their blocks");
			check_eq(0ull, vm.counters.boundaries, "a member array at an arithmetic tree: the read is no boundary statement");
			for (const char* fn : { "draw", "by_value" }) {
				const std::string part = listing_of(vm.listing, fn);
				check(part.find("-- cell loop") != std::string::npos && part.find("-- open loop") == std::string::npos && part.find("C_BOUNDARY") == std::string::npos, std::string("a member array at an arithmetic tree: the loops of ") + fn + " close without a boundary");
			}
		});

		test("the_member_is_read_ahead_of_the_tree", [this]() {
			// what the stack form does: the member's value first, then the index. A null base raises the member's text ahead of the tree's
			// zero divisor; a tree operand that replaces the member indexes the array read before it; a tree operand that raises leaves the
			// member read done (its getter counted)
			both(std::string(k_art) + R"JAI(
int READS = 0;
class Lazy {
public:
	array<int> data = [5, 6, 7, 8, 9, 10, 11, 12];
	array<int> _get_pix() { READS = READS + 1; return data; }
}
function swap_pix(shared_ptr<Art> art) -> int { art.pix = [100, 200, 300, 400, 500, 600, 700, 800]; return 0; }
function boom(int i) -> int { if (i == 3) { throw "boom at " + to_string(i); } return 0; }
function run() -> string {
	string out = "";
	shared_ptr<Art> none = null;
	int zero = 0;
	try { int v = none.pix[4 / zero + 1]; out += to_string(v); } catch (err) { out += "1:" + to_string(err) + "|"; }
	shared_ptr<Art> art = new Art();
	try { int v = art.pix[4 / zero + 1]; out += to_string(v); } catch (err) { out += "2:" + to_string(err) + "|"; }
	try { int v = art.pix[art.w * 100 + 1]; out += to_string(v); } catch (err) { out += "3:" + to_string(err) + "|"; }
	int before = art.pix[swap_pix(art) + 1 * 2];
	int after = art.pix[0 + 1 * 2];
	out += "4:" + to_string(before) + "," + to_string(after) + "|";
	Lazy lazy = Lazy();
	int s = 0;
	for (int i = 0; i < 6; ++i) { int v = lazy.pix[READS + i * 0]; s = s + v; }
	out += "5:" + to_string(s) + " reads " + to_string(READS) + "|";
	try { for (int i = 0; i < 6; ++i) { int v = lazy.pix[boom(i) + i * 1]; s = s + v; } } catch (err) { out += "6:" + to_string(err) + " reads " + to_string(READS) + "|"; }
	return out + to_string(s);
}
run();
)JAI", "", "a null base, a zero divisor, an index out of range, a tree that replaces the member, a getter, a tree that raises");
		});

		test("the_member_replaced_between_iterations_and_a_subclass_field", [this]() {
			// a callee that replaces the member between iterations (through the door: the site resolves again at the landing), a loop
			// that misses on an index out of range, and a subclass that declares the field again with another element type
			both(std::string(k_art) + R"JAI(
class Base {
public:
	int w = 2;
	array<int> pix = [1, 2, 3, 4, 5, 6, 7, 8];
}
class Wide : Base {
public:
	float w = 1.0;
	array<float> pix = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5];
}
function replace(shared_ptr<Art> art, int i) -> int {
	if (i == 5) { art.pix = []; for (int k = 0; k < 48; ++k) { art.pix.push(1000 + k); } }
	return 1;
}
function replaced(int n) -> int {
	shared_ptr<Art> art = new Art();
	int s = 0;
	for (int i = 0; i < n; ++i) {
		int v = art.pix[i * 2 + 1];
		s = s + v;
		s = s + replace(art, i);
	}
	return s;
}
function past_the_end(int n) -> string {
	shared_ptr<Art> art = new Art();
	int s = 0; int i = 0;
	try { while (i < n) { int v = art.pix[i * art.w + 3]; s = s + v; i = i + 1; } } catch (err) { return "caught at " + to_string(i) + ": " + to_string(s); }
	return "ok " + to_string(s);
}
function sum(var& o, int n) -> string {
	var s = 0;
	for (int i = 0; i < n; ++i) { var v = o.pix[i * 2 + 1]; s = s + v; }
	return to_string(s);
}
function run() -> string {
	Base b = Base(); Wide w = Wide();
	string out = to_string(replaced(12)) + "|" + past_the_end(12) + "|";
	for (int r = 0; r < 3; ++r) { out += sum(b, 3) + "," + sum(w, 3) + ";"; }
	return out;
}
run();
)JAI", "", "a member replaced by a callee, an index past the end, a field a subclass declares again");
		});

		test("the_read_in_other_positions", [this]() {
			// a var declaration, an operand of an expression, a condition, an argument, a reference argument and a store beside it
			both(std::string(k_art) + R"JAI(
function bump(int& cell) -> int { cell = cell + 100; return cell; }
function twice(int v) -> int { return v * 2; }
function run() -> string {
	shared_ptr<Art> art = new Art();
	int s = 0; int hits = 0;
	for (int i = 0; i < 20; ++i) {
		var a = art.pix[i * 2 + 1];
		s = s + art.pix[i + art.w * 2] + a;
		if (art.pix[i * 2 + (i & 1)] > 7) { hits = hits + 1; }
		s = s + twice(art.pix[i + i / 2]);
		art.pix[i + 20 * 1] = s & 255;
	}
	int last = bump(art.pix[2 * 3 + 1]);
	return to_string(s) + " " + to_string(hits) + " " + to_string(last) + " " + to_string(art.pix[7]) + " " + to_string(art.pix[25]);
}
run();
)JAI", "", "a var declaration, an operand, a condition, an argument, a reference argument, a store");
		});
	}
};

}
using member_tree_index_tests = jai::foundry::tests::member_tree_index_tests;
FOUNDRY_REGISTER(member_tree_index_tests)
