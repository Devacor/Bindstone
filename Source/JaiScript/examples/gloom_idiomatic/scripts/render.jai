// The Renderer, idiomatic: one class owning the view geometry, z-buffer, wall
// tone tables, texture strips, sprite LUT cache, ray workspaces and the
// automap — where canonical used ~20 file-scope globals + free functions.
//
// What COULD NOT move into the class (parallel admission is the boundary):
//   PIX / VW / ROWIDX / QUADG / RESET (+ PAL_* in util.jai) are read by
//   gloom_row_quad, a parallel_transform body — captured reads only admit
//   globals, member access on a captured instance is rejected. So the pixel
//   grid and the glyph/palette tables stay globals, and the row/ray bodies
//   stay free functions. Everything serial lives on the class.

var PIXW = 2;        // pixels per cell horizontally (set by gloom_boot)
var PIXH = 2;        // pixels per cell vertically
array<int> PIX = [];  // the flat pixel grid (parallel bodies borrow it)
var VW = 0;
var VH = 0;
array<int> ROWIDX = [];   // parallel_transform input: one element per glyph row
var QUADG = [];      // quadrant glyphs by TL|TR<<1|BL<<2|BR<<3
var RESET = "";

// quad mode row builder: 2x2 pixels per cell, two colors by luminosity
// partition. A parallel_transform body over CAPTURED globals (PIX and PAL_LUM
// borrow zero-copy; PAL_FG/PAL_BG/QUADG snapshot per worker; VW/RESET are
// scalar copies). No var& aliases of captured names (admission rejects them) —
// subscript the globals directly. The SAME function runs in a plain serial
// loop when HOST_WORKERS == 0.
function gloom_row_quad(int ry) -> string {
	var parts = [];
	int vw = VW;
	int cells = vw / 2;
	int pfg = -1;
	int pbg = -1;
	int itop = ry * 2 * vw;
	int ibot = itop + vw;
	for (int cx = 0; cx < cells; ++cx) {
		int xx = cx * 2;
		int p0 = PIX[itop + xx];
		int p1 = PIX[itop + xx + 1];
		int p2 = PIX[ibot + xx];
		int p3 = PIX[ibot + xx + 1];
		if (p0 == p1 && p0 == p2 && p0 == p3) {
			if (p0 != pbg) { parts.push(PAL_BG[p0]); pbg = p0; }
			parts.push(" ");
			continue;
		}
		// wall slices split cells into vertical or horizontal halves far
		// more often than not - cheap two-color paths before the full
		// luminosity partition
		if (p0 == p2 && p1 == p3) {
			if (p0 != pfg) { parts.push(PAL_FG[p0]); pfg = p0; }
			if (p1 != pbg) { parts.push(PAL_BG[p1]); pbg = p1; }
			parts.push(QUADG[5]);                 // left half
			continue;
		}
		if (p0 == p1 && p2 == p3) {
			if (p0 != pfg) { parts.push(PAL_FG[p0]); pfg = p0; }
			if (p2 != pbg) { parts.push(PAL_BG[p2]); pbg = p2; }
			parts.push(QUADG[3]);                 // upper half
			continue;
		}
		int l0 = PAL_LUM[p0];
		int l1 = PAL_LUM[p1];
		int l2 = PAL_LUM[p2];
		int l3 = PAL_LUM[p3];
		int lmin = l0;
		int lmax = l0;
		int fgp = p0;
		int bgp = p0;
		if (l1 < lmin) { lmin = l1; bgp = p1; }
		if (l1 > lmax) { lmax = l1; fgp = p1; }
		if (l2 < lmin) { lmin = l2; bgp = p2; }
		if (l2 > lmax) { lmax = l2; fgp = p2; }
		if (l3 < lmin) { lmin = l3; bgp = p3; }
		if (l3 > lmax) { lmax = l3; fgp = p3; }
		int thr = (lmin + lmax + 1) / 2;
		int bits = 0;
		if (l0 >= thr) { bits = 1; }
		if (l1 >= thr) { bits += 2; }
		if (l2 >= thr) { bits += 4; }
		if (l3 >= thr) { bits += 8; }
		if (fgp != pfg) { parts.push(PAL_FG[fgp]); pfg = fgp; }
		if (bits != 15 && bgp != pbg) { parts.push(PAL_BG[bgp]); pbg = bgp; }
		parts.push(QUADG[bits]);
	}
	parts.push(RESET);
	return parts.join("");
}

