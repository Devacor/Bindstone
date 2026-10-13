// LAVALAMP (Squirrel port of scenes/lava.jai): lit metaballs with blended hues
// and a glow falloff. parallel_transform rows run serially.

::LV_BX <- [];
::LV_BY <- [];
::LV_R2 <- [];
::LV_HUE <- [];
::LV_ROWS <- [];
::LV_W <- 0;
::LV_GLOW <- 0;
::LV_RR <- 1.0;

function lava_row(y) {
	local out = [];
	local ww = ::LV_W;
	local bxs = ::LV_BX, bys = ::LV_BY, r2s = ::LV_R2, hues = ::LV_HUE;
	local glow = ::LV_GLOW;
	local sqrt = ::sqrt;
	local n = bxs.len();
	local rr = ::LV_RR;
	local lx = -0.45;
	local ly = -0.55;
	local lz = 0.70;
	local hl = 1.0 / sqrt(lx * lx + ly * ly + (lz + 1.0) * (lz + 1.0));
	local hx = lx * hl;
	local hy = ly * hl;
	local hz = (lz + 1.0) * hl;
	local fy = y + 0.5;
	for (local x = 0; x < ww; ++x) {
		local fx = x + 0.5;
		local f = 0.0;
		local gx = 0.0;
		local gy = 0.0;
		local hsum = 0.0;
		for (local b = 0; b < n; ++b) {
			local dx = fx - bxs[b];
			local dy = fy - bys[b];
			local d2 = dx * dx + dy * dy + 1.0;
			local inv = r2s[b] / d2;
			f += inv;
			local q = inv / d2;
			gx += q * dx;
			gy += q * dy;
			hsum += inv * hues[b];
		}
		local hq = (hsum / f + 0.5).tointeger();
		if (f >= 1.0) {
			local rim = 1.0 - 1.0 / f;
			rim = rim < 0.004 ? 0.004 : rim;
			local kz = rr / (f * f * sqrt(rim));
			local nx = gx * kz;
			local ny = gy * kz;
			local il = 1.0 / sqrt(nx * nx + ny * ny + 1.0);
			nx *= il; ny *= il;
			local d = nx * lx + ny * ly + il * lz;
			d = d < 0.0 ? 0.0 : d;
			local s = nx * hx + ny * hy + il * hz;
			local s2 = s * s;
			local s4 = s2 * s2;
			local s8 = s4 * s4;
			local s16 = s8 * s8;
			local lv = ((d * 0.78 + s16 * s16 * 0.42) * 15.99).tointeger();
			out.push(hq * 16 + (lv > 15 ? 15 : lv));
		} else {
			local gl = (f * f * 7.99).tointeger();
			out.push(glow + hq * 8 + gl);
		}
	}
	return out;
}

class Lava extends Scene {
	bx = null;
	by = null;
	br2 = null;
	hue = null;
	fa = null;
	fb = null;
	pa = null;
	pb = null;
	nb = 8;
	glow0 = 0;
	tt = 0.0;

	constructor() {
		bx = [];
		by = [];
		br2 = [];
		hue = [];
		fa = [];
		fb = [];
		pa = [];
		pb = [];
	}

	function build() {
		name = "LAVALAMP";
		handle = "wax and gradient . by operator overload";
		for (local hq = 0; hq < 24; ++hq) {
			local c = ::gfx.rainbow_rgb(hq * 0.22 + 4.4);
			local cr = c[0], cg = c[1], cb = c[2];
			for (local s = 0; s < 16; ++s) {
				local f = s / 15.0;
				local lit = f < 0.75 ? 0.10 + f * 1.2 : 1.0;
				local wh = f < 0.75 ? 0.0 : (f - 0.75) * 3.6;
				wh = wh > 0.9 ? 0.9 : wh;
				local r = cr * lit;
				local g = cg * lit;
				local b = cb * lit;
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh);
			}
		}
		glow0 = pal.size();
		for (local hq2 = 0; hq2 < 24; ++hq2) {
			local c2 = ::gfx.rainbow_rgb(hq2 * 0.22 + 4.4);
			for (local g2 = 0; g2 < 8; ++g2) {
				local k = g2 / 7.0;
				k = k * k * 0.55;
				pal.add(10.0 + c2[0] * k, 4.0 + c2[1] * k, 18.0 + c2[2] * k);
			}
		}
		for (local i = 0; i < nb; ++i) {
			local r = h * (0.075 + rng.next(50) * 0.001);
			br2.push(r * r);
			hue.push(i * 23.0 / (nb - 1.0));
			fa.push(0.13 + rng.next(100) * 0.003);
			fb.push(0.17 + rng.next(100) * 0.004);
			pa.push(rng.next(628) * 0.01);
			pb.push(rng.next(628) * 0.01);
			bx.push(0.0);
			by.push(0.0);
		}
	}

	function update(t, dt) {
		tt = t;
		for (local i = 0; i < nb; ++i) {
			bx[i] = w * (0.5 + 0.38 * ::sin(t * fa[i] * 2.0 + pa[i]));
			by[i] = h * (0.5 + 0.42 * ::sin(t * fb[i] * 2.0 + pb[i]));
		}
	}

	function render(grid) {
		::LV_BX = bx;
		::LV_BY = by;
		::LV_R2 = br2;
		::LV_HUE = hue;
		::LV_W = w;
		::LV_RR = h * 0.1;
		::LV_GLOW = glow0;
		if (::LV_ROWS.len() != h) {
			::LV_ROWS = [];
			for (local r = 0; r < h; ++r) { ::LV_ROWS.push(r); }
		}
		local rows = [];
		foreach (r in ::LV_ROWS) { rows.push(::lava_row(r)); }
		local idx = 0;
		for (local y = 0; y < h; ++y) {
			local row = rows[y];
			for (local x = 0; x < w; ++x) {
				grid[idx] = row[x];
				idx = idx + 1;
			}
		}
	}
}
