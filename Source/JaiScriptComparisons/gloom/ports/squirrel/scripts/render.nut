// The renderer. The view is a flat truecolor palette-index pixel grid, blitted
// to the terminal through sub-cell block glyphs:
//
//   half  (--pix half): 1x2 px/cell, upper-half-block, fg=top bg=bottom
//   quad  (--pix quad): 2x2 px/cell, quadrant glyphs, 2 colors by luminosity
//   sext  (--pix sext): 2x3 px/cell, sextant glyphs (needs a Unicode-13 font)
//
// The reference computes wall columns via parallel_transform over chunk
// records; ports run the serial-equivalent loop (outputs defined identical) —
// here the DDA is inlined per column, reading the tile array directly. The
// reference look casts a ray per cell column over a prebaked floor and ceiling
// (reference_view); the showcase look casts one per pixel column and textures
// and lights every pixel (showcase_view, tables from look.nut).

::PIXW <- 2;        // pixels per cell horizontally (set by gloom_boot)
::PIXH <- 2;        // pixels per cell vertically
::RESET <- "";
::QUADG <- [];      // quadrant glyphs by TL|TR<<1|BL<<2|BR<<3
::SEXTG <- [];      // sextant glyphs by TL|TR<<1|ML<<2|MR<<3|BL<<4|BR<<5
::HALFG <- "";      // upper half block
::AM_COLS <- [];    // automap tile colors (built at boot)
::AM_FLOOR <- 0;
::AM_BLINK <- 0;
::AM_ITEM <- 0;
::AM_ENEMY <- 0;
::AM_SELF <- 0;
::AM_HEAD <- 0;
::HURT_RED <- 0;
::STRIPS <- [];      // lazy 33-tall wall texture strips by (kind,side,shade,tx)
::STRIP_SET <- [];

function init_render() {
	::RESET = ::ESC + "[0m";
	::rgb_idx(0, 0, 0);               // palette index 0 is black (grid clear value)
	::HALFG = ::utf8(0x2580);
	::QUADG = [" "];
	local quad_cps = [0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
		0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588];
	foreach (cp in quad_cps) { ::QUADG.push(::utf8(cp)); }
	::SEXTG = [];
	for (local b = 0; b < 64; ++b) {
		if (b == 0) { ::SEXTG.push(" "); }
		else if (b == 21) { ::SEXTG.push(::utf8(0x258C)); }        // left half
		else if (b == 42) { ::SEXTG.push(::utf8(0x2590)); }        // right half
		else if (b == 63) { ::SEXTG.push(::utf8(0x2588)); }        // full
		else {
			local cp = 0x1FB00 + b - 1;
			if (b > 21) { cp = cp - 1; }
			if (b > 42) { cp = cp - 1; }
			::SEXTG.push(::utf8(cp));
		}
	}
	::AM_COLS = [0, ::rgb_idx(150, 140, 120), ::rgb_idx(110, 150, 110), ::rgb_idx(110, 120, 160), ::rgb_idx(160, 110, 96),
		::rgb_idx(200, 170, 110), ::rgb_idx(90, 230, 110), ::rgb_idx(150, 140, 120), ::rgb_idx(230, 70, 60), ::rgb_idx(90, 120, 240)];
	::AM_FLOOR = ::rgb_idx(40, 40, 46);
	::AM_BLINK = ::rgb_idx(240, 255, 160);
	::AM_ITEM = ::rgb_idx(255, 220, 80);
	::AM_ENEMY = ::rgb_idx(255, 60, 50);
	::AM_SELF = ::rgb_idx(255, 255, 255);
	::AM_HEAD = ::rgb_idx(255, 250, 140);
	::HURT_RED = ::rgb_idx(200, 30, 24);
	::build_particle_colors();
}

// wall tone table: [kind 0..9][side 0..1][shade 0..15][tone 0..2] flattened;
// tones: 0 base, 1 accent, 2 mortar/frame. Truecolor, 16 shades.
::WALLT <- [];
::CEILP <- [];     // per pixel-row ceiling color
::FLOORP <- [];    // per pixel-row floor color

function push_wall_tones(tone_rgb, accent) {   // ('base' is reserved in Squirrel)
	for (local side = 0; side < 2; ++side) {
		local facing = side == 1 ? 0.70 : 1.0;
		for (local s = 0; s < 16; ++s) {
			local lum = (0.13 + 0.87 * s / 15.0) * facing;
			::WALLT.push(::rgb_idx((tone_rgb[0] * lum).tointeger(), (tone_rgb[1] * lum).tointeger(), (tone_rgb[2] * lum).tointeger()));
			local alum = lum * 1.35;
			alum = alum > 1.0 ? 1.0 : alum;
			::WALLT.push(::rgb_idx((accent[0] * alum).tointeger(), (accent[1] * alum).tointeger(), (accent[2] * alum).tointeger()));
			local mlum = lum * 0.42;
			::WALLT.push(::rgb_idx((tone_rgb[0] * mlum).tointeger(), (tone_rgb[1] * mlum).tointeger(), (tone_rgb[2] * mlum).tointeger()));
		}
	}
}