// ------------------------------------------------------------ the class -----
class Renderer {
public:
	int view_w = 0;
	int view_h = 0;
	bool automap = false;
	array<float> zbuf = [];
	var rowparts = [];

	Renderer(int w, int h) {
		view_w = w * PIXW;
		view_h = (h - 6) * PIXH;
		VW = view_w;
		VH = view_h;
		PIX = [];
		for (int i = 0; i < view_w * view_h; ++i) { PIX.push(0); }
		ROWIDX = [];
		int vrows = PIXH == 3 ? view_h / 3 : view_h / 2;
		for (int r = 0; r < vrows; ++r) { ROWIDX.push(r); }
		zbuf = [];
		for (int i = 0; i < view_w; ++i) { zbuf.push(99.0); }
	}

	// boot-time glyph tables + fixed colors (order matches canonical so the
	// palette intern indices — and therefore the frame bytes — are identical)
	void init() {
		RESET = ESC + "[0m";
		rgb_idx(0, 0, 0);                 // palette index 0 is black (grid clear value)
		halfg = utf8(0x2580);
		QUADG = [" "];
		var quad_cps = [0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
			0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588];
		for (auto cp : quad_cps) { QUADG.push(utf8(cp)); }
		sextg = [];
		for (int b = 0; b < 64; ++b) {
			if (b == 0) { sextg.push(" "); }
			else if (b == 21) { sextg.push(utf8(0x258C)); }        // left half
			else if (b == 42) { sextg.push(utf8(0x2590)); }        // right half
			else if (b == 63) { sextg.push(utf8(0x2588)); }        // full
			else {
				int cp = 0x1FB00 + b - 1;
				if (b > 21) { cp = cp - 1; }
				if (b > 42) { cp = cp - 1; }
				sextg.push(utf8(cp));
			}
		}
		am_cols = [0, rgb_idx(150, 140, 120), rgb_idx(110, 150, 110), rgb_idx(110, 120, 160), rgb_idx(160, 110, 96),
			rgb_idx(200, 170, 110), rgb_idx(90, 230, 110), rgb_idx(150, 140, 120), rgb_idx(230, 70, 60), rgb_idx(90, 120, 240)];
		am_floor = rgb_idx(40, 40, 46);
		am_blink = rgb_idx(240, 255, 160);
		am_item = rgb_idx(255, 220, 80);
		am_enemy = rgb_idx(255, 60, 50);
		am_self = rgb_idx(255, 255, 255);
		am_head = rgb_idx(255, 250, 140);
		hurt_red = rgb_idx(200, 30, 24);
		G.fx.build_colors();
	}

