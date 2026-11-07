// Lua's tostring of a float (%.14g, plus ".0" when it reads as an integer) for 1 <= |v| < 1e13: the stdlib's
// formatFloat is fixed-point only, so 14 significant digits become 14 minus the integer digits, then trailing zeros go.
string lua_number(double v) {
	int digits = 1;
	for (double limit = 10; limit <= abs(v); limit *= 10) {
		++digits;
	}
	string text = formatFloat(v, "", 0, 14 - digits);
	int last = text.findLastNotOf("0");
	if (last == text.findFirst(".")) {
		++last;
	}
	return text.substr(0, last + 1);
}
