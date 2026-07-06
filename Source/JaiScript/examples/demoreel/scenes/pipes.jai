// PIPEDREAM — the 3D Pipes screensaver homage. Four pipes race through a
// 16x12x16 voxel volume with a straight bias, ball joints at every turn, and a
// slowly orbiting camera; the volume fills to a threshold, holds, fades, and
// reseeds — forever, like the real thing. Canon demands the teapot: ~2% of
// joints instead spawn the jackodile's eye, a one-cell rainbow flicker.

class Pipes : Scene {
	var cells = [];      // 0 empty, 1+p*2 segment of pipe p, 2+p*2 joint, 9 the eye
	var zbuf = [];
	var hx = [];         // pipe heads
	var hy = [];
	var hz = [];
	var hd = [];
	var DX = [1, -1, 0, 0, 0, 0];   // dirs paired so (d ^ 1) is the reverse
	var DY = [0, 0, 1, -1, 0, 0];
	var DZ = [0, 0, 0, 0, 1, -1];
	int gw = 16;
	int gh = 12;
	int gd = 16;
	int npipes = 4;
	int shades = 6;
	int egg_base = 0;
	int filled = 0;
	int cap = 0;
	int phase = 0;       // 0 grow, 1 hold, 2 fade
	float phase_t = 0.0;
	int fade_lv = 0;
	float step_acc = 0.0;
	float tt = 0.0;

	void build() override {
		name = "PIPEDREAM";
		handle = "screensaver seance . watch for the jackodile";
		cells = gfx.make_grid(gw * gh * gd);
		zbuf = gfx.make_grid(w * h);
		cap = gw * gh * gd * 38 / 100;
		pal.add_bg(3, 4, 10);   // 0: the office monitor at 2am
		var hues = [[70.0, 225.0, 195.0], [255.0, 178.0, 58.0], [232.0, 88.0, 212.0], [128.0, 158.0, 255.0]];
		var seg_glyphs = [".", "-", "+", "o", "O", "@"];
		var joint_glyphs = [".", "+", "*", "o", "8", "8"];
		for (int p = 0; p < npipes; ++p) {
			var hue = hues[p];
			float hr = hue[0];
			float hg = hue[1];
			float hb = hue[2];
			for (int k = 0; k < 2; ++k) {
				for (int s = 0; s < shades; ++s) {
					float f = 0.30 + 0.70 * s / (shades - 1.0);
					float cr = k == 1 ? (hr + (255.0 - hr) * 0.35) * f : hr * f;
					float cg = k == 1 ? (hg + (255.0 - hg) * 0.35) * f : hg * f;
					float cb = k == 1 ? (hb + (255.0 - hb) * 0.35) * f : hb * f;
					string gl = k == 1 ? joint_glyphs[s] : seg_glyphs[s];
					pal.add_fg_bg(cr, cg, cb, 3, 4, 10, gl);
				}
			}
		}
		egg_base = 1 + npipes * 2 * shades;
		for (int e = 0; e < 6; ++e) {
			var c = gfx.rainbow_rgb(e * 1.0471975512);
			pal.add_fg_bg(c[0], c[1], c[2], 3, 4, 10, "*");
		}
		for (int p2 = 0; p2 < npipes; ++p2) {
			hx.push(0);
			hy.push(0);
			hz.push(0);
			hd.push(0);
			respawn(p2);
		}
	}

	int cell_idx(int x, int y, int z) {
		return (y * gd + z) * gw + x;
	}

	bool is_free(int x, int y, int z) {
		if (x < 0 || x >= gw || y < 0 || y >= gh || z < 0 || z >= gd) { return false; }
		return cells[cell_idx(x, y, z)] == 0;
	}

	void advance(int p, int nx, int ny, int nz) {
		hx[p] = nx;
		hy[p] = ny;
		hz[p] = nz;
		cells[cell_idx(nx, ny, nz)] = 1 + p * 2;
		filled = filled + 1;
	}

	void respawn(int p) {
		for (int tr = 0; tr < 30; ++tr) {
			int x = rng.next(gw);
			int y = rng.next(gh);
			int z = rng.next(gd);
			if (cells[cell_idx(x, y, z)] == 0) {
				hx[p] = x;
				hy[p] = y;
				hz[p] = z;
				hd[p] = rng.next(6);
				cells[cell_idx(x, y, z)] = 1 + p * 2;
				filled = filled + 1;
				return;
			}
		}
	}

