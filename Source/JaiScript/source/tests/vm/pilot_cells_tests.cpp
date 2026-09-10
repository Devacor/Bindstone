// A closed typed loop's scalar slots run as raw cells inside one block. The contract controls: every
// shape lands the same on the interpreter and the vm: a miss right after the body-defined cell is
// written and after the first pixel store, late overflow with prior stores retained, a budget stop
// inside the cell loop, proofs off, zero and one iteration, a loop that is not closed, chained index
// stores, a float slot fed by int arithmetic, interleaved coroutines, the hoisted containers.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/vm/disassembler.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class pilot_cells_tests : public suite {
public:
	pilot_cells_tests() : suite("Cell Loops") {}

	// The render column loop's shape over a var pixel array (so a string element can sit where a
	// store lands) and a var strip table. Returns "<gi*1000+y>:<weighted pixel sum>" or "throw:<text>".
	static constexpr const char* k_prelude = R"(
		var PIX = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
		var STRIPS = [[7, 8, 9, 10]];
		function paint(var& pix, var& strip, int cy0, int cy1, int vw, int px0, int tstep, int tacc0, int k) -> int {
			int gi = cy0 * vw + px0;
			int tacc = tacc0;
			int y = cy0;
			int acc = 0;
			while (y < cy1) {
				int c = strip[tacc >> 11];
				pix[gi] = c;
				pix[gi + 1] = c;
				tacc += tstep;
				gi += vw;
				acc += k;
				y = y + 1;
			}
			return gi * 1000 + y;
		}
		function weighted() -> int { int s = 0; for (int i = 0; i < PIX.size(); ++i) { if (type_of(PIX[i]) == "int") { s += PIX[i] * (i + 1); } } return s; }
		function run(int cy0, int cy1, int tstep, int k) -> string {
			var& s = STRIPS[0];
			string out = "";
			try { out = to_string(paint(PIX, s, cy0, cy1, 2, 0, tstep, 0, k)); }
			catch (err) { out = "throw:" + err; }
			return out + ":" + to_string(weighted());
		}
	)";

	std::string run_on(bool vm, const std::string& script, bool proofs = true) {
		auto e = engine::make();
		if (vm) { e->set_backend(backend_type::vm); }
		stdlib::register_all(*e);
		if (!proofs) { e->type_proofs(false); }
		try { return e->execute(script).as_string(); }
		catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void both(const std::string& script, const std::string& expected, const char* label, bool proofs = true) {
		const std::string interp = run_on(false, script, proofs);
		const std::string vm = run_on(true, script, proofs);
		check_eq(interp, vm, std::string(label) + ": both backends");
		if (!expected.empty()) { check_eq(expected, vm, std::string(label) + ": expected"); }
	}

	void forge_tests() override {
		test("column_loop_lands_the_same_on_both_backends", [this]() {
			both(std::string(k_prelude) + "run(0, 4, 2048, 1);", "8004:326", "four iterations");
		});
		test("zero_and_one_iteration", [this]() {
			both(std::string(k_prelude) + "run(4, 4, 2048, 1);", "8004:0", "zero iterations");
			both(std::string(k_prelude) + "run(0, 1, 2048, 1);", "2001:21", "one iteration");
		});
		test("late_overflow_keeps_prior_stores_and_the_text", [this]() {
			// acc += k with k = 2^62: the second addition overflows after the second iteration's stores
			both(std::string(k_prelude) + "run(0, 4, 2048, 4611686018427387904);", "", "late overflow");
			const std::string vm = run_on(true, std::string(k_prelude) + "run(0, 4, 2048, 4611686018427387904);");
			check_true(vm.rfind("throw:", 0) == 0, "the overflow raises: " + vm);
			check_true(vm.find(":77") != std::string::npos, "two iterations of stores retained: " + vm);
		});
		test("miss_right_after_the_body_defined_cell_is_written", [this]() {
			// the third strip element is a string: the cell load misses at the third iteration after two
			// complete iterations; the flushed state must be what the general declaration then rejects
			const std::string script = std::string(k_prelude) + "STRIPS[0][2] = \"x\"; run(0, 4, 2048, 1);";
			both(script, "", "string element on the third iteration");
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) == 0, "the declaration rejects the string: " + vm);
			check_true(vm.find(":77") != std::string::npos, "two iterations of stores retained: " + vm);
		});
		test("miss_at_the_first_pixel_store_after_c_is_defined", [this]() {
			// a string sits where the third iteration's first store lands (a hetero target of another kind):
			// the cell store misses AFTER c was defined in its cell; the flush must materialize c before the
			// general store runs, and the general store then writes the int over the string
			const std::string script = std::string(k_prelude) + "PIX[4] = \"s\"; run(0, 4, 2048, 1);";
			both(script, "8004:326", "store miss after the cell definition of c");
		});
		test("miss_at_the_second_pixel_store", [this]() {
			const std::string script = std::string(k_prelude) + "PIX[5] = \"s\"; run(0, 4, 2048, 1);";
			both(script, "8004:326", "second store misses, first already landed raw");
		});
		test("getter_backed_reference_container_runs_the_getter_identically", [this]() {
			struct probe_box { script_int reads = 0; std::vector<script_int> values{0, 0, 0, 0, 0, 0, 0, 0}; };
			for (bool proofs : {true, false}) {
				std::string seen[2];
				for (int backend = 0; backend < 2; ++backend) {
					probe_box box;
					auto e = engine::make();
					if (backend == 1) { e->set_backend(backend_type::vm); }
					e->type_proofs(proofs);
					stdlib::register_all(*e);
					dynamic_binder<probe_box>(*e, "probe_box")
						.property("values", [](probe_box& b) { ++b.reads; return b.values; }, nullptr)
						.build();
					e->add_global_ref("box", box);
					const char* src = R"(
						var STRIPS = [[7, 8, 9, 10]];
						function paint(var& pix, var& strip, int cy0, int cy1, int vw, int px0, int tstep) -> int {
							int gi = cy0 * vw + px0; int tacc = 0; int y = cy0;
							while (y < cy1) { int c = strip[tacc >> 11]; pix[gi] = c; pix[gi + 1] = c; tacc += tstep; gi += vw; y = y + 1; }
							return gi * 1000 + y;
						}
						var& s = STRIPS[0];
						paint(box.values, s, 0, 3, 2, 0, 2048);
					)";
					std::string got;
					try { got = std::to_string(e->execute(src).as_int()); } catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
					seen[backend] = got + "|reads=" + std::to_string(box.reads);
				}
				check_eq(seen[0], seen[1], std::string("getter-backed container, proofs ") + (proofs ? "on" : "off"));
			}
		});
		test("proofs_off_lands_the_same", [this]() {
			both(std::string(k_prelude) + "run(0, 4, 2048, 1);", "8004:326", "proofs off", /*proofs*/ false);
		});
		test("budget_exhaustion_inside_the_cell_loop_raises_the_same_text", [this]() {
			// warmed by a short direct call under the default budget; only the long direct call runs
			// under the small budget; the loop is a closed arithmetic loop with no bounded writes
			const char* spin = "function spin(int n, int k) -> int { int y = 0; int acc = 0; while (y < n) { acc += k; y = y + 1; } return acc; }";
			std::string seen[2];
			for (int backend = 0; backend < 2; ++backend) {
				auto e = engine::make();
				if (backend == 1) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				e->execute(spin);
				script_value fn = e->get_variable("spin");
				std::vector<script_value> warm{ script_value(static_cast<script_int>(10), e.get()), script_value(static_cast<script_int>(1), e.get()) };
				auto warmed = fn.as_function()(warm);
				check_true(warmed.has_value() && warmed.value().as_int() == 10, "the warm call runs the loop ten times");
				std::vector<script_value> big{ script_value(static_cast<script_int>(200000000), e.get()), script_value(static_cast<script_int>(1), e.get()) };
				e->execution_budget(0.00001);
				auto result = fn.as_function()(big);
				seen[backend] = result.has_value() ? std::to_string(result.value().as_int()) : std::string("throw:") + std::string(result.message());
			}
			check_true(seen[0].rfind("throw:", 0) == 0, "interpreter raises the budget: " + seen[0]);
			check_true(seen[1].rfind("throw:", 0) == 0, "vm raises the budget: " + seen[1]);
			check_eq(seen[0], seen[1], "the same budget text");
		});
		test("values_survive_the_loop_exit_after_a_budget_free_run", [this]() {
			// the flushed live-outs are the general form's: y, tacc and gi after the loop
			const std::string script = R"(
				function walk(int n) -> string { int y = 0; int tacc = 5; int gi = 1; while (y < n) { tacc += 3; gi += 2; y = y + 1; } return to_string(y) + "," + to_string(tacc) + "," + to_string(gi); }
				walk(0) + "|" + walk(1) + "|" + walk(7);
			)";
			both(script, "0,5,1|1,8,3|7,26,15", "live-outs after exit");
		});
		test("float_cells_land_bit_exactly", [this]() {
			const std::string script = R"(
				function acc(int n) -> string { float x = 0.1; float s = 0.0; int i = 0; while (i < n) { s += x; x = x * 1.5; i = i + 1; } return to_string(s) + "," + to_string(x); }
				acc(0) + "|" + acc(3) + "|" + acc(12);
			)";
			both(script, "", "float payloads");
		});
		test("chained_index_store_folds_every_step", [this]() {
			// Astra's control (b-review/chained-store.jai): pix[gi + 1 + offset] carries a chain step past the
			// store's first binary; the cell store folds every step exactly as the general lane does
			const std::string script = R"(
				array<int> pixels = [0, 0, 0, 0, 0, 0, 0, 0];
				array<int> strip = [7, 8, 9];
				function paint(var& pix, var& src, int offset) -> int {
					int y = 0; int gi = 0; int tacc = 0;
					while (y < 3) { int c = src[tacc >> 11]; pix[gi + 1 + offset] = c; tacc += 2048; gi += 2; y = y + 1; }
					return 0;
				}
				paint(pixels, strip, 1);
				to_string(pixels[2] * 3 + pixels[4] * 5 + pixels[6] * 7);
			)";
			both(script, "124", "chained index store");
		});
		test("late_chain_overflow_keeps_prior_stores_and_the_text", [this]() {
			// Astra's control (b-review/late-chain-overflow.jai, verified on stock: "Integer overflow in '+'" and 49):
			// the first chained index lands in bounds through the final %8, the second iteration's addition
			// overflows in checked mode; the text and the prior store match the interpreter
			const std::string script = R"(
				array<int> pixels = [0, 0, 0, 0, 0, 0, 0, 0];
				array<int> strip = [7, 8, 9];
				function paint(var& pix, var& src, int offset) -> int {
					int y = 0; int gi = 0; int tacc = 0;
					while (y < 3) { int c = src[tacc >> 11]; pix[(gi + 1 + offset) % 8] = c; tacc += 2048; gi += 2; y = y + 1; }
					return 0;
				}
				string out = "";
				try { paint(pixels, strip, 9223372036854775805); out = "ok"; } catch (err) { out = "throw:" + err; }
				out + "|" + to_string(pixels[2] * 3 + pixels[4] * 5 + pixels[6] * 7);
			)";
			const std::string interp = run_on(false, script), vm = run_on(true, script);
			check_eq(interp, vm, "late chain overflow: both backends");
			check(vm.find("Integer overflow in '+'") != std::string::npos, "late chain overflow: the text");
			check(vm.size() >= 3 && vm.compare(vm.size() - 3, 3, "|49") == 0, "late chain overflow: the prior store survives (49)");
		});
		test("store_landing_into_a_slot_of_another_declared_kind_takes_the_general_form", [this]() {
			// f is a float slot fed by int arithmetic: the general landing converts through the slot's declared
			// type; a cell of int kind over that slot is refused at entry (the slot holds a float), so the loop
			// runs the general form and lands the same
			const std::string script = R"(
				function run(int n) -> string {
					float f = 0.0; int y = 0; int acc = 0;
					while (y < n) { f = y + 3; acc += y; y = y + 1; }
					return to_string(f) + ":" + type_of(f) + ":" + to_string(acc);
				}
				run(5);
			)";
			both(script, "", "float slot fed by int arithmetic");
		});
		test("interleaved_coroutines_each_running_a_closed_loop", [this]() {
			// two coroutines yield between closed loops: a frame copied into fiber state carries no raw area and a
			// record's area is its own, so the interleaving lands the same everywhere
			const std::string script = R"(
				coroutine walker(int start, int rounds) -> int {
					int total = start;
					for (int r = 0; r < rounds; ++r) {
						int y = 0; int acc = 0;
						while (y < 50) { acc += y * (r + 1); y = y + 1; }
						total += acc;
						yield total;
					}
					return total;
				}
				var a = walker(1, 4);
				var b = walker(1000, 4);
				string out = "";
				for (int k = 0; k < 4; ++k) { out = out + to_string(a.resume()) + "/" + to_string(b.resume()) + ";"; }
				out;
			)";
			both(script, "", "interleaved coroutines");
		});
		test("stores_through_the_hoisted_container_are_read_back_by_the_same_loop", [this]() {
			// pix is resolved once per entry; the loop reads what it wrote through the same node
			const std::string script = R"(
				function walk(var& pix, int n, int k) -> int {
					int gi = 0; int y = 0;
					while (y < n) { int c = pix[gi]; int d = c + k; pix[gi + 1] = d; gi += 1; y = y + 1; }
					return gi;
				}
				var PIX = [1, 0, 0, 0, 0, 0];
				int r = walk(PIX, 4, 3);
				string out = to_string(r); for (int i = 0; i < PIX.size(); ++i) { out = out + (i == 0 ? "|" : ",") + to_string(PIX[i]); }
				out;
			)";
			both(script, "4|1,4,7,10,13,0", "read back through the hoisted node");
		});
		test("a_second_call_with_other_containers_resolves_them_anew", [this]() {
			const std::string script = std::string(k_prelude) + R"(
				var PIX2 = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
				STRIPS.push([1, 2, 3, 4]);
				var& s0 = STRIPS[0]; var& s1 = STRIPS[1];
				int a = paint(PIX, s0, 0, 4, 2, 0, 2048, 0, 1);
				int b = paint(PIX2, s1, 0, 4, 2, 0, 2048, 0, 1);
				string out = to_string(a) + "/" + to_string(b) + "|" + to_string(PIX[6]) + "," + to_string(PIX2[6]);
				out;
			)";
			both(script, "8004/8004|10,4", "second call, other containers");
		});
		test("element_reads_inside_typed_expressions_land_the_same", [this]() {
			// acc = acc + data[idx]: the read lands in a compiler temp (T_LOAD), the add is typed, the loop closes
			const std::string script = R"(
				function run(int n) -> int {
					array<int> data = [3, 1, 4, 1, 5, 9, 2, 6];
					int acc = 0; int i = 0;
					while (i < n) { int idx = i % 8; acc = acc + data[idx]; i = i + 1; }
					return acc;
				}
				to_string(run(20));
			)";
			both(script, "71", "expression-form element read");
		});
		test("element_reads_from_a_var_array_land_and_a_string_element_falls_back", [this]() {
			const std::string script = R"(
				function run(var& data, int n) -> string {
					int acc = 0; int i = 0;
					string out = "";
					try { while (i < n) { int idx = i % 8; acc = acc + data[idx]; i = i + 1; } out = to_string(acc); }
					catch (err) { out = "throw:" + err + "|" + to_string(acc); }
					return out;
				}
				var D = [3, 1, 4, 1, 5, 9, 2, 6];
				string a = run(D, 20);
				D[5] = "s";
				string b = run(D, 20);
				a + " / " + b;
			)";
			both(script, "", "var container, then a string element on the sixth read");
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("71 / throw:", 0) == 0, "first pass 71, second raises: " + vm);
			check_true(vm.find("|14") != std::string::npos, "the sum before the string element is retained (3+1+4+1+5 = 14): " + vm);
		});
		test("an_out_of_range_element_read_inside_an_expression_raises_the_same", [this]() {
			const std::string script = R"(
				function run(int n) -> string {
					array<int> data = [10, 20, 30, 40];
					int acc = 0; int i = 0; string out = "";
					try { while (i < n) { int idx = i + 2; acc = acc + data[idx]; i = i + 1; } out = to_string(acc); }
					catch (err) { out = "throw:" + err + "|" + to_string(acc); }
					return out;
				}
				run(5);
			)";
			both(script, "", "out of range on the third read");
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) == 0 && vm.find("|70") != std::string::npos, "30 + 40 retained, then the range error: " + vm);
		});
		test("two_element_reads_and_a_comparison_land_the_same", [this]() {
			const std::string script = R"(
				function run(int n, int limit) -> string {
					array<int> a = [1, 2, 3, 4, 5, 6]; array<int> b = [10, 20, 30, 40, 50, 60];
					int sum = 0; int count = 0; int i = 0;
					while (i < n) { int s = a[i] + b[i]; sum = sum + s; if (a[i] < limit) { count = count + 1; } i = i + 1; }
					return to_string(sum) + ":" + to_string(count);
				}
				run(6, 4);
			)";
			both(script, "231:3", "two reads, a comparison read");
		});
		test("float_element_reads_inside_expressions_land_bit_exactly", [this]() {
			const std::string script = R"(
				function run(int n) -> float {
					array<float> fd = [0.5, 1.25, -2.75, 3.125, 1e-3];
					float acc = 0.0; int i = 0;
					while (i < n) { int idx = i % 5; acc = acc + fd[idx]; i = i + 1; }
					return acc;
				}
				to_string(run(23));
			)";
			both(script, "", "float reads");
		});
		test("a_global_container_read_inside_an_expression_lands_the_same", [this]() {
			const std::string script = R"(
				array<int> G = [7, 8, 9, 10, 11];
				function run(int n) -> int { int acc = 0; int i = 0; while (i < n) { int idx = i % 5; acc = acc + G[idx]; i = i + 1; } return acc; }
				to_string(run(12));
			)";
			both(script, "105", "env-resident container");   // 45 + 45 + 7 + 8
		});
		test("the_expression_form_lowers_to_a_typed_load_and_the_loop_closes", [this]() {
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* script = "var f = function(int n) { array<int> data = [3, 1, 4, 1, 5, 9, 2, 6]; int acc = 0; int i = 0;"
			                     " while (i < n) { int idx = i % 8; acc = acc + data[idx]; i = i + 1; } return acc; };";
			auto templates = vm->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("T_LOAD") != std::string::npos, "the element read is a typed load: " + listing);
			check_true(listing.find("CELLS_BACK") != std::string::npos, "the loop closes around it: " + listing);
		});
		test("a_loop_with_a_break_is_not_closed_and_still_lands_the_same", [this]() {
			const std::string script = R"(
				array<int> PIX = [0, 0, 0, 0, 0, 0, 0, 0];
				var STRIPS = [[7, 8, 9, 10]];
				function paint(var& pix, var& strip, int cy1) -> int {
					int gi = 0; int tacc = 0; int y = 0;
					while (y < cy1) { int c = strip[tacc >> 11]; if (c == 9) { break; } pix[gi] = c; tacc += 2048; gi += 2; y = y + 1; }
					return gi * 1000 + y;
				}
				var& s = STRIPS[0];
				to_string(paint(PIX, s, 4)) + ":" + to_string(PIX[0]) + to_string(PIX[2]) + to_string(PIX[4]);
			)";
			both(script, "4002:780", "break inside the loop");
		});
		test("the_recognizer_marks_the_column_loop", [this]() {
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* script = "array<int> PIX = [0, 0, 0, 0]; var S = [[1, 2, 3]];"
			                     " var f = function(var& pix, var& strip, int cy0, int cy1, int vw, int px0, int tstep) {"
			                     " int gi = cy0 * vw + px0; int tacc = 0; int y = cy0;"
			                     " while (y < cy1) { int c = strip[tacc >> 11]; pix[gi] = c; pix[gi + 1] = c; tacc += tstep; gi += vw; y = y + 1; }"
			                     " return gi; };";
			auto templates = vm->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("CELLS_BACK") != std::string::npos, "the closed typed loop's back edge is the cell entry: " + listing);
			check_true(listing.find("C_LOAD") != std::string::npos && listing.find("C_INDEX_STORE") != std::string::npos, "the cell copy is present: " + listing);
		});
	}
};

}

using pilot_cells_tests = jai::foundry::tests::pilot_cells_tests;
FOUNDRY_REGISTER(pilot_cells_tests)
