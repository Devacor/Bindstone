// The first-person view: a DDA raycaster locked to the four cardinal facings,
// drawn into a 24-bit pixel framebuffer at twice the text rows (JaiDOOM's
// method: each cell is an upper-half block, fg = top pixel, bg = bottom pixel,
// escapes only when a color changes along the row). Procedural textures per
// wall material, floor/ceiling casting with fog, billboard pixel-art sprites
// behind a per-column z-buffer, and the torch you are holding.

int VIEW_W = 64;            // text cells; the framebuffer is VIEW_W x VIEW_H*2
int VIEW_H = 20;
float PLANE_LEN = 0.9;      // half-FOV tangent: 0.9 ~ 84 degrees
float WALL_K = 1.3;         // wall height at distance 1, in framebuffer heights
float SPRITE_RANGE = 14.0;  // torchlight: sprites past this are swallowed by dark
float SPRITE_ASPECT = 0.85;
int VIEW_WORKERS = 0;       // nonzero: parallel_transform the blit rows (pays off on wide views)
var VIEW_PROBE = null;      // headless frame tap: called once per smoke turn when set

// facing: 0=N 1=E 2=S 3=W (y grows downward, so north is -y)
var FACE_DX = [0, 1, 0, -1];
var FACE_DY = [-1, 0, 1, 0];
var FACE_LETTER = ["N", "E", "S", "W"];
var FACE_NAME = ["NORTH", "EAST", "SOUTH", "WEST"];

function facing_for(int dx, int dy) -> int {
	if (dy < 0) { return 0; }
	if (dx > 0) { return 1; }
	if (dy > 0) { return 2; }
	return 3;
}

// ANSI SGR color codes (the bestiary/item identity colors) -> packed rgb
var ANSI_RGB = {
	"30": 0x282828, "31": 0xCD3C32, "32": 0x28AF5A, "33": 0xC8A03C,
	"34": 0x3C6EC8, "35": 0xAF50BE, "36": 0x32AABE, "37": 0xC8C8C8,
	"90": 0x828287, "91": 0xFF5A50, "92": 0x6EF082, "93": 0xFFE65A,
	"94": 0x788CFF, "95": 0xFF82E6, "96": 0x82F0FA, "97": 0xFFFFFF
};

// -------------------------------------------------------- depth themes -----
// Moraff-grade maximalism: every couple of floors the dungeon changes wardrobe.
// pattern: 0 mossy brick, 1 bloody brick, 2 fungus blocks, 3 riveted panels,
// 4 ember-cracked rock, 5 veined marble.
var DEPTH_THEMES = [
	{"name": "Mossrot Warrens",       "pattern": 0, "wall": 0x6E8C4A, "mortar": 0x3A3428, "accent": 0x8CF03C,
	 "glow": 0xC8FF6E, "ceil": 0x2E3A6E, "floor": 0x68522E, "fog": 0x080C1E},
	{"name": "Bloodbrick Vaults",     "pattern": 1, "wall": 0xC44034, "mortar": 0x4A2A2E, "accent": 0x7A0C18,
	 "glow": 0xFFAA78, "ceil": 0x3A1A42, "floor": 0x786256, "fog": 0x1A060E},
	{"name": "Gilded Fungus Halls",   "pattern": 2, "wall": 0xAC3EC4, "mortar": 0x2A1036, "accent": 0x3CD2A0,
	 "glow": 0xFFD65A, "ceil": 0x144650, "floor": 0x967A24, "fog": 0x08181C},
	{"name": "Screaming Copper Vein", "pattern": 3, "wall": 0x24BAB0, "mortar": 0x0E2A30, "accent": 0xFF963C,
	 "glow": 0xFFE08C, "ceil": 0x46104E, "floor": 0xB0601E, "fog": 0x1E0622},
	{"name": "The Ashen Throat",      "pattern": 4, "wall": 0x8C8474, "mortar": 0x241E1C, "accent": 0x5A4A3C,
	 "glow": 0xFF6E1E, "ceil": 0x36343A, "floor": 0x7C7060, "fog": 0x16100E},
	{"name": "The Hollow Gallery",    "pattern": 5, "wall": 0x8E8E94, "mortar": 0x3C3C42, "accent": 0x5C5A60,
	 "glow": 0xDCDCE6, "ceil": 0x26262A, "floor": 0x4A443E, "fog": 0x0C0C0E}
];

function theme_index(int floor) -> int {
	if (floor >= 10) { return 5; }
	if (floor >= 9) { return 4; }
	return (floor - 1) / 2;
}

function theme_name(int floor) -> string {
	return DEPTH_THEMES[theme_index(floor)]["name"];
}

// ---------------------------------------------------------- framebuffer ----
int PW = 0;
int PH = 0;
float WALL_KPX = 0.0;
array<int> VPIX = [];
array<float> ZBUF = [];
array<int> VROWS = [];
array<float> ROW_DIST = [];
array<float> ROW_LIGHT = [];
array<float> BAYER = [];
string VRESET = "";
string HALF_BLOCK = "";

map<int, int> VPAL_KEYS = {};
array<string> VPAL_FG = [];
array<string> VPAL_BG = [];
array<int> VPAL_RGB = [];

function utf8_char(int cp) -> string {
	if (cp < 128) { return "" + to_char(cp); }
	if (cp < 2048) { return "" + to_char(192 + (cp >> 6)) + to_char(128 + (cp & 63)); }
	return "" + to_char(224 + (cp >> 12)) + to_char(128 + ((cp >> 6) & 63)) + to_char(128 + (cp & 63));
}

function pal(int r, int g, int b) -> int {
	r = iclamp(r, 0, 255);
	g = iclamp(g, 0, 255);
	b = iclamp(b, 0, 255);
	int key = (r << 16) | (g << 8) | b;
	if (VPAL_KEYS.has(key)) { return VPAL_KEYS[key]; }
	int i = VPAL_FG.size();
	VPAL_KEYS[key] = i;
	VPAL_FG.push(`${ESC}[38;2;${r};${g};${b}m`);
	VPAL_BG.push(`${ESC}[48;2;${r};${g};${b}m`);
	VPAL_RGB.push(key);
	return i;
}

// ---------------------------------------------------------------- ramps ----
// A ramp is 16 palette indices from fog (level 0) to torch-lit (level 15),
// warming as it brightens. Glow ramps ignore the dark.
array<int> RAMP = [];
map<int, int> RAMP_CACHE = {};
int FOG_RGB = 0;

function lerp_i(int a, float b, float t) -> int { return a + (b - a) * t; }

function ramp(int rgb) -> int {
	if (RAMP_CACHE.has(rgb)) { return RAMP_CACHE[rgb]; }
	int base = RAMP.size();
	RAMP_CACHE[rgb] = base;
	int r = (rgb >> 16) & 255;
	int g = (rgb >> 8) & 255;
	int b = rgb & 255;
	int fr = (FOG_RGB >> 16) & 255;
	int fg = (FOG_RGB >> 8) & 255;
	int fb = FOG_RGB & 255;
	for (int l = 0; l < 16; ++l) {
		float t = l / 15.0;
		float lit = 0.3 + 0.85 * t;
		RAMP.push(pal(lerp_i(fr, r * lit * (1.0 + 0.12 * t), t), lerp_i(fg, g * lit, t), lerp_i(fb, b * lit * (1.0 - 0.12 * t), t)));
	}
	return base;
}

