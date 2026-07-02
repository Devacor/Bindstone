#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/vm/disassembler.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <limits>
#include <fstream>
#include <filesystem>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// These tests pin the VM backend explicitly (never make_engine) so they exercise the
// bytecode VM regardless of the runner's --backend flag.
class vm_backend_tests : public suite {
	static std::shared_ptr<jai::engine> vm_engine() {
		auto e = jai::engine::make();
		e->set_backend(jai::backend_type::vm);
		return e;
	}

public:
	vm_backend_tests() : suite("VM Backend") {}

	void forge_tests() override {
		test("backend_name", [this]() {
			auto e = vm_engine();
			check_eq(std::string("vm"), e->get_backend_name());
		});

		test("arithmetic_basics", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)15, e->execute("10 + 5").as_int());
			check_eq((int64_t)5, e->execute("10 - 5").as_int());
			check_eq((int64_t)50, e->execute("10 * 5").as_int());
			check_eq((int64_t)2, e->execute("10 / 5").as_int());
			check_eq((int64_t)1, e->execute("10 % 3").as_int());
			check_eq((int64_t)14, e->execute("2 + 3 * 4").as_int());
			check_eq((int64_t)20, e->execute("(2 + 3) * 4").as_int());
			check_near(16.0, e->execute("10.5 + 5.5").as_float(), 0.0001);
			check_near(5.25, e->execute("10.5 / 2.0").as_float(), 0.0001);
			check_near(15.5, e->execute("10 + 5.5").as_float(), 0.0001);
		});

		test("variables_and_arithmetic", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)30, e->execute("var a = 10; var b = 20; a + b").as_int());
			check_eq((int64_t)30, e->execute("a + b").as_int());  // globals persist
		});

		test("overflow_policy", [this]() {
			auto e = vm_engine();
			// Parse-time folding wraps silently, so force the runtime path with variables
			if (e->throw_on_overflow()) {
				check_throws([&]() {
					e->execute("var a = 9223372036854775807; var b = 1; a + b");
				}, "checked builds raise on integer overflow");
			} else {
				auto v = e->execute("var a = 9223372036854775807; var b = 1; a + b");
				check_eq(std::numeric_limits<int64_t>::min(), v.as_int());
			}
		});

		test("division_by_zero", [this]() {
			auto e = vm_engine();
			check_throws([&]() { e->execute("5 / 0"); }, "integer division by zero throws");
			check_throws([&]() { e->execute("5.0 / 0.0"); }, "float division by zero throws");
			check_throws([&]() { e->execute("var a = 5; var b = 0; a / b"); }, "variable division by zero throws");
		});

		test("string_concat_and_compare", [this]() {
			auto e = vm_engine();
			check_eq(std::string("Hello, World!"), e->execute("\"Hello, \" + \"World!\"").as<std::string>());
			check_eq(std::string("n=42"), e->execute("var n = 42; \"n=\" + n").as<std::string>());
			check_true(e->execute("var s1 = \"apple\"; var s2 = \"banana\"; s1 < s2").as<bool>());
			check_true(e->execute("\"same\" == \"same\"").as<bool>());
			check_true(e->execute("\"a\" != \"b\"").as<bool>());
		});

		test("truthiness", [this]() {
			auto e = vm_engine();
			check_false(e->execute("!1").as<bool>());
			check_true(e->execute("!0").as<bool>());
			check_true(e->execute("!\"\"").as<bool>());
			check_false(e->execute("!\"x\"").as<bool>());
			check_true(e->execute("!null").as<bool>());
			check_eq((int64_t)1, e->execute("var v = 5; v ? 1 : 2").as_int());
			check_eq((int64_t)2, e->execute("var w = 0; w ? 1 : 2").as_int());
		});

		test("comparisons_spaceship_bitwise", [this]() {
			auto e = vm_engine();
			check_true(e->execute("5 < 10").as<bool>());
			check_true(e->execute("5 <= 5").as<bool>());
			check_true(e->execute("10 > 5").as<bool>());
			check_true(e->execute("5 >= 5").as<bool>());
			check_true(e->execute("5 == 5").as<bool>());
			check_true(e->execute("5 != 3").as<bool>());
			check_eq((int64_t)-1, e->execute("5 <=> 10").as_int());
			check_eq((int64_t)1, e->execute("10 <=> 5").as_int());
			check_eq((int64_t)0, e->execute("5 <=> 5").as_int());
			check_eq((int64_t)-1, e->execute("\"apple\" <=> \"banana\"").as_int());
			check_eq((int64_t)1, e->execute("5 & 3").as_int());
			check_eq((int64_t)7, e->execute("5 | 3").as_int());
			check_eq((int64_t)6, e->execute("5 ^ 3").as_int());
			check_eq((int64_t)20, e->execute("5 << 2").as_int());
			check_eq((int64_t)5, e->execute("20 >> 2").as_int());
			check_eq((int64_t)~5, e->execute("~5").as_int());
		});

		test("short_circuit_logic", [this]() {
			auto e = vm_engine();
			check_true(e->execute("true && true").as<bool>());
			check_false(e->execute("true && false").as<bool>());
			check_true(e->execute("false || true").as<bool>());
			check_false(e->execute("false || false").as<bool>());

			e->execute("var counter = 0;");
			e->execute("function bump() -> auto { counter = counter + 1; return true; }");
			e->execute("false && bump()");
			check_eq((int64_t)0, e->execute("counter").as_int());
			e->execute("true || bump()");
			check_eq((int64_t)0, e->execute("counter").as_int());
			e->execute("true && bump()");
			check_eq((int64_t)1, e->execute("counter").as_int());
		});

		test("increment_decrement", [this]() {
			auto e = vm_engine();
			e->execute("var x = 5;");
			check_eq((int64_t)5, e->execute("x++").as_int());
			check_eq((int64_t)6, e->execute("x").as_int());
			check_eq((int64_t)7, e->execute("++x").as_int());
			check_eq((int64_t)7, e->execute("x--").as_int());
			check_eq((int64_t)6, e->execute("x").as_int());
			check_eq((int64_t)5, e->execute("--x").as_int());
		});

		test("compound_assignments", [this]() {
			auto e = vm_engine();
			e->execute("var x = 10;");
			check_eq((int64_t)15, e->execute("x += 5").as_int());
			check_eq((int64_t)12, e->execute("x -= 3").as_int());
			check_eq((int64_t)24, e->execute("x *= 2").as_int());
			check_eq((int64_t)6, e->execute("x /= 4").as_int());
			e->execute("var s = \"Hello\";");
			check_eq(std::string("Hello, World!"), e->execute("s += \", World!\"").as<std::string>());
		});

		test("while_loop", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)10, e->execute(R"(
				var sum = 0;
				var i = 1;
				while (i <= 4) {
					sum = sum + i;
					i = i + 1;
				}
				sum;
			)").as_int());
		});

		test("for_loop", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)45, e->execute(R"(
				var total = 0;
				for (var i = 0; i < 10; i = i + 1) {
					total = total + i;
				}
				total;
			)").as_int());
		});

		test("for_loop_optional_clauses", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)5, e->execute(R"(
				var i = 0;
				for (;;) {
					i = i + 1;
					if (i >= 5) { break; }
				}
				i;
			)").as_int());
		});

		test("nested_loops_break_continue", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)12, e->execute(R"(
				var count = 0;
				for (var i = 0; i < 5; i = i + 1) {
					if (i == 1) { continue; }
					for (var j = 0; j < 5; j = j + 1) {
						if (j == 3) { break; }
						count = count + 1;
					}
				}
				count;
			)").as_int());
			check_eq((int64_t)6, e->execute(R"(
				var hits = 0;
				var i = 0;
				while (i < 6) {
					i = i + 1;
					if (i % 2 == 0) { continue; }
					var j = 0;
					while (true) {
						j = j + 1;
						if (j >= 2) { break; }
					}
					hits = hits + j;
				}
				hits;
			)").as_int());
		});

		test("functions_basic", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)7, e->execute(R"(
				function add(int a, int b) -> int { return a + b; }
				add(3, 4);
			)").as_int());
		});

		test("function_default_args", [this]() {
			auto e = vm_engine();
			e->execute("function scaled(int a, int b = 10) -> int { return a * b; }");
			check_eq((int64_t)50, e->execute("scaled(5)").as_int());
			check_eq((int64_t)15, e->execute("scaled(5, 3)").as_int());
		});

		test("function_recursion", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)120, e->execute(R"(
				function fact(int n) -> int {
					if (n <= 1) { return 1; }
					return n * fact(n - 1);
				}
				fact(5);
			)").as_int());
		});

		test("recursion_depth_error", [this]() {
			auto e = vm_engine();
			check_throws([&]() {
				e->execute(R"(
					function forever() -> auto { return forever(); }
					forever();
				)");
			}, "runaway recursion raises max_recursion_depth");
		});

		test("variadic_cpp_function", [this]() {
			auto e = vm_engine();
			e->add_variadic_function("sum_all", [](const std::vector<script_value>& args) -> checked_result<script_value> {
				script_int total = 0;
				jai::engine* eng = nullptr;
				for (const auto& a : args) {
					total += a.as_int();
					eng = a.get_engine();
				}
				return script_value(total, eng);
			});
			check_eq((int64_t)6, e->execute("sum_all(1, 2, 3)").as_int());
			check_eq((int64_t)15, e->execute("sum_all(1, 2, 3, 4, 5)").as_int());
		});

		test("by_ref_params_mutate_caller", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)11, e->execute(R"(
				function bump(int& target) { target = target + 10; }
				var value = 1;
				bump(value);
				value;
			)").as_int());
		});

		test("value_params_do_not_mutate_caller", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)1, e->execute(R"(
				function tweak(int copy) { copy = copy + 10; }
				var original = 1;
				tweak(original);
				original;
			)").as_int());
		});

		test("lambda_basic", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)10, e->execute(R"(
				var double_it = [](int x) { return x * 2; };
				double_it(5);
			)").as_int());
		});

		test("lambda_capture_by_value", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)1, e->execute(R"(
				var x = 1;
				var f = [=]() { return x; };
				x = 2;
				f();
			)").as_int());
		});

		test("lambda_capture_by_reference", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)3, e->execute(R"(
				var c = 0;
				var inc = [&]() { c = c + 1; };
				inc();
				inc();
				inc();
				c;
			)").as_int());
		});

		test("lambda_explicit_captures", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)15, e->execute(R"(
				var base = 10;
				var f = [base](int x) { return base + x; };
				f(5);
			)").as_int());
		});

		test("lambda_auto_capture", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)30, e->execute(R"(
				var outer = 20;
				var f = [](int x) { return outer + x; };
				f(10);
			)").as_int());
		});

		test("lambda_closure_over_loop_vars", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)0, e->execute(R"(
				var captured = null;
				for (var i = 0; i < 3; i = i + 1) {
					if (i == 0) {
						captured = [=]() { return i; };
					}
				}
				captured();
			)").as_int());
		});

		test("lambda_inside_function_captures_locals", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)42, e->execute(R"(
				function make_getter() -> auto {
					var secret = 42;
					return [=]() { return secret; };
				}
				var g = make_getter();
				g();
			)").as_int());
		});

		test("array_literal_and_index", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)2, e->execute("var a = [1, 2, 3]; a[1]").as_int());
			check_eq((int64_t)13, e->execute("a[0] = 10; a[0] + a[2]").as_int());
			check_throws([&]() { e->execute("a[99]"); }, "out of bounds read throws");
			check_throws([&]() { e->execute("a[\"k\"]"); }, "non-integer array index throws");
		});

		test("map_literal_and_index", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)1, e->execute("var m = {\"k\": 1, \"j\": 2}; m[\"k\"]").as_int());
			check_eq((int64_t)5, e->execute("m[\"k\"] = 5; m[\"k\"]").as_int());
			check_eq((int64_t)42, e->execute("m[\"new\"] = 42; m[\"new\"]").as_int());
			check_true(e->execute("m[\"missing\"] == null").as<bool>());
		});

		test("nested_containers", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)4, e->execute(R"(
				var grid = [[1, 2], [3, 4]];
				grid[1][1];
			)").as_int());
			check_eq((int64_t)9, e->execute(R"(
				grid[1][1] = 9;
				grid[1][1];
			)").as_int());
		});

		test("budget_overrun_raises", [this]() {
			auto e = vm_engine();
			e->execution_budget(0.05);
			check_throws([&]() {
				e->execute("while (true) { }");
			}, "budget overrun raises an error");
			// Engine stays usable afterwards
			e->execution_budget(1.0);
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("unsupported_constructs_error_cleanly", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)7, e->execute("class Cat { int a = 7; } auto c = Cat(); c.a").as_int());
			check_throws([&]() { e->execute("coroutine gen() { yield 1; } auto g = gen(); g.resume();"); }, "coroutines report B4");
			check_throws([&]() { e->execute("include \"no_such_file.jai\";"); }, "include reports B4");
			// Engine stays usable
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("throw_across_frames_caught_by_outer_try", [this]() {
			auto e = vm_engine();
			check_eq(std::string("deep boom"), e->execute(R"(
				auto inner() { throw "deep boom"; }
				auto middle() { return inner(); }
				var caught = "";
				try { middle(); } catch (err) { caught = err; }
				caught;
			)").as<std::string>());
			// Typed payloads stay typed across frames
			check_eq((int64_t)42, e->execute(R"(
				auto thrower() { throw 42; }
				var got = 0;
				try { thrower(); } catch (err) { got = err; }
				got;
			)").as_int());
			// Unwinding across intermediate frames leaves the engine consistent
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		test("switch_fallthrough_chain", [this]() {
			auto e = vm_engine();
			check_eq(std::string("twothreedefault"), e->execute(R"(
				var r = "";
				switch (2) {
					case 1: r += "one"; fallthrough;
					case 2: r += "two"; fallthrough;
					case 3: r += "three"; fallthrough;
					default: r += "default";
				}
				r;
			)").as<std::string>());
			// Break-by-default: no fallthrough without the keyword
			check_eq(std::string("two"), e->execute(R"(
				var s = "";
				switch (2) { case 1: s = "one"; case 2: s = "two"; case 3: s = "three"; }
				s;
			)").as<std::string>());
		});

		test("differential_try_state_edges", [this]() {
			// Same scripts on both backends; results must agree exactly
			const char* catch_var_across_callee = R"(
				auto helper() { try { return 5; } catch (ignored) { return 0; } }
				var out = "";
				try { throw "orig"; } catch (err) { helper(); out = err; }
				out;
			)";
			const char* bare_rethrow_no_active = R"(
				auto f() { throw; }
				var msg = "";
				try { f(); } catch (err) { msg = err; }
				msg;
			)";
			const char* break_pops_handler = R"(
				var n = 0;
				for (var i = 0; i < 3; ++i) { try { n += 1; break; } catch (ignored) { n += 100; } }
				var hit = 0;
				try { throw "after"; } catch (err) { hit = 1; }
				to_string(n * 10 + hit);
			)";
			for (const char* src : {catch_var_across_callee, bare_rethrow_no_active, break_pops_handler}) {
				auto interp = jai::engine::make();
				jai::stdlib::register_all(interp);
				auto vm = vm_engine();
				jai::stdlib::register_all(vm);
				check_eq(interp->execute(src).as<std::string>(), vm->execute(src).as<std::string>());
			}
		});

		test("range_for_reference_mutation", [this]() {
			auto e = vm_engine();
			jai::stdlib::register_all(e);
			check_eq((int64_t)12, e->execute(R"(
				auto nums = [1, 2, 3];
				for (auto& x : nums) { x *= 2; }
				nums[0] + nums[1] + nums[2];
			)").as_int());
			check_eq((int64_t)60, e->execute(R"(
				auto scores = {"a": 10, "b": 20};
				for (auto& kv : scores) { kv.second *= 2; }
				scores["a"] + scores["b"];
			)").as_int());
		});

		test("custom_global_operator", [this]() {
			auto e = vm_engine();
			bool used = false;
			e->add_function("+", [&used](int a, int b) -> int {
				used = true;
				return (a + b) * 2;
			});
			e->set_has_custom_numeric_operators(true);
			check_eq((int64_t)16, e->execute("var a = 5; var b = 3; a + b").as_int());
			check_true(used);
		});

		test("class_inheritance_and_super", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)10, e->execute(R"(
				class Cat { int a = 0; Cat(int v) { a = v; } int get() { return a; } }
				class Tiger : Cat { Tiger() : super(5) {} int roar() { return a * 2; } }
				auto t = Tiger();
				t.roar()
			)").as_int());
			check_eq((int64_t)5, e->execute("auto t2 = Tiger(); t2.get()").as_int());
		});

		test("ctor_delegation_field_defaults", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)5, e->execute(R"(
				class D { int a = 1; D() : this(5) {} D(int v) { a = v; } }
				auto d = D();
				d.a
			)").as_int());
		});

		test("static_fields_and_methods", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)2, e->execute(R"(
				class Counter {
					static int count = 0;
					static int next() { count = count + 1; return count; }
				}
				Counter::next();
				Counter::next()
			)").as_int());
			check_eq((int64_t)2, e->execute("Counter::count").as_int());
		});

		test("builtin_container_and_string_methods", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)3, e->execute("auto arr = [1, 2]; arr.push(3); arr.size()").as_int());
			check_eq((int64_t)3, e->execute("arr.pop()").as_int());
			check_true(e->execute("auto m = {\"k\": 1}; m.contains(\"k\")").as<bool>());
			check_eq(std::string("cba"), e->execute("var s = \"abc\"; s.reverse(); s").as<std::string>());
			check_throws([&]() { e->execute("array<int> typed = [1]; typed.push(\"nope\");"); }, "typed push mismatch throws");
		});

		test("enum_and_namespace_dispatch", [this]() {
			auto e = vm_engine();
			check_eq((int64_t)1, e->execute("enum Color { red, green, blue } Color.green").as_int());
			check_eq((int64_t)5, e->execute(R"(
				namespace vmmath { int add(int a, int b) { return a + b; } }
				vmmath::add(2, 3)
			)").as_int());
		});

		test("null_safe_member_and_method", [this]() {
			auto e = vm_engine();
			check_true(e->execute("var nothing = null; nothing?.field == null").as<bool>());
			check_true(e->execute("nothing?.method() == null").as<bool>());
		});

		test("disassembler_smoke", [this]() {
			auto e = vm_engine();
			auto* symbolizer = e->get_symbolizer();
			std::string script = "var x = 1; while (x < 10) { x = x + 1; } x;";
			auto templates = e->get_registered_template_types();
			jai::lexer lex(script, symbolizer, templates);
			auto tokens = lex.tokenize();
			jai::parser p(tokens, symbolizer, e.get(), templates);
			auto parse_result = p.parse();
			check_true(parse_result.has_value(), "snippet parses");

			jai::vm::vm_compiler compiler(symbolizer);
			auto compiled = compiler.compile_program(parse_result.value());
			check_true(compiled != nullptr);

			std::string listing = jai::vm::disassemble(*compiled, symbolizer);
			check_true(listing.find("LOOP_BACK") != std::string::npos, "loop back edge present");
			check_true(listing.find("JUMP_IF_FALSE") != std::string::npos, "conditional jump present");
			check_true(listing.find("DECL_VAR") != std::string::npos, "variable declaration present");
			check_true(listing.find("STORE") != std::string::npos, "store present");
			check_true(listing.find("IMPLICIT_RETURN") != std::string::npos, "implicit return present");
			check_true(listing.find("HALT") != std::string::npos, "halt present");
		});

		test("differential_interpreter_vs_vm", [this]() {
			const char* scripts[] = {
				"var t = 0; for (var i = 1; i <= 10; i = i + 1) { t = t + i * i; } t;",
				"function fib(int n) -> int { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); } fib(12);",
				"var s = \"\"; var i = 0; while (i < 4) { s = s + \"ab\"; i = i + 1; } s;",
				"var a = [5, 3, 8]; var mx = a[0]; for (var i = 1; i < 3; i = i + 1) { if (a[i] > mx) { mx = a[i]; } } mx;",
				"var m = {\"x\": 1}; m[\"y\"] = 2; m[\"x\"] + m[\"y\"];",
				"var acc = 0; var f = [&](int v) { acc += v; }; f(3); f(4); acc;",
				"(5 <=> 2) + (2 <=> 5) + (3 <=> 3);",
				"var n = 6; var r = n > 5 ? \"big\" : \"small\"; r;",
			};
			for (const char* script : scripts) {
				auto interp = jai::engine::make();
				auto vm = vm_engine();
				auto lhs = interp->execute(script);
				auto rhs = vm->execute(script);
				check_eq(lhs.to_string(), rhs.to_string(), std::string("differential: ") + script);
			}
		});

		// A yield fired inside a range-for AND a try must resume mid-construct: the fiber
		// snapshot has to preserve the loop's iter-state and the try handler record.
		test("coroutine_yield_inside_range_for_and_try", [this]() {
			auto vm = vm_engine();
			jai::stdlib::register_all(vm);
			auto rhs = vm->execute(R"(
				coroutine int gen() {
					auto data = [10, 20, 30];
					for (auto x : data) {
						try {
							yield x;
						} catch (e) {
							yield -1;
						}
					}
					return 7;
				}
				auto c = gen();
				auto sum = 0;
				while (!c.done()) { sum = sum + c.resume(); }
				sum
			)");
			check_eq((int64_t)67, rhs.as_int());
		});

		// yield deep inside nested C-style loops resumes at the exact point (no restart).
		test("coroutine_yield_inside_nested_for", [this]() {
			auto vm = vm_engine();
			jai::stdlib::register_all(vm);
			auto rhs = vm->execute(R"(
				coroutine int gen() {
					for (int i = 0; i < 2; ++i) {
						for (int j = 0; j < 3; ++j) {
							yield i * 10 + j;
						}
					}
					return 99;
				}
				auto c = gen();
				auto sum = 0;
				while (!c.done()) { sum = sum + c.resume(); }
				sum
			)");
			check_eq((int64_t)135, rhs.as_int());
		});

		// Abandoning a suspended fiber (dropped at block-scope exit) must destruct cleanly;
		// the Debug env-cycle checker fires on a leaked closure/env cycle.
		test("abandoned_suspended_fiber_destructs_cleanly", [this]() {
			auto e = vm_engine();
			jai::stdlib::register_all(e);
			e->execute(R"(
				coroutine int forever() {
					int i = 0;
					while (true) { yield i; i = i + 1; }
				}
				{
					auto c = forever();
					c.resume();
					c.resume();
				}
			)");
			check_eq((int64_t)42, e->execute("21 * 2").as_int());
		});

		// A host callback that calls execute() mid-resume must not sever the running fiber's
		// yield machinery; the next yield still reaches the caller (regression #40 analog).
		test("nested_execute_during_resume", [this]() {
			auto e = vm_engine();
			jai::stdlib::register_all(e);
			engine* raw = e.get();
			e->add_function("nested_exec", [raw]() -> script_int {
				raw->execute("1 + 1");
				return 0;
			});
			auto r = e->execute(R"(
				coroutine int co() {
					yield 1;
					nested_exec();
					yield 3;
					return 0;
				}
				auto c = co();
				auto a = c.resume();
				auto b = c.resume();
				a * 10 + b
			)");
			check_eq((int64_t)13, r.as_int());
		});

		// include inside a function body: the VM must observe the same result as the
		// interpreter (re-entrant execute defines into the global environment).
		test("include_inside_function_differential", [this]() {
			auto temp_dir = std::filesystem::temp_directory_path();
			auto file_path = temp_dir / "vm_include_in_fn.jai";
			{
				std::ofstream f(file_path);
				f << "auto vm_included_flag = 77;";
			}
			const char* script = R"(
				function loader() { include "vm_include_in_fn.jai"; }
				loader();
				vm_included_flag
			)";
			auto interp = jai::engine::make();
			interp->add_include_path(temp_dir.string());
			auto vm = vm_engine();
			vm->add_include_path(temp_dir.string());
			auto lhs = interp->execute(script);
			auto rhs = vm->execute(script);
			check_eq(lhs.to_string(), rhs.to_string(), "include-inside-function differential");
			std::filesystem::remove(file_path);
		});
	}
};

} // namespace jai::foundry::tests

using vm_backend_tests_alias = jai::foundry::tests::vm_backend_tests;
FOUNDRY_REGISTER(vm_backend_tests_alias)
