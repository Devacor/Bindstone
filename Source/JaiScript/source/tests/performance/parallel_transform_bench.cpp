// Parallel Transform scaling benchmarks (parallel_design.md; v0 deliverable).
// Same body as a serial for-loop baseline, then parallel_transform at 1/2/4/8 workers.
// Foundry reports integer µs/iteration with ±50% variance (invariants.md §7): compare
// min-of-3 whole-suite runs and note machine load; the SHAPE of the scaling table is
// the claim, not sub-µs deltas.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <map>
#include <optional>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class parallel_transform_bench : public suite {
public:
	parallel_transform_bench() : suite("Parallel Transform Bench") {}

	std::shared_ptr<jai::engine> test_engine;
	std::map<std::string, jai::jaibite> bites;

	void pre_test() override {
		test_engine = make_engine();
		jai::stdlib::register_all(test_engine);
		bites.clear();
		// Inputs + bodies once; the benchmark iterations re-run only the call itself.
		test_engine->execute(R"(
			var a_math = [];
			for (var i = 0; i < 100000; i++) { a_math.push(0.5 + i * 0.173); }
			float f_math(float x) { return sqrt(x) * 2.0 + sqrt(x + 1.0) - pow(x, 0.25); }

			var a_small = [];
			for (var i = 0; i < 1000; i++) { a_small.push(0.5 + i * 0.173); }

			var a_str = [];
			for (var i = 0; i < 10000; i++) { a_str.push("payload_" + to_string(i * 37) + "_tail"); }
			string f_str(string s) {
				var t = s + "|" + to_string(s.size() * 3);
				return t.substr(0, t.size() % 11 + 6) + to_string(t.size());
			}
		)");
	}

	void run_bite(const char* key, const char* source) {
		auto it = bites.find(key);
		if (it == bites.end()) {
			it = bites.emplace(key, test_engine->jaibite(source)).first;
		}
		it->second.execute();
	}

	void bench_at(const char* name, size_t workers, const char* key, const char* source) {
		bench_at_n(name, workers, key, source, 3);
	}

	void bench_at_n(const char* name, size_t workers, const char* key, const char* source, int32_t iterations) {
		benchmark(name, [this, workers, key, source]() {
			test_engine->parallel_thread_count(workers);
			run_bite(key, source);
		}, iterations);
	}

	void forge_tests() override {
		static constexpr const char* k_math_serial =
			"var out = []; for (auto x : a_math) { out.push(f_math(x)); } out.size();";
		static constexpr const char* k_math_parallel =
			"parallel_transform(a_math, f_math).size();";
		static constexpr const char* k_str_serial =
			"var out = []; for (auto x : a_str) { out.push(f_str(x)); } out.size();";
		static constexpr const char* k_str_parallel =
			"parallel_transform(a_str, f_str).size();";

		benchmark("Math 100k doubles - serial for", [this]() {
			run_bite("ms", k_math_serial);
		}, 3);
		bench_at("Math 100k doubles - parallel W=1", 1, "mp", k_math_parallel);
		bench_at("Math 100k doubles - parallel W=2", 2, "mp", k_math_parallel);
		bench_at("Math 100k doubles - parallel W=4", 4, "mp", k_math_parallel);
		bench_at("Math 100k doubles - parallel W=8", 8, "mp", k_math_parallel);

		// Small-array row: context ACQUISITION dominates here - with slot reuse the
		// per-call overhead collapses to the reset + detach of 1k doubles. More
		// iterations: each call is short.
		static constexpr const char* k_small_serial =
			"var out = []; for (auto x : a_small) { out.push(f_math(x)); } out.size();";
		static constexpr const char* k_small_parallel =
			"parallel_transform(a_small, f_math).size();";
		benchmark("Math 1k doubles - serial for", [this]() {
			run_bite("sms", k_small_serial);
		}, 20);
		bench_at_n("Math 1k doubles - parallel W=1", 1, "smp", k_small_parallel, 20);
		bench_at_n("Math 1k doubles - parallel W=4", 4, "smp", k_small_parallel, 20);
		bench_at_n("Math 1k doubles - parallel W=8", 8, "smp", k_small_parallel, 20);

		benchmark("String 10k - serial for", [this]() {
			run_bite("ss", k_str_serial);
		}, 3);
		bench_at("String 10k - parallel W=1", 1, "sp", k_str_parallel);
		bench_at("String 10k - parallel W=2", 2, "sp", k_str_parallel);
		bench_at("String 10k - parallel W=4", 4, "sp", k_str_parallel);
		bench_at("String 10k - parallel W=8", 8, "sp", k_str_parallel);
	}
};

} // namespace jai::foundry::tests

using parallel_transform_bench = jai::foundry::tests::parallel_transform_bench;
FOUNDRY_REGISTER(parallel_transform_bench)
