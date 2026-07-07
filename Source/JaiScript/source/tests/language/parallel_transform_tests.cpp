// parallel_transform v0 (docs/parallel_design.md; prove_or_serial §5 - contract A).
// Determinism: same inputs => identical output at every thread count, both backends.
// Admission violations ERROR with location text - never silently serial.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class parallel_transform_tests : public suite {
public:
	parallel_transform_tests() : suite("Parallel Transform") {}

	// Deep structural equality: script_value operator== compares container types by
	// held-pointer identity, so nested arrays/maps need an element-wise recursion.
	static bool deep_equal(const script_value& a, const script_value& b) {
		if (a.is_array() && b.is_array()) {
			const auto& av = a.as_array();
			const auto& bv = b.as_array();
			if (av.size() != bv.size()) { return false; }
			for (size_t i = 0; i < av.size(); ++i) {
				if (!deep_equal(av[i], bv[i])) { return false; }
			}
			return true;
		}
		if (a.is_map() && b.is_map()) {
			const auto& am = a.as_map();
			const auto& bm = b.as_map();
			if (am.size() != bm.size()) { return false; }
			auto bi = bm.begin();
			for (auto ai = am.begin(); ai != am.end(); ++ai, ++bi) {
				if (!deep_equal(ai->first, bi->first) || !deep_equal(ai->second, bi->second)) { return false; }
			}
			return true;
		}
		return a == b;
	}

	static bool arrays_equal(const script_value& a, const script_value& b) {
		return deep_equal(a, b);
	}

	// Runs `source` (which must return {parallel, serial} results as a two-element
	// array) at several worker counts and checks parallel == serial for each.
	void check_matches_serial_at(std::shared_ptr<engine> e, const std::string& source,
	                             std::initializer_list<size_t> worker_counts) {
		for (size_t workers : worker_counts) {
			e->parallel_thread_count(workers);
			auto r = e->execute(source);
			check_true(r.is_array());
			const auto& pair = r.as_array();
			check_eq((size_t)2, pair.size());
			check_true(arrays_equal(pair[0], pair[1]));
		}
	}

	void forge_tests() override {
		test("int_transform_matches_serial_1_2_8", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 200; i++) { a.push(i); }
				int f(int x) { return x * 3 - 1; }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("string_transform_with_methods_matches_serial", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 64; i++) { a.push("item_" + to_string(i * 7)); }
				string f(string s) {
					var upper_len = s.size() * 2;
					return s.substr(0, 4) + "|" + to_string(upper_len);
				}
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("float_math_whitelist_matches_serial", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 128; i++) { a.push(1.0 + i * 0.37); }
				float f(float x) { return sqrt(x) * 2.0 + pow(x, 0.25) - floor(x); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("byte_identical_across_thread_counts", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 100; i++) { a.push(0.1 + i * 1.7); }
				float f(float x) { return sqrt(x) / (x + 0.5) * 3.14159; }
				return parallel_transform(a, f);
			)";
			e->parallel_thread_count(1);
			auto r1 = e->execute(src);
			e->parallel_thread_count(2);
			auto r2 = e->execute(src);
			e->parallel_thread_count(8);
			auto r8 = e->execute(src);
			check_true(arrays_equal(r1, r2));
			check_true(arrays_equal(r1, r8));
		});

		test("nested_array_and_map_elements", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 40; i++) { a.push([i, i * 2, i * 3]); }
				int f(var row) {
					var total = 0;
					for (auto& v : row) { total += v; }
					return total;
				}
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 4});

			const std::string map_src = R"(
				var a = [];
				for (var i = 0; i < 32; i++) { a.push({"x": i, "y": i * i}); }
				int f(var m) { return m["x"] + m["y"]; }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, map_src, {1, 4});
		});

		test("transitive_calls_and_recursion", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				int helper(int x) { return x % 7 + 1; }
				int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
				int f(int x) { return fib(helper(x)) * 10 + helper(x); }
				var a = [];
				for (var i = 0; i < 60; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 4});
		});

		test("fn_result_arrays", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				var a = [];
				for (var i = 0; i < 24; i++) { a.push(i); }
				var f(int x) { return [x, x * x, "n" + to_string(x)]; }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			jai::stdlib::register_all(*e);
			check_matches_serial_at(e, src, {1, 4});
		});

		test("thread_count_builtin", [this]() {
			auto e = make_engine();
			auto r = e->execute("return thread_count();");
			check_ge(r.as_int(), (int64_t)1);
			e->parallel_thread_count(3);
			check_eq((int64_t)3, e->execute("return thread_count();").as_int());
		});

		test("empty_and_small_arrays", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(8);
			auto r = e->execute(R"(
				int f(int x) { return x + 1; }
				var empty_out = parallel_transform([], f);
				var tiny_out = parallel_transform([1, 2, 3], f);
				return [empty_out.size(), tiny_out[0], tiny_out[2]];
			)");
			const auto& vals = r.as_array();
			check_eq((int64_t)0, vals[0].as_int());
			check_eq((int64_t)2, vals[1].as_int());
			check_eq((int64_t)4, vals[2].as_int());
		});

		// === Admission: contract A - violations ERROR (never silent-serial) ===

		test("rejects_enclosing_write", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				var g = 0;
				int f(int x) { g = x; return x; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("parallel_transform: body writes enclosing state 'g'") != std::string::npos);
			check_true(msg.find(" at ") != std::string::npos);   // location suffix
		});

		test("rejects_enclosing_read", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				var scale = 4;
				int f(int x) { return x * scale; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("body reads enclosing state 'scale'") != std::string::npos);
		});

		test("rejects_host_call_print", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				int f(int x) { print(x); return x; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("host function 'print' is not callable in a parallel body") != std::string::npos);
		});

		test("rejects_yield_and_lambda", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				coroutine int f(int x) { yield x; return x; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			// A coroutine's minted value is not a plain script function payload
			auto msg = r.as<std::string>();
			check_true(msg.find("parallel_transform") != std::string::npos);

			auto r2 = e->execute(R"(
				int f(int x) {
					auto g = [](int y) { return y * 2; };
					return g(x);
				}
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r2.as<std::string>().find("lambdas are not allowed in a parallel body") != std::string::npos);
		});

		test("rejects_dynamic_callee_and_ref_param", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				int f(int x) { var h = 5; return h(x); }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("call target 'h' is not statically resolvable") != std::string::npos);

			auto r2 = e->execute(R"(
				int f(int& x) { return x; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r2.as<std::string>().find("may not take its element by reference") != std::string::npos);
		});

		test("rejects_object_elements", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				class P { int x = 1; }
				int f(var v) { return 1; }
				var a = [];
				a.push(P());
				try { parallel_transform(a, f); } catch (e) { return e; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("parallel_transform: element 0") != std::string::npos);
			check_true(msg.find("not value-semantic") != std::string::npos);
		});

		test("rejects_nested_parallel_transform", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				int g(int y) { return y; }
				int f(int x) { parallel_transform([1], g); return x; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("host function 'parallel_transform' is not callable in a parallel body") != std::string::npos);
		});

		test("rejects_member_access", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				int f(var x) { return x.field; }
				try { parallel_transform([1, 2, 3], f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("member access is not allowed in a parallel body") != std::string::npos);
		});

		test("rejects_impure_weight_fn", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				var seen = 0;
				int f(int x) { return x; }
				float w(int x) { seen = seen + 1; return 1.0; }
				try { parallel_transform([1, 2, 3], f, w); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("body writes enclosing state 'seen'") != std::string::npos);
		});

		// === Errors inside the region ===

		test("first_error_in_iteration_order_wins", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(8);
			auto r = e->execute(R"(
				int f(int x) {
					if (x == 90) { throw "boom90"; }
					if (x == 5) { throw "boom5"; }
					return x;
				}
				var a = [];
				for (var i = 0; i < 100; i++) { a.push(i); }
				try { parallel_transform(a, f); } catch (e) { return e; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("boom5") != std::string::npos);
			check_true(msg.find("boom90") == std::string::npos);
		});

		test("fn_internal_catch_keeps_region_alive", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				int f(int x) {
					try { if (x % 2 == 0) { throw "even"; } return x; }
					catch (e) { return -x; }
				}
				var a = [];
				for (var i = 0; i < 40; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				if (pout.size() != sout.size()) { return false; }
				for (var i = 0; i < pout.size(); i++) { if (pout[i] != sout[i]) { return false; } }
				return true;
			)");
			check_true(r.as<bool>());
		});

		test("worker_budget_overrun_is_terminal", [this]() {
			auto e = make_engine();
			e->execution_budget(0.05);
			e->parallel_thread_count(4);
			// One worker's infinite fn fails the whole call terminally: the script-level
			// catch must NOT swallow it (budget overruns latch the terminal rail).
			bool host_saw_failure = false;
			try {
				auto r = e->execute(R"(
					int f(int x) { while (true) { var y = x + 1; } return x; }
					var a = [];
					for (var i = 0; i < 32; i++) { a.push(i); }
					try { parallel_transform(a, f); } catch (e) { return "swallowed"; }
					return "done";
				)");
				host_saw_failure = r.is_null() || (r.is_string() && r.as<std::string>() != "swallowed" && r.as<std::string>() != "done");
			} catch (const std::exception&) {
				host_saw_failure = true;
			}
			check_true(host_saw_failure);
		});

		test("worker_memory_cap_fails_region", [this]() {
			auto e = make_engine();
			e->memory_cap(64 * 1024);
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				string f(int x) {
					var s = "block";
					for (var i = 0; i < 20; i++) { s = s + s; }
					return s;
				}
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(i); }
				try { parallel_transform(a, f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("memory cap") != std::string::npos);
		});

		// === Weight hint (the builtin's third argument IS the hint - no new syntax) ===

		test("weight_hint_shifts_chunk_bounds_output_identical", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			const std::string flat_src = R"(
				int f(int x) { return x * 2; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				return parallel_transform(a, f);
			)";
			const std::string weighted_src = R"(
				int f(int x) { return x * 2; }
				float w(int x) { if (x < 8) { return 1000.0; } return 1.0; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				return parallel_transform(a, f, w);
			)";
			auto flat = e->execute(flat_src);
			auto flat_bounds = e->last_parallel_chunk_bounds();
			auto weighted = e->execute(weighted_src);
			auto weighted_bounds = e->last_parallel_chunk_bounds();
			check_true(arrays_equal(flat, weighted));       // observable output identical
			check_eq(flat_bounds.size(), weighted_bounds.size());
			check_true(flat_bounds != weighted_bounds);     // heavy prefix pulled boundaries left
			// Cumulative-weight split: the first boundary lands inside the heavy prefix
			check_lt(weighted_bounds[1], flat_bounds[1]);
		});

		test("admission_recheck_after_function_redefine", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(2);
			e->execute("int helper(int x) { return x + 1; }");
			auto r1 = e->execute(R"(
				int f(int x) { return helper(x); }
				return parallel_transform([10, 20, 30, 40, 50, 60, 70, 80,
				                           11, 21, 31, 41, 51, 61, 71, 81], f);
			)");
			check_eq((int64_t)11, r1.as_array()[0].as_int());
			// Hot-reload the transitive target: the cached admission graph must notice
			e->execute("int helper(int x) { return x + 5; }");
			auto r2 = e->execute(R"(
				int f2(int x) { return helper(x); }
				return parallel_transform([10, 20, 30, 40, 50, 60, 70, 80,
				                           11, 21, 31, 41, 51, 61, 71, 81], f2);
			)");
			check_eq((int64_t)15, r2.as_array()[0].as_int());
		});

		// Fuzz-style battery: seed-derived pure fns over int and string arrays, run at
		// one worker vs many, element-wise compared against each other AND a hand serial
		// map. Fully deterministic (constants derive from the seed, no wall clock).
		test("fuzz_pure_fns_1_vs_N", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			auto mix = [](uint64_t z) {
				z ^= z >> 33; z *= 0xFF51AFD7ED558CCDULL;
				z ^= z >> 33; z *= 0xC4CEB9FE1A85EC53ULL;
				z ^= z >> 33; return z;
			};
			for (uint64_t seed = 1; seed <= 32; ++seed) {
				const uint64_t h = mix(seed);
				const int64_t k1 = static_cast<int64_t>(h % 13) + 2;
				const int64_t k2 = static_cast<int64_t>((h >> 8) % 29) - 14;
				const int64_t k3 = static_cast<int64_t>((h >> 16) % 7) + 1;
				const int shape = static_cast<int>((h >> 24) % 3);
				std::string body;
				switch (shape) {
				case 0:
					body = "var y = x * " + std::to_string(k1) + " + " + std::to_string(k2) + ";"
					       "var z = (x % " + std::to_string(k3) + ") - y;"
					       "return z * 2 + y % " + std::to_string(k1) + ";";
					break;
				case 1:
					body = "var y = x < " + std::to_string(k2) + " ? x + " + std::to_string(k1) + " : x - " + std::to_string(k1) + ";"
					       "var total = 0;"
					       "for (var j = 0; j < (x % 5 + 1); j++) { total += y + j; }"
					       "return total;";
					break;
				default:
					body = "var s = to_string(x * " + std::to_string(k1) + ");"
					       "return s.size() + x % " + std::to_string(k3) + ";";
					break;
				}
				const std::string src =
					"var a = [];"
					"for (var i = -20; i < 44; i++) { a.push(i * " + std::to_string(k3) + "); }"
					"int f(int x) { " + body + " }"
					"var pout = parallel_transform(a, f);"
					"var sout = [];"
					"for (auto x : a) { sout.push(f(x)); }"
					"return [pout, sout];";
				e->parallel_thread_count(1);
				auto r1 = e->execute(src);
				e->parallel_thread_count(7);
				auto rn = e->execute(src);
				check_true(r1.is_array());
				check_true(rn.is_array());
				check_true(deep_equal(r1.as_array()[0], r1.as_array()[1]));   // parallel == serial (1 worker)
				check_true(deep_equal(rn.as_array()[0], rn.as_array()[1]));   // parallel == serial (7 workers)
				check_true(deep_equal(r1.as_array()[0], rn.as_array()[0]));   // 1-vs-N byte parity
			}
			// String-array shapes
			for (uint64_t seed = 1; seed <= 8; ++seed) {
				const uint64_t h = mix(seed * 977);
				const int64_t k = static_cast<int64_t>(h % 5) + 2;
				const std::string src =
					"var a = [];"
					"for (var i = 0; i < 48; i++) { a.push(\"w\" + to_string(i * " + std::to_string(k) + ") + \"_tail\"); }"
					"string f(string s) {"
					"  var t = s + \"|\" + to_string(s.size() % " + std::to_string(k) + ");"
					"  return t.substr(0, t.size() % 9 + 3);"
					"}"
					"var pout = parallel_transform(a, f);"
					"var sout = [];"
					"for (auto x : a) { sout.push(f(x)); }"
					"return [pout, sout];";
				e->parallel_thread_count(1);
				auto r1 = e->execute(src);
				e->parallel_thread_count(6);
				auto rn = e->execute(src);
				check_true(deep_equal(r1.as_array()[0], r1.as_array()[1]));
				check_true(deep_equal(rn.as_array()[0], rn.as_array()[1]));
				check_true(deep_equal(r1.as_array()[0], rn.as_array()[0]));
			}
		});

		test("usage_errors", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				try { parallel_transform(5, thread_count); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r.as<std::string>().find("first argument must be an array") != std::string::npos);

			auto r2 = e->execute(R"(
				try { parallel_transform([1, 2], 7); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r2.as<std::string>().find("fn must be a script-defined function") != std::string::npos);
		});
	}
};

} // namespace jai::foundry::tests

using parallel_transform_tests = jai::foundry::tests::parallel_transform_tests;
FOUNDRY_REGISTER(parallel_transform_tests)