	// per-map tone tables + the prebaked ceiling/floor background
	void build_tables(int mi) {
		var theme = MAPS[mi]["theme"];
		wallt = [];
		// kind 0 (never rendered): flat dark
		var zero = [10.0, 10.0, 12.0];
		push_wall_tones(zero, zero);
		// kinds 1..4 from the theme
		for (int k = 0; k < 4; ++k) {
			var base = theme["walls"][k];
			push_wall_tones(base, base);
		}
		// kind 5 door, 6 exit, 7 red door, 8 blue door (episode constants)
		var door_b = [138.0, 116.0, 94.0];
		var door_a = [188.0, 164.0, 132.0];
		push_wall_tones(door_b, door_a);
		var exit_b = [64.0, 160.0, 84.0];
		var exit_a = [220.0, 235.0, 120.0];
		push_wall_tones(exit_b, exit_a);
		var red_b = [176.0, 50.0, 42.0];
		var red_a = [245.0, 130.0, 96.0];
		push_wall_tones(red_b, red_a);
		var blue_b = [62.0, 94.0, 198.0];
		var blue_a = [136.0, 176.0, 255.0];
		push_wall_tones(blue_b, blue_a);

		// ceiling / floor gradients (dark at the horizon), quantized to CELL rows
		// so flat regions hit the row builder's uniform-cell fast path
		ceilp = [];
		floorp = [];
		var crgb = theme["ceil"];
		var frgb = theme["floor"];
		int half = view_h / 2;
		for (int y = 0; y < view_h; ++y) {
			int yq = (y / PIXH) * PIXH;
			if (y < half) {
				float t = half > 1 ? 1.0 * yq / (half - 1) : 1.0;
				float lum = 1.0 - 0.72 * t;
				ceilp.push(rgb_idx(math::itrunc(crgb[0] * lum), math::itrunc(crgb[1] * lum), math::itrunc(crgb[2] * lum)));
				floorp.push(0);
			} else {
				float t2 = 1.0 * (yq - half) / imax(1, view_h - 1 - half);
				t2 = t2 > 1.0 ? 1.0 : (t2 < 0.0 ? 0.0 : t2);
				float lum2 = 0.26 + 0.74 * t2;
				ceilp.push(0);
				floorp.push(rgb_idx(math::itrunc(frgb[0] * lum2), math::itrunc(frgb[1] * lum2), math::itrunc(frgb[2] * lum2)));
			}
		}
		// prebaked background: one deep-copy assignment per frame replaces the
		// per-pixel ceiling/floor loops outright
		var newbg = [];
		for (int y2 = 0; y2 < view_h; ++y2) {
			// (a ternary of two element reads mis-types into an int local; if/else works)
			int c = 0;
			if (y2 < half) { c = ceilp[y2]; }
			else { c = floorp[y2]; }
			for (int x2 = 0; x2 < view_w; ++x2) { newbg.push(c); }
		}
		bg = newbg;
		// wall tone strips (32-tall texture columns) build lazily per combination
		strips = [];
		strip_set = [];
		int nstrips = 10 * 2 * 16 * 32;
		for (int i2 = 0; i2 < nstrips; ++i2) {
			strips.push(0);
			strip_set.push(0);
		}
		lutc = {};
	}