function build_render_tables(mi) {
	local g = ::G;
	g.lutc = {};
	if (::SHOWCASE) {
		::build_showcase_tables(mi);
		return;
	}
	local theme = ::MAPS[mi].theme;
	::WALLT = [];
	// kind 0 (never rendered): flat dark
	local zero = [10.0, 10.0, 12.0];
	::push_wall_tones(zero, zero);
	// kinds 1..4 from the theme
	for (local k = 0; k < 4; ++k) {
		local wall_rgb = theme.walls[k];
		::push_wall_tones(wall_rgb, wall_rgb);
	}
	// kind 5 door, 6 exit, 7 red door, 8 blue door (episode constants)
	local door_b = [138.0, 116.0, 94.0];
	local door_a = [188.0, 164.0, 132.0];
	::push_wall_tones(door_b, door_a);
	local exit_b = [64.0, 160.0, 84.0];
	local exit_a = [220.0, 235.0, 120.0];
	::push_wall_tones(exit_b, exit_a);
	local red_b = [176.0, 50.0, 42.0];
	local red_a = [245.0, 130.0, 96.0];
	::push_wall_tones(red_b, red_a);
	local blue_b = [62.0, 94.0, 198.0];
	local blue_a = [136.0, 176.0, 255.0];
	::push_wall_tones(blue_b, blue_a);

	// ceiling / floor gradients (dark at the horizon), quantized to CELL rows
	// so flat regions hit the row builder's uniform-cell fast path
	::CEILP = [];
	::FLOORP = [];
	local crgb = theme.ceil;
	local frgb = theme.floor;
	local half = g.vh / 2;
	for (local y = 0; y < g.vh; ++y) {
		local yq = (y / ::PIXH) * ::PIXH;
		if (y < half) {
			local t = half > 1 ? 1.0 * yq / (half - 1) : 1.0;
			local lum = 1.0 - 0.72 * t;
			::CEILP.push(::rgb_idx((crgb[0] * lum).tointeger(), (crgb[1] * lum).tointeger(), (crgb[2] * lum).tointeger()));
			::FLOORP.push(0);
		} else {
			local t2 = 1.0 * (yq - half) / ::imax(1, g.vh - 1 - half);
			t2 = t2 > 1.0 ? 1.0 : (t2 < 0.0 ? 0.0 : t2);
			local lum2 = 0.26 + 0.74 * t2;
			::CEILP.push(0);
			::FLOORP.push(::rgb_idx((frgb[0] * lum2).tointeger(), (frgb[1] * lum2).tointeger(), (frgb[2] * lum2).tointeger()));
		}
	}
	// prebaked background: one clone per frame replaces the per-pixel
	// ceiling/floor loops outright
	local bg = [];
	for (local y2 = 0; y2 < g.vh; ++y2) {
		local c = y2 < half ? ::CEILP[y2] : ::FLOORP[y2];
		for (local x2 = 0; x2 < g.vw; ++x2) { bg.push(c); }
	}
	g.bg = bg;
	// wall tone strips (32-tall texture columns) build lazily per combination
	local nstrips = 10 * 2 * 16 * 32;
	::STRIPS = ::array(nstrips, 0);
	::STRIP_SET = ::array(nstrips, 0);
}

// texture tone at (tx, ty) in 0..31 for a wall kind: 0 base, 1 accent, 2 mortar
function tex_tone(kind, tx, ty) {
	if (kind == 1) {
		local tone = ((tx >> 2) + (ty >> 2)) & 1;
		if ((ty & 7) == 7) { tone = 2; }
		return tone;
	}
	if (kind == 2) {
		local tone = ((tx + ((ty >> 3 & 1) << 2)) >> 3) & 1;
		if ((ty & 7) == 0) { tone = 2; }
		return tone;
	}
	if (kind == 3) {
		local tone = (tx >> 3) & 1;
		if ((tx & 7) == 0) { tone = 2; }
		return tone;
	}
	if (kind == 4) {
		local tone = ((tx >> 4) + (ty >> 4)) & 1;
		if ((tx & 15) == 0 || (ty & 15) == 0) { tone = 2; }
		return tone;
	}
	if (kind == 6) {
		local tone = ((tx + ty) >> 2) & 1;               // hazard stripes
		if (ty > 26) { tone = 2; }
		return tone;
	}
	// doors (plain / red / blue): frame, seam, lock studs
	if (tx < 3 || tx > 28 || ty < 2 || ty > 29) { return 2; }
	if (tx == 15 || tx == 16) { return 2; }
	if (ty >= 14 && ty <= 17 && (tx & 3) == 1) { return 1; }
	return 0;
}

function build_strip(kind, side, shade, tx) {
	local tbase = ((kind * 2 + side) * 16 + shade) * 3;
	local wallt = ::WALLT;
	local s = [];
	for (local ty = 0; ty < 32; ++ty) {
		s.push(wallt[tbase + ::tex_tone(kind, tx, ty)]);
	}
	s.push(s[31]);        // fixed-point overshoot guard
	return s;
}

// ---------------------------------------------------------- sprite LUTs -----
// per (art, shade band 0..7, flash, light variant) palette-code -> palette-index
// table, cached
::BAND_LUM <- [1.0, 0.88, 0.76, 0.64, 0.52, 0.42, 0.32, 0.24];

function sprite_lut(name, band, flash, variant) {
	local g = ::G;
	local key = name + ":" + band + ":" + flash + ":" + variant;
	if (key in g.lutc) { return g.lutc[key]; }
	if (::SHOWCASE) {
		local slut = ::showcase_sprite_lut(name, band, flash, variant);
		g.lutc[key] <- slut;
		return slut;
	}
	local art = ::ARTS[name];
	local rgb = art.rgb;
	local lum = ::BAND_LUM[band];
	local lut = [0];
	for (local c = 1; c < rgb.len(); ++c) {
		local r = rgb[c][0];
		local gr = rgb[c][1];
		local b = rgb[c][2];
		if (flash == 1) {
			r = ::imin(255, r + 170);
			gr = ::imin(255, gr + 150);
			b = ::imin(255, b + 140);
		}
		lut.push(::rgb_idx((r * lum).tointeger(), (gr * lum).tointeger(), (b * lum).tointeger()));
	}
	g.lutc[key] <- lut;
	return lut;
}

