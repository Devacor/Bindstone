// Small helpers: math, hashing, the truecolor palette intern, text padding.
//
// Fractional constants are written as ratios such as (9.0 / 10.0) throughout:
// AngelScript 2.38 scans a literal like 0.9 with its own decimal parser, which
// is not correctly rounded (0.3, 0.012, 6.2831853 all land an ulp or more off
// the IEEE nearest), while one folded division of two exact integers is. The
// sim and frame hashes are bit-exact contracts, so every literal must be too.

const double TICK = 1.0 / 30.0;   // fixed sim timestep; --smoke feeds exactly this dt
const int64 MASK32 = 0xFFFFFFFF;

int iabs(int v) { return v < 0 ? -v : v; }
int imax(int a, int b) { return a > b ? a : b; }
int imin(int a, int b) { return a < b ? a : b; }
int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
double fclamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// FNV-1a, 32-bit lane inside an int64 (masked every round like the reference)
int64 mix32(int64 h, int64 v) {
	int64 x = h ^ (v & MASK32);
	x = x ^ ((v >> 32) & MASK32);
	return ((x & MASK32) * 16777619) & MASK32;
}

// the byte length as a signed int: length() is uint, and uint arithmetic wraps
int len(const string &in s) { return int(s.length()); }

string rep(const string &in s, int n) {
	string result;
	for (int i = 0; i < n; ++i) { result += s; }
	return result;
}

// ------------------------------------------------ truecolor palette intern --
// Every color the game shows is minted at boot / map load through rgb_idx(),
// which interns exact 24-bit colors: pixel grids store small palette indices,
// the row builder emits prebuilt truecolor escapes, and PAL_LUM feeds the
// quadrant/sextant bright/dark cell partition.
dictionary PAL_KEYS;            // "r<<16|g<<8|b" -> index
array<string> PAL_FG;           // ESC[38;2;r;g;bm per index
array<string> PAL_BG;           // ESC[48;2;r;g;bm per index
array<int> PAL_LUM;             // 2r+3g+b per index

int rgb_idx(int r, int g, int b) {
	r = iclamp(r, 0, 255);
	g = iclamp(g, 0, 255);
	b = iclamp(b, 0, 255);
	string key = formatInt(r * 65536 + g * 256 + b);
	int64 found;
	if (PAL_KEYS.get(key, found)) { return int(found); }
	int idx = int(PAL_FG.length());
	PAL_KEYS.set(key, int64(idx));
	PAL_FG.insertLast(ESC + "[38;2;" + r + ";" + g + ";" + b + "m");
	PAL_BG.insertLast(ESC + "[48;2;" + r + ";" + g + ";" + b + "m");
	PAL_LUM.insertLast(r * 2 + g * 3 + b);
	return idx;
}

// pad/truncate plain text to n columns
string pad_to(const string &in s, int n) {
	if (len(s) > n) { return s.substr(0, n); }
	return s + rep(" ", n - len(s));
}

// smallest signed angle from a to b, in (-pi, pi]
double ang_diff(double a, double b) {
	double d = b - a;
	while (d > (314159265.0 / 100000000.0)) { d -= (62831853.0 / 10000000.0); }
	while (d < -(314159265.0 / 100000000.0)) { d += (62831853.0 / 10000000.0); }
	return d;
}

double dist2(double ax, double ay, double bx, double by) {
	double dx = ax - bx;
	double dy = ay - by;
	return dx * dx + dy * dy;
}
