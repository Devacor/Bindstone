// PIPEDREAM (Squirrel port of scenes/pipes.jai): the 3D Pipes screensaver;
// non-intersecting pipes in an occupancy grid, lit sphere impostors splatted
// into a persistent frame under a camera that holds still per generation.

class Pipes extends Scene {
	cells = null;
	pgrid = null;
	pz = null;
	pending = null;
	eggs = null;
	hx = null;
	hy = null;
	hz = null;
	hd = null;
	DX = null;
	DY = null;
	DZ = null;
	gw = 16;
	gh = 12;
	gd = 16;
	npipes = 4;
	pipe_base = 0;
	egg_base = 0;
	filled = 0;
	cap = 0;
	phase = 0;
	phase_t = 0.0;
	last_wipe = 0;
	step_acc = 0.0;
	tt = 0.0;
	gen_ang = 0.0;
	gen_tilt = 0.34;
	f_ca = 1.0;
	f_sa = 0.0;
	f_ct = 1.0;
	f_st = 0.0;
	flick = 0;

	constructor() {
		cells = [];
		pgrid = [];
		pz = [];
		pending = [];
		eggs = [];
		hx = [];
		hy = [];
		hz = [];
		hd = [];
		DX = [1, -1, 0, 0, 0, 0];
		DY = [0, 0, 1, -1, 0, 0];
		DZ = [0, 0, 0, 0, 1, -1];
	}

	function build() {
		name = "PIPEDREAM";
		handle = "screensaver seance . watch for the jackodile";
		cells = ::gfx.make_grid(gw * gh * gd);
		pgrid = ::gfx.make_grid(w * h);
		pz = ::gfx.make_fgrid(w * h);
		cap = gw * gh * gd * 34 / 100;
		for (local y = 0; y < h; ++y) {
			local f = y / (h - 1.0);
			pal.add(3.0 + 6.0 * f, 4.0 + 5.0 * f, 12.0 + 14.0 * f);
		}
		for (local q = 0; q < w * h; ++q) { pgrid[q] = q / w; }
		pipe_base = h;
		local hues = [[70.0, 225.0, 195.0], [255.0, 178.0, 58.0], [232.0, 88.0, 212.0], [128.0, 158.0, 255.0]];
		for (local p = 0; p < npipes; ++p) {
			local hue = hues[p];
			shade_ramp(hue[0], hue[1], hue[2]);
		}
		egg_base = pal.size();
		for (local e = 0; e < 6; ++e) {
			local c = ::gfx.rainbow_rgb(e * 1.0471975512);
			shade_ramp(c[0], c[1], c[2]);
		}
		set_view(rng.next(628) * 0.01, 0.26 + rng.next(20) * 0.01);
		for (local p2 = 0; p2 < npipes; ++p2) {
			hx.push(0);
			hy.push(0);
			hz.push(0);
			hd.push(0);
			respawn(p2);
		}
	}

	// 16 levels: shadow -> full color -> white-hot highlight
	function shade_ramp(r, g, b) {
		for (local s = 0; s < 16; ++s) {
			local f = s / 15.0;
			local k = f < 0.8 ? 0.06 + f * 1.18 : 1.0;
			local wh = f < 0.8 ? 0.0 : (f - 0.8) * 4.0;
			pal.add(r * k + (255.0 - r * k) * wh, g * k + (255.0 - g * k) * wh, b * k + (255.0 - b * k) * wh);
		}
	}

	function set_view(ang, tilt) {
		gen_ang = ang;
		gen_tilt = tilt;
		f_ca = ::cos(ang);
		f_sa = ::sin(ang);
		f_ct = ::cos(tilt);
		f_st = ::sin(tilt);
	}

	function cell_idx(x, y, z) {
		return (y * gd + z) * gw + x;
	}

	function is_free(x, y, z) {
		if (x < 0 || x >= gw || y < 0 || y >= gh || z < 0 || z >= gd) { return false; }
		return cells[cell_idx(x, y, z)] == 0;
	}

	function advance(p, nx, ny, nz) {
		hx[p] = nx;
		hy[p] = ny;
		hz[p] = nz;
		local ci = cell_idx(nx, ny, nz);
		cells[ci] = 1 + p * 3;
		pending.push(ci);
		filled = filled + 1;
	}