function glow(int rgb) -> int {
	int key = rgb | 0x1000000;
	if (RAMP_CACHE.has(key)) { return RAMP_CACHE[key]; }
	int base = RAMP.size();
	RAMP_CACHE[key] = base;
	int c = pal((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
	for (int l = 0; l < 16; ++l) { RAMP.push(c); }
	return base;
}

function scale_rgb(int rgb, float k) -> int {
	int r = iclamp(((rgb >> 16) & 255) * k, 0, 255);
	int g = iclamp(((rgb >> 8) & 255) * k, 0, 255);
	int b = iclamp((rgb & 255) * k, 0, 255);
	return (r << 16) | (g << 8) | b;
}

function tint_rgb(int rgb, int to, float t) -> int {
	int r = lerp_i((rgb >> 16) & 255, (to >> 16) & 255, t);
	int g = lerp_i((rgb >> 8) & 255, (to >> 8) & 255, t);
	int b = lerp_i(rgb & 255, to & 255, t);
	return (r << 16) | (g << 8) | b;
}

// integer lattice hash: textures and tile variants never touch the game RNG
function lat(int x, int y, int s) -> int {
	int h = (x * 374761393 + y * 668265263 + s * 1442695041) & 0xFFFFFF;
	h = (h ^ (h >> 7)) * 1274126177;
	h = h & 0x7FFFFFFF;
	return (h ^ (h >> 15)) & 0xFFFF;
}

function lightf(float d) -> float {
	float l = 1.7 / (1.0 + 0.4 * d + 0.05 * d * d);
	return l > 1.0 ? 1.0 : l;
}

// ------------------------------------------------------------- textures ----
// Textures are 32x32. Texels are material codes while generating, ramp bases
// once baked. Wall codes: 0 mortar 1 base 2 base-dark 3 base-lit 4 accent
// 5 accent-dark 6 glow 7 void 8 bone 9 iron 10 flame 11 flame-core 12 white-hot.
array<int> WTEX = [];       // 4 textures: base, torch A, torch B, skull niche
array<int> FTEX = [];       // 6 textures: flags, puddle, bones, moss, blood, stair runes
array<int> CTEX = [];       // 1 texture: slabs and beams
array<int> WALL_KIND = [];  // per tile: 0 plain, 1 torch sconce, 2 skull niche
array<int> FLOOR_KIND = []; // per tile: FTEX offset

function tex_brick(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		int row = v / 8;
		int bv = v % 8;
		for (int u = 0; u < 32; ++u) {
			int uu = (u + (row % 2) * 8) % 32;
			int bu = uu % 16;
			int brick = uu / 16 + row * 2;
			int code = 1;
			if (bv == 7 || bu == 15) { code = 0; }
			else {
				int tone = lat(brick, row, seed) % 5;
				code = tone == 0 ? 2 : (tone == 1 ? 3 : 1);
				if (bv == 0 || bu == 0) { code = code == 2 ? 1 : 3; }
				else if (bv == 6 || bu == 14) { code = 2; }
				else if (lat(u, v, seed + 1) % 13 == 0) { code = code == 3 ? 1 : 2; }
				if (lat(brick, 3, seed) % 4 == 0 && bu > 3 && bu < 7 && bv > 1 && bv < 4) { code = 0; }
			}
			t[v * 32 + u] = code;
		}
	}
}

function tex_block(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		int row = v / 16;
		int bv = v % 16;
		for (int u = 0; u < 32; ++u) {
			int uu = (u + row * 8) % 32;
			int bu = uu % 16;
			int code = 1;
			if (bu == 15 || bv == 15) { code = 0; }
			else if (bu == 0 || bv == 0) { code = 3; }
			else if (bu == 14 || bv == 14) { code = 2; }
			else if (lat(u, v, seed) % 9 == 0) { code = 2; }
			else if (lat(u / 4, v / 4, seed + uu / 16 + row * 2) % 5 == 0) { code = 3; }
			t[v * 32 + u] = code;
		}
	}
}

function tex_panel(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			int bu = u % 16;
			int bv = v % 16;
			bool ru = bu == 3 || bu == 4 || bu == 11 || bu == 12;
			bool rv = bv == 3 || bv == 4 || bv == 11 || bv == 12;
			int code = 1;
			if (bu == 0 || bv == 0) { code = 7; }
			else if (bu == 1 || bv == 1) { code = 3; }
			else if (bu == 15 || bv == 15) { code = 2; }
			else if (ru && rv) { code = (bu == 3 || bu == 11) && (bv == 3 || bv == 11) ? 3 : 4; }
			else if (lat(u / 8, v, seed) % 7 == 0) { code = 2; }
			t[v * 32 + u] = code;
		}
	}
	// orange weeping under the rivets
	for (int u = 0; u < 32; ++u) {
		int bu = u % 16;
		if (bu != 3 && bu != 4 && bu != 11 && bu != 12) { continue; }
		for (int r = 0; r < 4; ++r) {
			int top = (r / 2) * 16 + (r % 2 == 0 ? 5 : 13);
			int len = lat(u, r, seed + 5) % 7;
			for (int k = 0; k < len; ++k) {
				int v = top + k;
				if (v < 32 && v % 16 < 15 && v % 16 != 11 && v % 16 != 12) { t[v * 32 + u] = 5; }
			}
		}
	}
}

function tex_rock(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			int tone = lat((u + (v / 6) * 3) / 6, v / 6, seed) % 4;
			int code = tone == 0 ? 2 : (tone == 1 ? 3 : 1);
			if (lat(u, v, seed + 2) % 7 == 0) { code = 2; }
			t[v * 32 + u] = code;
		}
	}
	// ember cracks: a vertical meander and a branch that wanders off it
	int cu = 10;
	for (int v = 0; v < 32; ++v) {
		t[v * 32 + cu] = 6;
		t[v * 32 + cu + 1] = 7;
		cu = iclamp(cu + lat(v, 1, seed + 3) % 3 - 1, 3, 27);
	}
	int cv = 18;
	for (int u = 12; u < 32; ++u) {
		t[cv * 32 + u] = 6;
		t[(cv + 1) * 32 + u] = 7;
		cv = iclamp(cv + lat(u, 2, seed + 4) % 3 - 1, 8, 28);
	}
}

function tex_marble(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			float s = sin(u * 0.28 + v * 0.15 + 2.0 * sin(v * 0.22 + u * 0.05));
			int code = 1;
			if (s > 0.95) { code = 5; }
			else if (s > 0.84) { code = 2; }
			else if (lat(u, v, seed) % 17 == 0) { code = 3; }
			if (v <= 1 || v == 29) { code = 3; }
			if (v == 2 || v == 30) { code = 2; }
			if (v == 31) { code = 0; }
			t[v * 32 + u] = code;
		}
	}
}

function tex_moss(var& t, int seed) -> void {
	for (int u = 0; u < 32; ++u) {
		int len = lat(u / 2, 0, seed) % 9 + (lat(u / 2, 1, seed) % 4 == 0 ? 9 : 0) + lat(u, 2, seed) % 2;
		for (int v = 0; v < len; ++v) { t[v * 32 + u] = v == len - 1 ? 5 : 4; }
	}
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			if (lat(u / 2, v / 2, seed + 9) % 31 == 0) { t[v * 32 + u] = 4; }
		}
	}
}

function tex_blood(var& t, int seed) -> void {
	for (int u = 0; u < 32; ++u) {
		if (lat(u, 3, seed) % 4 != 0) { continue; }
		int len = 4 + lat(u, 4, seed) % 16;
		for (int v = 0; v < len; ++v) {
			if (t[v * 32 + u] != 0) { t[v * 32 + u] = 5; }
		}
	}
}