	void step_one(int p) {
		int x = hx[p];
		int y = hy[p];
		int z = hz[p];
		int d = hd[p];
		bool straight_ok = is_free(x + DX[d], y + DY[d], z + DZ[d]);
		if (straight_ok && rng.next(100) < 72) {
			advance(p, x + DX[d], y + DY[d], z + DZ[d]);
			return;
		}
		int nd = -1;
		for (int tr = 0; tr < 8; ++tr) {
			int cand = rng.next(6);
			if (cand == d || cand == (d ^ 1)) { continue; }
			if (is_free(x + DX[cand], y + DY[cand], z + DZ[cand])) { nd = cand; break; }
		}
		if (nd < 0 && straight_ok) {
			advance(p, x + DX[d], y + DY[d], z + DZ[d]);
			return;
		}
		if (nd < 0) {
			for (int cand2 = 0; cand2 < 6; ++cand2) {
				if (cand2 == (d ^ 1)) { continue; }
				if (is_free(x + DX[cand2], y + DY[cand2], z + DZ[cand2])) { nd = cand2; break; }
			}
		}
		if (nd < 0) {
			respawn(p);
			return;
		}
		// a turn: the head cell becomes a ball joint — or, rarely, the eye
		cells[cell_idx(x, y, z)] = rng.chance(2) ? 9 : 2 + p * 2;
		hd[p] = nd;
		advance(p, x + DX[nd], y + DY[nd], z + DZ[nd]);
	}

	void update(float t, float dt) override {
		tt = t;
		if (phase == 0) {
			step_acc += dt;
			while (phase == 0 && step_acc >= 0.03) {
				step_acc -= 0.03;
				for (int p = 0; p < npipes; ++p) { step_one(p); }
				if (filled >= cap) { phase = 1; phase_t = 0.0; step_acc = 0.0; }
			}
		} else if (phase == 1) {
			phase_t += dt;
			if (phase_t > 1.6) { phase = 2; phase_t = 0.0; }
		} else {
			phase_t += dt;
			fade_lv = itrunc(phase_t * 4.6);
			if (phase_t > 1.35) { reset_volume(); }
		}
	}

	void reset_volume() {
		for (auto& c : cells) { c = 0; }
		filled = 0;
		phase = 0;
		phase_t = 0.0;
		fade_lv = 0;
		step_acc = 0.0;
		for (int p = 0; p < npipes; ++p) { respawn(p); }
	}

	void render(var& grid) override {
		int nn = w * h;
		for (int q = 0; q < nn; ++q) { grid[q] = 0; zbuf[q] = 0; }
		float ca = cos(tt * 0.22);
		float sa = sin(tt * 0.22);
		float tilt = 0.38 + 0.10 * sin(tt * 0.13);
		float ct = cos(tilt);
		float st = sin(tilt);
		float cx = w * 0.5;
		float cy = h * 0.52;
		float k1 = w * 0.98;
		int flick = itrunc(tt * 8.0);
		int idx = 0;
		for (int iy = 0; iy < gh; ++iy) {
			float fy = iy - (gh - 1) * 0.5;
			for (int iz = 0; iz < gd; ++iz) {
				float fz = iz - (gd - 1) * 0.5;
				float zs = fz * sa;
				float zc = fz * ca;
				for (int ix = 0; ix < gw; ++ix) {
					int c = cells[idx];
					idx = idx + 1;
					if (c == 0) { continue; }
					float fx = ix - (gw - 1) * 0.5;
					float rx = fx * ca + zs;
					float rz = zc - fx * sa;
					float ry = fy * ct - rz * st;
					float zcam = fy * st + rz * ct + 27.0;
					float ooz = 1.0 / zcam;
					int sx = ifloor(cx + rx * ooz * k1);
					int sy = ifloor(cy - ry * ooz * k1 * 0.48);
					if (sy < 0 || sy >= h) { continue; }
					int s = itrunc((40.0 - zcam) * 0.27);
					s = s - fade_lv;
					if (s < 0) { continue; }
					s = s > 5 ? 5 : s;
					int pi = c == 9 ? egg_base + (flick + ix * 3 + iz) % 6 : 1 + (c - 1) * shades + s;
					if (sx >= 0 && sx < w) {
						int cell = sy * w + sx;
						if (ooz > zbuf[cell]) { zbuf[cell] = ooz; grid[cell] = pi; }
					}
					int sx2 = sx + 1;
					if (sx2 >= 0 && sx2 < w) {
						int cell2 = sy * w + sx2;
						if (ooz > zbuf[cell2]) { zbuf[cell2] = ooz; grid[cell2] = pi; }
					}
				}
			}
		}
	}
}
