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

	// the VM's listing of a snippet (the closed loops and the refusals appear under each chunk)
	std::string listing_of(const std::string& source) {
		auto e = engine::make();
		e->set_backend(backend_type::vm);
		stdlib::register_all(*e);
		auto* symbolizer = e->get_symbolizer();
		auto templates = e->get_registered_template_types();
		jai::lexer lex(source, symbolizer, templates);
		auto tokens = lex.tokenize();
		jai::parser p(tokens, symbolizer, e.get(), templates);
		auto parse_result = p.parse();
		if (!parse_result.has_value()) { return "<parse failed>"; }
		(void)jai::detail::run_static_check(*e, parse_result.value());
		jai::vm::vm_compiler compiler(symbolizer);
		return jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
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
		test("a_computed_index_read_inside_an_expression_lands_the_same", [this]() {
			const std::string script = R"(
				function run(int n) -> int {
					array<int> data = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3];
					int acc = 0; int i = 0;
					while (i < n) { acc = acc + data[i + 2]; i = i + 1; }
					return acc;
				}
				to_string(run(8));
			)";
			both(script, "35", "computed index operand");   // 4+1+5+9+2+6+5+3
		});
		test("an_unknown_container_read_inside_an_expression_lands_and_falls_back_the_same", [this]() {
			// tp is a var& parameter: no proof, the read lands as any scalar; a string element mid-loop raises the same
			const std::string script = R"(
				function run(var& tp, int n) -> string {
					int acc = 0; int i = 0; string out = "";
					try { while (i < n) { acc = acc + tp[i]; i = i + 1; } out = to_string(acc); }
					catch (err) { out = "throw:" + err + "|" + to_string(acc); }
					return out;
				}
				var T = [[10, 20, 30, 40, 50]];
				var& t0 = T[0];
				string a = run(t0, 5);
				t0[3] = "x";
				string b = run(t0, 5);
				a + " / " + b;
			)";
			both(script, "", "unknown container operand");
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("150 / throw:", 0) == 0 && vm.find("|60") != std::string::npos, "150, then the string element after 60: " + vm);
		});
		test("an_element_to_element_copy_lands_the_same", [this]() {
			const std::string script = R"(
				function copy(var& dst, var& src, int n) -> int { int i = 0; while (i < n) { dst[i] = src[i]; i = i + 1; } return i; }
				array<int> A = [0, 0, 0, 0]; array<int> B = [7, 8, 9, 10];
				var V = [0, 0, 0, 0]; var W = [1, "two", 3.5, 4];
				int a = copy(A, B, 4); int b = copy(V, W, 4);
				to_string(a) + to_string(b) + "|" + to_string(A[3]) + "," + to_string(V[1]) + "," + to_string(V[2]);
			)";
			both(script, "44|10,two,3.500000", "element-to-element copies, typed and var");
		});
		test("a_nested_index_read_lands_the_same", [this]() {
			const std::string script = R"(
				function run(int n) -> int {
					array<int> lut = [5, 6, 7, 8]; array<int> idx = [3, 1, 0, 2, 1];
					int acc = 0; int i = 0;
					while (i < n) { acc = acc + lut[idx[i]]; i = i + 1; }
					return acc;
				}
				to_string(run(5));
			)";
			both(script, "32", "nested index");   // 8+6+5+7+6
		});
		test("an_operand_that_could_raise_first_keeps_the_interpreter_order", [this]() {
			// `missing + data[9]`: the undefined global raises before the out-of-range read on both backends
			const std::string script = R"(
				function run() -> string {
					array<int> data = [1, 2, 3];
					string out = "";
					try { int v = missing + data[9]; out = to_string(v); } catch (err) { out = "throw:" + err; }
					return out;
				}
				run();
			)";
			both(script, "", "error order with an env partner");
		});
		test("a_reference_alias_of_a_typed_global_proves_its_element_reads", [this]() {
			// var& g = G with G an array<int>: the alias's reads are proven int, the loop closes as a cell block
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* scripts[2] = {
				"array<int> G = [3, 1, 4, 1, 5, 9, 2, 6]; var f = function(int n) { var& g = G; int acc = 0; int i = 0;"
				" while (i < n) { int idx = i % 8; acc = acc + g[idx]; i = i + 1; } return acc; };",
				"array<int> G = [3, 1, 4, 1, 5, 9, 2, 6]; var f = function(int n) { int acc = 0; int i = 0;"
				" while (i < n) { int idx = i % 8; acc = acc + G[idx]; i = i + 1; } return acc; };" };
			for (const char* script : scripts) {
			auto templates = vm->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("T_LOAD") != std::string::npos && listing.find("CELLS_BACK") != std::string::npos, "alias read typed and the loop closed: " + listing);
			}
			const std::string run = R"(
				array<int> G = [3, 1, 4, 1, 5, 9, 2, 6];
				function run(int n) -> int { var& g = G; int acc = 0; int i = 0; while (i < n) { int idx = i % 8; acc = acc + g[idx]; i = i + 1; } return acc; }
				to_string(run(20));
			)";
			both(run, "71", "alias read lands the same");
		});
		test("an_element_copy_between_aliases_lands_the_same_and_a_reassigned_global_converts", [this]() {
			// pix and pal alias typed globals; PIX is reassigned between calls (converted into array<int> by its declaration)
			const std::string script = R"(
				array<int> PIX = [0, 0, 0, 0]; array<int> PAL = [9, 8, 7, 6];
				function blit(int n) -> int { var& pix = PIX; var& pal = PAL; int i = 0; while (i < n) { pix[i] = pal[i]; i = i + 1; } return pix[0] * 1000 + pix[n - 1]; }
				int a = blit(4);
				PIX = [1, 1, 1, 1, 1, 1];
				int b = blit(3);
				to_string(a) + "|" + to_string(b) + "|" + to_string(PIX[3]) + "," + to_string(PIX.size());
			)";
			both(script, "9006|9007|1,6", "alias to alias copy, reassigned global");
		});
		test("a_store_to_a_typed_parameter_takes_the_typed_row_and_the_loop_closes", [this]() {
			// v = v + dv with both declared int parameters: a parse-proven typed store, so the loop is a cell block
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* script = "var g = function(int v, int dv, int n) { int i = 0; while (i < n) { v = v + dv; i = i + 1; } return v; };";
			auto templates = vm->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("BINARY_FUSED_STORE") == std::string::npos && listing.find("CELLS_BACK") != std::string::npos,
			           "the parameter store is typed and the loop closed: " + listing);
			both("function g(int v, int dv, int n) -> int { int i = 0; while (i < n) { v = v + dv; i = i + 1; } return v; } to_string(g(1, 3, 20));",
			     "61", "parameter store lands the same");
			// the column loop's shape: the parameter advances by a parameter each row, a negative texel row wraps
			const std::string column = R"(
				function col(int y0, int y1, int vfx, int dvfx, int th) -> int {
					int acc = 0;
					for (int y = y0; y <= y1; ++y) { int iv = (vfx >> 16) % th; if (iv < 0) { iv = iv + th; } acc = acc + iv; vfx = vfx + dvfx; }
					return acc;
				}
				to_string(col(0, 9, 0, 196608, 7)) + "," + to_string(col(0, 3, -131072, 65536, 7));
			)";
			both(column, "30,12", "column loop over parameters");
			// a parameter bound by reference downstream (its storage is a cell): the alias sees every store
			both("function k(int v, int dv, int n) -> int { var& a = v; int i = 0; while (i < n) { v = v + dv; i = i + 1; } return a; } to_string(k(1, 3, 20));",
			     "61", "reference-escaping parameter");
			// a float argument converts on entry: the parameter slot holds the declared int before the first store
			both("function g(int v, int dv, int n) -> int { int i = 0; while (i < n) { v = v + dv; i = i + 1; } return v; } to_string(g(1.0, 3, 20)) + \",\" + to_string(g(true, 3, 2));",
			     "61,7", "converted arguments");
			// array<int> parameters: the element class classifies a read, the store into an int parameter proves
			both("function s(array<int> t, int acc, int n) -> int { int i = 0; while (i < n) { acc = acc + t[i % 4]; i = i + 1; } return acc; } to_string(s([1, 2, 3, 4], 0, 10));",
			     "23", "typed array parameter");
		});
		test("a_hoisted_read_keeps_the_interpreter_order_past_a_property_reference", [this]() {
			// a var& bound to a C++ property dereferences through its getter; the interpreter evaluates that operand
			// before an element read that follows it, so the getter's error must come first on both backends
			struct order_box { bool armed = false; std::vector<script_int> values{5, 6, 7, 8}; script_int n = 3; };
			struct shape_case { const char* text; bool getter_first; };
			const shape_case shapes[5] = {
				{"function probe(int k, int i) -> int { var& p = box.values; box.arm(); return p[k + Q[i % 16]]; }", true},   // index-read arm
				{"function probe(int k, int i) -> int { var& p = box.values; box.arm(); return p[Q[i % 16]]; }", true},       // nested-index arm
				{"function probe(int k, int i) -> int { var& s = box.n; box.arm(); return s + Q[i % 16]; }", true},           // flat pre-step
				{"function probe(int k, int i) -> int { var& s = box.n; box.arm(); return s + Q[i % 16] * 2; }", true},       // the lift
				{"function probe(int k, int i) -> int { var& p = box.values; box.arm(); p[k] = Q[i % 16]; return 1; }", false} // a store: its right-hand side comes first
			};
			for (const shape_case& sc : shapes) {
				const char* shape = sc.text;
				std::string seen[2];
				for (int backend = 0; backend < 2; ++backend) {
					order_box box;
					auto e = engine::make();
					if (backend == 1) { e->set_backend(backend_type::vm); }
					stdlib::register_all(*e);
					dynamic_binder<order_box>(*e, "order_box")
						.property("values", [](order_box& b) { if (b.armed) { throw std::runtime_error("GETTER_FIRST"); } return b.values; }, nullptr)
						.property("n", [](order_box& b) { if (b.armed) { throw std::runtime_error("GETTER_FIRST"); } return b.n; }, nullptr)
						.method("arm", [](order_box& b) { b.armed = true; })
						.build();
					e->add_global_ref("box", box);
					const std::string src = std::string("array<int> Q = [1, 2];\n") + shape +
						"\nstring out = \"\"; try { out = to_string(probe(0, 9)); } catch (err) { out = \"throw:\" + err; } out;";
					try { seen[backend] = e->execute(src).as_string(); } catch (const std::exception& ex) { seen[backend] = std::string("throw:") + ex.what(); }
				}
				check_eq(seen[0], seen[1], std::string("evaluation order: ") + shape);
				check_eq(sc.getter_first, seen[1].find("GETTER_FIRST") != std::string::npos, std::string("which error comes first: ") + seen[1]);
			}
		});
		test("a_reference_declaration_keeps_a_function_on_the_lazy_env_path", [this]() {
			// var& g = G lands in a frame slot: the body needs no per-call scope environment
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* scripts[2] = {
				"array<int> G = [1, 2, 3, 4]; int H = 7; var f = function(int n) { var& g = G; int acc = 0;"
				" for (int i = 0; i < n; ++i) { acc = acc + g[i % 4] + H; } return acc; };",
				"array<int> G = [1, 2, 3, 4]; var f = function(int n) { int acc = 0;"
				" for (int i = 0; i < n; ++i) { auto& e = G[i % 4]; acc = acc + e; } return acc; };" };
			for (const char* script : scripts) {
				auto templates = vm->get_registered_template_types();
				jai::lexer lex(script, symbolizer, templates);
				auto tokens = lex.tokenize();
				jai::parser p(tokens, symbolizer, vm.get(), templates);
				auto parse_result = p.parse();
				check_true(parse_result.has_value(), "snippet parses");
				(void)jai::detail::run_static_check(*vm, parse_result.value());
				jai::vm::vm_compiler compiler(symbolizer);
				const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
				check_true(listing.find("(env: lazy)") != std::string::npos, "the body is lazy: " + listing);
			}
			// the aliases read and write through on both backends: a global, a parameter, a boxed local, an element
			const std::string run = R"(
				array<int> G = [1, 2, 3, 4]; int H = 7;
				function f(int n) -> int { var& g = G; int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + g[i % 4] + H; } return acc; }
				function g(int v) -> int { var& a = v; a = a + 1; int w = 3; var& b = w; b = b * 2; return v * 100 + w; }
				function r(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { var& e = G[i % 4]; s = s + e; e = e + 1; } return s; }
				function fact(int n) -> int { var& g = G; if (n <= 1) { return g[0]; } return n * fact(n - 1); }
				function outer() -> int { auto k = [](int x) -> int { return x; }; return f(4) + k(1); }
				string out = to_string(f(10)) + "," + to_string(g(4)) + "," + to_string(r(8)) + "," + to_string(G[0]) + "," + to_string(fact(5)) + "," + to_string(outer());
				G = [9, 9];
				out + "," + to_string(f(2));
			)";
			both(run, "93,506,24,3,360,47,32", "aliases through lazy frames");
			// a method's alias of its own field (the sticky method scope keeps the same rule)
			const std::string method = R"(
				class C { int hp = 10; int hit(int d) { var& h = hp; h = h - d; return hp; } }
				C c = C();
				to_string(c.hit(3)) + "," + to_string(c.hit(2));
			)";
			both(method, "7,5", "field alias in a method");
			// an alias of an undefined name raises the same text on both backends
			both("[[warn_mode]] function u() -> int { var& z = nowhere; return 1; } string out = \"\"; try { out = to_string(u()); } catch (e) { out = \"throw:\" + e; } out;", "", "undefined referent");
		});
		test("a_member_shadowing_a_same_named_global_keeps_its_getter_ahead_of_a_hoisted_read", [this]() {
			// same-unit `array<int> values`, a script class over a native class whose property is also `values`:
			// inside the method the bare name is the native property, so its getter still runs before the read
			struct shadow_box { bool armed = false; std::vector<script_int> values{5, 6, 7, 8}; };
			std::string seen[2];
			for (int backend = 0; backend < 2; ++backend) {
				shadow_box box;
				auto e = engine::make();
				if (backend == 1) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				dynamic_binder<shadow_box>(*e, "shadow_box")
					.constructor<>()
					.property("values", [](shadow_box& b) { if (b.armed) { throw std::runtime_error("GETTER_FIRST"); } return b.values; }, nullptr)
					.method("arm", [](shadow_box& b) { b.armed = true; })
					.build();
				e->add_global_ref("box", box);
				const char* src = R"(
					array<int> values = [1, 2, 3, 4];
					array<int> Q = [1, 2];
					class Probe : shadow_box {
						int probe(int k, int i) { var& p = values; arm(); return p[Q[i % 16]]; }
					}
					var pr = shadow_box();
					extend(pr, Probe);
					string out = "";
					try { out = to_string(pr.probe(0, 9)); } catch (err) { out = "throw:" + err; }
					out;
				)";
				try { seen[backend] = e->execute(src).as_string(); } catch (const std::exception& ex) { seen[backend] = std::string("throw:") + ex.what(); }
			}
			check_eq(seen[0], seen[1], "member shadowing a global: both backends");
			check_true(seen[1].find("GETTER_FIRST") != std::string::npos || seen[1].find("throw:") != 0,
			           "the getter's error comes first when the member is the referent: " + seen[1]);
		});
		test("a_read_inside_an_index_expression_lands_in_a_temp_and_the_outer_read_fuses", [this]() {
			// p[k + Q[i % 8]]: the inner read lands in a temp first, the outer read takes the T_LOAD row with the temp
			// as an index operand, and the loop closes as a cell block
			auto vm = engine::make();
			vm->set_backend(backend_type::vm);
			stdlib::register_all(*vm);
			auto* symbolizer = vm->get_symbolizer();
			const char* script =
				"array<int> P = [10, 20, 30, 40, 50, 60, 70, 80]; array<int> Q = [1, 2, 3, 0, 1, 2, 3, 0];"
				" var f = function(int n) { var& p = P; int acc = 0; int i = 0;"
				" while (i < n) { int k = i % 4; acc = acc + p[k + Q[i % 8]]; i = i + 1; } return acc; };";
			auto templates = vm->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("T_LOAD") != std::string::npos && listing.find("CELLS_BACK") != std::string::npos,
			           "both reads typed and the loop closed: " + listing);
			// the read on either side, or on both sides (left before right); values land the same on both backends
			const std::string run = R"(
				array<int> P = [10, 20, 30, 40, 50, 60, 70, 80]; array<int> Q = [1, 2, 3, 0, 1, 2, 3, 0];
				function r1(int n) -> int { var& p = P; int acc = 0; int i = 0; while (i < n) { int k = i % 4; acc = acc + p[k + Q[i % 8]]; i = i + 1; } return acc; }
				function r2(int n) -> int { var& p = P; int acc = 0; int i = 0; while (i < n) { int k = i % 4; acc = acc + p[Q[i % 8] + k]; i = i + 1; } return acc; }
				function r3(int n) -> int { var& p = P; int acc = 0; int i = 0; while (i < n) { acc = acc + p[Q[i % 8] + Q[(i + 1) % 8]]; i = i + 1; } return acc; }
				to_string(r1(20)) + "," + to_string(r2(21)) + "," + to_string(r3(22));
			)";
			both(run, "800,820,900", "nested reads land the same");
			// error order: the inner read's failure comes first, then the outer's; the same texts on both backends
			const std::string probe = R"(
				array<int> P = [1, 2, 3, 4]; array<int> Q = [0, 1];
				function f(int a, int b) -> int { var& p = P; return p[a + Q[b]]; }
				string out = "";
				try { out = to_string(f(1, 9)); } catch (e) { out = "throw:" + e; }
				try { out = out + "|" + to_string(f(9, 1)); } catch (e) { out = out + "|throw:" + e; }
				out;
			)";
			both(probe, "", "inner then outer raise");
			const std::string vm_probe = run_on(true, probe);
			check_true(vm_probe.rfind("throw:", 0) == 0 && vm_probe.find("|throw:") != std::string::npos, "both reads raise in turn: " + vm_probe);
			// an undefined outer container raises before the inner read (the container must be a frame slot for the hoist)
			const std::string outer_missing = R"(
				[[warn_mode]] array<int> Q = [1, 2];
				function probe(int k, int i) -> int { return missing_outer[k + Q[i % 16]]; }
				string out = "";
				try { out = to_string(probe(0, 9)); } catch (e) { out = "throw:" + e; }
				out;
			)";
			both(outer_missing, "", "undefined outer container raises first");
			const std::string vm_missing = run_on(true, outer_missing);
			check_true(vm_missing.find("missing_outer") != std::string::npos, "the outer name is the error: " + vm_missing);
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
		test("a_statement_increment_lowers_to_the_typed_row_and_the_loop_closes", [this]() {
			// `++y` / `y--` in statement position on a frame slot: the int compound row (`y += 1`'s landing);
			// a var slot holding a float, a shared cell, a reference and the overflow text take the general op
			const char* values =
				"function up(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += i; } int j = n; while (j > 0) { j--; s += j; } return s; }"
				"function post(int n) -> int { int i = 0; int t = 0; while (i < n) { t = i++; } return t * 100 + i; }"
				"function fl() -> string { var f = 1.5; ++f; f--; ++f; return to_string(f); }"
				"function viaref() -> int { int x = 1; int& r = x; ++r; r++; --r; return x * 10 + r; }"
				"function ovf() -> string { int m = 9223372036854775807; try { ++m; } catch (err) { return \"throw:\" + err; } return to_string(m); }"
				"function unf() -> string { int m = -9223372036854775807 - 1; try { m--; } catch (err) { return \"throw:\" + err; } return to_string(m); }"
				"function boxed() -> int { int c = 0; var bump = [&]() { ++c; }; bump(); bump(); return c; }"
				"function dyn() -> int { var v = 5; ++v; v++; return v; }"
				"to_string(up(10)) + \",\" + to_string(post(3)) + \",\" + fl() + \",\" + to_string(viaref()) + \",\" + ovf() + \",\" + unf() + \",\" + to_string(boxed()) + \",\" + to_string(dyn());";
			both(values, "", "increments");
			both(values, "", "increments, proofs off", false);
			const std::string vm = run_on(true, values);
			check_true(vm.rfind("90,203,2.500000,22,throw:", 0) == 0, "typed, postfix, float and reference shapes: " + vm);
			check_true(vm.find("Integer overflow in '++'") != std::string::npos && vm.find("Integer overflow in '--'") != std::string::npos, "the general op's overflow texts: " + vm);
			check_true(vm.size() >= 4 && vm.compare(vm.size() - 4, 4, ",2,7") == 0, "the shared cell and the var slot: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* script = "array<int> PIX = [0, 0, 0, 0]; var S = [[1, 2, 3]];"
			                     " var f = function(var& pix, var& strip, int cy0, int cy1, int vw, int px0, int tstep) {"
			                     " int gi = cy0 * vw + px0; int tacc = 0; int y = cy0;"
			                     " while (y < cy1) { int c = strip[tacc >> 11]; pix[gi] = c; pix[gi + 1] = c; tacc += tstep; gi += vw; ++y; }"
			                     " int z = cy1; while (z > cy0) { pix[gi] = z; z--; }"
			                     " return gi; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("INCDEC") == std::string::npos, "no general increment remains: " + listing);
			check_true(listing.find("T_ADD_II") != std::string::npos && listing.find("T_SUB_II") != std::string::npos, "++ and -- take the int rows: " + listing);
			check_true(listing.find("CELLS_BACK") != std::string::npos, "the column loop still closes with ++y: " + listing);
		});
		test("a_read_through_an_unproven_container_takes_the_typed_row_and_falls_back_the_same", [this]() {
			// pal[base + tp[i + j]] with both containers var& parameters (a computed inner index, JaiDOOM's texture shape): the checker cannot class the index binary, the
			// outer read takes the T_LOAD row on the element-read temp anyway; a map container, a float element and a
			// string element miss into the general op with its own results and texts
			const std::string script = R"(
				function f(var& tp, var& pal, int n, int j) -> string {
					int s = 0;
					try { for (int i = 0; i < n; ++i) { int base = i * 2; s = s + pal[base + tp[i + j]]; } }
					catch (err) { return "throw:" + err; }
					return to_string(s);
				}
				var T = [0, 1, 1]; var P = [10, 20, 30, 40, 50, 60]; var M = {0: 10, 3: 40, 5: 60};
				var F = [0, 1.5, 1]; var S = [0, "x", 1];
				f(T, P, 3, 0) + "," + f(T, M, 3, 0) + "," + f(F, P, 3, 0) + "," + f(S, P, 3, 0);
			)";
			both(script, "", "unproven index binary");
			both(script, "", "unproven index binary, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("110,110,", 0) == 0, "the array and the map land the same sum: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "var f = function(var& tp, var& pal, int n, int j) { int s = 0; for (int i = 0; i < n; ++i) { int base = i * 2; s = s + pal[base + tp[i + j]]; } return s; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			size_t loads = 0; for (size_t at = listing.find("T_LOAD"); at != std::string::npos; at = listing.find("T_LOAD", at + 6)) { ++loads; }
			check_true(loads >= 2 && listing.find("INDEX_FUSED_DECL") == std::string::npos, "inner and outer reads both take the typed row: " + listing);
		});
		test("a_math_intrinsic_result_lands_in_a_temp_and_the_binary_takes_the_typed_row", [this]() {
			// JaiDOOM's flat-column shapes: an intrinsic inside a typed binary, inside an index expression, in a float
			// expression; the kernel's errors keep their place ahead of and behind a hoisted element read
			const std::string script = R"(
				function band(int bb, float z) -> int { int b = bb + math::itrunc(z * 0.001953125); if (b < 0) { b = 0; } if (b > 31) { b = 31; } return b; }
				function texel(var& flat, float wx, float wy) -> int { return flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]; }
				function fl(float x) -> float { return math::sqrt(x) * 2.0 + math::floor(x); }
				function ints(int i) -> int { return math::itrunc(i) + math::abs(i - 10); }
				function bad(float x) -> string { try { return to_string(math::itrunc(x) + 1); } catch (err) { return "throw:" + err; } }
				function read_first(var& arr, int i) -> string { try { return to_string(arr[i] + math::itrunc(1e300)); } catch (err) { return "throw:" + err; } }
				function call_first(var& arr, int i) -> string { try { return to_string(math::itrunc(1e300) + arr[i]); } catch (err) { return "throw:" + err; } }
				var F = []; for (int i = 0; i < 4096; ++i) { F.push(i); }
				to_string(band(5, 1000.0)) + "," + to_string(band(0, 40000.0)) + "," + to_string(texel(F, 3.7, 2.2)) + "," + to_string(fl(4.0)) + "," + to_string(ints(3)) + "," + bad(1e300) + "," + read_first(F, 9999) + "," + call_first(F, 9999);
			)";
			both(script, "", "math temps");
			both(script, "", "math temps, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("6,31,131,8", 0) == 0, "band, clamp, texel and the float expression: " + vm);
			check_true(vm.find(",10,throw:") != std::string::npos, "the int forms and the conversion error: " + vm);
			const std::string interp = run_on(false, script);
			const size_t tail = vm.rfind(",throw:");
			check_true(tail != std::string::npos && vm.substr(tail) == interp.substr(interp.rfind(",throw:")), "the read-first and call-first texts agree: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "var f = function(var& flat, int bb, float z, float wx, float wy) { int b = bb + math::itrunc(z * 0.001953125); return b + flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			size_t maths = 0, temps = 0;
			for (size_t at = listing.find("MATH"); at != std::string::npos; at = listing.find("MATH", at + 4)) { ++maths; if (listing.compare(at, 9, "MATH_TEMP") == 0) { ++temps; } }
			check_true(maths == 3 && temps == 3, "every intrinsic lands in a temp: " + listing);
			check_true(listing.find("T_AND_II") != std::string::npos && listing.find("T_SHL_II") != std::string::npos && listing.find("T_ADD_II") != std::string::npos, "the binaries around them take the int rows: " + listing);
		});
		test("an_index_tree_lands_in_a_temp_and_the_read_takes_the_typed_row", [this]() {
			// flat[((itrunc(wy) & 63) << 6) + (itrunc(wx) & 63)] (draw_flat_col's texel), a plain arithmetic tree index, a
			// tree that raises inside the index, and the bounds error of a tree index: the same on both backends
			const std::string script = R"(
				function texel(var& flat, float wx, float wy) -> int { int v = flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]; return v; }
				function tree(var& arr, int i, int j) -> int { int s = 0; s = s + arr[(i + 1) * 2 - j]; return s; }
				function raises(var& arr, int i) -> string { try { return to_string(arr[math::itrunc(1e300) + i]); } catch (err) { return "throw:" + err; } }
				function oob(var& arr, int i) -> string { try { return to_string(arr[(i + 1) * 2]); } catch (err) { return "throw:" + err; } }
				var F = []; for (int i = 0; i < 4096; ++i) { F.push(i); }
				var A = [10, 20, 30, 40, 50, 60];
				to_string(texel(F, 3.7, 2.2)) + "," + to_string(tree(A, 1, 1)) + "," + raises(A, 1) + "," + oob(A, 4);
			)";
			both(script, "", "index trees");
			both(script, "", "index trees, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("131,40,throw:", 0) == 0, "the texel, the tree and the conversion error: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "var f = function(var& flat, float wx, float wy, int k) { int v = flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]; int w = flat[(k + 1) * 2 - v]; return v + w; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			size_t loads = 0; for (size_t at = listing.find("T_LOAD"); at != std::string::npos; at = listing.find("T_LOAD", at + 6)) { ++loads; }
			check_true(loads == 2 && listing.find("INDEX") == std::string::npos, "both tree-indexed reads take the typed row: " + listing);
		});
		test("a_plain_alias_container_reads_through_the_guard_and_a_shrunken_element_reference_does_not", [this]() {
			// the guard leaves a plain alias (a var& parameter, a var& of a same-unit or other-unit value) to the reader;
			// an element reference whose array shrank is not plain: its dereference raises before the index, both backends
			const std::string script = R"(
				var PAL = []; for (int i = 0; i < 512; ++i) { PAL.push(i * 3); }
				var TEX = [[0, 1, 2, 3], [4, 5, 6, 7]];
				function texel(var& tp, var& pal, int base, int iv) -> int { var& p = pal; int v = p[base + tp[iv + 0]]; return v + pal[(base + 1) * 2 + tp[iv]]; }
				function shrunk(int i) -> string { var rows = [[1, 2, 3], [4, 5, 6]]; var& r = rows[1]; rows.pop(); rows.pop(); try { return to_string(r[i + 0 + rows.size()]); } catch (err) { return "throw:" + err; } }
				var& t0 = TEX[0];
				to_string(texel(t0, PAL, 10, 2)) + "," + to_string(texel(TEX[1], PAL, 0, 3)) + "," + shrunk(9) + "," + shrunk(0);
			)";
			both(script, "", "guarded containers");
			both(script, "", "guarded containers, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("108,48,", 0) == 0, "the plain aliases land: " + vm);
		});
		test("a_closed_loop_may_branch_inside_itself", [this]() {
			// Astra's diamond fixtures in while form, the texture loop's guarded arm, a continue, a break (leaves the loop
			// open) and an overflow inside the taken arm: the same on both backends, proofs on and off
			const std::string script = R"(
				function diamond(int n) -> int { int total = 0; int i = 0; while (i < n) { if ((i % 2) == 0) { total += i; } else { total -= i; } i = i + 1; } return total; }
				function guarded(var& pix, var& pal, var& tp, int n, int th) -> int { int acc = 0; int y = 0; while (y < n) { int iv = (y * 7) % th - 3; if (iv < 0) { iv = iv + th; } pix[y] = pal[tp[iv]]; acc = acc + pix[y]; y = y + 1; } return acc; }
				function cont(int n) -> int { int total = 0; for (int i = 0; i < n; ++i) { if (i == 1) { continue; } total += i; } return total; }
				function brk(int n) -> int { int total = 0; int i = 0; while (i < n) { if (i == 5) { break; } total += i; i = i + 1; } return total; }
				function ovf(int n) -> string { int total = 9223372036854775800; int seen = 0; int i = 0; try { while (i < n) { seen = i; if ((i % 2) == 0) { total += i; } else { total -= 1; } i = i + 1; } } catch (err) { return "throw:" + err + "|" + to_string(seen); } return to_string(total); }
				var PIX = [0, 0, 0, 0, 0, 0, 0, 0]; var PAL = []; for (int k = 0; k < 64; ++k) { PAL.push(k * 2); } var TP = [3, 1, 4, 1, 5, 9, 2, 6];
				to_string(diamond(6)) + "," + to_string(diamond(0)) + "," + to_string(diamond(1)) + "," + to_string(guarded(PIX, PAL, TP, 8, 8)) + "," + to_string(cont(10)) + "," + to_string(brk(10)) + "," + ovf(12);
			)";
			both(script, "", "branching cells");
			both(script, "", "branching cells, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("-3,0,0,62,44,10,throw:", 0) == 0 && vm.find("|6") != std::string::npos, "diamonds, the guarded arm, continue, break and the overflow in the arm: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			auto listing_of = [&](const char* src) {
				auto templates = e->get_registered_template_types();
				jai::lexer lex(src, symbolizer, templates);
				auto tokens = lex.tokenize();
				jai::parser p(tokens, symbolizer, e.get(), templates);
				auto parse_result = p.parse();
				check_true(parse_result.has_value(), "snippet parses");
				(void)jai::detail::run_static_check(*e, parse_result.value());
				jai::vm::vm_compiler compiler(symbolizer);
				return jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			};
			const std::string d = listing_of("var f = function(int n) { int total = 0; int i = 0; while (i < n) { if ((i % 2) == 0) { total += i; } else { total -= i; } i = i + 1; } return total; };");
			check_true(d.find("CELLS_BACK") != std::string::npos && d.find("C_JCMP") != std::string::npos && d.find("C_JUMP") != std::string::npos, "the two-arm diamond closes with cell branches: " + d);
			const std::string g = listing_of("var f = function(var& pix, var& pal, var& tp, int n, int th) { int y = 0; while (y < n) { int iv = (y * 7) % th - 3; if (iv < 0) { iv = iv + th; } pix[y] = pal[tp[iv]]; y = y + 1; } return y; };");
			check_true(g.find("CELLS_BACK") != std::string::npos && g.find("C_JCMP") != std::string::npos && g.find("C_GUARD") != std::string::npos, "the texture shape closes over its guarded arm and its container guard: " + g);
			const std::string b = listing_of("var f = function(int n) { int total = 0; int i = 0; while (i < n) { if (i == 5) { break; } total += i; i = i + 1; } return total; };");
			check_true(b.find("CELLS_BACK") == std::string::npos, "a break leaves the loop open: " + b);
		});
		test("an_environment_scalar_in_a_store_index_is_a_read_only_cell", [this]() {
			// JaiDOOM's pix[y * VW + x] with VW a global: the loop closes with VW resolved once at entry; a float VW and a
			// reference VW keep the general form; a VW changed between calls is read anew at each entry
			const std::string script = R"(
				var VW = 4;
				var PIX = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
				function col(var& pix, var& pal, var& tp, int x, int n) -> int { int acc = 0; int y = 0; while (y < n) { int iv = (y * 5) % 8 - 2; if (iv < 0) { iv = iv + 8; } pix[y * VW + x] = pal[tp[iv]]; acc = acc + pix[y * VW + x]; y = y + 1; } return acc; }
				var PAL = []; for (int k = 0; k < 64; ++k) { PAL.push(k * 3); } var TP = [3, 1, 4, 1, 5, 9, 2, 6];
				int a = col(PIX, PAL, TP, 1, 4);
				VW = 2; int b = col(PIX, PAL, TP, 0, 4);
				VW = 1.5; string c = ""; try { c = to_string(col(PIX, PAL, TP, 0, 4)); } catch (err) { c = "throw:" + err; }
				to_string(a) + "," + to_string(b) + "," + c + "," + to_string(PIX[1]) + "," + to_string(PIX[5]);
			)";
			both(script, "", "env cell");
			both(script, "", "env cell, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("", 0) == 0 && vm.find(",") != std::string::npos, "ran: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "var VW = 4; var f = function(var& pix, var& pal, var& tp, int x, int n) { int y = 0; while (y < n) { int iv = (y * 5) % 8 - 2; if (iv < 0) { iv = iv + 8; } pix[y * VW + x] = pal[tp[iv]]; y = y + 1; } return y; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("CELLS_BACK") != std::string::npos && listing.find("C_INDEX_STORE") != std::string::npos, "the texture shape with a global view width closes: " + listing);
		});
		test("a_flat_column_loop_closes_over_var_globals_math_and_clamps", [this]() {
			// JaiDOOM's draw_flat_col: var globals in the arithmetic (CENTERY, VIEW_X, VIEW_Y, VW), a float / int division,
			// itrunc into a band, clamps, a continue, the texture as an element reference into a table; a float CENTERY
			// (a conversion at the int decl) and a pixel table aliasing the texture table keep the general form's results;
			// the same pixels on both backends, proofs on and off
			const std::string script = R"(
				var VW = 4; var VIEWH = 16; var CENTERY = 3; var VIEW_X = 0.5; var VIEW_Y = 0.25; var VIEW_Z = 41.0; var FOCALY = 60.0;
				var PIX = []; for (int k = 0; k < 64; ++k) { PIX.push(0); }
				var PALCM = []; for (int k = 0; k < 8192; ++k) { PALCM.push(k % 251); }
				var FLATS = []; for (int t = 0; t < 2; ++t) { var f = []; for (int k = 0; k < 4096; ++k) { f.push((k * (7 + t)) % 64); } FLATS.push(f); }
				function flat_col(int x, int y0, int y1, var& flat, float planeh, float raydx, float raydy, int light) -> int {
					if (y0 < 0) { y0 = 0; }
					if (y1 >= VIEWH) { y1 = VIEWH - 1; }
					float dz = (VIEW_Z - planeh) * FOCALY;
					int bandbase = ((255 - light) >> 3) - 1;
					var& pix = PIX; var& palcm = PALCM;
					for (int y = y0; y <= y1; ++y) {
						int dy = y - CENTERY;
						if (dy == 0) { continue; }
						float z = dz / dy;
						float wx = VIEW_X + raydx * z;
						float wy = VIEW_Y + raydy * z;
						int band = bandbase + math::itrunc(z * 0.001953125);
						if (band < 0) { band = 0; }
						if (band > 31) { band = 31; }
						pix[y * VW + x] = palcm[band * 256 + flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]];
					}
					int acc = 0; for (int k = 0; k < 64; ++k) { acc = acc + pix[k] * (k + 1); }
					return acc;
				}
				var& cflat = FLATS[1];
				int a = flat_col(1, -2, 40, cflat, 20.0, 0.7, -0.3, 96);
				int b = flat_col(2, 0, 15, FLATS[0], -8.0, 1.9, 0.4, 200);
				CENTERY = 2.5; string c = ""; try { c = to_string(flat_col(3, 0, 15, cflat, 20.0, 0.7, -0.3, 96)); } catch (err) { c = "throw:" + err; }
				CENTERY = 3; PIX = FLATS; string d = ""; try { d = to_string(flat_col(1, 0, 3, cflat, 20.0, 0.7, -0.3, 96)); } catch (err) { d = "throw:" + err; }
				to_string(a) + "," + to_string(b) + "," + c + "," + d + "," + to_string(FLATS[1].size()) + "," + type_of(FLATS[1]);
			)";
			both(script, "", "flat column");
			both(script, "", "flat column, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find(",") != std::string::npos, "ran: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "var VW = 4; var CENTERY = 3; var VIEW_X = 0.5; var VIEW_Y = 0.25;"
			                 " var f = function(var& pix, var& palcm, var& flat, int x, int y0, int y1, float dz, float raydx, float raydy, int bandbase) {"
			                 " for (int y = y0; y <= y1; ++y) { int dy = y - CENTERY; if (dy == 0) { continue; } float z = dz / dy; float wx = VIEW_X + raydx * z; float wy = VIEW_Y + raydy * z;"
			                 " int band = bandbase + math::itrunc(z * 0.001953125); if (band < 0) { band = 0; } if (band > 31) { band = 31; }"
			                 " pix[y * VW + x] = palcm[band * 256 + flat[((math::itrunc(wy) & 63) << 6) + (math::itrunc(wx) & 63)]]; } return y0; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("CELLS_BACK") != std::string::npos && listing.find("C_ARITH_ANY") != std::string::npos && listing.find("C_MATH") != std::string::npos && listing.find("C_MOV") != std::string::npos, "the flat column loop closes: " + listing);
		});
		test("two_pinned_tables_sharing_a_guard_slot_keep_their_own_snapshots", [this]() {
			// Astra's review probes (guard_slot_reuse, guard_slot_reuse_kind): two guarded element-reference reads in
			// different statements share one guard-temp slot; a bounds miss and a kind miss at the first read must resume
			// the general form on the first table's handle, not the second's
			const char* body = R"(
				function probe(var& a, var& b, int n, int zero, int one) -> int {
					int total = 0;
					int i = 0;
					while (i < n) {
						int x = a[(i + zero) * (one + zero)];
						int y = b[(zero + zero) * (one + zero)];
						total = total + x + y;
						i = i + 1;
					}
					return total;
				}
				var& a = rows[0];
				var& b = rows[1];
				string r = ""; try { r = to_string(probe(a, b, 4, 0, 1)); } catch (e) { r = "caught:" + e; }
				r + "|" + to_string(probe(a, b, 2, 0, 1));
			)";
			const std::string bounds = std::string("var rows = [[10,20], [100,101,102,103,104,105]];") + body;
			const std::string kind = std::string("var rows = [[10,20,\"bad\",40], [100,101,102,103,104,105]];") + body;
			both(bounds, "caught:Array index 2 out of bounds for array of size 2|230", "bounds miss on the first table");
			both(bounds, "", "bounds miss on the first table, proofs off", false);
			both(kind, "", "kind miss on the first table");
			both(kind, "", "kind miss on the first table, proofs off", false);
			const std::string vm = run_on(true, kind);
			check_true(vm.rfind("caught:", 0) == 0 && vm.find("declared int") != std::string::npos && vm.find("|230") != std::string::npos, "the kind miss raises the declaration's text: " + vm);
		});
		test("a_counted_loop_runs_as_a_block_with_its_back_edge_stepping_in_the_cells", [this]() {
			// for (int i = 0; i < n; ++i) (the counted form: pix_clear, the row loops): a clear into a typed global, a stride
			// with a continue, an end changed inside the body (read fresh at every back edge), a step that overflows (the
			// general back edge's text), zero iterations, a float and a string value into array<int> (the general store's
			// conversion and rejection); the same on both backends, proofs on and off
			const std::string script = R"(
				array<int> PIX = []; for (int k = 0; k < 64; ++k) { PIX.push(0); }
				var VW = 8; var VH = 8;
				function clear_to(int color) -> int { int n = VW * VH; for (int i = 0; i < n; ++i) { PIX[i] = color; } return PIX[0] + PIX[63]; }
				function fill_any(var v) -> int { int n = VW * VH; for (int i = 0; i < n; ++i) { PIX[i] = v; } return PIX[1]; }
				function sum_stride(var& pix, int n, int step) -> int { int acc = 0; for (int i = 0; i < n; i += step) { acc = acc + pix[i]; if (i == 42) { continue; } acc = acc + 1; } return acc; }
				function shrink(var& pix, int n) -> int { int hits = 0; for (int i = 0; i < n; ++i) { pix[i] = i; n = n - 2; hits = hits + 1; } return hits * 100 + pix[3]; }
				function stepped(int n) -> string { int acc = 0; for (int i = 9223372036854775800; i < n; i += 4) { acc = acc + 1; } return to_string(acc); }
				int a = clear_to(7);
				int b = sum_stride(PIX, 64, 3) + sum_stride(PIX, 0, 3);
				int c = shrink(PIX, 64);
				string d = ""; try { d = stepped(9223372036854775807); } catch (err) { d = "throw:" + err; }
				string e = ""; try { e = to_string(fill_any(2.5)); } catch (err) { e = "throw:" + err; }
				string f = ""; try { f = to_string(fill_any("x")); } catch (err) { f = "throw:" + err; }
				to_string(a) + "," + to_string(b) + "," + to_string(c) + "," + d + "," + e + "," + f + "," + to_string(PIX[2]);
			)";
			both(script, "", "counted loops");
			both(script, "", "counted loops, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("14,", 0) == 0 && vm.find("throw:Integer overflow in '+='") != std::string::npos, "the clear and the overflow text: " + vm);
			auto e = engine::make();
			e->set_backend(backend_type::vm);
			stdlib::register_all(*e);
			auto* symbolizer = e->get_symbolizer();
			const char* fn = "array<int> PIX = []; var VW = 8; var VH = 8; var f = function(int color) { int n = VW * VH; for (int i = 0; i < n; ++i) { PIX[i] = color; } return n; };";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(fn, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			(void)jai::detail::run_static_check(*e, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("CELLS_CFOR_BACK") != std::string::npos && listing.find("C_CFOR_BACK") != std::string::npos && listing.find("C_INDEX_STORE") != std::string::npos, "the counted clear loop closes: " + listing);
		});
		test("compare_jumps_with_run_time_kinds_close_a_patch_loop_and_an_env_headed_loop", [this]() {
			// JaiDOOM's draw_patch inner loop (py >= VH with VH a var global, a continue, a pinned sprite table, VW in the
			// store) and a while whose head compares against a var global; a float head operand takes the promotion row, a
			// string global keeps the general form; the same on both backends, proofs on and off
			const std::string script = R"(
				var VW = 8; var VH = 8; var LIMIT = 5; var FLIMIT = 4.5; var SLIMIT = "x";
				var PIX = []; for (int k = 0; k < 64; ++k) { PIX.push(0); }
				var PALCM = []; for (int k = 0; k < 256; ++k) { PALCM.push(k * 3 % 251); }
				var SPR_PIX = []; for (int t = 0; t < 2; ++t) { var s = []; for (int k = 0; k < 64; ++k) { s.push((k * (5 + t)) % 40 - 3); } SPR_PIX.push(s); }
				function patch_col(int lump, int x0, int y0, int w, int h, int pw, int ph) -> int {
					var& tp = SPR_PIX[lump]; var& pix = PIX; var& palcm = PALCM;
					int acc = 0;
					for (int sx = 0; sx < w; ++sx) {
						int px = x0 + sx;
						if (px < 0 || px >= VW) { continue; }
						int base = (sx * pw / w) * ph;
						for (int sy = 0; sy < h; ++sy) {
							int py = y0 + sy;
							if (py < 0 || py >= VH) { continue; }
							int texel = tp[base + sy * ph / h];
							if (texel >= 0) { pix[py * VW + px] = palcm[texel]; }
						}
					}
					for (int k = 0; k < 64; ++k) { acc = acc + pix[k] * (k + 1); }
					return acc;
				}
				function count_up(int start) -> int { int x = start; int n = 0; while (x < LIMIT) { n = n + x; x = x + 1; } return n; }
				function count_f(float start) -> int { float x = start; int n = 0; while (x < LIMIT) { n = n + 1; x = x + 0.5; } return n; }
				function count_fl(int start) -> int { int x = start; int n = 0; while (x < FLIMIT) { n = n + 1; x = x + 1; } return n; }
				int a = patch_col(1, -2, -3, 12, 12, 8, 8);
				int b = patch_col(0, 3, 2, 4, 9, 8, 8);
				int c = count_up(-3) * 1000 + count_f(1.25) * 10 + count_fl(0);
				LIMIT = 7.5; int d = count_up(0);
				string e = ""; try { LIMIT = SLIMIT; e = to_string(count_up(0)); } catch (err) { e = "throw:" + err; }
				to_string(a) + "," + to_string(b) + "," + to_string(c) + "," + to_string(d) + "," + e;
			)";
			both(script, "", "run-time-kind compares");
			both(script, "", "run-time-kind compares, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.find(",4085,28,") != std::string::npos, "the env-headed loops: " + vm);
			const std::string listing = listing_of("var VW = 8; var VH = 8; var LIMIT = 5;"
			                 " var f = function(var& pix, var& palcm, var& tp, int px, int y0, int h, int base, int ph) {"
			                 " for (int sy = 0; sy < h; ++sy) { int py = y0 + sy; if (py < 0) { continue; } if (py >= VH) { continue; } int texel = tp[base + sy * ph / h]; if (texel >= 0) { pix[py * VW + px] = palcm[texel]; } } return h; };"
			                 " var g = function(int start) { int x = start; int n = 0; while (x < LIMIT) { n = n + x; x = x + 1; } return n; };"
			                 " var f_or = function(int h, int y0) { int acc = 0; for (int sy = 0; sy < h; ++sy) { int py = y0 + sy; if (py < 0 || py >= VH) { continue; } acc = acc + py; } return acc; };");
			check_true(listing.find("C_JCMP_ANY") != std::string::npos && listing.find("CELLS_CFOR_BACK") != std::string::npos, "the patch loop closes over its env compare: " + listing);
			check_true(listing.find("C_CMP_ANY") != std::string::npos, "the env-headed while closes: " + listing);
			// the || chain (a comparison value and JUMP_IF_TRUE) is one cmp with a jump sense: f_or closes too
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(3), closed, "f, g and f_or close: " + listing);
		});
		test("translation_equivalence_the_same_loop_in_every_spelling_closes_the_same_way", [this]() {
			// one meaning, five spellings: the step as ++i, i += 1 and i = i + 1; the loop as a while, an identifier-start for
			// and a literal-start for; every one closes (the listing) and lands the same sums on both backends
			const char* defs =
				"array<int> A = []; for (int k = 0; k < 32; ++k) { A.push(k * 3 % 17); }"
				" var s1 = function(var& a, int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + a[i]; ++i; } return acc; };"
				" var s2 = function(var& a, int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + a[i]; i += 1; } return acc; };"
				" var s3 = function(var& a, int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + a[i]; i = i + 1; } return acc; };"
				" var s4 = function(var& a, int n, int from) { int acc = 0; for (int i = from; i < n; ++i) { acc = acc + a[i]; } return acc; };"
				" var s5 = function(var& a, int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + a[i]; } return acc; };";
			const std::string script = std::string(defs) + " to_string(s1(A, 32)) + \",\" + to_string(s2(A, 32)) + \",\" + to_string(s3(A, 32)) + \",\" + to_string(s4(A, 32, 0)) + \",\" + to_string(s5(A, 32));";
			both(script, "", "five spellings");
			both(script, "", "five spellings, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.find(",") != std::string::npos && vm.find("throw") == std::string::npos, "ran: " + vm);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(5), closed, "every spelling closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 400) + ")");
		});
		test("open_loops_are_named_by_class_and_first_instruction", [this]() {
			// the translation's refusals are recorded per loop: a call is an effect, a break is an effect, a string append is
			// a representation (a string is not an admitted value), a member access is a representation, a stack-form unary
			// minus is a translation gap
			const std::string listing = listing_of(
				"var g = function(int x) { return x; }; class P { int v = 1; }"
				" var f1 = function(int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + g(i); i = i + 1; } return acc; };"
				" var f2 = function(int n) { int acc = 0; int i = 0; while (i < n) { if (i == 3) { break; } acc = acc + i; i = i + 1; } return acc; };"
				" var f3 = function(int n) { string s = \"\"; int i = 0; while (i < n) { s += \"x\"; i = i + 1; } return s; };"
				" var f4 = function(int n, P p) { int acc = 0; int i = 0; while (i < n) { acc = acc + p.v; i = i + 1; } return acc; };"
				" var f5 = function(int n) { int acc = 0; int i = 0; while (i < n) { int v = -i; acc = acc + v; i = i + 1; } return acc; };");
			check_true(listing.find("-- open loop") != std::string::npos, "refusals listed: " + listing);
			check_true(listing.find(": effect at") != std::string::npos, "the call and the break are effects: " + listing);
			check_true(listing.find(": translation at") != std::string::npos && listing.find("(UNARY)") != std::string::npos, "the unary minus is a translation gap: " + listing);
			check_true(listing.find(": representation at") != std::string::npos && listing.find("(COMPOUND_FUSED)") != std::string::npos, "the string append and the member access are representations: " + listing);
		});
		test("head_spans_the_math_family_and_untyped_compounds_close", [this]() {
			// a while whose condition computes before its compare (a temp, an env value, an element read), the float family of
			// math on int and float values (abs of INT64_MIN takes the kernel's overflow text), x += y and ++x on var slots
			// (kinds at run time: a kind change on the slot lands through the general conversion); the same on both backends
			const char* defs =
				"var LIMIT = 6; array<int> B = [3, 1, 4, 1, 5, 9, 2, 6];"
				" var h1 = function(int n) { int acc = 0; int i = 0; while (i < n * 2) { acc = acc + i; i = i + 1; } return acc; };"
				" var h2 = function(int n) { int acc = 0; int i = 0; while (i < LIMIT + n) { acc = acc + i; i = i + 1; } return acc; };"
				" var h3 = function(var& b, int n) { int acc = 0; int i = 0; while (i < b[n]) { acc = acc + b[i]; i = i + 1; } return acc; };"
				" var m1 = function(float x0, int n) { float acc = 0.0; float x = x0; int i = 0; while (i < n) { acc = acc + math::floor(x) + math::ceil(x) + math::sqrt(x + 2.0) + math::abs(x - 3.0) + math::sin(x) + math::cos(x) + math::tan(x) + math::radians_to_degrees(x) + math::degrees_to_radians(x); x = x + 0.75; i = i + 1; } return acc; };"
				" var m2 = function(int n) { int acc = 0; int i = -n; while (i < n) { acc = acc + math::abs(i) + math::itrunc(math::sqrt(i * i)); i = i + 1; } return acc; };"
				" var m3 = function(int n) { int acc = 0; int i = 0; int v = -9223372036854775807 - 1; while (i < n) { if (i == 2) { acc = acc + math::abs(v); } i = i + 1; } return acc; };"
				" var c1 = function(var v, int n) { int i = 0; while (i < n) { v += 2; ++i; } return v; };"
				" var c2 = function(var v, int n) { int i = 0; while (i < n) { v += 0.5; i++; } return v; };"
				" var c3 = function(int n) { int i = 0; var k = 0; while (i < n) { k -= 3; k *= 2; k %= 1000; i = i + 1; } return k; };";
			const std::string script = std::string(defs) + " string r = to_string(h1(5)) + \",\" + to_string(h2(3)) + \",\" + to_string(h3(B, 4)) + \",\" + to_string(m1(0.5, 6)) + \",\" + to_string(m2(4)) + \",\" + to_string(c1(1, 5)) + \",\" + to_string(c1(1.5, 5)) + \",\" + to_string(c2(1, 4)) + \",\" + to_string(c3(6));"
			                           " try { r = r + \",\" + to_string(m3(4)); } catch (err) { r = r + \",throw:\" + err; } r;";
			both(script, "", "head spans, math, compounds");
			both(script, "", "head spans, math, compounds, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("45,36,", 0) == 0 && vm.find("throw:Integer overflow in math::abs") != std::string::npos, "the head spans and the abs overflow: " + vm);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(9), closed, "every loop closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 600) + ")");
		});
		test("var_and_auto_locals_land_by_their_declared_type", [this]() {
			// the declared-type rule: a var accumulator takes ints then floats (the kind change lands, the any stamp stays), an
			// auto local infers then enforces, an int local fed a float takes the general form's checked conversion, a float
			// local fed an int widens through a store as through a declaration; var element reads land the element's kind
			const char* defs =
				"array<int> A = [3, 1, 4, 1, 5, 9, 2, 6]; var H = [1, 2.5, 3, 4.5];"
				" var v1 = function(var& a, int n) { var acc = 0; int i = 0; while (i < n) { acc = acc + a[i]; i = i + 1; } return acc; };"
				" var v2 = function(var& h, int n) { var acc = 0; int i = 0; while (i < n) { var e = h[i]; acc = acc + e; i = i + 1; } return acc; };"
				" var v3 = function(var& a, int n) { auto acc = 0; int i = 0; while (i < n) { acc = acc + a[i] * 2; i = i + 1; } return acc; };"
				" var v4 = function(var& h, int n) { int acc = 0; int i = 0; string r = \"\"; try { while (i < n) { acc = acc + h[i]; i = i + 1; } r = to_string(acc); } catch (err) { r = \"throw:\" + err; } return r; };"
				" var v5 = function(var& a, int n) { float acc = 0.0; int i = 0; while (i < n) { int t = a[i]; acc = t; acc = acc + i; i = i + 1; } return acc; };"
				" var v6 = function(var& h, int n) { var last = 0; int i = 0; while (i < n) { last = h[i]; i = i + 1; } return last; };";
			const std::string script = std::string(defs) + " to_string(v1(A, 8)) + \",\" + to_string(v2(H, 4)) + \",\" + to_string(v3(A, 8)) + \",\" + v4(H, 4) + \",\" + to_string(v5(A, 8)) + \",\" + to_string(v6(H, 4)) + \",\" + type_of(v6(H, 4)) + \",\" + type_of(v1(A, 8));";
			both(script, "", "declared-type landings");
			both(script, "", "declared-type landings, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("31,11.000000,62,", 0) == 0, "the var, the float sum and the auto: " + vm);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			// v5's widening store from a local (`acc = t`, a LOAD and STORE pair) and v6's assignment from an element read
			// (`last = h[i]`, a pushing read and STORE) are stack pairs the table names for row 116c
			check_eq(size_t(4), closed, "the four value-model loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 600) + ")");
			check_true(listing.find(": translation at") != std::string::npos && listing.find("(INDEX_FUSED)") != std::string::npos && listing.find("(STORE)") != std::string::npos, "the two stack pairs are named gaps: " + listing);
		});
		test("temps_are_values_per_definition", [this]() {
			// a compiler temp reused by consecutive statements with different kinds (draw_seg's column loop: an int element
			// read in the test, a float subtraction in the next statement) is two values, not one slot of two kinds, so the
			// loop closes; a miss at an int overflow in the first statement, after the float definition of the same temp in the
			// previous iteration, still reports the overflow (the dead float is never flushed over the live int); both
			// backends, proofs on and off
			const char* defs =
				"array<int> done = [0, 1, 0, 0, 1, 0]; array<float> F = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5]; array<int> B = [1, 2, 3, 4, 5, 6]; array<int> C = [1, 2, 9223372036854775807, 4, 5, 6];"
				" var s1 = function(int xa, int xb, float x1f, float spanx) { float acc = 0.0; for (int x = xa; x <= xb; ++x) { if (done[x] != 0) { continue; } float t3 = (x - x1f) / spanx; acc = acc + t3 * F[x]; } return acc; };"
				" var s2 = function(var& a, int n) { int p = 0; float q = 0.0; int i = 0; while (i < n) { p = p + (a[i] + 1); q = q + (F[i] - 0.5) * 2.0; i = i + 1; } return to_string(p) + \",\" + to_string(q); };";
			const std::string script = std::string(defs) + " string r = to_string(s1(0, 5, 1.5, 0.75)) + \",\" + s2(B, 6);"
			                           " try { r = r + \",\" + s2(C, 6); } catch (err) { r = r + \",throw:\" + err; } r;";
			both(script, "", "values per definition");
			both(script, "", "values per definition, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.find(",throw:") != std::string::npos && vm.find("overflow") != std::string::npos, "the overflow after a float definition of the same temp: " + vm);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(2), closed, "both loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 400) + ")");
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
