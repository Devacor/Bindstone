// KINSTEIN 3D (Squirrel port of scenes/kinstein.jai): a textured grid
// raycaster with per-pixel floor/ceiling, billboard enemies and an auto-firing
// camera.

class Kinstein extends Scene {
	wmap = null;
	mw = 24;
	mh = 24;
	wpx = null;
	wpy = null;
	seglen = null;
	nwp = 6;
	path_len = 0.0;
	posx = 2.5;
	posy = 2.5;
	dirx = 0.0;
	diry = 1.0;
	planex = 0.0;
	planey = 0.0;
	tex = null;
	wall_base = 0;
	banner_base = 0;
	floor_base = 0;
	ceil_base = 0;
	sprite_base = 0;
	tt = 0.0;
	ex = null;
	ey = null;
	et = null;
	es = null;
	est = null;
	stx = null;
	sty = null;
	drawn = null;
	zcol = null;
	gun = null;
	ne = 11;
	kills = 0;
	cool = 0.6;
	muzzle_t = 0.0;
	tracer_t = 0.0;
	hot_t = 0.0;
	tr_x = 0;
	tr_y = 0;

	constructor() {
		wmap = [];
		wpx = [];
		wpy = [];
		seglen = [];
		tex = [];
		ex = [];
		ey = [];
		et = [];
		es = [];
		est = [];
		stx = [];
		sty = [];
		drawn = [];
		zcol = [];
		gun = [];
	}

