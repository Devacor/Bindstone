// MAELSTROM (Squirrel port of scenes/fluid.jai): neon ink in a slab of black water, simulated twice
// over and seen in three dimensions. Jos Stam's stable fluids on a flow grid a third of the pixel
// resolution (forces, vorticity confinement, warm-started SOR projection, semi-Lagrangian advection);
// the ink rides the same flow on a grid twice as fine and picks a place along eight neon tubes. The
// surface is a height field obeying the wave equation, struck by drops, dished by the vortices and the
// drain; each screen column casts one ray across the water front to back, over a neon grid floor.

// globals on purpose: the parallel row and column bodies read them
::MS_FU <- [];
::MS_FV <- [];
::MS_INK <- [];
::MS_HOME_X <- [];
::MS_HOME_Y <- [];
::MS_SETTLE <- [];
::MS_RADIUS <- [];
::MS_Z <- [];
::MS_LIT <- [];
::MS_GLOW <- [];
::MS_DUSK <- [];
::MS_INK_ROWS <- [];
::MS_COLS <- [];
::MS_W <- 0;
::MS_H <- 0;
::MS_DW <- 0;
::MS_DH <- 0;
::MS_CW <- 0;
::MS_CH <- 0;
::MS_RUN <- 0.0;
::MS_CX <- 0.0;
::MS_CY <- 0.0;
::MS_CAM_X <- 0.0;
::MS_CAM_Y <- 0.0;
::MS_CAM_H <- 1.0;
::MS_FACE_X <- 1.0;
::MS_FACE_Y <- 0.0;
::MS_FOCAL <- 1.0;
::MS_HORIZON <- 0.0;
::MS_DEPTH <- 1.0;
::MS_LIGHT_X <- 0.0;
::MS_LIGHT_Y <- 0.0;
::MS_GLINT_X <- 0.0;
::MS_GLINT_Y <- 0.0;
::MS_GLINT_NARROW <- 1.0;
::MS_GLINT <- 0.0;
::MS_CREST <- 0.0;
::MS_TUBES <- 0;
::MS_PHASE <- 0;
::MS_FLOOR <- 0;
::MS_GRID <- 1.0;
::MS_GRID_GAIN <- 0.0;
::MS_GRID_FADE <- 0.0;

// one row of ink: each cell takes the flow at its centre (bilinear over the flow grid, stepped along
// the row), traces back one step, blends the four cells it lands among and settles toward home
function maelstrom_ink_row(y) {
	local dw = ::MS_DW;
	local out = ::array(dw, 0.0);
	if (y < 1 || y >= ::MS_DH - 1) { return out; }
	local fu = ::MS_FU;
	local fv = ::MS_FV;
	local inkg = ::MS_INK;
	local home_x = ::MS_HOME_X;
	local settle = ::MS_SETTLE;
	local radius = ::MS_RADIUS;
	local gw = ::MS_CW;
	local gymax = ::MS_CH - 1.001;
	local xmax = dw - 2.001;
	local ymax = ::MS_DH - 2.001;
	local run = ::MS_RUN;
	local home_y = ::MS_HOME_Y[y];
	local qy = y * 0.5 - 0.25;
	qy = qy > gymax ? gymax : qy;
	local gy = qy.tointeger();
	local ty = qy - gy;
	local g = gy * gw;
	local ua = fu[g] + (fu[g + gw] - fu[g]) * ty;
	local va = fv[g] + (fv[g + gw] - fv[g]) * ty;
	local x = 1;
	local radius_at = y * dw;
	for (local gx = 1; gx < gw; ++gx) {
		++g;
		local ub = fu[g] + (fu[g + gw] - fu[g]) * ty;
		local vb = fv[g] + (fv[g + gw] - fv[g]) * ty;
		local du = (ub - ua) * 0.5;
		local dv = (vb - va) * 0.5;
		local u = ua + du * 0.5;
		local v = va + dv * 0.5;
		local xe = x + 2;
		while (x < xe) {
			local px = x - run * u;
			local py = y - run * v;
			px = px < 1.0 ? 1.0 : (px > xmax ? xmax : px);
			py = py < 1.0 ? 1.0 : (py > ymax ? ymax : py);
			local ix = px.tointeger();
			local iy = py.tointeger();
			local sx = px - ix;
			local k = iy * dw + ix;
			local k2 = k + dw;
			local a = inkg[k];
			local b = inkg[k2];
			local top = a + (inkg[k + 1] - a) * sx;
			local bottom = b + (inkg[k2 + 1] - b) * sx;
			local ink = top + (bottom - top) * (py - iy);
			out[x] = ink + (home_y + home_x[x] - ink) * settle[radius[radius_at + x]];
			u += du;
			v += dv;
			++x;
		}
		ua = ub;
		va = vb;
	}
	return out;
}

// one row of the surface's light: each cell's level from its slope (the key light's share, and the
// glint) and its height, at the glow of its radius
function maelstrom_light_row(y) {
	local dw = ::MS_DW;
	local out = ::array(dw, 0);
	local z = ::MS_Z;
	local radii = ::MS_RADIUS;
	local glow = ::MS_GLOW;
	local dusk = ::MS_DUSK;
	local row = y < 1 ? 1 : (y > ::MS_DH - 2 ? ::MS_DH - 2 : y);
	local light_x = ::MS_LIGHT_X;
	local light_y = ::MS_LIGHT_Y;
	local glint_x = ::MS_GLINT_X;
	local glint_y = ::MS_GLINT_Y;
	local narrow = ::MS_GLINT_NARROW;
	local glint = ::MS_GLINT;
	local crest = ::MS_CREST;
	local k = row * dw + 1;
	for (local x = 1; x < dw - 1; ++x) {
		local sx = z[k + 1] - z[k - 1];
		local sy = z[k + dw] - z[k - dw];
		local radius = radii[k];
		local lift = z[k] * crest;
		lift = lift < -2.5 ? -2.5 : (lift > 3.0 ? 3.0 : lift);
		local level = glow[radius] + lift - sx * light_x - sy * light_y;
		local ex = sx - glint_x;
		local ey = sy - glint_y;
		local near = 1.0 - (ex * ex + ey * ey) * narrow;
		if (near > 0.0) { level += near * near * glint; }
		if (level < 2.0) { level = 2.0; }
		local whole = (level * dusk[radius]).tointeger();
		if (whole > 15) { whole = 15; }
		out[x] = whole * 128;
		++k;
	}
	out[0] = out[1];
	out[dw - 1] = out[dw - 2];
	return out;
}

