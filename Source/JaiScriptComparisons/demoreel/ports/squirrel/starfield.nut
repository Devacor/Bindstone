// STARFIELD (Squirrel port of scenes/starfield.jai): a warp run over a
// periodic value-noise nebula; 700 stars as motion streaks, additive palette.

class Starfield extends Scene {
	sx = null;
	sy = null;
	sz = null;
	temp = null;
	neb = null;
	nstars = 700;
	nlev = 20;
	per = 33;
	tt = 0.0;
	speed = 0.3;
	roll = 0.0;
	flash = 0.0;

	constructor() {
		sx = [];
		sy = [];
		sz = [];
		temp = [];
		neb = [];
	}

	function build() {
		name = "STARFIELD";
		handle = "warp run . greets: parser crew";
		local stops = [[1.0, 1.0, 6.0], [10.0, 6.0, 30.0], [40.0, 12.0, 60.0], [84.0, 22.0, 86.0], [36.0, 70.0, 110.0], [120.0, 150.0, 190.0]];
		local temps = [[150.0, 180.0, 255.0], [255.0, 255.0, 255.0], [255.0, 236.0, 170.0], [255.0, 170.0, 110.0]];
		for (local l = 0; l < nlev; ++l) {
			local f = l * 5.0 / (nlev - 1.0);
			local k = f.tointeger();
			k = k > 4 ? 4 : k;
			local u = f - k;
			local a = stops[k];
			local b = stops[k + 1];
			local ar = a[0], ag = a[1], ab = a[2];
			local nr = ar + (b[0] - ar) * u;
			local ng = ag + (b[1] - ag) * u;
			local nb = ab + (b[2] - ab) * u;
			pal.add(nr, ng, nb);
			for (local tc = 0; tc < 4; ++tc) {
				local c = temps[tc];
				local cr = c[0], cg = c[1], cb = c[2];
				for (local lv = 0; lv < 8; ++lv) {
					local s = (lv + 1) / 8.0;
					s = s * (0.6 + 0.4 * s);
					pal.add(nr + cr * s, ng + cg * s, nb + cb * s);
				}
			}
		}
		grow_nebula();
		for (local i = 0; i < nstars; ++i) {
			sx.push((rng.next(2000) - 1000) * 0.0016);
			sy.push((rng.next(2000) - 1000) * 0.0016);
			sz.push(0.05 + rng.next(1000) * 0.0011);
			local r = rng.next(100);
			temp.push(r < 30 ? 0 : (r < 70 ? 1 : (r < 90 ? 2 : 3)));
		}
	}

	// three octaves of periodic value noise, squared for wispy contrast
	function grow_nebula() {
		local lat = [];
		local sd = seed & 65535;
		for (local i = 0; i < 32 * 32; ++i) {
			local hs = (i * 73856093 + sd * 19349663) & 1048575;
			lat.push(((hs * 1274126177) >> 12) & 255);
		}
		for (local y = 0; y < 128; ++y) {
			for (local x = 0; x < 128; ++x) {
				local acc = 0;
				local cell = 32;
				local n = 4;
				local wgt = 8;
				for (local o = 0; o < 3; ++o) {
					local ix = x / cell;
					local iy = y / cell;
					local fx = (x % cell) * 256 / cell;
					local fy = (y % cell) * 256 / cell;
					local ix1 = (ix + 1) % n;
					local iy1 = (iy + 1) % n;
					local bs = o * 5;
					local a = lat[((iy + bs) & 31) * 32 + ((ix + bs * 3) & 31)];
					local b = lat[((iy + bs) & 31) * 32 + ((ix1 + bs * 3) & 31)];
					local c = lat[((iy1 + bs) & 31) * 32 + ((ix + bs * 3) & 31)];
					local d = lat[((iy1 + bs) & 31) * 32 + ((ix1 + bs * 3) & 31)];
					local top = a + (b - a) * fx / 256;
					local bot = c + (d - c) * fx / 256;
					acc += (top + (bot - top) * fy / 256) * wgt;
					cell = cell / 2;
					n = n * 2;
					wgt = wgt / 2;
				}
				local v = acc / 14;
				v = v - 60;
				v = v < 0 ? 0 : v;
				v = v * v / 110;
				v = v * (nlev - 1) / 255;
				neb.push(v > nlev - 1 ? nlev - 1 : v);
			}
		}
	}