function tex_mushrooms(var& t, int seed) -> void {
	for (int b = 0; b < 4; ++b) {
		if (lat(b, 7, seed) % 5 < 2) { continue; }
		int row = b / 2;
		int cu = (b % 2) * 16 - row * 8 + 4 + lat(b, 8, seed) % 8 + 32;
		int v0 = row * 16 + 7;
		for (int du = -1; du <= 1; ++du) { t[v0 * 32 + (cu + du) % 32] = 6; }
		for (int du = -2; du <= 2; ++du) { t[(v0 + 1) * 32 + (cu + du) % 32] = 6; }
		for (int du = -3; du <= 3; ++du) { t[(v0 + 2) * 32 + (cu + du) % 32] = 4; }
		for (int dv = 3; dv <= 6; ++dv) { t[(v0 + dv) * 32 + cu % 32] = 8; }
		t[(v0 + 6) * 32 + (cu - 1) % 32] = 8;
		t[(v0 + 6) * 32 + (cu + 1) % 32] = 8;
	}
}

function stamp(var& t, int u0, int v0, var rows, string key, int code) -> void {
	for (int r = 0; r < rows.size(); ++r) {
		string row = rows[r];
		for (int c = 0; c < row.size(); ++c) {
			if (row.substr(c, 1) == key) { t[(v0 + r) * 32 + u0 + c] = code; }
		}
	}
}

function tex_torch(var& t, int frame) -> void {
	// the torchlight halo first, then bracket and flame over it
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			int du = u - 16;
			int dv = v - 10;
			if (du * du + dv * dv < 110 && t[v * 32 + u] != 0) { t[v * 32 + u] = 3; }
		}
	}
	for (int v = 16; v < 27; ++v) { t[v * 32 + 15] = 9; t[v * 32 + 16] = 9; }
	for (int u = 12; u < 20; ++u) { t[15 * 32 + u] = 9; }
	for (int u = 13; u < 19; ++u) { t[16 * 32 + u] = 9; }
	for (int u = 14; u < 18; ++u) { t[26 * 32 + u] = 9; t[27 * 32 + u] = 9; }
	var flame = frame == 0
		? ["............", ".....a......", ".....aa.....", "....aba.....", "....abaa....", "...aabba....",
		   "...abbba....", "..aabbbaa...", "..abbcbba...", "..abbccba...", "..abcccba...", "...abcba....", "....aaa....."]
		: ["............", "......a.....", ".....aa.....", ".....aba....", "....aaba....", "....abbaa...",
		   "...abbba....", "..aabbbba...", "..abbcbba...", "..abccbba...", "..abcccba...", "...abcba....", "....aaa....."];
	stamp(t, 10, 2, flame, "a", 10);
	stamp(t, 10, 2, flame, "b", 11);
	stamp(t, 10, 2, flame, "c", 12);
}

function tex_skull(var& t) -> void {
	for (int v = 5; v < 30; ++v) {
		for (int u = 7; u < 25; ++u) {
			int du = u * 2 - 31;
			int dv = (13 - v) * 2;
			if (v >= 13 || du * du + dv * dv < 300) { t[v * 32 + u] = 7; }
		}
	}
	for (int u = 6; u < 26; ++u) { t[30 * 32 + u] = 3; t[31 * 32 + u] = 2; }
	var skull = ["..bbbbbb..", ".bbbbbbbb.", "bbbbbbbbbb", "bb..bb..bb", "bb..bb..bb",
		"bbbb..bbbb", ".bbbbbbbb.", "..b.bb.b..", "..bbbbbb..", "...b..b..."];
	stamp(t, 11, 18, skull, "b", 8);
}

// Floor codes: 0 grout 1 base 2 dark 3 lit 4 accent 5 water 6 water-glint 7 bone 8 rune 9 blood.
function tex_flags(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		int split = v < 16 ? 19 : 9;
		for (int u = 0; u < 32; ++u) {
			int stone = (v < 16 ? 0 : 2) + (u < split ? 0 : 1);
			int code = 1;
			if (u == 0 || v == 0 || v == 16 || u == split) { code = 0; }
			else {
				code = lat(stone, 5, seed) % 3 == 0 ? 2 : 1;
				if (u == 1 || v == 1 || v == 17 || u == split + 1) { code = 3; }
				else if (u == 31 || v == 15 || v == 31 || u == split - 1) { code = 2; }
				else if (lat(u / 3, v / 3, seed + stone) % 6 == 0) { code = 2; }
				else if (lat(u, v, seed + 6) % 11 == 0) { code = 3; }
			}
			t[v * 32 + u] = code;
		}
	}
}

function tex_floor_decor(var& t, int kind, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			int du = u * 2 - 31;
			int dv = v * 2 - 33;
			int r2 = du * du + dv * dv * 2;
			int code = t[v * 32 + u];
			if (kind == 1) {
				if (r2 < 560) { code = (u + v == 26 || u + v == 27 || u + v == 35) ? 6 : 5; }
				else if (r2 < 700) { code = 2; }
			} else if (kind == 3) {
				if (lat(u / 3, v / 3, seed + 11) % 4 == 0 && lat(u, v, seed) % 3 != 0) { code = 4; }
			} else if (kind == 4) {
				int bu = u - 14;
				int bv = v - 18;
				int b2 = bu * bu + bv * bv;
				if (b2 < 26 || (b2 < 80 && lat(u, v, seed + 12) % 3 == 0) || (b2 < 200 && lat(u, v, seed + 13) % 17 == 0)) { code = 9; }
			} else if (kind == 5) {
				int q = du * du + (v * 2 - 31) * (v * 2 - 31);
				int m = iabs(du) + iabs(v * 2 - 31);
				code = 2;
				if (q > 600 && q < 820) { code = 8; }
				if (m < 16 && m > 10) { code = 8; }
				if ((du == v * 2 - 31 || du == 31 - v * 2) && q > 250 && q < 600) { code = 8; }
			}
			t[v * 32 + u] = code;
		}
	}
	if (kind == 2) {
		var bones = ["..............", ".77.......7...", "..777....777..", "....777..7....", "......777.....",
			"....7...777...", "...777....77..", "....7.........", ".........77...", "........7..7..",
			"........7..7..", ".........77..."];
		for (int r = 0; r < 24; ++r) {
			string row = bones[r / 2];
			for (int c = 0; c < 28; ++c) {
				if (row.substr(c / 2, 1) == "7") { t[(r + 4) * 32 + c + 2] = 7; }
			}
		}
	}
}

// Ceiling codes: 0 joint 1 slab 2 dark 3 lit 4 beam 5 beam-dark 6 root.
function tex_ceiling(var& t, int seed) -> void {
	for (int v = 0; v < 32; ++v) {
		for (int u = 0; u < 32; ++u) {
			int code = 1;
			if (v >= 12 && v <= 19) {
				code = (v == 12 || v == 19 || lat(u / 5, v, seed) % 4 == 0) ? 5 : 4;
			} else if (u % 16 == 0 || v == 0 || v == 31) { code = 0; }
			else if (lat(u, v, seed + 1) % 9 == 0) { code = 2; }
			else if (lat(u / 3, v / 3, seed + 2) % 9 == 0) { code = 3; }
			t[v * 32 + u] = code;
		}
	}
	for (int k = 0; k < 5; ++k) {
		int u = 1 + lat(k, 9, seed) % 30;
		int len = 3 + lat(k, 10, seed) % 6;
		int v0 = lat(k, 11, seed) % 2 == 0 ? 1 : 21;
		for (int d = 0; d < len; ++d) { t[(v0 + d) * 32 + u] = 6; }
	}
}

function bake(var& dst, var& codes, var& mats) -> void {
	for (int i = 0; i < 1024; ++i) { dst.push(mats[codes[i]]); }
}

