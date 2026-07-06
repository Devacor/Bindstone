// The first-person view: a DDA raycaster locked to the four cardinal facings.
// One ray per screen column across a ~84 degree FOV, perpendicular-distance
// wall slices (no fisheye), N/S vs E/W face darkening, torch-style distance
// shading, per-depth wall materials (garish on purpose), floor/ceiling
// gradients, and billboard sprites (monsters, loot, stairs, landmarks) with
// per-column z-buffer occlusion. Pure string building; the host just blits.

var VIEW_W = 64;
var VIEW_H = 20;
var PLANE_LEN = 0.9;        // half-FOV tangent: 0.9 ~ 84 degrees
var WALL_SCALE = 26.0;      // wall slice height at distance 1 (in rows)
var SPRITE_RANGE = 14.0;    // torchlight: sprites past this are swallowed by dark

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

// ------------------------------------------------------------- color -------
// 256-color cube index from 0..255 rgb.
function rgb_idx(int r, int g, int b) -> int {
	var ir = imin(5, imax(0, (r * 6) / 256));
	var ig = imin(5, imax(0, (g * 6) / 256));
	var ib = imin(5, imax(0, (b * 6) / 256));
	return 16 + 36 * ir + 6 * ig + ib;
}

// one fully-colored cell: fg + bg + a single character (reset once per row)
function cell(int fg, int bg, string ch) -> string {
	return ESC + "[38;5;" + to_string(fg) + ";48;5;" + to_string(bg) + "m" + ch;
}

// ANSI SGR color codes (the bestiary/item identity colors) -> rgb
var ANSI_RGB = {
	"30": [40, 40, 40],    "31": [205, 60, 50],   "32": [40, 175, 90],
	"33": [200, 160, 60],  "34": [60, 110, 200],  "35": [175, 80, 190],
	"36": [50, 170, 190],  "37": [200, 200, 200], "90": [130, 130, 135],
	"91": [255, 90, 80],   "92": [110, 240, 130], "93": [255, 230, 90],
	"94": [120, 140, 255], "95": [255, 130, 230], "96": [130, 240, 250],
	"97": [255, 255, 255]
};

// -------------------------------------------------------- depth themes -----
// Moraff-grade maximalism: every couple of floors the dungeon changes wardrobe.
// wall = base rgb, accent = texture fg tint, chars = 3 texture glyphs
// (checker A, checker B, mortar row).
var DEPTH_THEMES = [
	{"name": "Mossrot Warrens",       "wall": [96, 150, 62],   "accent": [190, 230, 120],
	 "ceil": [46, 58, 110],  "floor": [104, 82, 46],  "chars": ["#", "%", "="]},
	{"name": "Bloodbrick Vaults",     "wall": [196, 64, 52],   "accent": [255, 170, 120],
	 "ceil": [58, 26, 66],   "floor": [120, 98, 86],  "chars": ["#", "=", "-"]},
	{"name": "Gilded Fungus Halls",   "wall": [172, 62, 196],  "accent": [255, 214, 90],
	 "ceil": [20, 70, 80],   "floor": [150, 122, 36], "chars": ["%", "#", "o"]},
	{"name": "Screaming Copper Vein", "wall": [36, 186, 176],  "accent": [255, 150, 60],
	 "ceil": [70, 16, 78],   "floor": [176, 96, 30],  "chars": ["#", "$", "="]},
	{"name": "The Ashen Throat",      "wall": [198, 188, 152], "accent": [120, 104, 84],
	 "ceil": [54, 52, 58],   "floor": [124, 112, 96], "chars": ["#", "~", "-"]},
	{"name": "The Hollow Gallery",    "wall": [142, 142, 148], "accent": [92, 90, 96],
	 "ceil": [38, 38, 42],   "floor": [74, 68, 62],   "chars": ["#", "|", "="]}
];

function theme_index(int floor) -> int {
	if (floor >= 10) { return 5; }
	if (floor >= 9) { return 4; }
	return (floor - 1) / 2;
}

