#pragma once

// Run-time unit band: every segment of the program reaches a unit through a computed path ahead of its own statements, so each is a
// unit whose import the checker cannot follow, and the proofs its calls take of program-scope names are the ones a graft's guard
// checks (internal/type_proofs_design.md 4.7). The unit declares a name no segment uses. Its own RNG stream and name prefix `zru`:
// the main program's draws are unchanged.

#include "fuzz_harness.hpp"

#include <filesystem>
#include <string>

namespace jai::fuzz {

	class runtime_unit_band {
	public:
		explicit runtime_unit_band(uint64_t seed) : rng_(mix64(seed + 0x6A09E667F3BCC909ULL)), seed_(seed) {}

		bool roll() { return rng_.chance(25); }

		void apply(program& p) {
			const std::string path = (std::filesystem::temp_directory_path() / "jaiscript_fuzz" / ("zru" + std::to_string(seed_)) / "zruunit.jai").generic_string();
			p.files.emplace_back(path, "inline int zru_loads;\nzru_loads += 1;\n");
			for (std::string& segment : p.segments) { segment = "string zru_path = \"" + path + "\";\nimport(zru_path);\n" + segment; }
		}

	private:
		fuzz_rng rng_;
		uint64_t seed_;
	};

} // namespace jai::fuzz
