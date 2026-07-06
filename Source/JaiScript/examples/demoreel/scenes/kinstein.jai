// KINSTEIN 3D — a Wolfenstein-style grid raycaster: one DDA ray per screen
// column over an authored 24x24 map, perpendicular-distance wall slices (no
// fisheye by construction), N/S vs E/W face darkening, floor/ceiling gradient
// bands, and now a bestiary: billboard Grublins (small green blobs) and Orglis
// brutes stand along the route, occluded per column by the wall z-buffer and
// scaled by distance. The scripted camera FIGHTS them: when an alive enemy sits
// in the crosshair cone with clear line of sight, the gun auto-fires — muzzle
// flash, rainbow hitscan tracer, hit-flash, then a gib scatter and a kill tally
// on the scene HUD. Enemies respawn each lap so every loop is a fresh run.
// The camera flies waypoints plus a look-ahead point on the same path, so turns
// steer themselves — no per-frame trig (the ray fan comes from the camera-plane
// vector). This is the reel's deliberate compute stress: ~100 DDA walks, 4,000
// column writes, and a sprite pass per frame, all script. Walls are solid bg
// colors quantized to 8 shades — long same-escape runs, best case for the host
// VT filter.

class Kinstein : Scene {
	var wmap = [];
	int mw = 24;
	int mh = 24;
	var wpx = [];
	var wpy = [];
	var seglen = [];
	int nwp = 6;
	float path_len = 0.0;
	float posx = 2.5;
	float posy = 2.5;
	float dirx = 0.0;
	float diry = 1.0;
	float planex = 0.0;
	float planey = 0.0;
	int wall_base = 0;
	int banner_base = 0;
	int sprite_base = 0;
	float tt = 0.0;
	// the bestiary (parallel arrays): type 0 grublin, 1 orglis
	var ex = [];
	var ey = [];
	var et = [];
	var es = [];         // 0 alive, 1 hit-flash, 2 gibbing, 3 down (respawns)
	var est = [];
	var stx = [];        // per-frame camera-space transforms
	var sty = [];
	var drawn = [];
	var zcol = [];       // per-column wall depth from the last wall pass
	int ne = 11;
	int kills = 0;
	float cool = 0.6;
	float muzzle_t = 0.0;
	float tracer_t = 0.0;
	float hot_t = 0.0;
	int tr_x = 0;
	int tr_y = 0;