	function respawn(p) {
		for (local tr = 0; tr < 30; ++tr) {
			local x = rng.next(gw);
			local y = rng.next(gh);
			local z = rng.next(gd);
			local ci = cell_idx(x, y, z);
			if (cells[ci] == 0) {
				hx[p] = x;
				hy[p] = y;
				hz[p] = z;
				hd[p] = rng.next(6);
				cells[ci] = 1 + p * 3;
				pending.push(ci);
				filled = filled + 1;
				return;
			}
		}
	}

	function step_one(p) {
		local x = hx[p];
		local y = hy[p];
		local z = hz[p];
		local d = hd[p];
		local straight_ok = is_free(x + DX[d], y + DY[d], z + DZ[d]);
		if (straight_ok && rng.next(100) < 72) {
			advance(p, x + DX[d], y + DY[d], z + DZ[d]);
			return;
		}
		local nd = -1;
		for (local tr = 0; tr < 8; ++tr) {
			local cand = rng.next(6);
			if (cand == d || cand == (d ^ 1)) { continue; }
			if (is_free(x + DX[cand], y + DY[cand], z + DZ[cand])) { nd = cand; break; }
		}
		if (nd < 0 && straight_ok) {
			advance(p, x + DX[d], y + DY[d], z + DZ[d]);
			return;
		}
		if (nd < 0) {
			for (local cand2 = 0; cand2 < 6; ++cand2) {
				if (cand2 == (d ^ 1)) { continue; }
				if (is_free(x + DX[cand2], y + DY[cand2], z + DZ[cand2])) { nd = cand2; break; }
			}
		}
		if (nd < 0) {
			respawn(p);
			return;
		}
		local here = cell_idx(x, y, z);
		if (rng.chance(2)) {
			cells[here] = 3 + p * 3;
			eggs.push(here);
		} else {
			cells[here] = 2 + p * 3;
		}
		pending.push(here);
		hd[p] = nd;
		advance(p, x + DX[nd], y + DY[nd], z + DZ[nd]);
	}

	function update(t, dt) {
		tt = t;
		if (phase == 0) {
			step_acc += dt;
			while (phase == 0 && step_acc >= 0.03) {
				step_acc -= 0.03;
				for (local p = 0; p < npipes; ++p) { step_one(p); }
				if (filled >= cap) { phase = 1; phase_t = 0.0; step_acc = 0.0; }
			}
		} else if (phase == 1) {
			phase_t += dt;
			if (phase_t > 1.8) { phase = 2; phase_t = 0.0; }
		} else {
			phase_t += dt;
			if (phase_t > 1.5) { reset_volume(); }
		}
	}

	function reset_volume() {
		local n = cells.len();
		for (local i = 0; i < n; ++i) { cells[i] = 0; }
		local nn = w * h;
		for (local q = 0; q < nn; ++q) { pgrid[q] = q / w; pz[q] = 0.0; }
		pending.clear();
		eggs.clear();
		filled = 0;
		phase = 0;
		phase_t = 0.0;
		last_wipe = 0;
		step_acc = 0.0;
		set_view(rng.next(628) * 0.01, 0.26 + rng.next(20) * 0.01);
		for (local p = 0; p < npipes; ++p) { respawn(p); }
	}