function fresh_codes() -> array<int> {
	array<int> t = [];
	for (int i = 0; i < 1024; ++i) { t.push(0); }
	return t;
}

// ------------------------------------------------------------ view cache ---
// Rebuilt whenever the floor or view size changes (or after a hot reload /
// load wipes it): theme ramps, baked textures, per-tile wall and floor
// variants, landmark sprites, per-row floor distances.
int VC_FLOOR = -1;
array<int> ROLE_RAMP = [];
array<int> FLASH_TAB = [];
int HURT_RAMP = 0;
int BAR_GOOD = 0;
int BAR_BAD = 0;
array<int> LM_X = [];
array<int> LM_Y = [];
array<int> LM_ART = [];

function view_cache_invalidate() -> void { VC_FLOOR = -1; }

// visual-only combat marks: the turn each monster was last hit, and the blood
// its death leaves on this floor's flagstones
map<int, int> VIEW_HITS = {};
array<int> DECALS = [];
int DECAL_FLOOR = -1;

function view_note_hit(int id) -> void { VIEW_HITS[id] = G.turn; }

function view_note_kill(int x, int y) -> void {
	if (DECAL_FLOOR != G.floor) { DECALS = []; DECAL_FLOOR = G.floor; }
	DECALS.push(idx(x, y));
	if (VC_FLOOR == G.floor) { apply_decals(); }
}

function apply_decals() -> void {
	if (DECAL_FLOOR != G.floor) { return; }
	for (int i = 0; i < DECALS.size(); ++i) {
		int t = DECALS[i];
		if (FLOOR_KIND[t] != 5 * 1024) { FLOOR_KIND[t] = 4 * 1024; }
	}
}

function build_frame_buffers() -> void {
	PW = VIEW_W;
	PH = VIEW_H * 2;
	WALL_KPX = WALL_K * PH;
	VPIX = [];
	for (int i = 0; i < PW * PH; ++i) { VPIX.push(0); }
	ZBUF = [];
	for (int i = 0; i < PW; ++i) { ZBUF.push(0.0); }
	VROWS = [];
	for (int r = 0; r < VIEW_H; ++r) { VROWS.push(r); }
	ROW_DIST = [];
	ROW_LIGHT = [];
	for (int y = 0; y < PH; ++y) {
		float off = y + 0.5 - PH * 0.5;
		if (off < 0.0) { off = -off; }
		float d = WALL_KPX * 0.5 / off;
		ROW_DIST.push(d);
		ROW_LIGHT.push(lightf(d) * 15.0);
	}
	BAYER = [];
	var order = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
	for (int i = 0; i < 16; ++i) { int o = order[i]; BAYER.push((o + 0.5) / 16.0); }
	VRESET = ESC + "[0m";
	HALF_BLOCK = utf8_char(0x2580);
	VIEW_WORKERS = PW >= 160 ? 1 : 0;
}

function build_view_cache() -> void {
	if (PW != VIEW_W || PH != VIEW_H * 2) { build_frame_buffers(); }
	if (ART_W.size() == 0) { build_art(); }
	VC_FLOOR = G.floor;
	int ti = theme_index(G.floor);
	var theme = DEPTH_THEMES[ti];
	int pattern = theme["pattern"];
	int wall = theme["wall"];
	int accent = theme["accent"];
	int glow_rgb = theme["glow"];
	FOG_RGB = theme["fog"];
	RAMP = [];
	RAMP_CACHE = {};
	SPRITE_RAMPS = {};

	array<int> wmat = [ramp(theme["mortar"]), ramp(wall), ramp(scale_rgb(wall, 0.62)), ramp(tint_rgb(wall, 0xFFFFFF, 0.3)),
		ramp(accent), ramp(scale_rgb(accent, 0.6)), glow(glow_rgb), ramp(0x0A080C), ramp(0xE6DCC0), ramp(0x5A5A68),
		glow(0xFF8C1E), glow(0xFFD050), glow(0xFFFCE8)];
	int seed = ti * 101 + 7;
	var base = fresh_codes();
	if (pattern == 0) { tex_brick(base, seed); tex_moss(base, seed); }
	else if (pattern == 1) { tex_brick(base, seed); tex_blood(base, seed); }
	else if (pattern == 2) { tex_block(base, seed); tex_mushrooms(base, seed); }
	else if (pattern == 3) { tex_panel(base, seed); }
	else if (pattern == 4) { tex_rock(base, seed); }
	else { tex_marble(base, seed); }
	WTEX = [];
	bake(WTEX, base, wmat);
	for (int frame = 0; frame < 2; ++frame) {
		var torch = base;
		tex_torch(torch, frame);
		bake(WTEX, torch, wmat);
	}
	var niche = base;
	tex_skull(niche);
	bake(WTEX, niche, wmat);

	int floor_rgb = theme["floor"];
	array<int> fmat = [ramp(scale_rgb(floor_rgb, 0.45)), ramp(floor_rgb), ramp(scale_rgb(floor_rgb, 0.75)),
		ramp(tint_rgb(floor_rgb, 0xFFFFFF, 0.22)), ramp(accent), ramp(tint_rgb(FOG_RGB, 0x3C64B4, 0.6)),
		ramp(0xB4DCFF), ramp(0xDCD2B4), glow(0x5AFFF0), ramp(0x8C1020)];
	var flags = fresh_codes();
	tex_flags(flags, seed + 3);
	FTEX = [];
	bake(FTEX, flags, fmat);
	for (int kind = 1; kind <= 5; ++kind) {
		var decor = flags;
		tex_floor_decor(decor, kind, seed + kind);
		bake(FTEX, decor, fmat);
	}

	int ceil_rgb = theme["ceil"];
	array<int> cmat = [ramp(scale_rgb(ceil_rgb, 0.45)), ramp(ceil_rgb), ramp(scale_rgb(ceil_rgb, 0.7)),
		ramp(tint_rgb(ceil_rgb, 0xFFFFFF, 0.2)), ramp(0x5A3A22), ramp(0x32200F), ramp(tint_rgb(accent, ceil_rgb, 0.5))];
	var slabs = fresh_codes();
	tex_ceiling(slabs, seed + 8);
	CTEX = [];
	bake(CTEX, slabs, cmat);

	WALL_KIND = [];
	FLOOR_KIND = [];
	LM_X = [];
	LM_Y = [];
	LM_ART = [];
	for (int ty = 0; ty < MAP_H; ++ty) {
		for (int tx = 0; tx < MAP_W; ++tx) {
			int t = G.tiles[idx(tx, ty)];
			int h = lat(tx, ty, G.floor);
			int wk = h % 13 == 0 ? 1 : (h % 13 == 6 ? 2 : 0);
			WALL_KIND.push(wk);
			int fk = h % 19;
			fk = fk == 3 ? 1 : (fk == 7 ? 2 : (fk == 11 ? 3 : (fk == 15 ? 4 : 0)));
			if (t == TILE_STAIRS) { fk = 5; }
			FLOOR_KIND.push(fk * 1024);
			if (t == TILE_STAIRS) { LM_X.push(tx); LM_Y.push(ty); LM_ART.push(ART_ID["stairs"]); }
			else if (t == TILE_STATUE) { LM_X.push(tx); LM_Y.push(ty); LM_ART.push(ART_ID["statue"]); }
			else if (t == TILE_MURAL) { LM_X.push(tx); LM_Y.push(ty); LM_ART.push(ART_ID["mural"]); }
		}
	}

	apply_decals();

	ROLE_RAMP = [];
	FLASH_TAB = [];
	int flash = glow(0xFFFFFF);
	for (int i = 0; i < ROLE_RGB.size(); ++i) {
		int rgb = ROLE_RGB[i];
		ROLE_RAMP.push(ROLE_GLOW[i] != 0 ? glow(rgb) : ramp(rgb));
		FLASH_TAB.push(flash);
	}
	HURT_RAMP = glow(0xC80A14);
	BAR_GOOD = RAMP[glow(0x3CF05A)];
	BAR_BAD = RAMP[glow(0x781414)];
}