	function update(t, dt) {
		tt = t;
		local target = 0.3;
		if (t > 2.5) { target = 0.3 + (t - 2.5) * 0.5; }
		if (t > 6.0) { target = 2.1 + (t - 6.0) * 1.1; }
		if (t > 8.5) { target = 0.35; }
		target = target > 3.6 ? 3.6 : target;
		local mixv = dt * (t > 8.5 ? 6.0 : 1.5);
		mixv = mixv > 1.0 ? 1.0 : mixv;
		speed += (target - speed) * mixv;
		flash = t > 8.5 && t < 9.4 ? 1.0 - (t - 8.5) / 0.9 : 0.0;
		roll += dt * (0.05 + speed * 0.07);
		for (local i = 0; i < nstars; ++i) {
			local z = sz[i] - dt * speed * 0.9;
			if (z < 0.03) {
				sx[i] = (rng.next(2000) - 1000) * 0.0016;
				sy[i] = (rng.next(2000) - 1000) * 0.0016;
				z = 1.1 + rng.next(1000) * 0.0003;
			}
			sz[i] = z;
		}
	}

	function render(grid) {
		local ww = w;
		local hh = h;
		local pr = per;
		local nb = neb;
		local boost = (flash * 12.0).tointeger();
		local ox = (tt * 3.0).tointeger();
		local oy = (tt * 1.3).tointeger();
		local nl1 = nlev - 1;
		local idx = 0;
		for (local y = 0; y < hh; ++y) {
			local row = ((y + oy) & 127) * 128;
			for (local x = 0; x < ww; ++x) {
				local l = nb[row + ((x + ox) & 127)] + boost;
				grid[idx] = (l > nl1 ? nl1 : l) * pr;
				idx = idx + 1;
			}
		}
		local cx = ww * 0.5;
		local cy = hh * 0.5;
		local f = ww * 0.45;
		local ca = ::cos(roll);
		local sa = ::sin(roll);
		local trail = 0.02 + speed * 0.05;
		local xs = sx;
		local ys = sy;
		local zs = sz;
		local tp = temp;
		for (local i = 0; i < nstars; ++i) {
			local z = zs[i];
			local rx = xs[i] * ca - ys[i] * sa;
			local ry = xs[i] * sa + ys[i] * ca;
			local hx = cx + rx / z * f;
			local hy = cy + ry / z * f;
			if (hx < 0.0 || hx >= ww || hy < 0.0 || hy >= hh) { continue; }
			local zb = z + trail;
			local tx = cx + rx / zb * f;
			local ty = cy + ry / zb * f;
			local lvl = (8.8 - z * 7.0).tointeger();
			lvl = lvl < 0 ? 0 : (lvl > 7 ? 7 : lvl);
			local tbase = 1 + tp[i] * 8;
			local ddx = hx - tx;
			local ddy = hy - ty;
			local adx = ddx < 0.0 ? -ddx : ddx;
			local ady = ddy < 0.0 ? -ddy : ddy;
			local n = (adx > ady ? adx : ady).tointeger();
			n = n > 48 ? 48 : n;
			for (local k = 0; k <= n; ++k) {
				local u = n > 0 ? k * 1.0 / n : 1.0;
				local px = (tx + ddx * u).tointeger();
				local py = (ty + ddy * u).tointeger();
				if (px < 0 || px >= ww || py < 0 || py >= hh) { continue; }
				local sl = (lvl * (0.25 + 0.75 * u)).tointeger();
				local gi = py * ww + px;
				local cur = grid[gi];
				local bare = cur - cur % pr;
				local had = cur % pr;
				local had_l = had > 0 ? (had - 1) % 8 : -1;
				if (sl > had_l) { grid[gi] = bare + tbase + sl; }
			}
			// near stars bloom: a dim cross around the head
			if (lvl >= 6) {
				local hxi = hx.tointeger();
				local hyi = hy.tointeger();
				for (local q = 0; q < 4; ++q) {
					local gx = hxi + (q == 0 ? 1 : (q == 1 ? -1 : 0));
					local gy = hyi + (q == 2 ? 1 : (q == 3 ? -1 : 0));
					if (gx < 0 || gx >= ww || gy < 0 || gy >= hh) { continue; }
					local gi2 = gy * ww + gx;
					local cur2 = grid[gi2];
					local had2 = cur2 % pr;
					local hl2 = had2 > 0 ? (had2 - 1) % 8 : -1;
					if (lvl - 4 > hl2) { grid[gi2] = cur2 - had2 + tbase + lvl - 4; }
				}
			}
		}
	}
}