	function build() {
		name = "KINSTEIN 3D";
		handle = "grublin keep . one ray per column, zero mercy";
		local rows = [
			"##################BBBB##",
			"#......##########......#",
			"#......................#",
			"#......MMMMMMMMMM......#",
			"#...G..##########......#",
			"##.##############......#",
			"##.##################.##",
			"#M.M#################.##",
			"##...B###############.##",
			"##.#################M.M#",
			"##.#######MMMMMM#####.##",
			"##.#######......#####.##",
			"##.#######............##",
			"##.#######...G..########",
			"##.#######......########",
			"##.#######MM.MMM########",
			"##.#########.###########",
			"##.#########.###########",
			"#......#####.###########",
			"#..G.G.#####.###########",
			"#......#####.###########",
			"#............###########",
			"#......#################",
			"##BBBB##################"
		];
		for (local y = 0; y < mh; ++y) {
			local row = rows[y];
			for (local x = 0; x < mw; ++x) {
				local ch = row.slice(x, x + 1);
				local v = 0;
				if (ch == "#") { v = 1; }
				else if (ch == "M") { v = 2; }
				else if (ch == "G") { v = 3; }
				else if (ch == "B") { v = 4; }
				wmap.push(v);
			}
		}
		local fogc = [9.0, 9.0, 16.0];
		local wall_cols = [
			[[40.0, 36.0, 44.0], [88.0, 88.0, 100.0], [104.0, 104.0, 118.0], [120.0, 120.0, 136.0], [138.0, 138.0, 152.0], [156.0, 156.0, 170.0], [186.0, 186.0, 200.0], [70.0, 70.0, 82.0]],
			[[34.0, 36.0, 34.0], [84.0, 90.0, 92.0], [100.0, 106.0, 108.0], [118.0, 124.0, 126.0], [60.0, 118.0, 50.0], [84.0, 150.0, 62.0], [120.0, 190.0, 80.0], [40.0, 86.0, 36.0]],
			[[90.0, 60.0, 20.0], [140.0, 100.0, 36.0], [176.0, 130.0, 50.0], [206.0, 160.0, 64.0], [232.0, 190.0, 90.0], [252.0, 222.0, 130.0], [255.0, 246.0, 200.0], [118.0, 80.0, 28.0]]];
		wall_base = pal.size();
		for (local t = 0; t < 3; ++t) {
			local cols = wall_cols[t];
			for (local side = 0; side < 2; ++side) {
				for (local f = 0; f < 8; ++f) {
					for (local c = 0; c < 8; ++c) {
						local cc = cols[c];
						fogged(cc[0], cc[1], cc[2], f, side, fogc);
					}
				}
			}
		}
		banner_base = pal.size();
		for (local side2 = 0; side2 < 2; ++side2) {
			for (local f2 = 0; f2 < 8; ++f2) {
				for (local hu = 0; hu < 4; ++hu) {
					local rc = ::gfx.rainbow_rgb(hu * 1.5708);
					local rr = rc[0], rg = rc[1], rb = rc[2];
					fogged(40.0, 16.0, 60.0, f2, side2, fogc);
					fogged(rr * 0.45, rg * 0.45, rb * 0.45, f2, side2, fogc);
					fogged(rr, rg, rb, f2, side2, fogc);
					fogged(255.0, 196.0, 80.0, f2, side2, fogc);
				}
			}
		}
		local floor_cols = [[46.0, 38.0, 34.0], [92.0, 80.0, 70.0], [104.0, 92.0, 80.0], [116.0, 102.0, 88.0], [128.0, 112.0, 96.0], [140.0, 124.0, 106.0], [76.0, 66.0, 58.0], [160.0, 142.0, 120.0]];
		floor_base = pal.size();
		for (local f3 = 0; f3 < 8; ++f3) {
			for (local c3 = 0; c3 < 8; ++c3) { local fc = floor_cols[c3]; fogged(fc[0], fc[1], fc[2], f3, 0, fogc); }
		}
		local ceil_cols = [[20.0, 12.0, 8.0], [60.0, 38.0, 22.0], [72.0, 46.0, 26.0], [84.0, 54.0, 30.0], [96.0, 62.0, 34.0], [50.0, 32.0, 18.0], [110.0, 72.0, 40.0], [36.0, 24.0, 14.0]];
		ceil_base = pal.size();
		for (local f4 = 0; f4 < 8; ++f4) {
			for (local c4 = 0; c4 < 8; ++c4) { local kc = ceil_cols[c4]; fogged(kc[0] * 0.8, kc[1] * 0.8, kc[2] * 0.8, f4, 0, fogc); }
		}
		sprite_base = pal.size();
		pal.ramp(12, 10.0, 34.0, 12.0, 140.0, 240.0, 110.0);
		pal.ramp(12, 36.0, 16.0, 10.0, 230.0, 140.0, 80.0);
		pal.add(255, 255, 255);
		pal.add(250, 246, 220);
		pal.add(20, 8, 8);
		pal.ramp(4, 36.0, 38.0, 48.0, 150.0, 156.0, 176.0);
		pal.add(255, 250, 200);
		pal.add(255, 196, 70);
		pal.add(255, 110, 30);
		for (local tr = 0; tr < 6; ++tr) {
			local ct = ::gfx.rainbow_rgb(tr * 1.0471975512);
			pal.add(ct[0], ct[1], ct[2]);
		}
		pal.add(130, 255, 150);
		pal.add(255, 84, 64);
		pal.add(255, 226, 100);
		pal.add(255, 140, 50);
		pal.add(8, 6, 10);
		pal.add(170, 30, 30);
		pal.add(226, 170, 128);
		pal.add(150, 100, 72);
		grow_textures();
		gun = ["......dlhd......", "......dlmd......", "......dlmd......", ".....ddlmdd.....", ".....dlhmmd.....",
			"....ddlmmmdd....", "....dlhmmmmd....", "...kdlmmmmmdk...", "..kkdmmmmmmdkk..", ".kkkkdmmmmdkkkk.",
			"skkkkkddddkkkkks", "sskkkkkkkkkkkkss"];
		wpx = [2.5, 2.5, 12.5, 12.5, 21.5, 21.5];
		wpy = [2.5, 21.5, 21.5, 12.5, 12.5, 2.5];
		for (local i = 0; i < nwp; ++i) {
			local ddx = wpx[(i + 1) % nwp] - wpx[i];
			local ddy = wpy[(i + 1) % nwp] - wpy[i];
			seglen.push(::sqrt(ddx * ddx + ddy * ddy));
			path_len += seglen[i];
		}
		ex = [2.5, 2.5, 2.5, 4.5, 5.5, 12.5, 12.5, 14.5, 18.5, 21.5, 20.5];
		ey = [6.5, 12.5, 16.5, 19.5, 21.5, 18.5, 13.5, 12.5, 12.5, 8.5, 2.5];
		et = [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1];
		for (local i2 = 0; i2 < ne; ++i2) {
			es.push(0);
			est.push(0.0);
			stx.push(0.0);
			sty.push(0.0);
			drawn.push(0);
		}
		zcol = ::gfx.make_fgrid(w);
		for (local i3 = 0; i3 < w; ++i3) { zcol[i3] = 99.0; }
	}

