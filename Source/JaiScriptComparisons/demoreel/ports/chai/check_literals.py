#!/usr/bin/env python3
"""List (or with --fix rewrite) the float literals ChaiScript 6.1 parses to a different double than C++ does.

ChaiScript's parse_num<double> (chaiscript_defines.hpp) adds each fraction digit as digit / 10^k, so a literal such as
0.82 can land one ulp from the correctly rounded double the JaiScript VM uses. --fix respells each such literal as a
parenthesized quotient of two exactly parsed constants, (82.0 / 100.0), which ChaiScript folds at parse time to the
correctly rounded value. Usage: check_literals.py [--fix] file.chai...; exits 1 when a checked file has one.
"""
import re
import sys

LITERAL = re.compile(r"(?<![\w.])\d+\.\d+(?![\w.eE])")


def chai_value(text):
	result = 0.0
	place = 0.0
	for c in text:
		if c == ".":
			place = 10.0
		elif place < 10.0:
			result = result * 10.0 + float(ord(c) - 48)
		else:
			result += float(ord(c) - 48) / place
			place *= 10.0
	return result


def exact_spelling(text):
	whole, frac = text.split(".")
	numerator, denominator = f"{int(whole + frac)}.0", f"1{'0' * len(frac)}.0"
	assert chai_value(numerator) == float(numerator) and chai_value(denominator) == float(denominator)
	assert float(numerator) / float(denominator) == float(text), text
	return f"({numerator} / {denominator})"


def main():
	fix = "--fix" in sys.argv[1:]
	files = [a for a in sys.argv[1:] if a != "--fix"]
	found = 0
	for path in files:
		lines = open(path, encoding="utf-8", newline="").read().split("\n")
		bad = []
		for i, line in enumerate(lines):
			code, sep, comment = line.partition("//")
			def respell(match):
				text = match.group(0)
				if chai_value(text) == float(text):
					return text
				bad.append(text)
				return exact_spelling(text) if fix else text
			lines[i] = LITERAL.sub(respell, code) + sep + comment
		found += len(bad)
		print(f"{path}: {len(bad)} {' '.join(sorted(set(bad)))}")
		if fix and bad:
			open(path, "w", encoding="utf-8", newline="").write("\n".join(lines))
	return 1 if found and not fix else 0


if __name__ == "__main__":
	sys.exit(main())
