// CHROME (Squirrel port of scenes/chrome.jai): a one-ray-per-pixel tracer of three
// chrome spheres over a checkerboard. The reference's parallel_transform rows run
// as a serial loop here.

::CH_X0 <- 0.0; ::CH_Y0 <- 0.0; ::CH_Z0 <- 0.0; ::CH_R0 <- 0.9;
::CH_X1 <- 0.0; ::CH_Y1 <- 0.0; ::CH_Z1 <- 0.0; ::CH_R1 <- 0.7;
::CH_X2 <- 0.0; ::CH_Y2 <- 0.0; ::CH_Z2 <- 0.0; ::CH_R2 <- 0.55;
::CH_EX <- 0.0; ::CH_EY <- 1.6; ::CH_EZ <- 5.0;
::CH_FX <- 0.0; ::CH_FY <- 0.0; ::CH_FZ <- -1.0;
::CH_RX <- 1.0; ::CH_RZ <- 0.0;
::CH_UX <- 0.0; ::CH_UY <- 1.0; ::CH_UZ <- 0.0;
::CH_LX <- 0.5; ::CH_LY <- 0.75; ::CH_LZ <- 0.43;
::CH_W <- 0;
::CH_H <- 0;
::CH_FLOOR <- 0;
::CH_BALL <- 0;
::CH_NBASE <- 0;
::CH_GLINT <- 0;
::CH_ROWS <- [];

// what a ray from (ox,oy,oz) along unit (dx,dy,dz) sees, as a class index;
// `skip` is the sphere it left
function ch_trace(ox, oy, oz, dx, dy, dz, skip) {
	local best = 1000000.0;
	local hit = -1;
	local hr = 1.0;
	local hx = 0.0, hy = 0.0, hz = 0.0;
	if (skip != 0) {
		local qx = ox - ::CH_X0, qy = oy - ::CH_Y0, qz = oz - ::CH_Z0;
		local b = qx * dx + qy * dy + qz * dz;
		local disc = b * b - (qx * qx + qy * qy + qz * qz - ::CH_R0 * ::CH_R0);
		if (disc > 0.0) {
			local t = 0.0 - b - ::sqrt(disc);
			if (t > 0.001 && t < best) { best = t; hit = 0; hr = ::CH_R0; hx = ::CH_X0; hy = ::CH_Y0; hz = ::CH_Z0; }
		}
	}
	if (skip != 1) {
		local qx1 = ox - ::CH_X1, qy1 = oy - ::CH_Y1, qz1 = oz - ::CH_Z1;
		local b1 = qx1 * dx + qy1 * dy + qz1 * dz;
		local disc1 = b1 * b1 - (qx1 * qx1 + qy1 * qy1 + qz1 * qz1 - ::CH_R1 * ::CH_R1);
		if (disc1 > 0.0) {
			local t1 = 0.0 - b1 - ::sqrt(disc1);
			if (t1 > 0.001 && t1 < best) { best = t1; hit = 1; hr = ::CH_R1; hx = ::CH_X1; hy = ::CH_Y1; hz = ::CH_Z1; }
		}
	}
	if (skip != 2) {
		local qx2 = ox - ::CH_X2, qy2 = oy - ::CH_Y2, qz2 = oz - ::CH_Z2;
		local b2 = qx2 * dx + qy2 * dy + qz2 * dz;
		local disc2 = b2 * b2 - (qx2 * qx2 + qy2 * qy2 + qz2 * qz2 - ::CH_R2 * ::CH_R2);
		if (disc2 > 0.0) {
			local t2 = 0.0 - b2 - ::sqrt(disc2);
			if (t2 > 0.001 && t2 < best) { best = t2; hit = 2; hr = ::CH_R2; hx = ::CH_X2; hy = ::CH_Y2; hz = ::CH_Z2; }
		}
	}
	if (hit >= 0) {
		local d = ((ox + dx * best - hx) * ::CH_LX + (oy + dy * best - hy) * ::CH_LY + (oz + dz * best - hz) * ::CH_LZ) / hr;
		d = d < 0.0 ? 0.0 : d;
		local lv = ((0.2 + 0.8 * d) * 7.99).tointeger();
		return ::CH_BALL + hit * 8 + lv;
	}
	if (dy < -0.0005) {
		local tf = (0.0 - oy) / dy;
		local fx = ox + dx * tf;
		local fz = oz + dz * tf;
		local ix = (fx + 4096.0).tointeger();
		local iz = (fz + 4096.0).tointeger();
		local fog = (11.5 - tf * 0.55).tointeger();
		fog = fog < 0 ? 0 : (fog > 11 ? 11 : fog);
		local shade = 0;
		local sx = fx - ::CH_X0, sz = fz - ::CH_Z0;
		local sb = sx * ::CH_LX - ::CH_Y0 * ::CH_LY + sz * ::CH_LZ;
		if (sb < 0.0 && sb * sb - (sx * sx + ::CH_Y0 * ::CH_Y0 + sz * sz - ::CH_R0 * ::CH_R0) > 0.0) { shade = 1; }
		local sx1 = fx - ::CH_X1, sz1 = fz - ::CH_Z1;
		local sb1 = sx1 * ::CH_LX - ::CH_Y1 * ::CH_LY + sz1 * ::CH_LZ;
		if (sb1 < 0.0 && sb1 * sb1 - (sx1 * sx1 + ::CH_Y1 * ::CH_Y1 + sz1 * sz1 - ::CH_R1 * ::CH_R1) > 0.0) { shade = 1; }
		local sx2 = fx - ::CH_X2, sz2 = fz - ::CH_Z2;
		local sb2 = sx2 * ::CH_LX - ::CH_Y2 * ::CH_LY + sz2 * ::CH_LZ;
		if (sb2 < 0.0 && sb2 * sb2 - (sx2 * sx2 + ::CH_Y2 * ::CH_Y2 + sz2 * sz2 - ::CH_R2 * ::CH_R2) > 0.0) { shade = 1; }
		return ::CH_FLOOR + (((ix + iz) & 1) * 2 + shade) * 12 + fog;
	}
	local s = (dy * 30.0).tointeger();
	return s < 0 ? 0 : (s > 23 ? 23 : s);
}