// ------------------------------------------------------------ pixel art ----
// Sprites are strings: one char per texel, '.' transparent, the rest roles.
// b/d/h take the sprite's own color (body, shade, highlight); the others are
// fixed, and the glowing ones stay lit in the dark.
string ROLES = "kbdhewrygcfFsn123456pE";
array<int> ROLE_RGB = [0x120E18, 0, 0, 0, 0xFFEC50, 0xECE6D2, 0xBE1828, 0xFFC428, 0x9698AA, 0x5AFFFA, 0xFF821E,
	0xFFF4AA, 0x807A72, 0x764A28, 0xFF2828, 0xFF9600, 0xFFF028, 0x3CE650, 0x3C78FF, 0xBE46FF, 0xD6A076, 0xFF3C28];
array<int> ROLE_GLOW = [0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1];
array<int> ART_W = [];
array<int> ART_H = [];
array<int> ART_OFF = [];
array<int> ART_TEX = [];
map<string, int> ART_ID = {};

function art(string name, array<string> rows) -> void {
	int w = rows[0].size();
	ART_ID[name] = ART_W.size();
	ART_W.push(w);
	ART_H.push(rows.size());
	ART_OFF.push(ART_TEX.size());
	for (int r = 0; r < rows.size(); ++r) {
		string row = rows[r];
		if (row.size() != w) { throw `art '${name}' row ${r} is ${row.size()} wide, expected ${w}`; }
		for (int c = 0; c < w; ++c) { ART_TEX.push(ROLES.find(row.substr(c, 1))); }
	}
}

function art_mural() -> void {
	array<string> rows = [];
	for (int v = 0; v < 16; ++v) {
		string row = "";
		for (int u = 0; u < 16; ++u) {
			string ch = ".";
			if (v < 12) {
				if (u == 0 || u == 15 || v == 0 || v == 11) { ch = "y"; }
				else if (u == 1 || u == 14 || v == 1 || v == 10) { ch = "n"; }
				else { ch = "123456".substr(((u + v * 2) / 3) % 6, 1); }
				if (v >= 3 && v <= 6 && u >= 6 && u <= 9) { ch = (v == 4 && (u == 7 || u == 8)) ? "k" : "y"; }
			} else if (u == 3 || u == 12 || (v == 12 && u == 8)) { ch = "n"; }
			row += ch;
		}
		rows.push(row);
	}
	art("mural", rows);
}