function theme_name(int floor) -> string {
	return DEPTH_THEMES[theme_index(floor)]["name"];
}

// ------------------------------------------------------------ view cache ---
// Rebuilt whenever the floor changes (or after a hot reload / load wipes it):
// pre-baked cell strings for walls (2 faces x 8 torch shades x 3 texture
// chars), per-row ceiling/floor gradient cells, and landmark sprite tiles.
var VC_FLOOR = -1;
var WALL_CELLS = [];
var CEIL_CELLS = [];
var FLOOR_CELLS = [];
var LANDMARKS = [];   // [x, y, glyph, r, g, b, sizef, wide]

function view_cache_invalidate() -> void { VC_FLOOR = -1; }

function build_view_cache() -> void {
	VC_FLOOR = G.floor;
	var theme = DEPTH_THEMES[theme_index(G.floor)];
	var wall = theme["wall"];
	var accent = theme["accent"];
	var chars = theme["chars"];

	WALL_CELLS = [];
	for (int side = 0; side < 2; ++side) {
		var face = side == 1 ? 0.68 : 1.0;
		for (int s = 0; s < 8; ++s) {
			var lum = (0.14 + 0.86 * s / 7.0) * face;
			int br = wall[0] * lum;
			int bg = wall[1] * lum;
			int bb = wall[2] * lum;
			int fr = imin(255, accent[0] * lum);
			int fg = imin(255, accent[1] * lum);
			int fb = imin(255, accent[2] * lum);
			var bgi = rgb_idx(br, bg, bb);
			var fgi = rgb_idx(fr, fg, fb);
			for (int c = 0; c < 3; ++c) {
				WALL_CELLS.push(cell(fgi, bgi, chars[c]));
			}
		}
	}

	CEIL_CELLS = [];
	FLOOR_CELLS = [];
	var mid = VIEW_H / 2;
	var crgb = theme["ceil"];
	var frgb = theme["floor"];
	for (int y = 0; y < VIEW_H; ++y) {
		// ceiling: bright overhead, fading into the horizon dark
		var ct = y < mid ? 1.0 * y / (mid - 1) : 1.0;
		var cf = 1.0 - 0.72 * ct;
		int cr = crgb[0] * cf;
		int cg = crgb[1] * cf;
		int cb = crgb[2] * cf;
		var ci = rgb_idx(cr, cg, cb);
		CEIL_CELLS.push(cell(ci, ci, " "));
		// floor: dark at the horizon, torch-bright at your feet, speckled
		var ft = y >= mid ? 1.0 * (y - mid) / (VIEW_H - 1 - mid) : 0.0;
		var ff = 0.24 + 0.76 * ft;
		int rr = frgb[0] * ff;
		int rg = frgb[1] * ff;
		int rb = frgb[2] * ff;
		int sr = imin(255, rr + 34);
		int sg = imin(255, rg + 34);
		int sb = imin(255, rb + 30);
		FLOOR_CELLS.push(cell(rgb_idx(sr, sg, sb), rgb_idx(rr, rg, rb), y % 2 == 0 ? "." : " "));
	}

	LANDMARKS = [];
	for (int ty = 0; ty < MAP_H; ++ty) {
		for (int tx = 0; tx < MAP_W; ++tx) {
			var t = G.tiles[idx(tx, ty)];
			if (t == TILE_STAIRS) { LANDMARKS.push([tx, ty, ">", 120, 255, 255, 0.34, 3.0]); }
			else if (t == TILE_STATUE) { LANDMARKS.push([tx, ty, "&", 255, 220, 96, 0.85, 0.9]); }
			else if (t == TILE_MURAL) { LANDMARKS.push([tx, ty, "%", 255, 130, 225, 0.9, 1.6]); }
		}
	}
}