// one column of the picture: its ray across the ground is clipped to the tank; where it enters, the
// slab's cut face hangs from the waterline; then the ray walks the surface front to back, a step a
// row, and each sample that rises above everything nearer fills the rows it adds. The rows nothing
// covered show the floor and its grid.
function maelstrom_column(x) {
	local hh = ::MS_H;
	local dw = ::MS_DW;
	local tubes = ::MS_TUBES;
	local phase = ::MS_PHASE;
	local floor = ::MS_FLOOR;
	local zg = ::MS_Z;
	local inkg = ::MS_INK;
	local litg = ::MS_LIT;
	local out = ::array(hh, floor);
	local focal = ::MS_FOCAL;
	local camx = ::MS_CAM_X;
	local camy = ::MS_CAM_Y;
	local camh = ::MS_CAM_H;
	local hz = ::MS_HORIZON;
	local aside = (x + 0.5 - ::MS_W * 0.5) / focal;
	local dx = ::MS_FACE_X - aside * ::MS_FACE_Y;
	local dy = ::MS_FACE_Y + aside * ::MS_FACE_X;
	local xhi = dw - 2.01;
	local yhi = ::MS_DH - 2.01;
	local t0 = 1.0;
	local t1 = 1000000.0;
	local face = 7.0;
	local m = 0.0;
	// math::max(a, b) is a >= b ? a : b, math::min(a, b) is a <= b ? a : b
	if (dx > 0.00001) {
		m = (1.0 - camx) / dx;
		t0 = t0 >= m ? t0 : m;
		m = (xhi - camx) / dx;
		t1 = t1 <= m ? t1 : m;
	} else if (dx < -0.00001) {
		m = (xhi - camx) / dx;
		t0 = t0 >= m ? t0 : m;
		m = (1.0 - camx) / dx;
		t1 = t1 <= m ? t1 : m;
	} else if (camx < 1.0 || camx > xhi) {
		t1 = 0.0;
	}
	local across0 = 1.0;
	if (dy > 0.00001) {
		across0 = (1.0 - camy) / dy;
		m = (yhi - camy) / dy;
		t1 = t1 <= m ? t1 : m;
	} else if (dy < -0.00001) {
		across0 = (yhi - camy) / dy;
		m = (1.0 - camy) / dy;
		t1 = t1 <= m ? t1 : m;
	} else if (camy < 1.0 || camy > yhi) {
		t1 = 0.0;
	}
	if (across0 > t0) {
		t0 = across0;
		face = 5.0;
	}
	local ytop = hh;
	local ybase = hh;
	if (t1 > t0) {
		local px0 = camx + t0 * dx;
		local py0 = camy + t0 * dy;
		px0 = px0 < 1.0 ? 1.0 : (px0 > xhi ? xhi : px0);
		py0 = py0 < 1.0 ? 1.0 : (py0 > yhi ? yhi : py0);
		local ix0 = px0.tointeger();
		local iy0 = py0.tointeger();
		local k0 = iy0 * dw + ix0;
		local scale0 = focal / t0;
		local waterline = hz + (camh - zg[k0]) * scale0;
		local foot = hz + (camh + ::MS_DEPTH) * scale0;
		local edge_ink = inkg[k0].tointeger();
		edge_ink = tubes + ((edge_ink + phase) & 127);
		local dusk = ::MS_DUSK[::MS_RADIUS[k0]];
		local level = face * dusk;
		local fall = (level - dusk) / (foot - waterline + 0.001);
		local ya = 0;
		if (waterline > 0.0) {
			ya = waterline.tointeger();
		} else {
			level += fall * waterline;
		}
		if (ya > hh) { ya = hh; }
		if (foot < hh) { ybase = foot < 0.0 ? 0 : foot.tointeger(); }
		if (ybase < ya) { ybase = ya; }
		for (local yf = ya; yf < ybase; ++yf) {
			local whole = level.tointeger();
			out[yf] = edge_ink + whole * 128;
			level -= fall;
		}
		if (ya < ybase) {
			local meniscus = (12.0 * dusk).tointeger();
			out[ya] = edge_ink + meniscus * 128;
		}
		ytop = ya;
		local t = t0;
		local row_step = 1.0 / (camh * focal);
		while (t < t1 && ytop > 0) {
			local px = camx + t * dx;
			local py = camy + t * dy;
			local ix = px.tointeger();
			local iy = py.tointeger();
			local sx = px - ix;
			local sy = py - iy;
			local k = iy * dw + ix;
			local k2 = k + dw;
			local za = zg[k];
			local zb = zg[k2];
			local ztop = za + (zg[k + 1] - za) * sx;
			local zbot = zb + (zg[k2 + 1] - zb) * sx;
			local row = hz + (camh - ztop - (zbot - ztop) * sy) * focal / t;
			if (row < ytop) {
				local ys = row < 0.0 ? 0 : row.tointeger();
				if (ys < ytop) {
					local a = inkg[k];
					local b = inkg[k2];
					local top = a + (inkg[k + 1] - a) * sx;
					local bottom = b + (inkg[k2 + 1] - b) * sx;
					local ink = (top + (bottom - top) * sy).tointeger();
					local pix = tubes + litg[k] + ((ink + phase) & 127);
					for (local yy = ys; yy < ytop; ++yy) { out[yy] = pix; }
					ytop = ys;
				}
			}
			local stride = t * t * row_step;
			t += stride < 0.35 ? 0.35 : (stride > 1.0 ? 1.0 : stride);
		}
	}
	local floor_k = (camh + ::MS_DEPTH) * focal;
	local lines = ::MS_GRID;
	local line_wide = lines * 1.2 / focal;
	local gain = ::MS_GRID_GAIN;
	local fade = ::MS_GRID_FADE;
	local from = 0;
	local to = ytop;
	for (local pass = 0; pass < 2; ++pass) {
		for (local y = from; y < to; ++y) {
			local under = y + 0.5 - hz;
			if (under < 1.0) { continue; }
			local far = floor_k / under;
			local shade = gain - far * fade;
			if (shade < 1.0) { continue; }
			local wide = far * line_wide;
			local gx = (camx + far * dx) * lines + 4096.0;
			local gy = (camy + far * dy) * lines + 4096.0;
			local wx = gx.tointeger();
			local wy = gy.tointeger();
			if (gx - wx < wide || gy - wy < wide) {
				local sh = shade.tointeger();
				out[y] = floor + (sh > 8 ? 8 : sh);
			}
		}
		from = ybase;
		to = hh;
	}
	return out;
}

class Maelstrom extends ::Scene {
	cw = 0;
	ch = 0;
	dw = 0;
	dh = 0;
	unit = 1.0;
	u = null;
	v = null;
	u1 = null;
	v1 = null;
	pr = null;
	dv = null;
	cu = null;
	away_x = null;
	away_y = null;
	away_r = null;
	radial = null;
	ring_born = null;
	ring_power = null;
	ring_lays = null;
	ring_next = 0;
	hw = null;
	hv = null;
	dish_pair = null;
	dish_pool = null;
	drop_x = null;
	drop_y = null;
	drop_z = null;
	drop_vx = null;
	drop_vy = null;
	drop_vz = null;
	drop_size = null;
	drop_kind = null;
	drop_entry = null;
	drop_next = 0;
	neon = null;
	tubes = 0;
	floor_at = 0;
	jets = 5;
	sor_iters = 8;
	vorticity = 12.0;
	jet_speed = 14.0;
	settling = 0.18;
	pool_settling = 0.6;
	home_rate = 1.4;
	home_turned = 0.0;
	pair_speed = 16.0;
	pool_speed = 18.0;
	drain_speed = 14.0;
	wave_speed = 1.0;
	wave_drag = 1.0;
	gravity = 1.0;
	fall_lead = 0.55;
	fall_speed = 1.0;
	tt = 0.0;
	turn = 1.0;
	orbit = 0.0;
	cam_yaw = -0.6;
	traded = 0;
	kick_before = 0.0;
	kick_ahead_before = 0.0;
	snare_before = 0.0;
	rain_due = 0.0;
	blob_tube = 0;
	blob_turn = 0;
	spout_turn = 0;
	paddle_turn = 0;

	t_pair = ::MUSIC_BAR * 2.0;
	t_pool = ::MUSIC_BAR * 4.0;
	t_flip = ::MUSIC_BAR * 6.0;
	t_drain = ::MUSIC_BAR * 8.0;

	// the reel picks the grid before build runs, so the budget is set at construction
	constructor() {
		u = [];
		v = [];
		u1 = [];
		v1 = [];
		pr = [];
		dv = [];
		cu = [];
		away_x = [];
		away_y = [];
		away_r = [];
		radial = [];
		ring_born = [];
		ring_power = [];
		ring_lays = [];
		hw = [];
		hv = [];
		dish_pair = [];
		dish_pool = [];
		drop_x = [];
		drop_y = [];
		drop_z = [];
		drop_vx = [];
		drop_vy = [];
		drop_vz = [];
		drop_size = [];
		drop_kind = [];
		drop_entry = [];
		neon = [
			1.00, 0.08, 0.55,
			0.00, 0.80, 1.00,
			1.00, 0.84, 0.04,
			0.58, 0.20, 1.00,
			0.55, 1.00, 0.00,
			1.00, 0.38, 0.00,
			0.10, 0.35, 1.00,
			0.00, 1.00, 0.50
		];
		pixel_budget = 30000;
	}

