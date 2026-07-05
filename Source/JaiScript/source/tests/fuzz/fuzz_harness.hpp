#pragma once

// Differential fuzzing harness: runs one generated program on BOTH backends
// (tree-walking interpreter + bytecode VM) and compares final results, printed
// output, error text, and engine-alive-afterward. Any mismatch is a divergence.
// Fully deterministic: everything derives from an explicit uint64 seed.

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace jai::fuzz {

	// ------------------------------------------------------------------
	// Deterministic RNG (splitmix64) - no random_device, no wall clock.
	// ------------------------------------------------------------------
	// Stafford mix13 finalizer: places each seed's stream at a pseudo-random
	// point of the splitmix orbit. (Seeding with seed*GAMMA aliases: seed N+1
	// would be seed N's stream shifted by a constant number of draws.)
	inline uint64_t mix64(uint64_t z) {
		z ^= z >> 33;
		z *= 0xFF51AFD7ED558CCDULL;
		z ^= z >> 33;
		z *= 0xC4CEB9FE1A85EC53ULL;
		z ^= z >> 33;
		return z;
	}

	struct fuzz_rng {
		uint64_t state;
		explicit fuzz_rng(uint64_t seed) : state(seed) {}
		uint64_t next() {
			uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}
		uint32_t below(uint32_t n) { return n == 0 ? 0 : static_cast<uint32_t>(next() % n); }
		bool chance(uint32_t percent) { return below(100) < percent; }
		int64_t range(int64_t lo, int64_t hi) { // inclusive
			return lo + static_cast<int64_t>(next() % static_cast<uint64_t>(hi - lo + 1));
		}
		template <typename T>
		const T& pick(const std::vector<T>& v) { return v[below(static_cast<uint32_t>(v.size()))]; }
	};

	// A generated program: one or more source segments executed sequentially on
	// the SAME engine (multiple segments exercise hot-reload / cross-execute state).
	struct program {
		uint64_t seed = 0;
		std::vector<std::string> segments;

		std::string joined() const {
			std::string out;
			for (size_t i = 0; i < segments.size(); ++i) {
				if (i) out += "\n// --- next execute() on same engine ---\n";
				out += segments[i];
			}
			return out;
		}
	};

	// ------------------------------------------------------------------
	// Value description: deterministic deep serialization for comparison.
	// ------------------------------------------------------------------
	inline const char* type_tag(const script_value& v) {
		switch (v.type()) {
			case script_value_type::jai_null_type: return "null";
			case script_value_type::jai_bool_type: return "bool";
			case script_value_type::jai_int_type: return "int";
			case script_value_type::jai_float_type: return "float";
			case script_value_type::jai_char_type: return "char";
			case script_value_type::jai_string_type: return "string";
			case script_value_type::jai_array_type: return "array";
			case script_value_type::jai_map_type: return "map";
			case script_value_type::jai_function_type: return "function";
			case script_value_type::jai_object_type: return "object";
			case script_value_type::jai_reference_type: return "reference";
			case script_value_type::jai_weak_ptr_type: return "weak_ptr";
			default: return "unknown";
		}
	}

	inline std::string describe_value(const script_value& v, int depth = 0) {
		if (depth > 6) return "<max-depth>";
		try {
			switch (v.type()) {
				case script_value_type::jai_reference_type:
					return "ref->" + describe_value(v.deref(), depth + 1);
				case script_value_type::jai_array_type: {
					const auto& arr = v.as_array();
					std::string out = "array[";
					size_t shown = arr.size() < 48 ? arr.size() : 48;
					for (size_t i = 0; i < shown; ++i) {
						if (i) out += ", ";
						out += describe_value(arr[i], depth + 1);
					}
					if (shown < arr.size()) out += ", <+" + std::to_string(arr.size() - shown) + " more>";
					return out + "]";
				}
				case script_value_type::jai_map_type: {
					const auto& m = v.as_map();
					std::string out = "map{";
					size_t i = 0;
					for (const auto& kv : m) {
						if (i++) out += ", ";
						if (i > 48) { out += "<+more>"; break; }
						out += describe_value(kv.first, depth + 1) + ": " + describe_value(kv.second, depth + 1);
					}
					return out + "}";
				}
				default:
					return std::string(type_tag(v)) + ":" + v.to_string();
			}
		} catch (const std::exception& ex) {
			return std::string("<describe-error: ") + ex.what() + ">";
		}
	}

	// ------------------------------------------------------------------
	// Oracle: run a program on one backend, capture everything observable.
	// ------------------------------------------------------------------
	struct run_outcome {
		std::vector<std::string> segment_results; // "VALUE <desc>" or "ERROR <what>"
		std::string printed;                      // everything print() emitted
		bool alive = false;                       // trivial follow-up execute succeeded
		bool budget_hit = false;                  // any segment raised a budget error
	};

	struct run_config {
		bool use_vm = false;
		double budget_seconds = 0.25;
		size_t print_cap = 32 * 1024;
	};

	inline bool contains_ci(const std::string& hay, const std::string& needle) {
		auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c))); return s; };
		return lower(hay).find(lower(needle)) != std::string::npos;
	}

	inline run_outcome run_program(const program& p, const run_config& cfg) {
		run_outcome out;
		auto eng = jai::engine::make();
		if (cfg.use_vm) eng->set_backend(jai::backend_type::vm);
		eng->execution_budget(cfg.budget_seconds);

		auto printed = std::make_shared<std::string>();
		const size_t cap = cfg.print_cap;
		jai::engine* raw = eng.get();
		eng->add_variadic_function("print", [printed, cap, raw](const std::vector<script_value>& args) -> script_value {
			if (printed->size() < cap) {
				std::string line;
				for (size_t i = 0; i < args.size(); ++i) {
					if (i) line += " ";
					line += args[i].to_string();
				}
				line += "\n";
				printed->append(line);
				if (printed->size() > cap) {
					printed->resize(cap);
					printed->append("<print-capped>");
				}
			}
			return script_value(std::monostate{}, raw);
		});

		for (const auto& seg : p.segments) {
			std::string r;
			auto seg_start = std::chrono::steady_clock::now();
			try {
				script_value v = eng->execute(seg);
				r = "VALUE " + describe_value(v);
			} catch (const std::exception& ex) {
				r = std::string("ERROR ") + ex.what();
				if (contains_ci(r, "budget exceeded")) out.budget_hit = true;
			}
			// A script can CATCH the budget error and keep going - then the two
			// backends print wall-clock-dependent amounts of output. Any segment
			// that consumed most of the budget is timing-sensitive: never a finding.
			double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - seg_start).count();
			if (cfg.budget_seconds > 0 && elapsed >= cfg.budget_seconds * 0.9) out.budget_hit = true;
			out.segment_results.push_back(std::move(r));
		}
		try {
			out.alive = (eng->execute("1 + 1").as_int() == 2);
		} catch (...) {
			out.alive = false;
		}
		out.printed = *printed;
		return out;
	}

	// ------------------------------------------------------------------
	// Comparison
	// ------------------------------------------------------------------
	struct compare_result {
		enum kind_t { agree, diverge, budget_skip } kind = agree;
		bool budget_suspect = false; // only ONE backend hit the budget (possible hang divergence, wall-clock noise more likely)
		std::string diff;
		run_outcome interp;
		run_outcome vm;
	};

	inline std::string first_difference(const std::string& a, const std::string& b) {
		size_t i = 0;
		while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
		auto excerpt = [i](const std::string& s) {
			size_t begin = i > 40 ? i - 40 : 0;
			std::string e = s.substr(begin, 120);
			for (auto& c : e) if (c == '\n') c = '|';
			return e;
		};
		return "at byte " + std::to_string(i) +
			"\n      interp(len " + std::to_string(a.size()) + "): ..." + excerpt(a) +
			"\n      vm    (len " + std::to_string(b.size()) + "): ..." + excerpt(b);
	}

	inline compare_result compare_program(const program& p, double budget_seconds) {
		compare_result cr;
		cr.interp = run_program(p, run_config{false, budget_seconds});
		cr.vm = run_program(p, run_config{true, budget_seconds});
		if (cr.interp.budget_hit || cr.vm.budget_hit) {
			// The budget is wall-clock: one side can time out while the other squeaks
			// by, so budget programs are never findings. Flag one-sided hits for review.
			cr.kind = compare_result::budget_skip;
			cr.budget_suspect = (cr.interp.budget_hit != cr.vm.budget_hit);
			return cr;
		}
		std::string diff;
		size_t n = cr.interp.segment_results.size();
		for (size_t i = 0; i < n; ++i) {
			const auto& a = cr.interp.segment_results[i];
			const auto& b = cr.vm.segment_results[i];
			if (a != b) {
				diff += "  segment " + std::to_string(i) + " result:\n    interp: " + a + "\n    vm:     " + b + "\n";
			}
		}
		if (cr.interp.printed != cr.vm.printed) {
			diff += "  printed output differs " + first_difference(cr.interp.printed, cr.vm.printed) + "\n";
		}
		if (cr.interp.alive != cr.vm.alive) {
			diff += std::string("  engine-alive-after: interp=") + (cr.interp.alive ? "yes" : "NO") +
				" vm=" + (cr.vm.alive ? "yes" : "NO") + "\n";
		}
		if (!diff.empty()) {
			cr.kind = compare_result::diverge;
			cr.diff = diff;
		}
		return cr;
	}

	// ------------------------------------------------------------------
	// Suppressions for KNOWN divergences (fixes currently in flight on this
	// branch). A suppression matches when every program needle appears in the
	// program text AND every diff needle appears in the divergence summary.
	// Documented in known_divergences.md next to this header.
	// ------------------------------------------------------------------
	struct suppression {
		std::string id;
		std::string description;
		std::vector<std::string> program_needles;
		std::vector<std::string> diff_needles;
	};

	inline const std::vector<suppression>& known_suppressions() {
		// Shapes observed and triaged in real campaigns; see known_divergences.md
		// for the ledger (repro seeds, wrong-backend judgment, status).
		static const std::vector<suppression> list = {
			// Resuming a coroutine past its last yield (no return): interpreter
			// repeats the last yielded value, VM yields null. In-flight coroutine
			// final-return fix territory. Pilot seeds 4, 7, 38, 116, 195, 216.
			{"KD-CORO-RESUME-PAST-END", "resume past final yield: interp repeats last value, vm nulls",
				{"coroutine ", ".resume()"}, {"null"}},
			// Undefined-variable error text: interpreter decorates with
			// "(no 'this' in scope)" in compound-assign paths, VM does not.
			// (Also masks the compound-assign eval-order aspect of seed 28:
			// interp reports the lhs first, VM the rhs.) Pilot seeds 34, 28.
			// Ordered before FZ-TYPED-FN-NO-RETURN: this text needle is the
			// more specific discriminator.
			{"FZ-UNDEF-VAR-THIS-DECORATION", "interp-only \"(no 'this' in scope)\" error decoration",
				{}, {"(no 'this' in scope)"}},
			// Typed function (-> T) falling off the end without a return: the
			// interpreter's typed-return epilogue raises a bogus
			// "Undefined variable '<function-local>'" while the VM returns null.
			// Pilot seeds 66, 239.
			{"FZ-TYPED-FN-NO-RETURN", "typed fn falls off end: interp bogus undefined-variable error",
				{") -> "}, {"interp: ERROR Undefined variable"}},
			// Same bug, caught variant: the bogus interpreter error is caught by a
			// script-level try/catch, so it surfaces as printed "caught: Undefined
			// variable 'x'" on the interpreter and silence on the VM.
			// Campaign seeds 2529, 3657, 3784.
			{"FZ-TYPED-FN-NO-RETURN-CAUGHT", "typed fn falls off end, bogus error caught in script",
				{") -> "}, {"caught: Undefined variable '"}},
			// Unary minus on 0.0: interpreter yields +0.0, VM yields IEEE -0.0.
			// Campaign seeds 1985, 2148, 2255.
			{"FZ-NEG-ZERO", "-(0.0): interp +0.0 vs vm -0.0",
				{}, {"-0.000000"}},
			// Integer-overflow error text names the wrong operator, and a different
			// wrong operator per backend (e.g. interp '*' vs vm '+='). Seed 2706.
			{"FZ-OVERFLOW-OP-NAME", "overflow error text: operator name differs per backend",
				{}, {"Integer overflow in '"}},
			// Checked-overflow error raised inside a <=> operand: the interpreter
			// swallows it entirely (program completes, null result) while the VM
			// raises it (with its own wrong-operator text). Campaign seed 9336.
			{"FZ-SPACESHIP-OVERFLOW-SWALLOW", "overflow inside <=> operand: interp swallows, vm raises",
				{"<=>"}, {"Integer overflow"}},
			// Float division-by-zero error text differs.
			{"FZ-FLOAT-DIVZERO-TEXT", "\"Division by zero\" vs \"Division by zero in float operation\"",
				{}, {"Division by zero in float operation"}},
			// Deep-recursion error text differs (and the VM's message has an
			// empty "()" where the depth should be). Pilot seed 137.
			{"FZ-RECURSION-MSG-TEXT", "native-stack vs max-recursion-depth message mismatch",
				{}, {"Native stack exhausted", "Maximum recursion depth"}},
			// throw inside a class ordering operator swallowed by one backend
			// (fix in flight; not observed since - matcher kept for the campaign).
			{"KD-ORDERING-THROW", "throw inside operator< swallowed by one backend",
				{"operator<"}, {"op-lt-boom"}},
		};
		return list;
	}

	inline std::string match_suppression(const program& p, const std::string& diff) {
		std::string text = p.joined();
		for (const auto& s : known_suppressions()) {
			bool ok = true;
			for (const auto& n : s.program_needles) {
				if (text.find(n) == std::string::npos) { ok = false; break; }
			}
			if (ok) {
				for (const auto& n : s.diff_needles) {
					if (diff.find(n) == std::string::npos) { ok = false; break; }
				}
			}
			if (ok) return s.id;
		}
		return {};
	}

	// ------------------------------------------------------------------
	// Minimizer: greedy structural reduction over source lines. Candidate
	// programs that go parse-invalid simply stop diverging and are rejected,
	// so structural validity is enforced by the oracle itself.
	// ------------------------------------------------------------------
	using diverges_fn = std::function<bool(const program&)>;

	namespace detail_min {
		inline std::vector<std::string> split_lines(const std::string& s) {
			std::vector<std::string> lines;
			std::string cur;
			for (char c : s) {
				if (c == '\n') { lines.push_back(cur); cur.clear(); }
				else cur += c;
			}
			if (!cur.empty()) lines.push_back(cur);
			return lines;
		}
		inline std::string join_lines(const std::vector<std::string>& lines) {
			std::string out;
			for (const auto& l : lines) { out += l; out += '\n'; }
			return out;
		}
		inline int brace_delta(const std::string& line) {
			int d = 0;
			bool in_str = false;
			char quote = 0;
			for (size_t i = 0; i < line.size(); ++i) {
				char c = line[i];
				if (in_str) {
					if (c == '\\') { ++i; continue; }
					if (c == quote) in_str = false;
				} else if (c == '"' || c == '\'') {
					in_str = true; quote = c;
				} else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
					break;
				} else if (c == '{') ++d;
				else if (c == '}') --d;
			}
			return d;
		}
	}

	struct minimize_stats { int checks = 0; int reductions = 0; };

	inline program minimize(program p, const diverges_fn& still_diverges, minimize_stats& stats, int max_checks = 600) {
		using namespace detail_min;
		auto check = [&](const program& cand) -> bool {
			if (stats.checks >= max_checks) return false;
			++stats.checks;
			return still_diverges(cand);
		};

		// Pass 0: drop whole segments (keep at least one).
		bool changed = true;
		while (changed && p.segments.size() > 1) {
			changed = false;
			for (size_t i = 0; i < p.segments.size() && p.segments.size() > 1; ++i) {
				program cand = p;
				cand.segments.erase(cand.segments.begin() + i);
				if (check(cand)) { p = cand; ++stats.reductions; changed = true; break; }
			}
		}

		for (size_t seg = 0; seg < p.segments.size(); ++seg) {
			bool outer_changed = true;
			while (outer_changed && stats.checks < max_checks) {
				outer_changed = false;
				auto lines = split_lines(p.segments[seg]);

				// Pass 1: ddmin-style chunk removal.
				for (size_t chunk = lines.size() / 2 ? lines.size() / 2 : 1; chunk >= 1; chunk /= 2) {
					for (size_t start = 0; start + chunk <= lines.size();) {
						std::vector<std::string> reduced;
						reduced.insert(reduced.end(), lines.begin(), lines.begin() + start);
						reduced.insert(reduced.end(), lines.begin() + start + chunk, lines.end());
						program cand = p;
						cand.segments[seg] = join_lines(reduced);
						if (check(cand)) {
							p = cand;
							lines = reduced;
							++stats.reductions;
							outer_changed = true;
						} else {
							start += chunk;
						}
					}
					if (chunk == 1) break;
				}

				// Pass 2: brace-block removal and unwrap (header line ... matching close).
				for (size_t i = 0; i < lines.size(); ++i) {
					if (brace_delta(lines[i]) <= 0) continue;
					int depth = 0;
					size_t close = i;
					bool found = false;
					for (size_t j = i; j < lines.size(); ++j) {
						depth += brace_delta(lines[j]);
						if (depth <= 0 && j > i) { close = j; found = true; break; }
					}
					if (!found) continue;
					// (a) remove whole block
					{
						std::vector<std::string> reduced(lines.begin(), lines.begin() + i);
						reduced.insert(reduced.end(), lines.begin() + close + 1, lines.end());
						program cand = p;
						cand.segments[seg] = join_lines(reduced);
						if (check(cand)) { p = cand; lines = reduced; ++stats.reductions; outer_changed = true; continue; }
					}
					// (b) unwrap: drop header + close, keep interior
					{
						std::vector<std::string> reduced(lines.begin(), lines.begin() + i);
						reduced.insert(reduced.end(), lines.begin() + i + 1, lines.begin() + close);
						reduced.insert(reduced.end(), lines.begin() + close + 1, lines.end());
						program cand = p;
						cand.segments[seg] = join_lines(reduced);
						if (check(cand)) { p = cand; lines = reduced; ++stats.reductions; outer_changed = true; }
					}
				}

				// Pass 3: per-line literal simplification (big ints -> 1, strings -> "s").
				for (size_t i = 0; i < lines.size(); ++i) {
					std::string simplified = lines[i];
					bool any = false;
					// shrink multi-digit integer literals
					std::string rebuilt;
					for (size_t j = 0; j < simplified.size();) {
						if (isdigit(static_cast<unsigned char>(simplified[j]))) {
							size_t k = j;
							while (k < simplified.size() && isdigit(static_cast<unsigned char>(simplified[k]))) ++k;
							bool is_float = (k < simplified.size() && simplified[k] == '.');
							if (!is_float && k - j > 1) { rebuilt += "1"; any = true; }
							else rebuilt += simplified.substr(j, k - j);
							j = k;
						} else {
							rebuilt += simplified[j];
							++j;
						}
					}
					if (!any) continue;
					auto candidate_lines = lines;
					candidate_lines[i] = rebuilt;
					program cand = p;
					cand.segments[seg] = join_lines(candidate_lines);
					if (check(cand)) { p = cand; lines = candidate_lines; ++stats.reductions; outer_changed = true; }
				}
			}
			p.segments[seg] = join_lines(split_lines(p.segments[seg]));
		}
		return p;
	}

	// Ready-to-paste Foundry test skeleton for a minimized repro.
	inline std::string foundry_skeleton(const program& p, const std::string& diff) {
		std::ostringstream os;
		os << "// Divergence repro, fuzz seed " << p.seed << "\n";
		os << "// Diff observed:\n";
		std::istringstream ds(diff);
		std::string line;
		while (std::getline(ds, line)) os << "//   " << line << "\n";
		os << "test(\"fuzz_seed_" << p.seed << "_divergence\", [this]() {\n";
		os << "\tstd::string out[2];\n";
		os << "\tint idx = 0;\n";
		os << "\tfor (bool use_vm : {false, true}) {\n";
		os << "\t\tauto e = jai::engine::make();\n";
		os << "\t\tif (use_vm) { e->set_backend(jai::backend_type::vm); }\n";
		os << "\t\te->execution_budget(0);\n";
		for (size_t i = 0; i < p.segments.size(); ++i) {
			os << "\t\ttry { out[idx] += e->execute(R\"__jai(\n" << p.segments[i] << ")__jai\").to_string(); }\n";
			os << "\t\tcatch (const std::exception& ex) { out[idx] += std::string(\"ERROR: \") + ex.what(); }\n";
		}
		os << "\t\t++idx;\n";
		os << "\t}\n";
		os << "\tcheck_eq(out[0], out[1], \"backend parity, fuzz seed " << p.seed << "\");\n";
		os << "});\n";
		return os.str();
	}

} // namespace jai::fuzz
