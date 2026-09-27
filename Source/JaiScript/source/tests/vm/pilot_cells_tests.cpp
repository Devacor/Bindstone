// A closed typed loop's scalar slots run as raw cells inside one block. The contract controls: every
// shape lands the same on the interpreter and the vm: a miss right after the body-defined cell is
// written and after the first pixel store, late overflow with prior stores retained, a budget stop
// inside the cell loop, proofs off, zero and one iteration, a loop that is not closed, chained index
// stores, a float slot fed by int arithmetic, interleaved coroutines, the hoisted containers.
#include <filesystem>
#include <fstream>
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/vm/disassembler.hpp>
#include <jaiscript/vm/chunk_manifest.hpp>
#include <cstring>
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
		jai::builtin_method_registries builtins; jai::init_builtin_method_registries(symbolizer, builtins);   // the call sites' builtin indexes, as the engine's compiler has them (a push site needs its array index)
		jai::vm::vm_compiler compiler(symbolizer, &builtins);
		compiler.ir_listing(true);
		return jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
	}
	// the listing of every chunk the engine's own compiler makes for a script (the checker's proofs stamped, so the grafts of the
	// inliner appear), through the disassembly sink; the script runs on the vm
	std::string listing_run(const std::string& source) {
		auto e = engine::make();
		e->set_backend(backend_type::vm);
		stdlib::register_all(*e);
		std::string out;
		e->disassembly_sink([&out](const std::string& listing, const std::string&) { out += listing; out += "\n"; });
		try { e->execute(source); } catch (const std::exception& ex) { out += std::string("throw:") + ex.what(); }
		return out;
	}
	// the compiled program of a snippet and its symbolizer (the manifest controls mutate a copy)
	struct compiled_snippet { std::shared_ptr<engine> eng; std::shared_ptr<jai::vm::chunk> program; std::shared_ptr<jai::vm::chunk> function; };   // function: the first function's body (its loops close; program-scope loops run over environment values)
	compiled_snippet compile_snippet(const std::string& source) {
		compiled_snippet c; c.eng = engine::make();
		c.eng->set_backend(backend_type::vm);
		stdlib::register_all(*c.eng);
		auto* symbolizer = c.eng->get_symbolizer();
		auto templates = c.eng->get_registered_template_types();
		jai::lexer lex(source, symbolizer, templates);
		auto tokens = lex.tokenize();
		jai::parser p(tokens, symbolizer, c.eng.get(), templates);
		auto parse_result = p.parse();
		if (!parse_result.has_value()) { return c; }
		(void)jai::detail::run_static_check(*c.eng, parse_result.value());
		jai::vm::vm_compiler compiler(symbolizer);
		compiler.ir_listing(true);
		c.program = compiler.compile_program(parse_result.value());
		for (const auto& d : parse_result.value()) {
			if (d && d->get_type() == node_type::function_decl) {
				auto* fd = static_cast<function_decl*>(d.get());
				c.function = compiler.compile_callable(fd->name, fd->parameters, fd->body, fd->local_count);
				break;
			}
		}
		return c;
	}
	void both(const std::string& script, const std::string& expected, const char* label, bool proofs = true) {
		const std::string interp = run_on(false, script, proofs);
		const std::string vm = run_on(true, script, proofs);
		check_eq(interp, vm, std::string(label) + ": both backends");
		if (!expected.empty()) { check_eq(expected, vm, std::string(label) + ": expected"); }
	}

	void forge_tests() override {
		test("manifest_is_the_same_across_engines_and_names_no_runtime_state", [this]() {
			// row 125: two engines intern different ids for the same names; the manifest names symbols by spelling
			const std::string src = "float f = 1.0000001; function run() { int s = 0; int i = 0; while (i < 10) { s = s + i * 2; i = i + 1; } return s; } print(to_string(run()) + to_string(f));";
			auto a = compile_snippet(src); auto b = compile_snippet(src);
			check_true(a.program && b.program && a.function && b.function, "compiled");
			const std::string ma = jai::vm::chunk_manifest(*a.program, a.eng->get_symbolizer()) + jai::vm::chunk_manifest(*a.function, a.eng->get_symbolizer());
			const std::string mb = jai::vm::chunk_manifest(*b.program, b.eng->get_symbolizer()) + jai::vm::chunk_manifest(*b.function, b.eng->get_symbolizer());
			check_eq(ma, mb, "the same program and function manifest the same in two engines");
			check_true(ma.find("cell_loops 1") != std::string::npos, "the function's loop closed");
		});
		test("manifest_changes_with_a_constants_low_bit", [this]() {
			// Astra (2026-09-12 19:54): 1.0000001 and 1.0000002 list the same; the manifest carries the constant's 64 bits
			auto a = compile_snippet("float f = 1.0000001; print(to_string(f));");
			check_true(static_cast<bool>(a.program), "compiled");
			const std::string before = jai::vm::chunk_manifest(*a.program, a.eng->get_symbolizer());
			bool flipped = false;
			for (auto& c : a.program->constants) {
				if (c.raw_storage_index() != script_value::TYPEID_FLOAT) continue;
				double d = c.unchecked_as_float(); uint64_t bits = 0; std::memcpy(&bits, &d, sizeof bits); bits ^= 1u; std::memcpy(&d, &bits, sizeof d);
				c = script_value(d, static_cast<engine*>(nullptr)); flipped = true; break;
			}
			check_true(flipped, "a float constant was found");
			check_ne(before, jai::vm::chunk_manifest(*a.program, a.eng->get_symbolizer()), "one low bit of a constant changes the manifest");
		});
		test("manifest_changes_with_a_live_mask_bit_and_an_origin_entry", [this]() {
			auto a = compile_snippet("function run() { int s = 0; int i = 0; while (i < 10) { s = s + i * 2; i = i + 1; } return s; } print(to_string(run()));");
			check_true(a.function && !a.function->cell_loops.empty(), "a closed loop in the function's body (a compound with a binary right side is a translation refusal; the plain store closes)");
			const std::string before = jai::vm::chunk_manifest(*a.function, a.eng->get_symbolizer());
			auto& loop = a.function->cell_loops[0];
			check_true(!loop.live_temps.empty() && !loop.origin.empty(), "live masks and origins exist");
			loop.live_temps[0].w[0] ^= 1u;
			const std::string after_mask = jai::vm::chunk_manifest(*a.function, a.eng->get_symbolizer());
			check_ne(before, after_mask, "one live mask bit changes the manifest");
			loop.live_temps[0].w[0] ^= 1u;
			loop.origin[0] += 1;
			check_ne(before, jai::vm::chunk_manifest(*a.function, a.eng->get_symbolizer()), "one origin entry changes the manifest");
			loop.origin[0] -= 1;
			check_eq(before, jai::vm::chunk_manifest(*a.function, a.eng->get_symbolizer()), "restored, the manifest is the same again");
		});
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
			both(script, "44|10,two,3.5", "element-to-element copies, typed and var");
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
			check_true(vm.rfind("90,203,2.5,22,throw:", 0) == 0, "typed, postfix, float and reference shapes: " + vm);
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
			// Astra's diamond fixtures in while form, the texture loop's guarded arm, a continue, a break (an exit of the
			// block) and an overflow inside the taken arm: the same on both backends, proofs on and off
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
			check_true(b.find("CELLS_BACK") != std::string::npos && b.find("C_EXIT") != std::string::npos, "a break is an exit of the block: " + b);
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
			// the translation's refusals are recorded per loop: a call is an effect (a break was one until row 119f, a string append
			// a representation until row 122: both close now), a member read of a frame-slot instance is a load site (row 147a; a temp the block did not take between rows 142 and 146), a tree deeper than
			// the lift's node cap (21 binary nodes: the lifted part pushes a fused binary and the general BINARY consumes it) is a translation gap
			const std::string listing = listing_of(
				"var g = function(int x) { return x; }; class P { int v = 1; }"
				" var f1 = function(int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + g(i); i = i + 1; } return acc; };"
				" var f2 = function(int n) { int acc = 0; int i = 0; while (i < n) { if (i == 3) { break; } acc = acc + i; i = i + 1; } return acc; };"
				" var f3 = function(int n) { string s = \"\"; int i = 0; while (i < n) { s += \"x\"; i = i + 1; } return s; };"
				" var f4 = function(int n, P p) { int acc = 0; int i = 0; while (i < n) { acc = acc + p.v; i = i + 1; } return acc; };"
				" var f5 = function(int n) { int acc = 0; int i = 0; while (i < n) { int v = i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i + i; acc = acc + v; i = i + 1; } return acc; };"
				" var f6 = function(int n) { int acc = 0; int i = 0; while (i < n) { acc = acc + to_string(i).size(); i = i + 1; } return acc; };");
			check_true(listing.find("-- open loop") != std::string::npos, "refusals listed: " + listing);
			check_true(listing.find(": effect at") != std::string::npos, "the method call on a call's result is an effect: " + listing);
			check_true(listing.find("CALL_TEMP") != std::string::npos && listing.find("C_PROBE") != std::string::npos, "the call the sum consumes lands in a temp and its loop closes through the door (row 146a): " + listing);
			check_true(listing.find(": translation at") != std::string::npos && listing.find("(BINARY_FUSED)") != std::string::npos, "the tree past the lift's cap is a translation gap (the pushing fused binary the stack binary consumes): " + listing);
			check_true(listing.find("C_MLOAD") != std::string::npos && listing.find("(MEMBER_TEMP)") == std::string::npos, "the member read of a frame-slot instance is a load site and its loop closes (row 147a): " + listing);
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
			check_true(vm.rfind("31,11.0,62,", 0) == 0, "the var, the float sum and the auto: " + vm);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			// v5's widening store from a local (`acc = t`) and v6's assignment from an element read (`last = h[i]`) are move
			// rows since row 118b (a LOAD and STORE pair, a pushing read and STORE before it), so all six close
			check_eq(size_t(6), closed, "the six value-model loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 600) + ")");
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
		test("moves_are_one_row", [this]() {
			// a statement-position move from a slot, a literal or an element read is one destination-addressed row whatever the
			// checker proved: the widening store, the var store, the unproven same-kind store, the literal into a float, the
			// element read into a var and into a typed local; the row's guard is total (the same-type payload test, else the
			// exact general pair), so proofs on and off land the same, the listing has no general STORE left in these bodies,
			// and every loop closes
			const char* defs =
				"array<int> A = [3, 1, 4, 1, 5]; var H = [1, 2.5, 3];"
				" var m1 = function(int n) { float acc = 0.0; int t = 0; int i = 0; while (i < n) { t = A[i]; acc = t; i = i + 1; } return acc; };"
				" var m2 = function(var p, int n) { var q = 0; int i = 0; while (i < n) { q = p; p = i; i = i + 1; } return q; };"
				" var m3 = function(int n) { float f = 0.0; int k = 0; int i = 0; while (i < n) { f = 2; k = 7; i = i + 1; } return f + k; };"
				" var m4 = function(int n) { var last = 0; int e = 0; int i = 0; while (i < n) { last = H[i]; e = A[i]; i = i + 1; } return to_string(last) + \",\" + to_string(e); };"
				" var m5 = function(int n) { float w = 0.0; int i = 0; while (i < n) { w = A[i]; i = i + 1; } return w; };";
			const std::string script = std::string(defs) + " to_string(m1(5)) + \",\" + to_string(m2(9, 4)) + \",\" + to_string(m3(3)) + \",\" + m4(3) + \",\" + to_string(m5(5)) + \",\" + type_of(m2(2.5, 0));";
			both(script, "", "moves");
			both(script, "", "moves, proofs off", false);
			const std::string listing = listing_of(defs);
			size_t stores = 0; for (size_t at = listing.find("\tSTORE "); at != std::string::npos; at = listing.find("\tSTORE ", at + 1)) { ++stores; }
			size_t moves = 0; for (size_t at = listing.find("T_STORE_MOV"); at != std::string::npos; at = listing.find("T_STORE_MOV", at + 1)) { ++moves; }
			check_eq(size_t(0), stores, "no general STORE left in the move bodies: " + listing);
			check_true(moves >= 7, "the moves are rows: " + std::to_string(moves));
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(5), closed, "every move loop closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 400) + ")");
		});
		test("a_removed_map_entry_refuses_the_block", [this]() {
			// Astra's finding (2026-09-11): the entry dereferenced a guarded map-entry reference before the loop's own code ran,
			// so a key erased since the reference was taken raised the removed-entry text at admission on a path the general
			// form never takes; admission is non-observing, the stale entry refuses the block, and the text appears only where
			// the loop itself reads the entry (the taken companion), on both backends, proofs on and off
			const char* defs =
				"var untaken = function(int n, int zero, int one) { var table = {\"key\": [10, 20]}; var& pixels = table[\"key\"]; table.erase(\"key\"); int total = 0; int i = 0;"
				" while (i < n) { if (i < zero) { total = total + pixels[(i + zero) * (one + zero)]; } total = total + i; i = i + 1; } return total; };"
				" var taken = function(int n, int zero, int one) { var table = {\"key\": [10, 20]}; var& pixels = table[\"key\"]; table.erase(\"key\"); int total = 0; int i = 0;"
				" while (i < n) { if (i >= one) { total = total + pixels[(i + zero) * (one + zero)]; } i = i + 1; } return total; };";
			const std::string untaken_script = std::string(defs) + " to_string(untaken(1, 0, 1)) + \",\" + to_string(untaken(3, 0, 1));";
			const std::string taken_script = std::string(defs) + " string r = \"\"; try { r = to_string(taken(3, 0, 1)); } catch (err) { r = \"caught:\" + err; } r;";
			both(untaken_script, "0,3", "the untaken branch never touches the removed entry");
			both(untaken_script, "0,3", "the untaken branch, proofs off", false);
			both(taken_script, "", "the taken branch raises the removed-entry text");
			both(taken_script, "", "the taken branch, proofs off", false);
			const std::string vm = run_on(true, taken_script);
			check_true(vm.find("removed map entry") != std::string::npos, "the text where the loop reads it: " + vm);
			const std::string listing = listing_of(defs);
			check_true(listing.find("-- cell loop ") != std::string::npos, "the probe loops close, so the entry is what refuses: " + listing);
		});
		test("unaries_are_values", [this]() {
			// a unary minus, complement or not over an operand is a definition into a compiler value: in a declaration, in a
			// store, as a binary's operand, as a math argument and as an element store's value; the general unary runs
			// underneath (the overflow text at the minimum int, the numeric-operand text on a string), the loops over int and
			// float values close (neg and bnot in the copy), `!` keeps its bool on the general path; both backends, proofs on
			// and off; the listing has no general UNARY left in these bodies
			const char* defs =
				"array<int> A = [3, 1, 4, 1, 5]; array<float> F = [0.5, 1.5, 2.5]; array<int> M = [1, 1 << 63, 3]; array<int> O = [0, 0, 0];"
				" var u1 = function(int n) { int acc = 0; int i = 0; while (i < n) { int v = -A[i]; int w = ~A[i]; acc = acc + v + w; i = i + 1; } return acc; };"
				" var u2 = function(int n) { float acc = 0.0; int i = 0; while (i < n) { float g = -F[i] * 2.0; acc = acc + g; i = i + 1; } return acc; };"
				" var u3 = function(int n) { int acc = 0; int y = 0; int i = 0; while (i < n) { y = -i; acc = acc + math::abs(-A[i]) + y; O[i] = -y; i = i + 1; } return acc + O[0] + O[1] + O[2]; };"
				" var u4 = function(int n) { int acc = 0; int i = 0; while (i < n) { int v = -M[i]; acc = acc + v; i = i + 1; } return acc; };"
				" var u5 = function(int n) { int c = 0; int i = 0; while (i < n) { bool z = !A[i]; if (z) { c = c + 1; } i = i + 1; } return c; };";
			const std::string script = std::string(defs) + " string r = to_string(u1(5)) + \",\" + to_string(u2(3)) + \",\" + to_string(u3(3)) + \",\" + to_string(u5(5));"
			                           " try { r = r + \",\" + to_string(u4(3)); } catch (err) { r = r + \",throw:\" + err; } r;";
			both(script, "", "unaries");
			both(script, "", "unaries, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.find(",throw:") != std::string::npos && vm.find("unary '-'") != std::string::npos, "the negation overflow text: " + vm);
			const std::string listing = listing_of(defs);
			size_t unaries = 0; for (size_t at = listing.find("\tUNARY "); at != std::string::npos; at = listing.find("\tUNARY ", at + 1)) { ++unaries; }
			check_eq(size_t(0), unaries, "no general UNARY left in the bodies: " + listing);
			check_true(listing.find("UNARY_TEMP") != std::string::npos && listing.find("C_NEG") != std::string::npos && listing.find("C_BNOT") != std::string::npos, "the rows and their copies: " + listing);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(5), closed, "the four numeric loops and the ! loop close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 400) + ")");
		});
		test("value_doors_close", [this]() {
			// draw_sprite's `int fiv = math::itrunc((fy - y0f) * dv)` (a math call as a declaration's value), a math call as a store's
			// and an element store's value, trace_block_dz's `VX_X[LD_V1[i]]` (an element read as the index of an environment
			// array), and truth tests on int and float locals as a loop head and inside a body, all landing the same on both
			// backends with proofs on and off, every one of those loops closing, no general MATH or INDEX left in them
			const char* defs =
				"array<float> VX_X = [0.5, 1.5, 2.5, 3.5]; array<int> LD_V1 = [2, 0, 3, 1]; array<int> O = [0, 0, 0, 0];"
				" var d1 = function(int y0, int y1, float y0f, float dv, int h) { int acc = 0; for (int fy = y0; fy <= y1; ++fy) { int fiv = math::itrunc((fy - y0f) * dv); if (fiv < 0 || fiv >= h) { continue; } acc = acc + fiv; } return acc; };"
				" var d2 = function(int n) { float acc = 0.0; float r = 0.0; int i = 0; while (i < n) { r = math::sqrt(acc + 1.0); O[i] = math::itrunc(r * 2.0); acc = acc + r; i = i + 1; } return to_string(acc) + \",\" + to_string(O[0] + O[1] + O[2] + O[3]); };"
				" var d3 = function(int n) { float acc = 0.0; int i = 0; while (i < n) { float ax = VX_X[LD_V1[i]]; acc = acc + ax; i = i + 1; } return acc; };"
				" var d4 = function(int n) { int acc = 0; while (n) { acc = acc + n; n = n - 1; } return acc; };"
				" var d5 = function(int n) { int acc = 0; int i = 0; while (i < n) { int m = i % 3; if (m) { acc = acc + 1; } float f = 0.5 * i; if (f) { acc = acc + 10; } i = i + 1; } return acc; };";
			const std::string script = std::string(defs) + " to_string(d1(2, 7, 1.5, 0.75, 6)) + \",\" + d2(4) + \",\" + to_string(d3(4)) + \",\" + to_string(d4(5)) + \",\" + to_string(d5(6));";
			both(script, "", "value doors");
			both(script, "", "value doors, proofs off", false);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(5), closed, "every value-door loop closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 600) + ")");
			size_t general = 0; for (const char* op : {"\tMATH ", "\tINDEX "}) { for (size_t at = listing.find(op); at != std::string::npos; at = listing.find(op, at + 1)) { ++general; } }
			check_eq(size_t(0), general, "no general MATH or INDEX left in the bodies (the declarations at the top level are the script's own): " + listing);
		});
		test("negative_literals_and_environment_bounds", [this]() {
			// -1 is a literal: `int other = -1` takes the constant row, `while (bm[i] != -1)` takes the fused compare and its loop
			// closes (cross_check's head), -1.5 and -(-1) and INT64_MIN spelled as -9223372036854775807 - 1 land the same on both
			// backends, -x on a variable keeps the unary; a counted loop whose bound is a global (render_view's `i < VW`) closes
			const char* defs =
				"int VW = 6; array<int> CL = [1, 1, 1, 1, 1, 1]; array<int> BM = [3, 7, 9, -1, 5];"
				" var e1 = function() { int acc = 0; for (int i = 0; i < VW; ++i) { CL[i] = 0; acc = acc + i; } return acc + CL[0] + CL[5]; };"
				" var e2 = function(var& bm) { int acc = 0; int i = 0; while (bm[i] != -1) { int other = -1; acc = acc + bm[i] + other; i = i + 1; } return acc; };"
				" var e3 = function(int x) { float f = -1.5; int m = -(-1); int lo = -9223372036854775807 - 1; return to_string(f) + \",\" + to_string(m) + \",\" + to_string(lo) + \",\" + to_string(-x) + \",\" + type_of(-1) + \",\" + to_string(-0.0); };";
			const std::string script = std::string(defs) + " to_string(e1()) + \",\" + to_string(e2(BM)) + \",\" + e3(7);";
			both(script, "15,16,-1.5,1,-9223372036854775808,-7,int,-0.0", "negative literals and environment bounds");
			both(script, "", "proofs off", false);
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(2), closed, "the environment-bound loop and the -1 head close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 600) + ")");
			size_t unaries = 0; for (const char* op : {"\tUNARY ", "\tUNARY_TEMP "}) { for (size_t at = listing.find(op); at != std::string::npos; at = listing.find(op, at + 1)) { ++unaries; } }
			check_eq(size_t(1), unaries, "the one unary left is -x on the variable: " + listing);
		});
		test("calls_inside_a_block", [this]() {
			// draw_seg's shape: a call landing in a declaration, a statement call with a computed and a reference argument, float
			// math around them, an element read of a var& container guarding a continue; and every effect a callee can have on the
			// block, re-read after the call: a global the loop reads, an array the loop reads (grown by the callee), an int& taken
			// on a loop local (the slot is the value from then on), a float result into an int declaration (the checked conversion
			// and its rejection), a throw out of the callee into the caller's catch (materialized locals), a recursive callee, a
			// zero-argument callee, a string result into a var, and the argument values: a negation, a math call, an element read,
			// an environment name
			const char* defs = R"JAI(
int K = 1; array<int> G = [1, 2, 3, 4, 5, 6]; var FLATS = [[10, 20], [30, 40]]; array<int> DONE = [0, 1, 0, 0, 1, 0];
var light_band = function(int light, float dist) -> int { return light + math::itrunc(dist); };
var col = function(int x, int y0, int y1, var& flat, float ph, float rdx, float rdy, int light) -> int { return x + y0 + y1 + flat[0] + math::itrunc(ph + rdx + rdy) + light; };
var seg = function(int n, var& done) { int acc = 0; float dist = 1.5; int light = 3; var& cflat = FLATS[1]; float fceil = 2.0;
  for (int x = 0; x < n; ++x) { if (done[x] != 0) { continue; } float rdx = x * 0.5; float rdy = rdx + 1.0; int band = light_band(light, dist); acc = col(x, band + 1, acc, cflat, fceil, rdx, rdy, light); }
  return acc; };
var bump = function() -> int { K = K + 1; return K; };
var globals = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + K; int r = bump(); acc = acc + r; } return acc; };
var grow = function(var& a) -> int { a.push(9); return a.size(); };
var arrays = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + G[i]; int s = grow(G); acc = acc + s; } return acc; };
var inc = function(int& v) -> void { v = v + 100; };
var escape = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int t = i * 2; inc(t); acc = acc + t; } return acc; };
var half = function(int a) -> float { return a * 0.5; };
var narrow = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int q = half(i * 4); acc = acc + q; } return acc; };
var huge = function(int a) -> float { return 1e300; };
var rejects = function(int n) { int acc = 0; try { for (int i = 0; i < n; ++i) { acc = acc + i; int q = huge(i); acc = acc + q; } } catch (e) { return to_string(acc) + " " + e; } return to_string(acc); };
var boom = function(int i) -> int { if (i == 2) { throw "boom at " + to_string(i); } return i; };
var thrown = function(int n) { int acc = 0; float f = 0.5; try { for (int i = 0; i < n; ++i) { int b = boom(i); acc = acc + b; f = f * 2.0; } } catch (e) { return to_string(acc) + " " + to_string(f) + " " + e; } return to_string(acc); };
var fact = function(int k) -> int { if (k <= 1) { return 1; } return k * fact(k - 1); };
var recursive = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int fk = fact(i); acc = acc + fk; } return acc; };
var tick = function() -> int { return 7; };
var zero = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int t = tick(); acc = acc + t; } return acc; };
var name = function(int i) -> string { return "n" + to_string(i); };
var strings = function(int n) { var s = ""; for (int i = 0; i < n; ++i) { var w = name(i); s = s + w; } return s; };
var values = function(int n) { int acc = 0; float r = 2.5; for (int i = 0; i < n; ++i) { int a1 = light_band(-i, r); int a2 = light_band(math::itrunc(r), 1.0); int a3 = light_band(G[i], 2.0); int a4 = light_band(K, 0.5); acc = acc + a1 + a2 + a3 + a4; } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(seg(6, DONE));", "184" }, { "to_string(globals(3));", "15" }, { "to_string(arrays(3));", "30" }, { "to_string(escape(3));", "306" },
				{ "to_string(narrow(3));", "6" }, { "rejects(3);", "" }, { "thrown(4);", "1 2.0 boom at 2" }, { "to_string(recursive(5));", "34" },
				{ "to_string(zero(3));", "21" }, { "strings(3);", "n0n1n2" }, { "to_string(values(3));", "27" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(11), closed, "every loop with a call closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
			size_t calls = 0; for (size_t at = listing.find("\tC_CALL "); at != std::string::npos; at = listing.find("\tC_CALL ", at + 1)) { ++calls; }
			check_eq(size_t(15), calls, "one C_CALL per call in a closed loop: " + listing);
		});
		test("globals_written_inside_a_block", [this]() {
			// draw_seg's REMAIN = REMAIN - 1: an environment scalar written by a fused store, a compound, an increment, a move and a
			// call result; the head reading the written value; the value seen by a callee (written back ahead of the call) and
			// rewritten by one (re-read after it); a var global changing kind; an int global fed a float (the general conversion);
			// a float global fed ints; a throw after the write (the catch sees the written value)
			const char* defs = R"JAI(
int REMAIN = 10; var V = 1; int T = 1; float F = 0.0; int SEEN = 0;
var peek = function() -> int { return REMAIN; };
var poke = function(int v) -> void { REMAIN = v; };
var boom = function() -> void { throw "boom"; };
var count_down = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN = REMAIN - 1; acc = acc + REMAIN; } return acc; };
var compound = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN -= 2; REMAIN--; acc = acc + REMAIN; } return acc; };
var head = function() { int acc = 0; while (REMAIN > 0) { REMAIN = REMAIN - 1; acc = acc + 1; } return acc; };
var seen_by_callee = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN = REMAIN + 1; int p = peek(); acc = acc + p; } return acc; };
var written_by_callee = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN = REMAIN + 1; poke(100); acc = acc + REMAIN; } return acc; };
var var_kind = function(int n) { for (int i = 0; i < n; ++i) { V = V * 1.5; } return to_string(V) + " " + type_of(V); };
var narrow = function(int n) { try { for (int i = 0; i < n; ++i) { T = T * 1.5; } } catch (e) { return "caught " + to_string(T); } return to_string(T); };
var widen = function(int n) { for (int i = 0; i < n; ++i) { F = F + i; } return to_string(F); };
var thrown = function(int n) { try { for (int i = 0; i < n; ++i) { REMAIN = REMAIN + 10; if (i == 1) { boom(); } } } catch (e) { return to_string(REMAIN) + " " + e; } return to_string(REMAIN); };
var mov = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN = i; SEEN = 7; acc = acc + REMAIN + SEEN; } return acc; };
var from_call = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { REMAIN = peek(); acc = acc + REMAIN; } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(count_down(3));", "24" }, { "to_string(compound(2));", "11" }, { "to_string(head());", "10" }, { "to_string(seen_by_callee(3));", "36" },
				{ "to_string(written_by_callee(3));", "300" }, { "var_kind(2);", "2.25 float" }, { "narrow(2);", "" }, { "widen(3);", "3.0" },
				{ "thrown(3);", "30 boom" }, { "to_string(mov(3));", "24" }, { "to_string(from_call(2));", "20" },
				{ "count_down(2); to_string(REMAIN);", "8" }, { "head(); to_string(REMAIN);", "0" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(11), closed, "every loop writing a global closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
		});
		test("element_store_values_are_values", [this]() {
			// draw_seg's `clipsn[x] = cn + 1`: an element store whose value is a value binary lands the binary in a compiler temp and
			// stores the temp; the store's typed lane and the block hold; a lifted tree, a float value, a value that raises (the
			// kernel's text first), an int array fed a float value (the checked conversion), a map container and a var& container
			const char* defs = R"JAI(
array<int> G = [0, 0, 0, 0, 0, 0]; array<float> F = [0.0, 0.0, 0.0, 0.0]; var M = {1: 0, 2: 0}; var V = [0, 0, 0, 0];
var fill = function(int n, var& a) { int acc = 0; for (int x = 0; x < n; ++x) { int v = a[x]; a[x] = v + x * 2 + 1; acc = acc + a[x]; } return acc; };
var tree = function(int n) { for (int i = 0; i < n; ++i) { G[i] = i * 3 + (i - 1) * 2; } return to_string(G); };
var floats = function(int n) { for (int i = 0; i < n; ++i) { F[i] = i * 0.5 + 1.0; } return to_string(F); };
var raises = function(int n) { int d = 0; try { for (int i = 0; i < n; ++i) { G[i] = i / d; } } catch (e) { return e; } return "no"; };
var narrow = function(int n) { try { for (int i = 0; i < n; ++i) { G[i] = i * 1.5; } } catch (e) { return e; } return to_string(G); };
var keyed = function(int n) { for (int i = 1; i <= n; ++i) { M[i] = i * 10 + 1; } return to_string(M); };
var mixed = function(int n) { for (int i = 0; i < n; ++i) { V[i] = i + 0.5; } return to_string(V); };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(fill(4, G));", "16" }, { "tree(4);", "" }, { "floats(4);", "" }, { "raises(3);", "" }, { "narrow(3);", "" }, { "keyed(2);", "" }, { "mixed(3);", "" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_ge(closed, size_t(5), "the stores with value binaries close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
			size_t pushing = 0; for (size_t at = listing.find("\tBINARY_FUSED op="); at != std::string::npos; at = listing.find("\tBINARY_FUSED op=", at + 1)) { ++pushing; }
			check_eq(size_t(0), pushing, "no pushing fused binary feeds an element store: " + listing);
		});
		test("bools_are_cells", [this]() {
			// draw_seg's `if (solid)`, `if (fsky && bsky)`, `if (!solid && ...)`, `bool painted = false; painted = true;`: bool locals
			// and globals are cells; a truth test is the language's truthiness of an int, a float or a bool (a local or an
			// environment value, as a head or in a body), `!x` and a comparison land bools, a bool argument boxes, a bool result
			// lands, a bool into an int declaration takes the general conversion
			const char* defs = R"JAI(
bool NIGHTMARE = true; bool RUNNING = true; int TICKS = 0; var FLAG = false;
var is_odd = function(int x) -> bool { return (x & 1) == 1; };
var take = function(bool b, int x) -> int { if (b) { return x; } return -x; };
var seg = function(int n, bool solid, bool fsky, bool bsky) { int acc = 0; for (int x = 0; x < n; ++x) { bool painted = false; if (solid) { acc = acc + 1; painted = true; } if (fsky && bsky) { acc = acc + 10; } if (!solid && x > 1) { acc = acc + 100; } if (painted) { acc = acc + 1000; } } return acc; };
var values = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { bool lt = i < 2; bool odd = is_odd(i); var neg = !lt; int t = take(lt, i); acc = acc + t; if (odd) { acc = acc + 1; } if (neg) { acc = acc + 5; } } return acc; };
var env_tests = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { if (NIGHTMARE) { acc = acc + 1; } if (!RUNNING) { acc = acc + 100; } if (FLAG) { acc = acc + 1000; } FLAG = i == 1; } return acc; };
var head = function() { int acc = 0; while (RUNNING) { TICKS = TICKS + 1; acc = acc + 1; if (TICKS >= 4) { RUNNING = false; } } return acc; };
var narrow = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { bool b = i < 2; int q = b; acc = acc + q; } return acc; };
var floats = function(int n) { int acc = 0; float f = -1.0; for (int i = 0; i < n; ++i) { f = f + 0.5; if (f) { acc = acc + 1; } var nz = !f; if (nz) { acc = acc + 10; } } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(seg(4, true, true, false));", "4004" }, { "to_string(seg(4, false, true, true));", "240" }, { "to_string(values(4));", "8" },
				{ "to_string(env_tests(3));", "1003" }, { "to_string(head()) + \" \" + to_string(RUNNING);", "4 false" }, { "to_string(narrow(3));", "2" }, { "to_string(floats(4));", "13" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(6), closed, "every loop over bools closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
		});
		test("compound_binary_operands_land_in_temps", [this]() {
			// x op= a op b inside a loop: the binary lands in a temp and the compound reads it; the results, the string rows, the errors
			// of the binary and of the compound are the general form's
			const std::string script = R"(
				function run() -> string {
					float fs = 0.0; int n = 0; string s = "";
					for (int i = 0; i < 6; ++i) { float v = i * 0.5; fs += math::sqrt(v) * 1.5 - v / 3.0; n += i * i - 1; s += to_string(i + 1); }
					string e1 = ""; int z = 0;
					try { for (int i = 0; i < 3; ++i) { n += i / z; } } catch (ex) { e1 = ex; }
					string e2 = ""; int big = 9223372036854775807;
					try { for (int i = 0; i < 3; ++i) { big += i * 2 + 1; } } catch (ex) { e2 = ex; }
					return "" + fs + "|" + n + "|" + s + "|" + e1 + "|" + e2 + "|" + big;
				}
				run();
			)";
			const std::string interp = run_on(false, script);
			check_eq(interp, run_on(true, script), "compound temps: interpreter and vm agree");
			check_true(interp.find("|49|123456|") != std::string::npos, interp.c_str());
		});

		test("member_reads_of_slot_instances_close_loops", [this]() {
			// a member read in a truth test, a compound operand and a declaration lands in a member temp: the loop closes, and every
			// kernel fact holds (a user accessor runs per read, a call in the body that changes a field is seen, a null receiver raises)
			const std::string script = R"(
				class En { bool alive = true; float x = 0.0; En(float a) { x = a; alive = a > 3.0; } }
				class Acc { int n = 0; int reads = 0; function _get_n() -> int { reads += 1; return n; } }
				function bump(En& e) -> void { e.x = e.x + 100.0; }
				function run() -> string {
					array<En> ens = []; for (int i = 0; i < 8; ++i) { ens.push(En(i * 1.0)); }
					float s = 0.0; float t = 0.0;
					for (int r = 0; r < 3; ++r) {
						for (auto& e : ens) { if (!e.alive) { continue; } s += e.x; float v = e.x; t = t + v; }
					}
					En p = En(5.0); float u = 0.0;
					for (int i = 0; i < 4; ++i) { if (p.alive) { u += p.x; } if (i == 1) { bump(p); } }
					Acc a = Acc(); a.n = 2; int w = 0;
					for (int i = 0; i < 5; ++i) { w += a.n; }
					string err = "";
					En q = null;
					try { for (int i = 0; i < 2; ++i) { u += q.x; } } catch (ex) { err = ex; }
					return "" + s + "," + t + "," + u + "," + w + "," + a.reads + "|" + err;
				}
				run();
			)";
			both(script, "66.0,66.0,220.0,10,5|Cannot access 'x' on null ('q' is null)", "member temps");
			const std::string listing = listing_run(R"(
				class En { bool alive = true; float x = 0.0; }
				function f(array<En> ens) -> float { float s = 0.0; for (auto& e : ens) { if (!e.alive) { continue; } s += e.x; } return s; }
				array<En> xs = [En(), En()]; f(xs);
			)");
			check_true(listing.find("-- cell loop") != std::string::npos, "the range-for over instances closes");
		});

		test("size_heads_land_in_temps", [this]() {
			// k < xs.size() as a loop head: the size lands in a temp and the loop closes; the size is read live (a push inside the body
			// grows it), a string and a map answer size and length, a null receiver raises the call's text, a class with its own size()
			// keeps its method
			const std::string script = R"(
				class Box { int n = 3; function size() -> int { return n; } }
				function run() -> string {
					array<int> xs = [1, 2, 3]; int s = 0;
					for (int k = 0; k < xs.size(); ++k) { s += xs[k]; if (xs.size() < 6) { xs.push(k + 10); } }
					string t = "abcd"; int c = 0;
					for (int k = 0; k < t.length(); ++k) { c += k; }
					map<string, int> m = {"a": 1, "b": 2}; int d = 0;
					for (int k = 0; k < m.size(); ++k) { d += 10; }
					Box b = Box(); int e = 0;
					for (int k = 0; k < b.size(); ++k) { e += 1; }
					string err = "";
					var z = null;
					try { for (int k = 0; k < z.size(); ++k) { s += 1; } } catch (ex) { err = ex; }
					return "" + s + "," + xs.size() + "," + c + "," + d + "," + e + "|" + err;
				}
				run();
			)";
			const std::string interp = run_on(false, script);
			check_eq(interp, run_on(true, script), "size temps: interpreter and vm agree");
			check_true(interp.find("39,6,6,20,3|") == 0, interp.c_str());
			const std::string listing = listing_run(R"(
				function f(array<int> xs) -> int { int s = 0; for (int k = 0; k < xs.size(); ++k) { s += xs[k]; } return s; }
				f([1, 2, 3]);
			)");
			check_true(listing.find("-- cell loop") != std::string::npos, "the size head closes");
		});

		test("compound_element_operands_land_in_temps", [this]() {
			// x op= a[i] (and a size, a unary or a math operand) inside a loop: the operand lands in a temp and the compound reads it; the
			// sums, the float promotion, an out-of-range read, an overflow and a string element are the general form's
			const std::string script = R"(
				function run() -> string {
					array<int> xs = [1, 2, 3, 4]; array<float> fs = [0.5, 1.5];
					int s = 0; float g = 0.0; int n = 0; float h = 1.0;
					for (int k = 0; k < xs.size(); ++k) { s += xs[k]; g += xs[k]; n -= -xs[k]; h *= fs[k % 2]; n += xs.size(); }
					string e1 = "";
					try { for (int k = 0; k < 9; ++k) { s += xs[k]; } } catch (ex) { e1 = ex; }
					array<int> big = [9223372036854775807, 1]; int b = 0; string e2 = "";
					try { for (int k = 0; k < 2; ++k) { b += big[k]; } } catch (ex) { e2 = ex; }
					array<string> ws = ["a", "b"]; string w = "";
					for (int k = 0; k < 2; ++k) { w += ws[k]; }
					return "" + s + "," + g + "," + n + "," + h + "," + w + "|" + e1 + "|" + e2;
				}
				run();
			)";
			const std::string interp = run_on(false, script);
			check_eq(interp, run_on(true, script), "compound element temps: interpreter and vm agree");
			check_true(interp.find("20,10.0,26,0.5625,ab|") == 0, interp.c_str());
			const std::string listing = listing_run(R"(
				function f(array<int> xs) -> int { int s = 0; for (int k = 0; k < xs.size(); ++k) { s += xs[k]; } return s; }
				f([1, 2, 3]);
			)");
			check_true(listing.find("C_APPEND") == std::string::npos, listing.c_str());
		});

		test("a_block_holds_up_to_256_cells", [this]() {
			// draw_seg's column loop with its column bodies grafted holds more than 128 cells (its locals, the outer locals it reads, its
			// compiler values, its argument values, its environment values and the grafts' locals); loops over 80 and 140 locals close
			// and land the same, the boundary sits at 256 cells
			auto wide = [](int n) {
				std::string s = "var f = function(int n) { ";
				for (int i = 0; i < n; ++i) { s += "int a" + std::to_string(i) + " = " + std::to_string(i) + "; "; }
				s += "for (int k = 0; k < 3; ++k) { ";
				for (int i = 0; i < n; ++i) { s += "a" + std::to_string(i) + " = a" + std::to_string(i) + " + k; "; }
				s += "} int acc = 0; ";
				for (int i = 0; i < n; ++i) { s += "acc = acc + a" + std::to_string(i) + "; "; }
				s += "return acc; };";
				return s;
			};
			// the limit's boundary: n locals and the counter are n + 1 cells; 255 locals close, 256 stay open
			for (int n : {63, 64, 65, 80, 127, 128, 140, 255}) { both(wide(n) + " to_string(f(3));", std::to_string(n * (n - 1) / 2 + n * 3), (std::to_string(n) + " locals").c_str()); const std::string l = listing_of(wide(n)); check(l.find("-- cell loop ") != std::string::npos, std::to_string(n) + " locals close: " + l.substr(l.find("-- ") == std::string::npos ? l.size() : l.find("-- "), 200)); }
			both(wide(256) + " to_string(f(3));", std::to_string(256 * 255 / 2 + 256 * 3), "256 locals"); check(listing_of(wide(256)).find("too many cells") != std::string::npos, "256 locals stay open");
		});
		test("body_declared_locals_enter_empty", [this]() {
			// draw_seg's column loop declares locals inside branches the first iterations never take, so their slots held
			// placeholders at every entry and the block was refused; a local the body declares enters empty, its declaration is
			// the first write (an int takes ints, a float widens, a var takes any kind), and the loop lands the same on both backends
			const char* defs = R"JAI(
var branches = function(int n) { int acc = 0; for (int x = 0; x < n; ++x) { if (x > 2) { int v = x * 2; float w = v; acc = acc + v + math::itrunc(w); } if (x == 4) { var any = x; acc = acc + any; } float f = x * 0.5; acc = acc + math::itrunc(f); } return acc; };
var late = function(int n) { int acc = 0; for (int x = 0; x < n; ++x) { if (x >= 1) { int q = x; acc = acc + q; } } return acc; };
var clamped = function(int n) { int acc = 0; for (int x = 0; x < n; ++x) { int c1 = x - 1; if (c1 > 2) { c1 = 2; } var w = x; if (x > 3) { w = 3; } acc = acc + c1 + w; } return acc + w_kind(n); };
var w_kind = function(int n) { var w = 1; for (int x = 0; x < n; ++x) { var v = x; if (x > 1) { v = x * 0.5; } w = v; } return math::itrunc(w * 10); };
)JAI";
			both(std::string(defs) + " to_string(branches(6));", "58", "locals declared in branches");
			both(std::string(defs) + " to_string(branches(6));", "", "proofs off", false);
			both(std::string(defs) + " to_string(late(5));", "10", "a local declared past the first iteration");
			both(std::string(defs) + " to_string(clamped(6));", "43", "body-declared locals re-assigned in a branch");
			both(std::string(defs) + " type_of(w_kind(4)) + to_string(w_kind(4));", "int15", "a var declared in the body changes kind");
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(4), closed, "the four loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 400) + ")");
		});
		test("a_landing_sees_what_the_call_changed", [this]() {
			// the landing after a call re-checks the containers by identity and re-reads the environment cells through their
			// bindings: a callee that rebinds the container a var& local aliases, that rebinds a container the loop names, that
			// grows the container, that writes a scalar global the loop reads, or that needs a frame environment (the epoch
			// moves) lands the same on both backends
			const char* defs = R"JAI(
array<int> D = [1, 2, 3, 4, 5, 6, 7, 8];
array<int> E = [10, 20, 30, 40, 50, 60, 70, 80];
var D2 = [1, 2, 3, 4, 5, 6, 7, 8];
array<int> W = [5, 6, 7, 8];
int G = 100;
function swap_d(int i) -> void { if (i == 3) { D = E; } }
function swap_d2(int i) -> void { if (i == 3) { D2 = [10, 20, 30, 40, 50, 60, 70, 80]; } }
function bump_g(int i) -> void { G = G + i; }
function grow_w(int i) -> void { if (i == 2) { W.push(1000); W.push(2000); } }
function env_callee(int i) -> int { var f = function() { return i * 2; }; return f(); }
var through_ref = function(int n) { var& d = D; int acc = 0; for (int i = 0; i < n; ++i) { swap_d(i); acc = acc + d[i]; } return acc; };
var by_name = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { swap_d(i); acc = acc + D[i]; } return acc; };
var through_ref2 = function(int n) { var& d = D2; int acc = 0; for (int i = 0; i < n; ++i) { swap_d2(i); acc = acc + d[i]; } return acc; };
var reads_g = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { bump_g(i); acc = acc + G; } return acc; };
var grows = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { grow_w(i); acc = acc + W[i]; } return acc; };
var via_env = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int v = env_callee(i); acc = acc + v + G; } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(through_ref(8));", "306" }, { "to_string(by_name(8));", "306" }, { "to_string(through_ref2(8));", "306" },
				{ "to_string(reads_g(5));", "520" }, { "to_string(grows(6));", "3026" }, { "to_string(via_env(4));", "412" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(6), closed, "the six loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
		});
		test("exits_leave_the_block", [this]() {
			// a break, a compare whose edge leaves the loop, and a return inside the loop (bare, with a value, with an element read
			// pushed for it) end the block: the cells flush and the general form runs the statement or continues at the target
			const char* defs = R"JAI(
array<int> A = [3, 1, 4, 1, 5, 9, 2, 6];
var first_over = function(int n, int lim) { int acc = 0; int found = -1; for (int i = 0; i < n; ++i) { acc = acc + A[i]; if (A[i] > lim) { found = i; break; } } return acc * 100 + found; };
var while_break = function(float lim) { int i = 0; int acc = 0; while (i < 100) { float f = i * 1.5; if (f > lim) { break; } acc = acc + i; i = i + 1; } return acc * 10 + i; };
var and_head = function(int n) { int i = 0; int acc = 0; while (i < n && acc < 20) { acc = acc + i; i = i + 1; } return acc * 100 + i; };
var ret_bare = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + i; if (acc > 6) { return acc; } } return -acc; };
var ret_elem = function(int n) { for (int i = 0; i < n; ++i) { int v = A[i]; if (v == 4) { return A[i + 1]; } } return -1; };
var ret_expr = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + i * 2; if (i == 2) { return acc + 1000; } } return acc; };
var nested_break = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int t = i * 3; if (t > 6) { if (t > 9) { break; } acc = acc + 100; } acc = acc + t; } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(first_over(8, 4));", "1404" }, { "to_string(while_break(4.0));", "33" }, { "to_string(and_head(10));", "2107" },
				{ "to_string(ret_bare(10));", "10" }, { "to_string(ret_elem(8));", "1" }, { "to_string(ret_expr(6));", "1006" }, { "to_string(nested_break(6));", "118" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(7), closed, "every loop with an exit closes (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
			check(listing.find("C_EXIT") != std::string::npos, "the exits are in the copies: " + listing);
		});
		test("a_callee_may_reload_arm_operators_or_unwind_through_the_block", [this]() {
			// the call door's boundaries through a native callee: one that rebinds the function the loop calls (the probe resolves
			// the callee at every call), one that rebinds a container the loop reads by name and one that rebinds the container a
			// var& local aliases (the landing checks the sites by identity), one that arms custom numeric operators (the landing
			// leaves the block for the general form from then on), and a throw out of a callee that unwinds through the looping
			// frame into its caller's catch (the record's cell state resets for the next call); the interpreter is the oracle
			const char* defs = R"JAI(
var f = function(int i) -> int { return i; };
var f2 = function(int i) -> int { return i * 100; };
array<int> G = [1, 2, 3, 4, 5, 6, 7, 8];
array<int> G2 = [10, 20, 30, 40, 50, 60, 70, 80];
var swap_f_at = function(int i, int at) -> void { if (i == at) { host_swap_f(); } };
var rebind_g_at = function(int i, int at) -> void { if (i == at) { host_rebind_g(); } };
var arm_at = function(int i, int at) -> void { if (i == at) { host_arm_ops(); } };
var calls_f = function(int n, int at) { int acc = 0; for (int i = 0; i < n; ++i) { swap_f_at(i, at); int r = f(i); acc = acc + r; } return acc; };
var reads_g = function(int n, int at) { int acc = 0; for (int i = 0; i < n; ++i) { rebind_g_at(i, at); acc = acc + G[i]; } return acc; };
var reads_alias = function(int n, int at) { var& g = G; int acc = 0; for (int i = 0; i < n; ++i) { rebind_g_at(i, at); acc = acc + g[i]; } return acc; };
var armed = function(int n, int at) { int acc = 0; float w = 0.5; for (int i = 0; i < n; ++i) { arm_at(i, at); int r = f(i); acc = acc + r * 2; w = w + r; } return to_string(acc) + " " + to_string(w); };
var boom = function(int i) -> int { if (i == 3) { throw "boom " + to_string(i); } return i; };
var looping = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int b = boom(i); acc = acc + b * 10; } return acc; };
var across = function(int n) { string s = ""; try { s = to_string(looping(n)); } catch (e) { s = "caught " + e; } return s + "|" + to_string(looping(2)); };
)JAI";
			auto run_with = [&](bool vm, const std::string& script, bool proofs) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				e->add_function("host_swap_f", [raw]() { raw->execute("f = f2;"); });
				e->add_function("host_rebind_g", [raw]() { raw->execute("G = G2;"); });
				e->add_function("host_arm_ops", [raw]() { raw->set_has_custom_numeric_operators(true); });
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "to_string(calls_f(6, 3));", "1203" }, { "to_string(calls_f(6, 0));", "1500" }, { "to_string(calls_f(6, 9));", "15" },
				{ "to_string(reads_g(6, 2));", "183" }, { "to_string(reads_alias(6, 2));", "183" }, { "to_string(reads_g(6, 0));", "210" },
				{ "armed(6, 3);", "30 15.5" }, { "armed(6, 0);", "30 15.5" }, { "across(6);", "caught boom 3|10" }, { "across(2);", "10|10" },
			};
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string script = std::string(defs) + " " + p.call;
					const std::string interp = run_with(false, script, proofs), vm = run_with(true, script, proofs);
					check_eq(interp, vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(5), closed, "the five loops with calls close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
		});
		test("the_region_lists_its_operations_and_control_flow", [this]() {
			// the listing prints each closed region's canonical operations with their control flow: a while loop's head compare
			// exits, a diamond's compare targets an operation and its else arm jumps, a break is an exit, a counted loop's continue
			// jumps to the back edge; the region verifier (must-define over that graph) accepts every loop the suite closes
			const char* defs = R"JAI(
var diamond = function(int n) { int total = 0; int i = 0; while (i < n) { if ((i % 2) == 0) { total += i; } else { total -= i; } i = i + 1; } return total; };
var brk = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { if (i == 3) { break; } acc = acc + i; } return acc; };
var cont = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { if (i == 1) { continue; } acc = acc + i; } return acc; };
)JAI";
			both(std::string(defs) + " to_string(diamond(6)) + \",\" + to_string(brk(10)) + \",\" + to_string(cont(10));", "-3,3,44", "the three loops");
			const std::string listing = listing_of(defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(3), closed, "the three loops close (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
			size_t ir_lines = 0; for (size_t at = listing.find("-- ir "); at != std::string::npos; at = listing.find("-- ir ", at + 1)) { ++ir_lines; }
			check_eq(size_t(3), ir_lines, "one IR line per closed region: " + listing);
			check(listing.find("-- ir 0:cmp->exit") != std::string::npos, "the while loop's head compare exits: " + listing);
			check(listing.find(":jump->") != std::string::npos, "the diamond's else arm is a jump to an operation: " + listing);
			check(listing.find(":exit->exit") != std::string::npos, "the break is an exit: " + listing);
			check(listing.find(":jump->back") != std::string::npos, "the continue jumps to the back edge: " + listing);
		});
		test("pinned_operations_skip_their_kind_checks", [this]() {
			// a loop over declared ints and floats runs its arithmetic and compares in the pinned forms; a var local (an adopted kind)
			// keeps the checked forms and a kind change inside the loop still lands the same; an int declared from a float
			// converts through the general form on both backends
			const char* defs = R"JAI(
var ints = function(int n) { int acc = 0; for (int i = 0; i < n; ++i) { int t = i * 3 - 1; if (t > 4) { acc = acc + t; } } return acc; };
var flts = function(int n) { float acc = 0.0; float k = 0.5; int i = 0; while (i < n) { acc = acc + k * 1.5; if (acc > 3.0) { acc = acc - 1.0; } i = i + 1; } return math::itrunc(acc * 100.0); };
var adopted = function(int n) { var v = 1; int acc = 0; for (int i = 0; i < n; ++i) { v = v + i; if (i == 2) { v = v * 1.5; } acc = acc + math::itrunc(v); } return to_string(acc) + " " + type_of(v); };
var narrows = function(int n) { int acc = 0; float f = 0.5; for (int i = 0; i < n; ++i) { f = f + 1.25; int q = f; acc = acc + q; } return acc; };
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = { { "to_string(ints(6));", "38" }, { "to_string(flts(8));", "300" }, { "adopted(5);", "31 float" }, { "to_string(narrows(4));", "13" } };
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			const std::string listing = listing_of(defs);
			check(listing.find("C_ARITH_P") != std::string::npos && listing.find("C_JCMP_P") != std::string::npos && listing.find("C_CMP_P") != std::string::npos, "the pinned forms are emitted: " + listing);
			const std::string adopted_listing = listing_of("var adopted = function(int n) { var v = 1; int acc = 0; for (int i = 0; i < n; ++i) { v = v + i; acc = acc + math::itrunc(v); } return acc; };");
			check(adopted_listing.find("C_ARITH_ANY") != std::string::npos, "an adopted kind keeps the run-time kinds: " + adopted_listing);
			// the switch off: the checked forms only, the same results (the differential the fuzz's --no-passes chain runs)
			auto run_passes_off = [&](bool vm, const std::string& script) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				e->ir_passes(false);
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (const probe& p : probes) { check_eq(run_passes_off(false, std::string(defs) + " " + p.call), run_passes_off(true, std::string(defs) + " " + p.call), std::string(p.call) + " passes off: both backends"); }
			{
				auto e = engine::make();
				e->set_backend(backend_type::vm);
				stdlib::register_all(*e);
				auto* symbolizer = e->get_symbolizer();
				auto templates = e->get_registered_template_types();
				jai::lexer lex(defs, symbolizer, templates);
				auto tokens = lex.tokenize();
				jai::parser p(tokens, symbolizer, e.get(), templates);
				auto parse_result = p.parse();
				check_true(parse_result.has_value(), "the pinned defs parse");
				(void)jai::detail::run_static_check(*e, parse_result.value());
				jai::vm::vm_compiler compiler(symbolizer);
				compiler.ir_passes(false);
				const std::string off = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
				check(off.find("C_ARITH_P") == std::string::npos && off.find("C_JCMP_P") == std::string::npos && off.find("C_CMP_P") == std::string::npos && off.find("C_ARITH ") != std::string::npos, "the switch off emits the checked forms only: " + off);
			}
		});
		test("a_redeclared_global_after_a_call_lands_by_its_new_type", [this]() {
			// Astra's fourth finding: a native callee redeclares the global the loop holds as an environment cell (`var G` becomes
			// `int G = 5`, the raw kind unchanged); the next iteration's float store must take the int boundary (the general form's
			// conversion), so the landing misses on the stamp change and both backends land the same
			const char* defs = R"JAI(
var G = 1;
var retag_at = function(int i, int at) -> void { if (i == at) { host_retag(); } };
var probe = function(int n, int at) { int i = 0; while (i < n) { retag_at(i, at); if (i == 2) { G = 1.5; } else { G = G + 1; } i = i + 1; } return to_string(G) + " " + type_of(G); };
)JAI";
			// same_unit: the host redeclares through the program's own unit (a reload of its file writes in place, the retag this row
			// is about); otherwise through a literal, another unit, which shadows and leaves the loop on its own cell
			auto run_with = [&](bool vm, const std::string& script, bool proofs, bool same_unit) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				e->disable(jai::filesystem);
				engine* raw = e.get();
				const std::string path = "D:/virtual/pilot/retag_unit.jai";
				e->add_function("host_retag", [raw, same_unit, path]() { if (same_unit) { raw->execute_file_source(path, "int G = 5;"); } else { raw->execute("int G = 5;"); } });
				try { return (same_unit ? e->execute_file_source(path, script) : e->execute(script)).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; };
			const probe probes[] = { { "probe(4, 2);" }, { "probe(4, 0);" }, { "probe(4, 9);" }, { "probe(6, 3);" } };
			for (const probe& p : probes) {
				for (bool same_unit : { true, false }) {
					for (bool proofs : { true, false }) {
						const std::string script = std::string(defs) + " " + p.call;
						const std::string interp = run_with(false, script, proofs, same_unit), vm = run_with(true, script, proofs, same_unit);
						check_eq(interp, vm, std::string(p.call) + (same_unit ? " same unit" : " other unit") + (proofs ? "" : " proofs off") + ": both backends");
					}
				}
			}
			check(run_with(true, std::string(defs) + " probe(4, 2);", true, true).find("int") != std::string::npos, "the redeclared int boundary holds after the call when the same unit redeclares: " + run_with(true, std::string(defs) + " probe(4, 2);", true, true));
			check(run_with(true, std::string(defs) + " probe(4, 2);", true, false).rfind("2.5", 0) == 0, "another unit's redeclaration shadows and the loop keeps its own cell: " + run_with(true, std::string(defs) + " probe(4, 2);", true, false));
		});
		test("call_arguments_and_the_callee_keep_their_source_order_across_a_getter", [this]() {
			// Astra's third finding and its callee-getter follow-up: `take(V, G + 1)` where V is a global reference to a native
			// property whose getter steps G, the same through a local reference slot, and `F(G + 1)` where F is a reference to a
			// property whose getter returns the callee and steps G. Argument order is the source order on both backends: a computed
			// argument after a verbatim load keeps the loop general (an effect refusal); the callee's resolution runs first, at its
			// own place, and a getter observed there re-lands the block, so the loop with the getter-backed callee still closes.
			// The fixture arms the getter after the references are bound, so no read during binding moves G; the getter sets G = 20 (idempotent, so how often it runs does not enter: a
			// property argument converted by value can run its getter more than once on a first call, both backends the same, an
			// open finding) and each iteration resets G = 10, so the order shows: 21 per call when the getter precedes G + 1, 11
			// otherwise; before(3) computes a = 11 ahead of the getter (take_ab: 1100 per call, 2100 if the getter came first)
			struct stepper { bool armed = false; };
			const char* defs = R"JAI(
var G = 10;
var box = Stepper();
var& V = box.number;
var& F = box.callable;
function target(int a) -> int { return a; }
function take_b(int a, int b) -> int { return b; }
function take_ab(int a, int b) -> int { return a * 100 + b; }
function after(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_b(V, G + 1); acc = acc + z; } return to_string(acc); }
function before(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_ab(G + 1, V); acc = acc + z; } return to_string(acc); }
function via_local(int n) { var& p = box.number; box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_b(p, G + 1); acc = acc + z; } return to_string(acc); }
function via_callee(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = F(G + 1); acc = acc + z; } return to_string(acc); }
)JAI";
			// the listing compiles the same four loops as lambdas held in vars: a named function body compiles on its first call
			const char* listing_defs = R"JAI(
