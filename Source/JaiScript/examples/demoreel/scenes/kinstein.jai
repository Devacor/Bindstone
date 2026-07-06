// KINSTEIN 3D — a Wolfenstein-style grid raycaster: one DDA ray per screen
// column over an authored 24x24 map, perpendicular-distance wall slices (no
// fisheye by construction), N/S vs E/W face darkening, and floor/ceiling
// gradient bands. The camera flies a scripted loop: waypoints plus a look-ahead
// point on the same path, so turns steer themselves — no per-frame trig at all
// (the ray fan comes from the camera-plane vector, not angles). This is the
// reel's deliberate compute stress: ~100 DDA walks and 4,000 column writes per
// frame, all script. Walls are solid bg colors quantized to 8 shades, so slice
// columns are long same-escape runs — best case for the host VT filter.

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
	float tt = 0.0;

	void build() override {
		name = "KINSTEIN 3D";
		handle = "grublin keep . one ray per column, zero textures";
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
		// the flight loop: west corridor, bottom hall, mid room, east corridor
		wpx = [2.5, 2.5, 12.5, 12.5, 21.5, 21.5];
		wpy = [2.5, 21.5, 21.5, 12.5, 12.5, 2.5];
		for (int i = 0; i < nwp; ++i) {
			float ddx = wpx[(i + 1) % nwp] - wpx[i];
			float ddy = wpy[(i + 1) % nwp] - wpy[i];
			seglen.push(sqrt(ddx * ddx + ddy * ddy));
			path_len += seglen[i];
		}
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
				// declaration, not assignment: `hitv = wmap[i]` (typed-local slot store
				// from a bare element ref) throws Type mismatch on the interpreter at
				// current HEAD — see README language feedback
				int cellv = wmap[my * mw + mx];
				hitv = cellv;
				guard = guard + 1;
			}
			hitv = hitv == 0 ? 1 : hitv;
			float pdist = side == 0 ? sdx - ddx : sdy - ddy;   // perpendicular: no fisheye
			pdist = pdist < 0.08 ? 0.08 : pdist;
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
	}
}