	// ---------------------------------------------------------- the view ----
	var draw_world() {
		int vw = view_w;
		int vh = view_h;
		float px = G.player.px;
		float py = G.player.py;
		float dirx = math::cos(G.player.pang);
		float diry = math::sin(G.player.pang);
		float planex = 0.0 - diry * 0.78;
		float planey = dirx * 0.78;

		// --- 1. the rays: ONE ray per cell column, parallel_for over
		// PERSISTENT column-chunk workspaces (built on map change, pose header
		// rewritten per frame) - zero per-frame array traffic, in-place outputs.
		int pxw = PIXW;
		int ncols = vw / pxw;
		if (ws_mapgen != G.world.mapgen) {
			build_wall_ws(ncols);
			ws_mapgen = G.world.mapgen;
		}
		for (auto& t : wall_ws) {
			t[3] = px;
			t[4] = py;
			t[5] = dirx;
			t[6] = diry;
			t[7] = planex;
			t[8] = planey;
		}
		if (HOST_WORKERS == 0) {
			for (auto& t : wall_ws) { gloom_ray_chunk_step(t); }
		} else {
			parallel_for (auto& t : wall_ws) { gloom_ray_chunk_step(t); }
		}

		// --- 2. columns: prebaked background + textured wall slices ---
		PIX = bg;                          // ceiling + floor in ONE deep-copy assign
		auto& pix = PIX;
		auto& zb = zbuf;
		int boost = G.player.muzzle > 0 ? 2 : 0;
		int x = 0;
		for (auto& ch : wall_ws) {
			int cn = ch.size();
			int ci = ch[10];
			while (ci < cn) {
				float dist = ch[ci];
				int side = ch[ci + 1];
				int kind = ch[ci + 2];
				int tx = ch[ci + 3] >> 1;             // 0..31
				ci += 4;
				int px0 = x * pxw;
				zb[px0] = dist;
				if (pxw == 2) { zb[px0 + 1] = dist; }
				int lineh = 1.0 * vh / dist;
				lineh = lineh < 1 ? 1 : lineh;
				int y0 = (vh - lineh) / 2;
				int y1 = y0 + lineh;
				int cy0 = y0 < 0 ? 0 : y0;
				int cy1 = y1 > vh ? vh : y1;
				int shade = 15 - math::itrunc(dist * 1.55) + boost;
				shade = shade < 0 ? 0 : (shade > 15 ? 15 : shade);
				int sidx = ((kind * 2 + side) * 16 + shade) * 32 + tx;
				if (strip_set[sidx] == 0) {
					strips[sidx] = build_strip(kind, side, shade, tx);
					strip_set[sidx] = 1;
				}
				auto& strip = strips[sidx];
				int tstep = 65536 / lineh;            // (32 << 11) / lineh
				int tacc = (cy0 - y0) * tstep;
				int gi = cy0 * vw + px0;
				int y = cy0;
				if (pxw == 2) {
					while (y < cy1) {
						int c = strip[tacc >> 11];
						pix[gi] = c;
						pix[gi + 1] = c;
						tacc += tstep;
						gi += vw;
						y = y + 1;
					}
				} else {
					while (y < cy1) {
						pix[gi] = strip[tacc >> 11];
						tacc += tstep;
						gi += vw;
						y = y + 1;
					}
				}
				x = x + 1;
			}
		}

		// --- 3. billboards: enemies, pickups, projectiles (far to near) ---
		float det = planex * diry - dirx * planey;
		if (det < 0.0001 && det > -0.0001) { det = 0.0001; }
		float inv = 1.0 / det;
		var sp = [];
		for (auto& e : G.enemies) {
			collect(sp, e.x, e.y, e.art_name(), ENEMY_DEFS[e.kind].size, 0.0,
				e.flash > 0 ? 1 : 0, inv, dirx, diry, planex, planey);
		}
		for (auto& it : G.items) {
			if (it.taken) { continue; }
			auto& idef = ITEM_DEFS[it.kind];
			collect(sp, it.x, it.y, idef.art, idef.size, 0.0, 0, inv, dirx, diry, planex, planey);
		}
		var shot_art = ["", "pr_spit", "pr_fire", "pr_hex"];
		for (auto& s : G.shots) {
			collect(sp, s.x, s.y, shot_art[s.kind], 0.16, 0.35, 0, inv, dirx, diry, planex, planey);
		}
		// insertion sort, far first (records: [depth, scx, name, size, zlift, flash])
		for (int i3 = 1; i3 < sp.size(); ++i3) {
			var rec = sp[i3];
			int j = i3 - 1;
			while (j >= 0) {
				var cmp = sp[j];
				float dj = cmp[0];
				float di = rec[0];
				if (dj >= di) { break; }
				sp[j + 1] = cmp;
				j = j - 1;
			}
			sp[j + 1] = rec;
		}
		for (auto& rec2 : sp) { draw_billboard(rec2); }

		// --- 4. particles: pixel confetti with z-buffer clipping ---
		draw_particles(G.fx.cols, inv, dirx, diry, planex, planey, px, py);

		// --- 5. overlays: automap, hurt flash ---
		if (automap) { draw_automap(G.world.tiles); }
		if (G.player.hurt_flash > 0) {
			int red = hurt_red;
			int thick = pxw * 2;
			for (int x4 = 0; x4 < vw; ++x4) {
				pix[x4] = red;
				pix[vw + x4] = red;
				pix[(vh - 2) * vw + x4] = red;
				pix[(vh - 1) * vw + x4] = red;
			}
			for (int y4 = 0; y4 < vh; ++y4) {
				int rb = y4 * vw;
				for (int t4 = 0; t4 < thick; ++t4) {
					pix[rb + t4] = red;
					pix[rb + vw - 1 - t4] = red;
				}
			}
		}

		// --- 6. pixel grid -> sub-cell glyph rows ---
		if (PIXH == 3) { return rows_sext(); }
		if (PIXW == 2) { return rows_quad(); }
		return rows_half();
	}

private:
	var bg = [];
	var lutc = {};
	var wallt = [];       // [kind 0..9][side 0..1][shade 0..15][tone 0..2] flattened
	var ceilp = [];       // per pixel-row ceiling color
	var floorp = [];      // per pixel-row floor color
	var strips = [];      // lazy 33-tall wall texture strips by (kind,side,shade,tx)
	var strip_set = [];
	var wall_ws = [];     // persistent ray-chunk pair workspaces (parallel_for v1)
	int ws_mapgen = -1;
	string halfg = "";    // upper half block
	var sextg = [];       // sextant glyphs by TL|TR<<1|ML<<2|MR<<3|BL<<4|BR<<5
	var am_cols = [];     // automap tile colors (built at init)
	int am_floor = 0;
	int am_blink = 0;
	int am_item = 0;
	int am_enemy = 0;
	int am_self = 0;
	int am_head = 0;
	int hurt_red = 0;
	var band_lum = [1.0, 0.88, 0.76, 0.64, 0.52, 0.42, 0.32, 0.24];