	function length_bars() { return 10; }
	function hit_times() { return [t_pair, t_pool, t_flip, t_drain]; }
	function hit_levels() { return [1, 2, 1, 2]; }

	function build() {
		name = "MAELSTROM";
		handle = "stable fluids under a wave tank . neon ink";
		replay = 20.0;
		cw = (w + 2) / 3;
		ch = (h + 2) / 3;
		dw = cw * 2;
		dh = ch * 2;
		unit = dh / 80.0;
		wave_speed = dh / 2.0;
		wave_drag = 2.0 * unit * unit;
		gravity = 120.0 * unit;
		fall_speed = 30.0 * unit;
		local cn = cw * ch;
		local dn = dw * dh;
		local gfx = ::gfx;
		u = gfx.make_fgrid(cn);
		v = gfx.make_fgrid(cn);
		u1 = gfx.make_fgrid(cn);
		v1 = gfx.make_fgrid(cn);
		pr = gfx.make_fgrid(cn);
		dv = gfx.make_fgrid(cn);
		cu = gfx.make_fgrid(cn);
		hw = gfx.make_fgrid(dn);
		hv = gfx.make_fgrid(dn);
		::MS_W = w;
		::MS_H = h;
		::MS_DW = dw;
		::MS_DH = dh;
		::MS_CW = cw;
		::MS_CH = ch;
		::MS_CX = dw * 0.5 - 0.5;
		::MS_CY = dh * 0.5 - 0.5;
		::MS_DEPTH = 10.0 * unit;
		::MS_GRID = 4.0 / dh;
		::MS_INK = gfx.make_fgrid(dn);
		::MS_Z = gfx.make_fgrid(dn);
		::MS_LIT = gfx.make_grid(dn);
		::MS_HOME_X = gfx.make_fgrid(dw);
		::MS_HOME_Y = gfx.make_fgrid(dh);
		::MS_INK_ROWS = [];
		for (local r = 0; r < dh; ++r) { ::MS_INK_ROWS.push(r); }
		::MS_COLS = [];
		for (local c = 0; c < w; ++c) { ::MS_COLS.push(c); }
		mix_tubes();
		local reach = (::sqrt(dw * dw + dh * dh * 1.0) * 0.5).tointeger() + 3;
		::MS_SETTLE = gfx.make_fgrid(reach);
		::MS_GLOW = gfx.make_fgrid(reach);
		::MS_DUSK = gfx.make_fgrid(reach);
		radial = gfx.make_fgrid(reach);
		::MS_RADIUS = gfx.make_grid(dn);
		dish_pair = gfx.make_fgrid(dn);
		dish_pool = gfx.make_fgrid(dn);
		local radius = ::MS_RADIUS;
		local pair_core = 5.0 * unit;
		local pool_core = 8.0 * unit;
		for (local y = 0; y < dh; ++y) {
			local ey = y + 0.5 - dh * 0.5;
			for (local x = 0; x < dw; ++x) {
				local ex = x + 0.5 - dw * 0.5;
				local left = ex + dw * 0.23;
				local right = ex - dw * 0.23;
				local at = y * dw + x;
				radius[at] = ::sqrt(ex * ex + ey * ey).tointeger();
				dish_pool[at] = 1.0 / (1.0 + (ex * ex + ey * ey) / (pool_core * pool_core));
				dish_pair[at] = 1.0 / (1.0 + (left * left + ey * ey) / (pair_core * pair_core))
					+ 1.0 / (1.0 + (right * right + ey * ey) / (pair_core * pair_core));
			}
		}
		away_x = gfx.make_fgrid(cn);
		away_y = gfx.make_fgrid(cn);
		away_r = gfx.make_grid(cn);
		for (local gy = 0; gy < ch; ++gy) {
			local fy = gy - (ch - 1) * 0.5;
			for (local gx = 0; gx < cw; ++gx) {
				local fx = gx - (cw - 1) * 0.5;
				local far = ::sqrt(fx * fx + fy * fy);
				local i = gy * cw + gx;
				away_x[i] = fx / (far + 0.0001);
				away_y[i] = fy / (far + 0.0001);
				away_r[i] = (far * 2.0).tointeger();
			}
		}
		for (local k = 0; k < 8; ++k) {
			ring_born.push(-100.0);
			ring_power.push(0.0);
			ring_lays.push(0.0);
		}
		for (local d = 0; d < 192; ++d) {
			drop_x.push(0.0);
			drop_y.push(0.0);
			drop_z.push(0.0);
			drop_vx.push(0.0);
			drop_vy.push(0.0);
			drop_vz.push(0.0);
			drop_size.push(0.0);
			drop_kind.push(-1);
			drop_entry.push(0);
		}
		lay_bands();
		for (local settle = 0; settle < 12; ++settle) { project(); }
		carry_ink(0.0, 0.0);
		shape_surface(0.0);
	}