	function splat_at(fx, fy, fz, c) {
		local rx = fx * f_ca + fz * f_sa;
		local rz = fz * f_ca - fx * f_sa;
		local ry = fy * f_ct - rz * f_st;
		local zcam = fy * f_st + rz * f_ct + 30.0;
		local ooz = 1.0 / zcam;
		local kind = (c - 1) % 3;
		local p = (c - 1) / 3;
		local scale = (w < h * 1.3 ? w : h * 1.3) * 1.25 * ooz;
		local rad = scale * (kind == 0 ? 0.40 : 0.58);
		rad = rad < 0.7 ? 0.7 : rad;
		local sx = w * 0.5 + rx * scale;
		local sy = h * 0.52 - ry * scale;
		local fog = 1.25 - zcam * 0.018;
		fog = fog > 1.0 ? 1.0 : (fog < 0.35 ? 0.35 : fog);
		local cbase = kind == 2 ? egg_base + ((flick + p + (fx * 3.0 + fz + 40.0).tointeger()) % 6) * 16 : pipe_base + p * 16;
		local x0 = (sx - rad).tointeger();
		local x1 = (sx + rad).tointeger() + 1;
		local y0 = (sy - rad).tointeger();
		local y1 = (sy + rad).tointeger() + 1;
		local ir = 1.0 / rad;
		local r2 = rad * rad;
		local dz_gain = ooz * 0.02;
		local pg = pgrid;
		local zz = pz;
		local ww = w;
		local hh = h;
		for (local yy = y0; yy <= y1; ++yy) {
			if (yy < 0 || yy >= hh) { continue; }
			local dy = yy + 0.5 - sy;
			local rowb = yy * ww;
			for (local xx = x0; xx <= x1; ++xx) {
				if (xx < 0 || xx >= ww) { continue; }
				local dx = xx + 0.5 - sx;
				local q = r2 - dx * dx - dy * dy;
				if (q < 0.0) { continue; }
				local nz = ::sqrt(q) * ir;
				local depth = ooz + nz * dz_gain;
				if (depth < zz[rowb + xx]) { continue; }
				local nx = dx * ir;
				local ny = dy * ir;
				local d = nx * -0.48 + ny * -0.56 + nz * 0.67;
				d = d < 0.0 ? 0.0 : d;
				local s = nx * -0.25 + ny * -0.29 + nz * 0.92;
				s = s < 0.0 ? 0.0 : s;
				local s2 = s * s;
				local s4 = s2 * s2;
				local s8 = s4 * s4;
				local lit = (0.10 + d * 0.78) * fog + s8 * s8 * (kind == 0 ? 0.5 : 0.8);
				local lv = (lit * 15.99).tointeger();
				zz[rowb + xx] = depth;
				pg[rowb + xx] = cbase + (lv > 15 ? 15 : lv);
			}
		}
	}

	// splat one voxel plus midpoints toward every same-pipe neighbor
	function splat_voxel(vidx) {
		local c = cells[vidx];
		if (c == 0) { return; }
		local ix = vidx % gw;
		local rest = vidx / gw;
		local iz = rest % gd;
		local iy = rest / gd;
		local fx = ix - (gw - 1) * 0.5;
		local fy = iy - (gh - 1) * 0.5;
		local fz = iz - (gd - 1) * 0.5;
		splat_at(fx, fy, fz, c);
		local pp = (c - 1) / 3;
		local seg = 1 + pp * 3;
		if (ix > 0) {
			local n0 = cells[vidx - 1];
			if (n0 != 0 && (n0 - 1) / 3 == pp) { splat_at(fx - 0.25, fy, fz, seg); splat_at(fx - 0.5, fy, fz, seg); }
		}
		if (ix < gw - 1) {
			local n1 = cells[vidx + 1];
			if (n1 != 0 && (n1 - 1) / 3 == pp) { splat_at(fx + 0.25, fy, fz, seg); splat_at(fx + 0.5, fy, fz, seg); }
		}
		if (iz > 0) {
			local n2 = cells[vidx - gw];
			if (n2 != 0 && (n2 - 1) / 3 == pp) { splat_at(fx, fy, fz - 0.25, seg); splat_at(fx, fy, fz - 0.5, seg); }
		}
		if (iz < gd - 1) {
			local n3 = cells[vidx + gw];
			if (n3 != 0 && (n3 - 1) / 3 == pp) { splat_at(fx, fy, fz + 0.25, seg); splat_at(fx, fy, fz + 0.5, seg); }
		}
		if (iy > 0) {
			local n4 = cells[vidx - gw * gd];
			if (n4 != 0 && (n4 - 1) / 3 == pp) { splat_at(fx, fy - 0.25, fz, seg); splat_at(fx, fy - 0.5, fz, seg); }
		}
		if (iy < gh - 1) {
			local n5 = cells[vidx + gw * gd];
			if (n5 != 0 && (n5 - 1) / 3 == pp) { splat_at(fx, fy + 0.25, fz, seg); splat_at(fx, fy + 0.5, fz, seg); }
		}
	}

	function render(grid) {
		local nn = w * h;
		if (phase == 2) {
			local target = (phase_t * h / 1.3).tointeger();
			target = target > h ? h : target;
			local stop = target * w;
			for (local q = last_wipe * w; q < stop; ++q) { pgrid[q] = q / w; pz[q] = 0.0; }
			last_wipe = target;
		} else {
			flick = (tt * 8.0).tointeger();
			foreach (v in pending) { splat_voxel(v); }
			pending.clear();
			foreach (e in eggs) { splat_voxel(e); }
		}
		local pg = pgrid;
		for (local i = 0; i < nn; ++i) { grid[i] = pg[i]; }
	}
}