function ch_row(y) {
	local out = [];
	local ww = ::CH_W;
	local sc = 2.0 / ww;
	local v = (::CH_H * 0.5 - y - 0.5) * sc;
	local ex = ::CH_EX, ey = ::CH_EY, ez = ::CH_EZ;
	local bx = ::CH_FX * 1.35 + ::CH_UX * v;
	local by = ::CH_FY * 1.35 + ::CH_UY * v;
	local bz = ::CH_FZ * 1.35 + ::CH_UZ * v;
	for (local x = 0; x < ww; ++x) {
		local u = (x + 0.5 - ww * 0.5) * sc;
		local dx = bx + ::CH_RX * u;
		local dy = by;
		local dz = bz + ::CH_RZ * u;
		local il = 1.0 / ::sqrt(dx * dx + dy * dy + dz * dz);
		dx *= il; dy *= il; dz *= il;
		local best = 1000000.0;
		local hit = -1;
		local cx = 0.0, cy = 0.0, cz = 0.0, cr = 1.0;
		local qx = ex - ::CH_X0, qy = ey - ::CH_Y0, qz = ez - ::CH_Z0;
		local b = qx * dx + qy * dy + qz * dz;
		local disc = b * b - (qx * qx + qy * qy + qz * qz - ::CH_R0 * ::CH_R0);
		if (disc > 0.0) {
			local t = 0.0 - b - ::sqrt(disc);
			if (t > 0.001) { best = t; hit = 0; cx = ::CH_X0; cy = ::CH_Y0; cz = ::CH_Z0; cr = ::CH_R0; }
		}
		local qx1 = ex - ::CH_X1, qy1 = ey - ::CH_Y1, qz1 = ez - ::CH_Z1;
		local b1 = qx1 * dx + qy1 * dy + qz1 * dz;
		local disc1 = b1 * b1 - (qx1 * qx1 + qy1 * qy1 + qz1 * qz1 - ::CH_R1 * ::CH_R1);
		if (disc1 > 0.0) {
			local t1 = 0.0 - b1 - ::sqrt(disc1);
			if (t1 > 0.001 && t1 < best) { best = t1; hit = 1; cx = ::CH_X1; cy = ::CH_Y1; cz = ::CH_Z1; cr = ::CH_R1; }
		}
		local qx2 = ex - ::CH_X2, qy2 = ey - ::CH_Y2, qz2 = ez - ::CH_Z2;
		local b2 = qx2 * dx + qy2 * dy + qz2 * dz;
		local disc2 = b2 * b2 - (qx2 * qx2 + qy2 * qy2 + qz2 * qz2 - ::CH_R2 * ::CH_R2);
		if (disc2 > 0.0) {
			local t2 = 0.0 - b2 - ::sqrt(disc2);
			if (t2 > 0.001 && t2 < best) { best = t2; hit = 2; cx = ::CH_X2; cy = ::CH_Y2; cz = ::CH_Z2; cr = ::CH_R2; }
		}
		if (hit < 0) {
			out.push(::ch_trace(ex, ey, ez, dx, dy, dz, -1));
			continue;
		}
		local hx = ex + dx * best;
		local hy = ey + dy * best;
		local hz = ez + dz * best;
		local nx = (hx - cx) / cr;
		local ny = (hy - cy) / cr;
		local nz = (hz - cz) / cr;
		local dn = 2.0 * (dx * nx + dy * ny + dz * nz);
		local mx = dx - nx * dn;
		local my = dy - ny * dn;
		local mz = dz - nz * dn;
		local g = mx * ::CH_LX + my * ::CH_LY + mz * ::CH_LZ;
		if (g > 0.975) {
			local gl = ((g - 0.975) * 100.0).tointeger();
			out.push(::CH_GLINT + (gl > 2 ? 2 : gl));
		} else {
			out.push(::CH_NBASE * (hit + 1) + ::ch_trace(hx, hy, hz, mx, my, mz, hit));
		}
	}
	return out;
}

