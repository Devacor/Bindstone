// Small helpers: colors, hashing, direction tables, misc math.

var MAP_W = 64;
var MAP_H = 20;

// ANSI foreground color codes (strings; the lexer has no \x escape, so the
// host hands us ESC as a global).
function col(string code, string text) -> string {
	return `${ESC}[${code}m${text}${ESC}[0m`;
}

function col256(int code, string text) -> string {
	return `${ESC}[38;5;${code}m${text}${ESC}[0m`;
}

// FNV-1a, 32-bit lane inside an int64 (checked-overflow build: a real 64-bit
// FNV multiply would raise, so we mask every round).
function mix32(int h, int v) -> int {
	var x = h ^ (v & 0xFFFFFFFF);
	x = x ^ ((v >> 32) & 0xFFFFFFFF);
	return ((x & 0xFFFFFFFF) * 16777619) & 0xFFFFFFFF;
}

// Movement keys -> [dx, dy]. Classic vi keys plus diagonals plus arrows,
// plus wasd for cardinal moves (diagonals stay on yubn).
var DIR_KEYS = {
	"h": [-1, 0], "l": [1, 0], "k": [0, -1], "j": [0, 1],
	"y": [-1, -1], "u": [1, -1], "b": [-1, 1], "n": [1, 1],
	"w": [0, -1], "a": [-1, 0], "s": [0, 1], "d": [1, 0],
	"left": [-1, 0], "right": [1, 0], "up": [0, -1], "down": [0, 1]
};

var EIGHT_DIRS = [[-1, 0], [1, 0], [0, -1], [0, 1], [-1, -1], [1, -1], [-1, 1], [1, 1]];
var FOUR_DIRS = [[-1, 0], [1, 0], [0, -1], [0, 1]];

function iabs(int v) -> int { return v < 0 ? -v : v; }
function imax(int a, int b) -> int { return a > b ? a : b; }
function imin(int a, int b) -> int { return a < b ? a : b; }
function iclamp(int v, int lo, int hi) -> int { return v < lo ? lo : (v > hi ? hi : v); }

// Chebyshev distance: adjacency in 8 directions.
function cheb(int x1, int y1, int x2, int y2) -> int {
	return imax(iabs(x1 - x2), iabs(y1 - y2));
}

function idx(int x, int y) -> int { return y * MAP_W + x; }
function in_bounds(int x, int y) -> bool {
	return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H;
}

function pick(var rng, var arr) -> var {
	return arr[rng.next(arr.size())];
}

// Weighted pick from [[weight, value], ...]
function pick_weighted(var rng, var pairs) -> var {
	var total = 0;
	for (auto p : pairs) { total = total + p[0]; }
	var roll = rng.next(total);
	for (auto p : pairs) {
		if (roll < p[0]) { return p[1]; }
		roll = roll - p[0];
	}
	return pairs[pairs.size() - 1][1];
}