	void build_wall_ws(int ncols) {
		wall_ws = [];
		int nchunks = 16;
		int per_chunk = (ncols + nchunks - 1) / nchunks;
		int bx0 = 0;
		while (bx0 < ncols) {
			int bx1 = bx0 + per_chunk;
			if (bx1 > ncols) { bx1 = ncols; }
			var rec = [bx0, bx1, ncols, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, G.world.mw, 0];
			for (auto wv : G.world.mappack) { rec.push(wv); }
			rec[10] = rec.size();          // out_base: outputs start after the pack
			int outn = (bx1 - bx0) * 4;
			for (int k = 0; k < outn; ++k) { rec.push(0); }
			wall_ws.push(rec);
			bx0 = bx1;
		}
	}

	void push_wall_tones(var& base, var& accent) {
		for (int side = 0; side < 2; ++side) {
			float facing = side == 1 ? 0.70 : 1.0;
			for (int s = 0; s < 16; ++s) {
				float lum = (0.13 + 0.87 * s / 15.0) * facing;
				wallt.push(rgb_idx(math::itrunc(base[0] * lum), math::itrunc(base[1] * lum), math::itrunc(base[2] * lum)));
				float alum = lum * 1.35;
				alum = alum > 1.0 ? 1.0 : alum;
				wallt.push(rgb_idx(math::itrunc(accent[0] * alum), math::itrunc(accent[1] * alum), math::itrunc(accent[2] * alum)));
				float mlum = lum * 0.42;
				wallt.push(rgb_idx(math::itrunc(base[0] * mlum), math::itrunc(base[1] * mlum), math::itrunc(base[2] * mlum)));
			}
		}
	}

	// texture tone at (tx, ty) in 0..31 for a wall kind: 0 base, 1 accent, 2 mortar
	int tex_tone(int kind, int tx, int ty) {
		if (kind == 1) {
			int tone = ((tx >> 2) + (ty >> 2)) & 1;
			if ((ty & 7) == 7) { tone = 2; }
			return tone;
		}
		if (kind == 2) {
			int tone = ((tx + ((ty >> 3 & 1) << 2)) >> 3) & 1;
			if ((ty & 7) == 0) { tone = 2; }
			return tone;
		}
		if (kind == 3) {
			int tone = (tx >> 3) & 1;
			if ((tx & 7) == 0) { tone = 2; }
			return tone;
		}
		if (kind == 4) {
			int tone = ((tx >> 4) + (ty >> 4)) & 1;
			if ((tx & 15) == 0 || (ty & 15) == 0) { tone = 2; }
			return tone;
		}
		if (kind == 6) {
			int tone = ((tx + ty) >> 2) & 1;               // hazard stripes
			if (ty > 26) { tone = 2; }
			return tone;
		}
		// doors (plain / red / blue): frame, seam, lock studs
		if (tx < 3 || tx > 28 || ty < 2 || ty > 29) { return 2; }
		if (tx == 15 || tx == 16) { return 2; }
		if (ty >= 14 && ty <= 17 && (tx & 3) == 1) { return 1; }
		return 0;
	}

	var build_strip(int kind, int side, int shade, int tx) {
		int tbase = ((kind * 2 + side) * 16 + shade) * 3;
		var s = [];
		for (int ty = 0; ty < 32; ++ty) {
			s.push(wallt[tbase + tex_tone(kind, tx, ty)]);
		}
		s.push(s[31]);        // fixed-point overshoot guard
		return s;
	}