// ------------------------------------------------------------ the caster ---
// Renders the viewport for an arbitrary heading (the turn animation sweeps
// through non-cardinal headings; gameplay only ever calls it with cardinals).
// Returns VIEW_H fully-colored row strings.
function render_view(float dirx, float diry) -> var {
	if (VC_FLOOR != G.floor) { build_view_cache(); }
	var planex = -diry * PLANE_LEN;
	var planey = dirx * PLANE_LEN;
	var posx = G.player.x + 0.5;
	var posy = G.player.y + 0.5;

	var cells = [];
	for (int i = 0; i < VIEW_W * VIEW_H; ++i) { cells.push(""); }
	var zbuf = [];

	for (int x = 0; x < VIEW_W; ++x) {
		float camx = 2.0 * x / (VIEW_W - 1.0) - 1.0;
		float rdx = dirx + planex * camx;
		float rdy = diry + planey * camx;
		int mx = G.player.x;
		int my = G.player.y;
		float adx = rdx < 0.0 ? -rdx : rdx;
		float ady = rdy < 0.0 ? -rdy : rdy;
		float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		int stepx = rdx < 0.0 ? -1 : 1;
		int stepy = rdy < 0.0 ? -1 : 1;
		float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		int side = 0;
		var hit = false;
		int guard = 0;
		while (!hit && guard < 96) {
			if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
			else { sdy += ddy; my += stepy; side = 1; }
			if (tile_at(mx, my) == TILE_WALL) { hit = true; }
			guard = guard + 1;
		}
		float pdist = side == 0 ? sdx - ddx : sdy - ddy;
		pdist = pdist < 0.05 ? 0.05 : pdist;
		zbuf.push(pdist);

		float wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
		wallx = wallx - floor(wallx);
		int tx4 = wallx * 4.0;

		int lineh = WALL_SCALE / pdist;
		int y0 = (VIEW_H - lineh) / 2;
		int y1 = y0 + lineh;
		y0 = y0 < 0 ? 0 : y0;
		y1 = y1 > VIEW_H ? VIEW_H : y1;
		int sh = 9.6 / (pdist * 0.85 + 1.2);
		sh = sh > 7 ? 7 : sh;
		int wbase = side * 24 + sh * 3;

		int gi = x;
		int y = 0;
		while (y < y0) { cells[gi] = CEIL_CELLS[y]; gi += VIEW_W; y = y + 1; }
		while (y < y1) {
			int ci = (tx4 + y) % 2;
			if (y % 4 == 3) { ci = 2; }
			cells[gi] = WALL_CELLS[wbase + ci];
			gi += VIEW_W;
			y = y + 1;
		}
		while (y < VIEW_H) { cells[gi] = FLOOR_CELLS[y]; gi += VIEW_W; y = y + 1; }
	}

	draw_sprites(cells, zbuf, dirx, diry, planex, planey, posx, posy);

	var lines = [];
	for (int ry = 0; ry < VIEW_H; ++ry) {
		var row = "";
		int base = ry * VIEW_W;
		for (int rx = 0; rx < VIEW_W; ++rx) {
			row = row + cells[base + rx];
		}
		lines.push(row + ESC + "[0m");
	}
	return lines;
}

// --------------------------------------------------------------- sprites ---
function sprite_rgb(string ansi_code) -> var {
	if (ANSI_RGB.has(ansi_code)) { return ANSI_RGB[ansi_code]; }
	return [200, 200, 200];
}

function monster_sizef(var& m) -> float {
	if (m.behavior == "boss") { return 1.4; }
	return 0.5 + m.tier * 0.08;
}

// world sprite -> camera space; appended to sp as a flat record
// [depth, screen_x, glyph, r, g, b, sizef, wide]
function collect_sprite(var& sp, float wx, float wy, string glyph, int r, int g, int b,
		float sizef, float wide, float invdet, float dirx, float diry,
		float planex, float planey, float posx, float posy) -> void {
	float rx = wx + 0.5 - posx;
	float ry = wy + 0.5 - posy;
	float depth = invdet * (0.0 - planey * rx + planex * ry);
	if (depth < 0.12 || depth > SPRITE_RANGE) { return; }
	float ttx = invdet * (diry * rx - dirx * ry);
	int screen_x = (VIEW_W / 2.0) * (1.0 + ttx / depth);
	if (screen_x < -VIEW_W || screen_x > VIEW_W * 2) { return; }
	sp.push([depth, screen_x, glyph, r, g, b, sizef, wide]);
}

