// DONUT (Squirrel port of scenes/donut.jai): a frosted torus, sphere-traced per
// pixel. The reference's parallel_transform rows run as a serial loop here.

::DN_M00 <- 1.0; ::DN_M01 <- 0.0; ::DN_M02 <- 0.0;
::DN_M10 <- 0.0; ::DN_M11 <- 1.0; ::DN_M12 <- 0.0;
::DN_M20 <- 0.0; ::DN_M21 <- 0.0; ::DN_M22 <- 1.0;
::DN_LX <- 0.0; ::DN_LY <- 0.0; ::DN_LZ <- 0.0;
::DN_HX <- 0.0; ::DN_HY <- 0.0; ::DN_HZ <- 0.0;
::DN_CAM <- 5.6;
::DN_W <- 0;
::DN_H <- 0;
::DN_MAT <- 0;
::DN_SEED <- 0;
::DN_ROWS <- [];

function donut_row(y) {
	local out = [];
	local ww = ::DN_W;
	local hh = ::DN_H;
	local m00 = ::DN_M00, m01 = ::DN_M01, m02 = ::DN_M02;
	local m10 = ::DN_M10, m11 = ::DN_M11, m12 = ::DN_M12;
	local m20 = ::DN_M20, m21 = ::DN_M21, m22 = ::DN_M22;
	local lx = ::DN_LX, ly = ::DN_LY, lz = ::DN_LZ;
	local hx = ::DN_HX, hy = ::DN_HY, hz = ::DN_HZ;
	local mat = ::DN_MAT;
	local dseed = ::DN_SEED;
	local sqrt = ::sqrt, atan2 = ::atan2, sin = ::sin;
	local sc = 1.75 / ww;
	local v = (hh * 0.5 - y - 0.5) * sc;
	local cam = ::DN_CAM;
	local ox = 0.0 - m02 * cam;
	local oy = 0.0 - m12 * cam;
	local oz = 0.0 - m22 * cam;
	local oo = ox * ox + oy * oy + oz * oz;
	for (local x = 0; x < ww; ++x) {
		local u = (x + 0.5 - ww * 0.5) * sc;
		local il = 1.0 / sqrt(u * u + v * v + 1.0);
		local wx = u * il;
		local wy = v * il;
		local wz = il;
		local dx = m00 * wx + m01 * wy + m02 * wz;
		local dy = m10 * wx + m11 * wy + m12 * wz;
		local dz = m20 * wx + m21 * wy + m22 * wz;
		local b = ox * dx + oy * dy + oz * dz;
		local disc = b * b - oo + 9.3;
		if (disc <= 0.0) { out.push(y); continue; }
		local sq = sqrt(disc);
		local t = 0.0 - b - sq;
		local tfar = 0.0 - b + sq;
		local hit = 0;
		local px = 0.0, py = 0.0, pz = 0.0, ring = 0.0;
		for (local step = 0; step < 40; ++step) {
			px = ox + dx * t;
			py = oy + dy * t;
			pz = oz + dz * t;
			ring = sqrt(px * px + pz * pz);
			local qx = ring - 2.0;
			local d = sqrt(qx * qx + py * py) - 1.0;
			if (d < 0.004) { hit = 1; break; }
			t += d;
			if (t > tfar) { break; }
		}
		if (hit == 0) { out.push(y); continue; }
		local rs = 2.0 / (ring > 0.0001 ? ring : 0.0001);
		local nx = px - px * rs;
		local ny = py;
		local nz = pz - pz * rs;
		local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz);
		nx *= nl; ny *= nl; nz *= nl;
		local dl = nx * lx + ny * ly + nz * lz;
		dl = dl < 0.0 ? 0.0 : dl;
		local s = nx * hx + ny * hy + nz * hz;
		s = s < 0.0 ? 0.0 : s;
		local s2 = s * s;
		local s4 = s2 * s2;
		local s8 = s4 * s4;
		local s16 = s8 * s8;
		local phi = atan2(pz, px);
		local m = 0;
		if (ny > -0.22 + 0.16 * sin(phi * 5.0 + 2.0 * sin(phi * 3.0))) {
			m = 1;
			local th = atan2(ny, nx * px / ring + nz * pz / ring);
			local fp = (phi + 3.14159265) * 9.5;
			local ft = (th + 3.14159265) * 3.5;
			local cp = fp.tointeger();
			local ct = ft.tointeger();
			local hs = ((cp * 73856093 + ct * 19349663 + dseed) & 1048575) * 1274126177;
			local r = (hs >> 12) & 1023;
			local ep = fp - cp - 0.5;
			local et = ft - ct - 0.5;
			local across = (r & 1) == 0;
			local la = across ? ep : et;
			local lb = across ? et : ep;
			if (r < 560 && la * la < 0.13 && lb * lb < 0.024) { m = 2 + r % 5; }
		}
		local di = (dl * 15.99).tointeger();
		local si = (s16 * s8 * 3.99).tointeger();
		out.push(mat + m * 64 + di * 4 + si);
	}
	return out;
}

