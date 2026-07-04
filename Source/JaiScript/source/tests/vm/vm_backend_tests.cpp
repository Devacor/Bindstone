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

		// Parity-locked quirk (verified on both backends 2026-07-03): when a method calls a
		// plain top-level function, the callee's exit path clears the CALLER method scope's
		// this-binding. Afterwards an unqualified field read throws undefined-variable and
		// `this.x` reads through a nulled this (type mismatch). Any call-path rework must
		// reproduce this bug-for-bug on both backends.
		test("pinned_quirk_callee_clears_caller_this", [this]() {
			const char* unqualified_field_read = R"(
				int helper() { return 1; }
				class P { int x = 5; int m() { helper(); return x; } }
				var p = P();
				p.m();
			)";
			const char* qualified_this_read = R"(
				int helper() { return 1; }
				class Q { int x = 5; auto m() { helper(); return this.x; } }
				var q = Q();
				q.m();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				std::string unqualified_error;
				try { e->execute(unqualified_field_read); }
				catch (const std::exception& ex) { unqualified_error = ex.what(); }
				check_true(unqualified_error.find("Undefined variable 'x'") != std::string::npos,
					"unqualified field read after a plain-function call throws undefined variable");
				std::string qualified_error;
				try { e->execute(qualified_this_read); }
				catch (const std::exception& ex) { qualified_error = ex.what(); }
				check_true(qualified_error.find("Type mismatch") != std::string::npos,
					"this.x after a plain-function call reads a nulled this");
			}
		});

		// Stage 2 (compile-time lazy envs): plain in-loop callees whose bodies provably never
		// touch the per-call scope environment skip creating it. These pin the parity contract
		// on both backends and the compiler's fail-closed trigger table.
		test("lazy_env_quirk_from_method_top_scope", [this]() {
			// helper's body is lazy-eligible; the pinned caller-this-clear quirk must survive
			const char* src = R"(
				int helper(int a) { return a + 1; }
				class L { int x = 5; int m() { helper(1); return x; } }
				var l = L();
				l.m();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				std::string error;
				try { e->execute(src); }
				catch (const std::exception& ex) { error = ex.what(); }
				check_true(error.find("Undefined variable 'x'") != std::string::npos,
					"lazy callee still clears the caller method's this");
			}
		});

		test("lazy_env_no_this_clear_from_method_block_scope", [this]() {
			// Caller env at the call is a plain block env, so neither the eager two-level
			// walk nor the lazy single-level rule clears this: the field stays readable
			const char* src = R"(
				int helper(int a) { return a + 1; }
				class B { int x = 5; int m() { { var t = helper(1); return x + t; } } }
				var b = B();
				b.m();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)7, e->execute(src).as_int());
			}
		});

		test("lazy_env_ref_param_binding", [this]() {
			// Caller env metadata resolves through env == environment_.get() into a lazy
			// callee's reference parameter; repeated calls exercise pooled-record reuse.
			// (By-ref args THROUGH an intermediate function frame diverge between the
			// backends today — pre-existing before lazy envs, deliberately not pinned here.)
			const char* direct = R"(
				function bump(int& t) { t = t + 1; }
				var v = 5;
				bump(v);
				bump(v);
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)7, e->execute(direct).as_int());
			}
		});

		test("lazy_env_throw_caught_by_caller", [this]() {
			const char* src = R"(
				function boom(int x) { throw "lazy bang"; }
				var msg = "";
				try { boom(1); } catch (err) { msg = err; }
				msg;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq(std::string("lazy bang"), e->execute(src).as<std::string>());
			}
		});

		test("lazy_env_ref_decl_callee_parity", [this]() {
			// op_decl_ref bodies stay eager (references capture env identity); semantics
			// must remain byte-identical on both backends
			const char* src = R"(
				var gv = 4;
				function via_ref() -> int { int& r = gv; r = r * 3; return gv; }
				via_ref();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)12, e->execute(src).as_int());
			}
		});

		test("needs_frame_env_trigger_table", [this]() {
			auto e = vm_engine();
			auto body_needs_env = [&](const char* script) -> bool {
				auto* symbolizer = e->get_symbolizer();
				auto templates = e->get_registered_template_types();
				jai::lexer lex(script, symbolizer, templates);
				auto tokens = lex.tokenize();
				jai::parser p(tokens, symbolizer, e.get(), templates);
				auto parsed = p.parse();
				check_true(parsed.has_value(), "trigger-table snippet parses");
				for (const auto& d : parsed.value()) {
					if (d->get_type() == jai::node_type::function_decl) {
						auto fn = std::static_pointer_cast<jai::function_decl>(d);
						jai::vm::vm_compiler compiler(symbolizer);
						auto compiled = compiler.compile_callable(fn->name, fn->parameters, fn->body, fn->local_count);
						return compiled->needs_frame_env;
					}
				}
				check_true(false, "no function decl in trigger-table snippet");
				return true;
			};
			// Lazy-eligible bodies (call-dense recursion is the whole point)
			check_false(body_needs_env("function fib(auto n) -> auto { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); }"),
				"fib-shaped body elides its frame env");
			check_false(body_needs_env("function bump(int& t) { t = t + 1; }"),
				"ref-param assignment body elides");
			check_false(body_needs_env("function boom() { throw \"bang\"; }"),
				"throwing body elides");
			// Trigger table: env-identity-capturing bodies stay eager
			check_true(body_needs_env("function refs(int x) -> int { int& r = x; r = 7; return x; }"),
				"reference decl keeps the eager env");
			check_true(body_needs_env("function closes() -> auto { var n = 2; return [=](int v) { return v + n; }; }"),
				"closure mint keeps the eager env");
			check_true(body_needs_env("function tries() -> int { try { return 1; } catch (e) { return 0; } }"),
				"try/catch keeps the eager env");
			check_true(body_needs_env("function scoped() -> int { { var t = 1; t = t + 1; } return 2; }"),
				"block scope push keeps the eager env");
		});

		// Stage 3 (method flattening): method self-recursion no longer stacks native
		// frames, but the depth-10000 error must still be raised and stay catchable
		// through the flattened path on both backends.
		test("ctor_self_recursion_raises_catchable_error", [this]() {
			// Constructor recursion runs on the native entry path with wide frames; it
			// used to hard-crash (0xC00000FD) in Debug before reaching the depth cap.
			// Either guard (depth cap or native-stack headroom) must fire catchably.
			const char* src = R"(
				class C { C() { var c = C(); } }
				var msg = "";
				try { var root = C(); } catch (err) { msg = err; }
				msg;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				e->execution_budget(0);
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("recursion") != std::string::npos,
					"ctor self-recursion raises a catchable recursion error");
			}
		});

		test("method_recursion_depth_error_catchable", [this]() {
			const char* src = R"(
				class R { int spin(int n) { return this.spin(n + 1); } }
				var msg = "";
				try { R().spin(0); } catch (err) { msg = err; }
				msg;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				e->execution_budget(0);   // depth-10000 method recursion outruns the default budget in Debug
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("Maximum recursion depth") != std::string::npos,
					"method self-recursion raises the catchable depth error");
			}
		});

		// Stage 4 (metadata skip): ref-free direct callees skip the per-call arg-metadata
		// build, so current_arg_metadata_ holds a stale outer snapshot while they run.
		// Ref-param binding around and after such calls must be unaffected.
		test("ref_param_call_surrounds_ref_free_call", [this]() {
			const char* src = R"(
				int plain(int a) { return a + 1; }
				function bump(int& t) { var w = plain(2); t = t + w; }
				var v = 5;
				bump(v);
				bump(v);
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)11, e->execute(src).as_int());
			}
		});

		test("ref_free_call_inside_ref_call_arguments", [this]() {
			// plain() runs (and skips metadata) while bump's argument list is being
			// evaluated; bump's own ref metadata is built afterwards and must bind v
			const char* src = R"(
				int plain(int a) { return a * 2; }
				function bump(int& t, int by) { t = t + by; }
				var v = 1;
				bump(v, plain(3));
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)7, e->execute(src).as_int());
			}
		});

		// Ref-param binding across scopes: both backends must mutate the actual argument
		// variable, wherever it lives (block env, method env, caller frame slot).
		test("ref_param_from_block_and_loop_scopes", [this]() {
			const char* src = R"(
				function bump(int& t) { t = t + 1; }
				var a = 0;
				{ bump(a); }
				if (true) { bump(a); }
				var i = 0;
				while (i < 2) { bump(a); i = i + 1; }
				for (int k = 0; k < 3; k++) { bump(a); }
				for (auto x : [1, 2]) { bump(a); }
				a;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)9, e->execute(src).as_int());
			}
		});

		test("ref_param_binds_caller_slot_local_not_same_named_global", [this]() {
			// The worst historical failure: the callee wrote through a same-named GLOBAL
			// while the caller's slot local stayed untouched
			const char* src = R"(
				var t = 100;
				function bump(int& t) { t = t + 1; }
				function caller() -> int { var t = 5; bump(t); return t; }
				caller() * 1000 + t;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)6100, e->execute(src).as_int());
			}
		});

		test("ref_param_slot_local_from_inner_block_survives_block_exit", [this]() {
			const char* src = R"(
				function bump(int& t) { t = t + 1; }
				function f() -> int {
					var t = 5;
					{ bump(t); }
					return t;
				}
				f();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)6, e->execute(src).as_int());
			}
		});

		test("ref_param_global_from_method_body", [this]() {
			const char* src = R"(
				var g = 1;
				function bumpg(int& t) { t = t + 5; }
				class C { int m() { bumpg(g); return g; } }
				var c = C();
				c.m();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)6, e->execute(src).as_int());
			}
		});

		test("ref_param_pass_through", [this]() {
			const char* src = R"(
				function inner(int& x) { x = x + 1; }
				function outer(int& y) { inner(y); }
				var v = 0;
				outer(v);
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)1, e->execute(src).as_int());
			}
		});

		test("ref_param_pass_through_with_intermediate_local", [this]() {
			// The extra local used to flip the vm intermediate into a framed env and
			// change behavior; both shapes must work identically now
			const char* src = R"(
				function inner(int& x) { x = x + 1; }
				function mid(int& x) { var t = x; inner(x); }
				var x = 1;
				mid(x);
				x;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)2, e->execute(src).as_int());
			}
		});

		test("ref_param_array_mutation_from_try_block", [this]() {
			const char* src = R"(
				var arr = [1];
				function r1(auto& a) { a.push(4); }
				var failed = 0;
				try { r1(arr); } catch (e) { failed = 1; }
				arr.size() * 10 + failed;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)20, e->execute(src).as_int());
			}
		});

		test("ref_param_default_arg_slow_path_from_function_local", [this]() {
			const char* src = R"(
				function r4(auto& a, int z = 0) { a.push(9); }
				function f() -> int {
					var arr = [1];
					r4(arr);
					return arr.size();
				}
				f();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)2, e->execute(src).as_int());
			}
		});

		test("ref_param_two_same_named_block_locals", [this]() {
			const char* src = R"(
				function bump(int& t) { t = t + 1; }
				function f() -> int {
					var acc = 0;
					{ var t = 10; bump(t); acc = acc + t; }
					{ var t = 20; bump(t); acc = acc + t; }
					return acc;
				}
				f();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)32, e->execute(src).as_int());
			}
		});

		test("ref_param_interleaved_locals_in_loop", [this]() {
			const char* src = R"(
				function bump(int& t) { t = t + 1; }
				function f() -> int {
					var a = 0;
					var b = 100;
					for (int i = 0; i < 3; i++) { bump(a); bump(b); }
					return a * 1000 + b;
				}
				f();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)3103, e->execute(src).as_int());
			}
		});

		test("ref_param_instance_field_from_method_body", [this]() {
			// The mutation must land in the instance field on both backends. (A bare
			// field read right after a plain call in the same method body hits the
			// separately-pinned caller-this-clear quirk, so the size is read outside.)
			const char* src = R"(
				function pushRef(auto& a) { a.push(5); }
				class Holder {
					auto d = [1];
					int go() { pushRef(d); return 7; }
				}
				var h = Holder();
				var r = h.go();
				r * 10 + h.d.size();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)72, e->execute(src).as_int());
			}
		});

		// Stage 1 (shared-holder pass-through): passing a bound reference onward shares the
		// original holder instead of minting a new one per hop, so element refs keep their
		// container re-resolution and env-anchored refs keep the origin anchor.
		test("ref_param_pass_through_chain_depth3", [this]() {
			// pre-fix: green both backends (slot-path flatten kept the origin anchor alive)
			const char* src = R"(
				function inc(int& x) { x = x + 1; }
				function relay1(int& y) { inc(y); }
				function relay2(int& z) { relay1(z); }
				function origin() -> int { var t = 5; relay2(t); return t; }
				origin();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)6, e->execute(src).as_int());
			}
		});

		test("ref_param_pass_through_env_origin_depth3", [this]() {
			// pre-fix: green both backends
			const char* src = R"(
				function inc(int& x) { x = x + 7; }
				function relay1(int& y) { inc(y); }
				function relay2(int& z) { relay1(z); }
				var v = 1;
				relay2(v);
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)8, e->execute(src).as_int());
			}
		});

		test("ref_param_env_visible_ref_decl_arg", [this]() {
			// pre-fix: green both backends (env-path flatten of a global ref decl)
			const char* src = R"(
				var v = 1;
				auto& g = v;
				function bump(int& t) { t = t + 9; }
				bump(g);
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)10, e->execute(src).as_int());
			}
		});

		test("ref_param_element_ref_single_hop", [this]() {
			// pre-fix: green both backends (no realloc, creation-time element address valid)
			const char* src = R"(
				var arr = [1, 2, 3];
				function inc(int& x) { x = x + 10; }
				function driver() { for (auto& el : arr) { inc(el); } }
				driver();
				arr[0] + arr[1] + arr[2];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)36, e->execute(src).as_int());
			}
		});

		test("ref_param_element_ref_pass_through_realloc", [this]() {
			// Sanctioned fix (b): pre-fix the bind flatten pinned the creation-time element
			// address, so the write landed in the freed pre-realloc buffer (arr[0] stayed 7)
			const char* src = R"(
				var arr = [7];
				function grow(int& x) {
					for (int i = 0; i < 64; i = i + 1) { arr.push(0); }
					x = 42;
				}
				function relay(int& x) { grow(x); }
				function driver() { for (auto& el : arr) { relay(el); break; } }
				driver();
				arr[0];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)42, e->execute(src).as_int());
			}
		});

		test("ref_param_element_ref_shrink_errors", [this]() {
			// Sanctioned fix (b): pre-fix the write went through a stale pointer into the
			// popped element (silent corruption, msg stayed empty); now the re-resolve errors
			const char* src = R"(
				var arr = [1, 2, 3];
				var msg = "";
				function wipe(int& x) { arr.pop(); arr.pop(); arr.pop(); x = 5; }
				function driver() { for (auto& el : arr) { try { wipe(el); } catch (e) { msg = e; } break; } }
				driver();
				msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("removed array element") != std::string::npos,
					"shrunk element write errors");
				if (!use_vm) { first = msg; } else { check_eq(first, msg, "identical error text on both backends"); }
			}
		});

		test("ref_param_element_compound_store", [this]() {
			// Compound stores on a ref param bound to an array element go through the
			// re-resolving deref (the shared holder is container-based, not a pointer).
			// (++ on a ref param is a separate pre-existing gap on both backends.)
			const char* src = R"(
				var arr = [5, 6];
				function addTo(int& x) { x += 10; }
				function driver() { for (auto& el : arr) { addTo(el); } }
				driver();
				arr[0] * 100 + arr[1];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)1516, e->execute(src).as_int());
			}
		});

		test("ref_param_recursive_pass_down", [this]() {
			// pre-fix: green both backends (one fresh holder per hop); post-fix shares one holder
			const char* src = R"(
				function down(int& x, int n) { if (n > 0) { down(x, n - 1); } else { x = 99; } }
				function go() -> int { var v = 0; down(v, 40); return v; }
				go();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)99, e->execute(src).as_int());
			}
		});

		test("ref_decl_shares_env_ref", [this]() {
			// pre-fix: green both backends (decl flatten re-targeted v directly)
			const char* src = R"(
				var v = 1;
				auto& g = v;
				auto& h = g;
				h = h + 3;
				v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)4, e->execute(src).as_int());
			}
		});

		test("ref_decl_element_constraint_dropped", [this]() {
			// D4v pin: auto& over a typed-array element drops the element-type constraint
			// (today's decl semantics), so the retyping write must keep succeeding
			const char* src = R"(
				array<int> arr = [1, 2, 3];
				auto& x = arr[1];
				x = "s";
				type_of(arr[1]);
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				check_eq(std::string("string"), e->execute(src).as<std::string>());
			}
		});

		test("ref_decl_element_ref_survives_realloc", [this]() {
			// Sanctioned fix (b) at the decl site: pre-fix auto& flattened the element ref
			// into a raw pointer, so the post-realloc write was lost (arr[1] stayed 8)
			const char* src = R"(
				var arr = [7, 8];
				auto& x = arr[1];
				for (int i = 0; i < 64; i = i + 1) { arr.push(0); }
				x = 42;
				arr[1];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)42, e->execute(src).as_int());
			}
		});

		test("ref_param_type_of_stable_through_hops", [this]() {
			// Wrapper-tag canary: sharing keeps the origin's reference tag; type_of must
			// keep reporting the referent type at every hop
			const char* src = R"(
				function leaf(int& x) -> string { return type_of(x); }
				function mid(int& y) -> string { return type_of(y) + ":" + leaf(y); }
				var v = 5;
				mid(v);
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				check_eq(std::string("int:int"), e->execute(src).as<std::string>());
			}
		});

		test("ref_escape_via_lambda_after_frame_death", [this]() {
			// pre-fix: "6|" both backends (the [&] capture snapshots the ref param's value,
			// so the escaped lambda works on its own copy). Sharing must not change this.
			const char* src = R"(
				var f = null;
				function trap(int& x) { f = [&]() { x = x + 1; return x; }; }
				function run() -> int { var v = 5; trap(v); return v; }
				run();
				var msg = "";
				var got = -1;
				try { got = f(); } catch (e) { msg = e; }
				to_string(got) + "|" + msg;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				check_eq(std::string("6|"), e->execute(src).as<std::string>());
			}
		});

		test("ref_param_calls_inside_coroutine_across_yields", [this]() {
			// Metadata invariant: inner ref-param calls complete before each yield, so the
			// suspended fiber never holds in-flight arg metadata
			const char* src = R"(
				function bump(int& t) { t = t + 1; }
				coroutine int co() {
					var a = 0;
					bump(a);
					yield a;
					bump(a);
					yield a;
					return a * 10;
				}
				auto c = co();
				auto r1 = c.resume();
				auto r2 = c.resume();
				auto r3 = c.resume();
				r1 * 100 + r2 * 10 + r3;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				check_eq((int64_t)140, e->execute(src).as_int());
			}
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