// the reference look's rays and columns: one ray per cell column over the
// prebaked background, cached texture strips
function reference_view(pix, px, py, dirx, diry, planex, planey) {
	local g = ::G;
	local vw = g.vw;
	local vh = g.vh;
	local mw = g.mw;
	local tiles = g.tiles;
	local t2k = ::TILE_TO_KIND;
	// one ray per CELL column with the wall slice painted immediately (serial
	// equivalent of the chunk bodies)
	local zbuf = g.zbuf;
	local strips = ::STRIPS;
	local stripset = ::STRIP_SET;
	local boost = g.muzzle > 0 ? 2 : 0;
	local pxw = ::PIXW;
	local ncols = vw / pxw;
	local xstep = 2.0 / (ncols - 1.0);
	for (local x = 0; x < ncols; ++x) {
		local camx = x * xstep - 1.0;
		local rdx = dirx + planex * camx;
		local rdy = diry + planey * camx;
		local mx = px.tointeger();
		local my = py.tointeger();
		local adx = rdx < 0.0 ? -rdx : rdx;
		local ady = rdy < 0.0 ? -rdy : rdy;
		local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		local stepx = rdx < 0.0 ? -1 : 1;
		local stepy = rdy < 0.0 ? -1 : 1;
		local sdx = rdx < 0.0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
		local sdy = rdy < 0.0 ? (py - my) * ddy : (my + 1.0 - py) * ddy;
		local side = 0;
		local kind = 1;
		local guard = 0;
		local hit = false;
		while (!hit && guard < 128) {
			if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
			else { sdy += ddy; my += stepy; side = 1; }
			local c = t2k[tiles[my * mw + mx]];
			if (c != 0) { hit = true; kind = c; }
			guard++;
		}
		local dist = side == 0 ? sdx - ddx : sdy - ddy;
		dist = dist < 0.02 ? 0.02 : dist;
		local wallx = side == 0 ? py + dist * rdy : px + dist * rdx;
		wallx = wallx - ::floor(wallx);
		local tx = (wallx * 64.0).tointeger() >> 1;   // texcol/2: 0..31

		local px0 = x * pxw;
		zbuf[px0] = dist;
		if (pxw == 2) { zbuf[px0 + 1] = dist; }
		local lineh = (1.0 * vh / dist).tointeger();
		if (lineh < 1) { lineh = 1; }
		local y0 = (vh - lineh) / 2;
		local y1 = y0 + lineh;
		local cy0 = y0 < 0 ? 0 : y0;
		local cy1 = y1 > vh ? vh : y1;
		local shade = 15 - (dist * 1.55).tointeger() + boost;
		shade = shade < 0 ? 0 : (shade > 15 ? 15 : shade);
		local sidx = ((kind * 2 + side) * 16 + shade) * 32 + tx;
		if (stripset[sidx] == 0) {
			strips[sidx] = ::build_strip(kind, side, shade, tx);
			stripset[sidx] = 1;
		}
		local strip = strips[sidx];
		local tstep = 65536 / lineh;            // (32 << 11) / lineh
		local tacc = (cy0 - y0) * tstep;
		local gi = cy0 * vw + px0;
		local y = cy0;
		if (pxw == 2) {
			while (y < cy1) {
				local c = strip[tacc >> 11];
				pix[gi] = c;
				pix[gi + 1] = c;
				tacc += tstep;
				gi += vw;
				y++;
			}
		} else {
			while (y < cy1) {
				pix[gi] = strip[tacc >> 11];
				tacc += tstep;
				gi += vw;
				y++;
			}
		}
	}
}

