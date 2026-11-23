#pragma once

// Unwind band: a class whose binary, comparison and subscript operators and whose to_string throw on some inputs, used as the
// right side, an operand, an index and a text piece of stores, declarations, compounds and conditions, each in its own try:
// the caught value and every target's value after each round are the string. A backend that stores, declares or tests on the
// value an unwinding operator left shows there. Its own RNG stream and name prefix `zu`: the main program's draws are unchanged.

#include "fuzz_harness.hpp"

#include <string>

namespace jai::fuzz {

	class unwind_band {
	public:
		explicit unwind_band(uint64_t seed) : rng_(mix64(seed + 0xA0761D6478BD642FULL)) {}

		bool roll() { return rng_.chance(25); }

		std::string emit() {
			const int64_t modulus = rng_.range(2, 4);
			auto residue = [&]() { return std::to_string(rng_.range(0, modulus - 1)); };
			const std::string m = std::to_string(modulus);
			std::string out = "class zuC {\n\tint n = 1;\n\tzuC() {}\n\tzuC(int zua) { n = zua; }\n"
			                  "\tzuC operator+(int zuo) { if (zuo % " + m + " == " + residue() + ") { throw \"zu-plus\"; } return zuC(n + zuo); }\n"
			                  "\tbool operator<(int zuo) { if (zuo % " + m + " == " + residue() + ") { throw \"zu-less\"; } return n < zuo; }\n"
			                  "\tbool operator==(int zuo) { if (zuo % " + m + " == " + residue() + ") { throw \"zu-eq\"; } return n == zuo; }\n"
			                  "\tint operator[](int zui) { if (zui % " + m + " == " + residue() + ") { throw \"zu-index\"; } return n + zui; }\n"
			                  "\tstring to_string() { if (n % " + m + " == " + residue() + ") { throw \"zu-text\"; } return `<${n}>`; }\n}\n"
			                  "class zuH {\n\tzuC c = zuC(1);\n\tvar v = zuC(2);\n"
			                  "\tvoid put(int zuk) { c = c + zuk; }\n\tvoid add(int zuk) { v += zuk; }\n}\n";
			const char* uses[] = {
				"zue = zue + zuk;",
				"zuv = zuv + zuk;",
				"zuw = zue + zuk; zuw = zuw.n;",
				"zuC zud = zue + zuk; zur = zud.n;",
				"zur = zue[zuk] + 1;",
				"zuw = zuv[zuk] * 2;",
				"zub = zue < zuk;",
				"zub = zue == zuk;",
				"zuw = zue == zuk;",
				"if (zue < zuk) { zur += 1; }",
				"zus = \"c\" + zue;",
				"zus = \"c\" + zuv + \"d\";",
				"for (auto& zuel : zua) { zuel = zuel + zuk; }",
				"zua[0] = zua[1] + zuk;",
				"zui[zue[zuk] % 3] = zuk;",
				"zur = zui[zue[zuk] % 3];",
				"zuh.put(zuk);",
				"zuh.c = zue + zuk;",
				"zue += zuk;",
				"zua[1] += zuk;",
				"zuh.add(zuk);",
				"zuh.v += zuk;",
			};
			std::string body;
			for (const char* use : uses) {
				if (!rng_.chance(70)) { continue; }
				body += std::string("\t\ttry { ") + use + " } catch (zux) { zuout += `!${zux}`; }\n";
			}
			const std::string rounds = std::to_string(rng_.range(3, 6));
			out += "string zurun(int zucount) {\n\tstring zuout = \"\";\n\tzuC zue = zuC(1);\n\tvar zuv = zuC(2);\n\tint zur = 0;\n\tvar zuw = 0;\n"
			       "\tbool zub = false;\n\tstring zus = \"s\";\n\tarray<zuC> zua = [zuC(3), zuC(4)];\n\tarray<int> zui = [1, 2, 3];\n\tzuH zuh;\n"
			       "\tfor (int zuk = 0; zuk < zucount; ++zuk) {\n" + body +
			       "\t\tzuout += `${zue.n},${zuv.n},${zur},${zuw},${zub},${zus},${zua[0].n},${zua[1].n},${zui},${zuh.c.n},${zuh.v.n};`;\n\t}\n"
			       "\treturn zuout;\n}\n";
			// the same right sides over program-scope targets, which a function's frame slots do not reach
			out += "zuC zug = zuC(2);\nvar zugw = 0;\nstring zugs = \"g\";\nstring zugo = \"\";\n"
			       "for (int zugk = 0; zugk < " + rounds + "; ++zugk) {\n"
			       "\ttry { zug = zug + zugk; } catch (zux) { zugo += `!${zux}`; }\n"
			       "\ttry { zugw = zug[zugk] + 1; } catch (zux) { zugo += `!${zux}`; }\n"
			       "\ttry { zugs = \"g\" + zug; } catch (zux) { zugo += `!${zux}`; }\n"
			       "\ttry { zug += zugk; } catch (zux) { zugo += `!${zux}`; }\n"
			       "\tzugo += `${zug.n},${zugw},${zugs};`;\n}\n";
			out += "zurun(" + rounds + ") + zugo;\n";
			return out;
		}

	private:
		fuzz_rng rng_;
	};

} // namespace jai::fuzz