	// per (art, shade band 0..7, flash) palette-code -> palette-index LUT, cached
	var sprite_lut(string name, int band, int flash) {
		string key = `${name}:${band}:${flash}`;
		if (lutc.has(key)) { return lutc[key]; }
		auto& art = ARTS[name];
		float lum = band_lum[band];
		var lut = [0];
		for (int c = 1; c < art.rgb.size(); ++c) {
			int r = art.rgb[c][0];
			int g = art.rgb[c][1];
			int b = art.rgb[c][2];
			if (flash == 1) {
				r = imin(255, r + 170);
				g = imin(255, g + 150);
				b = imin(255, b + 140);
			}
			lut.push(rgb_idx(math::itrunc(r * lum), math::itrunc(g * lum), math::itrunc(b * lum)));
		}
		lutc[key] = lut;
		return lut;
	}

	// world -> camera -> screen; appends [depth, scx, art, size, zlift, flash]
	void collect(var& sp, float wx, float wy, string art, float size, float zlift,
			int flash, float inv, float dirx, float diry, float planex, float planey) {
		float rx = wx - G.player.px;
		float ry = wy - G.player.py;
		float depth = inv * (planex * ry - planey * rx);
		if (depth < 0.18 || depth > 15.0) { return; }
		float ttx = inv * (diry * rx - dirx * ry);
		int scx = (view_w / 2.0) * (1.0 + ttx / depth);
		if (scx < -view_w || scx > view_w * 2) { return; }
		sp.push([depth, scx, art, size, zlift, flash]);
	}

	void draw_billboard(var& rec) {
		float depth = rec[0];
		int scx = rec[1];
		string name = rec[2];
		float size = rec[3];
		float zlift = rec[4];
		int flash = rec[5];
		auto& art = ARTS[name];
		int aw = art.w;
		int ah = art.h;
		int vh = view_h;
		float span = 1.0 * vh / depth;
		int ph = span * size;
		if (ph < 2) { ph = 2; }
		// pixel aspect: a cell is ~1:2 (w:h) split into PIXW x PIXH
		int pw = ph * aw * 2 * PIXW / (ah * PIXH);
		if (pw < 2) { pw = 2; }
		int band = math::itrunc(depth * 0.85) - (G.player.muzzle > 0 ? 1 : 0);
		band = band < 0 ? 0 : (band > 7 ? 7 : band);
		var lut = sprite_lut(name, band, flash);
		int bottom = vh / 2 + span * 0.5 - zlift * span;
		int top = bottom - ph;
		int x0 = scx - pw / 2;
		int x1 = x0 + pw;
		int cx0 = x0 < 0 ? 0 : x0;
		int cx1 = x1 > view_w ? view_w : x1;
		int cy0 = top < 0 ? 0 : top;
		int cy1 = bottom > vh ? vh : bottom;
		// art.pix can't bind to an auto& DECLARATION (member expression), but it
		// IS a valid lvalue ARGUMENT — hence the blit helper. (A naive inline
		// with chained art.pix[...] reads measured the same within noise at
		// GLOOM's billboard load — see IDIOMATIC_COST.md §4.)
		blit_sprite(art.pix, lut, aw, ah, depth, x0, top, pw, ph, cx0, cx1, cy0, cy1);
	}

	void blit_sprite(var& apix, var& lut, int aw, int ah, float depth,
			int x0, int top, int pw, int ph, int cx0, int cx1, int cy0, int cy1) {
		auto& pix = PIX;
		auto& zb = zbuf;
		int vw = view_w;
		for (int x = cx0; x < cx1; ++x) {
			float zd = zb[x];
			if (depth >= zd) { continue; }
			int ax = (x - x0) * aw / pw;
			for (int y = cy0; y < cy1; ++y) {
				int ay = (y - top) * ah / ph;
				int code = apix[ay * aw + ax];
				if (code > 0) { pix[y * vw + x] = lut[code]; }
			}
		}
	}

