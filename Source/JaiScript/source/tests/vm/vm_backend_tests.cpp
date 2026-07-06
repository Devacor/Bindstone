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

		// Fixed 2026-07 (demoreel finding 1): a plain-function call inside a method must
		// NOT clear the caller method scope's this-binding — field reads and this.x keep
		// working after the call, on both backends.
		test("plain_callee_keeps_caller_this", [this]() {
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
				check_eq((int64_t)5, e->execute(unqualified_field_read).as_int(),
					"unqualified field read after a plain-function call");
				check_eq((int64_t)5, e->execute(qualified_this_read).as_int(),
					"this.x after a plain-function call");
			}
		});

		// Stage 2 (compile-time lazy envs): plain in-loop callees whose bodies provably never
		// touch the per-call scope environment skip creating it. These pin the parity contract
		// on both backends and the compiler's fail-closed trigger table.
		test("lazy_env_callee_keeps_caller_this_from_method_top_scope", [this]() {
			// helper's body is lazy-eligible (no env created): the caller method's this
			// must survive the call on both backends
			const char* src = R"(
				int helper(int a) { return a + 1; }
				class L { int x = 5; int m() { helper(1); return x; } }
				var l = L();
				l.m();
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)5, e->execute(src).as_int(),
					"field read after a lazy-elided plain call");
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

		test("element_ref_arg_error_script_catchable_parity", [this]() {
			// OOB element-ref deref during call-ARGUMENT evaluation must be script-catchable
			// in the calling frame on BOTH backends (interpreter converts at visit_call; the
			// VM converts in-frame inside call-arg zones instead of popping the frame or
			// rethrowing at records_base).
			const char* srcs[] = {
				// top-level ref-param call, decl-ref arg (pre-fix: VM escaped execute() entirely)
				R"(
					var arr = [1, 2, 3];
					auto& x = arr[2];
					arr.pop();
					var msg = "";
					function f(int& t) { }
					try { f(x); } catch (e) { msg = e; }
					msg;
				)",
				// top-level value-param call
				R"(
					var arr = [1, 2, 3];
					auto& x = arr[2];
					arr.pop();
					var msg = "";
					function g(int t) { }
					try { g(x); } catch (e) { msg = e; }
					msg;
				)",
				// in-function range-for element arg (pre-fix: VM popped driver's frame, its try never fired)
				R"(
					var arr = [1, 2, 3];
					var msg = "";
					function readIt(int& x) { return x; }
					function driver() { for (auto& el : arr) { arr.pop(); arr.pop(); arr.pop(); try { readIt(el); } catch (e) { msg = e; } break; } }
					driver();
					msg;
				)",
				// method-call argument zone
				R"(
					var arr = [1, 2, 3];
					var msg = "";
					class C { int take(int v) { return v; } }
					function driver() { auto c = C(); for (auto& el : arr) { arr.pop(); arr.pop(); arr.pop(); try { c.take(el); } catch (e) { msg = e; } break; } }
					driver();
					msg;
				)",
			};
			for (const char* src : srcs) {
				std::string first;
				for (bool use_vm : {false, true}) {
					auto e = jai::engine::make();
					if (use_vm) { e->set_backend(jai::backend_type::vm); }
					auto msg = e->execute(src).as<std::string>();
					check_true(msg.find("removed array element") != std::string::npos,
						"arg-position OOB element ref caught by the calling frame's try");
					if (!use_vm) { first = msg; } else { check_eq(first, msg, "identical error text on both backends"); }
				}
			}
		});

		test("element_ref_callee_error_caught_at_depth2", [this]() {
			// Mid-call shrink two hops down a shared-holder chain: the store's re-resolve
			// error must unwind to the outermost script try identically on both backends.
			const char* src = R"(
				var arr = [1, 2, 3];
				var msg = "";
				function w(int& x) { arr.pop(); arr.pop(); arr.pop(); x = 5; }
				function relay(int& x) { w(x); }
				function driver() { for (auto& el : arr) { try { relay(el); } catch (e) { msg = e; } break; } }
				driver();
				msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("removed array element") != std::string::npos,
					"depth-2 write error unwinds to driver's try");
				if (!use_vm) { first = msg; } else { check_eq(first, msg, "identical error text on both backends"); }
			}
		});

		test("unwinding_skips_typed_return_conversion", [this]() {
			// An exception in flight across a typed-return frame must keep its text: the
			// interpreter's call epilogue used to run the -> int conversion on the junk
			// return slot and replace the error with "Type mismatch: expected int but got this".
			const char* src = R"(
				var arr = [1, 2, 3];
				var msg = "";
				function readIt(int& x) -> int { return x; }
				function wipeThenRead(int& x) -> int { arr.pop(); arr.pop(); arr.pop(); return readIt(x); }
				function driver() { for (auto& el : arr) { try { wipeThenRead(el); } catch (e) { msg = e; } break; } }
				driver();
				msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("removed array element") != std::string::npos,
					"typed-return frame does not mangle the in-flight error");
				if (!use_vm) { first = msg; } else { check_eq(first, msg, "identical error text on both backends"); }
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

		// Stage 2 (Tier 1): field/subscript/chain lvalue arguments bind to reference
		// parameters through the shared resolver (detail/ref_lvalue.hpp). Field refs pin
		// the instance, element refs pin the vector; typed fields/elements enforce their
		// bind-time tag on assign-through.
		test("tier1_bst_insert_by_ref", [this]() {
			// The headline: recursive insert through field refs, no return-reassign
			const char* src = R"(
				class TreeNode { int value = 0; TreeNode left = null; TreeNode right = null; TreeNode(int val) { value = val; } }
				function insertNode(var& node, int val) {
					if (node == null) { node = TreeNode(val); return; }
					if (val < node.value) { insertNode(node.left, val); } else { insertNode(node.right, val); }
				}
				function inorderSum(var node) -> int {
					if (node == null) { return 0; }
					return inorderSum(node.left) + node.value + inorderSum(node.right);
				}
				var root = null;
				insertNode(root, 8);
				insertNode(root, 4);
				insertNode(root, 12);
				insertNode(root, 2);
				insertNode(root, 6);
				insertNode(root, 10);
				insertNode(root, 14);
				inorderSum(root);
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)56, e->execute(src).as_int());
			}
		});

		test("tier1_field_ref_write_through", [this]() {
			const char* src = R"(
				class Box { int v = 1; }
				function set9(int& x) { x = 9; }
				var b = Box();
				set9(b.v);
				b.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)9, e->execute(src).as_int());
			}
		});

		test("tier1_chain_and_mixed_lvalue_args", [this]() {
			// a.b.c, arr[i].field, obj.arr[j] chains all resolve to the innermost node
			const char* src = R"(
				class Inner { int val = 1; }
				class Outer { Inner inner = null; var arr = [0, 0]; Outer() { inner = Inner(); } }
				function bump(int& x) { x = x + 10; }
				var o = Outer();
				bump(o.inner.val);
				var boxes = [Inner(), Inner()];
				bump(boxes[1].val);
				var j = 0;
				bump(o.arr[j]);
				o.inner.val * 10000 + boxes[1].val * 100 + o.arr[0];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)111110, e->execute(src).as_int());
			}
		});

		test("tier1_subscript_literal_and_identifier_index", [this]() {
			// Literal index, env-resolved identifier index, and slot-local identifier index
			const char* src = R"(
				function set(int& x, int val) { x = val; }
				var arr = [1, 2, 3];
				set(arr[0], 7);
				var gi = 2;
				set(arr[gi], 8);
				function driver() { var li = 1; set(arr[li], 9); }
				driver();
				arr[0] * 100 + arr[1] * 10 + arr[2];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)798, e->execute(src).as_int());
			}
		});

		test("tier1_this_field_ref_from_method", [this]() {
			// this.field as a ref arg: free-function callee and method callee. Results
			// observed from outside m() - a method's OWN this/field lookups after any
			// free-function call are broken at HEAD (pre-existing, both backends)
			const char* src = R"(
				function bump(int& x) { x = x + 1; }
				class Svc { void put(int& x, int val) { x = val; } }
				class C {
					int v = 5;
					int m() { bump(this.v); return 0; }
					int n(var s) { s.put(this.v, 40); return this.v; }
				}
				var c = C();
				c.m();
				var first = c.v;
				var second = c.n(Svc());
				first * 100 + second;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)640, e->execute(src).as_int(), use_vm ? "vm" : "interp");
			}
		});

		test("tier1_method_callee_field_ref_arg", [this]() {
			// enter_script_method builds the same lvalue metadata as plain calls
			const char* src = R"(
				class Box { int v = 0; }
				class Svc { void put(int& x, int val) { x = val; } }
				var b = Box();
				var s = Svc();
				s.put(b.v, 42);
				b.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)42, e->execute(src).as_int());
			}
		});

		test("tier1_shared_ptr_base_field_ref", [this]() {
			const char* src = R"(
				class N { int v = 1; N(int val) { v = val; } }
				function bump(int& x) { x = x + 1; }
				var sp = shared_ptr<N>(5);
				bump(sp.v);
				sp.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)6, e->execute(src).as_int());
			}
		});

		test("tier1_typed_field_wrong_type_errors", [this]() {
			const char* src = R"(
				class Box { int v = 1; }
				function put(var& x) { x = "nope"; }
				var b = Box();
				var msg = "";
				try { put(b.v); } catch (e) { msg = e; }
				msg + "|" + to_string(b.v);
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto msg = e->execute(src).as<std::string>();
				check_true(msg.find("Cannot assign 'string' to field of type 'int'") != std::string::npos,
					"typed field rejects retype through the ref");
				check_true(msg.find("|1") != std::string::npos, "field value untouched after rejected write");
				if (!use_vm) { first = msg; } else { check_eq(first, msg, "identical error text on both backends"); }
			}
		});

		test("tier1_typed_field_numeric_conversion_and_null", [this]() {
			// float converts into an int field like elements do; typed object fields
			// stay nullable through the ref (the `TreeNode left = null` idiom)
			const char* src = R"(
				class Node { int num = 1; Node next = null; }
				function put(var& x, var val) { x = val; }
				var n = Node();
				put(n.num, 2.7);
				n.next = Node();
				put(n.next, null);
				to_string(n.num) + "|" + type_of(n.next);
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto got = e->execute(src).as<std::string>();
				check_eq(std::string("2|null"), got);
				if (!use_vm) { first = got; } else { check_eq(first, got); }
			}
		});

		test("tier1_field_constraint_is_value_carried", [this]() {
			// Field typedness is value-carried (declared var/int both tag by the stored
			// value): an int-tagged field constrains the ref exactly like the in-method
			// identifier-assign path; an any-tagged field value binds unconstrained
			const char* src = R"(
				class Box { var v = 1; }
				function put(var& x) { x = "str"; }
				var b = Box();
				var msg = "";
				try { put(b.v); } catch (e) { msg = e; }
				var anyTagged = 2;
				b.v = anyTagged;
				put(b.v);
				to_string(b.v) + "|" + msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto got = e->execute(src).as<std::string>();
				check_true(got.find("str|") == 0, "any-tagged field value binds unconstrained");
				check_true(got.find("Cannot assign 'string' to field of type 'int'") != std::string::npos,
					"int-tagged field value constrains the ref");
				if (!use_vm) { first = got; } else { check_eq(first, got, "identical error text on both backends"); }
			}
		});

		test("tier1_typed_element_enforcement_through_param", [this]() {
			// Typed array element bound by f(a[i]): wrong type errors with the element
			// text; compound += goes through the same constraint machinery
			const char* src = R"(
				array<int> a = [1, 2, 3];
				function put(var& x) { x = "bad"; }
				function inc(var& x) { x += 10; }
				var msg = "";
				try { put(a[0]); } catch (e) { msg = e; }
				inc(a[1]);
				msg + "|" + to_string(a[0]) + "|" + to_string(a[1]);
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto got = e->execute(src).as<std::string>();
				check_true(got.find("Cannot assign 'string' to element of type 'int'") != std::string::npos,
					"typed element rejects retype through the bound ref");
				check_true(got.find("|1|12") != std::string::npos, "rejected write untouched, compound applied");
				if (!use_vm) { first = got; } else { check_eq(first, got, "identical error text on both backends"); }
			}
		});

		test("tier1_owner_pinned_past_caller_handle_drop", [this]() {
			// The holder pins the instance/vector: dropping the caller's last handle
			// mid-call must leave the ref writable (Debug 0xDD canary covers dangling)
			const char* src = R"(
				class Box { int v = 5; }
				var g = Box();
				var arr = [1, 2, 3];
				function fldWrite(int& x) { g = null; x = x + 1; return x; }
				function elemWrite(int& x) { arr = null; x = x + 1; return x; }
				var a = fldWrite(g.v);
				var b = elemWrite(arr[1]);
				a * 10 + b;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)63, e->execute(src).as_int());
			}
		});

		test("tier1_element_ref_realloc_and_shrink", [this]() {
			// Tier-1-bound element refs re-resolve container+index: writes survive
			// realloc, shrink errors instead of corrupting
			const char* src = R"(
				var arr = [7];
				var msg = "";
				function grow(int& x) { for (int i = 0; i < 64; i = i + 1) { arr.push(0); } x = 42; }
				function wipe(int& x) { while (arr.size() > 0) { arr.pop(); } x = 5; }
				grow(arr[0]);
				var grown = arr[0];
				try { wipe(arr[0]); } catch (e) { msg = e; }
				to_string(grown) + "|" + msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto got = e->execute(src).as<std::string>();
				check_true(got.find("42|") == 0, "write through the ref survives realloc");
				check_true(got.find("removed array element") != std::string::npos, "shrunk element write errors");
				if (!use_vm) { first = got; } else { check_eq(first, got, "identical error text on both backends"); }
			}
		});

		test("tier1_non_lvalue_exclusion_matrix", [this]() {
			// Everything outside the sanctioned lvalue shapes keeps today's exact error
			const char* setup = R"(
				class Box { int v = 1; int _get_area() { return v * 2; } }
				function f(int& x) { x = 0; }
				var b = Box();
				var arr = [1, 2, 3];
				var m = {"k": 1};
				var s = "hi";
				var msg = "";
			)";
			const char* cases[] = {
				"try { f(b.v + 1); } catch (e) { msg = e; } msg;",
				"try { f(arr.size()); } catch (e) { msg = e; } msg;",
				"try { f(5); } catch (e) { msg = e; } msg;",
				"try { f(m[\"k\"]); } catch (e) { msg = e; } msg;",
				"try { f(arr.size); } catch (e) { msg = e; } msg;",
				"try { f(b.area); } catch (e) { msg = e; } msg;",   // computed property: no backing field
			};
			for (bool use_vm : {false, true}) {
				for (const char* c : cases) {
					auto e = jai::engine::make();
					if (use_vm) { e->set_backend(jai::backend_type::vm); }
					auto msg = e->execute(std::string(setup) + c).as<std::string>();
					check_true(msg.find("Cannot pass non-lvalue to reference parameter") != std::string::npos,
						std::string("non-lvalue arg keeps the exact error: ") + c + " got: " + msg);
				}
				// String subscript args fail during argument EVALUATION (strings have no
				// [] operator) - pre-existing error, never reaches ref binding
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				auto msg = e->execute(std::string(setup) + "try { f(s[0]); } catch (e) { msg = e; } msg;").as<std::string>();
				check_true(msg.find("Subscript can only be used on") != std::string::npos,
					std::string("string subscript arg keeps the eval-time error, got: ") + msg);
			}
		});

		test("tier1_bind_time_ordering_pinned", [this]() {
			// Chain resolution happens at bind time, after ALL args evaluate: a later
			// arg that swaps the chain spine changes what binds (identifier precedent)
			const char* src = R"(
				class B { int v = 1; }
				class A { B b = null; A() { b = B(); } }
				var a = A();
				var keep = a.b;
				function swapB() -> int { a.b = B(); return 0; }
				function f(int& x, int ignored) { x = 50; }
				f(a.b.v, swapB());
				a.b.v * 10 + keep.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)501, e->execute(src).as_int());
			}
		});

		test("tier1_field_ref_pass_through_hop", [this]() {
			// Tier 1 x Tier 2: the bound field ref shares its holder through further hops
			const char* src = R"(
				class Box { int v = 1; }
				function leaf(int& x) { x = x + 1; }
				function mid(int& x) { leaf(x); x = x + 10; }
				var b = Box();
				mid(b.v);
				b.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)12, e->execute(src).as_int());
			}
		});

		test("tier1_ref_param_type_forms", [this]() {
			// var&, auto&, int&, ClassName&, array<int>& all parse, bind, and write through
			const char* src = R"(
				class P { int v = 1; }
				function f1(var& x) { x = x + 1; }
				function f2(auto& x) { x = x + 2; }
				function f3(int& x) { x = x + 3; }
				function f4(P& p) { p = P(); p.v = 99; }
				function f5(array<int>& a) { a[0] = 9; }
				class H { int n = 10; P p = null; var arr = null; H() { p = P(); arr = [0]; } }
				var h = H();
				f1(h.n);
				f2(h.n);
				f3(h.n);
				f4(h.p);
				f5(h.arr);
				h.n * 1000 + h.p.v * 10 + h.arr[0];
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)16999, e->execute(src).as_int());
			}
		});

		test("tier1_hot_reload_field_ref", [this]() {
			// Field refs re-resolve by (pinned instance, field id): a kept field stays
			// writable across a mid-call reload; a removed field errors on write
			const char* src = R"(
				class Box { int v = 1; }
				var b = Box();
				var msg = "";
				function keepWrite(var& x) { reloadKeep(); x = 42; }
				function dropWrite(var& x) { reloadDrop(); x = 43; }
				keepWrite(b.v);
				var kept = b.v;
				b2 = Box();
				try { dropWrite(b2.v); } catch (e) { msg = e; }
				to_string(kept) + "|" + msg;
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				jai::engine* raw = e.get();
				e->add_function("reloadKeep", [raw]() -> jai::script_int {
					raw->execute("class Box { int v = 1; float extra = 0.0; }");
					return 0;
				});
				e->add_function("reloadDrop", [raw]() -> jai::script_int {
					raw->execute("class Box { float other = 0.0; }");
					return 0;
				});
				e->execute("var b2 = null;");
				auto got = e->execute(src).as<std::string>();
				check_true(got.find("42|") == 0, "kept field writable through the live ref after reload");
				check_true(got.find("Reference to a removed field") != std::string::npos,
					"removed field errors on write through the ref");
				if (!use_vm) { first = got; } else { check_eq(first, got, "identical error text on both backends"); }
			}
		});

		test("tier1_member_args_to_ref_free_functions_unaffected", [this]() {
			// R5: member/subscript args to REF-FREE callees consume no lvalue metadata
			// and stay pure rvalue reads, surrounded by live ref-param calls
			const char* src = R"(
				class Box { int v = 3; }
				int plain(int a) { return a + 1; }
				function bump(int& t) { t = t + 1; }
				var b = Box();
				var arr = [5];
				var r1 = plain(b.v);
				bump(b.v);
				var r2 = plain(arr[0]);
				bump(b.v);
				r1 * 1000 + r2 * 10 + b.v;
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				check_eq((int64_t)4065, e->execute(src).as_int());
			}
		});

		test("tier1_type_of_and_read_back_through_bound_field_ref", [this]() {
			// Reads through the bound ref see live field state; type_of reports the referent
			const char* src = R"(
				class Box { int v = 7; }
				function probe(int& x) -> string { var t = type_of(x); x = x + 1; return t + ":" + to_string(x); }
				var b = Box();
				probe(b.v) + ":" + to_string(b.v);
			)";
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				check_eq(std::string("int:8:8"), e->execute(src).as<std::string>());
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
			check_true(listing.find("FUSED_CMP_JUMP") != std::string::npos, "fused conditional jump present");
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

		// Tier-1 reference args x coroutines x deep recursion: dual-backend differentials
		// (parity is checked as interp == vm; absolute values pin sanctioned semantics)

		auto run_both_backends = [](const char* src) {
			std::string out[2];
			int idx = 0;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				e->execution_budget(0);
				try { out[idx] = e->execute(src).to_string(); }
				catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
				++idx;
			}
			return std::make_pair(out[0], out[1]);
		};

		test("tier1_field_ref_call_inside_coroutine_body", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 0; }
				function bump(int& x) { x = x + 1; }
				coroutine int gen(var b) {
					bump(b.v);
					yield b.v;
					bump(b.v);
					yield b.v;
					bump(b.v);
					return b.v;
				}
				var box = Box();
				auto g = gen(box);
				auto r1 = g.resume();
				auto r2 = g.resume();
				auto r3 = g.resume();
				r1 * 1000 + r2 * 100 + r3 * 10 + box.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("1230"), i_out, "interp: tier1 field ref inside coroutine body");
			check_eq(std::string("1230"), v_out, "vm: tier1 field ref inside coroutine body");
		});

		test("tier1_global_refs_across_resumes_with_mutation", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 10; }
				var box = Box();
				var arr = [1, 2, 3];
				function bump(int& x) { x = x + 100; }
				coroutine int gen() {
					bump(arr[1]);
					yield arr[1];
					bump(box.v);
					yield box.v;
					return arr[1] + box.v;
				}
				auto g = gen();
				auto r1 = g.resume();
				arr.push(4); arr.push(5); arr.push(6); arr.push(7);
				box.v = 50;
				auto r2 = g.resume();
				auto r3 = g.resume();
				r1 * 100000 + r2 * 100 + r3;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("10215252"), i_out, "interp: global tier1 refs across resumes");
			check_eq(std::string("10215252"), v_out, "vm: global tier1 refs across resumes");
		});

		// Coroutine parameters always bind by VALUE (both backends): resume runs far from
		// the creation-site lvalues, so int& behaves as a value param here
		test("coroutine_ref_param_lvalue_binds_by_value", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 7; }
				var box = Box();
				coroutine int gen(int& x) { x = x + 1; yield x; x = x + 1; return x; }
				auto g = gen(box.v);
				auto r1 = g.resume();
				auto r2 = g.resume();
				r1 * 1000 + r2 * 10 + box.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("8097"), i_out, "interp: coroutine int& binds the creation-time value");
			check_eq(std::string("8097"), v_out, "vm: coroutine int& binds the creation-time value");
		});

		test("coroutine_ref_param_identifier_binds_by_value", [this, run_both_backends]() {
			const char* src = R"(
				var y = 7;
				coroutine int gen(int& x) { x = x + 1; yield x; x = x + 1; return x; }
				auto g = gen(y);
				auto r1 = g.resume();
				auto r2 = g.resume();
				r1 * 1000 + r2 * 10 + y;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("8097"), i_out, "interp: coroutine int& binds the identifier's value");
			check_eq(std::string("8097"), v_out, "vm: coroutine int& binds the identifier's value");
		});

		test("stale_metadata_first_resume_lvalue", [this, run_both_backends]() {
			// resume() during argument evaluation of an enclosing call: the enclosing
			// call's arg metadata must NOT leak into the coroutine's first-resume bind
			const char* src = R"(
				class Box { int v = 7; }
				var box = Box();
				coroutine int gen(int& x) { x = 99; yield 1; return 2; }
				function take(var a, var b) -> int { return a + b; }
				auto g = gen(555);
				take(box.v, g.resume());
				box.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("7"), i_out, "interp: stale lvalue metadata must not leak into first resume");
			check_eq(std::string("7"), v_out, "vm: stale lvalue metadata must not leak into first resume");
		});

		test("stale_metadata_first_resume_identifier", [this, run_both_backends]() {
			const char* src = R"(
				var y = 7;
				coroutine int gen(int& x) { x = 99; yield 1; return 2; }
				function take(var a, var b) -> int { return a + b; }
				auto g = gen(555);
				take(y, g.resume());
				y;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("7"), i_out, "interp: stale identifier metadata must not leak into first resume");
			check_eq(std::string("7"), v_out, "vm: stale identifier metadata must not leak into first resume");
		});

		test("deep_tier1_bst_sequential", [this, run_both_backends]() {
			// Worst-case (right-spine) ref-BST: 200 sequential inserts = ~20k calls with a
			// Tier-1 lvalue arg per hop; superlinear per-call cost turns this into minutes
			const char* src = R"(
				class TreeNode { int value = 0; TreeNode left = null; TreeNode right = null; TreeNode(int v) { value = v; } }
				function insertNode(var& node, int val) {
					if (node == null) { node = TreeNode(val); return; }
					if (val < node.value) { insertNode(node.left, val); } else { insertNode(node.right, val); }
				}
				var root = null;
				for (int i = 1; i <= 200; ++i) { insertNode(root, i); }
				var cur = root;
				var count = 0;
				var last = 0;
				while (cur != null) { count = count + 1; last = cur.value; cur = cur.right; }
				count * 10000 + last;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("2000200"), i_out, "interp: 200-deep tier1 ref BST");
			check_eq(std::string("2000200"), v_out, "vm: 200-deep tier1 ref BST");
		});

		test("deep_tier2_passthrough_800", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 0; }
				function deep(int& x, int d) { if (d == 0) { x = 42; return; } deep(x, d - 1); }
				var b = Box();
				deep(b.v, 800);
				b.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("42"), i_out, "interp: 800-hop tier2 ref pass-through");
			check_eq(std::string("42"), v_out, "vm: 800-hop tier2 ref pass-through");
		});

		test("yield_in_nested_ref_param_call", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 1; }
				var box = Box();
				function pumpYield(int& x) { x = x + 1; yield x; x = x + 1; }
				coroutine int gen() { pumpYield(box.v); return box.v; }
				auto g = gen();
				auto r1 = g.resume();
				auto r2 = g.resume();
				r1 * 100 + r2 * 10 + box.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(i_out, v_out, "yield below a ref-param frame must agree across backends");
		});

		test("tier1_ref_call_in_loop_yield_replay", [this, run_both_backends]() {
			const char* src = R"(
				class Box { int v = 0; }
				var box = Box();
				function bump(int& x) { x = x + 1; }
				coroutine int gen() {
					for (int i = 0; i < 3; ++i) {
						bump(box.v);
						yield box.v;
					}
					return 0;
				}
				auto g = gen();
				auto a = g.resume();
				auto b = g.resume();
				auto c = g.resume();
				a * 1000 + b * 100 + c * 10 + box.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("1233"), i_out, "interp: tier1 ref call replayed inside loop+yield");
			check_eq(std::string("1233"), v_out, "vm: tier1 ref call replayed inside loop+yield");
		});

		// The generic-binary custom-operator consult used to walk the caller env chain,
		// which grows with recursion depth for eager-env callees - per-call cost became
		// O(depth) and the ref-BST superlinear. Scaling ratio, not absolute time: 8x the
		// work must stay near 8x the time (the walk made it ~250x).
		test("recursive_generic_binary_cost_stays_linear", [this]() {
			auto descent_ms = [this](bool use_vm, int depth) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				e->execution_budget(0);
				e->execute(
					"class Box { int v = 0; }\nvar b = Box();\n"
					"function f(var& x, int d) { if (d < -1) { return; } if (d == 0) { x = x + 1; return; } f(b.v, d - 1); }");
				std::string run = "b.v = 0; for (int rep = 0; rep < 10; ++rep) { f(b.v, " + std::to_string(depth) + "); } b.v;";
				auto t0 = std::chrono::steady_clock::now();
				auto result = e->execute(run);
				double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				check_eq((int64_t)10, result.as_int(), "descent workload result");
				return ms;
			};
			for (bool use_vm : {false, true}) {
				double small = std::max(descent_ms(use_vm, 50), 1.0);
				double big = descent_ms(use_vm, 400);
				check_lt(big, small * 40.0, use_vm ? "vm: deep descent must scale linearly"
				                                   : "interp: deep descent must scale linearly");
			}
		});

		// Ref-store error catchability is uniform: a removed element/field surfaces as the
		// same caller-frame-catchable throw whether or not the bound container was typed;
		// constraint violations are checked errors, catchable in the throwing frame.
		test("ref_store_error_catchability_uniform", [this, run_both_backends]() {
			const char* plain_store = R"(
				array<int> arr = [1, 2, 3];
				function f(int& x) {
					while (arr.size() > 0) { arr.pop(); }
					try { x = 5; } catch (e) { return "inner:" + e; }
					return "no-error";
				}
				var out = "";
				try { out = f(arr[2]); } catch (e) { out = "outer:" + e; }
				out;
			)";
			auto [i_plain, v_plain] = run_both_backends(plain_store);
			check_true(i_plain.find("outer:") == 0, "interp: plain ref store escapes to caller");
			check_true(i_plain.find("removed array element") != std::string::npos, "interp: plain ref store error text");
			check_eq(i_plain, v_plain, "plain ref store catchability parity");

			const char* compound_store = R"(
				array<int> arr = [1, 2, 3];
				function f(int& x) {
					while (arr.size() > 0) { arr.pop(); }
					try { x += 1; } catch (e) { return "inner:" + e; }
					return "no-error";
				}
				var out = "";
				try { out = f(arr[2]); } catch (e) { out = "outer:" + e; }
				out;
			)";
			auto [i_comp, v_comp] = run_both_backends(compound_store);
			check_eq(i_plain, i_comp, "interp: constrained compound matches plain store catchability");
			check_eq(i_comp, v_comp, "constrained compound catchability parity");

			const char* type_error = R"(
				array<int> arr = [1, 2, 3];
				function f(int& x) {
					try { x = "nope"; } catch (e) { return "inner:" + e; }
					return "no-error";
				}
				var out = "";
				try { out = f(arr[1]); } catch (e) { out = "outer:" + e; }
				out;
			)";
			auto [i_type, v_type] = run_both_backends(type_error);
			check_true(i_type.find("inner:") == 0, "interp: constraint violations stay catchable in the throwing frame");
			check_true(i_type.find("Cannot assign 'string' to element of type 'int'") != std::string::npos,
				"interp: constraint violation error text");
			check_eq(i_type, v_type, "constraint violation catchability parity");
		});

		// Derived-to-base through a Tier-1 field ref: the constraint accepts subtypes,
		// like typed parameter conversion and every direct field-write path do
		test("tier1_field_ref_accepts_subtype", [this, run_both_backends]() {
			const char* src = R"(
				class Animal { int id = 0; }
				class Dog : Animal { int extra = 1; }
				class Holder { Animal pet = null; Holder() { pet = Animal(); } }
				function put(var& x, var val) { x = val; }
				var h = Holder();
				put(h.pet, Dog());
				h.pet.extra;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("1"), i_out, "interp: Dog assigns through Animal-constrained field ref");
			check_eq(std::string("1"), v_out, "vm: Dog assigns through Animal-constrained field ref");
		});

		// A var local's tag is 'any'; the constraint check resolves the actual instance
		// class instead of rejecting on the declaration tag
		test("tier1_field_ref_accepts_var_held_instance", [this, run_both_backends]() {
			const char* src = R"(
				class Node { int v = 1; }
				class Holder { Node n = null; Holder() { n = Node(); } }
				function put(var& x, var val) { x = val; }
				var h = Holder();
				var src = Node();
				src.v = 42;
				put(h.n, src);
				var sp = shared_ptr<Node>();
				sp.v = 7;
				var h2 = Holder();
				put(h2.n, sp);
				h.n.v * 100 + h2.n.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("4207"), i_out, "interp: var-held Node and shared_ptr<Node> assign through Node field ref");
			check_eq(std::string("4207"), v_out, "vm: var-held Node and shared_ptr<Node> assign through Node field ref");
		});

		// Genuinely incompatible classes still error, naming the ACTUAL class (never 'any')
		test("tier1_field_ref_rejects_unrelated_class_with_actual_name", [this, run_both_backends]() {
			const char* src = R"(
				class Node { int v = 1; }
				class Stranger { int s = 2; }
				class Holder { Node n = null; Holder() { n = Node(); } }
				function put(var& x, var val) { x = val; }
				var h = Holder();
				var wrong = Stranger();
				var out = "";
				try { put(h.n, wrong); } catch (e) { out = "" + e; }
				out;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_true(i_out.find("Cannot assign 'Stranger' to field of type 'Node'") != std::string::npos,
				"interp: rejection names the actual class");
			check_eq(i_out, v_out, "rejection message parity");
		});

		// A script destructor firing at callee-frame pop used to segfault the interpreter:
		// call_stack_.pop_back() released the frame's locals mid-mutation and the dtor's
		// reentrant call_function grew the vector under it. Teardown now destroys the
		// frame's locals after the pop completes (VM stable-record parity).
		test("script_dtor_at_frame_pop_reentrancy", [this, run_both_backends]() {
			const char* plain = R"(
				class Box { int v = 0; ~Box() { } }
				function f() -> int { var b = Box(); return 1; }
				f();
			)";
			auto [i_plain, v_plain] = run_both_backends(plain);
			check_eq(std::string("1"), i_plain, "interp: local dtor at frame pop");
			check_eq(i_plain, v_plain, "plain dtor-at-pop parity");

			// Ref-pinned owner dropped mid-call: the holder keeps the instance alive, so
			// the dtor fires at callee-frame pop, after the write and before the caller
			// resumes - same ordering on both backends
			const char* pinned = R"(
				var log = "";
				class Box { int v = 5; ~Box() { log = log + "D"; } }
				var g = Box();
				function f(int& x) { g = null; log = log + "a"; x = 7; log = log + "b"; return x; }
				var r = f(g.v);
				log = log + "c";
				if (r == 7) { log = log + "!"; }
				log;
			)";
			auto [i_pin, v_pin] = run_both_backends(pinned);
			check_eq(std::string("abDc!"), i_pin, "interp: dtor fires at frame pop, ref write lands");
			check_eq(i_pin, v_pin, "pinned dtor ordering parity");
		});

		// dynamic_binder .property() members (stdlib pair, engine objects) have a
		// class-level field default but NO instance fields_ node: binding must keep the
		// exact non-lvalue error instead of lazily inserting a dead shadow field that
		// swallows writes
		test("tier1_cpp_property_member_keeps_non_lvalue_error", [this]() {
			const char* src = R"(
				var m = {"a": 1, "b": 2};
				function f(int& x) { x = 77; }
				var msg = "";
				for (var& kv : m) {
					try { f(kv.second); } catch (e) { msg = "" + e; }
					kv.second = 55;
					break;
				}
				msg + "|" + to_string(m["a"]);
			)";
			std::string first;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				jai::stdlib::register_all(e);
				auto got = e->execute(src).as<std::string>();
				check_true(got.find("Cannot pass non-lvalue to reference parameter") != std::string::npos,
					std::string("C++ property member keeps the exact error, got: ") + got);
				check_true(got.find("|55") != std::string::npos, "direct property write still reaches the map");
				if (!use_vm) { first = got; } else { check_eq(first, got, "identical behavior on both backends"); }
			}
		});

		// shared_ptr-tagged field constraint: raw same-class/subtype instances are
		// accepted (every direct field-write path accepts them), and rejections render
		// the constraint in full instead of "Cannot assign 'S' to field of type 'S'"
		test("tier1_shared_ptr_field_constraint_accepts_same_class", [this, run_both_backends]() {
			const char* src = R"(
				class S { int x = 1; }
				class T : S { int t = 2; }
				class H { S p = null; }
				function put(var& x, var val) { x = val; }
				var h1 = H(); h1.p = shared_ptr<S>();
				var s2 = S(); s2.x = 9;
				put(h1.p, s2);
				var h2 = H(); h2.p = shared_ptr<S>();
				put(h2.p, T());
				var h3 = H(); h3.p = shared_ptr<S>();
				var msg = "";
				try { put(h3.p, 5); } catch (e) { msg = "" + e; }
				var summary = "";
				if (h1.p.x == 9) { summary = summary + "A"; }
				if (h2.p.t == 2) { summary = summary + "B"; }
				summary + "|" + msg;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_true(i_out.find("AB|") == 0, std::string("interp: raw same-class and subtype accepted, got: ") + i_out);
			check_true(i_out.find("Cannot assign 'int' to field of type 'shared_ptr<S>'") != std::string::npos,
				"interp: rejection names the full shared_ptr constraint");
			check_eq(i_out, v_out, "shared_ptr field constraint parity");
		});

		// Element refs and direct subscript assign are ONE gate: the same store into the
		// same typed array passes or fails with byte-identical text on both routes
		// (the ref gate used to accept subtypes/var-held instances the direct gate
		// rejects, and named 'any'-tagged values by their actual class)
		test("tier1_element_ref_gate_matches_direct_subscript", [this, run_both_backends]() {
			const char* src = R"(
				class Animal { int id = 0; }
				class Dog : Animal { int extra = 1; }
				class Box { int b = 0; }
				function put(var& x, var val) { x = val; }
				array<Animal> pen = [Animal()];
				array<int> ints = [1, 2, 3];
				var va = Animal();
				var vb = Box();
				var d1 = ""; var r1 = "";
				try { pen[0] = Dog(); } catch (e) { d1 = "" + e; }
				try { put(pen[0], Dog()); } catch (e) { r1 = "" + e; }
				var d2 = ""; var r2 = "";
				try { pen[0] = va; } catch (e) { d2 = "" + e; }
				try { put(pen[0], va); } catch (e) { r2 = "" + e; }
				var d3 = ""; var r3 = "";
				try { ints[0] = vb; } catch (e) { d3 = "" + e; }
				try { put(ints[1], vb); } catch (e) { r3 = "" + e; }
				d1 + "&" + r1 + "&" + d2 + "&" + r2 + "&" + d3 + "&" + r3;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			auto split = [](const std::string& s) {
				std::vector<std::string> parts;
				size_t pos = 0;
				while (true) {
					size_t amp = s.find('&', pos);
					parts.push_back(amp == std::string::npos ? s.substr(pos) : s.substr(pos, amp - pos));
					if (amp == std::string::npos) { break; }
					pos = amp + 1;
				}
				return parts;
			};
			auto parts = split(i_out);
			check_eq((size_t)6, parts.size(), "six error captures");
			check_true(parts[0].find("Cannot assign 'Dog' to element of type 'Animal'") != std::string::npos,
				std::string("direct gate rejects subtype, got: ") + parts[0]);
			check_eq(parts[0], parts[1], "subtype: ref matches direct");
			check_true(parts[2].find("Cannot assign 'any' to element of type 'Animal'") != std::string::npos,
				std::string("direct gate rejects var-held instance, got: ") + parts[2]);
			check_eq(parts[2], parts[3], "var-held: ref matches direct");
			check_true(parts[4].find("Cannot assign 'any' to element of type 'int'") != std::string::npos,
				std::string("direct gate names var tags 'any', got: ") + parts[4]);
			check_eq(parts[4], parts[5], "naming: ref matches direct");
			check_eq(i_out, v_out, "element gate parity");
		});

		auto run_both_backends_stdlib = [](const char* src) {
			std::string out[2];
			int idx = 0;
			for (bool use_vm : {false, true}) {
				auto e = jai::engine::make();
				if (use_vm) { e->set_backend(jai::backend_type::vm); }
				e->execution_budget(0);
				stdlib::register_all(*e);
				try { out[idx] = e->execute(src).to_string(); }
				catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
				++idx;
			}
			return std::make_pair(out[0], out[1]);
		};

		// Tier 3(a): the primitive bind fast path must be invisible - tagging, retype
		// freedom, and caller isolation identical whether the gate takes (primitive
		// arg) or not (string arg takes the full conversion path)
		test("tier3_var_param_bind_semantics", [this, run_both_backends_stdlib]() {
			const char* src = R"(
				function probe(var x) -> string {
					var before = type_of(x);
					x = [1, 2];
					return before + ">" + type_of(x);
				}
				function clobber(var x) { x = 999; }
				var i = 7; var f = 1.5; var b = true; var c = 'q'; var s = "str";
				var out = probe(i) + "|" + probe(f) + "|" + probe(b) + "|" + probe(c) + "|" + probe(s);
				clobber(i); clobber(s);
				out + "|" + i + "|" + s;
			)";
			auto [i_out, v_out] = run_both_backends_stdlib(src);
			check_eq(std::string("int>array|float>array|bool>array|char>array|string>array|7|str"), i_out,
				"interp var-param bind semantics");
			check_eq(i_out, v_out, "var-param bind parity");
		});

		test("tier3_int_param_bind_semantics", [this, run_both_backends_stdlib]() {
			const char* src = R"(
				function take(int x) -> string {
					var t = type_of(x);
					x = x + 1;
					return t + ":" + x;
				}
				var a = 5;
				var r1 = take(a);
				var r2 = take(2.9);
				r1 + "|" + r2 + "|" + a;
			)";
			auto [i_out, v_out] = run_both_backends_stdlib(src);
			check_eq(i_out, v_out, "int-param bind parity");
			check_true(i_out.find("|5") == i_out.size() - 2, std::string("caller local isolated, got: ") + i_out);
			check_true(i_out.find("int:6|") == 0, std::string("int arg binds unconverted, got: ") + i_out);
		});

		// Tier 3(b): the exact-class-match conversion skip must keep value semantics -
		// exact match still deep-clones, subclass and shared_ptr args keep the full path
		test("tier3_exact_class_param_still_clones", [this, run_both_backends]() {
			const char* src = R"(
				class P { int v = 1; }
				class Q : P { int w = 2; }
				function bump(P p) -> int { p.v = p.v + 10; return p.v; }
				var a = P(); a.v = 5;
				var r1 = bump(a);
				var q = Q(); q.v = 3;
				var r2 = bump(q);
				var sp = shared_ptr<P>(); sp.v = 4;
				var r3 = bump(sp);
				function share(shared_ptr<P> p) { p.v = p.v + 100; }
				auto sh = shared_ptr<P>(); sh.v = 7;
				share(sh);
				"" + r1 + "," + a.v + "|" + r2 + "," + q.v + "|" + r3 + "," + sp.v + "|" + sh.v;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("15,5|13,3|14,4|107"), i_out, "interp exact-class clone semantics");
			check_eq(i_out, v_out, "exact-class clone parity");
		});

		// Tier 3(c): a throw during callee setup (default-arg eval / param conversion)
		// restores the caller environment - script keeps running and sees caller locals
		test("tier3_env_restored_after_throwing_bind", [this, run_both_backends]() {
			const char* src = R"(
				class NC { int z = 0; }
				function boom(int n, int d = 1 / 0) -> int { return n + d; }
				function wants(int x) -> int { return x; }
				var keep = 40;
				var msg = "";
				try { boom(1); } catch (e) { msg = "caught"; }
				keep = keep + 1;
				try { wants(NC()); } catch (e) { msg = msg + "+caught2"; }
				keep = keep + 1;
				msg + ":" + keep;
			)";
			auto [i_out, v_out] = run_both_backends(src);
			check_eq(std::string("caught+caught2:42"), i_out, "interp env restored after throwing bind");
			check_eq(i_out, v_out, "throwing-bind parity");
		});
	}
};

} // namespace jai::foundry::tests

using vm_backend_tests_alias = jai::foundry::tests::vm_backend_tests;
FOUNDRY_REGISTER(vm_backend_tests_alias)