// -------------------------------------------------------------- the view ----
function render_view() {
	local g = ::G;
	local vw = g.vw;
	local vh = g.vh;
	local pang = ::SHOWCASE ? g.pang + g.feel.yaw : g.pang;
	local dirx = ::cos(pang);
	local diry = ::sin(pang);
	local planex = 0.0 - diry * 0.78;
	local planey = dirx * 0.78;
	local px = g.px;
	local py = g.py;
	local zbuf = g.zbuf;
	local pxw = ::PIXW;
	local pix = null;
	if (::SHOWCASE) {
		pix = g.pix;
		::showcase_view(px, py, dirx, diry, planex, planey);
	} else {
		pix = clone g.bg;
		::reference_view(pix, px, py, dirx, diry, planex, planey);
	}
	local horizon = g.horizon;

	// --- 3. billboards: enemies, pickups, projectiles (far to near) ---
	local det = planex * diry - dirx * planey;
	if (det < 0.0001 && det > -0.0001) { det = 0.0001; }
	local inv = 1.0 / det;
	local sp = [];
	foreach (e in g.enemies) {
		::collect_billboard(sp, e.x, e.y, ::enemy_art(e), ::ENEMY_DEFS[e.kind].size, 0.0,
			e.flash > 0 ? 1 : 0, inv, dirx, diry, planex, planey);
	}
	for (local i = 0; i < g.items.len(); ++i) {
		local it = g.items[i];
		if (it[3] == 1) { continue; }
		local ik = it[0];
		::collect_billboard(sp, it[1], it[2], ::ITEM_DEFS[ik].art,
			::ITEM_DEFS[ik].size, 0.0, 0, inv, dirx, diry, planex, planey);
	}
	local shot_art = ["", "pr_spit", "pr_fire", "pr_hex"];
	for (local i2 = 0; i2 < g.shots.len(); ++i2) {
		local s = g.shots[i2];
		::collect_billboard(sp, s[1], s[2], shot_art[s[0]], 0.16, 0.35, 0, inv, dirx, diry, planex, planey);
	}
	// insertion sort, far first (records: [depth, scx, name, size, zlift, flash, wx, wy])
	for (local i3 = 1; i3 < sp.len(); ++i3) {
		local rec = sp[i3];
		local j = i3 - 1;
		while (j >= 0) {
			local cmp = sp[j];
			if (cmp[0] >= rec[0]) { break; }
			sp[j + 1] = cmp;
			j--;
		}
		sp[j + 1] = rec;
	}
	foreach (rec2 in sp) { ::draw_billboard(rec2, pix, zbuf, horizon); }

	// --- 4. particles: pixel confetti with z-buffer clipping ---
	local pcols = ::PART_COLS;
	local half2 = horizon;
	local pxh = ::PIXH;
	foreach (prt in g.fx.pool) {
		local pk = prt[0];
		if (pk == 0) { continue; }
		local rx = prt[1] - px;
		local ry = prt[2] - py;
		local depth = inv * (planex * ry - planey * rx);
		if (depth < 0.15 || depth > 14.0) { continue; }
		local ttx = inv * (diry * rx - dirx * ry);
		local scx = ((vw / 2.0) * (1.0 + ttx / depth)).tointeger();
		if (scx < 0 || scx >= vw) { continue; }
		if (depth >= zbuf[scx]) { continue; }
		local span = 1.0 * vh / depth;
		local pyx = (half2 + span * 0.5 - prt[3] * span).tointeger();
		local life = prt[7];
		local phase = 3 - ::imin(3, life / 5);
		local color = pcols[pk * 4 + phase];
		local psz = (0.07 * span).tointeger() + 1;
		if (psz > 3) { psz = 3; }
		local psx = (psz * 2 * pxw) / pxh;           // square-ish regardless of mode
		if (psx < 1) { psx = 1; }
		for (local oy = 0; oy < psz; ++oy) {
			local yy = pyx + oy;
			if (yy < 0 || yy >= vh) { continue; }
			for (local ox = 0; ox < psx; ++ox) {
				local xx = scx + ox;
				if (xx >= vw) { continue; }
				if (depth < zbuf[xx]) { pix[yy * vw + xx] = color; }
			}
		}
	}

	// --- 5. the gun in hand and the look's overlays, the automap, the hurt flash ---
	if (::SHOWCASE) {
		::draw_viewmodel(pix);
		g.feel.overlay(vw, vh);
	}
	if (g.automap) { ::draw_automap(pix, g.tiles); }
	if (!::SHOWCASE && g.hurt_flash > 0) {
		local red = ::HURT_RED;
		local thick = pxw * 2;
		for (local x4 = 0; x4 < vw; ++x4) {
			pix[x4] = red;
			pix[vw + x4] = red;
			pix[(vh - 2) * vw + x4] = red;
			pix[(vh - 1) * vw + x4] = red;
		}
		for (local y4 = 0; y4 < vh; ++y4) {
			local rb = y4 * vw;
			for (local t4 = 0; t4 < thick; ++t4) {
				pix[rb + t4] = red;
				pix[rb + vw - 1 - t4] = red;
			}
		}
	}

	// --- 6. pixel grid -> sub-cell glyph rows -------------------------------
	if (::PIXH == 3) { return ::rows_sext(pix); }
	if (::PIXW == 2) { return ::rows_quad(pix); }
	return ::rows_half(pix);
}

// half mode: fg = top pixel, bg = bottom pixel, upper-half-block glyph
function rows_half(pix) {
	local g = ::G;
	local vw = g.vw;
	local fgs = ::PAL_FG;
	local bgs = ::PAL_BG;
	local hb = ::HALFG;
	local rows = [];
	local vrows = g.vh / 2;
	for (local ry = 0; ry < vrows; ++ry) {
		local parts = [];
		local ptop = -1;
		local pbot = -1;
		local itop = ry * 2 * vw;
		local ibot = itop + vw;
		for (local x = 0; x < vw; ++x) {
			local tc = pix[itop + x];
			local bc = pix[ibot + x];
			if (tc != ptop) { parts.push(fgs[tc]); ptop = tc; }
			if (bc != pbot) { parts.push(bgs[bc]); pbot = bc; }
			parts.push(hb);
		}
		parts.push(::RESET);
		rows.push(::join_arr(parts, ""));
	}
	return rows;
}

