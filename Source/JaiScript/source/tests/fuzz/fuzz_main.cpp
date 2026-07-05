// jaiscript_fuzz: differential fuzzer for the two JaiScript backends.
//
// Generates random-but-valid programs from a weighted grammar, runs each on
// BOTH the tree-walking interpreter and the bytecode VM, and reports any
// divergence in results / printed output / error text / engine liveness.
// Fully deterministic from the seed.
//
// Usage:
//   jaiscript_fuzz                          run seeds 0..999 (default --iterations 1000)
//   jaiscript_fuzz --iterations 10000       run seeds 0..9999
//   jaiscript_fuzz --seed 1234              run exactly one seed (verbose)
//   jaiscript_fuzz --seed-start 5000        first seed of the campaign range
//   jaiscript_fuzz --minimize               auto-minimize each divergence
//   jaiscript_fuzz --corpus DIR             write failing programs + minimized forms + repro skeletons
//   jaiscript_fuzz --timeout-ms 250         per-execute engine budget (default 250)
//   jaiscript_fuzz --max-depth 4            expression depth cap
//   jaiscript_fuzz --dump N                 print the generated program for seed N and exit
//   jaiscript_fuzz --child --seed N         one seed, machine-readable verdict via exit code
//                                           (0 agree, 20 diverge, 21 budget-skip)
//   jaiscript_fuzz --isolate                spawn a --child process per seed (crash isolation:
//                                           a VM crash doesn't kill the campaign)
//   jaiscript_fuzz --no-suppressions        report known (suppressed) divergences too
//
// Exit code: 0 = no new divergences, 1 = new divergences found, 20/21 = child verdicts.

#include "fuzz_generator.hpp"
#include "fuzz_harness.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace jai::fuzz;

namespace {

	struct options {
		uint64_t seed = 0;
		bool seed_given = false;
		uint64_t seed_start = 0;
		uint64_t iterations = 1000;
		int max_depth = 4;
		bool minimize_flag = false;
		std::string corpus;
		double timeout_ms = 250.0;
		bool child = false;
		bool isolate = false;
		bool dump = false;
		bool use_suppressions = true;
		std::string run_file; // triage: compare an on-disk program instead of a generated one
		std::string exe_path;
	};

	bool parse_args(int argc, char** argv, options& opt) {
		opt.exe_path = argv[0];
		for (int i = 1; i < argc; ++i) {
			std::string a = argv[i];
			auto next = [&](const char* what) -> const char* {
				if (i + 1 >= argc) { std::cerr << "missing value for " << what << "\n"; return nullptr; }
				return argv[++i];
			};
			if (a == "--seed") { const char* v = next("--seed"); if (!v) return false; opt.seed = std::stoull(v); opt.seed_given = true; }
			else if (a == "--seed-start") { const char* v = next("--seed-start"); if (!v) return false; opt.seed_start = std::stoull(v); }
			else if (a == "--iterations") { const char* v = next("--iterations"); if (!v) return false; opt.iterations = std::stoull(v); }
			else if (a == "--max-depth") { const char* v = next("--max-depth"); if (!v) return false; opt.max_depth = std::stoi(v); }
			else if (a == "--minimize") { opt.minimize_flag = true; }
			else if (a == "--corpus") { const char* v = next("--corpus"); if (!v) return false; opt.corpus = v; }
			else if (a == "--timeout-ms") { const char* v = next("--timeout-ms"); if (!v) return false; opt.timeout_ms = std::stod(v); }
			else if (a == "--child") { opt.child = true; }
			else if (a == "--isolate") { opt.isolate = true; }
			else if (a == "--dump") { const char* v = next("--dump"); if (!v) return false; opt.seed = std::stoull(v); opt.seed_given = true; opt.dump = true; }
			else if (a == "--no-suppressions") { opt.use_suppressions = false; }
			else if (a == "--run") { const char* v = next("--run"); if (!v) return false; opt.run_file = v; }
			else { std::cerr << "unknown argument: " << a << "\n"; return false; }
		}
		return true;
	}