function build_art() -> void {
	art("g", [
		"................", ".k....kkkk....k.", ".bk..kbbbbk..kb.", ".bbkkbbbbbbkkbb.",
		"..bbbbhhbbbbbb..", "..bbeebbbbeebb..", "..bbekbbbbekbb..", "..bbbbbbbbbbbb..",
		"..bkwkwkwkwkwb..", "..bbrrrrrrrrbb..", "...bbbbbbbbbb...", "..dbbbbbbbbbbd..",
		".dd.bbbbbbbb.dd.", "....dbb..bbd....", "....dd....dd....", "...kkk....kkk..."]);
	art("s", [
		"............gg..", "...........n..n.", "....kkkk..n...n.", "...kddddk.n..n..",
		"..kddddddkn.n...", "..kdeeddeedb....", "..bddddddddb....", "..bdkwkwkwdb....",
		"...dddddddd.....", "..bnnnnnnnnb....", "..bddddddddb....", "...dddddddd.....",
		"...dd....dd.....", "...dd....dd.....", "..kkk....kkk....", "................"]);
	art("o", [
		"................", "................", "................", "................",
		"......kkkk......", "....kkbbbbkk....", "...kbhhbbbbbk...", "..kbhhbbwwbbbk..",
		"..kbbbbwkewbbk..", ".kbbbbbbwwbbbbk.", ".kbbhbbbbbbbbbk.", "kbbbbbbbbbhbbbbk",
		"kbbbbbbbbbbbbbbk", "kdbbbbdbbbbdbbdk", ".kddddddddddddk.", "..kkkkkkkkkkkk.."]);
	art("b", [
		"................", "................", "k..............k", "dk....k..k....kd",
		"ddk...kbbk...kdd", "dddk.kbbbbk.kddd", "ddddkbebbebkdddd", "dddddbbbbbbddddd",
		"dkdddbwbbwbdddkd", "k.kddbbrrbbddk.k", "...kd.bbbb.dk...", "....k..bb..k....",
		"................", "................", "................", "................"]);
	art("k", [
		"................", "................", "................", "....kk..........",
		"...kbbk.kk......", "...kbhbkbbk.....", "....kbbbbbbk....", "...kbbebbbbbk...",
		"..kbbbbbbbbbbk..", ".kkbbwbbbbhbbbk.", ".krbbbbbbbbhbbbk", "..kkbbddbbbbbbbk",
		"....kbbddbbbbbk.", "....kdd.kddddk.d", "...kkk..kkkk..dd", "..............d."]);
	art("r", [
		".....kkkkkk.....", "....kbbbbbbk....", "....bkkbbkkb....", "....bkebbkeb....",
		"....bbbkkbbb....", ".....bwbwbwb....", "......kbbk......", "...bbbbbbbbbb...",
		"...b.bdbdbd.b...", "...b.bdbdbd.b...", "...b.bdbdbd.bg..", "...b..bbbb..bg..",
		"......b..b...g..", "......b..b......", "......b..b......", ".....bb..bb....."]);
	art("chest_open", [
		"................", "................", "................", "..kkkkkkkkkkkk..",
		".knnnnnnnnnnnnk.", ".knyyyyyyyyyynk.", ".kwkwkwkwkwkwkk.", ".krreerrrreerrk.",
		".krrrrrrrrrrrrk.", ".krrrrrrrrrrrrk.", ".kwkwkwkwkwkwkk.", ".knnnnnnnnnnnnk.",
		".knnnnyyyynnnnk.", ".knnnnykkynnnnk.", ".knnnnnnnnnnnnk.", "..kkkkkkkkkkkk.."]);
	art("chest", [
		"................", "................", "................", "................",
		"..kkkkkkkkkkkk..", ".knnhhhhhhhhnnk.", ".knnnnnnnnnnnnk.", ".kyyyyyyyyyyyyk.",
		".knnnnnyynnnnnk.", ".knnnnykkynnnnk.", ".knnnnnyynnnnnk.", ".knnnnnnnnnnnnk.",
		".kyyyyyyyyyyyyk.", ".knnnnnnnnnnnnk.", ".knnnnnnnnnnnnk.", "..kkkkkkkkkkkk.."]);
	art("O", [
		".kk..........kk.", ".kwk..kkkk..kwk.", "..kwkkbbbbkkwk..", "...kbbbbbbbbk...",
		"..kbbEEbbEEbbk..", "..kbbbbbbbbbbk..", "..kbwkkkkkkwbk..", "...kbrrrrrrbk...",
		".hhbbbbbbbbbbhh.", "hbbbbbhbbhbbbbbh", "bbd.bbbbbbbb.dbb", "bd..bbbbbbbb..db",
		"kk..nnnnnnnn..kk", "....dddddddd....", "...ddd....ddd...", "..kkkk....kkkk.."]);
	art("m", [
		"................", "....k......k....", ".....k....k.....", "hhh...kbbk...hhh",
		"hbbh..bbbb..hbbh", "bb1bb.beeb.bb1bb", "bb11bbbbbbbb11bb", "bbbbbbbbbbbbbbbb",
		".bdddbbbbbbdddb.", "..bbd.bbbb.dbb..", "..bb1b.bb.b1bb..", "...bbb.bb.bbb...",
		"....b..bb..b....", "......kbbk......", "................", "................"]);
	art("W", [
		"..kk........kk..", ".kbbk......kbbk.", ".kbbbkkkkkkbbbk.", "kbbEbbbbbbbbEbbk",
		"kbbbbbbbbbbbbbbk", "kbwkwkwkwkwkwkbk", "kbrrrrrrrrrrrrbk", "kbrrrrrrrrrrrrbk",
		"kbrrrrkkkkrrrrbk", "kbrrrrrrrrrrrrbk", "kbwkwkwkwkwkwkbk", "kbbbbbbbbbbbbbbk",
		".kbbhbbbbbbhbbk.", "..dddd....dddd..", "..dd........dd..", ".kkk........kkk."]);
	art("w", [
		".......f........", "......ff....f...", "...f..fFf..ff...", "...ff.fFff.f....",
		"....ffFFFffff...", "...fffFFFFff....", "..ffFFFFFFFff...", "..fFFeFFFeFFf...",
		"..fFFFFFFFFFf...", "..ffFFFrrFFff...", "...ffFFFFFFf....", "....fffFFff.....",
		".....ffffff.....", "......ffff......", ".......ff.......", "................"]);
	art("d", [
		"................", ".k...........kk.", "kdk.........kbwk", "kddk.......kbbbk",
		"kdddk.....kbbEbk", "kddddk...kbbbbbk", ".kddddkkkbbbrrwk", "..kdddbbbbbbkkk.",
		"...kbbhhhbbbk...", "...bbhhhhhbbb...", "..bbbbhhhbbbbd..", ".dbb.bbbbb.bbdd.",
		".dd..bb.bb..dddk", "....bb...bb...kk", "...kkk...kkk....", "................"]);
	art("V", [
		"k..................k", "bk....kk....kk....kb", "bbk..kbbk..kbbk..kbb", "bbbk.kbbbkkbbbk.kbbb",
		"dbbbkbbbbbbbbbbkbbbd", "ddbbbbbbbbbbbbbbbbdd", ".ddbbbbccbbbbccbbdd.", "..dbbbbccbbbbccbbd..",
		"...bbbbbbbbbbbbbb...", "...bbwkwkwkwkwkbb...", "...bbkccccccccckb...", "...bbwkwkwkwkwkbb...",
		"..hbbbbbbbbbbbbbbh..", ".hbbbbbhhhhhhbbbbbh.", "hbbbb.bhhhhhhb.bbbbh", "bbbd..bbhhhhbb..dbbb",
		"bdd...bbbbbbbb...ddb", "kk...ddbb..bbdd...kk", ".....ddd....ddd.....", "....kkkk....kkkk...."]);
	art(")", [
		"............", "..........g.", ".........gh.", "........gh..",
		".......gh...", "......gh....", "..y..gh.....", "...ygh......",
		"....y.......", "...n.y......", "..n.........", ".k.........."]);
	art("[", [
		"............", "..kk....kk..", ".kbbkkkkbbk.", ".kbhbbbbhbk.",
		".kbhbbbbbbk.", "..kbbbbbbk..", "..kbbhbbbk..", "..kbbbbbbk..",
		"..kyyyyyyk..", "..kbbbbbbk..", "...kbbbbk...", "....kkkk...."]);
	art("=", [
		"............", "............", "............", "....yyyy....",
		"...y....y...", "..y......y..", "..y......y..", "..y......y..",
		"...y....y...", "....ybby....", ".....hb.....", "......b....."]);
	art("!", [
		"............", "....nnnn....", ".....gg.....", ".....gg.....",
		"....gbbg....", "...gbhbbg...", "..gbhbbbbg..", "..gbbbbbbg..",
		"..gbbbbbbg..", "..gdbbbbdg..", "...gddddg...", "....gggg...."]);
	art("~", [
		"............", "............", "............", "............",
		"............", ".kwwwwwwwwk.", "kwhwwwwwwhwk", "kwwwwbbwwwwk",
		"kwwwwbbwwwwk", "kwhwwwwwwhwk", ".kwwwwwwwwk.", "............"]);
	art("$", [
		"............", "............", "............", "............",
		"............", ".....hh.....", "....hbbh....", "...hbbhbbh..",
		"..hbbbbhbbh.", ".hbbhbbbbbbh", "hbbbbbhbbbbb", "dddddddddddd"]);
	art("stairs", [
		"....ssssssss....", "..sshhhhhhhhss..", ".sskkkkkkkkkkss.", ".skkkkkkkkkkkks.",
		"sskkcccccccckkss", "skkkkkkkkkkkkkks", "skkkkccccccckkks", "skkkkkkkkkkkkkks",
		"skkkkkkcccckkkks", "skkkkkkkkkkkkkks", "sskkkkkkkcckkkss", "ssssssssssssssss"]);
	art("statue", [
		"....ss........", "...ssss.......", "...syssssss...", "...sssswwws...",
		"....ssssss....", "..ssssssssss..", ".ss.ssssss.ss.", ".s..ssssss..s.",
		"....ssssss....", "....ss..ss....", "...sss..sss...", "..yyyyyyyyyy..",
		"..ssssssssss..", "..ssssssssss..", ".ssssssssssss.", ".kkkkkkkkkkkk."]);
	art_mural();
	art("torch_a", [
		"....F.......", "...fFf......", "..ffFFf.....", "..fFFFf.....",
		"...fFf......", "...nnn......", "....nn......", "....nn......",
		"...pnnp.....", "..ppnnpp....", "..pppppp....", "..pppppp....",
		".dpppppp....", ".ddppppp....", ".ddddddd....", ".ddddddd...."]);
	art("torch_b", [
		"...F........", "...fFf......", "..fFFff.....", "..fFFFf.....",
		"...fFf......", "...nnn......", "....nn......", "....nn......",
		"...pnnp.....", "..ppnnpp....", "..pppppp....", "..pppppp....",
		".dpppppp....", ".ddppppp....", ".ddddddd....", ".ddddddd...."]);
	art("blade", [
		"...........g..", "..........gh..", ".........gh...", "........gh....",
		".......gh.....", "......gh......", ".....gh.......", "..y.gh........",
		"...yh.........", "...py.........", "..ppp.........", ".pppp.........",
		"ppppd.........", "ppddd.........", "dddd..........", "ddd..........."]);
}

// --------------------------------------------------------------- camera ----
float CAM_DX = 0.0;
float CAM_DY = 0.0;
float CAM_PX = 0.0;
float CAM_PY = 0.0;
float CAM_POSX = 0.0;
float CAM_POSY = 0.0;
float CAM_INVDET = 0.0;