	// one entry: a color fogged toward the keep's gloom at fog level f (7 = near)
	function fogged(r, g, b, f, side, fogc) {
		local k = (0.10 + 0.90 * f / 7.0) * (side == 1 ? 0.72 : 1.0);
		local fr = fogc[0], fg = fogc[1], fb = fogc[2];
		pal.add(r * k + fr * (1.0 - k), g * k + fg * (1.0 - k), b * k + fb * (1.0 - k));
	}

	function hash2(a, b) {
		local hs = (a * 73856093 + b * 19349663 + (seed & 255) * 83492791) & 1048575;
		return ((hs * 1274126177) >> 12) & 1023;
	}

	function grow_textures() {
		for (local t = 0; t < 5; ++t) {
			for (local v = 0; v < 16; ++v) {
				for (local u = 0; u < 16; ++u) {
					local c = 0;
					if (t <= 1) {
						local row = v / 4;
						local bu = (u + (row % 2) * 4) % 8;
						local bv = v % 4;
						local brick = hash2((u + (row % 2) * 4) / 8 + row * 7, t);
						c = 1 + brick % 4;
						if (bv == 1) { c = c + 1; }
						if (bv == 3 && c > 1) { c = c - 1; }
						if (bu == 0 || bv == 0) { c = 0; }
						if (t == 1 && c != 0) {
							local m = hash2(u * 3 + v, v * 5 + 11) % 100;
							if (m < v * 5) { c = 4 + m % 3; }
						}
						if (t == 1 && c == 0 && hash2(u, v) % 3 == 0) { c = 7; }
					} else if (t == 2) {
						local fl = u % 4;
						c = fl == 0 ? 7 : (fl == 1 ? 5 : (fl == 2 ? 4 : 2));
						if (v % 8 == 0) { c = 6; }
						if (v % 8 == 1) { c = 1; }
					} else if (t == 3) {
						local su = (u + (v / 8) * 4) % 8;
						local sv = v % 8;
						local stone = hash2((u + (v / 8) * 4) / 8, v / 8 + 40);
						c = 1 + stone % 5;
						if (sv == 1 || su == 1) { c = 7; }
						if (su == 0 || sv == 0) { c = 0; }
						if (hash2(u + 50, v) % 11 == 0 && c != 0) { c = 6; }
					} else {
						local plank = v / 4;
						local pv = v % 4;
						c = 2 + (hash2(plank, 90) % 3);
						if ((u + plank * 5) % 16 == 0) { c = 0; }
						if (pv == 0) { c = 5; }
						if (pv == 3) { c = 7; }
						if (hash2(u, v + 90) % 13 == 0) { c = 6; }
					}
					tex.push(c);
				}
			}
		}
	}

	function path_at(s) {
		s = s - ::floor(s / path_len) * path_len;
		for (local i = 0; i < nwp; ++i) {
			local L = seglen[i];
			if (s <= L) {
				local f = L > 0.0001 ? s / L : 0.0;
				local j = (i + 1) % nwp;
				return [wpx[i] + (wpx[j] - wpx[i]) * f, wpy[i] + (wpy[j] - wpy[i]) * f];
			}
			s -= L;
		}
		return [wpx[0], wpy[0]];
	}

	// DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
	function los_clear(dx, dy, frac) {
		local mx = posx.tointeger();
		local my = posy.tointeger();
		local adx = dx < 0.0 ? -dx : dx;
		local ady = dy < 0.0 ? -dy : dy;
		local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		local stepx = dx < 0.0 ? -1 : 1;
		local stepy = dy < 0.0 ? -1 : 1;
		local sdx = dx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		local sdy = dy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		for (local g = 0; g < 64; ++g) {
			local trav = sdx < sdy ? sdx : sdy;
			if (trav >= frac) { return true; }
			if (sdx < sdy) { sdx += ddx; mx += stepx; }
			else { sdy += ddy; my += stepy; }
			local cv = wmap[my * mw + mx];
			if (cv > 0) { return false; }
		}
		return false;
	}

	function update(t, dt) {
		tt = t;
		local s = t * 2.4;
		local p = path_at(s);
		local q = path_at(s + 1.7);
		posx = p[0];
		posy = p[1];
		local dx = q[0] - posx;
		local dy = q[1] - posy;
		local dl = ::sqrt(dx * dx + dy * dy);
		if (dl > 0.0001) { dirx = dx / dl; diry = dy / dl; }
		planex = 0.0 - diry * 0.66;
		planey = dirx * 0.66;
		cool -= dt;
		muzzle_t -= dt;
		tracer_t -= dt;
		hot_t -= dt;
		for (local i = 0; i < ne; ++i) {
			local st = es[i];
			if (st == 1) { est[i] = est[i] + dt; if (est[i] > 0.14) { es[i] = 2; est[i] = 0.0; } }
			else if (st == 2) { est[i] = est[i] + dt; if (est[i] > 0.55) { es[i] = 3; est[i] = 0.0; } }
			else if (st == 3) { est[i] = est[i] + dt; if (est[i] > 16.0) { es[i] = 0; est[i] = 0.0; } }
		}
		if (cool <= 0.0) {
			local invdet = 1.0 / (planex * diry - dirx * planey);
			local best = -1;
			local besty = 99.0;
			local bestx = 0.0;
			for (local i2 = 0; i2 < ne; ++i2) {
				local st2 = es[i2];
				if (st2 != 0) { continue; }
				local rx = ex[i2] - posx;
				local ry = ey[i2] - posy;
				local txx = invdet * (diry * rx - dirx * ry);
				local tyy = invdet * (planex * ry - planey * rx);
				if (tyy < 1.0 || tyy > 4.2) { continue; }
				local aim = txx < 0.0 ? -txx : txx;
				if (aim > tyy * 0.14) { continue; }
				if (!los_clear(rx, ry, 0.95)) { continue; }
				if (tyy < besty) { best = i2; besty = tyy; bestx = txx; }
			}
			if (best >= 0) {
				es[best] = 1;
				est[best] = 0.0;
				kills = kills + 1;
				cool = 0.7;
				muzzle_t = 0.12;
				tracer_t = 0.14;
				hot_t = 0.35;
				tr_x = (w * 0.5 * (1.0 + bestx / besty)).tointeger();
				tr_y = h / 2 + (h * 0.10 / besty).tointeger();
			}
		}
	}

	// floor and ceiling, cast per pixel along the camera plane
	function cast_planes(grid) {
		local ww = w;
		local hh = h;
		local half = hh / 2;
		local tx = tex;
		local fb = floor_base;
		local cb = ceil_base;
		local rdx0 = dirx - planex;
		local rdy0 = diry - planey;
		local rdx1 = dirx + planex;
		local rdy1 = diry + planey;
		for (local y = half; y < hh; ++y) {
			local p = y + 0.5 - hh * 0.5;
			local rowd = hh * 0.5 / p;
			local stx_ = rowd * (rdx1 - rdx0) / ww;
			local sty_ = rowd * (rdy1 - rdy0) / ww;
			local fx = posx + rowd * rdx0 + 64.0;
			local fy = posy + rowd * rdy0 + 64.0;
			local fog = (19.2 / (rowd + 1.4)).tointeger();
			fog = fog > 7 ? 7 : fog;
			local frow = fb + fog * 8;
			local crow = cb + fog * 8;
			local gf = y * ww;
			local gc = (hh - 1 - y) * ww;
			for (local x = 0; x < ww; ++x) {
				local iu = (fx * 16.0).tointeger();
				local iv = (fy * 16.0).tointeger();
				local ti = (iv & 15) * 16 + (iu & 15);
				grid[gf + x] = frow + tx[768 + ti];
				grid[gc + x] = crow + tx[1024 + ti];
				fx += stx_;
				fy += sty_;
			}
		}
	}