// quad mode: 2x2 pixels per cell, two colors by luminosity partition
function rows_quad(pix) {
	local g = ::G;
	local vw = g.vw;
	local fgs = ::PAL_FG;
	local bgs = ::PAL_BG;
	local lums = ::PAL_LUM;
	local qg = ::QUADG;
	local rows = [];
	local vrows = g.vh / 2;
	local cells = vw / 2;
	for (local ry = 0; ry < vrows; ++ry) {
		local parts = [];
		local pfg = -1;
		local pbg = -1;
		local itop = ry * 2 * vw;
		local ibot = itop + vw;
		for (local cx = 0; cx < cells; ++cx) {
			local xx = cx * 2;
			local p0 = pix[itop + xx];
			local p1 = pix[itop + xx + 1];
			local p2 = pix[ibot + xx];
			local p3 = pix[ibot + xx + 1];
			if (p0 == p1 && p0 == p2 && p0 == p3) {
				if (p0 != pbg) { parts.push(bgs[p0]); pbg = p0; }
				parts.push(" ");
				continue;
			}
			// wall slices split cells into vertical or horizontal halves far
			// more often than not - cheap two-color paths before the full
			// luminosity partition
			if (p0 == p2 && p1 == p3) {
				if (p0 != pfg) { parts.push(fgs[p0]); pfg = p0; }
				if (p1 != pbg) { parts.push(bgs[p1]); pbg = p1; }
				parts.push(qg[5]);                 // left half
				continue;
			}
			if (p0 == p1 && p2 == p3) {
				if (p0 != pfg) { parts.push(fgs[p0]); pfg = p0; }
				if (p2 != pbg) { parts.push(bgs[p2]); pbg = p2; }
				parts.push(qg[3]);                 // upper half
				continue;
			}
			local l0 = lums[p0];
			local l1 = lums[p1];
			local l2 = lums[p2];
			local l3 = lums[p3];
			local lmin = l0;
			local lmax = l0;
			local fgp = p0;
			local bgp = p0;
			if (l1 < lmin) { lmin = l1; bgp = p1; }
			if (l1 > lmax) { lmax = l1; fgp = p1; }
			if (l2 < lmin) { lmin = l2; bgp = p2; }
			if (l2 > lmax) { lmax = l2; fgp = p2; }
			if (l3 < lmin) { lmin = l3; bgp = p3; }
			if (l3 > lmax) { lmax = l3; fgp = p3; }
			local thr = (lmin + lmax + 1) / 2;
			local bits = 0;
			if (l0 >= thr) { bits = 1; }
			if (l1 >= thr) { bits += 2; }
			if (l2 >= thr) { bits += 4; }
			if (l3 >= thr) { bits += 8; }
			if (fgp != pfg) { parts.push(fgs[fgp]); pfg = fgp; }
			if (bits != 15 && bgp != pbg) { parts.push(bgs[bgp]); pbg = bgp; }
			parts.push(qg[bits]);
		}
		parts.push(::RESET);
		rows.push(::join_arr(parts, ""));
	}
	return rows;
}