	void build() override {
		name = "KINSTEIN 3D";
		handle = "grublin keep . one ray per column, zero mercy";
		// # stone  M moss  G gold pillar  B banner (rainbow-fire, animated)
		var rows = [
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
		for (int y = 0; y < mh; ++y) {
			string row = rows[y];
			for (int x = 0; x < mw; ++x) {
				string ch = row.substr(x, 1);
				int v = 0;
				if (ch == "#") { v = 1; }
				else if (ch == "M") { v = 2; }
				else if (ch == "G") { v = 3; }
				else if (ch == "B") { v = 4; }
				wmap.push(v);
			}
		}
		// entries 0..h-1: per-row ceiling/floor gradient bands (entry y = row y)
		int mid = h / 2;
		for (int py = 0; py < h; ++py) {
			if (py < mid) {
				float f = 1.0 - py / (mid - 1.0);
				pal.add_bg(10.0 + 26.0 * f, 12.0 + 30.0 * f, 22.0 + 44.0 * f);
			} else {
				float f2 = (py - mid) / (h - 1.0 - mid);
				pal.add_bg(24.0 + 40.0 * f2, 21.0 + 32.0 * f2, 18.0 + 24.0 * f2);
			}
		}
		// walls: type x face x 8 distance shades, all solid backgrounds
		wall_base = h;
		var bases = [[152.0, 154.0, 172.0], [92.0, 170.0, 96.0], [212.0, 168.0, 74.0]];
		for (int t = 0; t < 3; ++t) {
			var bc = bases[t];
			float br = bc[0];
			float bg = bc[1];
			float bb = bc[2];
			for (int side = 0; side < 2; ++side) {
				float dk = side == 1 ? 0.66 : 1.0;
				for (int s = 0; s < 8; ++s) {
					float f3 = (0.14 + 0.86 * s / 7.0) * dk;
					pal.add_bg(br * f3, bg * f3, bb * f3);
				}
			}
		}
		// banner: four rainbow-fire hue banks, cycled by time in render
		banner_base = wall_base + 48;
		for (int bank = 0; bank < 4; ++bank) {
			for (int side2 = 0; side2 < 2; ++side2) {
				float dk2 = side2 == 1 ? 0.66 : 1.0;
				for (int s2 = 0; s2 < 8; ++s2) {
					var c = gfx.rainbow_rgb(bank * 1.5707963268 + s2 * 0.33);
					float f4 = (0.18 + 0.82 * s2 / 7.0) * dk2;
					pal.add_bg(c[0] * f4, c[1] * f4, c[2] * f4);
				}
			}
		}
		// sprites + combat kit
		sprite_base = banner_base + 64;
		pal.add_bg(26, 74, 30);                           // +0..3 grublin, far -> near
		pal.add_bg(38, 104, 42);
		pal.add_bg(52, 138, 54);
		pal.add_bg(70, 178, 66);
		pal.add_bg(74, 40, 26);                           // +4..7 orglis, far -> near
		pal.add_bg(104, 54, 32);
		pal.add_bg(140, 70, 38);
		pal.add_bg(178, 90, 44);
		pal.add_fg_bg(255, 232, 90, 52, 138, 54, "o");    // +8 grublin eye
		pal.add_fg_bg(255, 140, 40, 140, 70, 38, "o");    // +9 orglis eye
		pal.add_bg(255, 255, 255);                        // +10 hit flash
		pal.add_bg(58, 58, 70);                           // +11 gun dark
		pal.add_bg(96, 96, 112);                          // +12 gun lite
		pal.add_bg(255, 244, 150);                        // +13 muzzle core
		pal.add_bg(255, 176, 48);                         // +14 muzzle rim
		for (int tr = 0; tr < 6; ++tr) {                  // +15..20 rainbow tracer
			var ct = gfx.rainbow_rgb(tr * 1.0471975512);
			pal.add_bg(ct[0], ct[1], ct[2]);
		}
		pal.add_fg_bg(130, 255, 150, 8, 10, 20, "+");     // +21 crosshair
		pal.add_fg_bg(255, 84, 64, 8, 10, 20, "+");       // +22 crosshair, target down
		pal.add_fg_bg(255, 208, 70, 10, 8, 24, "x");      // +23 kill tally
		// the flight loop: west corridor, bottom hall, mid room, east corridor
		wpx = [2.5, 2.5, 12.5, 12.5, 21.5, 21.5];
		wpy = [2.5, 21.5, 21.5, 12.5, 12.5, 2.5];
		for (int i = 0; i < nwp; ++i) {
			float ddx = wpx[(i + 1) % nwp] - wpx[i];
			float ddy = wpy[(i + 1) % nwp] - wpy[i];
			seglen.push(sqrt(ddx * ddx + ddy * ddy));
			path_len += seglen[i];
		}
		// garrison, posted along the route (a couple wait around corners)
		ex = [2.5, 2.5, 2.5, 4.5, 5.5, 12.5, 12.5, 14.5, 18.5, 21.5, 20.5];
		ey = [6.5, 12.5, 16.5, 19.5, 21.5, 18.5, 13.5, 12.5, 12.5, 8.5, 2.5];
		et = [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1];
		for (int i2 = 0; i2 < ne; ++i2) {
			es.push(0);
			est.push(0.0);
			stx.push(0.0);
			sty.push(0.0);
			drawn.push(0);
		}
		zcol = gfx.make_grid(w);
		for (int i3 = 0; i3 < w; ++i3) { zcol[i3] = 99.0; }
	}

	var path_at(float s) {
		s = s - floor(s / path_len) * path_len;
		for (int i = 0; i < nwp; ++i) {
			float L = seglen[i];
			if (s <= L) {
				float f = L > 0.0001 ? s / L : 0.0;
				int j = (i + 1) % nwp;
				return [wpx[i] + (wpx[j] - wpx[i]) * f, wpy[i] + (wpy[j] - wpy[i]) * f];
			}
			s -= L;
		}
		return [wpx[0], wpy[0]];
	}

	// DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
	bool los_clear(float dx, float dy, float frac) {
		int mx = itrunc(posx);
		int my = itrunc(posy);
		float adx = dx < 0.0 ? -dx : dx;
		float ady = dy < 0.0 ? -dy : dy;
		float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		int stepx = dx < 0.0 ? -1 : 1;
		int stepy = dy < 0.0 ? -1 : 1;
		float sdx = dx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		float sdy = dy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		for (int g = 0; g < 64; ++g) {
			float trav = sdx < sdy ? sdx : sdy;
			if (trav >= frac) { return true; }
			if (sdx < sdy) { sdx += ddx; mx += stepx; }
			else { sdy += ddy; my += stepy; }
			int cv = wmap[my * mw + mx];
			if (cv > 0) { return false; }
		}
		return false;
	}

	void update(float t, float dt) override {
		tt = t;
		float s = t * 2.4;
		var p = path_at(s);
		var q = path_at(s + 1.7);   // look-ahead steers the turns
		posx = p[0];
		posy = p[1];
		float dx = q[0] - posx;
		float dy = q[1] - posy;
		float dl = sqrt(dx * dx + dy * dy);
		if (dl > 0.0001) { dirx = dx / dl; diry = dy / dl; }
		planex = -diry * 0.66;
		planey = dirx * 0.66;
		// enemy state machine
		cool -= dt;
		muzzle_t -= dt;
		tracer_t -= dt;
		hot_t -= dt;
		for (int i = 0; i < ne; ++i) {
			int st = es[i];
			if (st == 1) { est[i] = est[i] + dt; if (est[i] > 0.14) { es[i] = 2; est[i] = 0.0; } }
			else if (st == 2) { est[i] = est[i] + dt; if (est[i] > 0.55) { es[i] = 3; est[i] = 0.0; } }
			else if (st == 3) { est[i] = est[i] + dt; if (est[i] > 16.0) { es[i] = 0; est[i] = 0.0; } }
		}
		// auto-fire: nearest alive enemy in the crosshair cone with clear LOS
		if (cool <= 0.0) {
			float invdet = 1.0 / (planex * diry - dirx * planey);
			int best = -1;
			float besty = 99.0;
			float bestx = 0.0;
			for (int i2 = 0; i2 < ne; ++i2) {
				int st2 = es[i2];
				if (st2 != 0) { continue; }
				float rx = ex[i2] - posx;
				float ry = ey[i2] - posy;
				float txx = invdet * (diry * rx - dirx * ry);
				float tyy = invdet * (planex * ry - planey * rx);
				if (tyy < 1.0 || tyy > 9.0) { continue; }
				float aim = txx < 0.0 ? -txx : txx;
				if (aim > tyy * 0.14) { continue; }
				if (!los_clear(rx, ry, 0.95)) { continue; }
				if (tyy < besty) { best = i2; besty = tyy; bestx = txx; }
			}
			if (best >= 0) {
				es[best] = 1;
				est[best] = 0.0;
				kills = kills + 1;
				cool = 0.55;
				muzzle_t = 0.12;
				tracer_t = 0.14;
				hot_t = 0.35;
				tr_x = itrunc(w * 0.5 * (1.0 + bestx / besty));
				tr_y = h / 2 + itrunc(h * 0.10 / besty);
			}
		}
	}

	// one billboard blob: ellipse body, eyes, distance shade, hit-flash white.
	// Row-major with a per-row half-width (one sqrt per row) — no per-cell float
	// math, the inner loop is bounds + z-occlusion + write.
	void draw_blob(var& grid, int i) {
		float tyy = sty[i];
		float txx = stx[i];
		int scx = itrunc(w * 0.5 * (1.0 + txx / tyy));
		int typ = et[i];
		float size = typ == 1 ? 0.95 : 0.62;
		int sh = itrunc(h * size / tyy);
		sh = sh < 2 ? 2 : sh;
		sh = sh > 32 ? 32 : sh;
		int hw_max = (sh + (typ == 1 ? 4 : 1)) / 2;
		int feet = h / 2 + itrunc(h * 0.5 / tyy);
		int top = feet - sh;
		float wob = sin(tt * 5.0 + i * 2.1) * 0.10;   // idle wobble (sways per row)
		int shade = 3 - itrunc(tyy * 0.34);
		shade = shade < 0 ? 0 : shade;
		shade = shade > 3 ? 3 : shade;
		bool flash = es[i] == 1;
		int body = flash ? sprite_base + 10 : sprite_base + typ * 4 + shade;
		int eye = sprite_base + 8 + typ;
		for (int dy = 0; dy < sh; ++dy) {
			int rr = top + dy;
			if (rr < 0 || rr >= h) { continue; }
			float v = (dy + 0.5) / sh * 2.0 - 1.0;
			float vv = 1.0 - v * v * 1.15;
			if (vv <= 0.0) { continue; }
			int hw = itrunc(sqrt(vv) * hw_max);
			int cxr = scx + itrunc(wob * v * hw_max);
			int e0 = hw_max + 1;
			int e1 = 0;
			if (!flash && v > -0.4 && v < -0.05) {
				e0 = itrunc(0.2 * hw_max);
				e1 = itrunc(0.5 * hw_max);
			}
			int rowb = rr * w;
			for (int dx = 0 - hw; dx <= hw; ++dx) {
				int col = cxr + dx;
				if (col < 0 || col >= w) { continue; }
				if (tyy >= zcol[col]) { continue; }   // walls occlude per column
				int pi = body;
				int adx = dx < 0 ? 0 - dx : dx;
				if (adx >= e0 && adx <= e1) { pi = eye; }
				grid[rowb + col] = pi;
			}
		}
	}

	void render(var& grid) override {
		int bbase = banner_base + (itrunc(tt * 2.0) % 4) * 16;
		for (int x = 0; x < w; ++x) {
			float camx = 2.0 * x / (w - 1.0) - 1.0;
			float rdx = dirx + planex * camx;
			float rdy = diry + planey * camx;
			int mx = itrunc(posx);
			int my = itrunc(posy);
			float adx = rdx < 0.0 ? -rdx : rdx;
			float ady = rdy < 0.0 ? -rdy : rdy;
			float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
			float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
			int stepx = rdx < 0.0 ? -1 : 1;
			int stepy = rdy < 0.0 ? -1 : 1;
			float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
			float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
			int side = 0;
			int hitv = 0;
			int guard = 0;
			while (hitv == 0 && guard < 64) {
				if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
				else { sdy += ddy; my += stepy; side = 1; }
				// declaration, not assignment: `hitv = wmap[i]` (typed-local slot
				// store from a bare element ref) throws Type mismatch on the
				// interpreter at current HEAD — see README language feedback
				int cellv = wmap[my * mw + mx];
				hitv = cellv;
				guard = guard + 1;
			}
			hitv = hitv == 0 ? 1 : hitv;
			float pdist = side == 0 ? sdx - ddx : sdy - ddy;   // perpendicular: no fisheye
			pdist = pdist < 0.08 ? 0.08 : pdist;
			zcol[x] = pdist;
			int lineh = itrunc(h / pdist);
			int y0 = (h - lineh) / 2;
			int y1 = y0 + lineh;
			y0 = y0 < 0 ? 0 : y0;
			y1 = y1 > h ? h : y1;
			int s = itrunc(19.2 / (pdist + 1.4));
			s = s > 7 ? 7 : s;
			int pi = hitv == 4 ? bbase + side * 8 + s : wall_base + (hitv - 1) * 16 + side * 8 + s;
			int gi = x;
			int y = 0;
			while (y < y0) { grid[gi] = y; gi += w; y = y + 1; }
			while (y < y1) { grid[gi] = pi; gi += w; y = y + 1; }
			while (y < h) { grid[gi] = y; gi += w; y = y + 1; }
		}
		// sprite pass: camera-space transforms, then paint far to near
		float invdet = 1.0 / (planex * diry - dirx * planey);
		for (int i = 0; i < ne; ++i) {
			float rx = ex[i] - posx;
			float ry = ey[i] - posy;
			stx[i] = invdet * (diry * rx - dirx * ry);
			sty[i] = invdet * (planex * ry - planey * rx);
			drawn[i] = 0;
		}
		for (int pass = 0; pass < ne; ++pass) {
			int pick = -1;
			float pty = -1.0;
			for (int i2 = 0; i2 < ne; ++i2) {
				if (drawn[i2] == 1) { continue; }
				int st = es[i2];
				if (st >= 2) { continue; }
				float sv = sty[i2];
				if (sv <= 0.25) { continue; }
				if (sv > pty) { pty = sv; pick = i2; }
			}
			if (pick < 0) { break; }
			drawn[pick] = 1;
			draw_blob(grid, pick);
		}
		// gib scatter for the dying
		int flick = itrunc(tt * 10.0);
		for (int i3 = 0; i3 < ne; ++i3) {
			int st3 = es[i3];
			if (st3 != 2) { continue; }
			float tyy = sty[i3];
			if (tyy < 0.25) { continue; }
			float prog = est[i3] * 1.9;
			int scx = itrunc(w * 0.5 * (1.0 + stx[i3] / tyy));
			int scy = h / 2 + itrunc(h * 0.22 / tyy);
			float r = prog * (h * 0.45 / tyy);
			int body = sprite_base + et[i3] * 4 + 3;
			for (int k = 0; k < 10; ++k) {
				float a = k * 0.628 + i3;
				int gx = scx + itrunc(cos(a) * r * 2.0);
				int gy = scy + itrunc(sin(a) * r + prog * prog * 6.0);
				if (gx < 0 || gx >= w || gy < 1 || gy >= h) { continue; }
				if (tyy >= zcol[gx]) { continue; }
				grid[gy * w + gx] = k % 3 == 0 ? sprite_base + 8 + et[i3] : body;
			}
		}
		// rainbow hitscan tracer, gun, muzzle flash, crosshair, kill tally
		int gx0 = w / 2;
		if (tracer_t > 0.0) {
			int y0t = h - 4;
			for (int k2 = 1; k2 < 14; ++k2) {
				int xx = gx0 + (tr_x - gx0) * k2 / 14;
				int yy = y0t + (tr_y - y0t) * k2 / 14;
				if (xx < 0 || xx >= w || yy < 1 || yy >= h) { continue; }
				grid[yy * w + xx] = sprite_base + 15 + (k2 + flick) % 6;
			}
		}
		grid[(h - 1) * w + gx0 - 1] = sprite_base + 11;
		grid[(h - 1) * w + gx0] = sprite_base + 12;
		grid[(h - 1) * w + gx0 + 1] = sprite_base + 11;
		grid[(h - 2) * w + gx0] = sprite_base + 11;
		if (muzzle_t > 0.0) {
			grid[(h - 3) * w + gx0] = sprite_base + 13;
			grid[(h - 3) * w + gx0 - 1] = sprite_base + 14;
			grid[(h - 3) * w + gx0 + 1] = sprite_base + 14;
			grid[(h - 4) * w + gx0] = sprite_base + 14;
		}
		int chc = hot_t > 0.0 ? sprite_base + 22 : sprite_base + 21;
		grid[(h / 2) * w + gx0] = chc;
		grid[(h / 2) * w + gx0 - 3] = chc;
		grid[(h / 2) * w + gx0 + 3] = chc;
		int nk = kills > 24 ? 24 : kills;
		for (int k3 = 0; k3 < nk; ++k3) { grid[w + 2 + k3] = sprite_base + 23; }
	}
}