	// the palette: eight tubes of sixteen entries each, sixteen levels, then the negative of every
	// entry; then the floor's dark and its grid lines, and their negative
	function mix_tubes() {
		tubes = pal.size();
		local gain = [0.0, 0.08, 0.16, 0.25, 0.35, 0.46, 0.58, 0.72, 0.86, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];
		local glare = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.12, 0.26, 0.42, 0.6, 0.8, 1.0];
		for (local negative = 0; negative < 2; ++negative) {
			for (local shade = 0; shade < 16; ++shade) {
				for (local e = 0; e < 128; ++e) {
					local hue = (e / 16) * 3;
					local off = ((e % 16) + 0.5) / 16.0 - 0.5;
					local glow = 1.0 - off * off / 0.1156;
					glow = glow < 0.0 ? 0.0 : glow;
					local core = glow > 0.8 ? (glow - 0.8) * 2.2 : 0.0;
					local light = 0.05 + 0.95 * glow;
					local white = core + (1.0 - core) * glare[shade] * (0.3 + 0.7 * glow);
					local r = 3.0 + 252.0 * neon[hue] * light;
					local g = 8.0 + 247.0 * neon[hue + 1] * light;
					local b = 24.0 + 231.0 * neon[hue + 2] * light;
					r = (r + (255.0 - r) * white) * gain[shade];
					g = (g + (255.0 - g) * white) * gain[shade];
					b = (b + (255.0 - b) * white) * gain[shade];
					if (negative == 1) {
						pal.add(255.0 - r, 255.0 - g, 255.0 - b);
					} else {
						pal.add(r, g, b);
					}
				}
			}
		}
		floor_at = pal.size();
		for (local negative2 = 0; negative2 < 2; ++negative2) {
			for (local line = 0; line < 9; ++line) {
				local lit = line / 8.0;
				local r2 = 90.0 * lit;
				local g2 = 14.0 * lit;
				local b2 = 5.0 + 125.0 * lit;
				if (negative2 == 1) {
					pal.add(255.0 - r2, 255.0 - g2, 255.0 - b2);
				} else {
					pal.add(r2, g2, b2);
				}
			}
		}
	}

	// the ramp the ink settles back to: seven tubes to the tank's height, deepening along (across, down)
	function lay_home(across, down) {
		local tube = 112.0 / dh;
		local home_x = ::MS_HOME_X;
		local home_y = ::MS_HOME_Y;
		local cx = ::MS_CX;
		local cy = ::MS_CY;
		for (local x = 0; x < dw; ++x) { home_x[x] = (x - cx) * across * tube; }
		for (local y = 0; y < dh; ++y) { home_y[y] = 1080.0 + (y - cy) * down * tube; }
	}

	// the opening: the ink in its bands, across jets that run against their neighbours
	function lay_bands() {
		lay_home(0.0, 1.0);
		local ink = ::MS_INK;
		local home_y = ::MS_HOME_Y;
		for (local y = 0; y < dh; ++y) {
			local i = y * dw;
			for (local x = 0; x < dw; ++x) {
				ink[i] = home_y[y];
				++i;
			}
		}
		local phase = [];
		for (local s = 0; s <= jets; ++s) {
			phase.push(rng.nextf() * 6.2831853);
			phase.push(rng.nextf() * 6.2831853);
		}
		for (local gy = 1; gy < ch - 1; ++gy) {
			local across = (gy + 0.5) * jets / ch;
			local seam_at = (across + 0.5).tointeger();
			local ripple = ::cos(3.14159265 * across);
			ripple = seam_at == 0 || seam_at == jets ? 0.0 : ripple * ripple * ripple * ripple;
			for (local gx = 1; gx < cw - 1; ++gx) {
				local j = gy * cw + gx;
				u[j] = band_jet(gy);
				local along = gx * 6.2831853 / cw;
				v[j] = jet_speed * unit * ripple * (0.3 * ::sin(along * 5.0 + phase[seam_at * 2]) + 0.2 * ::sin(along * 3.0 + phase[seam_at * 2 + 1]));
			}
		}
	}

	// a flow row's place in the opening's jets: alternate ways, shearing at the seams
	function band_jet(gy) {
		local wave = ::sin(3.14159265 * (gy + 0.5) * jets / ch) * 3.0;
		local steep = ::exp(wave);
		return jet_speed * unit * (steep - 1.0 / steep) / (steep + 1.0 / steep);
	}

	function smooth01(x) {
		local c = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		return c * c * (3.0 - 2.0 * c);
	}

	function kick_at(t) { return cue == null ? 0.0 : cue.kick_env(t); }
	function snare_at(t) { return cue == null ? 0.0 : cue.snare_env(t); }
	function hit_at(t) { return cue == null ? 0.0 : cue.hit(t); }

	function update(t, dt) {
		local before = tt;
		tt = t;
		if (dt <= 0.0) { return; }
		vorticity = 9.0 + 9.0 * (cue == null ? 0.6 : cue.energy(t));
		local step = dt > 0.05 ? 0.05 : dt;
		strike(before, t);
		stir(t, step);
		confine(step);
		project();
		advect_flow(step);
		carry_ink(t, step);
		throw_water(before, t, step);
		ripple(step);
		fall(step);
		cam_yaw += turn * (0.35 + 0.5 * smooth01((t - t_drain) / (::MUSIC_BAR * 2.0))) * step;
		shape_surface(t);
	}

	// the hits, once each as the clock crosses them, and each kick a wave
	function strike(before, t) {
		if (before < t_pair && t >= t_pair) { ring(t_pair, 0.8, 0.0); }
		if (before < t_pool && t >= t_pool) { ring(t_pool, 1.0, 1.0); }
		if (before < t_flip && t >= t_flip) {
			ring(t_flip, 1.0, 1.0);
			turn = 0.0 - turn;
			traded += 16;
			local cn = cw * ch;
			local uu = u;
			local vv = v;
			local p = pr;
			for (local i = 0; i < cn; ++i) {
				uu[i] = 0.0 - uu[i];
				vv[i] = 0.0 - vv[i];
				p[i] = 0.0 - p[i];
			}
			local vel = hv;
			local cx = ::MS_CX;
			local tilt = 12.0 * unit * turn / (dw * 0.5);
			for (local y = 1; y < dh - 1; ++y) {
				local j = y * dw + 1;
				for (local x = 1; x < dw - 1; ++x) {
					vel[j] += (x - cx) * tilt;
					++j;
				}
			}
		}
		if (before < t_drain && t >= t_drain) {
			ring(t_drain, 1.0, 1.0);
			splash(::MS_CX, ::MS_CY, 12.0 * unit, 9.0 * unit);
			crown(::MS_CX, ::MS_CY, 22, 4.0 * unit, 70.0 * unit, 5);
		}
		local kick = kick_at(t);
		if (kick > kick_before + 0.3) {
			if (t > t_pool) {
				ring(t, 0.3 * kick, 0.0);
				bump(::MS_CX, ::MS_CY, 45.0 * unit * kick, 7.0 * unit);
			} else {
				paddle(45.0 * unit * kick);
			}
		}
		kick_before = kick;
	}

	// a side wall shoves the water beside it up and the water beyond that down, left and right in turn
	function paddle(speed) {
		local vel = hv;
		paddle_turn = 1 - paddle_turn;
		for (local y = 1; y < dh - 1; ++y) {
			local row = y * dw;
			for (local k = 0; k < 8; ++k) {
				local x = 1 + k;
				if (paddle_turn == 1) { x = dw - 2 - k; }
				vel[row + x] += speed * ::cos(0.44879895 * k);
			}
		}
	}

	function ring(born, power, lays) {
		ring_born[ring_next] = born;
		ring_power[ring_next] = power;
		ring_lays[ring_next] = lays;
		ring_next = (ring_next + 1) % ring_born.len();
	}

	// the choreography, in ink cells: the jets, then the pair with the rod, then the whirlpool the
	// drain tightens, six nozzles orbiting from the pair on
	function stir(t, step) {
		local cx = ::MS_CX;
		local cy = ::MS_CY;
		local kick = kick_at(t);
		local snare = snare_at(t);
		if (t < t_pair) {
			drive_jets((0.5 + 1.6 * kick) * step);
			return;
		}
		local drain = smooth01((t - t_drain) / (::MUSIC_BAR * 1.5));
		if (t >= t_pool) {
			local pool = smooth01((t - t_pool) / ::MUSIC_BEAT);
			home_turned += turn * home_rate * (1.0 + drain) * pool * step;
			lay_home(0.0 - ::sin(home_turned), ::cos(home_turned));
			whirl(cx, cy, turn * unit * (pool_speed + drain_speed * drain) * pool, ch * (0.26 - 0.1 * drain), (4.0 + 2.0 * drain) * step);
		} else {
			local pair = smooth01((t - t_pair) / ::MUSIC_BEAT);
			whirl(cx - dw * 0.23, cy, 0.0 - pair_speed * unit * pair, ch * 0.2, 3.5 * step);
			whirl(cx + dw * 0.23, cy, pair_speed * unit * pair, ch * 0.2, 3.5 * step);
			local a = t * 1.3;
			local rod_x = cx + dw * 0.3 * ::sin(a);
			local rod_y = cy + dh * 0.27 * ::sin(a * 2.0);
			drag(rod_x, rod_y, dw * 0.195 * ::cos(a), dh * 0.351 * ::cos(a * 2.0), 4.0 * unit);
			bump(rod_x, rod_y, 0.0 - 90.0 * unit * step, 3.5 * unit);
		}
		local rush = cue == null ? 0.0 : cue.riser(t);
		orbit += turn * (0.55 + 1.2 * rush * rush + 1.4 * drain) * step;
		local reach = 1.0 - 0.5 * drain;
		for (local k = 0; k < 6; ++k) {
			local a2 = orbit + k * 1.0471976;
			local ca = ::cos(a2);
			local sa = ::sin(a2);
			local pours = k % 2 == 0;
			local beat = pours ? kick : snare;
			local aim = pours ? 0.55 : 1.4;
			local speed = (5.0 + 22.0 * beat) * unit / ::sqrt(1.0 + aim * aim);
			local x = cx + ca * dw * 0.37 * reach;
			local y = cy + sa * dh * 0.37 * reach;
			drag(x, y, (0.0 - sa * turn - ca * aim) * speed, (ca * turn - sa * aim) * speed, 3.5 * unit);
			pour(x, y, (pours ? 1.0 : -1.0) * (12.0 + 80.0 * beat) * step, 4.5 * unit);
		}
	}

	// the opening's jets pull the flow back toward their shear
	function drive_jets(grip) {
		local uu = u;
		for (local gy = 1; gy < ch - 1; ++gy) {
			local jet = band_jet(gy);
			local i = gy * cw + 1;
			for (local gx = 1; gx < cw - 1; ++gx) {
				uu[i] += (jet - uu[i]) * grip;
				++i;
			}
		}
	}

	// a disc of `rad` ink cells about ink cell (x, y) pulls the flow toward (vx, vy), hardest at its centre
	function drag(x, y, vx, vy, rad) {
		local uu = u;
		local vv = v;
		local gw = cw;
		local qx = x * 0.5 - 0.25;
		local qy = y * 0.5 - 0.25;
		local qr = rad * 0.5 + 0.75;
		local qr2 = qr * qr;
		local x0 = (qx - qr).tointeger();
		x0 = 1 >= x0 ? 1 : x0;
		local x1 = (qx + qr).tointeger() + 1;
		x1 = gw - 2 <= x1 ? gw - 2 : x1;
		local y0 = (qy - qr).tointeger();
		y0 = 1 >= y0 ? 1 : y0;
		local y1 = (qy + qr).tointeger() + 1;
		y1 = ch - 2 <= y1 ? ch - 2 : y1;
		for (local gy = y0; gy <= y1; ++gy) {
			local ey = gy - qy;
			for (local gx = x0; gx <= x1; ++gx) {
				local ex = gx - qx;
				local q = ex * ex + ey * ey;
				if (q >= qr2) { continue; }
				local f = (1.0 - q / qr2) * 0.6;
				local i = gy * gw + gx;
				uu[i] += (vx - uu[i]) * f;
				vv[i] += (vy - vv[i]) * f;
			}
		}
	}

	// ink poured into (or, negative, drawn out of) a disc about ink cell (x, y), most at its centre
	function pour(x, y, amount, rad) {
		local ink = ::MS_INK;
		local gw = dw;
		local r2 = rad * rad;
		local x0 = (x - rad).tointeger();
		x0 = 1 >= x0 ? 1 : x0;
		local x1 = (x + rad).tointeger() + 1;
		x1 = gw - 2 <= x1 ? gw - 2 : x1;
		local y0 = (y - rad).tointeger();
		y0 = 1 >= y0 ? 1 : y0;
		local y1 = (y + rad).tointeger() + 1;
		y1 = dh - 2 <= y1 ? dh - 2 : y1;
		for (local py = y0; py <= y1; ++py) {
			local ey = py - y;
			for (local px = x0; px <= x1; ++px) {
				local ex = px - x;
				local q = ex * ex + ey * ey;
				if (q >= r2) { continue; }
				local fall = 1.0 - q / r2;
				ink[py * gw + px] += amount * fall * fall;
			}
		}
	}

	// a vortex at ink cell (x, y): the flow is pulled toward a swirl about it that turns `speed` flow
	// cells a second at `core` cells out and falls away beyond
	function whirl(x, y, speed, core, grip) {
		local uu = u;
		local vv = v;
		local gw = cw;
		local gh = ch;
		local qx = x * 0.5 - 0.25;
		local qy = y * 0.5 - 0.25;
		local c2 = core * core;
		local k = 2.0 * speed / core;
		for (local gy = 1; gy < gh - 1; ++gy) {
			local ey = gy - qy;
			local i = gy * gw + 1;
			for (local gx = 1; gx < gw - 1; ++gx) {
				local ex = gx - qx;
				local fall = 1.0 / (1.0 + (ex * ex + ey * ey) / c2);
				local pull = grip * fall;
				uu[i] += (0.0 - ey * k * fall - uu[i]) * pull;
				vv[i] += (ex * k * fall - vv[i]) * pull;
				++i;
			}
		}
	}

	// vorticity confinement: push along N x curl, N the unit gradient of |curl|
	function confine(step) {
		local uu = u;
		local vv = v;
		local c = cu;
		local sqrt = ::sqrt;
		local gw = cw;
		local gh = ch;
		for (local y = 1; y < gh - 1; ++y) {
			local i = y * gw + 1;
			for (local x = 1; x < gw - 1; ++x) {
				c[i] = 0.5 * (vv[i + 1] - vv[i - 1] - uu[i + gw] + uu[i - gw]);
				++i;
			}
		}
		local eps = vorticity * step;
		for (local y2 = 2; y2 < gh - 2; ++y2) {
			local j = y2 * gw + 2;
			for (local x2 = 2; x2 < gw - 2; ++x2) {
				local cr = c[j + 1];
				local cl = c[j - 1];
				local cd = c[j + gw];
				local cn = c[j - gw];
				local gx = (cr < 0.0 ? -cr : cr) - (cl < 0.0 ? -cl : cl);
				local gy = (cd < 0.0 ? -cd : cd) - (cn < 0.0 ? -cn : cn);
				local f = eps * c[j] / (sqrt(gx * gx + gy * gy) + 0.00001);
				uu[j] += gy * f;
				vv[j] -= gx * f;
				++j;
			}
		}
	}

	// make the flow divergence-free: SOR on laplace(p) = div(u) from last frame's pressure (Neumann
	// walls: a border cell copies its inner neighbour), then subtract grad(p)
	function project() {
		local uu = u;
		local vv = v;
		local p = pr;
		local dvg = dv;
		local gw = cw;
		local gh = ch;
		for (local y = 1; y < gh - 1; ++y) {
			local i = y * gw + 1;
			for (local x = 1; x < gw - 1; ++x) {
				dvg[i] = 0.5 * (uu[i + 1] - uu[i - 1] + vv[i + gw] - vv[i - gw]);
				++i;
			}
		}
		local last = (gh - 1) * gw;
		local iters = sor_iters;
		for (local it = 0; it < iters; ++it) {
			for (local y2 = 1; y2 < gh - 1; ++y2) {
				local j = y2 * gw + 1;
				for (local x2 = 1; x2 < gw - 1; ++x2) {
					local g = (p[j - 1] + p[j + 1] + p[j - gw] + p[j + gw] - dvg[j]) * 0.25;
					p[j] += 1.7 * (g - p[j]);
					++j;
				}
			}
			for (local x3 = 0; x3 < gw; ++x3) {
				p[x3] = p[x3 + gw];
				p[last + x3] = p[last - gw + x3];
			}
			for (local y3 = 0; y3 < gh; ++y3) {
				local r = y3 * gw;
				p[r] = p[r + 1];
				p[r + gw - 1] = p[r + gw - 2];
			}
		}
		for (local y4 = 1; y4 < gh - 1; ++y4) {
			local k = y4 * gw + 1;
			for (local x4 = 1; x4 < gw - 1; ++x4) {
				uu[k] -= 0.5 * (p[k + 1] - p[k - 1]);
				vv[k] -= 0.5 * (p[k + gw] - p[k - gw]);
				++k;
			}
		}
	}

	// semi-Lagrangian: each cell traces its velocity back one step and takes the bilinear blend of the
	// four cells it lands among; the walls hold still and the water slowly calms
	function advect_flow(step) {
		local uu = u;
		local vv = v;
		local un = u1;
		local vn = v1;
		local gw = cw;
		local gh = ch;
		local xmax = gw - 1.001;
		local ymax = gh - 1.001;
		local keep = 1.0 - 0.15 * step;
		for (local y = 1; y < gh - 1; ++y) {
			local i = y * gw + 1;
			for (local x = 1; x < gw - 1; ++x) {
				local px = x - step * uu[i];
				local py = y - step * vv[i];
				px = px < 0.0 ? 0.0 : (px > xmax ? xmax : px);
				py = py < 0.0 ? 0.0 : (py > ymax ? ymax : py);
				local ix = px.tointeger();
				local iy = py.tointeger();
				local sx = px - ix;
				local sy = py - iy;
				local k = iy * gw + ix;
				local top = uu[k] + (uu[k + 1] - uu[k]) * sx;
				local bot = uu[k + gw] + (uu[k + gw + 1] - uu[k + gw]) * sx;
				un[i] = (top + (bot - top) * sy) * keep;
				local top2 = vv[k] + (vv[k + 1] - vv[k]) * sx;
				local bot2 = vv[k + gw] + (vv[k + gw + 1] - vv[k + gw]) * sx;
				vn[i] = (top2 + (bot2 - top2) * sy) * keep;
				++i;
			}
		}
		// the reference's array assignment copies
		u = clone u1;
		v = clone v1;
	}

	// what the tank's middle sends out, by radius: every ring's lit crest, dark wake and shove outward
	// (a shock's wake also lays the ink back in its bands), the drain's pull inward, its white eye and
	// the dark closing in on it
	function radiate(t, step) {
		local settle = ::MS_SETTLE;
		local push = radial;
		local lit = ::MS_GLOW;
		local kept = ::MS_DUSK;
		local n = lit.len();
		local drain = smooth01((t - t_drain) / ::MUSIC_BAR);
		local closing = smooth01((t - t_drain - ::MUSIC_BAR * 0.5) / (::MUSIC_BAR * 1.5));
		local dark_at = n * (1.0 - closing) + 11.0 * unit * closing;
		local dusk = 0.25 / unit;
		local eye = 3.5 * unit;
		local mouth = 9.0 * unit;
		local pull = 26.0 * unit * drain;
		local calm = (settling + (pool_settling - settling) * smooth01((t - t_pool) / ::MUSIC_BEAT)) * step;
		local full = 8.0 + 2.5 * hit_at(t);
		for (local r = 0; r < n; ++r) {
			local m = r / mouth;
			local q = r / eye;
			push[r] = 0.0 - pull * m / (1.0 + m * m);
			lit[r] = full + 5.0 * drain / (1.0 + q * q * q * q);
			settle[r] = calm;
		}
		local wide = 3.5 * unit;
		local nr = ring_born.len();
		for (local k = 0; k < nr; ++k) {
			local age = t - ring_born[k];
			if (age < 0.0 || age > 2.2) { continue; }
			local amp = ring_power[k] * ::exp(0.0 - age * 1.5);
			local at = (3.0 + age * 48.0) * unit;
			local r0 = (at - wide * 2.6).tointeger();
			r0 = 0 >= r0 ? 0 : r0;
			local r1 = (at + wide).tointeger() + 1;
			r1 = n - 1 <= r1 ? n - 1 : r1;
			for (local r2 = r0; r2 <= r1; ++r2) {
				local e = (r2 - at) / wide;
				if (e > -1.0) {
					local crest = 1.0 - e * e;
					if (crest > 0.0) {
						lit[r2] += 3.4 * amp * crest;
						push[r2] += 24.0 * unit * amp * crest;
					}
				} else {
					local wake = 1.0 - (e + 1.8) * (e + 1.8) / 0.64;
					if (wake > 0.0) {
						lit[r2] -= 3.0 * amp * wake;
						settle[r2] += ring_lays[k] * wake;
					}
				}
			}
		}
		for (local r3 = 0; r3 < n; ++r3) {
			if (settle[r3] > 1.0) { settle[r3] = 1.0; }
			local left = (dark_at - r3) * dusk;
			kept[r3] = left < 0.0 ? 0.0 : (left > 1.0 ? 1.0 : left);
		}
	}

	// the ink rides the flow plus what the rings and the drain add; its rows run in parallel and
	// rebuild the ink once every row has read the old one
	function carry_ink(t, step) {
		radiate(t, step);
		// the reference's array assignment copies, and the copy is then pushed
		::MS_FU = clone u;
		::MS_FV = clone v;
		local fu = ::MS_FU;
		local fv = ::MS_FV;
		local push = radial;
		local ar = away_r;
		local ax = away_x;
		local ay = away_y;
		local cn = cw * ch;
		for (local i = 0; i < cn; ++i) {
			local out = push[ar[i]];
			fu[i] += ax[i] * out;
			fv[i] += ay[i] * out;
		}
		::MS_RUN = step * 2.0;
		local rows = ::MS_INK_ROWS.map(::maelstrom_ink_row);
		local ink = ::MS_INK;
		ink.clear();
		local nrows = rows.len();
		for (local r = 0; r < nrows; ++r) { ink.extend(rows[r]); }
	}

	// the wave equation on the ink's grid, in steps short enough that a wave crosses less than a cell
	// in one; a wall cell copies its inner neighbour, so waves reflect
	function ripple(step) {
		local hgt = hw;
		local vel = hv;
		local gw = dw;
		local gh = dh;
		local subs = (step * wave_speed / 0.6).tointeger() + 1;
		local part = step / subs;
		local pull = wave_speed * wave_speed * part;
		local calm = wave_drag * part;
		local keep = 1.0 - 0.8 * part;
		local last = (gh - 1) * gw;
		for (local sub = 0; sub < subs; ++sub) {
			for (local y = 1; y < gh - 1; ++y) {
				local i = y * gw + 1;
				for (local x = 1; x < gw - 1; ++x) {
					local here = hgt[i];
					local speed = vel[i];
					vel[i] = (speed + pull * (hgt[i - 1] + hgt[i + 1] + hgt[i - gw] + hgt[i + gw] - 4.0 * here)
						+ calm * (vel[i - 1] + vel[i + 1] + vel[i - gw] + vel[i + gw] - 4.0 * speed)) * keep;
					++i;
				}
			}
			for (local y2 = 1; y2 < gh - 1; ++y2) {
				local j = y2 * gw + 1;
				for (local x2 = 1; x2 < gw - 1; ++x2) {
					hgt[j] += vel[j] * part;
					++j;
				}
			}
			for (local x3 = 0; x3 < gw; ++x3) {
				hgt[x3] = hgt[x3 + gw];
				hgt[last + x3] = hgt[last - gw + x3];
			}
			for (local y3 = 0; y3 < gh; ++y3) {
				local r = y3 * gw;
				hgt[r] = hgt[r + 1];
				hgt[r + gw - 1] = hgt[r + gw - 2];
			}
		}
	}

	// something lands at ink cell (x, y): a crater `deep` at its middle with a raised rim, which between
	// them hold as much water as the flat they replace
	function splash(x, y, deep, rad) {
		local hgt = hw;
		local gw = dw;
		local r2 = rad * rad;
		local x0 = (x - rad).tointeger();
		x0 = 1 >= x0 ? 1 : x0;
		local x1 = (x + rad).tointeger() + 1;
		x1 = gw - 2 <= x1 ? gw - 2 : x1;
		local y0 = (y - rad).tointeger();
		y0 = 1 >= y0 ? 1 : y0;
		local y1 = (y + rad).tointeger() + 1;
		y1 = dh - 2 <= y1 ? dh - 2 : y1;
		for (local py = y0; py <= y1; ++py) {
			local ey = py - y;
			for (local px = x0; px <= x1; ++px) {
				local ex = px - x;
				local q = (ex * ex + ey * ey) / r2;
				if (q >= 1.0) { continue; }
				hgt[py * gw + px] -= deep * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q);
			}
		}
	}

	// a disc of the surface about ink cell (x, y) is thrown upward (or, negative, pressed down), fastest
	// at its middle, its rim going the other way by as much water
	function bump(x, y, speed, rad) {
		local vel = hv;
		local gw = dw;
		local r2 = rad * rad;
		local x0 = (x - rad).tointeger();
		x0 = 1 >= x0 ? 1 : x0;
		local x1 = (x + rad).tointeger() + 1;
		x1 = gw - 2 <= x1 ? gw - 2 : x1;
		local y0 = (y - rad).tointeger();
		y0 = 1 >= y0 ? 1 : y0;
		local y1 = (y + rad).tointeger() + 1;
		y1 = dh - 2 <= y1 ? dh - 2 : y1;
		for (local py = y0; py <= y1; ++py) {
			local ey = py - y;
			for (local px = x0; px <= x1; ++px) {
				local ex = px - x;
				local q = (ex * ex + ey * ey) / r2;
				if (q >= 1.0) { continue; }
				vel[py * gw + px] += speed * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q);
			}
		}
	}

	function drop(kind, size, entry, x, y, z, vx, vy, vz) {
		local i = drop_next;
		drop_next = (drop_next + 1) % drop_kind.len();
		drop_kind[i] = kind;
		drop_size[i] = size;
		drop_entry[i] = entry;
		drop_x[i] = x;
		drop_y[i] = y;
		drop_z[i] = z;
		drop_vx[i] = vx;
		drop_vy[i] = vy;
		drop_vz[i] = vz;
	}

	// the bright part of the tube the ink at ink cell (x, y) lies in
	function tube_at(x, y) {
		local ink = ::MS_INK[y.tointeger() * dw + x.tointeger()].tointeger();
		return (((ink + traded) & 127) / 16) * 16 + 5;
	}

	// a blob let go over ink cell (x, y) so that it lands `fall_lead` from now
	function let_fall(x, y, size) {
		blob_tube = (blob_tube + 3) % 8;
		local from = fall_speed * fall_lead + 0.5 * gravity * fall_lead * fall_lead;
		drop(2, size, blob_tube * 16 + 5, x, y, from, 0.0, 0.0, 0.0 - fall_speed);
	}

	// `count` drops of spray thrown up and outward from a ring about ink cell (x, y)
	function crown(x, y, count, rad, up, entry) {
		local from = ::MS_Z[y.tointeger() * dw + x.tointeger()] + 0.6;
		for (local j = 0; j < count; ++j) {
			local a = rng.nextf() * 6.2831853;
			local out = (5.0 + 9.0 * rng.nextf()) * unit;
			local ca = ::cos(a);
			local sa = ::sin(a);
			drop(0, 0.0, entry, x + ca * rad, y + sa * rad, from, ca * out, sa * out, up * (0.55 + 0.6 * rng.nextf()));
		}
	}

	// what the cue throws at the water: rain, a blob ahead of every kick, the hits' own blobs, a spout
	// on each snare, and spray wherever a wave climbs a wall
	function throw_water(before, t, step) {
		local cx = ::MS_CX;
		local cy = ::MS_CY;
		rain_due += (t < t_pair ? 7.0 : (t < t_drain ? 1.5 : 0.0)) * step;
		while (rain_due >= 1.0) {
			rain_due -= 1.0;
			local rx = 2.0 + rng.nextf() * (dw - 5.0);
			local ry = 2.0 + rng.nextf() * (dh - 5.0);
			drop(1, 0.0, tube_at(rx, ry), rx, ry, 34.0 * unit, 0.0, 0.0, -25.0 * unit);
		}
		local lands = t + fall_lead;
		local ahead = kick_at(lands);
		local scripted = ::fabs(lands - t_pair) < 0.25 || ::fabs(lands - t_pool) < 0.25 || ::fabs(lands - t_drain) < 0.25;
		if (ahead > kick_ahead_before + 0.3 && !scripted && lands < t_drain + ::MUSIC_BAR) {
			if (lands < t_pair) {
				let_fall(cx + (rng.nextf() - 0.5) * dw * 0.7, cy + (rng.nextf() - 0.5) * dh * 0.7, 2.2 * unit);
			} else {
				local reach = 1.0 - 0.5 * smooth01((lands - t_drain) / (::MUSIC_BAR * 1.5));
				local a = orbit + turn * 0.55 * fall_lead + blob_turn * 2.0943951;
				blob_turn = (blob_turn + 1) % 3;
				let_fall(cx + ::cos(a) * dw * 0.37 * reach, cy + ::sin(a) * dh * 0.37 * reach, 2.2 * unit);
			}
		}
		kick_ahead_before = ahead;
		local pair_falls = t_pair - fall_lead;
		if (before < pair_falls && t >= pair_falls) {
			let_fall(cx - dw * 0.23, cy, 3.0 * unit);
			let_fall(cx + dw * 0.23, cy, 3.0 * unit);
		}
		local pool_falls = t_pool - fall_lead;
		if (before < pool_falls && t >= pool_falls) { let_fall(cx, cy, 4.5 * unit); }
		local snare = snare_at(t);
		if (t >= t_pair && t < t_drain + ::MUSIC_BAR && snare > snare_before + 0.3) {
			local reach2 = 1.0 - 0.5 * smooth01((t - t_drain) / (::MUSIC_BAR * 1.5));
			local a2 = orbit + (1 + 2 * spout_turn) * 1.0471976;
			spout_turn = (spout_turn + 1) % 3;
			local sx = cx + ::cos(a2) * dw * 0.37 * reach2;
			local sy = cy + ::sin(a2) * dh * 0.37 * reach2;
			bump(sx, sy, 50.0 * unit, 5.0 * unit);
			crown(sx, sy, 5, 1.5 * unit, 60.0 * unit, tube_at(sx, sy));
		}
		snare_before = snare;
		for (local s = 0; s < 10; ++s) {
			local side = rng.next(4);
			local along = rng.nextf();
			local wx = 2.0 + along * (dw - 5.0);
			local wy = 2.0 + along * (dh - 5.0);
			local in_x = 0.0;
			local in_y = 0.0;
			if (side == 0) {
				wy = 2.0;
				in_y = 1.0;
			} else if (side == 1) {
				wy = dh - 3.0;
				in_y = -1.0;
			} else if (side == 2) {
				wx = 2.0;
				in_x = 1.0;
			} else {
				wx = dw - 3.0;
				in_x = -1.0;
			}
			local k = wy.tointeger() * dw + wx.tointeger();
			if (hw[k] > 3.0 * unit && hv[k] > 0.0) {
				drop(0, 0.0, tube_at(wx, wy), wx, wy, ::MS_Z[k] + 0.6, in_x * 5.0 * unit, in_y * 5.0 * unit, 30.0 * unit + 0.5 * hv[k]);
			}
		}
	}

	// everything in the air falls; what meets the water on its way down lands, and what leaves the
	// tank falls past it to the floor
	function fall(step) {
		local pull = gravity * step;
		local xhi = dw - 2.0;
		local yhi = dh - 2.0;
		local bed = 0.0 - ::MS_DEPTH;
		local z_at = ::MS_Z;
		local gw = dw;
		local kinds = drop_kind;
		local dx = drop_x;
		local dy = drop_y;
		local dz = drop_z;
		local vz = drop_vz;
		local n = kinds.len();
		for (local i = 0; i < n; ++i) {
			local kind = kinds[i];
			if (kind < 0) { continue; }
			vz[i] -= pull;
			local x = dx[i] + drop_vx[i] * step;
			local y = dy[i] + drop_vy[i] * step;
			local z = dz[i] + vz[i] * step;
			dx[i] = x;
			dy[i] = y;
			dz[i] = z;
			if (x < 1.0 || x > xhi || y < 1.0 || y > yhi) {
				if (z < bed) { kinds[i] = -1; }
				continue;
			}
			if (vz[i] > 0.0 || z > z_at[y.tointeger() * gw + x.tointeger()]) { continue; }
			kinds[i] = -1;
			land(kind, drop_size[i], drop_entry[i], x, y);
		}
	}

	// spray makes a ripple, rain a small crater and a couple of drops, a blob a crater to its size, a
	// crown of spray and its ink
	function land(kind, size, entry, x, y) {
		if (kind == 0) {
			local r = 3.0 * unit;
			splash(x, y, 0.4 * unit, r >= 2.5 ? r : 2.5);
			return;
		}
		if (kind == 1) {
			local r1 = 4.0 * unit;
			splash(x, y, 2.0 * unit, r1 >= 3.0 ? r1 : 3.0);
			crown(x, y, 2, 1.0 * unit, 26.0 * unit, entry);
			return;
		}
		local big = size / (2.2 * unit);
		splash(x, y, 8.0 * unit * big, 5.5 * unit * big);
		pour(x, y, 36.0 * big, 4.5 * unit * big);
		crown(x, y, (8.0 * big * big).tointeger(), 2.4 * unit * big, 46.0 * unit * ::sqrt(big), entry);
	}

	// the camera swings about the tank's long side, framing it whole from wherever it stands: it punches
	// in on the kick and the hits, shakes with a hit, and over the drain climbs and dives at the eye
	function place_camera(t) {
		local flash = hit_at(t);
		local dive = smooth01((t - t_drain) / (::MUSIC_BAR * 2.0));
		local yaw = 1.5707963 + 0.36 * ::sin(cam_yaw);
		local fx = ::cos(yaw);
		local fy = ::sin(yaw);
		local half_w = dw * 0.5;
		local half_h = dh * 0.5;
		local dist = 0.9 * ::sqrt(dw * dw + dh * dh * 1.0);
		local along_a = ::fabs(half_w * fx + half_h * fy);
		local aside_a = ::fabs(half_h * fx - half_w * fy);
		local along_b = ::fabs(half_w * fx - half_h * fy);
		local aside_b = ::fabs(half_h * fx + half_w * fy);
		local along = along_a > along_b ? along_a : along_b;
		local near = dist - along;
		local far = dist + along;
		local fit_a = (dist - along_a) / (aside_a + 0.001);
		local fit_b = (dist - along_b) / (aside_b + 0.001);
		local camh = dist * (0.5 + 0.5 * dive);
		local wide = 0.48 * w * (fit_a < fit_b ? fit_a : fit_b);
		local tall = 0.78 * h / ((camh + ::MS_DEPTH) / near - camh / far);
		local whole = wide < tall ? wide : tall;
		local focal = whole * (1.0 + 1.6 * dive);
		local hz_whole = 0.61 * h - 0.5 * whole * ((camh + ::MS_DEPTH) / near + camh / far);
		local hz_eye = 0.5 * h - camh * focal / dist;
		local hz = hz_whole + (hz_eye - hz_whole) * dive;
		local punch = 1.0 + 0.035 * kick_at(t) + 0.08 * flash;
		local mid = h * 0.5;
		::MS_FOCAL = focal * punch;
		::MS_HORIZON = mid - (mid - hz) * punch + ::sin(t * 53.0) * flash * flash * h * 0.02;
		::MS_CAM_H = camh;
		::MS_FACE_X = fx;
		::MS_FACE_Y = fy;
		::MS_CAM_X = dw * 0.5 - fx * dist;
		::MS_CAM_Y = dh * 0.5 - fy * dist;
		::MS_GLINT_X = 0.5 * fx;
		::MS_GLINT_Y = 0.5 * fy;
		local closing = smooth01((t - t_drain - ::MUSIC_BAR * 0.5) / (::MUSIC_BAR * 1.5));
		local reach = far * 1.5 - near * 0.8;
		::MS_GRID_FADE = 7.0 / reach;
		::MS_GRID_GAIN = (7.0 + near * 0.8 * ::MS_GRID_FADE + 1.5 * kick_at(t)) * (1.0 - closing);
	}

	// what the picture reads: the waves with the vortices' dimples and the drain's funnel pressed into
	// them, the camera, and the surface's light from its slopes
	function shape_surface(t) {
		place_camera(t);
		local pool = smooth01((t - t_pool) / ::MUSIC_BEAT);
		local pair = smooth01((t - t_pair) / ::MUSIC_BEAT) * (1.0 - pool);
		local drain = smooth01((t - t_drain) / (::MUSIC_BAR * 1.5));
		local pair_deep = 5.0 * unit * pair;
		local pool_deep = 6.0 * unit * pool;
		local drain_deep = 20.0 * unit * drain;
		local z = ::MS_Z;
		local hgt = hw;
		local pair_dish = dish_pair;
		local pool_dish = dish_pool;
		local dn = dw * dh;
		for (local i = 0; i < dn; ++i) {
			local f = pool_dish[i];
			z[i] = hgt[i] - pair_deep * pair_dish[i] - (pool_deep + drain_deep * f) * f;
		}
		::MS_LIGHT_X = 1.4;
		::MS_LIGHT_Y = 1.9;
		::MS_GLINT_NARROW = 1.0 / (0.42 * 0.42);
		::MS_GLINT = 7.0;
		::MS_CREST = 0.4 / unit;
		local rows = ::MS_INK_ROWS.map(::maelstrom_light_row);
		local lit = ::MS_LIT;
		lit.clear();
		local nrows = rows.len();
		for (local r = 0; r < nrows; ++r) { lit.extend(rows[r]); }
	}

	// what is in the air, over the finished picture: spray and rain as short streaks, blobs as discs
	function draw_drops(grid) {
		local fx = ::MS_FACE_X;
		local fy = ::MS_FACE_Y;
		local camx = ::MS_CAM_X;
		local camy = ::MS_CAM_Y;
		local camh = ::MS_CAM_H;
		local focal = ::MS_FOCAL;
		local hz = ::MS_HORIZON;
		local ww = w;
		local hh = h;
		local mid = ww * 0.5;
		local spray = ::MS_TUBES + 11 * 128;
		local blob = ::MS_TUBES + 14 * 128;
		local kinds = drop_kind;
		local n = kinds.len();
		for (local i = 0; i < n; ++i) {
			if (kinds[i] < 0) { continue; }
			local ex = drop_x[i] - camx;
			local ey = drop_y[i] - camy;
			local depth = ex * fx + ey * fy;
			if (depth < 4.0) { continue; }
			local scale = focal / depth;
			local px = mid + (ey * fx - ex * fy) * scale;
			local py = hz + (camh - drop_z[i]) * scale;
			local rad = drop_size[i] * scale;
			if (rad < 0.9) {
				local tail = py + drop_vz[i] * 0.03 * scale;
				local top = py < tail ? py : tail;
				local bottom = py < tail ? tail : py;
				if (px < 0.0 || px >= ww || bottom < 0.0 || top >= hh) { continue; }
				local sx = px.tointeger();
				local ya = top < 0.0 ? 0 : top.tointeger();
				local yb = bottom >= hh ? hh - 1 : bottom.tointeger();
				if (yb > ya + 3) { yb = ya + 3; }
				local pix = spray + drop_entry[i];
				for (local y = ya; y <= yb; ++y) { grid[y * ww + sx] = pix; }
				continue;
			}
			local reach = (rad + 1.0).tointeger();
			local cx = (px + 1000.0).tointeger() - 1000;
			local cy = (py + 1000.0).tointeger() - 1000;
			local r2 = rad * rad + 0.3;
			local core = blob + drop_entry[i];
			local rim = spray + drop_entry[i];
			for (local oy = 0 - reach; oy <= reach; ++oy) {
				local y2 = cy + oy;
				if (y2 < 0 || y2 >= hh) { continue; }
				for (local ox = 0 - reach; ox <= reach; ++ox) {
					local x2 = cx + ox;
					local q = (ox * ox + oy * oy).tofloat();
					if (x2 < 0 || x2 >= ww || q > r2) { continue; }
					if (q * 2.5 < r2) {
						grid[y2 * ww + x2] = core;
					} else {
						grid[y2 * ww + x2] = rim;
					}
				}
			}
		}
	}

	function render(grid) {
		// the shocks flash the picture negative
		local since_pool = tt - t_pool;
		local since_drain = tt - t_drain;
		local negative = (since_pool >= 0.0 && since_pool < 0.1) || (since_drain >= 0.0 && since_drain < 0.1);
		local ww = w;
		local hh = h;
		::MS_W = ww;
		::MS_H = hh;
		::MS_TUBES = negative ? tubes + 2048 : tubes;
		::MS_FLOOR = negative ? floor_at + 9 : floor_at;
		::MS_PHASE = traded + (3.5 * kick_at(tt)).tointeger();
		local cols = ::MS_COLS.map(::maelstrom_column);
		if (grid.len() != ww * hh) { grid.resize(ww * hh, 0); }
		for (local x = 0; x < ww; ++x) {
			local col = cols[x];
			local gi = x;
			for (local y = 0; y < hh; ++y) {
				grid[gi] = col[y];
				gi += ww;
			}
		}
		draw_drops(grid);
	}
}

::SCENES[10] <- ::Maelstrom;
