// Seeded differential-fuzz smoke suite: every CI run fuzzes a little.
// Runs a fixed range of generator seeds through the same harness as
// jaiscript_fuzz and asserts zero UNSUPPRESSED divergences (known in-flight
// parity gaps are matched by the suppressions in fuzz_harness.hpp and
// documented in known_divergences.md).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

#include "fuzz/fuzz_generator.hpp"
#include "fuzz/fuzz_harness.hpp"

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class fuzz_smoke_tests : public suite {
public:
	fuzz_smoke_tests() : suite("Differential Fuzz Smoke") {}

	void forge_tests() override {
		test("fixed_seeds_no_new_divergence", [this]() {
			// Seed count is budgeted to keep this suite fast; bump it as the
			// engines get faster. Seeds here are FIXED - failures reproduce with
			// jaiscript_fuzz --seed <N>.
			constexpr uint64_t k_first_seed = 0;
			constexpr uint64_t k_seed_count = 40;
			constexpr double k_budget_seconds = 0.1; // budget-skips cost 2x this; keeps the suite <2s

			int divergences = 0;
			std::string first_report;
			for (uint64_t seed = k_first_seed; seed < k_first_seed + k_seed_count; ++seed) {
				fuzz::program p = fuzz::generate_program(seed, 3);
				fuzz::compare_result cr = fuzz::compare_program(p, k_budget_seconds);
				if (cr.kind != fuzz::compare_result::diverge) continue;
				if (!fuzz::match_suppression(p, cr.diff).empty()) continue; // known, in-flight fix
				// Rare path: some known shapes are only recognizable minimized
				// (e.g. eval-order divergences buried in printed-output noise).
				{
					fuzz::minimize_stats stats;
					fuzz::program reduced = fuzz::minimize(p, [&](const fuzz::program& cand) {
						return fuzz::compare_program(cand, k_budget_seconds).kind == fuzz::compare_result::diverge;
					}, stats, 150);
					fuzz::compare_result mc = fuzz::compare_program(reduced, k_budget_seconds);
					if (mc.kind == fuzz::compare_result::diverge && !fuzz::match_suppression(reduced, mc.diff).empty())
						continue;
				}
				++divergences;
				if (first_report.empty()) {
					first_report = "fuzz seed " + std::to_string(seed) +
						" diverged (repro: jaiscript_fuzz --seed " + std::to_string(seed) +
						" --max-depth 3 --minimize)\n" + cr.diff;
				}
			}
			check_eq(0, divergences, first_report.empty() ? "no divergences" : first_report);
		});
	}
};

} // namespace jai::foundry::tests

using fuzz_smoke_tests = jai::foundry::tests::fuzz_smoke_tests;
FOUNDRY_REGISTER(fuzz_smoke_tests)