var G = 10;
var box = Stepper();
var& V = box.number;
var& F = box.callable;
function target(int a) -> int { return a; }
function take_b(int a, int b) -> int { return b; }
function take_ab(int a, int b) -> int { return a * 100 + b; }
var after = function(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_b(V, G + 1); acc = acc + z; } return to_string(acc); };
var before = function(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_ab(G + 1, V); acc = acc + z; } return to_string(acc); };
var via_local = function(int n) { var& p = box.number; box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = take_b(p, G + 1); acc = acc + z; } return to_string(acc); };
var via_callee = function(int n) { box.arm(); int acc = 0; for (int i = 0; i < n; ++i) { G = 10; int z = F(G + 1); acc = acc + z; } return to_string(acc); };
)JAI";
			auto run_with = [&](bool vm, const std::string& script, bool proofs) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				dynamic_binder<stepper>(*e, "Stepper").constructor<>()
					.method("arm", [](stepper& s) { s.armed = true; })
					.property("number", [raw](stepper& s) -> int64_t { if (s.armed) { raw->execute("G = 20;"); } return 0; }, nullptr)
					.property("callable", [raw](stepper& s) -> script_value { if (s.armed) { raw->execute("G = 20;"); } return raw->get_variable("target"); }, nullptr)
					.build();
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; const char* expected; };
			const probe probes[] = { { "after(3);", "63" }, { "before(3);", "3300" }, { "via_local(3);", "63" }, { "via_callee(3);", "63" } };
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string script = std::string(defs) + " " + p.call;
					const std::string interp = run_with(false, script, proofs), vm = run_with(true, script, proofs);
					check_eq(interp, vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
			const std::string listing = listing_of(listing_defs);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			size_t open = 0; for (size_t at = listing.find("-- open loop "); at != std::string::npos; at = listing.find("-- open loop ", at + 1)) { ++open; }
			check_eq(size_t(2), open, "the computed argument after a verbatim load (an environment name, a reference slot) keeps its loop general: " + listing);
			check_eq(size_t(2), closed, "the computed argument before the load, and the getter-backed callee, close: " + listing);
			check(listing.find("effect at") != std::string::npos, "the refusal is an effect refusal: " + listing);
			check(listing.find(":probe") != std::string::npos, "the probe is an operation of the region: " + listing);
		});
		test("a_read_that_may_run_user_code_never_runs_ahead_of_an_earlier_operand", [this]() {
			// Astra's second finding and its follow-ups: the outer container of a nested read, or the left operand of a fused
			// binary, is read before the inner read in the language's order; when that read may run user code (an operator[] on a
			// var container, a getter-backed reference as the index or as the receiver, a class operator or a custom numeric
			// operator inside the index) and the operand is a binding user code could rebind (a global, a local captured by
			// reference, a reference parameter to such a binding), the operand's value is taken ahead of the read on both
			// backends; a private local keeps the fused shape without a snapshot. The fixture is a C++ box bound as a global by reference
			// with a free arm() (a method call on the box would touch its properties), armed after the references are bound
			struct stepper { bool armed = false; };
			const char* defs = R"JAI(
var P = [10];
var X = 5;
var R = [10, 20];
var D = [1, 2, 3];
array<int> grid_source = [5, 6, 7];
var& PR = box.grid;
array<int> P2 = [10, 20];
array<int> Other = [30, 40, 50, 60];
array<int> Q2 = [0, 1, 2];
class Index { var target = null; Index(var f) { target = f; } int operator[](int i) { target(); return 0; } }
class Vec { int v = 0; Vec(int a) { v = a; } function operator+(int o) -> int { X = 100; return v + o; } }
var global_rebind = function() { var Q = Index([]() { P = [90]; }); return to_string(P[Q[0]]) + "|" + to_string(P[0]); };
var local_rebind = function() { var L = [10]; var Q = Index([&L]() { L = [90]; }); return to_string(L[Q[0]]) + "|" + to_string(L[0]); };
var lift_rebind = function() { var Q = Index([]() { X = 100; }); int r = X + Q[0]; return to_string(r) + "|" + to_string(X); };
var typed_getter = function() { array<int> Q = [0]; var& idx = box.number; arm(); return to_string(P[Q[idx]]) + "|" + to_string(P[0]); };
var private_operator = function() { Vec q = Vec(1); int r = X + R[q + 0]; return to_string(r) + "|" + to_string(X); };
var by_ref = function(var& p) { var& s = box.source; arm(); return to_string(p[s[0]]) + "|" + to_string(D[0]); };
var ref_param_rebind = function() { return by_ref(D); };
var typed_receiver = function() { array<int>& t = box.grid; arm(); int r = X + t[0]; return to_string(r) + "|" + to_string(X); };
var numeric_operator = function() { array<int> Q = [0, 0]; int i = 0; install_plus(); int r = P[Q[i + 0]]; int p0 = P[0]; return to_string(r * 1000 + p0); };
var outer_ref = function() { array<int> Q = [0]; arm(); string m = ""; try { int r = PR[Q[2]]; m = to_string(r); } catch (e) { m = "caught"; } return m + "|" + to_string(X); };
var guard_reuse = function(int n) { int s = 0; int i = 0; string m = ""; try { while (i < n) { s = s + P2[Q2[i + 0]]; s = s + Other[Q2[i + 0]]; i = i + 1; } } catch (e) { m = "caught"; } return m + "|" + to_string(s); };
var private_stays = function(int n) { array<int> A = [1, 2, 3, 4]; int p = 100; int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + (p + A[i]); } return acc; };
)JAI";
			auto run_with = [&](bool vm, const std::string& script, bool proofs) -> std::string {
				stepper source;   // outlives the engine below
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				dynamic_binder<stepper>(*e, "Stepper")
					.property("number", [raw](stepper& s) -> int64_t { if (s.armed) { raw->execute("P = [90];"); } return 0; }, nullptr)
					.property("source", [raw](stepper& s) -> script_value { if (s.armed) { raw->execute("D = [7, 8, 9];"); } return raw->execute("[0]"); }, nullptr)
					.property("grid", [raw](stepper& s) -> script_value { if (s.armed) { raw->execute("X = 100;"); } return raw->get_variable("grid_source"); }, nullptr)
					.build();
				e->add_global_ref("box", source);
				e->add_function("arm", [&source]() { source.armed = true; });
				e->add_function("install_plus", [raw]() {
					raw->add_function("+", [raw](script_int a, script_int b) { raw->execute("P = [90];"); return a + b; });
					raw->set_has_custom_numeric_operators(true);
				});
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "global_rebind();", "10|90" }, { "local_rebind();", "10|90" }, { "lift_rebind();", "5|100" }, { "typed_getter();", "10|90" },
				{ "private_operator();", "25|100" }, { "ref_param_rebind();", "1|7" }, { "typed_receiver();", "10|100" }, { "numeric_operator();", "10090" }, { "outer_ref();", "caught|100" }, { "guard_reuse(3);", "caught|100" },
				{ "to_string(private_stays(4));", "410" },
			};
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string script = std::string(defs) + " " + p.call;
					const std::string interp = run_with(false, script, proofs), vm = run_with(true, script, proofs);
					check_eq(interp, vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
			const std::string priv = listing_of("var f = function(int n) { array<int> A = [1, 2, 3, 4]; int p = 100; int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + (p + A[i]); } return acc; };");
			check(priv.find("OPERAND_TEMP") == std::string::npos && priv.find("-- cell loop") != std::string::npos, "a private local ahead of a proven read keeps the fused shape and the loop closes: " + priv);
			const std::string reuse = listing_of("array<int> P2 = [10, 20]; array<int> Other = [30, 40, 50, 60]; array<int> Q2 = [0, 1, 2]; var f = function(int n) { int s = 0; int i = 0; while (i < n) { s = s + P2[Q2[i + 0]]; s = s + Other[Q2[i + 0]]; i = i + 1; } return s; };");
			check(reuse.find("-- cell loop") != std::string::npos, "two guarded global containers in one body still close as a block (the miss path restores each snapshot): " + reuse);
			const std::string snap = listing_of("var f = function(var& t, var& pal, int n) { int acc = 0; for (int i = 0; i < n; ++i) { acc = acc + pal[t[i + 1] + 1]; } return acc; };");
			check(snap.find("-- cell loop") != std::string::npos, "a guarded container ahead of an index that may run user code still closes as a block: " + snap);
		});
		test("a_global_redeclared_as_a_property_reference_after_compilation_reads_first", [this]() {
			// Astra's reload case: a function compiled while P and X2 were values of this unit; the host then redeclares them as
			// references to properties (var& P = box.grid; var& X2 = box.number); the compiled reads must still read P (its getter,
			// or its error) before the inner read that raises, and a getter-backed binding inside a loop runs its getter on every
			// read (the block admits neither the site nor the cell). The fixture is a C++ box bound as a global by reference with
			// a free arm(); the getters count in H
			struct stepper { bool armed = false; };
			const char* defs = R"JAI(
var X = 5;
var H = 0;
var P = [10];
var X2 = 7;
array<int> grid_source = [5, 6, 7];
function probe() { array<int> Q = [0]; string m = ""; try { int r = P[Q[2]]; m = to_string(r); } catch (err) { m = "caught"; } return m + "|" + to_string(X); }
function probe2() { array<int> Q = [0]; string m = ""; try { int r = X2 + Q[2]; m = to_string(r); } catch (err) { m = "caught"; } return m + "|" + to_string(X); }
function loop_container(int n) { array<int> Q = [0, 0, 0, 0]; int s = 0; for (int i = 0; i < n; ++i) { s = s + P[Q[i]]; } return to_string(s) + "|" + to_string(H); }
function loop_scalar(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + X2; } return to_string(s) + "|" + to_string(H); }
)JAI";
			auto run_with = [&](bool vm, const char* first, const char* second, bool proofs) -> std::string {
				stepper source;   // outlives the engine below
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				dynamic_binder<stepper>(*e, "Stepper")
					.property("number", [raw](stepper& s) -> int64_t { if (s.armed) { raw->execute("X = 100; H = H + 1;"); } return 0; }, nullptr)
					.property("grid", [raw](stepper& s) -> script_value { if (s.armed) { raw->execute("X = 100; H = H + 1;"); } return raw->get_variable("grid_source"); }, nullptr)
					.build();
				e->add_global_ref("box", source);
				e->add_function("arm", [&source]() { source.armed = true; });
				try {
					e->disable(jai::filesystem);
					const std::string path = "D:/virtual/pilot/reload_unit.jai";
					const std::string before = e->execute_file_source(path, std::string(defs) + " " + first).as_string();   // compiled and run while the names are values
					e->execute_file_source(path, "var& P = box.grid; var& X2 = box.number;");   // the same unit (a reload of the file) redeclares them in place as references to properties
					return before + " / " + e->execute(std::string("arm(); ") + second).as_string();
				}
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* first; const char* second; const char* expected; };
			const probe probes[] = {
				{ "probe();", "probe();", "caught|5 / caught|100" },
				{ "probe2();", "probe2();", "caught|5 / caught|100" },
				{ "loop_container(4);", "loop_container(4);", "40|0 / 20|4" },
				{ "loop_scalar(4);", "loop_scalar(4);", "28|0 / 0|4" },
			};
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string interp = run_with(false, p.first, p.second, proofs), vm = run_with(true, p.first, p.second, proofs);
					check_eq(interp, vm, std::string(p.second) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.second) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
			const std::string listing = listing_of("var P = [10]; var f = function(int n) { array<int> Q = [0, 0, 0, 0]; int s = 0; for (int i = 0; i < n; ++i) { s = s + P[Q[i]]; } return s; };");
			size_t guards = 0; for (size_t at = listing.find("C_GUARD"); at != std::string::npos; at = listing.find("C_GUARD", at + 1)) { ++guards; }
			check(listing.find("-- cell loop") != std::string::npos && listing.find("OPERAND_TEMP") != std::string::npos && guards == 2, "an environment container ahead of a quiet inner read keeps its value snapshot at its own point in the block too (its guard and the counted loop's placeholder): " + listing);
		});
		test("a_guard_temp_is_a_value_at_its_own_point", [this]() {
			// Astra's two reproductions against 118d.2: a getter's captured null is a value, never a sign that no snapshot was taken (the
			// getter runs once whether it returns null or a table), and a guard temp's slot belongs to later definitions once the guard's
			// read is past (a live multiply operand of the next statement is never erased: the overflow text and the checked float-to-int
			// conversion both need it on the general form after a miss)
			struct nulls { bool armed = false; bool flips = false; int calls = 0; };
			auto run_null = [&](bool vm, bool flips, bool proofs) -> std::string {
				nulls source; source.flips = flips;
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				dynamic_binder<nulls>(*e, "Nulls")
					.property("values", [raw](nulls& s) -> script_value { if (s.armed) { ++s.calls; if (!s.flips || s.calls == 1) { return script_value(std::monostate{}, raw); } } return raw->get_variable("VALUES"); }, nullptr)
					.build();
				e->add_global_ref("box", source);
				try {
					e->disable(jai::filesystem);
					const std::string path = "D:/virtual/pilot/null_unit.jai";
					e->execute_file_source(path, "var VALUES = [10]; var P = [10]; function probe() { array<int> Q = [0]; return P[Q[0]]; } probe();");
					e->execute_file_source(path, "var& P = box.values;");   // the same unit (a reload) redeclares P in place
					source.armed = true; source.calls = 0;
					const std::string r = e->execute("var result = \"\"; try { probe(); result = \"value\"; } catch (err) { result = \"caught\"; } result;").as_string();
					return r + "|" + std::to_string(source.calls);
				}
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (bool flips : { false, true }) {
				for (bool proofs : { true, false }) {
					const std::string interp = run_null(false, flips, proofs), vm = run_null(true, flips, proofs);
					const std::string label = std::string(flips ? "null once then a table" : "null") + (proofs ? "" : " proofs off");
					check_eq(interp, vm, label + ": both backends");
					check_eq(std::string("caught|1"), vm, label + ": the getter runs once and its null is the container");
				}
			}
			const char* liveness = R"JAI(
array<int> P = [1, 2, 3, 4];
var overflow = function(int n, int limit) { array<int> Q = [0, 1, 2, 3]; int total = 0; int value = 0; int i = 0; string m = ""; try { while (i < n) { total = total + P[Q[i]]; value = (i + 1) * (limit - i); i = i + 1; } } catch (err) { m = err; } return m + "|" + to_string(total) + "|" + to_string(value); };
var conversion = function(int n) { array<int> Q = [0, 1, 2, 3]; int total = 0; int value = 0; int i = 0; while (i < n) { total = total + P[Q[i]]; value = (i + 0.5) * 2.0; i = i + 1; } return to_string(total) + "|" + to_string(value); };
)JAI";
			auto run_with = [&](bool vm, const std::string& script, bool proofs) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; const char* expected; };
			const probe probes[] = { { "overflow(3, 4611686018427387903);", "Integer overflow in '*'|6|9223372036854775804" }, { "conversion(3);", "6|5" } };
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string script = std::string(liveness) + " " + p.call;
					const std::string interp = run_with(false, script, proofs), vm = run_with(true, script, proofs);
					check_eq(interp, vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
		});
		test("strings_append_in_a_block", [this]() {
			// vid_row_half's shape (a var string built from string elements of var arrays and an environment string), an alias taken
			// before the loop (the first append misses and the general form unshares), the interned literal, a char appended as text,
			// an int appended (the mismatch text), a callee keeping a copy (the append after it misses), a typed array<string>, a
			// declared string local, a var that stops being a string mid-loop; all the same on both backends, proofs on and off
			const char* defs = R"JAI(
var PAL_FG = ["a", "b"]; var PAL_BG = ["c", "d"]; var HALFG = "h"; array<int> PIX = [0, 1, 1, 0, 1, 0, 0, 1]; array<string> WORDS = ["x", "y", "z"]; var KEEP = "";
var row_half = function(int cy, int vw) { var row = ""; int fg = -1; int bg = -1; int ti = cy * 2 * vw; int bi = ti + vw; for (int x = 0; x < vw; ++x) { int top = PIX[ti + x]; int bot = PIX[bi + x]; if (top != fg) { row += PAL_FG[top]; fg = top; } if (bot != bg) { row += PAL_BG[bot]; bg = bot; } row += HALFG; } return row; };
var aliased = function(int n) { var s = ""; var s2 = s; for (int i = 0; i < n; ++i) { s += HALFG; } return s + "|" + s2; };
var chars = function(int n) { var s = ""; for (int i = 0; i < n; ++i) { s += 'x'; s += HALFG; } return s; };
var ints = function(int n) { var s = ""; try { for (int i = 0; i < n; ++i) { s += HALFG; s += i; } } catch (e) { return s + "|" + e; } return s; };
var keep = function(string v) -> void { KEEP = v; };
var kept = function(int n) { var s = ""; for (int i = 0; i < n; ++i) { s += HALFG; keep(s); } return s + "|" + KEEP; };
var typed = function(int n) { var s = ""; for (int i = 0; i < n; ++i) { s += WORDS[i % 3]; } return s; };
var declared = function(int n) { string s = "s:"; for (int i = 0; i < n; ++i) { s += WORDS[i % 3]; s += "-"; } return s; };
var flips = function(int n) { var s = ""; try { for (int i = 0; i < n; ++i) { s += HALFG; if (i == 1) { s = 5; } } } catch (e) { return "caught " + e; } return to_string(s); };
var selfs = function(int n) { var s = "ab"; for (int i = 0; i < n; ++i) { s += s; } return s; };
var aliased_src = function(int n) { var s = "a"; var t = s; for (int i = 0; i < n; ++i) { s += t; } return s + "|" + t; };
var summed = function(var& data, int n) { int acc = 0; try { for (int i = 0; i < n; ++i) { acc = acc + data[i % 8]; } } catch (e) { return "throw|" + to_string(acc); } return to_string(acc); };
var MIXED = [3, 1, 4, 1, 5, "s", 2, 6];
)JAI";
			struct probe { const char* call; const char* expected; };
			const probe probes[] = {
				{ "row_half(0, 4);", "adhbchhadh" }, { "aliased(3);", "hhh|" }, { "chars(2);", "xhxh" }, { "ints(2);", "" }, { "kept(3);", "hhh|hhh" },
				{ "typed(5);", "xyzxy" }, { "declared(3);", "s:x-y-z-" }, { "flips(3);", "" }, { "selfs(3);", "abababababababab" }, { "aliased_src(3);", "aaaa|a" }, { "summed(MIXED, 8);", "throw|14" },
			};
			for (const probe& p : probes) {
				both(std::string(defs) + " " + p.call, p.expected, p.call);
				both(std::string(defs) + " " + p.call, "", (std::string(p.call) + " proofs off").c_str(), false);
			}
			// the memory cap reached inside the loop: the general form's text and post-state on both backends
			auto capped = [&](bool vm) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				e->memory_cap(65536);
				try { return e->execute(std::string(defs) + " var big = function(int n) { var s = \"\"; try { for (int i = 0; i < n; ++i) { s += WORDS[i % 3]; s += HALFG; } } catch (e) { return \"caught|\" + e; } return \"no cap\"; }; big(100000);").as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string ci = capped(false), cv = capped(true);
			check_eq(ci, cv, "the memory cap inside the loop: both backends");
			check(ci.find("|") != std::string::npos, "the cap raised inside the loop and was caught: " + ci);
			const std::string listing = listing_of(defs);
			size_t appends = 0; for (size_t at = listing.find("\tC_APPEND "); at != std::string::npos; at = listing.find("\tC_APPEND ", at + 1)) { ++appends; }
			check(appends >= 10, "the appends are cell operations (" + std::to_string(appends) + "): " + listing);
			size_t closed = 0; for (size_t at = listing.find("-- cell loop "); at != std::string::npos; at = listing.find("-- cell loop ", at + 1)) { ++closed; }
			check_eq(size_t(10), closed, "ten loops close, the char append's stays general (open loops: " + listing.substr(listing.find("-- open loop ") == std::string::npos ? listing.size() : listing.find("-- open loop "), 800) + ")");
			check(listing.find(":append") != std::string::npos, "the IR names the append: " + listing);
		});
		test("a_string_environment_cell_is_re_borrowed_after_every_effect_point", [this]() {
			// Astra's rebase requirement for row 122 on the stable chain: the string cells' nodes are re-read in cells_reland, so a callee
			// that rebinds the environment string (flip(i): TEXT = "b" at i == 2, a call taking only an int, so the loop stays admitted)
			// and a getter observed by the callee probe that rebinds it (var& F = box.callable, armed from i == 2) both show in the
			// next append; the fixture is a C++ box bound as a global by reference with a free arm()
			struct stepper { bool armed = false; };
			const char* defs = R"JAI(
var TEXT = "a";
var& F = box.callable;
function target(int a) -> int { return a; }
function flip(int i) -> void { if (i == 2) { TEXT = "b"; } }
function by_call() { var s = ""; int i = 0; while (i < 5) { flip(i); s += TEXT; i = i + 1; } return s + "|" + TEXT; }
function by_getter() { var s = ""; int i = 0; while (i < 5) { if (i == 2) { arm(); } int z = F(i); s += TEXT; i = i + 1; } return s + "|" + TEXT; }
)JAI";
			auto run_with = [&](bool vm, const std::string& script, bool proofs) -> std::string {
				stepper source;   // outlives the engine below
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				engine* raw = e.get();
				dynamic_binder<stepper>(*e, "Stepper")
					.property("callable", [raw](stepper& s) -> script_value { if (s.armed) { raw->execute("TEXT = \"b\";"); } return raw->get_variable("target"); }, nullptr)
					.build();
				e->add_global_ref("box", source);
				e->add_function("arm", [&source]() { source.armed = true; });
				try { return e->execute(script).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			struct probe { const char* call; const char* expected; };
			const probe probes[] = { { "by_call();", "aabbb|b" }, { "by_getter();", "aabbb|b" } };
			for (const probe& p : probes) {
				for (bool proofs : { true, false }) {
					const std::string script = std::string(defs) + " " + p.call;
					const std::string interp = run_with(false, script, proofs), vm = run_with(true, script, proofs);
					check_eq(interp, vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": both backends");
					check_eq(std::string(p.expected), vm, std::string(p.call) + (proofs ? "" : " proofs off") + ": expected");
				}
			}
			const std::string listing = listing_of("var TEXT = \"a\"; function flip(int i) -> void { if (i == 2) { TEXT = \"b\"; } } var f = function() { var s = \"\"; int i = 0; while (i < 5) { flip(i); s += TEXT; i = i + 1; } return s; };");
			check(listing.find("-- cell loop") != std::string::npos && listing.find("C_APPEND") != std::string::npos, "the loop with the int-only call closes and appends in place: " + listing);
		});
		test("a_nested_or_chain_and_a_ternary_declaration_close_inside_a_block", [this]() {
			// trace_block_dz's shape (JaiDOOM things.jai 324): `t < 0.0 || t > 1.0 || t >= best` is jumps with no bool materialized,
			// `float ob = f0 > f1 ? f0 : f1` lands each arm in the declaration (the block widens the int arm), and the loop closes
			const char* fn = "var f = function(var& VX, var& L1, var& L2, int n, float rdx, float rdy, float x1) -> float {"
			                 " float best = 1000.0; int hits = 0;"
			                 " for (int i = 0; i < n; ++i) {"
			                 "  float ax = VX[L1[i]]; float bx = VX[L2[i]];"
			                 "  float d = (bx - ax) * rdy - (bx + ax) * rdx;"
			                 "  if (d > -0.0001 && d < 0.0001) { continue; }"
			                 "  float t = (x1 - ax) / d;"
			                 "  if (t < 0.0 || t > 1.0 || t >= best) { continue; }"
			                 "  int f0 = L1[i]; int f1 = L2[i];"
			                 "  float ob = f0 > f1 ? f0 : f1;"
			                 "  float ot = f0 < f1 ? f0 : f1;"
			                 "  if (t * 10.0 <= ob || ot < 0.5) { best = t; hits = hits + 1; }"
			                 " }"
			                 " return best + hits; };";
			const std::string listing = listing_of(fn);
			check_true(listing.find("CELLS_") != std::string::npos, "the loop closes: " + listing);
			check_true(listing.find("TO_BOOL") == std::string::npos, "no bool is materialized for the condition chain: " + listing);
			const std::string call = std::string(fn) + " array<float> VX = [0.0, 1.0, 2.5, 4.0, 8.0, 0.5]; array<int> L1 = [0, 1, 2, 3, 4, 5, 1]; array<int> L2 = [1, 2, 3, 4, 5, 0, 1];"
			                         " to_string(f(VX, L1, L2, 7, 0.25, 0.75, 3.0));";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
		});
		test("a_member_of_a_frame_slot_instance_is_a_block_container", [this]() {
			// render_view_impl's shape (GLOOM render.jai 336): a chunk record's columns are read and stored through members of the
			// reference parameter; the entry resolves each field once (the member IC's plain-field probe), a user accessor on the
			// field keeps its site on the general form, and a callee that rebinds the field is seen at the landing
			const char* fn = "class Chunk { array<float> odist = []; array<int> okind = []; array<float> out = []; }"
			                 " var f = function(var& ch, int cols, float vh) -> float {"
			                 " float acc = 0.0;"
			                 " for (int ci = 0; ci < cols; ++ci) {"
			                 "  float dist = ch.odist[ci]; int kind = ch.okind[ci];"
			                 "  float lineh = vh / dist; if (kind == 2) { lineh = lineh * 0.5; }"
			                 "  ch.out[ci] = lineh; acc = acc + lineh;"
			                 " }"
			                 " return acc; };";
			const std::string listing = listing_of(fn);
			check_true(listing.find("CELLS_") != std::string::npos && listing.find("C_LOAD") != std::string::npos && listing.find("C_INDEX_STORE") != std::string::npos, "the member containers close the loop: " + listing);
			const std::string call = std::string(fn) + " var c = Chunk(); c.odist = [2.0, 4.0, 8.0, 0.5]; c.okind = [1, 2, 1, 2]; c.out = [0.0, 0.0, 0.0, 0.0];"
			                         " float r = f(c, 4, 16.0); to_string(r) + \" \" + to_string(c.out[1]) + \" \" + to_string(c.out[3]);";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
			const std::string acc = "class Chunk { array<float> odist = []; int reads = 0; function _get_odist() { reads = reads + 1; return odist; } }"
			                        " var g = function(var& ch, int cols) -> float { float acc = 0.0; for (int ci = 0; ci < cols; ++ci) { acc = acc + ch.odist[ci]; } return acc; };"
			                        " var c = Chunk(); c.odist = [1.0, 2.0, 3.0]; float r = g(c, 3); to_string(r) + \" \" + to_string(c.reads);";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, acc, proofs), run_on(true, acc, proofs), std::string("an accessor-backed member agrees (the general form runs the getter per read)") + (proofs ? "" : " (proofs off)"));
			}
			const std::string rebind = "class Chunk { array<float> odist = []; }"
			                           " function swap(var& ch) { ch.odist = [10.0, 20.0, 30.0, 40.0]; }"
			                           " var h = function(var& ch, int cols) -> float { float acc = 0.0; for (int ci = 0; ci < cols; ++ci) { acc = acc + ch.odist[ci]; if (ci == 1) { swap(ch); } } return acc; };"
			                           " var c = Chunk(); c.odist = [1.0, 2.0, 3.0, 4.0]; to_string(h(c, 4));";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, rebind, proofs), run_on(true, rebind, proofs), std::string("a field rebound by a callee agrees (the landing resolves it again)") + (proofs ? "" : " (proofs off)"));
			}
		});
		test("a_float_compound_store_takes_the_float_row", [this]() {
			// `x += y` on floats took the int row (a fallback at every execution; inside a loop a representation refusal): the
			// class the checker stamps picks the float row, the loop closes, and the mixed and unknown classes keep the general
			// compound with its conversions
			const char* fn = "var f = function(int n, float d, float e) -> float {"
			                 " float x = 1.5; float y = 100.0; float z = 1.0; float w = 81.0; int i = 0;"
			                 " while (i < n) { x += d; y -= e; z *= 2.0; w /= 3.0; i += 1; }"
			                 " return x + y + z + w + i; };";
			const std::string listing = listing_of(fn);
			check_true(listing.find("T_ADD_FF") != std::string::npos && listing.find("T_SUB_FF") != std::string::npos && listing.find("T_MUL_FF") != std::string::npos && listing.find("T_DIV_FF") != std::string::npos,
			           "the float compounds take the float rows: " + listing);
			check_true(listing.find("CELLS_") != std::string::npos, "the loop closes: " + listing);
			const std::string call = std::string(fn) + " to_string(f(4, 0.25, 2.5)) + \" \" + to_string(f(0, 0.25, 2.5));";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
			const std::string mixed = "var g = function(int n) -> string { float x = 1.5; var v = 2; int i = 3; for (int k = 0; k < n; ++k) { x += 1; v += 2.5; i += 2; x *= 2.0; x /= 4.0; }"
			                          " return to_string(x) + \" \" + to_string(v) + \" \" + to_string(i); }; g(3);";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, mixed, proofs), run_on(true, mixed, proofs), std::string("mixed and unknown classes agree") + (proofs ? "" : " (proofs off)"));
			}
		});
		test("a_computed_index_on_a_member_container_fuses_and_the_dda_loop_closes", [this]() {
			// gloom_ray_chunk_step's DDA while (GLOOM pure.jai 46): `t.pack[cell / 15]` reads a member container at a computed
			// index; fused, the loop closes over the member container (row 129) with its float compounds (row 130)
			const char* fn = "class Ws { int mw = 8; array<int> pack = []; }"
			                 " var f = function(var& t, float posx, float posy, float rdx, float rdy) -> float {"
			                 " int mw = t.mw; int mx = posx; int my = posy;"
			                 " float adx = rdx < 0.0 ? -rdx : rdx; float ady = rdy < 0.0 ? -rdy : rdy;"
			                 " float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx; float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;"
			                 " int stepx = rdx < 0.0 ? -1 : 1; int stepy = rdy < 0.0 ? -1 : 1;"
			                 " float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;"
			                 " float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;"
			                 " int side = 0; int kind = 1; int guard = 0; bool hit = false;"
			                 " while (!hit && guard < 128) {"
			                 "  if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; } else { sdy += ddy; my += stepy; side = 1; }"
			                 "  int cell = my * mw + mx; int word = t.pack[cell / 15]; int c = (word >> ((cell % 15) * 4)) & 15;"
			                 "  if (c != 0) { hit = true; kind = c; }"
			                 "  guard = guard + 1;"
			                 " }"
			                 " return (side == 0 ? sdx - ddx : sdy - ddy) + kind * 1000.0 + guard; };";
			const std::string listing = listing_of(fn);
			check_true(listing.find("INDEX_FUSED_DECL") != std::string::npos, "the member container read is one fused instruction: " + listing);
			check_true(listing.find("CELLS_") != std::string::npos, "the DDA loop closes: " + listing);
			const std::string call = std::string(fn) + " var w = Ws(); array<int> pack = []; int word = 0; int fill = 0;"
			                         " for (int cell = 0; cell < 64; ++cell) { int cx = cell % 8; int cy = cell / 8; int k = (cx == 0 || cy == 0 || cx == 7 || cy == 7) ? 1 : 0;"
			                         "  word = word | (k << ((cell % 15) * 4)); fill = fill + 1; if (fill == 15) { pack.push(word); word = 0; fill = 0; } }"
			                         " if (fill > 0) { pack.push(word); } w.pack = pack;"
			                         " to_string(f(w, 3.5, 3.5, 1.0, 0.66)) + \" \" + to_string(f(w, 3.5, 3.5, -0.7, -0.3)) + \" \" + to_string(f(w, 1.2, 6.1, 0.1, 1.0));";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
			// the member read raises like the unfused shape: an index past the end and a missing member, the same text on both backends
			const std::string oob = "class Ws { array<int> pack = [1, 2, 3]; } var r = function(var& t, int i) -> int { return t.pack[i * 2]; };"
			                        " var w = Ws(); string out = \"\"; try { out = to_string(r(w, 1)); } catch (e) { out = \"caught \" + e; } try { out = out + \"|\" + to_string(r(w, 5)); } catch (e) { out = out + \"|caught \" + e; } out;";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, oob, proofs), run_on(true, oob, proofs), std::string("the fused member read raises the general text") + (proofs ? "" : " (proofs off)"));
			}
		});
		test("an_element_reference_is_a_member_containers_base", [this]() {
			// GLOOM's ray step takes its record as `var& t` bound to an element of `array<WallChunk>` (`for (auto& t : WALL_WS)`), a
			// reference in the container+index mode: the member resolution dereferences it and pins the table; a body that stores into
			// that table keeps the loop on the general form; a var array of records and a typed array behave alike on both backends
			const char* src = "class Chunk { array<float> odist = [1.0, 2.0, 4.0, 8.0]; array<float> out = [0.0, 0.0, 0.0, 0.0]; float k = 1.0; }"
			                  " function f(var& ch, int cols, float vh) -> float { float acc = 0.0; for (int ci = 0; ci < cols; ++ci) { float d = ch.odist[ci]; float v = vh / d * ch.k; ch.out[ci] = v; acc = acc + v; } return acc; }"
			                  " var loose = [Chunk(), Chunk(), Chunk()]; array<Chunk> typed = [Chunk(), Chunk()]; loose[1].k = 2.0; typed[1].k = 3.0;"
			                  " float total = 0.0; for (auto& ch : loose) { total = total + f(ch, 4, 16.0); } for (auto& ch : typed) { total = total + f(ch, 4, 16.0); }"
			                  " function g(var& ch, var& table, int cols) -> float { float acc = 0.0; for (int ci = 0; ci < cols; ++ci) { acc = acc + ch.odist[ci]; if (ci == 1) { table[0] = Chunk(); } } return acc; }"
			                  " var& first = loose[0]; float swapped = g(first, loose, 4);"
			                  " to_string(total) + \" \" + to_string(loose[2].out[3]) + \" \" + to_string(typed[1].out[0]) + \" \" + to_string(swapped) + \" \" + to_string(loose[0].k);";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, src, proofs), in = run_on(false, src, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the program completes: " + vm);
			}
		});
		test("pushes_onto_a_frame_slot_array_run_inside_the_block", [this]() {
			// gloom_row_quad's shape (GLOOM render.jai 526): string elements of environment tables and string constants pushed onto a
			// local var array inside the cells loop; the loop closes and the joined row agrees with the interpreter; a typed
			// array<string> receiver, an int buffer fed a float (the general text), the memory cap and an index past the end agree too
			const char* src = "array<int> PIX = [3, 3, 3, 3, 5, 3, 5, 3, 1, 2, 3, 4, 7, 7, 7, 7]; array<int> PAL_LUM = [0, 40, 80, 120, 160, 200, 240, 255];"
			                  " array<string> PAL_FG = [\"f0\", \"f1\", \"f2\", \"f3\", \"f4\", \"f5\", \"f6\", \"f7\"]; array<string> PAL_BG = [\"b0\", \"b1\", \"b2\", \"b3\", \"b4\", \"b5\", \"b6\", \"b7\"];"
			                  " array<string> QUADG = [\"q0\", \"q1\", \"q2\", \"q3\", \"q4\", \"q5\", \"q6\", \"q7\", \"q8\", \"q9\", \"qa\", \"qb\", \"qc\", \"qd\", \"qe\", \"qf\"];"
			                  " var row = function(int vw) -> string { var parts = []; int cells = vw / 2; int pfg = -1; int pbg = -1; int itop = 0; int ibot = vw;"
			                  "  for (int cx = 0; cx < cells; ++cx) { int xx = cx * 2; int p0 = PIX[itop + xx]; int p1 = PIX[itop + xx + 1]; int p2 = PIX[ibot + xx]; int p3 = PIX[ibot + xx + 1];"
			                  "   if (p0 == p1 && p0 == p2 && p0 == p3) { if (p0 != pbg) { parts.push(PAL_BG[p0]); pbg = p0; } parts.push(\" \"); continue; }"
			                  "   if (p0 == p2 && p1 == p3) { if (p0 != pfg) { parts.push(PAL_FG[p0]); pfg = p0; } if (p1 != pbg) { parts.push(PAL_BG[p1]); pbg = p1; } parts.push(QUADG[5]); continue; }"
			                  "   int l0 = PAL_LUM[p0]; int l1 = PAL_LUM[p1]; int l2 = PAL_LUM[p2]; int l3 = PAL_LUM[p3]; int lmin = l0; int lmax = l0; int fgp = p0; int bgp = p0;"
			                  "   if (l1 < lmin) { lmin = l1; bgp = p1; } if (l1 > lmax) { lmax = l1; fgp = p1; } if (l2 < lmin) { lmin = l2; bgp = p2; } if (l2 > lmax) { lmax = l2; fgp = p2; }"
			                  "   int thr = (lmin + lmax + 1) / 2; int bits = 0; if (l0 >= thr) { bits = 1; } if (l1 >= thr) { bits += 2; } if (l2 >= thr) { bits += 4; } if (l3 >= thr) { bits += 8; }"
			                  "   if (fgp != pfg) { parts.push(PAL_FG[fgp]); pfg = fgp; } if (bits != 15 && bgp != pbg) { parts.push(PAL_BG[bgp]); pbg = bgp; } parts.push(QUADG[bits]); }"
			                  "  return parts.join(\",\"); };";
			const std::string listing = listing_of(src);
			check_true(listing.find("C_PUSH") != std::string::npos && listing.find("CELLS_") != std::string::npos, "the pushes run inside the block: " + listing);
			const std::string call = std::string(src) + " row(8) + \"|\" + row(8);";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("the joined rows agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos && vm.find("q") != std::string::npos, "the row is built: " + vm);
			}
			const std::string kinds = "var f = function(int n) -> string { array<string> names = []; array<int> ints = []; array<float> floats = []; var mixed = []; string out = \"\";"
			                          " for (int i = 0; i < n; ++i) { names.push(\"n\"); ints.push(i); floats.push(i); floats.push(0.5); mixed.push(i); mixed.push(true); mixed.push(\"s\"); }"
			                          " out = out + names.join(\"\") + to_string(ints.size()) + to_string(floats[3]) + to_string(mixed.size());"
			                          " try { for (int i = 0; i < n; ++i) { ints.push(1.5); } } catch (e) { out = out + \"|\" + e; }"
			                          " array<string> tbl = [\"a\", \"b\"]; try { for (int i = 0; i < n; ++i) { names.push(tbl[i]); } } catch (e) { out = out + \"|\" + e; }"
			                          " return out; }; f(3);";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, kinds, proofs), run_on(true, kinds, proofs), std::string("typed receivers, a declined kind and an index past the end agree") + (proofs ? "" : " (proofs off)"));
			}
		});
		test("ternary_arms_take_every_landing_and_an_int_cell_takes_a_float", [this]() {
			// the ray step's column loop shapes (GLOOM pure.jai 28..62) without its inner while: `float adx = rdx < 0.0 ? -rdx : rdx;`
			// (a unary arm), `int mx = posx;` and `int texcol = wallx * 64.0;` (a float into an int, truncated as the declaration
			// does), `pdist = pdist < 0.02 ? 0.02 : pdist;` (a ternary assignment); the loop closes and agrees with the interpreter,
			// and a float out of the int range inside the loop raises the general text on both backends
			const char* fn = "var f = function(int n, float posx, float posy, float dirx, float planex, var& out) -> float {"
			                 " float acc = 0.0; float xstep = 2.0 / (n - 1.0);"
			                 " for (int x = 0; x < n; ++x) {"
			                 "  float camx = x * xstep - 1.0; float rdx = dirx + planex * camx;"
			                 "  int mx = posx; int my = posy;"
			                 "  float adx = rdx < 0.0 ? -rdx : rdx; float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;"
			                 "  int stepx = rdx < 0.0 ? -1 : 1;"
			                 "  float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;"
			                 "  float pdist = sdx - ddx; pdist = pdist < 0.02 ? 0.02 : pdist;"
			                 "  float wallx = posy + pdist * rdx; wallx = wallx - math::floor(wallx);"
			                 "  int texcol = wallx * 64.0; int shade = 15 - math::itrunc(pdist * 1.55);"
			                 "  shade = shade < 0 ? 0 : (shade > 15 ? 15 : shade);"
			                 "  out[x] = texcol * 16 + shade + stepx + my; acc = acc + pdist;"
			                 " }"
			                 " return acc; };";
			const std::string listing = listing_of(fn);
			check_true(listing.find("CELLS_") != std::string::npos, "the column loop closes: " + listing);
			check_true(listing.find("UNARY op=") == std::string::npos, "the unary arm lands through the move row: " + listing);
			const std::string call = std::string(fn) + " array<int> out = [0, 0, 0, 0, 0, 0]; float r = f(6, 3.5, 2.25, 1.0, 0.66, out);"
			                         " to_string(r) + \" \" + to_string(out[0]) + \" \" + to_string(out[2]) + \" \" + to_string(out[5]);";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, call, proofs), in = run_on(false, call, proofs);
				check_eq(in, vm, std::string("both backends agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
			const std::string narrow = "var g = function(int n, float big) -> string { string out = \"\"; int keep = 0;"
			                           " try { for (int i = 0; i < n; ++i) { float f = i * 2.5 + (i == 3 ? big : 0.0); int t = f; keep = keep + t; int s = 7; s = i > 1 ? f : s; keep = keep + s; } }"
			                           " catch (e) { out = \"caught \" + e; }"
			                           " return out + \"|\" + to_string(keep); }; g(3, 1.0e30) + \" \" + g(6, 1.0e30) + \" \" + g(4, -0.0);";
			for (bool proofs : { true, false }) {
				check_eq(run_on(false, narrow, proofs), run_on(true, narrow, proofs), std::string("a float into an int cell truncates in range and raises the general text out of range") + (proofs ? "" : " (proofs off)"));
			}
		});
		test("an_outer_loop_closes_over_an_inner_while", [this]() {
			// the ray step's column loop with its DDA while inside (GLOOM pure.jai 28) and render_view_impl's column loop with its
			// two pixel whiles (render.jai 336): the outer counted loop closes as one block whose inner back edges are backward cell
			// jumps; the results agree with the interpreter; an inner while that never exits raises the budget's text on both backends
			const char* ray = "class Ws { int mw = 8; array<int> pack = []; array<float> odist = [0.0, 0.0, 0.0, 0.0]; array<int> okind = [0, 0, 0, 0]; }"
			                  " var f = function(var& t, int n, float posx, float posy, float dirx, float planex) -> float {"
			                  " int mw = t.mw; float acc = 0.0; float xstep = 2.0 / (n - 1.0);"
			                  " for (int x = 0; x < n; ++x) {"
			                  "  float camx = x * xstep - 1.0; float rdx = dirx + planex * camx; float rdy = 0.3 + 0.1 * camx;"
			                  "  int mx = posx; int my = posy;"
			                  "  float adx = rdx < 0.0 ? -rdx : rdx; float ady = rdy < 0.0 ? -rdy : rdy;"
			                  "  float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx; float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;"
			                  "  int stepx = rdx < 0.0 ? -1 : 1; int stepy = rdy < 0.0 ? -1 : 1;"
			                  "  float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx; float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;"
			                  "  int side = 0; int kind = 1; int guard = 0; bool hit = false;"
			                  "  while (!hit && guard < 128) {"
			                  "   if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; } else { sdy += ddy; my += stepy; side = 1; }"
			                  "   int cell = my * mw + mx; int word = t.pack[cell / 15]; int c = (word >> ((cell % 15) * 4)) & 15;"
			                  "   if (c != 0) { hit = true; kind = c; }"
			                  "   guard = guard + 1;"
			                  "  }"
			                  "  float pdist = side == 0 ? sdx - ddx : sdy - ddy; pdist = pdist < 0.02 ? 0.02 : pdist;"
			                  "  t.odist[x] = pdist; t.okind[x] = kind; acc = acc + pdist + guard;"
			                  " }"
			                  " return acc; };";
			const std::string ray_listing = listing_of(ray);
			check_true(ray_listing.find("-- open loop") == std::string::npos && ray_listing.find("C_JUMP") != std::string::npos, "the column loop closes over its DDA while: " + ray_listing);
			const std::string ray_call = std::string(ray) + " var w = Ws(); array<int> pack = []; int word = 0; int fill = 0;"
			                             " for (int cell = 0; cell < 64; ++cell) { int cx = cell % 8; int cy = cell / 8; int k = (cx == 0 || cy == 0 || cx == 7 || cy == 7) ? 1 : 0;"
			                             "  word = word | (k << ((cell % 15) * 4)); fill = fill + 1; if (fill == 15) { pack.push(word); word = 0; fill = 0; } }"
			                             " if (fill > 0) { pack.push(word); } w.pack = pack;"
			                             " to_string(f(w, 4, 3.5, 3.5, 1.0, 0.66)) + \" \" + to_string(w.odist[1]) + \" \" + to_string(w.okind[3]) + \" \" + to_string(f(w, 4, 1.2, 6.1, -0.7, 0.2));";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, ray_call, proofs), in = run_on(false, ray_call, proofs);
				check_eq(in, vm, std::string("the ray columns agree") + (proofs ? "" : " (proofs off)"));
				check_true(vm.find("throw") == std::string::npos, "the call completes: " + vm);
			}
			const char* cols = "var g = function(var& pix, var& strip, int cols, int vh, int vw, int pxw) -> int {"
			                   " int touched = 0;"
			                   " for (int ci = 0; ci < cols; ++ci) {"
			                   "  int lineh = 1 + (ci * 7) % vh; lineh = lineh < 1 ? 1 : lineh; int y0 = (vh - lineh) / 2; int y1 = y0 + lineh;"
			                   "  int cy0 = y0 < 0 ? 0 : y0; int cy1 = y1 > vh ? vh : y1; int tstep = 65536 / lineh; int tacc = (cy0 - y0) * tstep;"
			                   "  int px0 = ci * pxw; int gi = cy0 * vw + px0; int y = cy0;"
			                   "  if (pxw == 2) { while (y < cy1) { int c = strip[tacc >> 11]; pix[gi] = c; pix[gi + 1] = c; tacc += tstep; gi += vw; y = y + 1; } }"
			                   "  else { while (y < cy1) { pix[gi] = strip[tacc >> 11]; tacc += tstep; gi += vw; y = y + 1; } }"
			                   "  touched = touched + 1;"
			                   " }"
			                   " return touched; };";
			const std::string cols_listing = listing_of(cols);
			check_true(cols_listing.find("-- open loop") == std::string::npos, "the column loop closes over its two pixel whiles: " + cols_listing);
			const std::string cols_call = std::string(cols) + " array<int> pix = []; for (int i = 0; i < 8 * 6; ++i) { pix.push(0); } array<int> strip = []; for (int i = 0; i < 32; ++i) { strip.push(i * 3 % 16); }"
			                              " int t2 = g(pix, strip, 4, 6, 8, 2); int t1 = g(pix, strip, 4, 6, 8, 1); to_string(t2) + \" \" + to_string(t1) + \" \" + to_string(pix[8 * 2 + 1]) + \" \" + to_string(pix[8 * 3 + 4]);";
			for (bool proofs : { true, false }) {
				const std::string vm = run_on(true, cols_call, proofs), in = run_on(false, cols_call, proofs);
				check_eq(in, vm, std::string("the pixel columns agree") + (proofs ? "" : " (proofs off)"));
			}
			// an inner while that never exits under a tiny budget: the same text on both backends, raised from the inner back edge
			const std::string spin = "var h = function(int n) -> int { int total = 0; for (int i = 0; i < n; ++i) { int k = 0; while (k >= 0) { k = k + 1; total = total + (k & 1); } } return total; };"
			                         " string out = \"\"; try { out = to_string(h(3)); } catch (e) { out = \"caught \" + e; } out;";
			for (bool vm : { false, true }) {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				e->execution_budget(0.00001);
				std::string got;
				try { got = e->execute(spin).as_string(); } catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_true(got.find("caught") != std::string::npos || got.find("throw") != std::string::npos, std::string(vm ? "vm" : "interp") + " raises the budget inside the inner while: " + got);
				check_true(got.find("budget") != std::string::npos || got.find("limit") != std::string::npos || got.find("exceeded") != std::string::npos, std::string(vm ? "vm" : "interp") + " names the budget: " + got);
			}
		});
		test("a_table_reached_by_several_member_sites_is_pinned_once", [this]() {
			// the ray step's shape: one member load and four member stores through one element reference into a typed record
			// buffer (five sites, one table); a body that also stores into that table keeps the general form; the same on both
			// backends, proofs on and off
			const std::string script = R"(
				class Rec { int x0 = 0; int x1 = 0; array<int> pack = []; array<float> odist = []; array<int> oside = []; array<int> okind = []; array<int> otex = [];
					Rec(int a, int b) { x0 = a; x1 = b; for (int i = 0; i < 64; ++i) { pack.push(i * 7 + 3); odist.push(0.0); oside.push(0); okind.push(0); otex.push(0); } } }
				array<Rec> WS = []; for (int k = 0; k < 4; ++k) { WS.push(new Rec(k * 16, k * 16 + 16)); }
				function step(var& t) -> void { int x0 = t.x0; int x1 = t.x1; int oi = 0; for (int x = x0; x < x1; ++x) { int cell = x * 3; int word = t.pack[cell / 15]; int c = (word >> ((cell % 15) * 4)) & 15; float pdist = c * 0.5 + 0.25; t.odist[oi] = pdist; t.oside[oi] = c & 1; t.okind[oi] = c; t.otex[oi] = word & 63; oi = oi + 1; } }
				function step_writes_table(var& t, var& ws, int k) -> int { int x0 = t.x0; int x1 = t.x1; int oi = 0; int acc = 0; for (int x = x0; x < x1; ++x) { int word = t.pack[x % 64]; t.okind[oi] = word & 15; acc = acc + t.okind[oi]; if (x == x0 + 2) { ws[k] = t; } oi = oi + 1; } return acc; }
				for (int r = 0; r < 3; ++r) { for (auto& t : WS) { step(t); } }
				int sum = 0; for (auto& t : WS) { for (int i = 0; i < 16; ++i) { sum = sum + t.oside[i] + t.okind[i] + t.otex[i] + math::itrunc(t.odist[i] * 4.0); } }
				int w = step_writes_table(WS[1], WS, 1) + step_writes_table(WS[2], WS, 3);
				to_string(sum) + "," + to_string(w) + "," + to_string(WS[3].x0) + "," + to_string(WS[3].okind[2]);
			)";
			both(script, "", "five member sites on one pinned table");
			both(script, "", "five member sites on one pinned table, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find(",32,") != std::string::npos, "the record loops ran and the table store copied the record: " + vm);
			const std::string l = listing_of("class Rec { int x0 = 0; int x1 = 0; array<int> pack = []; array<float> odist = []; array<int> oside = []; array<int> okind = []; array<int> otex = []; } var f = function(var& t) { int x0 = t.x0; int x1 = t.x1; int oi = 0; for (int x = x0; x < x1; ++x) { int word = t.pack[x / 15]; int c = (word >> ((x % 15) * 4)) & 15; t.odist[oi] = c * 0.5 + 0.25; t.oside[oi] = c & 1; t.okind[oi] = c; t.otex[oi] = word & 63; oi = oi + 1; } return oi; };");
			check_true(l.find("CELLS_BACK") != std::string::npos && l.find("C_INDEX_STORE") != std::string::npos && l.find("-- open loop") == std::string::npos, "the five-site record loop closes: " + l);
		});
		test("a_local_the_body_declares_lands_by_its_declaration_on_every_iteration", [this]() {
			// the ray step's `int mx = posx;`: a narrowing declaration inside the body lands the float by the declaration's kernel on
			// every iteration of a block run (not only the first, when the cell is empty), a float assigned to a body-declared int
			// local truncates as the assignment table does, a var re-kinds freely, an out-of-range float raises the general text with
			// the stores before it kept; the same on both backends, proofs on and off
			const std::string script = R"(
				function narrow(var& xs, int n) -> int { int acc = 0; float f = 0.5; int i = 0; while (i < n) { f = f + 1.25; int m = f; acc = acc + m; xs[i] = m; i = i + 1; } return acc; }
				function assign(var& xs, int n) -> int { int acc = 0; float f = 0.5; int i = 0; while (i < n) { f = f + 1.25; int m = 0; m = f; acc = acc + m; xs[i] = m; i = i + 1; } return acc; }
				function rekind(int n) -> string { string s = ""; float f = 0.5; int i = 0; while (i < n) { var v = i; if ((i % 2) == 1) { v = f; } s = s + to_string(v) + ","; f = f + 1.0; i = i + 1; } return s; }
				function far(var& xs, int n) -> string { float f = 1.0; int i = 0; try { while (i < n) { f = f * 10000000000.0; int m = f; xs[i] = m; i = i + 1; } } catch (err) { return "caught:" + err + "|" + to_string(i) + "|" + to_string(xs[0]); } return "ok"; }
				var XS = [0, 0, 0, 0, 0, 0, 0, 0];
				string a = to_string(narrow(XS, 6)) + "|" + to_string(assign(XS, 6)) + "|" + rekind(5) + "|" + to_string(XS[3]);
				a + "|" + far(XS, 6);
			)";
			both(script, "", "body-declared landings");
			both(script, "", "body-declared landings, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("27|27|0,1.5,2,3.5,4,|5|caught:", 0) == 0 && vm.find("|1|10000000000") != std::string::npos, "the declaration's kernel on every iteration, the var re-kinded, the out-of-range text after one kept store: " + vm);
			const std::string l = listing_of("var f = function(var& xs, int n) { int acc = 0; float f = 0.5; int i = 0; while (i < n) { f = f + 1.25; int m = f; acc = acc + m; xs[i] = m; i = i + 1; } return acc; };");
			check_true(l.find("CELLS_BACK") != std::string::npos && l.find("-- open loop") == std::string::npos, "the narrowing loop closes: " + l);
		});
		test("a_push_of_a_getter_backed_element_misses_before_the_getter_runs", [this]() {
			// Astra's host (2026-09-14): a property reference stored in an array<var> element through the native array view; its
			// getter adds 10 to a global (or rewrites the element); the block's push must not observe it ahead of the general
			// form, so the sums and the rewrite agree with the interpreter, proofs on and off (a getter that throws is a separate,
			// older boundary gap on both backends: the native exception is not the script's to catch through an element read)
			struct source_t { engine* eng = nullptr; int mode = 0; bool armed = false; int calls = 0; };
			auto run = [&](bool vm, bool proofs, int mode, const char* body) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				if (!proofs) { e->type_proofs(false); }
				stdlib::register_all(*e);
				source_t source{ e.get(), mode };
				dynamic_binder<source_t>(*e, "Source").property("number", [](source_t& s) -> script_int {
					if (!s.armed) { return 1; }
					++s.calls;
					if (s.mode == 0) { (void)s.eng->execute("G = G + 10;"); }
					else if (s.mode == 2 && s.calls == 2) { (void)s.eng->execute("SRC[0] = 7;"); }
					return 1;
				}, nullptr).build();
				e->add_global_ref("box", source);
				e->add_global("G", script_value(script_int(0), e.get()));
				(void)e->execute("var& r = box.number; var SRC = [0];");
				auto reference = e->get_global_environment()->get_ref("r");
				if (!reference) { return "no reference"; }
				auto src = e->get_variable("SRC");
				src.as_array()[0] = reference.value().get();
				source.armed = true; source.calls = 0;
				std::string out;
				try { out = e->execute(body).to_string(); }
				catch (const std::exception& ex) { out = std::string("throw:") + ex.what(); }
				return out + "|calls=" + std::to_string(source.calls) + "|G=" + e->get_variable("G").to_string();
			};
			const char* sum_body = "int probe() { array<int> dst = []; int sum = 0; int i = 0; while (i < 4) { dst.push(SRC[0]); sum = sum + G; i++; } return sum; } probe();";
			const char* rewrite_body = "string probe() { array<int> dst = []; int i = 0; while (i < 4) { dst.push(SRC[0]); i++; } return to_string(dst[0]) + \",\" + to_string(dst[1]) + \",\" + to_string(dst[2]) + \",\" + to_string(dst[3]); } probe();";
			for (bool proofs : { true, false }) {
				const std::string i0 = run(false, proofs, 0, sum_body), v0 = run(true, proofs, 0, sum_body);
				check_eq(i0, v0, "the getter's effect seen by the sum: both backends");
				check_eq(std::string("100|calls=4|G=40"), v0, "sum 100, four getter calls");
				const std::string i2 = run(false, proofs, 2, rewrite_body), v2 = run(true, proofs, 2, rewrite_body);
				check_eq(i2, v2, "the getter rewriting the element: both backends");
				check_true(v2.rfind("1,1,7,7|", 0) == 0, "the rewritten element from the third push: " + v2);
			}
		});
		test("a_block_closes_over_an_inner_counted_loop", [this]() {
			// part 2 of nested loops: draw_patch's shape (a counted column loop over a counted row loop with continues in both, a texel
			// table and a palette, the view width an environment scalar), += 2 and -- steps, a break out of the inner loop, an overflow
			// of the inner step (the general text, the count before it kept), an environment-scalar end, a three-deep nest, and a miss
			// inside the inner body (a bool element landing on an int declaration at the third column) that hands the general form the
			// rest of the iteration with the counted state in place; the same on both backends, proofs on and off
			const std::string script = R"(
				var VW = 8; var VH = 4;
				function patch(var& pix, var& tp, var& pal, int x0, int y0, int w, int h) -> int {
					int acc = 0;
					for (int sx = 0; sx < w; ++sx) { int px = x0 + sx; if (px < 0 || px >= VW) { continue; } int base = sx * h;
						for (int sy = 0; sy < h; ++sy) { int py = y0 + sy; if (py < 0 || py >= VH) { continue; } int texel = tp[base + sy]; if (texel >= 0) { pix[py * VW + px] = pal[texel]; acc = acc + pal[texel]; } } }
					return acc; }
				function steps(int n) -> int { int acc = 0; int i = 0; while (i < n) { for (int j = 0; j < 10; j += 2) { acc = acc + j; } for (int k = 5; k > 0; --k) { acc = acc + k; } i = i + 1; } return acc; }
				function brk(int n) -> int { int acc = 0; int i = 0; while (i < n) { for (int j = 0; j < 100; ++j) { if (j == 3) { break; } acc = acc + 1; } i = i + 1; } return acc; }
				function ovf() -> string { int acc = 0; try { int i = 0; while (i < 2) { for (int j = 9223372036854775800; j <= 9223372036854775807; j += 4) { acc = acc + 1; } i = i + 1; } } catch (err) { return "caught:" + err + "|" + to_string(acc); } return "ok:" + to_string(acc); }
				function envend(int n) -> int { int acc = 0; int i = 0; while (i < n) { for (int j = 0; j < VW; ++j) { acc = acc + j; } i = i + 1; } return acc; }
				function deep(int n) -> int { int acc = 0; int i = 0; while (i < n) { for (int a = 0; a < 3; ++a) { for (int b = 0; b < 2; ++b) { acc = acc + a * b + 1; } } i = i + 1; } return acc; }
				function missy(var& xs, int n) -> int { int acc = 0; int i = 0; while (i < n) { for (int j = 0; j < 4; ++j) { int v = xs[j]; acc = acc + v; } i = i + 1; } return acc; }
				var PIX = [0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0];
				var TP = [1, -1, 2, 3, 0, 1, 2, -1, 3, 3, 1, 0]; var PAL = [10, 20, 30, 40];
				var XS = [1, 2, true, 4];
				to_string(patch(PIX, TP, PAL, -1, 1, 4, 3)) + "|" + to_string(steps(3)) + "|" + to_string(brk(3)) + "|" + ovf() + "|" + to_string(envend(2)) + "|" + to_string(deep(2)) + "|" + to_string(missy(XS, 3)) + "|" + to_string(PIX[9]) + "," + to_string(PIX[17]);
			)";
			both(script, "", "inner counted loops");
			both(script, "", "inner counted loops, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("|caught:") != std::string::npos && vm.find("|2|") != std::string::npos && vm.find("|24|") != std::string::npos, "the shapes ran, the overflow caught after two steps, the bool element summed as one: " + vm);
			const std::string l = listing_of("var f = function(var& tp, var& pal, var& pix, int w, int h) { int acc = 0; for (int sx = 0; sx < w; ++sx) { for (int sy = 0; sy < h; ++sy) { int t = tp[sx * h + sy]; if (t >= 0) { pix[sy * 8 + sx] = pal[t]; acc = acc + t; } } } return acc; };");
			check_true(l.find("CELLS_CFOR_BACK") != std::string::npos && l.find("C_JCMP") != std::string::npos && l.find("-- open loop") == std::string::npos, "the counted column loop closes over its counted row loop: " + l);
		});
		test("an_untranslatable_instruction_inside_a_branch_arm_is_a_bail", [this]() {
			// GLOOM's column loop (a cache-fill arm storing a call result into a global table the first time a key is seen, the pixel
			// loop after it), JaiDOOM's fuzz loop (a call result stored into the pixel table under a parity test), a bail whose arm is
			// taken on every iteration, a bail inside an inner while, and a bail with a body-defined value live across it; the same on
			// both backends, proofs on and off; the listing shows the bail and no open loop
			const std::string script = R"(
				var STRIPS = [0, 0, 0, 0]; var STRIP_SET = [0, 0, 0, 0]; var PIX = [0, 0, 0, 0, 0, 0, 0, 0];
				function build_strip(int k) -> var { return [k * 10, k * 10 + 1, k * 10 + 2, k * 10 + 3]; }
				function columns(var& pix, int cols, int vh) -> int {
					int acc = 0; int x = 0;
					while (x < cols) {
						int sidx = x % 4; int have = STRIP_SET[sidx];
						if (have == 0) { STRIPS[sidx] = build_strip(sidx); STRIP_SET[sidx] = 1; }
						var& strip = STRIPS[sidx]; int y = 0; int gi = x;
						while (y < vh) { int c = strip[y]; pix[gi % 8] = c; acc = acc + c; gi = gi + 2; y = y + 1; }
						x = x + 1; }
					return acc; }
				function dark(int v) -> int { return v - 1; }
				function fuzz(var& pix, int n) -> int { int acc = 0; int i = 0; while (i < n) { int fpi = i % 8; if ((i & 1) != 0) { pix[fpi] = dark(pix[fpi]); } acc = acc + pix[fpi]; i = i + 1; } return acc; }
				function always(var& pix, int n) -> int { int acc = 0; int i = 0; while (i < n) { int fpi = i % 8; if (i >= 0) { pix[fpi] = dark(pix[fpi]); } acc = acc + pix[fpi]; i = i + 1; } return acc; }
				function inner(var& pix, int n) -> int { int acc = 0; int i = 0; while (i < n) { int j = 0; while (j < 4) { if (j == 3) { pix[j] = dark(pix[j]); } acc = acc + pix[j] + j; j = j + 1; } i = i + 1; } return acc; }
				function live(var& pix, int n) -> int { int acc = 0; int i = 0; while (i < n) { int t = i * 3 + 1; if (i == 2) { pix[i] = dark(pix[i]); } acc = acc + t * 2; i = i + 1; } return acc; }
				to_string(columns(PIX, 6, 4)) + "|" + to_string(fuzz(PIX, 10)) + "|" + to_string(always(PIX, 5)) + "|" + to_string(inner(PIX, 3)) + "|" + to_string(live(PIX, 5)) + "|" + to_string(PIX[0]) + "," + to_string(PIX[3]) + "," + to_string(STRIP_SET[2]);
			)";
			both(script, "", "bails");
			both(script, "", "bails, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("|") != std::string::npos, "the shapes ran: " + vm);
			const std::string l = listing_of("var STRIPS = [0, 0, 0, 0]; var STRIP_SET = [0, 0, 0, 0]; function build_strip(int k) -> var { return [k, k, k, k]; } var f = function(var& pix, int cols, int vh) { int acc = 0; int x = 0; while (x < cols) { int sidx = x % 4; int have = STRIP_SET[sidx]; if (have == 0) { STRIPS[sidx] = build_strip(sidx); STRIP_SET[sidx] = 1; } int y = 0; int gi = x; while (y < vh) { int c = STRIP_SET[sidx] + y; pix[gi % 8] = c; acc = acc + c; gi = gi + 2; y = y + 1; } x = x + 1; } return acc; };");
			check_true(l.find("C_BAIL") != std::string::npos && l.find("CELLS_BACK") != std::string::npos && l.find("-- open loop") == std::string::npos, "the column loop closes with a bail in its cache-fill arm (without the reference alias, a main-path gap of its own): " + l);
			const std::string m = listing_of("function dark(int v) -> int { return v - 1; } var f = function(var& pix, int n) { int acc = 0; int i = 0; while (i < n) { int fpi = i % 8; pix[fpi] = dark(pix[fpi]); acc = acc + pix[fpi]; i = i + 1; } return acc; };");
			check_true(m.find("-- open loop") != std::string::npos && m.find("translation at") != std::string::npos && m.find("C_BAIL") == std::string::npos, "a translation gap on the main path stays a refusal: " + m);
			// lights_tic's shape (JaiDOOM spec.jai): a declaration the translation does not take inside an arm, read by the arm's later
			// statements, under a continue-guarded body; a bail stands for the rest of its arm, so the arm's local leaves no cell to guard
			// at entry (the census at the first row 139 tip: lights_tic slot 5 want=1 have=0, refused at every entry)
			const std::string lights = R"(
				var LT_MAX = [200, 180, 160, 140]; var LT_MIN = [100, 90, 80, 70]; var SE_LIGHT = [0, 0, 0, 0]; var LT_CNT = [1, 2, 1, 3]; var LT_SEC = [0, 1, 2, 3]; var LT_KIND = [0, 1, 2, 0];
				function jitter(int k) -> int { return k % 3 + 1; }
				var tic = function() {
					int n = LT_SEC.size();
					for (int i = 0; i < n; ++i) {
						int s = LT_SEC[i]; int kind = LT_KIND[i];
						LT_CNT[i] = LT_CNT[i] - 1;
						if (LT_CNT[i] > 0) { continue; }
						if (kind == 1) {
							if (SE_LIGHT[s] == LT_MAX[i]) { SE_LIGHT[s] = LT_MIN[i]; LT_CNT[i] = jitter(i); }
							else { SE_LIGHT[s] = LT_MAX[i]; LT_CNT[i] = jitter(i + 1); }
						} else {
							int l2 = LT_MAX[i] - jitter(i) * 16;
							if (l2 < LT_MIN[i]) { l2 = LT_MIN[i]; }
							SE_LIGHT[s] = l2; LT_CNT[i] = 4;
						}
					}
				};
				tic(); tic(); tic();
				to_string(SE_LIGHT[0]) + "," + to_string(SE_LIGHT[1]) + "," + to_string(SE_LIGHT[2]) + "," + to_string(SE_LIGHT[3]) + "|" + to_string(LT_CNT[0]) + "," + to_string(LT_CNT[1]) + "," + to_string(LT_CNT[2]) + "," + to_string(LT_CNT[3]);
			)";
			both(lights, "", "a declaration bailed inside an arm");
			both(lights, "", "a declaration bailed inside an arm, proofs off", false);
			check_eq(std::string("184,180,112,124|2,2,2,4"), run_on(true, lights));
			const std::string k = listing_of(lights);
			size_t bails = 0, last_bail = std::string::npos;
			for (size_t at = k.find("C_BAIL"); at != std::string::npos; at = k.find("C_BAIL", at + 1)) { ++bails; last_bail = at; }
			// row 153: the two element stores of jitter's result graft (their arguments are locals), while `int l2 = LT_MAX[i] - jitter(i) * 16`
			// stands behind `LT_MAX[i]`, an element read of a var array with no kind to spill, so it keeps its call and its bail, standing for
			// the rest of its arm (the skips after it in the region's listing)
			(void)last_bail;
			size_t jitter_grafts = 0; for (size_t at = k.find("-- inlined jitter"); at != std::string::npos; at = k.find("-- inlined jitter", at + 1)) { ++jitter_grafts; }
			check_true(bails == 1 && jitter_grafts == 2 && k.find("-- open loop") == std::string::npos && k.find("bail->exit;") != std::string::npos && k.find("bail->exit;") < k.find("skip;", k.find("bail->exit;")),
			           "the light loop closes with one bail standing for the rest of its arm and two jitter grafts: " + k);
			const size_t cells_at = k.find("-- cell loop");
			const std::string cells_line = cells_at == std::string::npos ? std::string() : k.substr(cells_at, k.find('\n', cells_at) - cells_at);
			bool adopted_slot = false;
			for (size_t at = cells_line.find(" s"); at != std::string::npos; at = cells_line.find(" s", at + 1)) {
				size_t d = at + 2;
				while (d < cells_line.size() && cells_line[d] >= '0' && cells_line[d] <= '9') { ++d; }
				if (d > at + 2 && cells_line.compare(d, 2, ":0") == 0) { adopted_slot = true; }
			}
			check_true(!adopted_slot, "no slot cell of an adopted kind remains from the bailed arm: " + cells_line);
		});
		test("an_element_alias_declared_in_the_body_is_a_rebind", [this]() {
			// GLOOM's column loop as written (the cache-fill arm, then `var& strip = STRIPS[sidx]` and the pixel loop through it): the alias's
			// declaration rebinds its sites' container once per iteration and the loop closes; a store through the alias; a callee between the
			// alias and its reads replacing the element; a store into the source table (the pinned table refuses the entry); an element that is
			// not an array; a read through the alias missing mid-iteration (the general form resumes with the alias bound); both backends, proofs on and off
			const std::string script = R"(
				var STRIPS = [0, 0, 0, 0]; var STRIP_SET = [0, 0, 0, 0]; var PIX = [0, 0, 0, 0, 0, 0, 0, 0];
				function build_strip(int k) -> var { return [k * 10, k * 10 + 1, k * 10 + 2, k * 10 + 3]; }
				function columns(var& pix, int cols, int vh) -> int {
					int acc = 0; int x = 0;
					while (x < cols) {
						int sidx = x % 4; int have = STRIP_SET[sidx];
						if (have == 0) { STRIPS[sidx] = build_strip(sidx); STRIP_SET[sidx] = 1; }
						var& strip = STRIPS[sidx]; int y = 0; int gi = x;
						while (y < vh) { int c = strip[y]; pix[gi % 8] = c; acc = acc + c; gi = gi + 2; y = y + 1; }
						x = x + 1; }
					return acc; }
				function writes(int n) -> int {
					int acc = 0; int i = 0;
					while (i < n) { var& row = STRIPS[i % 4]; row[i % 4] = row[i % 4] + 1; acc = acc + row[(i + 1) % 4]; i = i + 1; }
					return acc; }
				function swap_strip(int k) -> int { STRIPS[k] = [k + 100, 1, 2, 3]; return 1; }
				function replaced(int n) -> int {
					int acc = 0; int i = 0;
					while (i < n) { var& s = STRIPS[i % 4]; int k = swap_strip(i % 4); acc = acc + s[0] + k; i = i + 1; }
					return acc; }
				function stale(int n) -> string {
					int acc = 0; int i = 0;
					try { while (i < n) { var& s = STRIPS[i % 4]; STRIPS[(i + 1) % 4] = 7; acc = acc + s[1]; i = i + 1; } } catch (err) { return "caught:" + to_string(acc); }
					return "ok:" + to_string(acc); }
				function scalar_element(int n) -> string {
					var T = [1, [2, 3], 4]; int acc = 0; int i = 0;
					try { while (i < n) { var& e = T[i]; acc = acc + e[0]; i = i + 1; } } catch (err) { return "caught:" + to_string(acc); }
					return "ok:" + to_string(acc); }
				function mid_loop_miss(int n) -> string {
					var T = [[1, 2], [3, "x"], [5, 6]]; int acc = 0; int i = 0;
					try { while (i < n) { var& r = T[i]; int a = r[0]; int b = r[1]; acc = acc + a + b; i = i + 1; } } catch (err) { return "caught:" + to_string(acc); }
					return "ok:" + to_string(acc); }
				to_string(columns(PIX, 6, 4)) + "|" + to_string(writes(6)) + "|" + to_string(replaced(5)) + "|" + stale(5) + "|" + scalar_element(3) + "|" + mid_loop_miss(3) + "|" + to_string(PIX[0]) + "," + to_string(PIX[3]) + "," + to_string(STRIPS[3][1]);
			)";
			both(script, "", "element alias rebinds");
			both(script, "", "element alias rebinds, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("caught:") != std::string::npos, "the shapes ran: " + vm);
			const std::string l = listing_of("var STRIPS = [0, 0, 0, 0]; var STRIP_SET = [0, 0, 0, 0]; function build_strip(int k) -> var { return [k, k, k, k]; } var f = function(var& pix, int cols, int vh) { int acc = 0; int x = 0; while (x < cols) { int sidx = x % 4; int have = STRIP_SET[sidx]; if (have == 0) { STRIPS[sidx] = build_strip(sidx); STRIP_SET[sidx] = 1; } var& strip = STRIPS[sidx]; int y = 0; int gi = x; while (y < vh) { int c = strip[y]; pix[gi % 8] = c; acc = acc + c; gi = gi + 2; y = y + 1; } x = x + 1; } return acc; };");
			check_true(l.find("C_REBIND") != std::string::npos && l.find("C_BAIL") != std::string::npos && l.find("CELLS_BACK") != std::string::npos && l.find("-- open loop") == std::string::npos, "the column loop closes with its alias as a rebind and its cache-fill arm as a bail: " + l);
		});
		test("a_range_for_over_an_array_is_a_block", [this]() {
			// a range-for over an array as a block head: by value (a typed buffer, a var array, an int-declared variable over floats, which binds
			// the element as the general form does), by reference over arrays of arrays (a rebind), a continue and a break, the body growing the
			// array (the live size), an element the cells do not take (a string: one miss, then the call's iterations run general), a call inside,
			// an instance element by reference (a miss at the landing until member cells), a scalar element by reference, a mid-body miss that
			// resumes at the right element, and a nested range-for; both backends, proofs on and off
			const std::string script = R"(
				class P { float x = 0.0; float vx = 1.0; }
				function twice(int v) -> int { return v * 2; }
				function sum_typed(int n) -> int { array<int> a = [1, 2, 3, 4]; int s = 0; int k = 0; while (k < n) { for (int v : a) { s = s + v; } k = k + 1; } return s; }
				function sum_var() -> int { var a = [5, 6, 7]; int s = 0; for (auto v : a) { s = s + v; } return s; }
				function int_over_floats() -> string { var a = [1.5, 2.5]; string t = ""; for (int v : a) { t = t + type_of(v); } return t; }
				function rows() -> int { var g = [[1, 2], [3, 4], [5, 6]]; int s = 0; for (auto& r : g) { r[0] = r[0] + 10; s = s + r[0] + r[1]; } return s + g[2][0]; }
				function skip_break() -> int { var a = [1, 2, 3, 4, 5, 6]; int s = 0; for (auto v : a) { if (v == 2) { continue; } if (v == 5) { break; } s = s + v; } return s; }
				function grows() -> int { var a = [1, 2]; int s = 0; for (auto v : a) { if (v == 2) { a.push(9); } s = s + v; } return s; }
				function strings() -> string { var a = ["a", "bb", "ccc"]; int n = 0; for (auto v : a) { n = n + 1; } return to_string(n); }
				function calls() -> int { var a = [1, 2, 3]; int s = 0; for (auto v : a) { s = s + twice(v); } return s; }
				function objs() -> float { array<P> pool = [P(), P(), P()]; for (auto& p : pool) { p.x = p.x + p.vx; } float s = 0.0; for (auto& p : pool) { s = s + p.x; } return s; }
				function scalar_ref() -> int { var a = [1, 2, 3]; for (auto& v : a) { v = v * 2; } return a[0] + a[1] + a[2]; }
				function mid_miss() -> string { var a = [1, 2, 2.5, 4]; int s = 0; try { for (auto v : a) { int w = v; s = s + w; } } catch (err) { return "caught:" + to_string(s); } return "ok:" + to_string(s); }
				function nested() -> int { var g = [[1, 2], [3]]; int s = 0; for (auto& r : g) { for (auto v : r) { s = s + v; } } return s; }
				to_string(sum_typed(3)) + "|" + to_string(sum_var()) + "|" + int_over_floats() + "|" + to_string(rows()) + "|" + to_string(skip_break()) + "|" + to_string(grows()) + "|" + strings() + "|" + to_string(calls()) + "|" + to_string(objs()) + "|" + to_string(scalar_ref()) + "|" + mid_miss() + "|" + to_string(nested());
			)";
			both(script, "", "range-for blocks");
			both(script, "", "range-for blocks, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("30|18|floatfloat|66|8|12|3|12|3.0|12|", 0) == 0 && vm.size() > 2 && vm.substr(vm.size() - 2) == "|6", "the shapes: " + vm);
			const std::string l = listing_of("var f = function(var& a) { int s = 0; for (int v : a) { s = s + v; } return s; };");
			check_true(l.find("C_RANGE") != std::string::npos && l.find("C_BACK_STEP") != std::string::npos && l.find("C_LOAD") != std::string::npos && l.find("-- open loop") == std::string::npos, "a by-value range closes with the range head and the stepping back edge: " + l);
			const std::string m = listing_of("var f = function(var& g) { int s = 0; for (auto& r : g) { r[0] = r[0] + 1; s = s + r[1]; } return s; };");
			check_true(m.find("C_RANGE") != std::string::npos && m.find("C_REBIND") != std::string::npos && m.find("-- open loop") == std::string::npos, "a by-reference range closes with a rebind at the head: " + m);
		});
		test("member_reads_land_in_temps_and_member_stores_read_their_base_from_the_slot", [this]() {
			// the general form: a member read of a frame-slot base is a compiler temp (op_get_member's kernel, its place among the operands
			// kept), so it feeds the fused binaries and the typed lowering; a member store on a frame-slot base reads the base from the slot.
			// Both members of a binary, a member with a local, a member with a global read first, a user getter in a binary, a getter
			// that changes the partner global (the order), a null base (the text), a store through a reference parameter, a store into
			// an int field (the conversion), a store whose value is used, a user setter; both backends, proofs on and off
			const std::string script = R"(
				class P { float x = 1.5; float vx = 0.25; int n = 3; string s = "a"; }
				class G { int hits = 0; int _get_v() { hits = hits + 1; TOTAL = TOTAL + 100; return 7; } void _set_w(int v) { hits = hits + 10; } }
				var TOTAL = 1;
				function both_members(P p) -> float { return p.x + p.vx; }
				function member_local(P p, float y) -> float { float a = p.x * y; float b = y - p.vx; return a + b; }
				function global_first(G g) -> int { return TOTAL + g.v; }
				function getter_binary(G g) -> int { int r = g.v * 2 + g.v; return r + g.hits; }
				function null_base(int k) -> string { P q = P(); var z = null; try { if (k == 1) { return to_string(z.x + 1.0); } } catch (err) { return "caught:" + err; } return to_string(q.n + 1); }
				function store_ref(P& p, float d) -> float { p.x = p.x + d; p.vx = d; return p.x + p.vx; }
				function store_int(P p) -> string { try { p.n = p.n + 1; p.n = 2.5; } catch (err) { return "caught:" + to_string(p.n); } return "ok:" + to_string(p.n); }
				function store_used(P p) -> float { float r = (p.x = 4.5) + 1.0; return r + p.x; }
				function setter(G g) -> int { g.w = 5; g.w = g.hits + 1; return g.hits; }
				function decls(P p) -> string { float x = p.x; int k = p.n; var s = p.s; auto v = p.vx; p.x = 2.5; try { var q = p.x; int bad = q; return "ok:" + to_string(x + k + v) + s + to_string(bad); } catch (err) { return "caught:" + to_string(x + k + v) + s; } }
				function cond(P p, int t) -> int { int c = 0; if (p.n > t) { c = c + 1; } if (p.x < 2.0 && p.vx > 0.0) { c = c + 10; } return c; }
				class Leaf { int v = 0; string tag = "leaf"; } class Node { Leaf a = Leaf(); }
				function copies(Node n) -> string { auto l = n.a; l.tag = l.tag + "!"; var m = n.a; m.v = 5; string t = n.a.tag; t = t + "?"; return n.a.tag + to_string(n.a.v) + l.tag + to_string(m.v) + t; }
				P p = P(); G g = G(); Node nd = Node();
				to_string(both_members(p)) + "|" + to_string(member_local(p, 2.0)) + "|" + to_string(global_first(g)) + "|" + to_string(getter_binary(g)) + "|" + null_base(1) + "|" + null_base(0) + "|" + to_string(store_ref(p, 0.5)) + "|" + store_int(p) + "|" + to_string(store_used(p)) + "|" + to_string(setter(g)) + "|" + decls(p) + "|" + to_string(cond(p, 2)) + "|" + copies(nd) + "|" + to_string(TOTAL) + "|" + to_string(p.x);
			)";
			both(script, "", "member temps");
			both(script, "", "member temps, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("caught:") != std::string::npos, "the shapes ran: " + vm);
			const std::string l = listing_of("class P { float x = 1.5; float vx = 0.25; } var f = function(P p, float y) { float w = p.x; p.x = w + y; return p.x + p.vx; };");
			check_true(l.find("MEMBER_TEMP") != std::string::npos && (l.find("SET_MEMBER_FUSED") != std::string::npos || l.find("SET_MEMBER_VALUE") != std::string::npos) && l.find("T_DECL_MOV") != std::string::npos && l.find("GET_MEMBER") == std::string::npos && l.find("SET_MEMBER ") == std::string::npos, "member reads are temps (the declaration a move row) and the store reads its base from the slot: " + l);
		});
		test("an_alias_lives_to_the_end_of_the_body_and_an_inner_counted_step_polls_the_budget", [this]() {
			// Astra's reproductions on the landed 016175eb (2026-09-16): a return inside the body reading the alias after its last read
			// inside the block (the flush left the first iteration's reference), and an inner counted loop whose step never polled the budget
			// (a zero step, a huge bound); and the coverage limit beside them: a while holding a counted for was skipped as the counted
			// loop's own generic duplicate. Both backends, proofs on and off
			const std::string alias = "int probe() { var strips = [[1], [2, 20], [3, 30, 300]]; int sum = 0; int i = 0; while (i < 3) { var& strip = strips[i]; sum += strip[0]; if (i == 2) { return sum + strip.size(); } i++; } return sum; } to_string(probe());";
			both(alias, "", "an alias live at a return");
			both(alias, "", "an alias live at a return, proofs off", false);
			check_eq(std::string("9"), run_on(true, alias));
			const std::string alias_break = "int probe() { var g = [[1], [2, 20], [3, 30, 300]]; int sum = 0; int i = 0; int last = 0; while (i < 3) { var& row = g[i]; sum += row[0]; if (sum > 2) { last = row.size(); break; } i++; } return sum * 10 + last; } to_string(probe());";
			both(alias_break, "", "an alias live at a break");
			check_eq(std::string("32"), run_on(true, alias_break));
			auto budget_run = [&](bool vm, bool proofs, const std::string& src) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				if (!proofs) { e->type_proofs(false); }
				e->execution_budget(0.05);
				try { return "ok:" + e->execute(src).as_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string late = "int probe() { int sum = 0; for (int i = 0; i < 3; i++) { int step = 1; if (i == 1) { step = 0; } for (int j = 0; j < 3; j += step) { sum += 0; } } return sum; } to_string(probe());";
			const std::string bound = "int probe() { int sum = 0; for (int i = 0; i < 3; i++) { int bound = 3; if (i == 1) { bound = 1000000000; } for (int j = 0; j < bound; j++) { sum += 0; } } return sum; } to_string(probe());";
			for (const std::string* src : { &late, &bound }) {
				const std::string interp = budget_run(false, true, *src);
				check_true(interp.rfind("throw:", 0) == 0, "the interpreter raises the budget: " + interp);
				check_eq(interp, budget_run(true, true, *src));
				check_eq(interp, budget_run(true, false, *src));
			}
			const std::string l = listing_of("var f = function(int n) { int s = 0; int i = 0; while (i < n) { for (int j = 0; j < 3; j++) { s = s + j; } i = i + 1; } return s; };");
			size_t closed = 0;
			for (size_t at = l.find("-- cell loop"); at != std::string::npos; at = l.find("-- cell loop", at + 1)) { ++closed; }
			check_true(closed >= 2 && l.find("-- open loop") == std::string::npos, "a while holding a counted for closes over it (the duplicate test names the counted loop's own fallback): " + l);
			check_true(l.find("C_JUMP") != std::string::npos, "the inner step is preceded by the budget poll: " + l);
		});
		test("a_typed_push_moves_the_payload_without_an_intermediate_value", [this]() {
			// row 143 (Astra's first value-level experiment): a cell or a typed element pushed into a typed buffer moves its payload; a hetero
			// destination still takes a value with the cell's declared stamp; the conversion subset, the memory charge, self pushes and the
			// fallbacks are the general push's. Both backends, proofs on and off
			const std::string script = R"(
				array<int> INTS = [1, 2, 3]; array<float> FLOATS = [0.5, 1.5]; array<string> STRS = ["a", "bb"]; var PAL = ["x", "yy", "zzz"];
				function ints(int n) -> int { array<int> out = []; int i = 0; while (i < n) { int v = i * 3; out.push(v); int m = i % 3; out.push(INTS[m]); i = i + 1; } int s = 0; int k = 0; while (k < out.size()) { s = s + out[k]; k = k + 1; } return s * 100 + out.size(); }
				function floats(int n) -> float { array<float> out = []; int i = 0; while (i < n) { float v = i * 0.25; out.push(v); out.push(i); int m = i % 2; out.push(FLOATS[m]); i = i + 1; } float s = 0.0; int k = 0; while (k < out.size()) { s = s + out[k]; k = k + 1; } return s * 10.0 + out.size(); }
				function strings(int n) -> string { array<string> out = []; int i = 0; while (i < n) { int m = i % 2; out.push(STRS[m]); i = i + 1; } string s = ""; int k = 0; while (k < out.size()) { s = s + out[k]; k = k + 1; } return s + to_string(out.size()); }
				function hetero(int n) -> string { var out = []; int i = 0; while (i < n) { int v = i + 1; float w = i * 0.5; bool b = i % 2 == 0; out.push(v); out.push(w); out.push(b); int m = i % 3; out.push(PAL[m]); out.push(INTS[m]); i = i + 1; } return to_string(out.size()) + type_of(out[0]) + type_of(out[1]) + type_of(out[2]) + type_of(out[3]) + type_of(out[4]) + to_string(out[3]); }
				function float_into_ints(int n) -> string { array<int> out = []; int i = 0; try { while (i < n) { float v = i * 1.5; out.push(v); i = i + 1; } } catch (err) { return "caught:" + to_string(out.size()); } return "ok:" + to_string(out.size()); }
				function string_into_ints(int n) -> string { array<int> out = []; int i = 0; try { while (i < n) { int m = i % 2; var s = STRS[m]; out.push(s); i = i + 1; } } catch (err) { return "caught:" + to_string(out.size()); } return "ok:" + to_string(out.size()); }
				function self_push(int n) -> int { array<int> a = [1, 2]; int i = 0; while (i < n) { a.push(a[i]); i = i + 1; } int s = 0; int k = 0; while (k < a.size()) { s = s + a[k]; k = k + 1; } return s * 100 + a.size(); }
				function self_push_strings(int n) -> string { array<string> a = ["p", "q"]; int i = 0; while (i < n) { a.push(a[i]); i = i + 1; } string s = ""; int k = 0; while (k < a.size()) { s = s + a[k]; k = k + 1; } return s; }
				to_string(ints(6)) + "|" + to_string(floats(5)) + "|" + strings(5) + "|" + hetero(4) + "|" + float_into_ints(3) + "|" + string_into_ints(3) + "|" + to_string(self_push(6)) + "|" + self_push_strings(5);
			)";
			both(script, "", "typed pushes");
			both(script, "", "typed pushes, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("caught:") != std::string::npos, "the shapes ran: " + vm);
			const std::string l = listing_of("array<int> INTS = [1, 2, 3]; var f = function(int n) { array<int> out = []; int i = 0; while (i < n) { int v = i * 3; out.push(v); int m = i % 3; out.push(INTS[m]); i = i + 1; } return out.size(); };");
			check_true(l.find("C_PUSH") != std::string::npos && l.find("-- open loop") == std::string::npos, "the typed pushes close the loop: " + l);
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
		test("an_injected_miss_at_every_plain_position_keeps_the_interpreter_s_answer", [this]() {
			// the miss injection lever (row 143.1; a build with JAISCRIPT_MISS_INJECTION defined, else the shapes run as plain parity):
			// every nth plain cell instruction misses before it runs, so the flush, the alias and temp liveness, the inner counted
			// loop's state, the call door's landing and the range head's index sync continue on the general form from every position
			const char* body = R"(
				array<int> G = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8];
				array<float> F = [0.5, 1.5, 2.5, 3.5];
				int twice(int v) { return v * 2; }
				string probe() {
					int acc = 0; float facc = 0.0; array<int> out = [];
					int n = G.size();
					for (int i = 0; i < n; i++) {
						var& e = G[i];
						int t = e * 3 + i;
						if (t % 2 == 0) { acc = acc + twice(t); } else { acc = acc - t; }
						for (int j = 0; j < 3; j++) { acc = acc + j * e; }
						e = e + 1;
						out.push(t);
					}
					for (auto v : G) { acc = acc + v; }
					int k = 0; int m = F.size();
					while (k < m) { facc = facc + F[k] * 2.0; F[k] = F[k] + 1.0; k = k + 1; }
					return to_string(acc) + "|" + to_string(facc) + "|" + to_string(out.size()) + "|" + to_string(G[0]) + "|" + to_string(F[3]);
				}
				probe();
			)";
			auto run = [&](bool vm, uint32_t every) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				if (every) { e->cells_miss_every(every); }
				stdlib::register_all(*e);
				try { return e->execute(body).to_string(); }
				catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string oracle = run(false, 0);
			check_true(oracle.rfind("throw:", 0) != 0, "the shapes run: " + oracle);
			check_eq(oracle, run(true, 0), "the vm without the lever");
			if (!engine::miss_injection_available()) { return; }
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u, 11u, 13u }) {
				check_eq(oracle, run(true, every), "an injected miss at every " + std::to_string(every) + " plain cell instructions");
			}
		});
		test("a_var_local_fixed_to_int_by_a_typed_use_keeps_its_float", [this]() {
			// row 143.2: a var local declared in the body from a float constant, incremented by the speculative typed row: the cell's
			// kind was fixed to int by the increment and the landing took that for the declaration's kind (a span truncated the
			// constant, a loop's pinned arithmetic read the float's bits). The declaration's own kind judges the first write, the
			// cell's contract refuses any other kind at the landing, and a constant seeds the cell's kind at admission
			const char* script =
				"function span_shape() -> string { var f = 1.5; ++f; f--; ++f; return to_string(f); }\n"
				"function loop_shape() -> float { float acc = 0.0; for (int i = 0; i < 3; i++) { var f = 1.5; ++f; f--; ++f; acc = acc + f; } return acc; }\n"
				"function any_shape() -> float { float acc = 0.0; for (int i = 0; i < 3; i++) { var f = 1.5; f = f * 1.5; ++f; acc = acc + f; } return acc; }\n"
				"function from_slot(var g) -> float { float acc = 0.0; for (int i = 0; i < 3; i++) { var f = g; ++f; acc = acc + f; } return acc; }\n"
				"function int_var() -> int { int acc = 0; for (int i = 0; i < 40; i++) { var n = 0; n++; ++n; acc = acc + n; } return acc; }\n"
				"span_shape() + \"|\" + to_string(loop_shape()) + \"|\" + to_string(any_shape()) + \"|\" + to_string(from_slot(2.5)) + \"|\" + to_string(from_slot(2)) + \"|\" + to_string(int_var());";
			both(script, "2.5|7.5|9.75|10.5|9.0|80", "a var local under a typed use");
			both(script, "", "a var local under a typed use, proofs off", false);
			const std::string l = listing_of("var f = function() -> int { int acc = 0; for (int i = 0; i < 40; i++) { var n = 0; n++; ++n; acc = acc + n; } return acc; };");
			check_true(l.find("-- cell loop") != std::string::npos, "an int var under the typed rows still closes: " + l);
		});
		test("two_environment_arguments_of_one_call_push_their_own_values", [this]() {
			// row 143.3: a call with two global arguments inside a block ran both general loads at the copy's ip, so they shared one
			// lookup cache slot and the second argument received the first's value (JaiDOOM's draw_weapon once its body was a span);
			// the loop shape and the span shape, both backends, and the values through a third callee that writes the globals first
			const char* script =
				"var PX = 0.0; var PY = 0.0; var W = 320; int HITS = 0;\n"
				"function place(int k, float s) -> void { PX = W / 2.0 + k * s; PY = W / 4.0 - k * s; }\n"
				"function take(int k, float x, float y, float s) -> void { HITS = HITS + math::itrunc(x * 10.0) + math::itrunc(y * 1000.0) + k; }\n"
				"function span_shape(int k) -> int { int lump = k + 1; if (lump < 0) { return -1; } float scale = W / 320.0; place(lump, scale); take(lump, PX, PY, scale); return HITS; }\n"
				"function loop_shape(int n) -> int { int acc = 0; for (int i = 0; i < n; i++) { place(i, 0.5); take(i, PX, PY, 0.5); acc = acc + HITS; } return acc; }\n"
				"to_string(span_shape(3)) + \"|\" + to_string(span_shape(7)) + \"|\" + to_string(loop_shape(4));";
			both(script, "", "two environment arguments");
			both(script, "", "two environment arguments, proofs off", false);
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
		});
		test("a_call_the_expression_consumes_lands_in_a_temp_and_the_loop_closes", [this]() {
			// row 146a: `if (side(x, y, n) == 0)`, `acc + twice(i) * 2`, `while (dec(k) > 0)`: the call's result lands in a compiler temp
			// (op_call_temp) when its partner is a literal or a private local, the consumer reads a slot, the door lands it in a temp
			// cell; an environment partner keeps the stack form (the general read must follow the call); a string result and a
			// reference result land on the general form; an error inside the callee keeps its text
			const std::string prelude =
				"array<float> NX = [0.0, 1.0, 2.0]; array<float> NY = [0.0, 1.0, 0.0]; array<int> RIGHT = [1, 2, -1]; array<int> LEFT = [2, -1, -1]; int G = 5;\n"
				"int side(float x, float y, int n) { float dx = x - NX[n]; float dy = y - NY[n]; if (dx * 2.0 > dy) { return 1; } return 0; }\n"
				"int twice(int v) { return v * 2; }\n"
				"int dec(int v) { return v - 1; }\n"
				"string name(int i) { if (i % 2 == 0) { return \"a\"; } return \"b\"; }\n"
				"int ovf(int v) { return v * 3; }\n"
				"var walk = function(float x, float y) { int n = 0; int steps = 0; while (n >= 0 && steps < 8) { if (side(x, y, n) == 0) { n = RIGHT[n]; } else { n = LEFT[n]; } steps = steps + 1; } return steps * 10 + n; };\n"
				"var sum = function(int k) { int acc = 0; for (int i = 0; i < k; i++) { acc = acc + twice(i); } return acc; };\n"
				"var down = function(int k) { int c = 0; while (dec(k) > 0) { k = k - 1; c = c + 1; } return c; };\n"
				"var envpartner = function(int k) { int s = 0; for (int i = 0; i < k; i++) { s = s + (G + twice(i)); } return s; };\n"
				"var names = function(int k) { int c = 0; for (int i = 0; i < k; i++) { if (name(i) == \"b\") { c = c + 1; } } return c; };\n"
				"var blows = function(int k) { int c = 0; string m = \"\"; try { for (int i = 0; i < k; i++) { if (ovf(4611686018427387904) > 0) { c = c + 1; } } } catch (err) { m = err; } return m; };\n";
			const std::string body =
				"string run() { return to_string(walk(0.5, 0.2)) + \"|\" + to_string(sum(6)) + \"|\" + to_string(down(5)) + \"|\" + to_string(envpartner(4)) + \"|\" + to_string(names(7)) + \"|\" + blows(2); }\n"
				"run();";
			both(prelude + body, "", "calls as operands");
			both(prelude + body, "", "calls as operands, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("Integer overflow") != std::string::npos, "the shapes ran: " + vm);
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			// row 153: side, twice and dec are leaves and graft; a call the graft does not take lands in a temp as before
			check_true(count("CALL_TEMP") + count("-- inlined ") >= 4, "the calls the expressions consume land in temps or graft: " + std::to_string(count("CALL_TEMP")) + " + " + std::to_string(count("-- inlined ")) + "\n" + l);
			check_true(count("C_PROBE") + count("-- inlined ") >= 3, "the loops around them close with the door or the graft: " + std::to_string(count("C_PROBE")) + "\n" + l);
			const std::string envl = listing_of("int G = 5; int twice(int v) { return v * 2; } var envpartner = function(int k) { int s = 0; for (int i = 0; i < k; i++) { s = s + (G + twice(i)); } return s; };");
			check_true(envl.find("CALL_TEMP") == std::string::npos, "an environment partner keeps the general order: " + envl);
			const std::string refl = listing_of("function mutate(int& v) -> int { v = 20; return 2; } var g = function(int n) { int acc = 0; for (int i = 0; i < n; i++) { int x = 3; acc = acc + (x + mutate(x)); } return acc; };");
			check_true(refl.find("CALL_TEMP") == std::string::npos, "a partner the call names as an argument keeps the stack form (the binary reads x before the call writes it): " + refl);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "calls as operands under an injected miss at every " + std::to_string(every)); }
		});
		test("a_member_read_of_a_frame_slot_instance_is_a_block_site", [this]() {
			// row 147a: `p.x` inside a loop and inside a span is a load site: the field resolved once at entry, C_MLOAD lands its kind; a
			// string field, a user accessor and a null base keep the general form (the entry refuses); a callee that rewrites the field
			// between reads is seen (the hold check after the call re-resolves); both backends, proofs on and off
			const std::string prelude =
				"class P { int x = 3; float y = 1.5; bool on = true; string name = \"p\"; int _get_z() { return x * 100; } }\n"
				"int bump(P& p) { p.x = p.x + 1; return p.x; }\n"
				"var sum = function(P p, int n) { int acc = 0; for (int i = 0; i < n; i++) { acc = acc + p.x * i; if (p.on) { acc = acc + 1; } } return acc; };\n"
				"var mix = function(P p) { float a = p.y * 2.0; int b = p.x + 4; float c = a + b; int d = b * 3; float e = c - d; int f = d + 1; float g = e + f; return g; };\n"
				"var mix2 = function(P p, int n) { float acc = 0.0; for (int i = 0; i < n; i++) { acc = acc + p.y * 2.0 + p.x * 3; } return acc; };\n"
				"var strs = function(P p, int n) { int acc = 0; for (int i = 0; i < n; i++) { acc = acc + p.name.size(); } return acc; };\n"
				"var acc = function(P p, int n) { int t = 0; for (int i = 0; i < n; i++) { t = t + p.z; } return t; };\n"
				"var rewritten = function(P p, int n) { int t = 0; for (int i = 0; i < n; i++) { t = t + p.x; bump(p); } return t; };\n";
			const std::string body =
				"string run() { P p = P(); P q = P(); q.x = 10;\n"
				"  return to_string(sum(p, 5)) + \"|\" + to_string(mix(q)) + \"|\" + to_string(mix2(q, 4)) + \"|\" + to_string(strs(p, 3)) + \"|\" + to_string(acc(p, 2)) + \"|\" + to_string(rewritten(q, 4)) + \"|\" + to_string(q.x); }\n"
				"run();";
			both(prelude + body, "", "member read sites");
			both(prelude + body, "", "member read sites, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			check_true(count("C_MLOAD") >= 2, "the member reads are sites: " + std::to_string(count("C_MLOAD")) + "\n" + l);
			check_true(l.find("-- cell loop") != std::string::npos, "the loop over p.x closes: " + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "member read sites under an injected miss at every " + std::to_string(every)); }
		});
		test("a_member_read_through_an_element_alias_is_resolved_per_rebind", [this]() {
			// row 148: `for (auto& p : pool) { p.x }` over a hetero list of instances and over a class-typed buffer, `var& e = list[i]; e.x`,
			// a subclass element with the same field (the per-site cache re-probes on a class change), a callee that replaces the element
			// between reads (the hold check re-applies the rebind), a string field (the general form); both backends, proofs on and off
			const std::string prelude =
				"class P { int x = 3; float y = 1.5; bool on = true; string name = \"p\"; }\n"
				"class D : P { int extra = 7; D(int v) { x = v; on = false; } }\n"
				"var LIST = [P(), P(), D(10), P()];\n"
				"array<P> BUF = [P(), P(), P()];\n"
				"int swap(int i) { LIST[i] = D(100); return 1; }\n"
				"var sum = function(int n) { int acc = 0; for (int r = 0; r < n; r++) { for (auto& p : LIST) { acc = acc + p.x * 2; if (p.x > 5) { acc = acc + 1; } } } return acc; };\n"
				"var fsum = function() { float acc = 0.0; for (auto& p : BUF) { acc = acc + p.y * 2.0; } return acc; };\n"
				"var byidx = function() { int acc = 0; int n = LIST.size(); for (int i = 0; i < n; i++) { var& e = LIST[i]; acc = acc + e.x * 3; } return acc; };\n"
				"var replaced = function() { int acc = 0; int n = LIST.size(); for (int i = 0; i < n; i++) { var& e = LIST[i]; int before = e.x; int s = swap(i); acc = acc + before + s + e.x; } return acc; };\n"
				"var names = function() { int acc = 0; for (auto& p : LIST) { acc = acc + p.name.size(); } return acc; };\n";
			const std::string body =
				"string run() { string a = to_string(sum(3)) + \"|\" + to_string(fsum()) + \"|\" + to_string(byidx()) + \"|\" + to_string(names());\n"
				"  return a + \"|\" + to_string(replaced()) + \"|\" + to_string(byidx()); }\n"
				"run();";
			both(prelude + body, "", "alias member sites");
			both(prelude + body, "", "alias member sites, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			check_true(count("C_MLOAD") >= 5, "the member reads through the aliases are sites: " + std::to_string(count("C_MLOAD")) + "\n" + l);
			check_true(count("-- cell loop") >= 4, "the loops over the aliases close: " + std::to_string(count("-- cell loop")) + "\n" + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "alias member sites under an injected miss at every " + std::to_string(every)); }
		});
		test("a_member_store_in_statement_position_is_one_op_and_a_block_site", [this]() {
			// row 147b: `p.x = v;` takes its value as an operand (SET_MEMBER_VALUE, no push, no POP) and a block writes the field in place
			// (C_MSTORE) when the value's kind is the field's kind; an int into a float field, a float into a var field holding an int, a
			// string field and a user setter run the general store (its conversions and texts); the value read back after the loop, both
			// backends, proofs on and off, and under an injected miss
			const std::string prelude =
				"class P { int x = 0; float y = 0.0; bool on = false; var w = 1; string name = \"p\"; int _set_z(int v) { x = v * 100; return v; } int z = 0; }\n"
				"class T { float posx = 0.0; float posy = 0.0; int hits = 0; }\n"
				"var WS = [T(), T(), T()];\n"
				"var fill = function(P p, int n) { for (int i = 0; i < n; i++) { p.x = i; p.y = i * 0.5; p.on = i > 2; p.w = i; } return p.x * 1000 + p.w * 10 + (p.on ? 1 : 0); };\n"
				"var conv = function(P p, int n) { for (int i = 0; i < n; i++) { p.y = i; p.w = i * 0.5; p.x = p.x + 2; } return to_string(p.y) + \"|\" + to_string(p.w) + \"|\" + to_string(p.x); };\n"
				"var sweep = function(float px, float py) { int t = 0; for (auto& w : WS) { w.posx = px; w.posy = py + w.hits; w.hits = w.hits + 1; t = t + w.hits; } return t; };\n"
				"var strs = function(P p, int n) { for (int i = 0; i < n; i++) { p.name = \"n\" + to_string(i); } return p.name; };\n"
				"var setter = function(P p, int n) { for (int i = 0; i < n; i++) { p.z = i; } return p.x; };\n";
			const std::string body =
				"string run() { P a = P(); P b = P(); P c = P(); P d = P(); string out = to_string(fill(a, 5)) + \"|\" + conv(b, 3) + \"|\" + to_string(sweep(1.5, 2.0)) + \"|\" + to_string(sweep(0.5, 0.0));\n"
				"  return out + \"|\" + strs(c, 3) + \"|\" + to_string(setter(d, 4)) + \"|\" + to_string(WS[1].posy) + \"|\" + to_string(WS[2].hits); }\n"
				"run();";
			both(prelude + body, "", "member stores");
			both(prelude + body, "", "member stores, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			check_true(count("SET_MEMBER_VALUE") >= 8, "the statement stores take the value as an operand: " + std::to_string(count("SET_MEMBER_VALUE")) + "\n" + l);
			check_true(count("C_MSTORE") >= 6, "the stores are block sites: " + std::to_string(count("C_MSTORE")) + "\n" + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "member stores under an injected miss at every " + std::to_string(every)); }
		});
		test("an_element_alias_passed_to_a_callee_passes_its_reference_and_the_loop_stays_closed", [this]() {
			// row 148.1: `for (auto& p : pool) { step(p); }` with `step(var& p)` writing the element's fields: the alias is a call argument, the door
			// pushes the element reference the general LOAD pushes, the callee's writes land in the element, the loop closes and runs without a
			// miss (the injected-miss controls cover the door); a by-value callee copies as on the general form; a body-declared alias too
			const std::string prelude =
				"class P { int kind = 1; float x = 0.0; int life = 5; }\n"
				"function step(var& p) -> void { if (p.kind == 0) { return; } int life = p.life - 1; if (life <= 0) { p.kind = 0; p.life = 0; return; } p.x = p.x + 1.5; p.life = life; }\n"
				"function peek(P p) -> int { p.life = 100; return p.life + p.kind; }\n"
				"array<P> POOL = [new P(), new P(), new P(), new P()];\n"
				"var LIST = [P(), P(), P()];\n"
				"var run_steps = function(int n) { for (int t = 0; t < n; t++) { for (auto& p : POOL) { step(p); } } float s = 0.0; for (auto& p : POOL) { s = s + p.x + p.life + p.kind; } return s; };\n"
				"var peeks = function() { int s = 0; for (auto& p : LIST) { s = s + peek(p); } int u = 0; for (auto& p : LIST) { u = u + p.life; } return s * 1000 + u; };\n"
				"var byidx = function() { float s = 0.0; int n = LIST.size(); for (int i = 0; i < n; i++) { var& e = LIST[i]; step(e); s = s + e.x; } return s; };\n";
			const std::string body = "string run() { return to_string(run_steps(7)) + \"|\" + to_string(peeks()) + \"|\" + to_string(byidx()); }\nrun();";
			both(prelude + body, "", "alias arguments");
			both(prelude + body, "", "alias arguments, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			check_true(count("C_PROBE") + count("-- inlined ") >= 3, "the loops with the calls close through the door or take the leaf inline: " + std::to_string(count("C_PROBE")) + "\n" + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "alias arguments under an injected miss at every " + std::to_string(every)); }
		});
		test("adjacent_plain_cell_pairs_run_as_one_dispatch", [this]() {
			// row 150: arithmetic chains, compare chains and element load pairs in a block are fused pairs in the listing; the values match the
			// interpreter with the middle end's switch on and off, under the injected miss at every plain position (both halves count), with
			// an overflow inside a pair's second half (the general text), and with a jump landing on a pair's second instruction
			const std::string prelude =
				"array<int> A = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3]; array<int> B = [2, 7, 1, 8, 2, 8, 1, 8, 2, 8, 4, 5, 9, 0, 4, 5];\n"
				"var chains = function(int n) { int acc = 0; for (int i = 0; i < n; i++) { int a = i * 3 + 1; int b = a * 2 - i; int c = b + a + 7; acc = acc + c - b + a; } return acc; };\n"
				"var compares = function(int n) { int hits = 0; for (int i = 0; i < n; i++) { int p0 = A[i]; int p1 = B[i]; int p2 = A[15 - i]; int p3 = B[15 - i]; if (p0 == p1 && p0 == p2 && p0 == p3) { hits = hits + 100; continue; } if (p0 == p2 && p1 == p3) { hits = hits + 10; continue; } if (p0 > p1 && p2 > p3) { hits = hits + 1; } } return hits; };\n"
				"var loads = function(int n) { int s = 0; for (int i = 0; i < n; i++) { int x = A[i]; int y = B[i]; int z = A[(i + 1) & 15]; int w = B[(i + 2) & 15]; s = s + x * y - z + w; } return s; };\n"
				"var landing = function(int n) { int s = 0; for (int i = 0; i < n; i++) { if (i > 7) { s = s + 1; } s = s + i * 2; s = s - 1; } return s; };\n"
				"var blows = function(int n) { int s = 0; string m = \"\"; try { for (int i = 0; i < n; i++) { int a = 4611686018427387900 + i; int b = a * 2 + a; s = s + b; } } catch (err) { m = err; } return m; };\n";
			const std::string body = "string run() { return to_string(chains(16)) + \"|\" + to_string(compares(16)) + \"|\" + to_string(loads(16)) + \"|\" + to_string(landing(16)) + \"|\" + blows(3); }\nrun();";
			both(prelude + body, "", "fused pairs");
			both(prelude + body, "", "fused pairs, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0 && vm.find("Integer overflow") != std::string::npos, "the shapes ran: " + vm);
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(vm, off, "the middle end's switch off answers the same");
			}
			const std::string l = listing_of(prelude);
			auto count = [&](const char* needle) { size_t n = 0; for (size_t at = l.find(needle); at != std::string::npos; at = l.find(needle, at + 1)) { ++n; } return n; };
			check_true(count("C_ARITH_P2") >= 2, "the arithmetic chains pair: " + std::to_string(count("C_ARITH_P2")) + "\n" + l);
			check_true(count("C_JCMP_P2") >= 2, "the compare chains pair: " + std::to_string(count("C_JCMP_P2")) + "\n" + l);
			check_true(count("C_LOAD2") >= 1, "the element loads pair: " + std::to_string(count("C_LOAD2")) + "\n" + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "fused pairs under an injected miss at every " + std::to_string(every)); }
		});
		test("inlined_leaves_run_inside_blocks", [this]() {
			// row 152: small free leaves called from a loop block (light_band's early returns, dist2's plain body, lerp's floats, a var
			// parameter, a void leaf through an element alias) compile at their call sites; the values hold on both backends, with proofs
			// off, with the middle end's switch off (no graft) and under the injected miss; the listing names the grafts and the loop closes
			const std::string prelude =
				"int light_band(int l, int d) { int b = l - d / 8; if (b < 0) { return 0; } if (b > 31) { return 31; } return b; }\n"
				"int dist2(int ax, int ay, int bx, int by) { int dx = ax - bx; int dy = ay - by; return dx * dx + dy * dy; }\n"
				"float lerp(float a, float b, float t) { return a + (b - a) * t; }\n"
				"var twice(var v) { return v * 2; }\n"
				"class P { int x = 0; int y = 0; }\n"
				"void nudge(var& p, int k) { p.x = p.x + k; if (p.x > 50) { p.y = p.y + 1; } }\n"
				"var pool = []; for (int i = 0; i < 6; ++i) { pool.push(P()); }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { int lb = light_band(i, i * 3); int d = dist2(i, i + 1, 7, 9); float f = lerp(1.0, 5.0, i * 0.01); s = s + lb * 1000 + d % 977 + twice(i) + math::itrunc(f * 10.0); for (auto& p : pool) { nudge(p, i % 5); } } for (auto& p : pool) { s = s * 7 + p.x * 3 + p.y; } return s; };\n";
			const std::string body = "to_string(run(60)) + \"|\" + to_string(run(7));";
			both(prelude + body, "", "inlined leaves");
			both(prelude + body, "", "inlined leaves, proofs off", false);
			const std::string vm = run_on(true, prelude + body);
			check_true(vm.rfind("throw:", 0) != 0, "the shapes ran: " + vm);
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(vm, off, "the middle end's switch off (no graft) agrees");
			}
			const std::string l = listing_run(prelude + body);
			check_true(l.find("-- inlined light_band") != std::string::npos && l.find("-- inlined dist2") != std::string::npos && l.find("-- inlined nudge") != std::string::npos, "the leaves are inlined: " + l);
			check_true(l.find("-- cell loop") != std::string::npos, "the loop closes around the grafts: " + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) { check_eq(vm, injected(every), "inlined leaves under an injected miss at every " + std::to_string(every)); }
		});
		test("methods_graft_at_their_call_sites", [this]() {
			// row 171: recv.m(args) on a slot receiver compiles as the method's body (this and the bare fields read through the receiver);
			// a subclass instance behind a base reference takes the fallback (its own override), a field owned by a user accessor keeps the
			// call (the bare name is the backing field), a void method storing fields, a method with a loop; the values hold on both
			// backends, with proofs off, with the middle end off and under the injected miss; the listing names the graft and the loop closes
			const std::string prelude =
				"class Pt { float x = 0.0; float y = 0.0; Pt(float a, float b) { x = a; y = b; } function len2() -> float { return x * x + y * y; } "
				"function scale(float k) -> void { x = x * k; y = this.y * k; } function tri(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s = s + i; } return s; } }\n"
				"class Pt3 : Pt { float z = 0.0; Pt3(float a) : super(a, a) { z = a; } override function len2() -> float { return x * x + y * y + z * z; } }\n"
				"class Acc { float x = 3.0; function _get_x() -> float { return x + 1.0; } function twice() -> float { return x * 2.0; } }\n"
				"function measure(Pt& r) -> float { float h = 0.0; for (int i = 0; i < 3; ++i) { h += r.len2(); } return h; }\n"
				"var run = function(int n) -> string { Pt p = Pt(1.0, 2.0); float fs = 0.0; int k = 0; for (int i = 0; i < n; ++i) { fs += p.len2(); k = k + p.tri(i % 4); } "
				"p.scale(2.0); float g = p.len2(); var t3 = Pt3(1.0); float h = measure(t3); float h2 = measure(p); Acc a = Acc(); float w = 0.0; for (int i = 0; i < 2; ++i) { w += a.twice(); } "
				"return \"\" + fs + \",\" + k + \",\" + g + \",\" + h + \",\" + h2 + \",\" + w; };\n";
			const std::string body = "run(10);";
			both(prelude + body, "50.0,8,20.0,9.0,60.0,12.0", "method grafts");
			both(prelude + body, "50.0,8,20.0,9.0,60.0,12.0", "method grafts, proofs off", false);
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(std::string("50.0,8,20.0,9.0,60.0,12.0"), off, "the middle end's switch off (no graft) agrees");
			}
			const std::string l = listing_run(prelude + body);
			check_true(l.find("-- inlined len2") != std::string::npos && l.find("-- inlined scale") != std::string::npos, "the methods are inlined: " + l);
			check_true(l.find("-- inlined twice") == std::string::npos, "an accessor-owned field keeps the call: " + l);
			check_true(l.find("-- cell loop") != std::string::npos, "a loop closes around a method graft: " + l);
			if (!engine::miss_injection_available()) { return; }
			const std::string vm = run_on(true, prelude + body);
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 7u }) { check_eq(vm, injected(every), "method grafts under an injected miss at every " + std::to_string(every)); }
		});
		test("a_method_graft_follows_reloads", [this]() {
			// row 171: the guard validates the receiver's class at its method epoch; a reload of the class (a new body)
			// takes the fallback, so the caller sees the new method without recompiling
			const std::string prelude =
				"class Q { int v = 2; function get() -> int { return v * 10; } }\n"
				"class R { int v = 0; function get() -> int { return v + 1000; } }\n"
				"var run = function(Q& q, int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + q.get(); } return s; };\n"
				"Q qq = Q();\n";
			auto sequence = [&](bool vm) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				try {
					e->execute(prelude);
					const std::string a = e->execute("run(qq, 4);").to_string();
					e->execute("class Q { int v = 2; function get() -> int { return v * 100 + 1; } }");
					const std::string b = e->execute("run(qq, 4);").to_string();
					return a + "|" + b;
				} catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string iv = sequence(false), vv = sequence(true);
			check_eq(iv, vv, "the reload takes the fallback on the vm");
			check_eq(std::string("80|804"), vv, "the values before and after the reload");
		});
		test("member_reads_of_elements_close_loops", [this]() {
			// row 173: a[i].m inside a loop over an array of instances lands the element in an alias temp and the member in a member temp;
			// the loop closes, and the kernel facts hold: a subclass element, a user accessor read per element, a store through a call between
			// reads, an out-of-range index's text; both backends, proofs off, the middle end off and the injected miss agree
			const std::string prelude =
				"class Pt { float x = 0.0; float y = 0.0; bool alive = true; Pt(float a, float b) { x = a; y = b; alive = a > 2.0; } }\n"
				"class Pt2 : Pt { Pt2(float a) : super(a, a * 2.0) { } }\n"
				"class Acc { float v = 1.0; int reads = 0; function _get_v() -> float { reads += 1; return v + 0.5; } }\n"
				"function bump(Pt& p) -> void { p.x = p.x + 100.0; }\n"
				"var run = function(int n) -> string { array<Pt> pts = []; for (int i = 0; i < n; ++i) { pts.push(Pt(i * 1.0, 2.0)); } pts.push(Pt2(3.0)); "
				"float fs = 0.0; float gs = 0.0; int alive = 0; for (int i = 0; i < pts.size(); ++i) { fs += pts[i].x; gs = gs + pts[i].y * 2.0; if (pts[i].alive) { alive += 1; } if (i == 1) { bump(pts[2]); } } "
				"array<Acc> as = [Acc(), Acc()]; float w = 0.0; for (int i = 0; i < 2; ++i) { w += as[i].v; } "
				"string err = \"\"; try { for (int i = 0; i < 9; ++i) { fs += pts[i].x; } } catch (ex) { err = ex; } "
				"return \"\" + fs + \",\" + gs + \",\" + alive + \",\" + w + \",\" + as[0].reads + \"|\" + err; };\n";
			const std::string body = "run(5);";
			const std::string interp = run_on(false, prelude + body);
			both(prelude + body, "", "element member reads");
			both(prelude + body, "", "element member reads, proofs off", false);
			check_true(interp.find("226.0,32.0,3,3.0,1|") == 0, interp.c_str());
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(interp, off, "the middle end's switch off agrees");
			}
			const std::string l = listing_run(R"(
				class Pt { float x = 0.0; float y = 0.0; }
				function f(array<Pt> pts) -> float { float fs = 0.0; for (int i = 0; i < pts.size(); ++i) { fs += pts[i].x; } return fs; }
				array<Pt> xs = [Pt(), Pt()]; f(xs);
			)");
			check_true(l.find("-- cell loop") != std::string::npos, "the element member loop closes: " + l);
			if (!engine::miss_injection_available()) { return; }
			auto injected = [&](uint32_t every) -> std::string {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				try { return e->execute(prelude + body).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			for (uint32_t every : { 1u, 2u, 3u, 7u }) { check_eq(interp, injected(every), "element member reads under an injected miss at every " + std::to_string(every)); }
		});
		test("constructor_field_stores_convert_and_run_accessors_elsewhere", [this]() {
			// row 172: bare field stores in constructor bodies keep their this-field entry across constructions: the declared type converts
			// (int from float, float from int), a subclass constructor's stores, a user setter on another field untouched by the bare store
			const std::string script = R"(
				class C { int n = 0; float f = 0.0; float g = 0.0; int sets = 0; function _set_g(float v) -> void { sets += 1; g = v * 2.0; } C(float a, int b) { n = a; f = b; g = a; } }
				class D : C { int m = 0; D(float a) : super(a, 2) { m = a; } }
				function run() -> string { int sn = 0; float sf = 0.0; float sg = 0.0; int st = 0; for (int i = 0; i < 50; ++i) { C c = C(i * 1.5, i); sn += c.n; sf += c.f; sg += c.g; st += c.sets; D d = D(2.5); sn += d.m; } return "" + sn + "," + sf + "," + sg + "," + st; }
				run();
			)";
			both(script, "", "constructor field stores");
			both(script, "", "constructor field stores, proofs off", false);
		});
		test("lambda_locals_graft_at_their_calls", [this]() {
			// row 175: a local declared with a lambda literal grafts at its calls; a local rebound to another lambda, a capturing lambda
			// and an untyped result keep the call; the values hold on both backends, proofs off and the middle end off
			const std::string script = R"(
				int K = 3;
				function run() -> string {
					auto add = [](int a, int b) -> int { return a + b; };
					auto scale = [](float v) -> float { return v * 2.5; };
					int s = 0; float g = 0.0;
					for (int i = 0; i < 10; ++i) { s = add(s, i); g += scale(i * 1.0); }
					auto f = [](int a) -> int { return a + 1; };
					int t = 0;
					for (int i = 0; i < 4; ++i) { t = f(t); if (i == 1) { f = [](int a) -> int { return a * 10; }; } }
					int base = 7;
					auto cap = [=](int a) -> int { return a + base; };
					int u = 0;
					for (int i = 0; i < 3; ++i) { u = cap(u); }
					auto untyped = [](int a) { return a + K; };
					int w = 0;
					for (int i = 0; i < 3; ++i) { w = untyped(w); }
					return "" + s + "," + g + "," + t + "," + u + "," + w;
				}
				run();
			)";
			both(script, "45,112.5,200,21,9", "lambda grafts");
			both(script, "45,112.5,200,21,9", "lambda grafts, proofs off", false);
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(script).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(std::string("45,112.5,200,21,9"), off, "the middle end's switch off agrees");
			}
			const std::string l = listing_run(script);
			check_true(l.find("-- inlined <lambda>") != std::string::npos, "the lambda calls are inlined: " + l);
		});
		test("constructor_bodies_run_as_method_frames", [this]() {
			// row 176: constructor bodies through the method door: falling off the end yields the instance, private members (the eager
			// environment), defaults, super chains, a raise inside the body, methods and lambdas that see `this`; both backends agree
			const std::string script = R"(
				class A { int x = 0; A(int v) { if (v > 0) { x = v; } else { x = -1; } } }
				class P { private: int secret = 3; public: int shown = 0; P(int k) { shown = secret * k; } function get() -> int { return shown; } }
				class D { int a = 0; int b = 0; D(int x, int y = 7) { a = x; b = y; } }
				class Base { int n = 1; Base(int k) { n = k * 10; } }
				class Kid : Base { int m = 0; Kid(int k) : super(k) { m = n + 1; } }
				class Bad { int x = 0; Bad(int v) { x = 10 / v; } }
				class Cb { int v = 0; var f = null; Cb(int k) { v = k; f = [this]() -> int { return v * 2; }; } }
				function run() -> string {
					A a = A(5); A b = A(-2);
					P p = P(4); D d = D(1); D e = D(2, 3); Kid k = Kid(4); Cb c = Cb(21);
					string err = "";
					try { Bad z = Bad(0); } catch (ex) { err = ex; }
					int s = 0; for (int i = 1; i < 50; ++i) { A t = A(i); s += t.x; }
					return "" + a.x + "," + b.x + "," + p.get() + "," + d.b + "," + e.b + "," + k.m + "," + c.f() + "," + s + "|" + err;
				}
				run();
			)";
			const std::string interp = run_on(false, script);
			check_eq(interp, run_on(true, script), "constructors: interpreter and vm agree");
			check_true(interp.find("5,-1,12,7,3,41,42,1225|") == 0, interp.c_str());
		});
		test("untyped_lambdas_graft_with_their_proven_return_kind", [this]() {
			// row 180: an untyped lambda whose returns all yield one certain scalar kind grafts with that result type; a lambda with mixed
			// returns, an uncertain return and a void body keep the call; values agree on both backends, proofs off and the middle end off
			const std::string script = R"(
				function run() -> string {
					auto add = [](int a, int b) { return a + b; };
					auto half = [](float v) { if (v > 10.0) { return v * 0.5; } return v; };
					auto pos = [](int a) { return a > 0; };
					auto mixed = [](int a) { if (a > 2) { return a; } return 0.5; };
					var box = [0];
					auto grab = [](var b) { return b[0]; };
					int s = 0; float h = 0.0; int p = 0; float m = 0.0; int g = 0;
					for (int i = 0; i < 8; ++i) { s = add(s, i); h += half(i * 3.0); if (pos(i - 3)) { p += 1; } m += mixed(i); g += grab(box); }
					return "" + s + "," + h + "," + p + "," + m + "," + g;
				}
				run();
			)";
			both(script, "28,51.0,4,26.5,0", "untyped lambda grafts");
			both(script, "28,51.0,4,26.5,0", "untyped lambda grafts, proofs off", false);
			{
				auto e = engine::make(); e->set_backend(backend_type::vm); e->ir_passes(false); stdlib::register_all(*e);
				std::string off; try { off = e->execute(script).to_string(); } catch (const std::exception& ex) { off = std::string("throw:") + ex.what(); }
				check_eq(std::string("28,51.0,4,26.5,0"), off, "the middle end's switch off agrees");
			}
			const std::string l = listing_run(script);
			check_true(l.find("-- inlined <lambda>") != std::string::npos, "the untyped lambdas are inlined: " + l);
		});
		test("an_inlined_callee_redefined_takes_the_fallback", [this]() {
			// row 152: the guard compares the name's binding with the function the graft was made from; a reload of the callee (the same
			// unit executed again with another body) fails it, and the fallback runs the real call, so the caller sees the new body without
			// recompiling; both backends agree across the reload
			const std::string prelude =
				"int f(int a) { return a * 2; }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + f(i); } return s; };\n";
			auto sequence = [&](bool vm) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				try {
					e->execute(prelude);
					const std::string a = e->execute("run(6);").to_string();
					e->execute("int f(int a) { return a * 3 + 1; }");
					const std::string b = e->execute("run(6);").to_string();
					return a + "|" + b;
				} catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string iv = sequence(false), vv = sequence(true);
			check_eq(iv, vv, "the reload takes the fallback on the vm");
			check_eq(std::string("30|51"), vv, "the values before and after the reload");
		});
		test("a_dropped_jaibite_leaves_its_stamps_resolvable_or_expired", [this]() {
			// the checker's callee stamps outlive the program they name: a host that executes a jaibite and drops it (the demoreel)
			// keeps the leaf graftable while its function lives, and a leaf whose declaration died keeps the call (a stamp read
			// after free crashed the demoreel's first frames)
			const std::string program =
				"int leaf(int a) { return a * 2 + 1; }\n"
				"int caller(int n) { int s = 0; for (int i = 0; i < n; ++i) { int v = leaf(i); s = s + v; } return s; }\n";
			auto sequence = [&](bool vm, bool redefine, std::string* listing) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				if (listing) { e->disassembly_sink([listing](const std::string& l, const std::string&) { *listing += l; *listing += "\n"; }); }
				try {
					{ auto bite = e->jaibite(program); bite.execute(); }
					if (redefine) { e->execute("int leaf(int a) { return a * 3; }"); }
					return e->execute("caller(6);").to_string();
				} catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			std::string kept_listing, gone_listing;
			check_eq(sequence(false, false, nullptr), sequence(true, false, &kept_listing), "the live leaf: both backends");
			check_eq(std::string("36"), sequence(true, false, nullptr), "the live leaf's value");
			check_true(kept_listing.find("-- inlined leaf") != std::string::npos, "the leaf grafts after its jaibite is gone: " + kept_listing);
			check_eq(sequence(false, true, nullptr), sequence(true, true, &gone_listing), "the redefined leaf: both backends");
			check_eq(std::string("45"), sequence(true, true, nullptr), "the redefined leaf's value");
			check_true(gone_listing.find("-- call kept leaf line 2: a callee whose declaration is gone") != std::string::npos, "the dead declaration keeps the call: " + gone_listing);
		});
		test("a_raise_inside_an_inlined_body_names_the_callee", [this]() {
			// row 152: an inlined body that raises keeps the text and the trace's first frame is the callee, as the interpreter's is
			const std::string prelude =
				"int f(int a) { return 100 / a; }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + f(3 - i); } return s; };\n";
			both(prelude + "run(6);", "", "a raise inside an inlined body");
			auto e = engine::make(); e->set_backend(backend_type::vm); stdlib::register_all(*e);
			std::string text;
			try { e->execute(prelude + "run(6);"); } catch (const std::exception& ex) { text = ex.what(); }
			check_true(text.find("zero") != std::string::npos || text.find("Zero") != std::string::npos, "the division text: " + text);
			const auto trace = e->last_stack_trace();
			check_true(!trace.empty() && trace.front().function == "f", "the trace names the callee first: " + (trace.empty() ? std::string("<empty>") : trace.front().function));
		});
		test("a_global_read_before_a_writing_callee_is_not_hoisted", [this]() {
			// row 152: `s + G + bump()` reads G before the call on both backends; a body that writes a global keeps a statement that reads
			// one before it on the general form (no graft), so the order holds
			const std::string prelude =
				"int G = 1;\n"
				"int bump() { G = G + 1; return G; }\n"
				"int pure(int a) { return a + 1; }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + G + bump(); s = s + G + pure(i); } return s; };\n";
			both(prelude + "run(8);", "", "a global read before a writing callee");
			const std::string l = listing_run(prelude + "run(8);");
			check_true(l.find("-- inlined bump") == std::string::npos, "bump is not inlined after a global read: " + l);
			check_true(l.find("-- inlined pure") != std::string::npos, "pure is inlined after a global read: " + l);
		});
		test("string_and_var_parameters_at_the_grafts_edge", [this]() {
			// row 152: a var parameter taking a scalar local with a proven int return is a graft; a string parameter, a string result or a
			// var result keeps the call (their declarations would open the enclosing block); the values hold either way
			const std::string prelude =
				"string tag(string s, int k) { if (k > 2) { return s + \"!\"; } return s; }\n"
				"var half(var v) { return v / 2; }\n"
				"int half2(var v) { int r = v / 2; return r; }\n"
				"var run = function(int n) { int s = 0; string acc = \"\"; for (int i = 0; i < n; ++i) { acc = acc + tag(\"a\", i); s = s + tag(\"bc\", i).size(); s = s + half(i); s = s + half2(i); } return acc + \"|\" + to_string(s); };\n";
			both(prelude + "run(6);", "", "string and var parameters");
			const std::string l = listing_run(prelude + "run(6);");
			check_true(l.find("-- inlined half2") != std::string::npos, "half2 is inlined: " + l);
			check_true(l.find("-- inlined tag") == std::string::npos && l.find("-- inlined half ") == std::string::npos, "tag (a string) and half (a var result) keep their calls: " + l);
		});
		test("a_call_under_a_conditional_operand_keeps_its_call", [this]() {
			// row 152: a leaf in a ternary's arm or on the right side of && / || runs on the paths that choose it; a graft would run its body
			// on every path, so the statement keeps the call (N counts bump's runs); a leaf in a plain operand of the same loop grafts
			const std::string prelude =
				"int N = 0;\n"
				"int bump(int x) { N = N + 1; return x; }\n"
				"int pure(int a) { return a + 1; }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { int v = (i % 2 == 0) ? bump(i) : 0; bool ok = (i > 2) && bump(i) > 0; int w = ((i > 100) || bump(i) < 0) ? 1 : 0; s = s + v + w + pure(i) + (ok ? 1 : 0); } return s * 1000 + N; };\n";
			both(prelude + "to_string(run(8));", "53017", "a call under a conditional operand");
			const std::string l = listing_run(prelude + "run(8);");
			check_true(l.find("-- inlined bump") == std::string::npos, "bump under a conditional operand keeps its call: " + l);
			check_true(l.find("-- inlined pure") != std::string::npos, "pure in a plain operand is inlined: " + l);
		});
		test("a_body_writing_through_a_reference_parameter_grafts_only_at_the_statements_front", [this]() {
			// row 153: `s + i + inc(i)` reads i before inc writes it through its var& parameter (a store, or tick's ++ of the parameter's
			// name); the exact hoist spills `s + i` into a typed temp ahead of the body, so all three graft and the values hold
			const std::string prelude =
				"int inc(var& p) { p = p + 1; int r = p; return r; }\n"
				"int inc2(var& p) { p = p + 1; int r = p; return r; }\n"
				"int tick(var& p) { p++; int r = p; return r; }\n"
				"var run = function(int n) { int s = 0; int i = 0; int j = 0; int k = 0; while (i < n) { s = s + i + inc(i); j = j + k + tick(k); int t = inc2(j); s = s + t; } return s * 100 + j + k; };\n";
			both(prelude + "to_string(run(6));", "14848", "a reference write behind a read");
			const std::string l = listing_run(prelude + "run(6);");
			check_true(l.find("-- inlined inc2") != std::string::npos && l.find("-- inlined inc ") != std::string::npos && l.find("-- inlined tick") != std::string::npos, "inc, tick and inc2 are inlined (the reads before them spilled): " + l);
			check_true(l.find("-- cell loop") != std::string::npos, "the loop closes around the three grafts: " + l);
		});
		test("an_increment_of_a_global_in_the_body_is_a_write", [this]() {
			// row 152: `G++` in the body writes G (an identifier's own increment, not a compound store); a statement that reads G before
			// the call keeps it
			const std::string prelude =
				"int G = 0;\n"
				"int nextg() { G++; int r = G; return r; }\n"
				"var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + G + nextg(); } return s; };\n";
			both(prelude + "to_string(run(5));", "25", "an increment of a global in the body");
			const std::string l = listing_run(prelude + "run(5);");
			check_true(l.find("-- inlined nextg") == std::string::npos, "nextg after a read of G keeps its call: " + l);
		});
		test("a_store_the_statement_completes_before_the_call_keeps_it", [this]() {
			// row 152: `(i = i + 1) + twice(i)` stores i before the call reads it, `(G = G + 1) + readg()` stores G before the body reads it;
			// a graft would run the arguments and the body first, so both statements keep their calls; the declaration after them grafts
			const std::string prelude =
				"int G = 0;\n"
				"int twice(int a) { return a * 2; }\n"
				"int twice2(int a) { return a * 2; }\n"
				"int readg() { int r = G; return r; }\n"
				"var run = function(int n) { int s = 0; int i = 0; while (i < n) { s = s + (i = i + 1) + twice(i); s = s + (G = G + 1) + readg(); int d = twice2(i); s = s + d; } return s * 10 + i + G; };\n";
			both(prelude + "to_string(run(4));", "708", "a store before the call");
			const std::string l = listing_run(prelude + "run(4);");
			check_true(l.find("-- inlined twice2") != std::string::npos, "twice2 in a declaration is inlined: " + l);
			check_true(l.find("-- inlined twice ") == std::string::npos && l.find("-- inlined readg") == std::string::npos, "twice and readg behind a store keep their calls: " + l);
		});
		test("a_static_member_read_before_a_body_writing_it_keeps_the_call", [this]() {
			// row 152: `s + Counter::n + bumpc()` reads the static before bumpc writes it; a static member read is a member read to the scan
			const std::string prelude =
				"class Counter { static int n = 0; }\n"
				"int bumpc() { Counter::n = Counter::n + 1; int r = Counter::n; return r; }\n"
				"var run = function(int k) { int s = 0; for (int i = 0; i < k; ++i) { s = s + Counter::n + bumpc(); } return s; };\n";
			both(prelude + "to_string(run(5));", "25", "a static read before a body writing it");
			const std::string l = listing_run(prelude + "run(5);");
			check_true(l.find("-- inlined bumpc") == std::string::npos, "bumpc after a read of the static keeps its call: " + l);
		});
		test("every_position_takes_the_same_graft", [this]() {
			// row 153: the call becomes a temp, so an operand, a declaration, an assignment, an if condition, a while condition, a for condition
			// and update, an argument of another leaf and an argument of a host function are one position; the values on both backends
			const std::string prelude =
				"int add1(int a) { return a + 1; }\n"
				"int twice(int a) { return a * 2; }\n"
				"var run = function(int n) { int s = 0; int sum_d = 0; int sum_t = 0; int t = 0; int c1 = 0; int sum_e = 0; string acc = \"\";\n"
				"  for (int i = 0; i < n; ++i) { s = s + add1(i); int d = add1(i); sum_d = sum_d + d; t = add1(i); sum_t = sum_t + t; if (add1(i) > 3) { c1 = c1 + 1; } int e = twice(add1(i)); sum_e = sum_e + e; acc = acc + to_string(add1(i)); }\n"
				"  int w = 0; while (add1(w) < 4) { w = w + 1; }\n"
				"  int c2 = 0; for (int k = 0; add1(k) < 4; k = add1(k)) { c2 = c2 + 1; }\n"
				"  return to_string(s) + \",\" + to_string(sum_d) + \",\" + to_string(sum_t) + \",\" + to_string(c1) + \",\" + to_string(w) + \",\" + to_string(c2) + \",\" + to_string(sum_e) + \",\" + acc; };\n";
			both(prelude + "run(5);", "15,15,15,2,3,3,30,12345", "every position");
			both(prelude + "run(5);", "15,15,15,2,3,3,30,12345", "every position, proofs off", false);
			const std::string l = listing_run(prelude + "run(5);");
			size_t grafts = 0; for (size_t at = l.find("-- inlined add1"); at != std::string::npos; at = l.find("-- inlined add1", at + 1)) { ++grafts; }
			check_true(grafts >= 9, "add1 grafts at every position (" + std::to_string(grafts) + "): " + l);
			check_true(l.find("-- inlined twice") != std::string::npos, "twice over the hoisted add1 is inlined: " + l);
		});
		test("a_spilled_operand_raises_before_the_body", [this]() {
			// row 153: `A[i] + div(1, i - 9)` reads the element before the call; the graft spills the read into a typed temp ahead of the body, so
			// with i out of range the element's raise comes first, as the interpreter's does, never the body's division
			const std::string prelude =
				"array<int> A = [1, 2, 3];\n"
				"int div(int a, int b) { return a / b; }\n"
				"var run = function(int i) { int s = A[i] + div(1, i - 9); return s; };\n";
			both(prelude + "to_string(run(1));", "2", "a spilled element read, in range");
			both(prelude + "to_string(run(9));", "", "a spilled element read, out of range");
			const std::string vm = run_on(true, prelude + "to_string(run(9));");
			check_true(vm.rfind("throw:", 0) == 0 && vm.find("ivision") == std::string::npos, "the element's raise comes first: " + vm);
			const std::string l = listing_run(prelude + "run(1);");
			check_true(l.find("-- inlined div") != std::string::npos, "div behind a spilled read is inlined: " + l);
		});
		test("returns_in_many_arms_jump_to_one_join", [this]() {
			// row 153: a body with returns in six arms and one inside a nested block grafts (each return declares the result and jumps to the join;
			// the rests are never cloned), on both backends, proofs on and off
			const std::string prelude =
				"int tone(int kind, int tx) {\n"
				"  if (kind == 1) { int t = tx & 1; if (tx > 4) { t = 2; } return t; }\n"
				"  if (kind == 2) { int t = (tx >> 1) & 1; return t; }\n"
				"  if (kind == 3) { return 3; }\n"
				"  if (kind == 4) { { int q = tx * 2; return q; } }\n"
				"  if (tx < 3) { return 7; }\n"
				"  return 0;\n"
				"}\n"
				"var run = function(int n) { int s = 0; for (int k = 0; k <= 5; ++k) { for (int tx = 0; tx < n; ++tx) { s = s + tone(k, tx); } } return s; };\n";
			both(prelude + "to_string(run(6));", "96", "returns in many arms");
			both(prelude + "to_string(run(6));", "96", "returns in many arms, proofs off", false);
			const std::string l = listing_run(prelude + "run(6);");
			check_true(l.find("-- inlined tone") != std::string::npos, "tone is inlined through the join: " + l);
		});
		test("an_unproven_return_converts_with_the_returns_text", [this]() {
			// row 153: `return cells[i]` from a var container has no proof; the graft declares the result with the return site, so a string
			// element raises the callee's own text on both backends, and an int element is the value
			const std::string prelude =
				"var cells = [1, 2, \"x\"];\n"
				"int cell_at(int i) { return cells[i]; }\n"
				"var run = function(int i) { int v = cell_at(i) + 1; return v; };\n";
			both(prelude + "to_string(run(1));", "3", "an unproven return, an int element");
			both(prelude + "to_string(run(2));", "", "an unproven return, a string element");
			const std::string vm = run_on(true, prelude + "to_string(run(2));");
			check_true(vm.find("Cannot return") != std::string::npos && vm.find("cell_at") != std::string::npos, "the return's text: " + vm);
			const std::string l = listing_run(prelude + "run(1);");
			check_true(l.find("-- inlined cell_at") != std::string::npos, "cell_at is inlined with the return site: " + l);
		});
		test("a_hoisted_condition_takes_the_fallback_after_a_reload", [this]() {
			// row 153: a leaf in an if condition grafts ahead of the condition's jumps; a reload of the callee fails the guard and the fallback
			// declares the real call's result into the same temp, so the condition reads the new body's value
			const std::string prelude =
				"int lim(int a) { return a + 10; }\n"
				"var run = function(int n) { int c = 0; for (int i = 0; i < n; ++i) { if (lim(i) > 12) { c = c + 1; } } return c; };\n";
			auto sequence = [&](bool vm) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				try {
					e->execute(prelude);
					const std::string a = e->execute("run(5);").to_string();
					e->execute("int lim(int a) { return a - 10; }");
					const std::string b = e->execute("run(5);").to_string();
					return a + "|" + b;
				} catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			const std::string iv = sequence(false), vv = sequence(true);
			check_eq(iv, vv, "the reload takes the fallback on the vm");
			check_eq(std::string("2|0"), vv, "the condition before and after the reload");
			const std::string l = listing_run(prelude + "run(5);");
			check_true(l.find("-- inlined lim") != std::string::npos, "lim in the condition is inlined: " + l);
		});
		test("a_call_behind_an_effect_stays_a_call", [this]() {
			// row 153: the first call a statement runs is the one hoisted; a method call or a host call first keeps every later leaf a call
			// (it would run behind an effect), while a leaf first grafts and the effect follows
			const std::string prelude =
				"int add1(int a) { return a + 1; }\n"
				"int add2(int a) { return a + 2; }\n"
				"var run = function(int n) { var a = [1, 2]; int s = 0; for (int i = 0; i < n; ++i) { s = s + a.size() + add1(i); s = s + add2(i) + a.size(); } return s; };\n";
			both(prelude + "to_string(run(4));", "40", "a call behind an effect");
			const std::string l = listing_run(prelude + "run(4);");
			check_true(l.find("-- inlined add1") == std::string::npos, "add1 behind the method call keeps its call: " + l);
			check_true(l.find("-- inlined add2") != std::string::npos, "add2 ahead of the method call is inlined: " + l);
		});
		test("a_leaf_another_unit_declares_grafts", [this]() {
			// row 153.1: tile_at lives in maps.jai and spot_free in sim.jai; a leaf a unit imports resolves through the program's function table
			// (the closure's cached unit, the one the import executes), so it grafts in the importer's loop and the guard reads the live binding
			namespace fs = std::filesystem;
			fs::path dir = fs::temp_directory_path() / "jai_row1531";
			fs::create_directories(dir);
			std::ofstream(dir / "lib.jai") << "int add1(int a) { return a + 1; }\nclass Grid { int mw = 4; var cells = []; }\nshared_ptr<Grid> GR = null;\nint cell_at(int x, int y) { if (x < 0 || x >= GR.mw) { return 1; } return GR.cells[y * GR.mw + x]; }\n";
			std::ofstream(dir / "main.jai") << "import \"lib.jai\";\nGR = new Grid(); for (int k = 0; k < 16; ++k) { GR.cells.push(k % 5 == 0 ? 1 : 0); }\n"
			                                  "var run = function(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + add1(i); } return s; };\n"
			                                  "var probe = function(int x) { if (cell_at(x, 1) != 0) { return 7; } return add1(x); };\n"
			                                  "to_string(run(6)) + \"|\" + to_string(probe(0)) + \"|\" + to_string(probe(1));\n";
			auto run_file = [&](bool vm, std::string* listing) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				if (listing) { e->disassembly_sink([listing](const std::string& l, const std::string&) { *listing += l; *listing += "\n"; }); }
				e->add_include_path(dir.string());
				try { return e->execute_file((dir / "main.jai").string()).as_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			std::string l;
			const std::string iv = run_file(false, nullptr), vv = run_file(true, &l);
			check_eq(iv, vv, "a leaf another unit declares: both backends");
			check_eq(std::string("21|1|7"), vv, "the values");
			check_true(l.find("-- inlined add1") != std::string::npos, "add1 from the imported unit is inlined: " + l);
			check_true(l.find("-- inlined cell_at") != std::string::npos, "cell_at from the imported unit is inlined in a condition (its return converts at the result): " + l);
		});
		test("a_global_another_unit_declares_types_the_proofs", [this]() {
			// row 154.1: ND_X in JaiDOOM's map.jai and G in GLOOM's game.jai; a top-level global one unit declares with a spelled type is typed
			// by that declaration in every other unit's proof pass (never for its diagnostics), a class-typed one once its class is defined
			// (the importer's proofs re-stamp after the import; its callers are named functions, compiled at their first call after it), so `at`
			// and `span` graft inside their loops on both accounts
			namespace fs = std::filesystem;
			fs::path dir = fs::temp_directory_path() / "jai_row1541";
			fs::create_directories(dir);
			std::ofstream(dir / "data.jai") << "array<int> ND = [3, 1, 4, 1, 5, 9, 2, 6];\nclass World { int mw = 8; float px = 2.5; var tiles = []; }\nshared_ptr<World> G = null;\n";
			std::ofstream(dir / "lib.jai") << "int at(int i) { return ND[i]; }\nfloat span(float a) { return a - G.px; }\nint width() { return G.mw; }\nvoid never() { int bad = ND; }\n"
			                                 "int sum(int n) { int s = 0; for (int i = 0; i < n; ++i) { s = s + at(i); } return s; }\n"
			                                 "float walk(int n) { float acc = 0.0; for (int i = 0; i < n; ++i) { acc = acc + span(i * 1.0); } return acc; }\n";
			std::ofstream(dir / "main.jai") << "import \"lib.jai\";\nimport \"data.jai\";\nG = new World();\nto_string(sum(8)) + \"|\" + to_string(walk(4)) + \"|\" + to_string(width());\n";
			auto run_file = [&](bool vm, std::string* listing) -> std::string {
				auto e = engine::make(); if (vm) { e->set_backend(backend_type::vm); } stdlib::register_all(*e);
				if (listing) { e->disassembly_sink([listing](const std::string& l, const std::string&) { *listing += l; *listing += "\n"; }); }
				e->add_include_path(dir.string());
				try { return e->execute_file((dir / "main.jai").string()).as_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
			};
			std::string l;
			const std::string iv = run_file(false, nullptr), vv = run_file(true, &l);
			check_eq(iv, vv, "a global another unit declares: both backends");
			check_eq(std::string("31|-4.0|8"), vv, "the values (and `int bad = ND` in a body never run is no load error: the diagnostics never read the table)");
			check_true(l.find("-- inlined at") != std::string::npos, "at grafts in the loop: its return reads the typed global's element: " + l);
			check_true(l.find("-- inlined span") != std::string::npos, "span grafts in the loop: its return reads the class-typed global's field after the re-stamp: " + l);
			check_true(l.find("-- call kept at ") == std::string::npos, "no kept call for at: " + l);
		});
		test("a_particle_step_through_an_alias_grafts_with_its_ternary", [this]() {
			// row 153.2: gloom_particle_step's shape, a var& parameter bound to a range-for alias, member reads and stores through it and the
			// ternary store `life = life < 4 ? life : 4`, which lands as the condition's jumps and one store per arm; the leaf grafts inside the
			// range-for and the values hold on both backends, proofs on and off
			const std::string prelude =
				"class P { int kind = 1; float x = 0.0; float vx = 0.1; int life = 10; }\n"
				"void step(var& p) { int kind = p.kind; if (kind == 0) { return; } float x = p.x; float vx = p.vx; int life = p.life; life = life - 1;\n"
				"  if (life <= 0) { p.kind = 0; p.x = 0.0; p.life = 0; return; } if (kind == 1) { vx = vx * 0.9; } else { vx = vx * 0.5; } x = x + vx * 0.5;\n"
				"  if (x > 2.0) { x = 2.0; if (kind == 1 || kind == 2) { life = life < 4 ? life : 4; } } p.x = x; p.vx = vx; p.life = life; }\n"
				"var pool = []; for (int i = 0; i < 8; ++i) { auto q = P(); q.kind = i % 3; q.x = i * 0.3; pool.push(q); }\n"
				"var tick = function(int n) { float acc = 0.0; for (int t = 0; t < n; ++t) { for (auto& p : pool) { step(p); } } for (auto& p : pool) { acc = acc + p.x + p.life; } return acc; };\n";
			both(prelude + "to_string(tick(12));", "32.7", "a particle step through an alias");
			both(prelude + "to_string(tick(12));", "32.7", "a particle step through an alias, proofs off", false);
			const std::string l = listing_run(prelude + "tick(12);");
			check_true(l.find("-- inlined step") != std::string::npos, "step grafts inside the range-for with its ternary store: " + l);
		});
		test("the_listing_names_each_kept_call_with_its_rule", [this]() {
			// row 154.0: a call the inliner keeps is named in the listing with the rule that kept it: a var local into an int parameter (an
			// unproven argument), a leaf as a method call's argument, and a leaf returning a var element inside a loop (an unproven return)
			const std::string l = listing_run(
				"int add1(int a) { return a + 1; }\n"
				"var G = [3, 4, 5];\n"
				"int at(int i) { return G[i]; }\n"
				"int run() { var v = 3; int s = add1(v); var acc = []; acc.push(add1(2)); for (int i = 0; i < 3; ++i) { s = s + at(i); } return s; }\n"
				"run();");
			check_true(l.find("-- call kept add1 line 4: an unproven argument") != std::string::npos, "an unproven argument keeps the call: " + l);
			check_true(l.find("-- call kept add1 line 4: a method call's argument") != std::string::npos, "a method call's argument keeps the call: " + l);
			check_true(l.find("-- call kept at line 4: an unproven return in a loop") != std::string::npos, "an unproven return in a loop keeps the call: " + l);
		});
		test("a_graft_after_a_continue_closes_the_loop_without_a_bail", [this]() {
			// row 152: draw_seg's shape, `if (done[x] != 0) { continue; }` then a leaf call, then a statement whose float temp takes a slot the
			// graft declared as an int; the graft declares no default for its result (a bare DECL_VAR is no block instruction, and inside the
			// conditional span after the continue it made the graft a planned bail) and its declarations are compiler values to the walk (a
			// later temp may take their slots with another kind), so the loop's copy holds the graft and runs it as a block
			const std::string prelude =
				"array<int> done = [0, 1, 0, 0, 1, 0, 0, 0];\n"
				"int light_band(int l, int d) { int b = l - d / 8; if (b < 0) { return 0; } if (b > 31) { return 31; } return b; }\n"
				"var run = function(int n) { int s = 0; for (int x = 0; x < n; ++x) { if (done[x] != 0) { continue; } int band = light_band(x * 9, x); s = s + band; int t2 = math::itrunc(band * 0.5); s = s + t2; } return s; };\n";
			both(prelude + "to_string(run(8));", "205", "a graft after a continue");
			const std::string l = listing_run(prelude + "run(8);");
			check_true(l.find("-- inlined light_band") != std::string::npos, "light_band is inlined: " + l);
			check_true(l.find("-- cell loop") != std::string::npos, "the loop closes: " + l);
			check_true(l.find("bail->") == std::string::npos && l.find("C_BAIL") == std::string::npos, "no bail inside the loop: " + l);
		});
		test("a_callee_with_a_loop_grafts_into_a_column_loop", [this]() {
			// row 159: draw_seg's column shape, a callee whose body is a loop with a continue and a break, taking its texture and the frame
			// buffer as typed references bound to the caller's aliases: both calls graft, the grafted loops and the column loop close
			const std::string prelude =
				"array<int> PIX = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];\n"
				"array<array<int>> TEX = [[1, 2, 3, 4], [5, 6, 7, 8]];\n"
				"void col(int x, int y0, int y1, array<int>& tp, int k, array<int>& pix) {\n"
				"\tif (y0 < 0) { y0 = 0; }\n"
				"\tfor (int y = y0; y <= y1; ++y) {\n"
				"\t\tif (y == 2) { continue; }\n"
				"\t\tif (y > 5) { break; }\n"
				"\t\tpix[y * 2 + x] = tp[(y + k) & 3] + x;\n"
				"\t}\n"
				"}\n"
				"int run() {\n"
				"\tarray<int>& pix = PIX;\n"
				"\tarray<int>& t0 = TEX[0];\n"
				"\tarray<int>& t1 = TEX[1];\n"
				"\tint s = 0;\n"
				"\tfor (int x = 0; x < 2; ++x) {\n"
				"\t\tif (x == 0) { col(x, -1, 7, t0, 1, pix); } else { col(x, 0, 7, t1, 2, pix); }\n"
				"\t\ts = s + x;\n"
				"\t}\n"
				"\tfor (int i = 0; i < 16; ++i) { s = s * 3 + PIX[i]; s = s & 1048575; }\n"
				"\treturn s;\n"
				"}\n";
			both(prelude + "to_string(run());", "113835", "a callee with a loop grafted into a column loop");
			const std::string l = listing_run(prelude + "run();");
			size_t grafts = 0; for (size_t at = l.find("-- inlined col"); at != std::string::npos; at = l.find("-- inlined col", at + 1)) { ++grafts; }
			size_t loops = 0; for (size_t at = l.find("-- cell loop"); at != std::string::npos; at = l.find("-- cell loop", at + 1)) { ++loops; }
			check_eq(size_t(2), grafts, "both calls graft: " + l);
			check_eq(size_t(4), loops, "the grafted loops, the column loop and the fold loop close: " + l);
		});
		test("a_return_inside_a_callee_loop_keeps_the_call", [this]() {
			// row 159: a return from inside the callee's own loop would leave the loop's state behind the graft's join, so the call stays
			const std::string prelude =
				"int find(array<int>& a, int v) { for (int i = 0; i < 4; ++i) { if (a[i] == v) { return i; } } return -1; }\n"
				"int run() { array<int> a = [4, 5, 6, 7]; int s = 0; for (int k = 0; k < 8; ++k) { s = s + find(a, k); } return s; }\n";
			both(prelude + "to_string(run());", "2", "a return inside a callee loop");
			const std::string l = listing_run(prelude + "run();");
			check_true(l.find("-- call kept find line 2: a return inside a loop") != std::string::npos, "the call stays with its rule: " + l);
		});
		test("a_typed_reference_parameter_grafts_as_the_callers_variable", [this]() {
			// row 159: a reference parameter's declared type binds nothing and converts nothing at run time (a store through it follows the
			// referent), so an int& bound to a float local is that local: 3.7 lands as a float on both backends, grafted or not
			const std::string prelude =
				"void set37(int& a) { a = 3.7; }\n"
				"float run() { float u = 1.5; for (int k = 0; k < 2; ++k) { set37(u); } return u; }\n";
			both(prelude + "to_string(run());", "3.7", "a store through a typed reference parameter");
			const std::string l = listing_run(prelude + "run();");
			check_true(l.find("-- inlined set37") != std::string::npos, "the typed reference parameter grafts: " + l);
		});
		test("a_reference_argument_that_is_not_a_local_keeps_the_call", [this]() {
			// row 159: a var& or typed reference parameter bound to a field or a method name binds through the caller's member rules;
			// the call stays, so a method name raises the binder's text on both backends
			const std::string prelude =
				"void by_ref(var& p) { p = 1; }\n"
				"void by_int(int& p) { p = 2; }\n"
				"class W { int fld = 5; int f() { return 1; } int fields() { by_ref(fld); int a = fld; by_int(fld); return a * 10 + fld; } void named() { by_ref(f); } }\n"
				"W w = W();\n";
			both(prelude + "to_string(w.fields());", "12", "a field bound by reference");
			both(prelude + "var r = \"\"; try { w.named(); } catch (e) { r = e; } r;", "Cannot take reference of undefined variable", "a method name bound by reference");
			const std::string l = listing_run(prelude + "w.fields();");
			check_true(l.find("-- call kept by_ref line 3: a reference argument that is not a local") != std::string::npos, "the call stays with its rule: " + l);
		});
		test("a_loop_of_more_than_128_cells_closes", [this]() {
			// row 159: the cell mask holds 256 cells; a loop over 140 locals runs as one block
			std::string src = "int run() { ";
			for (int i = 0; i < 140; ++i) { src += "int v" + std::to_string(i) + " = " + std::to_string(i) + "; "; }
			src += "\n\tfor (int k = 0; k < 3; ++k) { ";
			for (int i = 0; i < 140; ++i) { src += "v" + std::to_string(i) + " = v" + std::to_string(i) + " + " + std::to_string(i % 5 + 1) + "; "; }
			src += "}\n\treturn ";
			for (int i = 0; i < 140; ++i) { src += (i ? " + v" : "v") + std::to_string(i); }
			src += "; }\n";
			both(src + "to_string(run());", "10990", "a loop of 140 cells");
			const std::string l = listing_run(src + "run();");
			check_true(l.find("-- cell loop") != std::string::npos, "the loop closes: " + l.substr(0, 400));
		});
		test("a_miss_inside_the_argument_zone_rebuilds_every_argument", [this]() {
			// an element argument the block landed, then a later element argument the cells decline (a string): the general form re-runs the
			// argument zone from its first instruction, since argument values are never flushed
			const std::string prelude =
				"var OUT = \"\";\n"
				"function g2(int a, string g) -> void { OUT = OUT + to_string(a) + g + \";\"; }\n"
				"function g3(int a, int b, string g) -> void { OUT = OUT + to_string(a + b) + g + \";\"; }\n"
				"var LM = [[26, \"a\"], [22, \"b\"], [7, \"c\"]];\n"
				"var LN = [[26, 5, \"a\"], [22, 6, \"b\"], [1, 2, \"c\"]];\n"
				"var A = [26, \"a\", 22, \"b\", 3, \"c\"];\n"
				"function by_ref() -> void { for (auto& lm : LM) { g2(lm[0], lm[1]); } }\n"
				"function three() -> void { for (auto& ln : LN) { g3(ln[0], ln[1], ln[2]); } }\n"
				"function flat() -> void { for (int i = 0; i < 6; i += 2) { g2(A[i], A[i + 1]); } }\n";
			both(prelude + "by_ref(); three(); flat(); OUT;", "26a;22b;7c;31a;28b;3c;26a;22b;3c;", "element arguments with a declined second read");
			both(prelude + "by_ref(); three(); flat(); OUT;", "26a;22b;7c;31a;28b;3c;26a;22b;3c;", "element arguments, proofs off", false);
		});
		test("a_range_for_variable_binds_each_element_as_it_is", [this]() {
			// each iteration declares the loop variable afresh: an int after a float stays an int, a float after an int stays a float
			const std::string src =
				"var out = \"\";\n"
				"function g(a) { out = out + a + \";\"; }\n"
				"function d(var xs) { for (auto e : xs) { g(e); } for (var e : xs) { g(e); } }\n"
				"d([1, 7.5, 1]); d([true, 3.0, 1, 2.5, -3]); out;";
			both(src, "1;7.5;1;1;7.5;1;true;3.0;1;2.5;-3;true;3.0;1;2.5;-3;", "range-for kinds");
			both(src, "1;7.5;1;1;7.5;1;true;3.0;1;2.5;-3;true;3.0;1;2.5;-3;", "range-for kinds, proofs off", false);
		});
		test("a_constant_stored_into_a_var_element_belongs_to_the_engine", [this]() {
			// the stored element copies deep afterwards (slice clones each element), which needs its engine
			const std::string src =
				"var v7 = [4, 5, 12]; v7[1] = 11;\n"
				"function f() -> string { var v = [4, 5, 12]; v[1] = 11; return to_json(v.slice(0, 2)); }\n"
				"function g() -> string { var v = [0, 0, 0, 0]; for (int i = 0; i < 4; ++i) { v[i] = 7; v[i] = 2.5; } return to_json(v.slice(1, 3)); }\n"
				"to_json(v7.slice(0, 2)) + f() + g();";
			both(src, "[4,11][4,11][2.5,2.5]", "constant element stores");
			both(src, "[4,11][4,11][2.5,2.5]", "constant element stores, proofs off", false);
		});
		test("a_computed_index_beside_a_lifted_partner_compiles_without_typed_lowering", [this]() {
			const std::string src = "var A = [1, 2, 3, 4];\nfunction d() -> int { int i = 0; return (i + A[i]) + A[i + 2]; }\nto_string(d());";
			both(src, "4", "computed index, proofs off", false);
			both(src, "4", "computed index");
		});
		test("a_bail_inside_a_statement_flushes_its_spills_and_graft_values", [this]() {
			// the first itr grafts behind the spill of m[i], the second stays a call (its left operand has no class to spill) and the statement
			// bails inside the if after the graft's join; the spill and the graft's result live only in cells, so the bail flushes them
			const std::string prelude =
				"function itr(float v) -> int { return math::itrunc(v); }\n"
				"var q = 4;\n"
				"function run() -> string { array<int> m = [10, 20, 30, 40]; string out = \"\";\n"
				"  for (int i = 0; i < 4; ++i) { float d = i * 1.5; if (d > 0.5) { int c = m[i] + itr(d) * q + itr(d); out += to_string(c) + \" \"; } }\n"
				"  return out; }\n";
			both(prelude + "run();", "25 45 60 ", "a statement bailing after its graft");
			both(prelude + "run();", "25 45 60 ", "a statement bailing after its graft, proofs off", false);
			const std::string l = listing_run(prelude + "run();");
			check_true(l.find("-- inlined itr") != std::string::npos && l.find("bail->") != std::string::npos, "the graft and the bail are in the loop: " + l.substr(0, 400));
		});

		// row 187: a bare call to a stdlib math builtin lowers to the math temp while the name binds the registered function
		test("stdlib_math_calls_lower_to_math_temps", [this]() {
			const std::string src = "function f(float x, float y) -> float { float a = sqrt(x * x + y * y); float b = sin(x) + cos(y); return a + b + floor(y * 0.5) + ceil(x) + tan(0.25) + degrees(radians(x)); }\n"
				"to_string(f(3.0, 4.0)) + \",\" + to_string(f(1, 2));";
			both(src, "", "the aliases");
			check_true(run_on(true, src).rfind("throw:", 0) != 0, "runs: " + run_on(true, src));
			both(src, "", "the aliases, proofs off", false);
			const std::string all = listing_run(src);
			const size_t fbeg = all.find("== f ");
			const std::string l = fbeg == std::string::npos ? all : all.substr(fbeg);
			size_t temps = 0, guards = 0;
			for (size_t at = l.find("MATH_TEMP"); at != std::string::npos; at = l.find("MATH_TEMP", at + 9)) { ++temps; }
			for (size_t at = l.find("MATH_GUARD"); at != std::string::npos; at = l.find("MATH_GUARD", at + 10)) { ++guards; }
			check_true(temps == 8 && guards >= 3, "every alias lands in a math temp, a computed argument behind a guard: " + l);
			check_true(l.find("PROBE_CALLEE") == std::string::npos, "no alias call is a probe: " + l);
		});
		test("stdlib_math_alias_numbers_match_the_builtin", [this]() {
			const std::string src = "function r(var v) -> string { return to_string(sqrt(v)) + \":\" + type_of(sqrt(v)) + \" \" + to_string(floor(v)) + \" \" + to_string(sin(v)); }\n"
				"function k(int i) -> string { return to_string(sqrt(i)) + \" \" + to_string(cos(i)); }\n"
				"r(4) + \"|\" + r(2.25) + \"|\" + r(-1.5) + \"|\" + r(9007199254740993) + \"|\" + k(7) + \"|\" + k(-9223372036854775807) + \"|\" + to_string(sqrt(-1.0) == sqrt(-1.0));";
			both(src, "", "int and float arguments");
			check_true(run_on(true, src).rfind("2.0:float 4.0 ", 0) == 0, "runs: " + run_on(true, src));
			both(src, "", "int and float arguments, proofs off", false);
		});
		test("stdlib_math_alias_non_numeric_arguments_take_the_builtin", [this]() {
			const std::string src = "class F { float to_float() { return 16.0; } }\n"
				"function g(var v) -> string { try { var r = sqrt(v); return to_string(r); } catch (e) { return \"E:\" + e; } }\n"
				"function h(var v) -> string { try { float r = sin(v * 1) + 1.0; return to_string(r); } catch (e) { return \"E:\" + e; } }\n"
				"g(true) + \"|\" + g(\"x\") + \"|\" + g(null) + \"|\" + g([1]) + \"|\" + g(F()) + \"|\" + h(\"s\") + \"|\" + h(false) + \"|\" + h(2);";
			both(src, "", "non-numeric arguments");
			check_true(run_on(true, src).rfind("E:script_value is not a float or integer. Actual type: 6|E:No viable candidates for function call|", 0) == 0, "the builtin's own texts: " + run_on(true, src));
		});
		test("stdlib_math_alias_follows_a_rebinding", [this]() {
			// rebound between calls, from inside a loop, from inside its own argument (the callee resolved first), to a
			// non-callable and to a function taking its parameter by reference
			const std::string src = "function f(float x) -> float { return sqrt(x * 2.0) + sqrt(x); }\n"
				"function twice(float x) -> float { sqrt = function(float v) { return v + 1000.0; }; return x * 2.0; }\n"
				"function g(float x) -> float { return sqrt(twice(x) + 0.0) + sqrt(x); }\n"
				"function loop() -> string { string s = \"\"; for (int i = 0; i < 4; ++i) { s += to_string(sqrt(i * 4.0)) + \" \"; if (i == 1) { sqrt = function(float v) { return -v; }; } } return s; }\n"
				"string out = to_string(f(8.0));\n"
				"sqrt = function(float v) { return v + 100.0; };\n"
				"out += \",\" + to_string(f(8.0));\n"
				"sqrt = SQRT0;\n"
				"out += \",\" + to_string(g(8.0)) + \",\" + to_string(g(8.0));\n"
				"sqrt = SQRT0;\n"
				"out += \",\" + loop();\n"
				"sqrt = function(float& v) { v = v + 1.0; return v; };\n"
				"function byref(float x) -> string { float y = x; float r = sqrt(y) + 0.0; return to_string(r) + \"/\" + to_string(y); }\n"
				"out += \",\" + byref(2.0);\n"
				"sqrt = 5;\n"
				"function nc(float x) -> string { try { return to_string(sqrt(x * 1.0)); } catch (e) { return \"E:\" + e; } }\n"
				"function nc2(float x) -> string { try { return to_string(sqrt(nope * 1.0)); } catch (e) { return \"E:\" + e; } }\n"
				"function nc3(float x) -> string { try { return to_string(sqrt(x)); } catch (e) { return \"E:\" + e; } }\n"
				"out += \",\" + nc(1.0) + \",\" + nc2(1.0) + \",\" + nc3(1.0);\n"
				"out;";
			const std::string prelude = "[[warn_mode]]\nvar SQRT0 = sqrt;\n";
			const std::string expected = "6.82842712474619,224.0,1012.0,2024.0,0.0 2.0 -8.0 -12.0 ,3.0/3.0,E:Cannot call 'sqrt': it is an int, not a function,"
				"E:Cannot call 'sqrt': it is an int, not a function,E:Cannot call 'sqrt': it is an int, not a function";
			both(prelude + src, expected, "rebinding");
			both(prelude + src, expected, "rebinding, proofs off", false);
		});
		test("stdlib_math_alias_member_named_like_the_builtin", [this]() {
			const std::string src = "class V { float k = 10.0; float sqrt(float x) { return x * k; } float g(float y) { return sqrt(y * 1.0) + sqrt(y) + cos(y * 0.0); } }\n"
				"class W { float cos = 3.0; float g(float y) { try { return cos(y * 2.0); } catch (e) { return -1.0; } } }\n"
				"var v = V(); var w = W(); to_string(v.g(2.0)) + \",\" + to_string(w.g(1.0)) + \",\" + to_string(sqrt(16.0));";
			both(src, "41.0,-1.0,4.0", "a member named like the builtin");
		});
		test("stdlib_math_alias_host_reregistration", [this]() {
			auto run = [](bool vm) {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				std::string out;
				out += e->execute("function f(float x) -> string { return to_string(sqrt(x * 1.0)) + \" \" + to_string(sqrt(x)); } f(16.0);").as_string();
				e->add_function("sqrt", [](const std::string& s) -> script_float { return static_cast<script_float>(s.size()); });
				out += "," + e->execute("f(16.0) + \" \" + to_string(sqrt(\"abc\"));").as_string();
				return out;
			};
			const std::string interp = run(false), vm = run(true);
			check_eq(interp, vm, "a host that registers sqrt again");
			check_eq(std::string("4.0 4.0,4.0 4.0 3.0"), vm, "the overload set answers");
		});
		// row 188: a loop whose body evaluates a stdlib alias closes; the name holds at the entry and after each call's landing
		test("stdlib_math_alias_loops_close_as_cell_blocks", [this]() {
			const std::string src = "array<float> XS = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5];\n"
				"function run(int n) -> float { float s = 0.0; for (int i = 0; i < n; ++i) { float x = XS[i & 7]; float d = sqrt(x * x + 1.0) - 0.5; s += d + sin(x) + floor(x); } return s; }\n"
				"to_string(run(64)) + \",\" + to_string(run(3));";
			both(src, "", "an alias loop");
			both(src, "", "an alias loop, proofs off", false);
			const std::string l = listing_run(src);
			const size_t fbeg = l.find("== run ");
			const std::string fl = fbeg == std::string::npos ? l : l.substr(fbeg, l.find("\n== ", fbeg + 1) - fbeg);
			check_true(fl.find("open loop") == std::string::npos && fl.find("C_MATH") != std::string::npos, "the loop closes over the aliases: " + fl);
			if (!engine::miss_injection_available()) { return; }
			const std::string oracle = run_on(false, src);
			for (uint32_t every : { 1u, 2u, 3u, 5u, 7u }) {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				std::string got;
				try { got = e->execute(src).as_string(); } catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_eq(oracle, got, "a miss at every " + std::to_string(every) + " re-runs the alias from its guard's mark");
			}
		});
		test("stdlib_math_alias_loop_follows_a_rebinding_by_a_call", [this]() {
			// a call in the body rebinds the name: the landing's check misses and the general form makes the real call from then on;
			// a call between an alias's guard and its op keeps the loop open
			const std::string src = "var SQRT0 = sqrt;\n"
				"function rb(int i) -> int { if (i == 5) { sqrt = function(float v) { return -v; }; } return i; }\n"
				"function run() -> float { float s = 0.0; for (int i = 0; i < 12; ++i) { float x = i * 1.5; s += sqrt(x * x + 1.0); int k = rb(i); s += k; } return s; }\n"
				"function ab(float x) -> float { return x * 2.0; }\n"
				"function run2() -> float { float s = 0.0; for (int i = 0; i < 12; ++i) { float x = i * 0.25; s += sqrt(ab(x) + 1.0); } return s; }\n"
				"string a = to_string(run()); sqrt = SQRT0; a += \",\" + to_string(run2()); sqrt = function(float v) { return v * 10.0; }; a + \",\" + to_string(run2());";
			both(src, "", "a rebinding call inside the loop");
			both(src, "", "a rebinding call inside the loop, proofs off", false);
		});
		// row 189: a typed global in arithmetic or a declaration is a typed row's environment operand
		test("typed_globals_are_typed_row_operands", [this]() {
			const std::string src = "float G = 1.5; float H = 2.0; int N = 7;\n"
				"function f(float x, int k) -> float { float a = x - G; float b = a * H + G; float s = G; int m = k * N + N; return b + s + m; }\n"
				"string out = to_string(f(3.0, 2)); G = 4.25; N = -3; out += \",\" + to_string(f(1.0, 5)); out;";
			both(src, "", "typed globals");
			both(src, "", "typed globals, proofs off", false);
			const std::string all = listing_run(src);
			const size_t fbeg = all.find("== f ");
			const std::string l = fbeg == std::string::npos ? all : all.substr(fbeg, all.find("\n== ", fbeg + 1) - fbeg);
			check_true(l.find("BINARY_FUSED_") == std::string::npos && l.find("DECL_VAR") == std::string::npos && l.find("T_DECL_MOV") != std::string::npos, "the global reads are typed rows: " + l);
		});
		test("typed_global_rows_fall_back_to_the_general_form", [this]() {
			// a global read before its declaration ran (the typed null placeholder), a member of the global's name inside a method, a
			// global another unit redeclares with another kind, a loop over the rows; the same values and texts on both backends
			const std::string src = "[[warn_mode]]\nfloat G = 1.5;\n"
				"function f(float x) -> float { float a = x - G; float s = G; return a * s; }\n"
				"float E = 0.0;\n"
				"class C { float G = 10.0; float g(float x) { float a = x - G; float s = G; return a + s; } }\n"
				"function loop(int n) -> float { float t = 0.0; for (int i = 0; i < n; ++i) { t += i * G - G; float w = G; t += w; } return t; }\n"
				"string out = to_string(f(3.0)) + \",\" + to_string(C().g(4.0)) + \",\" + to_string(loop(40));\n"
				"G = 0.25; out += \",\" + to_string(f(3.0)) + \",\" + to_string(loop(7)); out;";
			both(src, "", "typed global fallbacks");
			both(src, "", "typed global fallbacks, proofs off", false);
			const std::string pre = "float P = f0();\nfunction f0() -> float { return g0(2.0); }\nfunction g0(float x) -> float { try { float a = x - Q; return a; } catch (e) { return -1.0; } }\nfloat Q = 3.0;\n"
				"function h(float x) -> float { float a = x * P; return a; }\nto_string(P) + \",\" + to_string(h(2.0));";
			both(pre, "", "a global read ahead of its declaration");
		});
		// a free function grafted into a method reads its globals as the callee does: a field of the caller's class never shadows them
		test("graft_in_a_method_reads_the_callee_globals", [this]() {
			const std::string src = "[[warn_mode]]\nfloat G = 1.5;\nfunction f(float x) -> float { return x * G; }\n"
				"class C { float G = 10.0; float m(float y) { float r = f(y) + 1.0; return r; } float n(float y) { float t = 0.0; for (int i = 0; i < 6; ++i) { t += f(y + i); } return t; } }\n"
				"function top(float y) -> float { float r = f(y) + 1.0; return r; }\n"
				"var c = C(); to_string(c.m(2.0)) + \",\" + to_string(c.n(1.0)) + \",\" + to_string(top(2.0));";
			both(src, "4.0,31.5,4.0", "a graft in a method");
			both(src, "4.0,31.5,4.0", "a graft in a method, proofs off", false);
		});
		// row 190: a callee whose only calls are stdlib aliases or math:: intrinsics grafts; the alias keeps its guard inside the graft
		test("stdlib_math_alias_callees_graft", [this]() {
			const std::string src = "float K = 2.0;\n"
				"function len3(float x, float y, float z) -> float { float d = sqrt(x * x + y * y + z * z); return d; }\n"
				"function len3r(float x, float y, float z) -> float { return sqrt(x * x + y * y + z * z); }\n"
				"function wav(float x) -> float { float s = sin(x * K); float c = cos(x); float r = s * c + floor(x) + math::sqrt(x * x + 1.0); return r; }\n"
				"function run(int n) -> float { float t = 0.0; for (int i = 0; i < n; ++i) { float x = i * 0.25; t += len3(x, 1.0, 2.0); float w = wav(x); t += w; } return t; }\n"
				"function one(float a) -> float { float d = len3r(a, a, a) - 1.0; return d; }\n"
				"to_string(run(40)) + \",\" + to_string(one(3.0));";
			both(src, "", "alias callees");
			both(src, "", "alias callees, proofs off", false);
			const std::string l = listing_run(src);
			check_true(l.find("-- inlined len3 ") != std::string::npos && l.find("-- inlined wav") != std::string::npos && l.find("-- inlined len3r") != std::string::npos, "the alias callees graft: " + l.substr(0, 3000));
		});
		test("stdlib_math_alias_graft_follows_rebinding_and_members", [this]() {
			// the alias inside a graft rebound to a function that writes a global the statement reads; a caller method whose class has a
			// member named like the alias; the alias's value on the stack (a return, an argument, a compound's right side)
			const std::string src = "[[warn_mode]]\nvar SQRT0 = sqrt;\nfloat G = 1.0;\n"
				"function len2(float x, float y) -> float { float d = sqrt(x * x + y * y); return d; }\n"
				"function st(float a) -> float { float r = G + len2(a, 1.0); return r; }\n"
				"function lp() -> float { float t = 0.0; for (int i = 0; i < 5; ++i) { t += len2(i, 2.0); } return t; }\n"
				"function stackers(float a) -> string { float s = 0.0; s += sqrt(a); return to_string(sqrt(a * 4.0)) + \" \" + to_string(max(sqrt(a), 0.5)) + \" \" + to_string(s); }\n"
				"class M { float sqrt(float v) { return v * 100.0; } float m(float a) { float r = len2(a, 0.0) + 0.0; return r; } }\n"
				"string out = to_string(st(3.0)) + \",\" + to_string(lp()) + \",\" + stackers(9.0) + \",\" + to_string(M().m(4.0));\n"
				"sqrt = function(float v) { G = G + 10.0; return v; };\n"
				"out += \",\" + to_string(st(3.0)) + \",\" + to_string(lp()) + \",\" + stackers(9.0) + \",\" + to_string(M().m(4.0)) + \",\" + to_string(G);\n"
				"function nc() -> string { try { return to_string(len2(1.0, 1.0)); } catch (e) { return \"E:\" + e; } }\n"
				"function nc2() -> string { try { return to_string(sqrt(4.0)); } catch (e) { return \"E:\" + e; } }\n"
				"out + \",\" + nc() + \",\" + nc2();";
			both(src, "", "alias grafts under rebinding");
			both(src, "", "alias grafts under rebinding, proofs off", false);
			check_true(run_on(true, src).find("throw:") == std::string::npos, "runs: " + run_on(true, src));
		});
		// row 191: the entry reads an environment container through its armed lookup; a field of the name, a rebound or replaced global,
		// and a reference binding keep the full resolution
		test("block_entry_environment_containers", [this]() {
			const std::string src = "[[warn_mode]]\narray<float> A = [1.0, 2.0, 3.0, 4.0];\narray<float> B = [0.5, 0.5, 0.5, 0.5];\n"
				"function sum(int n) -> float { float t = 0.0; for (int i = 0; i < n; ++i) { t += A[i & 3] * B[i & 3]; } return t; }\n"
				"class K { array<float> A = [10.0, 20.0, 30.0, 40.0]; float s(int n) { float t = 0.0; for (int i = 0; i < n; ++i) { t += A[i & 3] * B[i & 3]; } return t; } }\n"
				"function outer() -> string { string out = \"\"; for (int r = 0; r < 4; ++r) { out += to_string(sum(9)) + \" \"; if (r == 1) { A = [5.0, 6.0, 7.0, 8.0]; } if (r == 2) { B[1] = 9.0; } } return out; }\n"
				"string res = outer() + \",\" + to_string(K().s(9)); A = [1.0]; res + \",\" + (function() -> string { try { return to_string(sum(3)); } catch (e) { return \"E:\" + e; } })();";
			both(src, "", "environment containers at the entry");
			check_true(run_on(true, src).find("throw:") == std::string::npos, "runs: " + run_on(true, src));
			if (!engine::miss_injection_available()) { return; }
			const std::string oracle = run_on(false, src);
			for (uint32_t every : { 1u, 2u, 3u }) {
				auto e = engine::make(); e->set_backend(backend_type::vm); e->cells_miss_every(every); stdlib::register_all(*e);
				std::string got;
				try { got = e->execute(src).as_string(); } catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
				check_eq(oracle, got, "a miss at every " + std::to_string(every));
			}
		});
		test("stdlib_math_alias_under_parallel_transform", [this]() {
			const std::string src = "function row(int i) -> float { float x = i * 0.5; return sqrt(x * x + 1.0) + sin(x) * cos(x * 0.5) + floor(x * 0.25); }\n"
				"array<int> idx = []; for (int i = 0; i < 400; ++i) { idx.push(i); }\n"
				"function serial() -> string { float s = 0.0; for (int i = 0; i < 400; ++i) { s += row(i); } return to_string(s); }\n"
				"function par() -> string { var r = parallel_transform(idx, row); float s = 0.0; for (int i = 0; i < 400; ++i) { s += r[i]; } return to_string(s); }\n"
				"string a = serial() + \"=\" + par();\n"
				"sqrt = function(float v) { return v * 2.0; };\n"
				"a + \",\" + serial() + \"=\" + par();";
			both(src, "", "the workers' aliases");
			check_true(run_on(true, src).rfind("throw:", 0) != 0, "runs: " + run_on(true, src));
			const std::string vm = run_on(true, src);
			const size_t c = vm.find(',');
			check_true(c != std::string::npos && vm.substr(0, vm.find('=')) == vm.substr(vm.find('=') + 1, c - vm.find('=') - 1), "serial and parallel agree: " + vm);
		});
	}
};

}

using pilot_cells_tests = jai::foundry::tests::pilot_cells_tests;
FOUNDRY_REGISTER(pilot_cells_tests)