	// one billboard: an ellipsoid body lit as a sphere per pixel, eyes, a grin
	function draw_blob(grid, i) {
		local tyy = sty[i];
		local txx = stx[i];
		local scx = (w * 0.5 * (1.0 + txx / tyy)).tointeger();
		local typ = et[i];
		local size = typ == 1 ? 1.1 : 0.78;
		local sh = (h * size / tyy).tointeger();
		sh = sh < 3 ? 3 : sh;
		sh = sh > 64 ? 64 : sh;
		local hwm = (sh + (typ == 1 ? 6.0 : 2.0)) * 0.5;
		local feet = h / 2 + (h * 0.5 / tyy).tointeger();
		local top = feet - sh;
		local wob = ::sin(tt * 5.0 + i * 2.1) * 0.10;
		local flash = es[i] == 1;
		local body = sprite_base + typ * 12;
		local fogk = 1.1 - tyy * 0.06;
		fogk = fogk < 0.45 ? 0.45 : (fogk > 1.0 ? 1.0 : fogk);
		for (local dy = 0; dy < sh; ++dy) {
			local rr = top + dy;
			if (rr < 0 || rr >= h) { continue; }
			local v = (dy + 0.5) / sh * 2.0 - 1.0;
			local vv = 1.0 - v * v;
			if (vv <= 0.0) { continue; }
			local hwf = ::sqrt(vv) * hwm;
			local hw = hwf.tointeger();
			local cxr = scx + (wob * v * hwm).tointeger();
			local rowb = rr * w;
			for (local dx = 0 - hw; dx <= hw; ++dx) {
				local col = cxr + dx;
				if (col < 0 || col >= w) { continue; }
				if (tyy >= zcol[col]) { continue; }
				local pi = sprite_base + 24;
				if (!flash) {
					local nx = dx / (hwm + 0.5);
					local q = 1.0 - nx * nx - v * v;
					local nz = q > 0.0 ? ::sqrt(q) : 0.0;
					local li = -0.45 * nx - 0.55 * v + 0.70 * nz;
					li = li < 0.0 ? 0.0 : li;
					local lv = ((0.12 + 0.88 * li) * fogk * 11.99).tointeger();
					pi = body + lv;
					local ax = nx < 0.0 ? -nx : nx;
					if (v > -0.5 && v < -0.12 && ax > 0.14 && ax < 0.52) {
						local ey2 = (v + 0.31) * 5.0;
						local ex2 = (ax - 0.33) * 5.0;
						if (ex2 * ex2 + ey2 * ey2 < 1.0) {
							pi = ex2 * ex2 + ey2 * ey2 < 0.3 ? sprite_base + 26 : sprite_base + 25;
						}
					}
					if (v > 0.18 && v < 0.34 && ax < 0.42) {
						pi = (dx & 1) == 0 && v < 0.24 ? sprite_base + 25 : sprite_base + 45;
					}
				}
				grid[rowb + col] = pi;
			}
		}
	}