	void write_corpus_entry(const options& opt, const program& original, const program* minimized,
	                        const std::string& diff, const std::string& suppression_id) {
		if (opt.corpus.empty()) return;
		namespace fs = std::filesystem;
		fs::create_directories(opt.corpus);
		std::string stem = (opt.corpus + "/div_seed") + std::to_string(original.seed);
		{
			std::ofstream f(stem + ".jai");
			f << "// fuzz seed " << original.seed << (suppression_id.empty() ? "" : (" [suppressed: " + suppression_id + "]")) << "\n";
			f << original.joined();
		}
		if (minimized) {
			std::ofstream f(stem + ".min.jai");
			f << "// fuzz seed " << original.seed << " (minimized)\n";
			f << minimized->joined();
		}
		{
			std::ofstream f(stem + ".report.md");
			f << "# Divergence: fuzz seed " << original.seed << "\n\n";
			if (!suppression_id.empty()) f << "Suppressed as known divergence: `" << suppression_id << "`\n\n";
			f << "Command: `jaiscript_fuzz --seed " << original.seed
			  << " --max-depth " << opt.max_depth << " --timeout-ms " << opt.timeout_ms << "`\n\n";
			f << "## Diff\n\n```\n" << diff << "```\n\n";
			const program& repro = minimized ? *minimized : original;
			f << "## Minimized program\n\n```\n" << repro.joined() << "```\n\n";
			f << "## Foundry test skeleton\n\n```cpp\n" << foundry_skeleton(repro, diff) << "```\n";
		}
	}

	// Verdict for one seed. Returns the classification and (optionally) reports.
	enum class verdict { agree, diverge_new, diverge_suppressed, budget_skip };

	verdict run_seed(const options& opt, uint64_t seed, bool verbose) {
		program p = generate_program(seed, opt.max_depth);
		double budget_seconds = opt.timeout_ms / 1000.0;
		compare_result cr = compare_program(p, budget_seconds);

		if (cr.kind == compare_result::budget_skip) {
			if (verbose || cr.budget_suspect) {
				std::cout << "[seed " << seed << "] budget-skip"
				          << (cr.budget_suspect ? " SUSPECT (only one backend hit the budget - possible hang divergence, likely wall-clock noise)" : "")
				          << "\n";
			}
			return verdict::budget_skip;
		}
		if (cr.kind == compare_result::agree) {
			if (verbose) {
				std::cout << "[seed " << seed << "] agree\n";
				for (size_t i = 0; i < cr.interp.segment_results.size(); ++i)
					std::cout << "  segment " << i << ": " << cr.interp.segment_results[i] << "\n";
				if (!cr.interp.printed.empty()) std::cout << "  printed:\n" << cr.interp.printed;
			}
			return verdict::agree;
		}

		std::string supp = opt.use_suppressions ? match_suppression(p, cr.diff) : std::string();

		std::cout << "\n=== DIVERGENCE seed " << seed << (supp.empty() ? "" : (" [suppressed: " + supp + "]")) << " ===\n";
		std::cout << cr.diff;

		program minimized = p;
		bool have_min = false;
		// Only minimize UNRECOGNIZED divergences: suppressed ones are already
		// triaged, and minimization is the expensive step of the campaign.
		if (opt.minimize_flag && supp.empty()) {
			minimize_stats stats;
			auto still = [&](const program& cand) {
				// Any divergence keeps the reduction: drifting into a KNOWN shape is
				// handled by re-matching suppressions on the minimized form below.
				return compare_program(cand, budget_seconds).kind == compare_result::diverge;
			};
			minimized = minimize(p, still, stats);
			have_min = true;
			std::cout << "--- minimized (" << stats.reductions << " reductions, " << stats.checks << " checks) ---\n";
			std::cout << minimized.joined() << "\n";
			compare_result mc = compare_program(minimized, budget_seconds);
			std::cout << "--- minimized diff ---\n" << mc.diff;
			// Some shapes only become recognizable after minimization (e.g. an
			// eval-order divergence buried under printed-output noise): re-match
			// the suppressions against the minimized form.
			if (opt.use_suppressions && supp.empty() && mc.kind == compare_result::diverge) {
				supp = match_suppression(minimized, mc.diff);
				if (!supp.empty())
					std::cout << "--- reclassified as suppressed after minimization: " << supp << " ---\n";
			}
		} else if (verbose) {
			std::cout << "--- program ---\n" << p.joined() << "\n";
		}
		write_corpus_entry(opt, p, have_min ? &minimized : nullptr, cr.diff, supp);
		std::cout.flush();
		return supp.empty() ? verdict::diverge_new : verdict::diverge_suppressed;
	}

