// The renderer. The view is a flat TRUECOLOR palette-index pixel grid, blitted
// to the terminal through sub-cell block glyphs (the doom-cli approach):
//
//   half  (--pix half): 1x2 px/cell, upper-half-block, fg=top bg=bottom
//   quad  (--pix quad): 2x2 px/cell, quadrant glyphs, 2 colors by luminosity
//   sext  (--pix sext): 2x3 px/cell, sextant glyphs (needs a Unicode-13 font)
//
// One wall ray per cell column, then ordinary serial painting: billboards,
// particles, overlays, and the glyph row builders.

int PIXW = 2;        // pixels per cell horizontally (set by gloom_boot)
int PIXH = 2;        // pixels per cell vertically

string RESET;
array<string> QUADG;           // quadrant glyphs by TL|TR<<1|BL<<2|BR<<3
array<string> SEXTG;           // sextant glyphs by TL|TR<<1|ML<<2|MR<<3|BL<<4|BR<<5
string HALFG;                  // upper half block
array<int> AM_COLS;            // automap tile colors (built at boot)
int AM_FLOOR = 0;
int AM_BLINK = 0;
int AM_ITEM = 0;
int AM_ENEMY = 0;
int AM_SELF = 0;
int AM_HEAD = 0;
int HURT_RED = 0;
array<array<int>@> STRIPS;     // lazy 33-tall wall texture strips by (kind,side,shade,tx)

void init_render() {
	RESET = ESC + "[0m";
	rgb_idx(0, 0, 0);                 // palette index 0 is black (grid clear value)
	HALFG = utf8(0x2580);
	QUADG = {" "};
	const array<int> quad_cps = {0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
		0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588};
	for (uint i = 0; i < quad_cps.length(); ++i) { QUADG.insertLast(utf8(quad_cps[i])); }
	SEXTG.resize(0);
	for (int b = 0; b < 64; ++b) {
		if (b == 0) { SEXTG.insertLast(" "); }
		else if (b == 21) { SEXTG.insertLast(utf8(0x258C)); }        // left half
		else if (b == 42) { SEXTG.insertLast(utf8(0x2590)); }        // right half
		else if (b == 63) { SEXTG.insertLast(utf8(0x2588)); }        // full
		else {
			int cp = 0x1FB00 + b - 1;
			if (b > 21) { cp = cp - 1; }
			if (b > 42) { cp = cp - 1; }
			SEXTG.insertLast(utf8(cp));
		}
	}
	AM_COLS = {0, rgb_idx(150, 140, 120), rgb_idx(110, 150, 110), rgb_idx(110, 120, 160), rgb_idx(160, 110, 96),
		rgb_idx(200, 170, 110), rgb_idx(90, 230, 110), rgb_idx(150, 140, 120), rgb_idx(230, 70, 60), rgb_idx(90, 120, 240)};
	AM_FLOOR = rgb_idx(40, 40, 46);
	AM_BLINK = rgb_idx(240, 255, 160);
	AM_ITEM = rgb_idx(255, 220, 80);
	AM_ENEMY = rgb_idx(255, 60, 50);
	AM_SELF = rgb_idx(255, 255, 255);
	AM_HEAD = rgb_idx(255, 250, 140);
	HURT_RED = rgb_idx(200, 30, 24);
	build_particle_colors();
}

// wall tone table: [kind 0..9][side 0..1][shade 0..15][tone 0..2] flattened;
// tones: 0 base, 1 accent, 2 mortar/frame. Truecolor, 16 shades.
array<int> WALLT;
array<int> CEILP;     // per pixel-row ceiling color
array<int> FLOORP;    // per pixel-row floor color

void push_wall_tones(double br, double bg, double bb, double ar, double ag, double ab) {
	for (int side = 0; side < 2; ++side) {
		double facing = side == 1 ? (7.0 / 10.0) : 1.0;
		for (int s = 0; s < 16; ++s) {
			double lum = ((13.0 / 100.0) + (87.0 / 100.0) * s / 15.0) * facing;
			WALLT.insertLast(rgb_idx(itrunc(br * lum), itrunc(bg * lum), itrunc(bb * lum)));
			double alum = lum * (135.0 / 100.0);
			alum = alum > 1.0 ? 1.0 : alum;
			WALLT.insertLast(rgb_idx(itrunc(ar * alum), itrunc(ag * alum), itrunc(ab * alum)));
			double mlum = lum * (42.0 / 100.0);
			WALLT.insertLast(rgb_idx(itrunc(br * mlum), itrunc(bg * mlum), itrunc(bb * mlum)));
		}
	}
}