class Chrome extends Scene {
	rad = null;
	bx = null;
	by = null;
	bz = null;
	nsky = 24;
	nfog = 12;
	floor0 = 0;
	ball0 = 0;
	nbase = 0;
	glint = 0;
	ex = 0.0;
	ey = 1.6;
	ez = 5.0;
	fx = 0.0;
	fy = 0.0;
	fz = -1.0;
	rx = 1.0;
	rz = 0.0;
	ux = 0.0;
	uy = 1.0;
	uz = 0.0;
	tt = 0.0;

	constructor() {
		rad = [0.9, 0.7, 0.55];
		bx = [0.0, 0.0, 0.0];
		by = [0.0, 0.0, 0.0];
		bz = [0.0, 0.0, 0.0];
	}

	function build() {
		name = "CHROME";
		handle = "juggler's grandchild . one ray per pixel, every core";
		local tints = [[1.0, 0.82, 0.45], [0.86, 0.9, 1.0], [1.0, 0.45, 0.55]];
		for (local copy = 0; copy < 4; ++copy) {
			local tr = 1.0, tg = 1.0, tb = 1.0;
			local k = 1.0;
			if (copy > 0) {
				local tc = tints[copy - 1];
				tr = tc[0]; tg = tc[1]; tb = tc[2];
				k = 0.86;
			}
			class_table(tr * k, tg * k, tb * k);
			if (copy == 0) { nbase = pal.size(); }
		}
		glint = pal.ramp(3, 255.0, 236.0, 190.0, 255.0, 255.0, 255.0);
	}