	int run_isolated(const options& opt, uint64_t seed) {
		std::string cmd = "\"" + opt.exe_path + "\" --child --seed " + std::to_string(seed) +
			" --max-depth " + std::to_string(opt.max_depth) +
			" --timeout-ms " + std::to_string(opt.timeout_ms);
		if (opt.minimize_flag) cmd += " --minimize";
		if (!opt.corpus.empty()) cmd += " --corpus \"" + opt.corpus + "\"";
		if (!opt.use_suppressions) cmd += " --no-suppressions";
#ifdef _WIN32
		cmd = "\"" + cmd + "\""; // cmd.exe strips one layer of quotes
		FILE* pipe = _popen(cmd.c_str(), "r");
#else
		FILE* pipe = popen(cmd.c_str(), "r");
#endif
		if (!pipe) { std::cerr << "[seed " << seed << "] failed to spawn child\n"; return -1; }
		char buf[4096];
		std::string out;
		while (fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
		int code = _pclose(pipe);
#else
		int code = pclose(pipe);
#endif
		if (!out.empty() && code != 0) std::cout << out;
		return code;
	}

} // namespace

int main(int argc, char** argv) {
	options opt;
	if (!parse_args(argc, argv, opt)) return 2;

	if (opt.dump) {
		program p = generate_program(opt.seed, opt.max_depth);
		std::cout << p.joined();
		return 0;
	}

	if (!opt.run_file.empty()) {
		std::ifstream f(opt.run_file);
		if (!f) { std::cerr << "cannot open " << opt.run_file << "\n"; return 2; }
		std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		program p;
		const std::string marker = "// --- next execute() on same engine ---";
		size_t pos = 0;
		while (true) {
			size_t hit = text.find(marker, pos);
			if (hit == std::string::npos) { p.segments.push_back(text.substr(pos)); break; }
			p.segments.push_back(text.substr(pos, hit - pos));
			pos = hit + marker.size();
		}
		compare_result cr = compare_program(p, opt.timeout_ms / 1000.0);
		if (cr.kind == compare_result::diverge) {
			std::cout << "DIVERGE\n" << cr.diff;
			for (size_t i = 0; i < cr.interp.segment_results.size(); ++i)
				std::cout << "segment " << i << " interp: " << cr.interp.segment_results[i] << "\n"
				          << "segment " << i << " vm:     " << cr.vm.segment_results[i] << "\n";
			return 1;
		}
		std::cout << (cr.kind == compare_result::budget_skip ? "BUDGET-SKIP\n" : "AGREE\n");
		for (size_t i = 0; i < cr.interp.segment_results.size(); ++i)
			std::cout << "segment " << i << ": " << cr.interp.segment_results[i] << "\n";
		if (!cr.interp.printed.empty()) std::cout << "printed:\n" << cr.interp.printed;
		return 0;
	}

	if (opt.child) {
		verdict v = run_seed(opt, opt.seed, false);
		switch (v) {
			case verdict::agree: return 0;
			case verdict::budget_skip: return 21;
			case verdict::diverge_suppressed: return 22;
			case verdict::diverge_new: return 20;
		}
		return 0;
	}

	uint64_t begin = opt.seed_given ? opt.seed : opt.seed_start;
	uint64_t end = opt.seed_given ? opt.seed + 1 : opt.seed_start + opt.iterations;

	uint64_t agree = 0, new_div = 0, suppressed_div = 0, budget = 0, crashes = 0, parse_valid = 0;
	std::vector<uint64_t> new_div_seeds, crash_seeds, suppressed_seeds;

	for (uint64_t seed = begin; seed < end; ++seed) {
		{
			// Parse-validity metric (jaibite throws on hard parse errors; recoverable
			// ones synchronize silently, so this is a lower bound on brokenness only).
			program p = generate_program(seed, opt.max_depth);
			bool ok = true;
			try {
				auto probe = jai::engine::make();
				for (const auto& s : p.segments) (void)probe->jaibite(s);
			} catch (...) { ok = false; }
			if (ok) ++parse_valid;
		}
		if (!opt.seed_given && (seed - begin) % 500 == 0 && seed != begin) {
			std::cout << "[progress] seed " << seed << " | agree " << agree << " | new-div " << new_div
			          << " | suppressed " << suppressed_div << " | budget " << budget << " | crashes " << crashes << "\n";
			std::cout.flush();
		}
		if (opt.isolate) {
			// Print BEFORE running so a child crash is attributable.
			int code = run_isolated(opt, seed);
			switch (code) {
				case 0: ++agree; break;
				case 20: ++new_div; new_div_seeds.push_back(seed); break;
				case 21: ++budget; break;
				case 22: ++suppressed_div; suppressed_seeds.push_back(seed); break;
				default:
					++crashes;
					crash_seeds.push_back(seed);
					std::cout << "\n=== CRASH seed " << seed << " (child exit code " << code << ") ===\n";
					std::cout << "reproduce: jaiscript_fuzz --child --seed " << seed
					          << " --max-depth " << opt.max_depth << "\n";
					break;
			}
		} else {
			// In-process: a crash kills the campaign; the marker line below makes the
			// dying seed attributable so a driver can resume at seed+1.
			std::cout << "[running seed " << seed << "]\r";
			std::cout.flush();
			verdict v = run_seed(opt, seed, opt.seed_given);
			switch (v) {
				case verdict::agree: ++agree; break;
				case verdict::diverge_new: ++new_div; new_div_seeds.push_back(seed); break;
				case verdict::diverge_suppressed: ++suppressed_div; suppressed_seeds.push_back(seed); break;
				case verdict::budget_skip: ++budget; break;
			}
		}
	}

	std::cout << "\n===== campaign summary =====\n";
	std::cout << "seeds:          " << begin << ".." << (end - 1) << " (" << (end - begin) << " programs)\n";
	std::cout << "parse-valid:    " << parse_valid << " (" << (100.0 * parse_valid / (end - begin)) << "%)\n";
	std::cout << "agree:          " << agree << "\n";
	std::cout << "new divergences:" << new_div;
	if (!new_div_seeds.empty()) {
		std::cout << "  seeds:";
		for (auto s : new_div_seeds) std::cout << " " << s;
	}
	std::cout << "\nsuppressed:     " << suppressed_div;
	if (!suppressed_seeds.empty() && suppressed_seeds.size() <= 50) {
		std::cout << "  seeds:";
		for (auto s : suppressed_seeds) std::cout << " " << s;
	}
	std::cout << "\nbudget-skips:   " << budget << "\n";
	std::cout << "crashes:        " << crashes;
	if (!crash_seeds.empty()) {
		std::cout << "  seeds:";
		for (auto s : crash_seeds) std::cout << " " << s;
	}
	std::cout << "\n";
	return (new_div > 0 || crashes > 0) ? 1 : 0;
}