// ------------------------------------------------------------ the caster ---
// Renders the viewport for an arbitrary heading (the turn animation sweeps
// through non-cardinal headings; gameplay only ever calls it with cardinals).
// Returns VIEW_H fully-colored row strings.
function render_view(float dirx, float diry) -> var {
	if (VC_FLOOR != G.floor || PW != VIEW_W || PH != VIEW_H * 2) { build_view_cache(); }
	float planex = 0.0 - diry * PLANE_LEN;
	float planey = dirx * PLANE_LEN;
	int ppx = G.player.x;
	int ppy = G.player.y;
	float posx = ppx + 0.5;
	float posy = ppy + 0.5;
	CAM_DX = dirx;
	CAM_DY = diry;
	CAM_PX = planex;
	CAM_PY = planey;
	CAM_POSX = posx;
	CAM_POSY = posy;
	int pw = PW;
	int ph = PH;
	float kpx = WALL_KPX;
	int turn = G.turn;
	int mapw = MAP_W;
	int maph = MAP_H;
	int wall_tile = TILE_WALL;
	float flicker = 1.0 + 0.035 * ((turn * 7) % 3 - 1);
	var& pix = VPIX;
	var& ramps = RAMP;
	var& wtex = WTEX;
	var& ftex = FTEX;
	var& ctex = CTEX;
	var& bayer = BAYER;
	var& rowd = ROW_DIST;
	var& rowl = ROW_LIGHT;
	var& zbuf = ZBUF;
	var& wkind = WALL_KIND;
	var& fkind = FLOOR_KIND;
	var& tiles = G.tiles;

	for (int x = 0; x < pw; ++x) {
		float camx = 2.0 * x / (pw - 1.0) - 1.0;
		float rdx = dirx + planex * camx;
		float rdy = diry + planey * camx;
		int mx = ppx;
		int my = ppy;
		float adx = rdx < 0.0 ? 0.0 - rdx : rdx;
		float ady = rdy < 0.0 ? 0.0 - rdy : rdy;
		float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		int stepx = rdx < 0.0 ? -1 : 1;
		int stepy = rdy < 0.0 ? -1 : 1;
		float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		int side = 0;
		bool hit = false;
		int guard = 0;
		while (!hit && guard < 96) {
			if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
			else { sdy += ddy; my += stepy; side = 1; }
			if (mx < 0 || my < 0 || mx >= mapw || my >= maph) { hit = true; }
			else if (tiles[my * mapw + mx] == wall_tile) { hit = true; }
			guard = guard + 1;
		}
		float pdist = side == 0 ? sdx - ddx : sdy - ddy;
		pdist = pdist < 0.05 ? 0.05 : pdist;
		zbuf[x] = pdist;

		float wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
		wallx = wallx - floor(wallx);
		int u = wallx * 32.0;
		if (u > 31) { u = 31; }
		if ((side == 0 && rdx > 0.0) || (side == 1 && rdy < 0.0)) { u = 31 - u; }
		int kind = 0;
		if (mx >= 0 && my >= 0 && mx < mapw && my < maph) { kind = wkind[my * mapw + mx]; }
		int tb = kind == 1 ? 1024 * (1 + (turn + mx + my) % 2) : (kind == 2 ? 3072 : 0);
		tb += u;

		int lineh = kpx / pdist;
		if (lineh < 1) { lineh = 1; }
		int ytop = ph / 2 - lineh / 2;
		int y0 = ytop < 0 ? 0 : ytop;
		int y1 = ytop + lineh > ph ? ph : ytop + lineh;
		float lf = lightf(pdist) * 15.0 * flicker * (side == 1 ? 0.72 : 1.0);
		if (lf > 15.0) { lf = 15.0; }
		int vstep = 2097152 / lineh;
		int v = (y0 - ytop) * vstep;
		int bx = x & 3;
		for (int y = y0; y < y1; ++y) {
			int l = lf + bayer[((y & 3) << 2) + bx];
			pix[y * pw + x] = ramps[wtex[tb + ((v >> 16) & 31) * 32] + l];
			v += vstep;
		}

		int ystart = y1 < ph - y0 ? y1 : ph - y0;
		for (int y = ystart; y < ph; ++y) {
			float d = rowd[y];
			float fx = posx + d * rdx;
			float fy = posy + d * rdy;
			int ix = fx;
			int iy = fy;
			int tu = (fx - ix) * 32.0;
			int tv = (fy - iy) * 32.0;
			int texel = (tv & 31) * 32 + (tu & 31);
			if (ix < 0) { ix = 0; } else if (ix >= mapw) { ix = mapw - 1; }
			if (iy < 0) { iy = 0; } else if (iy >= maph) { iy = maph - 1; }
			float fl = rowl[y] * flicker;
			if (fl > 15.0) { fl = 15.0; }
			if (y >= y1) {
				int l = fl + bayer[((y & 3) << 2) + bx];
				pix[y * pw + x] = ramps[ftex[fkind[iy * mapw + ix] + texel] + l];
			}
			int cy = ph - 1 - y;
			if (cy < y0) {
				int l = fl * 0.8 + bayer[((cy & 3) << 2) + bx];
				pix[cy * pw + x] = ramps[ctex[texel] + l];
			}
		}
	}

	draw_sprites();
	draw_hands();
	if (G.last_dmg > 0) { draw_hurt(); }

	var lines = [];
	if (VIEW_WORKERS != 0) {
		lines = parallel_transform(VROWS, view_row_half);
	} else {
		for (int cy = 0; cy < VIEW_H; ++cy) { lines.push(view_row_half(cy)); }
	}
	return lines;
}

// One glyph row: fg = top pixel, bg = bottom pixel, escapes only on change.
// Also the parallel_transform body, so it reads the globals directly.
function view_row_half(int cy) -> string {
	string row = "";
	int pw = PW;
	int fg = -1;
	int bg = -1;
	int ti = cy * 2 * pw;
	int bi = ti + pw;
	for (int x = 0; x < pw; ++x) {
		int top = VPIX[ti + x];
		int bot = VPIX[bi + x];
		if (top != fg) { row += VPAL_FG[top]; fg = top; }
		if (bot != bg) { row += VPAL_BG[bot]; bg = bot; }
		row += HALF_BLOCK;
	}
	return row + VRESET;
}

// --------------------------------------------------------------- sprites ---
array<float> SP_D = [];
array<float> SP_X = [];
array<int> SP_ART = [];
array<int> SP_RGB = [];
array<float> SP_HW = [];
array<float> SP_LIFT = [];
array<int> SP_CAP = [];
array<int> SP_HP = [];      // per mille of max hp for a wounded monster's bar, -1 none
array<int> SP_FLASH = [];
map<int, int> SPRITE_RAMPS = {};

function sprite_rgb(string ansi_code) -> int {
	if (ANSI_RGB.has(ansi_code)) { return ANSI_RGB[ansi_code]; }
	return 0xC8C8C8;
}

function monster_sizef(var& m) -> float {
	if (m.behavior == "boss") { return 1.4; }
	return 0.5 + m.tier * 0.08;
}

function body_ramps(int rgb) -> int {
	if (SPRITE_RAMPS.has(rgb)) { return SPRITE_RAMPS[rgb]; }
	int b = ramp(rgb);
	int d = ramp(scale_rgb(rgb, 0.55));
	int h = ramp(tint_rgb(rgb, 0xFFFFFF, 0.45));
	int packed = b | (d << 20) | (h << 40);
	SPRITE_RAMPS[rgb] = packed;
	return packed;
}

function sprite_add(int wx, int wy, int art_id, int rgb, float hw, float lift, int cap, int hp, int flash) -> void {
	float rx = wx + 0.5 - CAM_POSX;
	float ry = wy + 0.5 - CAM_POSY;
	float depth = CAM_INVDET * (0.0 - CAM_PY * rx + CAM_PX * ry);
	if (depth < 0.12 || depth > SPRITE_RANGE) { return; }
	float ttx = CAM_INVDET * (CAM_DY * rx - CAM_DX * ry);
	float sx = (PW / 2.0) * (1.0 + ttx / depth);
	if (sx < -PW || sx > PW * 2) { return; }
	SP_D.push(depth);
	SP_X.push(sx);
	SP_ART.push(art_id);
	SP_RGB.push(rgb);
	SP_HW.push(hw);
	SP_LIFT.push(lift);
	SP_CAP.push(cap);
	SP_HP.push(hp);
	SP_FLASH.push(flash);
}