	void draw_particles(var& pcols, float inv, float dirx, float diry,
			float planex, float planey, float px, float py) {
		auto& pix = PIX;
		auto& zb = zbuf;
		int vw = view_w;
		int vh = view_h;
		int half2 = vh / 2;
		int pxw = PIXW;
		int pxh = PIXH;
		for (auto& prt : G.fx.pool) {
			int pk = prt[0];
			if (pk == 0) { continue; }
			float rx = prt[1] - px;
			float ry = prt[2] - py;
			float depth = inv * (planex * ry - planey * rx);
			if (depth < 0.15 || depth > 14.0) { continue; }
			float ttx = inv * (diry * rx - dirx * ry);
			int scx = (vw / 2.0) * (1.0 + ttx / depth);
			if (scx < 0 || scx >= vw) { continue; }
			float zd = zb[scx];
			if (depth >= zd) { continue; }
			float span = 1.0 * vh / depth;
			int pyx = half2 + span * 0.5 - prt[3] * span;
			int life = prt[7];
			int phase = 3 - imin(3, life / 5);
			int color = pcols[pk * 4 + phase];
			int psz = math::itrunc(0.07 * span) + 1;
			psz = psz > 3 ? 3 : psz;
			int psx = (psz * 2 * pxw) / pxh;           // square-ish regardless of mode
			psx = psx < 1 ? 1 : psx;
			for (int oy = 0; oy < psz; ++oy) {
				int yy = pyx + oy;
				if (yy < 0 || yy >= vh) { continue; }
				for (int ox = 0; ox < psx; ++ox) {
					int xx = scx + ox;
					if (xx >= vw) { continue; }
					float zx = zb[xx];
					if (depth < zx) { pix[yy * vw + xx] = color; }
				}
			}
		}
	}

	// arcade automap: square tiles, walls in kind colors, live blips
	void draw_automap(var& tiles) {
		auto& pix = PIX;
		int vw = view_w;
		int mw = G.world.mw;
		int mh = G.world.mh;
		int sx = PIXW * 2;
		int sy = PIXH;
		int ox = sx * 2;
		int oy = sy * 2;
		auto& kc = am_cols;
		for (int ty = 0; ty < mh; ++ty) {
			for (int tx = 0; tx < mw; ++tx) {
				int t = tiles[ty * mw + tx];
				int c = t == 0 ? am_floor : kc[t];
				if (t == 6 && (G.tick & 8) == 0) { c = am_blink; }
				int bx = ox + tx * sx;
				int by = oy + ty * sy;
				for (int yy = 0; yy < sy; ++yy) {
					int rb = (by + yy) * vw + bx;
					for (int xx = 0; xx < sx; ++xx) { pix[rb + xx] = c; }
				}
			}
		}
		// blips: items gold, live enemies red, you white (+ heading pixel)
		for (auto& it : G.items) {
			if (it.taken) { continue; }
			blip(ox + math::itrunc(it.x) * sx, oy + math::itrunc(it.y) * sy, am_item);
		}
		for (auto& e : G.enemies) {
			if (!e.alive) { continue; }
			blip(ox + math::itrunc(e.x) * sx, oy + math::itrunc(e.y) * sy, am_enemy);
		}
		blip(ox + math::itrunc(G.player.px) * sx, oy + math::itrunc(G.player.py) * sy, am_self);
		blip(ox + math::itrunc(G.player.px + math::cos(G.player.pang) * 1.4) * sx,
			oy + math::itrunc(G.player.py + math::sin(G.player.pang) * 1.4) * sy, am_head);
	}

	void blip(int bx, int by, int c) {
		auto& pix = PIX;
		int vw = view_w;
		int vh = view_h;
		if (bx < 0 || by < 0 || bx + 1 >= vw || by + 1 >= vh) { return; }
		pix[by * vw + bx] = c;
		pix[by * vw + bx + 1] = c;
		pix[(by + 1) * vw + bx] = c;
		pix[(by + 1) * vw + bx + 1] = c;
	}