	function render(grid) {
		cast_planes(grid);
		local ww = w;
		local hh = h;
		local wm = wmap;
		local tx = tex;
		local zc = zcol;
		local wb = wall_base;
		local bb = banner_base;
		local mw_ = mw;
		local px = posx, py = posy, dx_ = dirx, dy_ = diry, plx = planex, ply = planey;
		local tshift = (tt * 14.0).tointeger();
		for (local x = 0; x < ww; ++x) {
			local camx = 2.0 * x / (ww - 1.0) - 1.0;
			local rdx = dx_ + plx * camx;
			local rdy = dy_ + ply * camx;
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
			local hitv = 0;
			local guard = 0;
			while (hitv == 0 && guard < 64) {
				if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
				else { sdy += ddy; my += stepy; side = 1; }
				hitv = wm[my * mw_ + mx];
				guard = guard + 1;
			}
			hitv = hitv == 0 ? 1 : hitv;
			local pdist = side == 0 ? sdx - ddx : sdy - ddy;
			pdist = pdist < 0.05 ? 0.05 : pdist;
			zc[x] = pdist;
			local wallx = side == 0 ? py + pdist * rdy : px + pdist * rdx;
			wallx = wallx - ::floor(wallx);
			local tu = (wallx * 16.0).tointeger() & 15;
			local lineh = hh / pdist;
			local topf = (hh - lineh) * 0.5;
			local y0 = topf.tointeger();
			local y1 = (topf + lineh).tointeger();
			y0 = y0 < 0 ? 0 : y0;
			y1 = y1 > hh ? hh : y1;
			local fog = (19.2 / (pdist + 1.4)).tointeger();
			fog = fog > 7 ? 7 : fog;
			local tstep = 16.0 / lineh;
			local tpos = (y0 + 0.5 - topf) * tstep;
			local gi = y0 * ww + x;
			if (hitv == 4) {
				local brow = bb + (side * 8 + fog) * 16;
				local wave = (tu * 3 + tshift) % 12;
				local flen = 6 + (wave < 6 ? wave : 12 - wave);
				local bank = (((tu >> 2) + (tshift >> 3)) & 3) * 4;
				for (local y = y0; y < y1; ++y) {
					local tv = tpos.tointeger();
					local up = 15 - (tv & 15);
					local heat = up > flen ? 0 : (up * 3 > flen * 2 ? 1 : (up * 3 > flen ? 2 : 3));
					grid[gi] = brow + bank + heat;
					tpos += tstep;
					gi += ww;
				}
			} else {
				local wrow = wb + (((hitv - 1) * 2 + side) * 8 + fog) * 8;
				local tbase = (hitv - 1) * 256 + tu;
				for (local y2 = y0; y2 < y1; ++y2) {
					local tv2 = tpos.tointeger();
					grid[gi] = wrow + tx[tbase + (tv2 & 15) * 16];
					tpos += tstep;
					gi += ww;
				}
			}
		}
		// sprite pass: camera-space transforms, then paint far to near
		local invdet = 1.0 / (planex * diry - dirx * planey);
		for (local i = 0; i < ne; ++i) {
			local rx = ex[i] - posx;
			local ry = ey[i] - posy;
			stx[i] = invdet * (diry * rx - dirx * ry);
			sty[i] = invdet * (planex * ry - planey * rx);
			drawn[i] = 0;
		}
		for (local pass = 0; pass < ne; ++pass) {
			local pick = -1;
			local pty = -1.0;
			for (local i2 = 0; i2 < ne; ++i2) {
				if (drawn[i2] == 1) { continue; }
				local st = es[i2];
				if (st >= 2) { continue; }
				local sv = sty[i2];
				if (sv <= 0.25) { continue; }
				if (sv > pty) { pty = sv; pick = i2; }
			}
			if (pick < 0) { break; }
			drawn[pick] = 1;
			draw_blob(grid, pick);
		}
		// gib scatter for the dying
		local flick = (tt * 10.0).tointeger();
		for (local i3 = 0; i3 < ne; ++i3) {
			local st3 = es[i3];
			if (st3 != 2) { continue; }
			local tyy = sty[i3];
			if (tyy < 0.25) { continue; }
			local prog = est[i3] * 1.9;
			local scx = (w * 0.5 * (1.0 + stx[i3] / tyy)).tointeger();
			local scy = h / 2 + (h * 0.22 / tyy).tointeger();
			local r = prog * (h * 0.45 / tyy);
			local body = sprite_base + et[i3] * 12 + 8;
			for (local k = 0; k < 18; ++k) {
				local a = k * 0.349 + i3;
				local gx = scx + (::cos(a) * r * (1.0 + (k % 3) * 0.3)).tointeger();
				local gy = scy + (::sin(a) * r + prog * prog * 10.0).tointeger();
				if (gx < 0 || gx >= w - 1 || gy < 1 || gy >= h - 1) { continue; }
				if (tyy >= zcol[gx]) { continue; }
				local gc = k % 4 == 0 ? sprite_base + 45 : body - (k % 3) * 2;
				grid[gy * w + gx] = gc;
				grid[gy * w + gx + 1] = gc;
			}
		}
		draw_hud(grid, flick);
	}