class Donut extends Scene {
	mat0 = 0;
	tt = 0.0;

	function build() {
		name = "DONUT";
		handle = "torus and effect . by strong_ptr";
		for (local y = 0; y < h; ++y) {
			local f = y / (h - 1.0);
			pal.add(34.0 + 30.0 * f, 18.0 + 14.0 * f, 30.0 - 8.0 * f);
		}
		mat0 = pal.size();
		local mats = [[196.0, 128.0, 64.0], [255.0, 132.0, 186.0], [255.0, 60.0, 60.0], [255.0, 220.0, 60.0],
			[70.0, 150.0, 255.0], [90.0, 230.0, 110.0], [250.0, 250.0, 250.0]];
		for (local m = 0; m < 7; ++m) {
			local c = mats[m];
			local cr = c[0], cg = c[1], cb = c[2];
			for (local d = 0; d < 16; ++d) {
				local df = 0.10 + 0.90 * d / 15.0;
				for (local s = 0; s < 4; ++s) {
					local sp = s / 3.0 * (m == 0 ? 0.45 : 0.9);
					pal.add(cr * df + (255.0 - cr * df) * sp, cg * df + (255.0 - cg * df) * sp, cb * df + (255.0 - cb * df) * sp);
				}
			}
		}
	}

	function update(t, dt) {
		tt = t;
	}

	function render(grid) {
		local ca = ::cos(tt * 0.9);
		local sa = ::sin(tt * 0.9);
		local cb = ::cos(tt * 0.53 + 1.2);
		local sb = ::sin(tt * 0.53 + 1.2);
		::DN_M00 = cb;        ::DN_M01 = sb;        ::DN_M02 = 0.0;
		::DN_M10 = 0.0 - sb * ca; ::DN_M11 = cb * ca; ::DN_M12 = sa;
		::DN_M20 = sb * sa;   ::DN_M21 = 0.0 - cb * sa; ::DN_M22 = ca;
		local lx = -0.42, ly = 0.66, lz = -0.62;
		local hx = lx, hy = ly, hz = lz - 1.0;
		local hl = 1.0 / ::sqrt(hx * hx + hy * hy + hz * hz);
		hx *= hl; hy *= hl; hz *= hl;
		::DN_LX = ::DN_M00 * lx + ::DN_M01 * ly + ::DN_M02 * lz;
		::DN_LY = ::DN_M10 * lx + ::DN_M11 * ly + ::DN_M12 * lz;
		::DN_LZ = ::DN_M20 * lx + ::DN_M21 * ly + ::DN_M22 * lz;
		::DN_HX = ::DN_M00 * hx + ::DN_M01 * hy + ::DN_M02 * hz;
		::DN_HY = ::DN_M10 * hx + ::DN_M11 * hy + ::DN_M12 * hz;
		::DN_HZ = ::DN_M20 * hx + ::DN_M21 * hy + ::DN_M22 * hz;
		::DN_W = w;
		::DN_H = h;
		::DN_MAT = mat0;
		::DN_SEED = seed & 1023;
		if (::DN_ROWS.len() != h) {
			::DN_ROWS = [];
			for (local r = 0; r < h; ++r) { ::DN_ROWS.push(r); }
		}
		local rows = [];
		foreach (r in ::DN_ROWS) { rows.push(::donut_row(r)); }
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