	// ------------------------------------------- pixel grid -> glyph rows ---
	// half mode: fg = top pixel, bg = bottom pixel, upper-half-block glyph
	var rows_half() {
		int vw = view_w;
		auto& pix = PIX;
		auto& fgs = PAL_FG;
		auto& bgs = PAL_BG;
		auto& parts = rowparts;
		string hb = halfg;
		var rows = [];
		int vrows = view_h / 2;
		for (int ry = 0; ry < vrows; ++ry) {
			parts.clear();
			int ptop = -1;
			int pbot = -1;
			int itop = ry * 2 * vw;
			int ibot = itop + vw;
			for (int x = 0; x < vw; ++x) {
				int tc = pix[itop + x];
				int bc = pix[ibot + x];
				if (tc != ptop) { parts.push(fgs[tc]); ptop = tc; }
				if (bc != pbot) { parts.push(bgs[bc]); pbot = bc; }
				parts.push(hb);
			}
			parts.push(RESET);
			rows.push(parts.join(""));
		}
		return rows;
	}

	// quad mode: ONE glyph row per gloom_row_quad call — a parallel_transform
	// over ROWIDX when the host grants workers, a serial loop otherwise
	var rows_quad() {
		if (HOST_WORKERS == 0) {
			var rows = [];
			int vrows = VH / 2;
			for (int ry = 0; ry < vrows; ++ry) { rows.push(gloom_row_quad(ry)); }
			return rows;
		}
		return parallel_transform(ROWIDX, gloom_row_quad);
	}

	// sext mode: 2x3 pixels per cell (Unicode 13 "Symbols for Legacy Computing")
	var rows_sext() {
		int vw = view_w;
		auto& pix = PIX;
		auto& fgs = PAL_FG;
		auto& bgs = PAL_BG;
		auto& lums = PAL_LUM;
		auto& sg = sextg;
		auto& parts = rowparts;
		var rows = [];
		int vrows = view_h / 3;
		int cells = vw / 2;
		for (int ry = 0; ry < vrows; ++ry) {
			parts.clear();
			int pfg = -1;
			int pbg = -1;
			int r0 = ry * 3 * vw;
			int r1 = r0 + vw;
			int r2 = r1 + vw;
			for (int cx = 0; cx < cells; ++cx) {
				int xx = cx * 2;
				int p0 = pix[r0 + xx];
				int p1 = pix[r0 + xx + 1];
				int p2 = pix[r1 + xx];
				int p3 = pix[r1 + xx + 1];
				int p4 = pix[r2 + xx];
				int p5 = pix[r2 + xx + 1];
				if (p0 == p1 && p0 == p2 && p0 == p3 && p0 == p4 && p0 == p5) {
					if (p0 != pbg) { parts.push(bgs[p0]); pbg = p0; }
					parts.push(" ");
					continue;
				}
				int l0 = lums[p0];
				int l1 = lums[p1];
				int l2 = lums[p2];
				int l3 = lums[p3];
				int l4 = lums[p4];
				int l5 = lums[p5];
				int lmin = l0;
				int lmax = l0;
				int fgp = p0;
				int bgp = p0;
				if (l1 < lmin) { lmin = l1; bgp = p1; }
				if (l1 > lmax) { lmax = l1; fgp = p1; }
				if (l2 < lmin) { lmin = l2; bgp = p2; }
				if (l2 > lmax) { lmax = l2; fgp = p2; }
				if (l3 < lmin) { lmin = l3; bgp = p3; }
				if (l3 > lmax) { lmax = l3; fgp = p3; }
				if (l4 < lmin) { lmin = l4; bgp = p4; }
				if (l4 > lmax) { lmax = l4; fgp = p4; }
				if (l5 < lmin) { lmin = l5; bgp = p5; }
				if (l5 > lmax) { lmax = l5; fgp = p5; }
				int thr = (lmin + lmax + 1) / 2;
				int bits = 0;
				if (l0 >= thr) { bits = 1; }
				if (l1 >= thr) { bits += 2; }
				if (l2 >= thr) { bits += 4; }
				if (l3 >= thr) { bits += 8; }
				if (l4 >= thr) { bits += 16; }
				if (l5 >= thr) { bits += 32; }
				if (fgp != pfg) { parts.push(fgs[fgp]); pfg = fgp; }
				if (bits != 63 && bgp != pbg) { parts.push(bgs[bgp]); pbg = bgp; }
				parts.push(sg[bits]);
			}
			parts.push(RESET);
			rows.push(parts.join(""));
		}
		return rows;
	}
}