	function draw_hud(grid, flick) {
		local gx0 = w / 2;
		if (tracer_t > 0.0) {
			local y0t = h - 22;
			for (local k2 = 1; k2 < 28; ++k2) {
				local xx = gx0 + (tr_x - gx0) * k2 / 28;
				local yy = y0t + (tr_y - y0t) * k2 / 28;
				if (xx < 0 || xx >= w || yy < 1 || yy >= h) { continue; }
				grid[yy * w + xx] = sprite_base + 34 + (k2 / 2 + flick) % 6;
			}
		}
		local bob = (::sin(tt * 7.0) * 2.5).tointeger();
		local kick = muzzle_t > 0.0 ? 4 : 0;
		local gw = 16;
		local gh = gun.len();
		local gxs = gx0 - gw + bob;
		local gys = h - gh * 2 + 4 + kick + (bob < 0 ? 0 - bob : bob) / 2;
		for (local ry = 0; ry < gh * 2; ++ry) {
			local row = gun[ry / 2];
			local yy2 = gys + ry;
			if (yy2 < 0 || yy2 >= h) { continue; }
			for (local rx = 0; rx < gw * 2; ++rx) {
				local ch = row.slice(rx / 2, rx / 2 + 1);
				if (ch == ".") { continue; }
				local c = ch == "d" ? 27 : (ch == "m" ? 28 : (ch == "l" ? 29 : (ch == "h" ? 30 : (ch == "k" ? 46 : 47))));
				local xx2 = gxs + rx;
				if (xx2 >= 0 && xx2 < w) { grid[yy2 * w + xx2] = sprite_base + c; }
			}
		}
		if (muzzle_t > 0.0) {
			local mx = gxs + 15;
			local my = gys - 4;
			for (local q = -5; q <= 5; ++q) {
				local aq = q < 0 ? 0 - q : q;
				local c2 = aq <= 1 ? 31 : (aq <= 3 ? 32 : 33);
				for (local th = 0; th < 2; ++th) {
					if (my + q >= 0 && my + q < h) { grid[(my + q) * w + mx + th] = sprite_base + c2; }
					if (mx + q >= 0 && mx + q < w && my + th >= 0) { grid[(my + th) * w + mx + q] = sprite_base + c2; }
				}
				if (aq <= 3 && mx + q >= 0 && mx + q < w && my + q >= 0 && my + q < h) { grid[(my + q) * w + mx + q] = sprite_base + 32; }
				if (aq <= 3 && mx - q >= 0 && mx - q < w && my + q >= 0 && my + q < h) { grid[(my + q) * w + mx - q] = sprite_base + 32; }
			}
		}
		local chc = hot_t > 0.0 ? sprite_base + 41 : sprite_base + 40;
		local cy = h / 2;
		for (local d = 2; d <= 3; ++d) {
			grid[cy * w + gx0 - d] = chc;
			grid[cy * w + gx0 + d] = chc;
			grid[(cy - d) * w + gx0] = chc;
			grid[(cy + d) * w + gx0] = chc;
		}
		local tally = "KILLS " + (kills < 10 ? "0" : "") + kills;
		::gfx.draw_text(grid, w, h, tally, 2, 2, 1, 1, sprite_base + 42, 2, sprite_base + 44, 99);
	}
}