void build_render_tables(int mi) {
	MapDef@ theme = MAPS[mi];
	WALLT.resize(0);
	// kind 0 (never rendered): flat dark
	push_wall_tones(10.0, 10.0, 12.0, 10.0, 10.0, 12.0);
	// kinds 1..4 from the theme
	for (int k = 0; k < 4; ++k) {
		double r = theme.walls[k * 3];
		double g = theme.walls[k * 3 + 1];
		double b = theme.walls[k * 3 + 2];
		push_wall_tones(r, g, b, r, g, b);
	}
	// kind 5 door, 6 exit, 7 red door, 8 blue door (episode constants)
	push_wall_tones(138.0, 116.0, 94.0, 188.0, 164.0, 132.0);
	push_wall_tones(64.0, 160.0, 84.0, 220.0, 235.0, 120.0);
	push_wall_tones(176.0, 50.0, 42.0, 245.0, 130.0, 96.0);
	push_wall_tones(62.0, 94.0, 198.0, 136.0, 176.0, 255.0);

	// ceiling / floor gradients (dark at the horizon), quantized to CELL rows
	// so flat regions hit the row builder's uniform-cell fast path
	CEILP.resize(0);
	FLOORP.resize(0);
	int half = G.vh / 2;
	for (int y = 0; y < G.vh; ++y) {
		int yq = (y / PIXH) * PIXH;
		if (y < half) {
			double t = half > 1 ? 1.0 * yq / (half - 1) : 1.0;
			double lum = 1.0 - (72.0 / 100.0) * t;
			CEILP.insertLast(rgb_idx(itrunc(theme.ceil[0] * lum), itrunc(theme.ceil[1] * lum), itrunc(theme.ceil[2] * lum)));
			FLOORP.insertLast(0);
		} else {
			double t2 = 1.0 * (yq - half) / imax(1, G.vh - 1 - half);
			t2 = t2 > 1.0 ? 1.0 : (t2 < 0.0 ? 0.0 : t2);
			double lum2 = (26.0 / 100.0) + (74.0 / 100.0) * t2;
			CEILP.insertLast(0);
			FLOORP.insertLast(rgb_idx(itrunc(theme.floor[0] * lum2), itrunc(theme.floor[1] * lum2), itrunc(theme.floor[2] * lum2)));
		}
	}
	// prebaked background: one array copy per frame replaces the per-pixel
	// ceiling/floor loops outright
	G.bg.resize(0);
	for (int y2 = 0; y2 < G.vh; ++y2) {
		int c = y2 < half ? CEILP[y2] : FLOORP[y2];
		for (int x2 = 0; x2 < G.vw; ++x2) { G.bg.insertLast(c); }
	}
	// wall tone strips (32-tall texture columns) build lazily per combination
	STRIPS.resize(0);
	STRIPS.resize(10 * 2 * 16 * 32);
	G.lutc.deleteAll();
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

array<int>@ build_strip(int kind, int side, int shade, int tx) {
	int tbase = ((kind * 2 + side) * 16 + shade) * 3;
	array<int> s(33);
	for (int ty = 0; ty < 32; ++ty) { s[ty] = WALLT[tbase + tex_tone(kind, tx, ty)]; }
	s[32] = s[31];        // fixed-point overshoot guard
	return s;
}

// ---------------------------------------------------------- sprite LUTs -----
// per (art, shade band 0..7, flash) palette-code -> palette-index table, cached
const array<double> BAND_LUM = {1.0, (88.0 / 100.0), (76.0 / 100.0), (64.0 / 100.0), (52.0 / 100.0), (42.0 / 100.0), (32.0 / 100.0), (24.0 / 100.0)};

array<int>@ sprite_lut(const string &in name, int band, int flash) {
	string key = name + ":" + band + ":" + flash;
	array<int>@ cached = cast<array<int>>(G.lutc[key]);
	if (cached !is null) { return cached; }
	Art@ art = cast<Art>(ARTS[name]);
	double lum = BAND_LUM[band];
	array<int> lut = {0};
	int codes = int(art.rgb.length()) / 3;
	for (int c = 1; c < codes; ++c) {
		int r = art.rgb[c * 3];
		int g = art.rgb[c * 3 + 1];
		int b = art.rgb[c * 3 + 2];
		if (flash == 1) {
			r = imin(255, r + 170);
			g = imin(255, g + 150);
			b = imin(255, b + 140);
		}
		lut.insertLast(rgb_idx(itrunc(r * lum), itrunc(g * lum), itrunc(b * lum)));
	}
	@G.lutc[key] = lut;
	return lut;
}

// -------------------------------------------------------------- the view ----
// returns the view's glyph rows
array<string>@ render_view() {
	array<int>@ pix = G.pix;
	array<double>@ zbuf = G.zbuf;
	int vw = G.vw;
	int vh = G.vh;
	double dirx = cos(G.pang);
	double diry = sin(G.pang);
	double planex = 0.0 - diry * (78.0 / 100.0);
	double planey = dirx * (78.0 / 100.0);
	double px = G.px;
	double py = G.py;

	// --- 1+2. one ray per CELL column (sub-pixels share it), painted over the
	// prebaked ceiling/floor background as a textured wall slice
	pix = G.bg;
	int pxw = PIXW;
	int ncols = vw / pxw;
	double xstep = 2.0 / (ncols - 1.0);
	int boost = G.muzzle > 0 ? 2 : 0;
	for (int x = 0; x < ncols; ++x) {
		double camx = x * xstep - 1.0;
		double dist;
		int side;
		int kind;
		int texcol;
		gloom_ray(px, py, dirx + planex * camx, diry + planey * camx, dist, side, kind, texcol);
		int tx = texcol >> 1;                 // 0..31
		int px0 = x * pxw;
		zbuf[px0] = dist;
		if (pxw == 2) { zbuf[px0 + 1] = dist; }
		int lineh = int(1.0 * vh / dist);
		lineh = lineh < 1 ? 1 : lineh;
		int y0 = (vh - lineh) / 2;
		int y1 = y0 + lineh;
		int cy0 = y0 < 0 ? 0 : y0;
		int cy1 = y1 > vh ? vh : y1;
		int shade = 15 - itrunc(dist * (155.0 / 100.0)) + boost;
		shade = shade < 0 ? 0 : (shade > 15 ? 15 : shade);
		int sidx = ((kind * 2 + side) * 16 + shade) * 32 + tx;
		array<int>@ strip = STRIPS[sidx];
		if (strip is null) {
			@strip = build_strip(kind, side, shade, tx);
			@STRIPS[sidx] = strip;
		}
		int tstep = 65536 / lineh;            // (32 << 11) / lineh
		int tacc = (cy0 - y0) * tstep;
		int gi = cy0 * vw + px0;
		if (pxw == 2) {
			for (int y = cy0; y < cy1; ++y) {
				int c = strip[tacc >> 11];
				pix[gi] = c;
				pix[gi + 1] = c;
				tacc += tstep;
				gi += vw;
			}
		} else {
			for (int y = cy0; y < cy1; ++y) {
				pix[gi] = strip[tacc >> 11];
				tacc += tstep;
				gi += vw;
			}
		}
	}

	// --- 3. billboards: enemies, pickups, projectiles (far to near) ---
	double det = planex * diry - dirx * planey;
	if (det < (1.0 / 10000.0) && det > -(1.0 / 10000.0)) { det = (1.0 / 10000.0); }
	double inv = 1.0 / det;
	array<Billboard@> sp;
	for (uint i = 0; i < G.enemies.length(); ++i) {
		Enemy@ e = G.enemies[i];
		collect_billboard(sp, e.x, e.y, enemy_art(e), ENEMY_DEFS[e.kind].size, 0.0,
			e.flash > 0 ? 1 : 0, inv, dirx, diry, planex, planey);
	}
	for (uint i = 0; i < G.items.length(); ++i) {
		Item@ it = G.items[i];
		if (it.taken) { continue; }
		ItemDef@ def = ITEM_DEFS[it.kind];
		collect_billboard(sp, it.x, it.y, def.art, def.size, 0.0, 0, inv, dirx, diry, planex, planey);
	}
	for (uint i = 0; i < G.shots.length(); ++i) {
		Shot@ s = G.shots[i];
		collect_billboard(sp, s.x, s.y, SHOT_ART[s.kind], (16.0 / 100.0), (35.0 / 100.0), 0, inv, dirx, diry, planex, planey);
	}
	// insertion sort, far first
	for (int i3 = 1; i3 < int(sp.length()); ++i3) {
		Billboard@ rec = sp[i3];
		double di = rec.depth;
		int j = i3 - 1;
		while (j >= 0) {
			Billboard@ cmp = sp[j];
			if (cmp.depth >= di) { break; }
			@sp[j + 1] = cmp;
			j = j - 1;
		}
		@sp[j + 1] = rec;
	}
	for (uint i = 0; i < sp.length(); ++i) { draw_billboard(sp[i], pix, zbuf); }

	// --- 4. particles: pixel confetti with z-buffer clipping ---
	int half2 = vh / 2;
	int pxh = PIXH;
	for (uint i = 0; i < G.fx.pool.length(); ++i) {
		Particle@ prt = G.fx.pool[i];
		int pk = prt.kind;
		if (pk == 0) { continue; }
		double rx = prt.x - px;
		double ry = prt.y - py;
		double depth = inv * (planex * ry - planey * rx);
		if (depth < (15.0 / 100.0) || depth > 14.0) { continue; }
		double ttx = inv * (diry * rx - dirx * ry);
		int scx = int((vw / 2.0) * (1.0 + ttx / depth));
		if (scx < 0 || scx >= vw) { continue; }
		if (depth >= zbuf[scx]) { continue; }
		double span = 1.0 * vh / depth;
		int pyx = int(half2 + span * (5.0 / 10.0) - prt.z * span);
		int phase = 3 - imin(3, prt.life / 5);
		int color = PART_COLS[pk * 4 + phase];
		int psz = itrunc((7.0 / 100.0) * span) + 1;
		psz = psz > 3 ? 3 : psz;
		int psx = (psz * 2 * pxw) / pxh;           // square-ish regardless of mode
		psx = psx < 1 ? 1 : psx;
		for (int oy = 0; oy < psz; ++oy) {
			int yy = pyx + oy;
			if (yy < 0 || yy >= vh) { continue; }
			for (int ox = 0; ox < psx; ++ox) {
				int xx = scx + ox;
				if (xx >= vw) { continue; }
				if (depth < zbuf[xx]) { pix[yy * vw + xx] = color; }
			}
		}
	}

	// --- 5. overlays: automap, hurt flash ---
	if (G.automap) { draw_automap(pix, G.tiles); }
	if (G.hurt_flash > 0) {
		int red = HURT_RED;
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
	if (PIXH == 3) { return rows_sext(pix); }
	if (PIXW == 2) { return rows_quad(pix); }
	return rows_half(pix);
}

const array<string> SHOT_ART = {"", "pr_spit", "pr_fire", "pr_hex"};

// half mode: fg = top pixel, bg = bottom pixel, upper-half-block glyph
array<string>@ rows_half(const array<int> &in pix) {
	int vw = G.vw;
	array<string> rows;
	int vrows = G.vh / 2;
	for (int ry = 0; ry < vrows; ++ry) {
		string row;
		int ptop = -1;
		int pbot = -1;
		int itop = ry * 2 * vw;
		int ibot = itop + vw;
		for (int x = 0; x < vw; ++x) {
			int tc = pix[itop + x];
			int bc = pix[ibot + x];
			if (tc != ptop) {
				row += PAL_FG[tc];
				ptop = tc;
			}
			if (bc != pbot) {
				row += PAL_BG[bc];
				pbot = bc;
			}
			row += HALFG;
		}
		row += RESET;
		rows.insertLast(row);
	}
	return rows;
}

// quad mode: 2x2 pixels per cell, two colors by luminosity partition
array<string>@ rows_quad(const array<int> &in pix) {
	int vw = G.vw;
	int cells = vw / 2;
	array<string> rows;
	int vrows = G.vh / 2;
	for (int ry = 0; ry < vrows; ++ry) {
		string row;
		int pfg = -1;
		int pbg = -1;
		int itop = ry * 2 * vw;
		int ibot = itop + vw;
		for (int cx = 0; cx < cells; ++cx) {
			int xx = cx * 2;
			int p0 = pix[itop + xx];
			int p1 = pix[itop + xx + 1];
			int p2 = pix[ibot + xx];
			int p3 = pix[ibot + xx + 1];
			if (p0 == p1 && p0 == p2 && p0 == p3) {
				if (p0 != pbg) {
					row += PAL_BG[p0];
					pbg = p0;
				}
				row += " ";
				continue;
			}
			// wall slices split cells into vertical or horizontal halves far
			// more often than not: cheap two-color paths before the full
			// luminosity partition
			if (p0 == p2 && p1 == p3) {
				if (p0 != pfg) {
					row += PAL_FG[p0];
					pfg = p0;
				}
				if (p1 != pbg) {
					row += PAL_BG[p1];
					pbg = p1;
				}
				row += QUADG[5];                 // left half
				continue;
			}
			if (p0 == p1 && p2 == p3) {
				if (p0 != pfg) {
					row += PAL_FG[p0];
					pfg = p0;
				}
				if (p2 != pbg) {
					row += PAL_BG[p2];
					pbg = p2;
				}
				row += QUADG[3];                 // upper half
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
			if (fgp != pfg) {
				row += PAL_FG[fgp];
				pfg = fgp;
			}
			if (bits != 15 && bgp != pbg) {
				row += PAL_BG[bgp];
				pbg = bgp;
			}
			row += QUADG[bits];
		}
		row += RESET;
		rows.insertLast(row);
	}
	return rows;
}

// sext mode: 2x3 pixels per cell (Unicode 13 "Symbols for Legacy Computing")
array<string>@ rows_sext(const array<int> &in pix) {
	int vw = G.vw;
	array<string> rows;
	int vrows = G.vh / 3;
	int cells = vw / 2;
	for (int ry = 0; ry < vrows; ++ry) {
		string row;
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
				if (p0 != pbg) {
					row += PAL_BG[p0];
					pbg = p0;
				}
				row += " ";
				continue;
			}
			int l0 = PAL_LUM[p0];
			int l1 = PAL_LUM[p1];
			int l2 = PAL_LUM[p2];
			int l3 = PAL_LUM[p3];
			int l4 = PAL_LUM[p4];
			int l5 = PAL_LUM[p5];
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
			if (fgp != pfg) {
				row += PAL_FG[fgp];
				pfg = fgp;
			}
			if (bits != 63 && bgp != pbg) {
				row += PAL_BG[bgp];
				pbg = bgp;
			}
			row += SEXTG[bits];
		}
		row += RESET;
		rows.insertLast(row);
	}
	return rows;
}

// a sprite slice queued for the far-to-near paint
class Billboard {
	double depth;
	int scx;
	string art;
	double size;
	double zlift;
	int flash;

	Billboard(double depth_, int scx_, const string &in art_, double size_, double zlift_, int flash_) {
		depth = depth_;
		scx = scx_;
		art = art_;
		size = size_;
		zlift = zlift_;
		flash = flash_;
	}
}

// world -> camera -> screen; appends a Billboard
void collect_billboard(array<Billboard@>@ sp, double wx, double wy, const string &in art, double size, double zlift,
		int flash, double inv, double dirx, double diry, double planex, double planey) {
	double rx = wx - G.px;
	double ry = wy - G.py;
	double depth = inv * (planex * ry - planey * rx);
	if (depth < (18.0 / 100.0) || depth > 15.0) { return; }
	double ttx = inv * (diry * rx - dirx * ry);
	int scx = int((G.vw / 2.0) * (1.0 + ttx / depth));
	if (scx < -G.vw || scx > G.vw * 2) { return; }
	sp.insertLast(Billboard(depth, scx, art, size, zlift, flash));
}

void draw_billboard(Billboard@ rec, array<int>@ pix, const array<double> &in zbuf) {
	double depth = rec.depth;
	int scx = rec.scx;
	Art@ art = cast<Art>(ARTS[rec.art]);
	int aw = art.w;
	int ah = art.h;
	int vw = G.vw;
	int vh = G.vh;
	double span = 1.0 * vh / depth;
	int ph = int(span * rec.size);
	if (ph < 2) { ph = 2; }
	// pixel aspect: a cell is ~1:2 (w:h) split into PIXW x PIXH
	int pw = ph * aw * 2 * PIXW / (ah * PIXH);
	if (pw < 2) { pw = 2; }
	int band = itrunc(depth * (85.0 / 100.0)) - (G.muzzle > 0 ? 1 : 0);
	band = band < 0 ? 0 : (band > 7 ? 7 : band);
	array<int>@ lut = sprite_lut(rec.art, band, rec.flash);
	int bottom = int(vh / 2 + span * (5.0 / 10.0) - rec.zlift * span);
	int top = bottom - ph;
	int x0 = scx - pw / 2;
	int x1 = x0 + pw;
	int cx0 = x0 < 0 ? 0 : x0;
	int cx1 = x1 > vw ? vw : x1;
	int cy0 = top < 0 ? 0 : top;
	int cy1 = bottom > vh ? vh : bottom;
	for (int x = cx0; x < cx1; ++x) {
		if (depth >= zbuf[x]) { continue; }
		int ax = (x - x0) * aw / pw;
		for (int y = cy0; y < cy1; ++y) {
			int ay = (y - top) * ah / ph;
			int code = art.pix[ay * aw + ax];
			if (code > 0) { pix[y * vw + x] = lut[code]; }
		}
	}
}

// arcade automap: square tiles, walls in kind colors, live blips
void draw_automap(array<int>@ pix, const array<int> &in tiles) {
	int vw = G.vw;
	int mw = G.mw;
	int mh = G.mh;
	int sx = PIXW * 2;
	int sy = PIXH;
	int ox = sx * 2;
	int oy = sy * 2;
	for (int ty = 0; ty < mh; ++ty) {
		for (int tx = 0; tx < mw; ++tx) {
			int t = tiles[ty * mw + tx];
			int c = t == 0 ? AM_FLOOR : AM_COLS[t];
			if (t == 6 && (G.tick & 8) == 0) { c = AM_BLINK; }
			int bx = ox + tx * sx;
			int by = oy + ty * sy;
			for (int yy = 0; yy < sy; ++yy) {
				int rb = (by + yy) * vw + bx;
				for (int xx = 0; xx < sx; ++xx) { pix[rb + xx] = c; }
			}
		}
	}
	// blips: items gold, live enemies red, you white (+ heading pixel)
	for (uint i = 0; i < G.items.length(); ++i) {
		Item@ it = G.items[i];
		if (it.taken) { continue; }
		blip(pix, ox + itrunc(it.x) * sx, oy + itrunc(it.y) * sy, AM_ITEM);
	}
	for (uint i = 0; i < G.enemies.length(); ++i) {
		Enemy@ e = G.enemies[i];
		if (!e.alive) { continue; }
		blip(pix, ox + itrunc(e.x) * sx, oy + itrunc(e.y) * sy, AM_ENEMY);
	}
	blip(pix, ox + itrunc(G.px) * sx, oy + itrunc(G.py) * sy, AM_SELF);
	blip(pix, ox + itrunc(G.px + cos(G.pang) * (14.0 / 10.0)) * sx, oy + itrunc(G.py + sin(G.pang) * (14.0 / 10.0)) * sy, AM_HEAD);
}

void blip(array<int>@ pix, int bx, int by, int c) {
	int vw = G.vw;
	int vh = G.vh;
	if (bx < 0 || by < 0 || bx + 1 >= vw || by + 1 >= vh) { return; }
	pix[by * vw + bx] = c;
	pix[by * vw + bx + 1] = c;
	pix[(by + 1) * vw + bx] = c;
	pix[(by + 1) * vw + bx + 1] = c;
}
