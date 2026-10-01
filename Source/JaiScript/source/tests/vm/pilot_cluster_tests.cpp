// Closed typed loops run from their back edge as one cell block. These are the contract controls
// for that shape: every shape lands the same on the interpreter and the vm.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <vector>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/vm/disassembler.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class pilot_cluster_tests : public suite {
public:
	pilot_cluster_tests() : suite("Closed Typed Loops") {}

	// The render column loop's shape: a typed compare head, a typed element load through a var&
	// container, two no-result typed-lane element stores through a var& container, typed adds,
	// the back edge. Returns "<gi*1000+y>:<weighted pixel sum>" or "throw:<text>".
	static constexpr const char* k_prelude = R"(
		array<int> PIX = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
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
		function weighted() -> int { int s = 0; for (int i = 0; i < PIX.size(); ++i) { s += PIX[i] * (i + 1); } return s; }
		function run(int cy0, int cy1, int tstep, int k) -> string {
			var& s = STRIPS[0];
			string out = "";
			try { out = to_string(paint(PIX, s, cy0, cy1, 2, 0, tstep, 0, k)); }
			catch (err) { out = "throw:" + err; }
			return out + ":" + to_string(weighted());
		}
	)";

	std::string run_on(bool vm, const std::string& script, bool proofs = true, double budget = 0.0) {
		auto e = engine::make();
		if (vm) { e->set_backend(backend_type::vm); }
		stdlib::register_all(*e);
		if (!proofs) { e->type_proofs(false); }
		if (budget > 0.0) { e->execution_budget(budget); }
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
		// four iterations over strip entries 0..3: PIX = [7,7,8,8,9,9,10,10,0,0,0,0], gi 8, y 4
		test("column_loop_lands_the_same_on_both_backends", [this]() {
			both(std::string(k_prelude) + "run(0, 4, 2048, 1);", "8004:326", "four iterations");
		});
		test("zero_and_one_iteration", [this]() {
			both(std::string(k_prelude) + "run(4, 4, 2048, 1);", "8004:0", "zero iterations");
			both(std::string(k_prelude) + "run(0, 1, 2048, 1);", "2001:21", "one iteration");
		});
		test("overflow_after_stores_keeps_the_stores_and_the_text", [this]() {
			// acc += k with k = 2^62: the first addition gives 2^62, the second overflows, so the
			// second iteration's stores are complete and the third never starts
			both(std::string(k_prelude) + "run(0, 4, 2048, 4611686018427387904);", "", "late overflow");
			const std::string vm = run_on(true, std::string(k_prelude) + "run(0, 4, 2048, 4611686018427387904);");
			check_true(vm.rfind("throw:", 0) == 0, "the overflow raises: " + vm);
			check_true(vm.find(":77") != std::string::npos, "two iterations of stores retained (7*1+7*2+8*3+8*4 = 77): " + vm);
		});
		test("non_int_element_mid_loop_falls_back_with_the_same_text", [this]() {
			const std::string script = std::string(k_prelude) + "STRIPS[0][2] = \"x\"; run(0, 4, 2048, 1);";
			both(script, "", "string element on the third iteration");
			const std::string vm = run_on(true, script);
			check_true(vm.rfind("throw:", 0) == 0, "the declaration rejects the string: " + vm);
			check_true(vm.find(":77") != std::string::npos, "two iterations of stores retained: " + vm);
		});
		test("getter_backed_reference_container_runs_the_getter_identically", [this]() {
			// A binder property with a getter and no setter bound by reference as the pixel
			// container: every deref runs the getter, which the host counts. The store lane steps
			// aside for such a reference (silent-deref rule), so the count is the interpreter's
			// on both backends, with the block and without it, proofs on and off.
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
		test("budget_exhaustion_inside_the_loop_raises_the_same_text", [this]() {
			// The function loads and warms (its body compiled and cached by a short direct call) under
			// the default budget; only the long direct call from the host runs under the small one, and
			// its loop is a closed arithmetic loop with no bounded writes, so the only thing that can
			// raise inside it is the budget poll at the back edge (the block's, or LOOP_BACK's in the
			// general form). A direct call from the host arms the deadline the way execute does.
			const char* spin = "function spin(int n, int k) -> int { int y = 0; int acc = 0; while (y < n) { acc += k; y = y + 1; } return acc; }";
			std::string seen[2];
			for (int backend = 0; backend < 2; ++backend) {
				auto e = engine::make();
				if (backend == 1) { e->set_backend(backend_type::vm); }
				stdlib::register_all(*e);
				e->execute(spin);
				script_value fn = e->get_variable("spin");
				check_true(fn.is_function(), "spin resolves to a function");
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
			jai::lexer lex(script, symbolizer);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, vm.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");
			// the store and compare proofs come from the static checker's walk, which the engine runs
			// at load; a raw compile without it lowers only the declaration reads
			(void)jai::detail::run_static_check(*vm, parse_result.value());
			jai::vm::vm_compiler compiler(symbolizer);
			const std::string listing = jai::vm::disassemble(*compiler.compile_program(parse_result.value()), symbolizer);
			check_true(listing.find("CELLS_BACK") != std::string::npos, "the closed typed loop's back edge is the cell entry: " + listing);
		});
	}
};

}

using pilot_cluster_tests = jai::foundry::tests::pilot_cluster_tests;
FOUNDRY_REGISTER(pilot_cluster_tests)
