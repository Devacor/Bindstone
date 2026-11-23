// A store or a push into a table an alias of the same loop body takes elements of (`var& row = T[i]; ... T[j] = v;`) is no site
// of the block: every entry refuses a store into a table an alias pins, so the statement runs on the general form (a boundary,
// or a bail in an arm) and the loop enters its block. A refused outer loop shows in the entry counter: none for a lone loop,
// one per outer iteration where an inner loop keeps its own block. Every case holds both backends to one text, proofs on and off.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class pinned_table_store_tests : public suite {
public:
	pinned_table_store_tests() : suite("Pinned Table Stores") {}

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

	void enters(const std::string& script, const std::string& expected, const std::string& what, unsigned long long most_entries) {
		const outcome vm = both(script, expected, what);
		check(vm.counters.entries > 0 && vm.counters.entries <= most_entries, what + ": the loop enters its block (" + std::to_string(vm.counters.entries) + " entries)");
		check(vm.listing.find("-- open loop") == std::string::npos, what + ": the loop closes");
	}

	void forge_tests() override {
		test("a_store_into_the_aliased_table_runs_on_the_general_form", [this]() {
			// a scalar stored beside the arrays the alias reads: a store site by its value, refused at every entry while it is one
			enters(R"JAI(
function run(int n) -> string {
	var T = [[1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12], 0];
	int acc = 0; int i = 0;
	while (i < n) {
		var& row = T[i % 4];
		acc = acc + row[0] * 3 + row[1];
		T[4] = i;
		acc = acc + row[2] + i * 2;
		i = i + 1;
	}
	return to_string(acc) + ":" + to_string(T[4]);
}
run(60);
)JAI", "", "a scalar store on the main path", 4);
			enters(R"JAI(
function run(int n) -> string {
	var T = [[1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12], 0];
	int acc = 0; int i = 0;
	while (i < n) {
		var& row = T[i % 4];
		acc = acc + row[0] * 3 + row[1];
		if ((i % 13) == 12) { T[4] = T[4] + 1; T.push(i); }
		acc = acc + row[2] + i * 2;
		i = i + 1;
	}
	return to_string(acc) + ":" + to_string(T[4]) + ":" + to_string(T.size());
}
run(60);
)JAI", "", "a scalar store and a push in an arm", 12);
			enters(R"JAI(
function run(int n) -> string {
	var T = [[1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]];
	int acc = 0; int i = 0;
	while (i < n) {
		var& row = T[i % 4];
		acc = acc + row[0] * 3 + row[1];
		T[(i + 1) % 4] = [i, i + 1, i + 2];
		acc = acc + row[2] + i;
		i = i + 1;
	}
	return to_string(acc) + ":" + to_string(T[0][0]) + "," + to_string(T[3][2]);
}
run(60);
)JAI", "", "a store on the main path", 4);
			enters(R"JAI(
function run(int n) -> string {
	var T = [[1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]];
	var SET = [0, 0, 0, 0];
	int acc = 0; int i = 0;
	while (i < n) {
		int k = i % 4;
		if (SET[k] == 0) { T[k] = [k * 10, k * 10 + 1, k * 10 + 2]; SET[k] = 1; }
		var& row = T[k];
		int y = 0;
		while (y < 3) { acc = acc + row[y] + y; y = y + 1; }
		if ((i % 17) == 16) { SET[k] = 0; }
		i = i + 1;
	}
	return to_string(acc);
}
run(120);
)JAI", "", "a cache-fill arm ahead of the alias", 40);
		});

		test("the_alias_sees_what_the_store_wrote", [this]() {
			// the alias is a reference to the element: a store that replaces the element it names, or the one it names next, is what it reads
			enters(R"JAI(
function run(int n) -> string {
	var T = [[1, 2], [3, 4], [5, 6]];
	int acc = 0; int i = 0;
	while (i < n) {
		int k = i % 3;
		var& row = T[k];
		acc = acc + row[0];
		T[k] = [i * 2, i * 2 + 1];
		acc = acc + row[0] * 100 + row[1];
		i = i + 1;
	}
	return to_string(acc) + ":" + to_string(T[1][1]);
}
run(40);
)JAI", "", "a store replacing the aliased element", 4);
			both(R"JAI(
function run(int n) -> string {
	var T = [[1, 2], [3, 4], [5, 6]];
	int acc = 0; int i = 0;
	try {
		while (i < n) {
			var& row = T[i % 3];
			T[(i + 1) % 3] = 7;
			acc = acc + row[1];
			i = i + 1;
		}
	} catch (err) { return "caught:" + to_string(acc) + ":" + to_string(i); }
	return "ok:" + to_string(acc);
}
run(9);
)JAI", "", "a store that leaves the next element no array");
		});

		test("a_push_onto_the_aliased_table", [this]() {
			both(R"JAI(
class P { public: int v = 0; P(int v_) { v = v_; } }
function run(int n) -> string {
	array<P> pool = [P(1), P(2), P(3)];
	int acc = 0;
	for (int i = 0; i < n; ++i) {
		acc = acc + pool[i % pool.size()].v;
		if ((i % 5) == 4) { pool.push(P(i)); }
		acc = acc + pool[i % 3].v * 2;
	}
	return to_string(acc) + ":" + to_string(pool.size());
}
run(80);
)JAI", "", "a push between member reads of elements");
			both(R"JAI(
function run(int n) -> string {
	var T = [[1, 2], [3, 4]];
	int acc = 0; int i = 0;
	while (i < n) {
		var& row = T[i % T.size()];
		acc = acc + row[0];
		if ((i % 4) == 3) { T.push([i, i + 1]); }
		acc = acc + row[1];
		i = i + 1;
	}
	return to_string(acc) + ":" + to_string(T.size());
}
run(40);
)JAI", "", "a push while an alias is live");
		});

		test("a_call_stored_into_the_aliased_table", [this]() {
			// GLOOM's column loop: the cache-fill arm stores a call's result into the table the pixel loop's alias reads
			enters(R"JAI(
function build_strip(int k) -> var { return [k * 10, k * 10 + 1, k * 10 + 2, k * 10 + 3]; }
function run(int cols, int vh) -> string {
	var strips = [0, 0, 0, 0, 0, 0]; var strip_set = [false, false, false, false, false, false]; var pix = [0, 0, 0, 0, 0, 0, 0, 0];
	int acc = 0; int x = 0;
	while (x < cols) {
		int sidx = (x * 7) % 6;
		if (!strip_set[sidx]) {
			strips[sidx] = build_strip(sidx);
			strip_set[sidx] = true;
		}
		auto& strip = strips[sidx];
		int y = 0; int gi = x;
		while (y < vh) { int c = strip[y]; pix[gi % 8] = c; acc = acc + c; gi = gi + 2; y = y + 1; }
		x = x + 1;
	}
	return to_string(acc) + ":" + to_string(pix[0]) + "," + to_string(pix[5]);
}
run(200, 4);
)JAI", "", "a call's result stored in the cache-fill arm", 40);
		});
	}
};

}
using pinned_table_store_tests = jai::foundry::tests::pinned_table_store_tests;
FOUNDRY_REGISTER(pinned_table_store_tests)
