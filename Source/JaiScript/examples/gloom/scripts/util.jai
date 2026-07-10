// Small helpers: math, hashing, the truecolor palette intern, text padding.

var TICK = 1.0 / 30.0;   // fixed sim timestep; --smoke feeds exactly this dt

function iabs(int v) -> int { return v < 0 ? -v : v; }
function imax(int a, int b) -> int { return a > b ? a : b; }
function imin(int a, int b) -> int { return a < b ? a : b; }
function iclamp(int v, int lo, int hi) -> int { return v < lo ? lo : (v > hi ? hi : v); }
function fclamp(float v, float lo, float hi) -> float { return v < lo ? lo : (v > hi ? hi : v); }

// FNV-1a, 32-bit lane inside an int64 (checked-overflow build: mask every round)
function mix32(int h, int v) -> int {
	var x = h ^ (v & 0xFFFFFFFF);
	x = x ^ ((v >> 32) & 0xFFFFFFFF);
	return ((x & 0xFFFFFFFF) * 16777619) & 0xFFFFFFFF;
}

// ------------------------------------------------ truecolor palette intern --
// Every color the game shows is minted at boot / map load through rgb_idx(),
// which interns exact 24-bit colors: pixel grids store small palette indices,
// the row builder emits prebuilt truecolor escapes, and PAL_LUM feeds the
// quadrant/sextant bright/dark cell partition (the doom-cli trick: N pixels
// per cell, two colors chosen by luminosity).
var PAL_KEYS = {};    // r<<16|g<<8|b -> index
var PAL_FG = [];      // ESC[38;2;r;g;bm per index
var PAL_BG = [];      // ESC[48;2;r;g;bm per index
array<int> PAL_LUM = [];     // 2r+3g+b per index

function rgb_idx(int r, int g, int b) -> int {
	r = iclamp(r, 0, 255);
	g = iclamp(g, 0, 255);
	b = iclamp(b, 0, 255);
	int key = r * 65536 + g * 256 + b;
	if (PAL_KEYS.has(key)) { return PAL_KEYS[key]; }
	int idx = PAL_FG.size();
	PAL_KEYS[key] = idx;
	PAL_FG.push(`${ESC}[38;2;${r};${g};${b}m`);
	PAL_BG.push(`${ESC}[48;2;${r};${g};${b}m`);
	PAL_LUM.push(r * 2 + g * 3 + b);
	return idx;
}

function col(string code, string text) -> string {
	return `${ESC}[${code}m${text}${ESC}[0m`;
}

// pad/truncate plain text to n columns
function pad_to(string s, int n) -> string {
	if (s.size() > n) { return s.substr(0, n); }
	return s + " ".repeat(n - s.size());
}

// smallest signed angle from a to b, in (-pi, pi]
function ang_diff(float a, float b) -> float {
	float d = b - a;
	while (d > 3.14159265) { d -= 6.2831853; }
	while (d < -3.14159265) { d += 6.2831853; }
	return d;
}

function dist2(float ax, float ay, float bx, float by) -> float {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}