// sext mode: 2x3 pixels per cell (Unicode 13 "Symbols for Legacy Computing")
function rows_sext(pix) {
	local g = ::G;
	local vw = g.vw;
	local fgs = ::PAL_FG;
	local bgs = ::PAL_BG;
	local lums = ::PAL_LUM;
	local sg = ::SEXTG;
	local rows = [];
	local vrows = g.vh / 3;
	local cells = vw / 2;
	for (local ry = 0; ry < vrows; ++ry) {
		local parts = [];
		local pfg = -1;
		local pbg = -1;
		local r0 = ry * 3 * vw;
		local r1 = r0 + vw;
		local r2 = r1 + vw;
		for (local cx = 0; cx < cells; ++cx) {
			local xx = cx * 2;
			local p0 = pix[r0 + xx];
			local p1 = pix[r0 + xx + 1];
			local p2 = pix[r1 + xx];
			local p3 = pix[r1 + xx + 1];
			local p4 = pix[r2 + xx];
			local p5 = pix[r2 + xx + 1];
			if (p0 == p1 && p0 == p2 && p0 == p3 && p0 == p4 && p0 == p5) {
				if (p0 != pbg) { parts.push(bgs[p0]); pbg = p0; }
				parts.push(" ");
				continue;
			}
			local l0 = lums[p0];
			local l1 = lums[p1];
			local l2 = lums[p2];
			local l3 = lums[p3];
			local l4 = lums[p4];
			local l5 = lums[p5];
			local lmin = l0;
			local lmax = l0;
			local fgp = p0;
			local bgp = p0;
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
			local thr = (lmin + lmax + 1) / 2;
			local bits = 0;
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
		parts.push(::RESET);
		rows.push(::join_arr(parts, ""));
	}
	return rows;
}

// world -> camera -> screen; appends [depth, scx, art, size, zlift, flash, wx, wy]
function collect_billboard(sp, wx, wy, art, size, zlift, flash, inv, dirx, diry, planex, planey) {
	local g = ::G;
	local rx = wx - g.px;
	local ry = wy - g.py;
	local depth = inv * (planex * ry - planey * rx);
	if (depth < 0.18 || depth > 15.0) { return; }
	local ttx = inv * (diry * rx - dirx * ry);
	local scx = ((g.vw / 2.0) * (1.0 + ttx / depth)).tointeger();
	if (scx < -g.vw || scx > g.vw * 2) { return; }
	sp.push([depth, scx, art, size, zlift, flash, wx, wy]);
}

function draw_billboard(rec, pix, zbuf, horizon) {
	local g = ::G;
	local depth = rec[0];
	local scx = rec[1];
	local name = rec[2];
	local size = rec[3];
	local zlift = rec[4];
	local flash = rec[5];
	local art = ::ARTS[name];
	local aw = art.w;
	local ah = art.h;
	local apix = art.pix;
	local vw = g.vw;
	local vh = g.vh;
	local span = 1.0 * vh / depth;
	local ph = (span * size).tointeger();
	if (ph < 2) { ph = 2; }
	// pixel aspect: a cell is ~1:2 (w:h) split into PIXW x PIXH
	local pw = ph * aw * 2 * ::PIXW / (ah * ::PIXH);
	if (pw < 2) { pw = 2; }
	local band = (depth * 0.85).tointeger() - (g.muzzle > 0 ? 1 : 0);
	band = band < 0 ? 0 : (band > 7 ? 7 : band);
	local lut = ::sprite_lut(name, band, flash, ::SHOWCASE ? g.lights.at(rec[6], rec[7]) : 0);
	local bottom = (horizon + span * 0.5 - zlift * span).tointeger();
	local top = bottom - ph;
	local x0 = scx - pw / 2;
	local x1 = x0 + pw;
	local cx0 = x0 < 0 ? 0 : x0;
	local cx1 = x1 > vw ? vw : x1;
	local cy0 = top < 0 ? 0 : top;
	local cy1 = bottom > vh ? vh : bottom;
	for (local x = cx0; x < cx1; ++x) {
		if (depth >= zbuf[x]) { continue; }
		local ax = (x - x0) * aw / pw;
		for (local y = cy0; y < cy1; ++y) {
			local ay = (y - top) * ah / ph;
			local code = apix[ay * aw + ax];
			if (code > 0) { pix[y * vw + x] = lut[code]; }
		}
	}
}

// arcade automap: square tiles, walls in kind colors, live blips
function draw_automap(pix, tiles) {
	local g = ::G;
	local vw = g.vw;
	local mw = g.mw;
	local mh = g.mh;
	local sx = ::PIXW * 2;
	local sy = ::PIXH;
	local ox = sx * 2;
	local oy = sy * 2;
	local kc = ::AM_COLS;
	for (local ty = 0; ty < mh; ++ty) {
		for (local tx = 0; tx < mw; ++tx) {
			local t = tiles[ty * mw + tx];
			local c = t == 0 ? ::AM_FLOOR : kc[t];
			if (t == 6 && (g.tick & 8) == 0) { c = ::AM_BLINK; }
			local bx = ox + tx * sx;
			local by = oy + ty * sy;
			for (local yy = 0; yy < sy; ++yy) {
				local rb = (by + yy) * vw + bx;
				for (local xx = 0; xx < sx; ++xx) { pix[rb + xx] = c; }
			}
		}
	}
	// blips: items gold, live enemies red, you white (+ heading pixel)
	for (local i = 0; i < g.items.len(); ++i) {
		local it = g.items[i];
		if (it[3] == 1) { continue; }
		::blip(pix, ox + it[1].tointeger() * sx, oy + it[2].tointeger() * sy, ::AM_ITEM);
	}
	foreach (e in g.enemies) {
		if (!e.alive) { continue; }
		::blip(pix, ox + e.x.tointeger() * sx, oy + e.y.tointeger() * sy, ::AM_ENEMY);
	}
	::blip(pix, ox + g.px.tointeger() * sx, oy + g.py.tointeger() * sy, ::AM_SELF);
	::blip(pix, ox + (g.px + ::cos(g.pang) * 1.4).tointeger() * sx, oy + (g.py + ::sin(g.pang) * 1.4).tointeger() * sy, ::AM_HEAD);
}

function blip(pix, bx, by, c) {
	local g = ::G;
	local vw = g.vw;
	local vh = g.vh;
	if (bx < 0 || by < 0 || bx + 1 >= vw || by + 1 >= vh) { return; }
	pix[by * vw + bx] = c;
	pix[by * vw + bx + 1] = c;
	pix[(by + 1) * vw + bx] = c;
	pix[(by + 1) * vw + bx + 1] = c;
}

// ------------------------------------------------------- the showcase look ----
// One ray per pixel column, then each column top to bottom: textured ceiling,
// the lit wall slice, textured floor with its stains. Every surface reads its
// light texel and a lit table (look.nut builds the tables, lights.nut the light).

::LWALL <- [];        // [kind][variant][side][shade][tone]
::LFLOOR <- [];       // [variant][shade][tone]
::LCEIL <- [];
::WALLTEX <- [];      // wall tones [far][kind][ty][tx]
::FLOORTEX <- [];     // floor tones [far][variant][v][u]
::CEILTEX <- [];
::FLOOR_VAR <- [];    // per tile: which floor variant, times 256
::CEIL_VAR <- [];     // per tile: 256 under a lamp
::STAINS <- [];       // per tile 8x8 decal tones, 0 none
::ROWD <- [];         // per pixel row: floor or ceiling distance
::ROWS_SHADE <- [];
::ROWS_FAR <- [];     // per pixel row: 1 past FAR_TEXTURE

// the showcase look's lit tables, textures and lamps for floor mi
function build_showcase_tables(mi) {
	local g = ::G;
	::LWALL = ::showcase_lit_wall_tones(mi);
	::LFLOOR = ::showcase_floor_tones(mi);
	::LCEIL = ::showcase_ceiling_tones(mi);
	::WALLTEX = ::showcase_wall_texture();
	::FLOORTEX = ::showcase_floor_texture(mi);
	::CEILTEX = ::showcase_ceiling_texture();
	g.lights.load();
	local lamps = g.lights.lamps;
	local mw = g.mw;
	local fvar = [];
	local cvar = [];
	for (local ty = 0; ty < g.mh; ++ty) {
		for (local tx = 0; tx < mw; ++tx) {
			fvar.push((::texel_noise(31, tx, ty) & 3) * 256);
			cvar.push(lamps[ty * mw + tx] ? 256 : 0);
		}
	}
	::FLOOR_VAR = fvar;
	::CEIL_VAR = cvar;
	::STAINS = ::array(mw * g.mh * 64, 0);
	::ROWD = ::array(g.vh, 0.0);
	::ROWS_SHADE = ::array(g.vh, 0);
	::ROWS_FAR = ::array(g.vh, 0);
}

// a decal on the floor: tone over the texels within radius of (x, y), each kept with chance keep/256
function stain_floor(x, y, radius, tone, keep) {
	local g = ::G;
	local mw = g.mw;
	local tiles = g.tiles;
	local stains = ::STAINS;
	local x0 = ((x - radius) * 8.0).tointeger();
	local x1 = ((x + radius) * 8.0).tointeger();
	local y0 = ((y - radius) * 8.0).tointeger();
	local y1 = ((y + radius) * 8.0).tointeger();
	local r2 = radius * radius * 64.0;
	local sy_end = ::imin(g.mh * 8 - 1, y1);
	local sx_end = ::imin(mw * 8 - 1, x1);
	for (local sy = ::imax(0, y0); sy <= sy_end; ++sy) {
		for (local sx = ::imax(0, x0); sx <= sx_end; ++sx) {
			local dx = sx + 0.5 - x * 8.0;
			local dy = sy + 0.5 - y * 8.0;
			if (dx * dx + dy * dy > r2) { continue; }
			if ((::texel_noise(41, sx, sy) & 255) >= keep) { continue; }
			local tile = (sy >> 3) * mw + (sx >> 3);
			if (tiles[tile] != 0) { continue; }
			local i = tile * 64 + (sy & 7) * 8 + (sx & 7);
			local old = stains[i];
			if (tone == ::TONE_BLOOD && old == ::TONE_SCORCH) { continue; }
			stains[i] = tone;
		}
	}
}

// old blood dries: one pass a second darkens a slice of the stains
function dry_stains(tick) {
	local stains = ::STAINS;
	local n = stains.len();
	local slice = n / 30 + 1;
	local start = (tick % 30) * slice;
	local stop = ::imin(n, start + slice);
	for (local i = start; i < stop; ++i) {
		if (stains[i] == ::TONE_BLOOD && (::texel_noise(43, i, tick / 30) & 7) == 0) { stains[i] = ::TONE_BLOOD_DRY; }
	}
}

function showcase_view(px, py, dirx, diry, planex, planey) {
	local g = ::G;
	local vw = g.vw;
	local vh = g.vh;
	local horizon = vh / 2 + g.feel.pitch;
	g.horizon = horizon;
	local lights = g.lights;
	lights.frame(g.feel.sources);

	// the floor or ceiling distance and shade of every pixel row
	local hz = horizon;
	local rowd = ::ROWD;
	local rows_shade = ::ROWS_SHADE;
	local rows_far = ::ROWS_FAR;
	local far_texture = ::FAR_TEXTURE;
	for (local y = 0; y < vh; ++y) {
		local k = y + 0.5 - hz;
		if (k < 0.0) { k = 0.0 - k; }
		local d = k > 0.3 ? vh / (2.0 * k) : 60.0;
		rowd[y] = d;
		local shade = 15 - (d * 1.55).tointeger();
		rows_shade[y] = shade < 0 ? 0 : shade;
		rows_far[y] = d > far_texture ? 1 : 0;
	}

	local pix = g.pix;
	local zb = g.zbuf;
	local lg = lights.grid;
	local gw = lights.gw;
	local mw = g.mw;
	local tiles = g.tiles;
	local t2k = ::TILE_TO_KIND;
	local walls = ::LWALL;
	local floors = ::LFLOOR;
	local ceilings = ::LCEIL;
	local wtex = ::WALLTEX;
	local ftex = ::FLOORTEX;
	local ctex = ::CEILTEX;
	local fvar = ::FLOOR_VAR;
	local cvar = ::CEIL_VAR;
	local stain = ::STAINS;
	local stride = ::WALL_TABLE_STRIDE;
	local floor_tones = ::FLOOR_TONES;
	local xstep = 2.0 / (vw - 1.0);
	for (local x = 0; x < vw; ++x) {
		local camx = x * xstep - 1.0;
		local rdx = dirx + planex * camx;
		local rdy = diry + planey * camx;

		// the wall ray: the reference's chunk DDA over the tile kinds
		local mx = px.tointeger();
		local my = py.tointeger();
		local adx = rdx < 0.0 ? -rdx : rdx;
		local ady = rdy < 0.0 ? -rdy : rdy;
		local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		local stepx = rdx < 0.0 ? -1 : 1;
		local stepy = rdy < 0.0 ? -1 : 1;
		local sdx = rdx < 0.0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
		local sdy = rdy < 0.0 ? (py - my) * ddy : (my + 1.0 - py) * ddy;
		local side = 0;
		local kind = 1;
		local guard = 0;
		local hit = false;
		while (!hit && guard < 128) {
			if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
			else { sdy += ddy; my += stepy; side = 1; }
			local c = t2k[tiles[my * mw + mx]];
			if (c != 0) { hit = true; kind = c; }
			guard++;
		}
		local dist = side == 0 ? sdx - ddx : sdy - ddy;
		dist = dist < 0.02 ? 0.02 : dist;
		local wallx = side == 0 ? py + dist * rdy : px + dist * rdx;
		wallx = wallx - ::floor(wallx);
		local tx = (wallx * 64.0).tointeger() >> 1;
		zb[x] = dist;

		// the wall's rows are those whose centers lie within half of the horizon,
		// so every ceiling and floor pixel is strictly nearer than the wall
		local half = 0.5 * vh / dist;
		local ytop = hz - half;
		local y0 = 0 - ::floor(0.5 - ytop).tointeger();
		local y1 = ::floor(hz - 0.5 + half).tointeger() + 1;
		local cy0 = y0 < 0 ? 0 : (y0 > vh ? vh : y0);
		local cy1 = y1 > vh ? vh : (y1 < cy0 ? cy0 : y1);

		// ceiling
		local gi = x;
		for (local y = 0; y < cy0; ++y) {
			local d = rowd[y];
			local fx = px + rdx * d;
			local fy = py + rdy * d;
			local ix = fx.tointeger();
			local iy = fy.tointeger();
			local u = ((fx - ix) * 16.0).tointeger();
			local v = ((fy - iy) * 16.0).tointeger();
			local lx = (fx * 4.0).tointeger();
			local ly = (fy * 4.0).tointeger();
			local tone = ctex[rows_far[y] * 512 + cvar[iy * mw + ix] + v * 16 + u];
			pix[gi] = ceilings[(lg[ly * gw + lx] * 16 + rows_shade[y]) * 8 + tone];
			gi += vw;
		}

		// wall
		local shade = 15 - (dist * 1.55).tointeger();
		shade = shade < 0 ? 0 : (shade > 15 ? 15 : shade);
		local back = dist - 0.03;
		local wlx = ((px + rdx * back) * 4.0).tointeger();
		local wly = ((py + rdy * back) * 4.0).tointeger();
		local light = lg[wly * gw + wlx];
		local wbase = kind * stride + ((light * 2 + side) * 16 + shade) * 8;
		local tbase = (dist > far_texture ? 9216 : 0) + kind * 1024 + tx;
		local tscale = 65536.0 / (2.0 * half);
		local tstep = tscale.tointeger();
		local tacc = ((cy0 + 0.5 - ytop) * tscale).tointeger();
		gi = cy0 * vw + x;
		for (local y = cy0; y < cy1; ++y) {
			pix[gi] = walls[wbase + wtex[tbase + ((tacc >> 11) & 31) * 32]];
			tacc += tstep;
			gi += vw;
		}

		// floor
		for (local y = cy1; y < vh; ++y) {
			local d = rowd[y];
			local fx = px + rdx * d;
			local fy = py + rdy * d;
			local ix = fx.tointeger();
			local iy = fy.tointeger();
			local u = ((fx - ix) * 16.0).tointeger();
			local v = ((fy - iy) * 16.0).tointeger();
			local lx = (fx * 4.0).tointeger();
			local ly = (fy * 4.0).tointeger();
			local ti = iy * mw + ix;
			local tone = stain[ti * 64 + (v >> 1) * 8 + (u >> 1)];
			if (tone == 0) { tone = ftex[rows_far[y] * 1024 + fvar[ti] + v * 16 + u]; }
			pix[gi] = floors[(lg[ly * gw + lx] * 16 + rows_shade[y]) * floor_tones + tone];
			gi += vw;
		}
	}
}

// the showcase look's gun in hand: sways with the walk, kicks on each shot
function draw_viewmodel(pix) {
	local g = ::G;
	if (g.automap) { return; }
	local firing = g.cooldown > ::WEAPONS[g.weapon].cd - 4 || g.gun_kick > 0;
	local name = ::VIEWMODEL_NAMES[g.weapon];
	if (firing && g.muzzle > 0) { name = name + "_fire"; }
	local art = ::VIEWMODELS[name];
	local lut = ::viewmodel_lut(name, g.muzzle > 0, g.lights.at(g.px, g.py));
	local vw = g.vw;
	local vh = g.vh;
	local aw = art.w;
	local ah = art.h;
	local apix = art.pix;
	// art pixels are square: a view pixel is PIXW x PIXH of a 1:2 cell
	local scale = vh * 0.4 / 24.0;
	local ph = (ah * scale).tointeger();
	local pw = ph * aw * 2 * ::PIXW / (ah * ::PIXH);
	local sway = (::sin(g.bob * 0.5) * vw * 0.025).tointeger();
	local lift = (::fabs(::cos(g.bob * 0.5)) * scale * 1.5).tointeger() + g.gun_kick * (scale * 0.9).tointeger();
	local x0 = vw / 2 - pw / 2 + sway;
	local y0 = vh - ph + lift + (g.horizon - vh / 2) / 2;
	local cx0 = ::imax(0, x0);
	local cx1 = ::imin(vw, x0 + pw);
	local cy1 = ::imin(vh, y0 + ph);
	for (local y = ::imax(0, y0); y < cy1; ++y) {
		local row = ((y - y0) * ah / ph) * aw;
		local rb = y * vw;
		for (local x = cx0; x < cx1; ++x) {
			local code = apix[row + (x - x0) * aw / pw];
			if (code > 0) { pix[rb + x] = lut[code]; }
		}
	}
}

// palette code -> index for a gun under this floor's light, brighter in its own flash
function viewmodel_lut(name, flashing, variant) {
	local g = ::G;
	local key = name + ":" + flashing + ":" + variant;
	if (key in g.lutc) { return g.lutc[key]; }
	local mood = ::MOODS[g.map_i];
	local art = ::VIEWMODELS[name];
	local light = ::cadd(::cscale(::mix_rgb(mood.light, ::rgb(255, 255, 255), 0.4), flashing ? 1.25 : 0.7),
		::cscale(::sprite_light(mood, variant), 0.8));
	local lut = [0];
	local rgbs = art.rgb;
	local keys = art.keys;
	for (local c = 1; c < rgbs.len(); ++c) {
		local col = ::rgb(rgbs[c][0], rgbs[c][1], rgbs[c][2]);
		local key_char = keys[c];
		if (key_char >= 'A' && key_char <= 'Z') { lut.push(::color_idx(col)); }
		else { lut.push(::color_idx(::cclamp01(::cmul(col, light)))); }
	}
	g.lutc[key] <- lut;
	return lut;
}