function monster_art(var& m) -> int {
	if (m.glyph == "?") { return ART_ID["chest_open"]; }
	if (ART_ID.has(m.glyph)) { return ART_ID[m.glyph]; }
	return ART_ID["g"];
}

function draw_sprites() -> void {
	float det = CAM_PX * CAM_DY - CAM_DX * CAM_PY;
	if (det < 0.0001 && det > -0.0001) { return; }
	CAM_INVDET = 1.0 / det;
	SP_D = [];
	SP_X = [];
	SP_ART = [];
	SP_RGB = [];
	SP_HW = [];
	SP_LIFT = [];
	SP_CAP = [];
	SP_HP = [];
	SP_FLASH = [];
	int prop_cap = PH * 2 / 5;
	int stairs = ART_ID["stairs"];
	int statue = ART_ID["statue"];
	for (int i = 0; i < LM_X.size(); ++i) {
		int a = LM_ART[i];
		float hw = a == stairs ? 0.42 : (a == statue ? 0.98 : 0.9);
		int rgb = a == stairs ? 0x78FFFF : (a == statue ? 0xFFDC60 : 0xFF82E1);
		sprite_add(LM_X[i], LM_Y[i], a, rgb, hw, 0.0, a == stairs ? prop_cap : PH, -1, 0);
	}
	for (int i = 0; i < G.ground.size(); ++i) {
		var& it = G.ground[i];
		int a = ART_ID.has(it.glyph) ? ART_ID[it.glyph] : ART_ID["~"];
		sprite_add(it.x, it.y, a, sprite_rgb(it.color), 0.3, 0.0, prop_cap, -1, 0);
	}
	for (int i = 0; i < G.monsters.size(); ++i) {
		var& m = G.monsters[i];
		if (!m.alive) { continue; }
		if (m.disguised) {
			// a mimic is loot until it very much is not
			sprite_add(m.x, m.y, ART_ID["chest"], 0xC8A03C, 0.36, 0.0, prop_cap, -1, 0);
			continue;
		}
		float lift = 0.0;
		if (m.glyph == "b" || m.glyph == "m" || m.glyph == "w") { lift = 0.32 + 0.05 * ((G.turn + m.id) % 2); }
		int hp = m.hp < m.max_hp ? (m.hp * 1000) / imax(1, m.max_hp) : -1;
		int flash = VIEW_HITS.has(m.id) && VIEW_HITS[m.id] + 1 >= G.turn ? 1 : 0;
		sprite_add(m.x, m.y, monster_art(m), sprite_rgb(m.color), 1.15 * monster_sizef(m), lift, PH + 12, hp, flash);
	}

	// painter's order: far to near (n is tiny; an insertion sort of indices is honest work)
	int n = SP_D.size();
	array<int> order = [];
	for (int i = 0; i < n; ++i) {
		int j = order.size();
		order.push(i);
		while (j > 0 && SP_D[order[j - 1]] < SP_D[i]) {
			order[j] = order[j - 1];
			j = j - 1;
		}
		order[j] = i;
	}
	for (int k = 0; k < n; ++k) {
		int i = order[k];
		float depth = SP_D[i];
		int a = SP_ART[i];
		int hgt = WALL_KPX * SP_HW[i] / depth;
		if (hgt > SP_CAP[i]) { hgt = SP_CAP[i]; }
		if (hgt < 1) { hgt = 1; }
		int wdt = hgt * ART_W[a] * SPRITE_ASPECT / ART_H[a];
		if (wdt < 1) { wdt = 1; }
		int bot = PH * 0.5 + WALL_KPX * (0.5 - SP_LIFT[i]) / depth;
		int x0 = floor(SP_X[i] - wdt * 0.5);
		int lvl = lightf(depth) * 15.0 + 0.5;
		draw_art(a, SP_RGB[i], x0, bot - hgt, wdt, hgt, lvl, depth, SP_FLASH[i]);
		if (SP_HP[i] >= 0) { draw_hp_bar(x0, bot - hgt - 2, wdt, SP_HP[i], depth); }
	}
}

function draw_hp_bar(int x0, int y, int w, int per_mille, float depth) -> void {
	if (y < 0 || y >= PH) { return; }
	int bw = imax(3, imin(w, PW / 4));
	int bx = x0 + (w - bw) / 2;
	int full = (bw * per_mille + 999) / 1000;
	for (int x = imax(0, bx); x < imin(PW, bx + bw); ++x) {
		if (depth >= ZBUF[x]) { continue; }
		VPIX[y * PW + x] = x - bx < full ? BAR_GOOD : BAR_BAD;
	}
}

// Scales art a into the w x h rect at (x0, y0); depth > 0 tests the z-buffer,
// flash paints every opaque texel white (a monster that just took a hit).
function draw_art(int a, int rgb, int x0, int y0, int w, int h, int lvl, float depth, int flash) -> void {
	array<int> tab = FLASH_TAB;
	if (flash == 0) {
		int packed = body_ramps(rgb);
		tab = ROLE_RAMP;
		tab[1] = packed & 0xFFFFF;
		tab[2] = (packed >> 20) & 0xFFFFF;
		tab[3] = (packed >> 40) & 0xFFFFF;
	}
	if (lvl > 15) { lvl = 15; }
	if (lvl < 0) { lvl = 0; }
	int aw = ART_W[a];
	int ah = ART_H[a];
	int off = ART_OFF[a];
	int sx0 = x0 < 0 ? 0 : x0;
	int sx1 = x0 + w > PW ? PW : x0 + w;
	int sy0 = y0 < 0 ? 0 : y0;
	int sy1 = y0 + h > PH ? PH : y0 + h;
	int pw = PW;
	var& pix = VPIX;
	var& ramps = RAMP;
	var& texels = ART_TEX;
	for (int sx = sx0; sx < sx1; ++sx) {
		if (depth > 0.0 && depth >= ZBUF[sx]) { continue; }
		int col = off + ((sx - x0) * aw) / w;
		for (int sy = sy0; sy < sy1; ++sy) {
			int code = texels[col + (((sy - y0) * ah) / h) * aw];
			if (code >= 0) { pix[sy * pw + sx] = ramps[tab[code] + lvl]; }
		}
	}
}

// the torch in your off hand (its flame flickers turn to turn) and your blade
function draw_hands() -> void {
	int torch = ART_ID[(G.turn % 2 == 0) ? "torch_a" : "torch_b"];
	int th = PH * 2 / 5;
	int tw = th * ART_W[torch] / ART_H[torch];
	draw_art(torch, 0x5A3C8C, PW / 16, PH - th, tw, th, 14, 0.0, 0);
	if (G.player.weapon != null) {
		int blade = ART_ID["blade"];
		int bh = PH * 9 / 20;
		int bw = bh * ART_W[blade] / ART_H[blade];
		draw_art(blade, 0x5A3C8C, PW - bw - PW / 12, PH - bh, bw, bh, 13, 0.0, 0);
	}
}

// fresh damage: a ragged red rim around the view
function draw_hurt() -> void {
	int c = RAMP[HURT_RAMP];
	for (int y = 0; y < PH; ++y) {
		for (int x = 0; x < PW; ++x) {
			int e = imin(imin(x, PW - 1 - x), imin(y, PH - 1 - y));
			if (e == 0 || (e == 1 && (x + y) % 2 == 0) || (e == 2 && (x * 3 + y) % 5 == 0)) { VPIX[y * PW + x] = c; }
		}
	}
}