	function class_table(tr, tg, tb) {
		for (local s = 0; s < nsky; ++s) {
			local f = s / (nsky - 1.0);
			pal.add((250.0 - 200.0 * f) * tr, (200.0 - 120.0 * f) * tg, (170.0 + 40.0 * f) * tb);
		}
		if (floor0 == 0) { floor0 = pal.size(); }
		local tiles = [[235.0, 226.0, 210.0], [150.0, 36.0, 60.0]];
		for (local tile = 0; tile < 2; ++tile) {
			local tc = tiles[tile];
			local r0 = tc[0], g0 = tc[1], b0 = tc[2];
			for (local sh = 0; sh < 2; ++sh) {
				local dk = sh == 1 ? 0.38 : 1.0;
				for (local f2 = 0; f2 < nfog; ++f2) {
					local k = f2 / (nfog - 1.0);
					pal.add((r0 * dk * k + 250.0 * (1.0 - k)) * tr, (g0 * dk * k + 200.0 * (1.0 - k)) * tg, (b0 * dk * k + 170.0 * (1.0 - k)) * tb);
				}
			}
		}
		if (ball0 == 0) { ball0 = pal.size(); }
		local balls = [[255.0, 200.0, 110.0], [210.0, 220.0, 240.0], [255.0, 110.0, 130.0]];
		for (local b = 0; b < 3; ++b) {
			local bc = balls[b];
			for (local lv = 0; lv < 8; ++lv) {
				local k2 = 0.15 + 0.85 * lv / 7.0;
				pal.add(bc[0] * k2 * tr, bc[1] * k2 * tg, bc[2] * k2 * tb);
			}
		}
	}

	function update(t, dt) {
		tt = t;
		for (local i = 0; i < 3; ++i) {
			local bounce = ::sin(t * (1.9 + i * 0.37) + i * 2.1);
			bounce = bounce < 0.0 ? -bounce : bounce;
			local a = t * 0.45 + i * 2.0944;
			bx[i] = ::cos(a) * 1.45;
			bz[i] = ::sin(a) * 1.45;
			by[i] = rad[i] + bounce * (1.1 - i * 0.2);
		}
		local ca = t * 0.18 + 0.4;
		ex = ::sin(ca) * 5.2;
		ez = ::cos(ca) * 5.2;
		ey = 1.5 + 0.35 * ::sin(t * 0.3);
		local tx = 0.0 - ex;
		local ty = 0.8 - ey;
		local tz = 0.0 - ez;
		local il = 1.0 / ::sqrt(tx * tx + ty * ty + tz * tz);
		fx = tx * il; fy = ty * il; fz = tz * il;
		local rl = 1.0 / ::sqrt(fx * fx + fz * fz);
		rx = 0.0 - fz * rl;
		rz = fx * rl;
		ux = 0.0 - rz * fy;
		uy = rz * fx - rx * fz;
		uz = rx * fy;
	}

	function render(grid) {
		::CH_X0 = bx[0]; ::CH_Y0 = by[0]; ::CH_Z0 = bz[0]; ::CH_R0 = rad[0];
		::CH_X1 = bx[1]; ::CH_Y1 = by[1]; ::CH_Z1 = bz[1]; ::CH_R1 = rad[1];
		::CH_X2 = bx[2]; ::CH_Y2 = by[2]; ::CH_Z2 = bz[2]; ::CH_R2 = rad[2];
		::CH_EX = ex; ::CH_EY = ey; ::CH_EZ = ez;
		::CH_FX = fx; ::CH_FY = fy; ::CH_FZ = fz;
		::CH_RX = rx; ::CH_RZ = rz;
		::CH_UX = ux; ::CH_UY = uy; ::CH_UZ = uz;
		local ll = 1.0 / ::sqrt(0.5 * 0.5 + 0.75 * 0.75 + 0.43 * 0.43);
		::CH_LX = 0.5 * ll; ::CH_LY = 0.75 * ll; ::CH_LZ = 0.43 * ll;
		::CH_W = w;
		::CH_H = h;
		::CH_FLOOR = floor0;
		::CH_BALL = ball0;
		::CH_NBASE = nbase;
		::CH_GLINT = glint;
		if (::CH_ROWS.len() != h) {
			::CH_ROWS = [];
			for (local r = 0; r < h; ++r) { ::CH_ROWS.push(r); }
		}
		local rows = [];
		foreach (r in ::CH_ROWS) { rows.push(::ch_row(r)); }
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