function draw_sprites(var& cells, var& zbuf, float dirx, float diry,
		float planex, float planey, float posx, float posy) -> void {
	float det = planex * diry - dirx * planey;
	if (det < 0.0001 && det > -0.0001) { return; }
	float invdet = 1.0 / det;
	var sp = [];

	for (auto& lm : LANDMARKS) {
		collect_sprite(sp, lm[0], lm[1], lm[2], lm[3], lm[4], lm[5], lm[6], lm[7],
			invdet, dirx, diry, planex, planey, posx, posy);
	}
	for (auto& it : G.ground) {
		var rgb = sprite_rgb(it.color);
		collect_sprite(sp, it.x, it.y, it.glyph, rgb[0], rgb[1], rgb[2], 0.26, 1.6,
			invdet, dirx, diry, planex, planey, posx, posy);
	}
	for (auto& m : G.monsters) {
		if (!m.alive) { continue; }
		if (m.disguised) {
			// a mimic is loot until it very much is not
			collect_sprite(sp, m.x, m.y, "?", 200, 160, 60, 0.26, 1.6,
				invdet, dirx, diry, planex, planey, posx, posy);
			continue;
		}
		var rgb = sprite_rgb(m.color);
		collect_sprite(sp, m.x, m.y, m.glyph, rgb[0], rgb[1], rgb[2],
			monster_sizef(m), 1.25, invdet, dirx, diry, planex, planey, posx, posy);
	}

	// painter's order: far to near (n is tiny; a bubble is honest work)
	for (int i = 0; i < sp.size(); ++i) {
		for (int j = i + 1; j < sp.size(); ++j) {
			if (sp[j][0] > sp[i][0]) {
				var tmp = sp[i];
				sp[i] = sp[j];
				sp[j] = tmp;
			}
		}
	}

	var mid = VIEW_H / 2;
	for (auto& s : sp) {
		float depth = s[0];
		int scx = s[1];
		string glyph = s[2];
		float torch = 1.0 / (depth * 0.3 + 0.72);
		if (torch > 1.2) { torch = 1.2; }
		int r = s[3];
		int g = s[4];
		int b = s[5];
		int fr = imin(255, r * torch);
		int fgc = imin(255, g * torch);
		int fb = imin(255, b * torch);
		var fgi = rgb_idx(fr, fgc, fb);
		var bgi = rgb_idx((fr * 3) / 8, (fgc * 3) / 8, (fb * 3) / 8);
		var body = cell(fgi, bgi, glyph);

		float sizef = s[6];
		int hgt = VIEW_H * sizef * 1.5 / depth;
		if (hgt < 1) { hgt = 1; }
		if (hgt > VIEW_H + 6) { hgt = VIEW_H + 6; }
		// loot/landmarks stay prop-sized point-blank; only creatures loom
		if (sizef < 0.5 && hgt > 8) { hgt = 8; }
		float wide = s[7];
		int wdt = hgt * wide;
		if (wdt < 1) { wdt = 1; }
		int bot = mid + WALL_SCALE * 0.5 / depth;
		if (bot > VIEW_H) { bot = VIEW_H; }
		int top = bot - hgt;
		if (top < 0) { top = 0; }
		int x0 = scx - wdt / 2;
		int x1 = x0 + wdt;
		if (x0 < 0) { x0 = 0; }
		if (x1 > VIEW_W) { x1 = VIEW_W; }
		for (int sx = x0; sx < x1; ++sx) {
			if (depth >= zbuf[sx]) { continue; }
			for (int sy = top; sy < bot; ++sy) {
				cells[sy * VIEW_W + sx] = body;
			}
		}
	}
}
