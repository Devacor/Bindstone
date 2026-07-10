// parallel_transform v0 (docs/parallel_design.md; prove_or_serial §5 - contract A).
// Determinism: same inputs => identical output at every thread count, both backends.
// Admission violations ERROR with location text - never silently serial.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/parallel_transform.hpp>   // parallel_capture_kind (classification pins)
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

		// === Captured reads (v0.5): enclosing state is READABLE, proven-read-only ===

		test("captures_enclosing_scalar_reads", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			const std::string src = R"(
				var scale = 4;
				var bias = 0.5;
				var enabled = true;
				var tag = "v";
				string f(int x) {
					var v = enabled ? x * scale + bias : 0.0;
					return tag + to_string(v);
				}
				var a = [];
				for (var i = 0; i < 48; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("captured_grid_subscript_borrow", [this]() {
			auto e = make_engine();
			// The motivating shape: a flat all-primitive grid consumed by subscript reads.
			// On the vm this provisions as a BORROW (zero-copy raw reads); elsewhere as a
			// snapshot - semantics identical either way (pinned below).
			const std::string src = R"(
				var grid = [];
				for (var i = 0; i < 900; i++) { grid.push(i * 7 % 256); }
				var w = 30;
				int f(int row) {
					var total = 0;
					for (var x = 0; x < w; x++) { total += grid[row * w + x]; }
					return total * 2 + grid[row];
				}
				var rows = [];
				for (var r = 0; r < 30; r++) { rows.push(r); }
				var pout = parallel_transform(rows, f);
				var sout = [];
				for (auto r : rows) { sout.push(f(r)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
			// Classification pin: an all-primitive, subscript-only grid rides the borrow
			// tier (zero-copy raw reads) on BOTH backends; the scalar copies per worker
			const auto& caps = e->last_parallel_captures();
			const uint64_t grid_id = e->symbolize("grid");
			const uint64_t w_id = e->symbolize("w");
			bool saw_grid = false, saw_w = false;
			for (const auto& [id, kind] : caps) {
				if (id == grid_id) {
					saw_grid = true;
					check_eq((int)detail::parallel_capture_kind::borrow, (int)kind);
				}
				if (id == w_id) {
					saw_w = true;
					check_eq((int)detail::parallel_capture_kind::scalar, (int)kind);
				}
			}
			check_true(saw_grid);
			check_true(saw_w);
		});

		test("captured_string_table_snapshots", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(*e);
			// Non-primitive content (strings) -> per-worker snapshot at the barrier: dense
			// re-reads then cost nothing extra (the glyph-palette workload)
			const std::string src = R"(
				var pal = [];
				for (var i = 0; i < 16; i++) { pal.push("c" + to_string(i * 3)); }
				string f(int x) { return pal[x % 16] + "|" + pal[(x * 5) % 16]; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
			const auto& caps = e->last_parallel_captures();
			const uint64_t pal_id = e->symbolize("pal");
			bool saw = false;
			for (const auto& [id, kind] : caps) {
				if (id == pal_id) {
					saw = true;
					check_eq((int)detail::parallel_capture_kind::snapshot, (int)kind);
				}
			}
			check_true(saw);
		});

		test("captured_map_reads", [this]() {
			auto e = make_engine();
			// Primitive-keyed primitive map -> borrow-eligible; string keys -> snapshot
			const std::string src = R"(
				var lut = {};
				lut[1] = 10; lut[2] = 20; lut[3] = 30; lut[4] = 40;
				var names = {"a": 1, "b": 2};
				int f(int x) {
					var hit = lut[x % 4 + 1];
					return hit + names["a"] + names["b"];
				}
				var a = [];
				for (var i = 0; i < 40; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 4});
		});

		test("captured_nested_containers_and_iteration", [this]() {
			auto e = make_engine();
			// Nested containers (array of arrays) and by-VALUE iteration over a captured
			// container: both provision as snapshots and match serial exactly
			const std::string src = R"(
				var table = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
				int f(int x) {
					var total = table[x % 3][x % 2] * 100;
					for (auto row : table) {
						for (auto v : row) { total += v; }
					}
					return total + table.size();
				}
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(i); }
				var pout = parallel_transform(a, f);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 4});
		});

		test("captured_readonly_methods_and_weight_fn_reads", [this]() {
			auto e = make_engine();
			// Read-only builtin methods on captured receivers are admitted (size/contains);
			// the weight fn runs at the barrier on the engine backend, so its enclosing
			// reads are plain global reads
			const std::string src = R"(
				var pool = [5, 6, 7, 8];
				var heavy = 3.0;
				int f(int x) {
					if (pool.contains(x % 10)) { return x + pool.size(); }
					return x;
				}
				float w(int x) { return heavy; }
				var a = [];
				for (var i = 0; i < 40; i++) { a.push(i); }
				var pout = parallel_transform(a, f, w);
				var sout = [];
				for (auto x : a) { sout.push(f(x)); }
				return [pout, sout];
			)";
			check_matches_serial_at(e, src, {1, 4});
		});

		test("captured_snapshot_result_never_aliases", [this]() {
			auto e = make_engine();
			// `return s` shares the worker's snapshot handle - the result path must
			// materialize it, or elements produced by ONE worker would alias each other
			// while elements from different workers don't (worker-count-visible!)
			for (size_t workers : {size_t(1), size_t(8)}) {
				e->parallel_thread_count(workers);
				auto r = e->execute(R"(
					var s = [1, 2, 3];
					var f(int i) { return s; }
					var a = [];
					for (var i = 0; i < 24; i++) { a.push(i); }
					var pout = parallel_transform(a, f);
					pout[0].push(99);
					return [pout[0].size(), pout[1].size(), pout[23].size()];
				)");
				const auto& sizes = r.as_array();
				check_eq((int64_t)4, sizes[0].as_int());
				check_eq((int64_t)3, sizes[1].as_int());
				check_eq((int64_t)3, sizes[2].as_int());
			}
		});

		test("captured_snapshot_charges_memory_cap_borrow_does_not", [this]() {
			auto e = make_engine();
			e->execution_budget(0);   // Debug-interpreter array build outruns the 1s default
			e->execute(R"(
				var big = [];
				for (var i = 0; i < 8000; i++) { big.push(i); }
			)");
			e->memory_cap(32 * 1024);
			e->parallel_thread_count(4);
			// Whole-value read -> snapshot -> barrier detach charges the enclosing
			// execute's memory accounting -> catchable memory-cap raise at a back-edge
			auto r = e->execute(R"(
				var f(int i) { return big; }
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(i); }
				try {
					var pout = parallel_transform(a, f);
					for (var i = 0; i < 4; i++) { var nudge = i; }
					return "no-error";
				} catch (e) { return e; }
			)");
			check_true(r.as<std::string>().find("memory cap") != std::string::npos);

			// Borrow tier: subscript-only reads of the same container copy NOTHING, so
			// the same cap stays untouched (both backends)
			{
				auto e2 = make_engine();
				e2->execution_budget(0);
				e2->execute(R"(
					var big = [];
					for (var i = 0; i < 8000; i++) { big.push(i); }
				)");
				e2->memory_cap(32 * 1024);
				e2->parallel_thread_count(4);
				auto r2 = e2->execute(R"(
					int f(int i) { return big[i * 7 % 8000]; }
					var a = [];
					for (var i = 0; i < 64; i++) { a.push(i); }
					try {
						var pout = parallel_transform(a, f);
						for (var i = 0; i < 4; i++) { var nudge = i; }
						return pout[0];
					} catch (e) { return e; }
				)");
				check_true(r2.is_int());
			}
		});

		test("captured_write_rejection_matrix", [this]() {
			auto e = make_engine();
			auto expect_error = [&](const char* body_line, const char* fragment) {
				const std::string src = std::string(R"(
					var g = [1, 2, 3];
					int helper_ref(int& a) { return a; }
					int f(int x) { )") + body_line + R"( return x; }
					try { parallel_transform([1, 2, 3, 4], f); } catch (e) { return e; }
					return "no-error";
				)";
				auto r = e->execute(src);
				auto msg = r.as<std::string>();
				check_true(msg.find(fragment) != std::string::npos);
				check_true(msg.find(" at ") != std::string::npos);   // line:col position
			};
			expect_error("g = [9];", "body writes enclosing state 'g'");
			expect_error("g[0] = 5;", "body writes enclosing state 'g'");
			expect_error("g[0] += 1;", "body writes enclosing state 'g'");
			expect_error("g[0]++;", "body writes enclosing state 'g'");
			expect_error("g.push(4);", "method 'push' may mutate captured state 'g'");
			expect_error("g.sort();", "method 'sort' may mutate captured state 'g'");
			expect_error("var& r = g;", "reference declaration would alias enclosing state 'g'");
			expect_error("helper_ref(g[0]);", "argument passes enclosing state 'g' by reference");
			expect_error("for (auto& v : g) { }", "cannot iterate captured state 'g' by reference");
		});

		test("captured_reads_deterministic_1_2_8", [this]() {
			auto e = make_engine();
			// Grid-consumption determinism: byte-identical output at every worker count
			const std::string src = R"(
				var grid = [];
				for (var i = 0; i < 2048; i++) { grid.push((i * 76241) % 65536); }
				int f(int row) {
					var acc = 0;
					for (var x = 0; x < 64; x++) { acc = (acc * 31 + grid[row * 64 + x]) % 1000003; }
					return acc;
				}
				var rows = [];
				for (var r = 0; r < 32; r++) { rows.push(r); }
				return parallel_transform(rows, f);
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

		test("captured_snapshot_equals_barrier_content", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			// The region sees exactly the barrier-time content, every call (pin: content
			// changes BETWEEN calls are visible, content is frozen DURING one)
			auto r = e->execute(R"(
				var cfg = [10];
				int f(int x) { return cfg[0] + x; }
				var a = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
				var first = parallel_transform(a, f);
				cfg[0] = 20;
				var second = parallel_transform(a, f);
				return [first[0], second[0]];
			)");
			const auto& vals = r.as_array();
			check_eq((int64_t)11, vals[0].as_int());
			check_eq((int64_t)21, vals[1].as_int());
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

		test("slot_reuse_across_calls_consistent", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			const std::string src = R"(
				int f(int x) { return x * 7 - 3; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				return parallel_transform(a, f);
			)";
			auto r1 = e->execute(src);
			auto r2 = e->execute(src);   // slots reused (same fingerprint)
			auto r3 = e->execute(src);
			check_true(arrays_equal(r1, r2));
			check_true(arrays_equal(r1, r3));
		});

		test("slot_reuse_after_error_is_pristine", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			// First call fails mid-chunk (residual unwinding state in some slot)
			auto r1 = e->execute(R"(
				int f(int x) { if (x == 40) { throw "mid"; } return x; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				try { parallel_transform(a, f); } catch (e) { return e; }
				return "no-error";
			)");
			check_true(r1.as<std::string>().find("mid") != std::string::npos);
			// Second call on the SAME engine reuses the slots and must run clean
			auto r2 = e->execute(R"(
				int g(int x) { return x + 1; }
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				var out = parallel_transform(a, g);
				return out[63];
			)");
			check_eq((int64_t)64, r2.as_int());
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

// parallel_for v1 (Dev rulings 2026-07-09): the in-place fork-join statement.
// auto& mutates the owned element in place (no output array, no merge); plain auto is
// a side-effect-free worker copy. Elements must be value-closed (primitives, strings,
// value containers, flat classes); the barrier detaches shared string nodes instead of
// rejecting them. Determinism: identical results at every worker count.
class parallel_for_tests : public suite {
public:
	parallel_for_tests() : suite("Parallel For") {}

	// Runs `source` (returns [parallel_result, serial_expectation]) at several worker
	// counts and checks equality each time.
	void check_matches_serial_at(std::shared_ptr<engine> e, const std::string& source,
	                             std::initializer_list<size_t> worker_counts) {
		for (size_t workers : worker_counts) {
			e->parallel_thread_count(workers);
			auto r = e->execute(source);
			check_true(r.is_array());
			const auto& pair = r.as_array();
			check_eq((size_t)2, pair.size());
			check_true(parallel_transform_tests::deep_equal(pair[0], pair[1]));
		}
	}

	void forge_tests() override {
		test("inplace_row_mutation_matches_serial_1_2_8", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				var a = [];
				var b = [];
				for (var i = 0; i < 200; i++) { a.push([i, 0]); b.push([i, 0]); }
				parallel_for (auto& p : a) { p[1] = p[0] * 3 - 1; }
				for (auto& p : b) { p[1] = p[0] * 3 - 1; }
				return [a, b];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("primitive_elements_mutate_in_place", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				var a = [];
				for (var i = 0; i < 100; i++) { a.push(i); }
				parallel_for (auto& x : a) { x = x * x; }
				return [a[0], a[7], a[99]];
			)");
			const auto& vals = r.as_array();
			check_eq((int64_t)0, vals[0].as_int());
			check_eq((int64_t)49, vals[1].as_int());
			check_eq((int64_t)9801, vals[2].as_int());
		});

		test("flat_object_elements_mutate_in_place", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				class Particle { float x = 0.0; float vx = 0.0; int life = 0; }
				var pool = [];
				for (var i = 0; i < 64; i++) {
					auto p = Particle();
					p.x = 1.0 * i;
					p.vx = 0.5;
					p.life = i;
					pool.push(p);
				}
				parallel_for (auto& p : pool) {
					p.x = p.x + p.vx;
					p.life = p.life - 1;
				}
				return [pool[0].x, pool[10].x, pool[63].life];
			)");
			const auto& vals = r.as_array();
			check_near(0.5, vals[0].as_float(), 0.0001);
			check_near(10.5, vals[1].as_float(), 0.0001);
			check_eq((int64_t)62, vals[2].as_int());
		});

		// Strings are value-closed by ruling: fields AND elements. The shared-node case
		// (two elements assigned from one variable) must normalize at the barrier, not
		// error - and the mutation must not corrupt the sibling.
		test("string_fields_and_shared_nodes_normalize", [this]() {
			auto e = make_engine();
			jai::stdlib::register_all(e);
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				class Row { int idx = 0; string out = ""; }
				var rows = [];
				string shared_seed = "seed";
				for (var i = 0; i < 32; i++) {
					auto rec = Row();
					rec.idx = i;
					rec.out = shared_seed;
					rows.push(rec);
				}
				parallel_for (auto& rec : rows) {
					rec.out = "row_" + to_string(rec.idx);
				}
				return [rows[0].out, rows[31].out, shared_seed];
			)");
			const auto& vals = r.as_array();
			check_eq(std::string("row_0"), vals[0].as<std::string>());
			check_eq(std::string("row_31"), vals[1].as<std::string>());
			check_eq(std::string("seed"), vals[2].as<std::string>());
		});

		test("by_value_element_is_side_effect_free", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				var a = [];
				for (var i = 0; i < 64; i++) { a.push([i, 0]); }
				parallel_for (auto p : a) { p[1] = 999; }
				return [a[0][1], a[63][1]];
			)");
			const auto& vals = r.as_array();
			check_eq((int64_t)0, vals[0].as_int());
			check_eq((int64_t)0, vals[1].as_int());
		});

		test("captured_scalar_and_borrow_reads", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				var scale = 7;
				var lut = [];
				for (var i = 0; i < 256; i++) { lut.push(i * 2); }
				var a = [];
				var b = [];
				for (var i = 0; i < 128; i++) { a.push([i, 0]); b.push([i, 0]); }
				parallel_for (auto& p : a) { p[1] = lut[p[0]] * scale; }
				for (auto& p : b) { p[1] = lut[p[0]] * scale; }
				return [a, b];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		test("helper_function_calls_in_body", [this]() {
			auto e = make_engine();
			const std::string src = R"(
				int shape(int v) { return v * v - 3; }
				var a = [];
				var b = [];
				for (var i = 0; i < 96; i++) { a.push([i, 0]); b.push([i, 0]); }
				parallel_for (auto& p : a) { p[1] = shape(p[0]) + math::itrunc(math::sqrt(1.0 * p[0])); }
				for (auto& p : b) { p[1] = shape(p[0]) + math::itrunc(math::sqrt(1.0 * p[0])); }
				return [a, b];
			)";
			check_matches_serial_at(e, src, {1, 2, 8});
		});

		// === Contract A: violations ERROR ===

		test("enclosing_write_rejected", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				var g = 0;
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(i); }
				try { parallel_for (auto& x : a) { g = x; } } catch (err) { return err; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("parallel_for: body writes enclosing state 'g'") != std::string::npos);
		});

		test("enclosing_member_store_rejected", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				class World { int hits = 0; }
				var w = World();
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(i); }
				try { parallel_for (auto& x : a) { w.hits = x; } } catch (err) { return err; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("captured state 'w'") != std::string::npos);
		});

		test("mutated_container_borrow_capture_rejected", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				try { parallel_for (auto& x : a) { x = a[0] + x; } } catch (err) { return err; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("container being mutated") != std::string::npos);
		});

		test("non_value_semantic_element_rejected", [this]() {
			auto e = make_engine();
			auto r = e->execute(R"(
				class Node { var payload = 0; }
				var a = [];
				for (var i = 0; i < 32; i++) { a.push(Node()); }
				try { parallel_for (auto& x : a) { x.payload = 1; } } catch (err) { return err; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("cannot be mutated in place") != std::string::npos);
		});

		test("method_call_wall_and_unsafe_override", [this]() {
			auto e = make_engine();
			// Small n -> one worker chunk, so the trusted run is genuinely single-threaded
			auto r = e->execute(R"(
				class Cell { int v = 0; int doubled() { return v * 2; } }
				var a = [];
				for (var i = 0; i < 8; i++) { auto c = Cell(); c.v = i; a.push(c); }
				try { parallel_for (auto& c : a) { c.v = c.doubled(); } } catch (err) { return err; }
				return "no-error";
			)");
			auto msg = r.as<std::string>();
			check_true(msg.find("cannot call script class methods in a parallel body") != std::string::npos);
			e->allow_unsafe_parallel(true);
			auto r2 = e->execute(R"(
				var a2 = [];
				for (var i = 0; i < 8; i++) { auto c = Cell(); c.v = i; a2.push(c); }
				parallel_for (auto& c : a2) { c.v = c.doubled(); }
				return a2[3].v;
			)");
			check_eq((int64_t)6, r2.as_int());
		});

		test("determinism_across_worker_counts", [this]() {
			// Byte-identical results at 1/2/4/8 workers from identical initial state
			std::string first;
			for (size_t workers : {1, 2, 4, 8}) {
				auto e = make_engine();
				jai::stdlib::register_all(e);
				e->parallel_thread_count(workers);
				auto r = e->execute(R"(
					var a = [];
					for (var i = 0; i < 300; i++) { a.push([i, 0.0]); }
					parallel_for (auto& p : a) { p[1] = math::sqrt(1.0 * p[0]) * 3.7; }
					return to_json(a);
				)");
				if (first.empty()) { first = r.as<std::string>(); }
				else { check_eq(first, r.as<std::string>()); }
			}
		});

		test("second_region_reuses_slots_cleanly", [this]() {
			auto e = make_engine();
			e->parallel_thread_count(4);
			auto r = e->execute(R"(
				var a = [];
				for (var i = 0; i < 64; i++) { a.push(i); }
				parallel_for (auto& x : a) { x = x + 1; }
				parallel_for (auto& x : a) { x = x * 2; }
				return a[63];
			)");
			check_eq((int64_t)128, r.as_int());
		});
	}
};

} // namespace jai::foundry::tests

using parallel_transform_tests = jai::foundry::tests::parallel_transform_tests;
FOUNDRY_REGISTER(parallel_transform_tests)
using parallel_for_tests = jai::foundry::tests::parallel_for_tests;
FOUNDRY_REGISTER(parallel_for_tests)
