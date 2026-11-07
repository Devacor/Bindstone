// PIPEDREAM — the 3D Pipes screensaver homage, tuned for authenticity: thick
// round pipes, glossy elbow balls at every turn, one segment per fixed tick
// (patient, steady, inevitable), pipes never intersect (occupancy grid; a
// blocked pipe turns or terminates and a fresh pipe respawns), and a camera
// that holds STILL for a whole generation — fill to threshold, brief hold, a
// CRT-style wipe, then reseed with a NEW viewing angle, like the real thing.
// Every tube is a chain of lit sphere impostors a quarter voxel apart: each
// pixel of a splat derives its normal from the disc's bulge (diffuse, a sharp
// specular, distance fog) and nudges its depth by it, so crossings intersect.
//
// The pipes are chrome: each pixel's normal reflects the set (a sky above, a
// burning horizon line, a dark floor below), so every tube carries the bright
// band of the horizon along its length, tinted by its pipe's hue.
//
// The static camera is also the perf trick: the screen image is persistent
// (pgrid/pz fields), and each frame only the voxels grown since the last frame
// are projected and splatted (~a dozen), not the whole volume. The camera's
// moves happen after: the persistent image is read through a slow push-in and
// drift, over a synthwave set (a striped sun on the horizon, a floor grid
// rushing in) that never needs re-splatting. When a generation fills, a glint
// sweeps every pipe and the frame flashes; the wipe is a CRT beam that leaves
// the set behind it. Canon demands the teapot: ~2% of joints instead spawn the
// jackodile's eye, a rainbow-flickering ball (re-splatted per frame so it shimmers).

class Pipes : Scene {
	array<int> cells = [];   // 0 empty, else 1 + pipe*3 + kind (0 seg, 1 joint, 2 eye)
	array<int> pgrid = [];   // persistent projected frame (palette indices)
	array<float> pz = [];    // persistent depth buffer
	array<int> pending = []; // voxels grown since the last render
	array<int> eggs = [];    // jackodile eyes, re-splatted every frame
	array<int> hx = [];      // pipe heads
	array<int> hy = [];
	array<int> hz = [];
	array<int> hd = [];
	array<int> DX = [1, -1, 0, 0, 0, 0];   // dirs paired so (d ^ 1) is the reverse
	array<int> DY = [0, 0, 1, -1, 0, 0];
	array<int> DZ = [0, 0, 0, 0, 1, -1];
	int gw = 16;
	int gh = 12;
	int gd = 16;
	int npipes = 6;
	int pipe_base = 0;
	int egg_base = 0;
	int sky_base = 0;      // the set: 32 sky rows, 16 sun shades, floor and grid
	int sun_base = 0;
	int floor_base = 0;
	int grid_base = 0;
	array<int> backdrop = [];  // the sky and sun, painted once
	array<int> col_map = [];   // the camera: which persistent column and row each screen pixel reads
	array<int> row_map = [];
	float done_t = -9.0;       // when the generation filled: the glint and flash
	int gen = 0;
	int filled = 0;
	int cap = 0;
	int phase = 0;       // 0 grow, 1 hold, 2 wipe
	float phase_t = 0.0;
	int last_wipe = 0;
	float step_acc = 0.0;
	float tt = 0.0;
	float gen_ang = 0.0; // camera angle for this generation (new one each reset)
	float gen_tilt = 0.34;
	float f_ca = 1.0;
	float f_sa = 0.0;
	float f_ct = 1.0;
	float f_st = 0.0;
	int flick = 0;

	void build() override {
		name = "PIPEDREAM";
		handle = "screensaver seance . watch for the jackodile";
		replay = 14.0;
		cells = gfx.make_grid(gw * gh * gd);
		pgrid = gfx.make_grid(w * h);
		pz = gfx.make_fgrid(w * h);
		cap = gw * gh * gd * 34 / 100;
		sky_base = pal.gradient(32, [[4.0, 2.0, 18.0], [22.0, 8.0, 52.0], [92.0, 18.0, 96.0], [230.0, 60.0, 120.0], [255.0, 150.0, 90.0]]);
		sun_base = pal.gradient(16, [[255.0, 236.0, 120.0], [255.0, 150.0, 70.0], [255.0, 60.0, 140.0]]);
		floor_base = pal.gradient(8, [[5.0, 2.0, 12.0], [70.0, 16.0, 78.0]]);
		grid_base = pal.gradient(8, [[40.0, 20.0, 90.0], [60.0, 220.0, 255.0]]);
		pipe_base = pal.size();
		var hues = [[70.0, 225.0, 195.0], [255.0, 178.0, 58.0], [232.0, 88.0, 212.0], [128.0, 158.0, 255.0],
			[250.0, 90.0, 80.0], [150.0, 240.0, 90.0]];
		for (int p = 0; p < npipes; ++p) {
			var hue = hues[p];
			shade_ramp(hue[0], hue[1], hue[2]);
		}
		egg_base = pal.size();
		for (int e = 0; e < 6; ++e) {
			var c = gfx.rainbow_rgb(e * 1.0471975512);
			shade_ramp(c[0], c[1], c[2]);
		}
		paint_backdrop();
		col_map = gfx.make_grid(w);
		row_map = gfx.make_grid(h);
		set_view(rng.next(628) * 0.01, 0.26 + rng.next(20) * 0.01);
		for (int p2 = 0; p2 < npipes; ++p2) {
			hx.push(0);
			hy.push(0);
			hz.push(0);
			hd.push(0);
			respawn(p2);
		}
	}

	// 16 levels: shadow -> full color -> white-hot highlight
	void shade_ramp(float r, float g, float b) {
		for (int s = 0; s < 16; ++s) {
			float f = s / 15.0;
			float k = f < 0.8 ? 0.06 + f * 1.18 : 1.0;
			float wh = f < 0.8 ? 0.0 : (f - 0.8) * 4.0;
			pal.add(r * k + (255.0 - r * k) * wh, g * k + (255.0 - g * k) * wh, b * k + (255.0 - b * k) * wh);
		}
	}

	int horizon() { return itrunc(h * 0.64); }

	// the sky's gradient and the striped sun sinking into the horizon
	void paint_backdrop() {
		backdrop = gfx.make_grid(w * h);
		int hz = horizon();
		float cx = w * 0.5;
		float sun_r = h * 0.3;
		float sun_y = hz - sun_r * 0.35;
		for (int y = 0; y < hz; ++y) {
			int sky = sky_base + itrunc(y * 31.0 / hz);
			for (int x = 0; x < w; ++x) {
				float dx = x + 0.5 - cx;
				float dy = y + 0.5 - sun_y;
				int ink = sky;
				if (dx * dx + dy * dy < sun_r * sun_r) {
					float down = (y + 0.5 - (sun_y - sun_r)) / (sun_r * 1.35);
					// the bands thicken toward the horizon as the gaps between them grow
					float band = (sun_y + sun_r - y) * 0.42;
					float gap = band - math::floor(band);
					bool cut = y > sun_y - sun_r * 0.25 && gap < 0.18 + down * 0.35;
					if (!cut) { ink = sun_base + itrunc((down > 0.999 ? 0.999 : down) * 16.0); }
				}
				backdrop[y * w + x] = ink;
			}
		}
	}

	void set_view(float ang, float tilt) {
		gen_ang = ang;
		gen_tilt = tilt;
		f_ca = math::cos(ang);
		f_sa = math::sin(ang);
		f_ct = math::cos(tilt);
		f_st = math::sin(tilt);
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
		int ci = cell_idx(nx, ny, nz);
		cells[ci] = 1 + p * 3;
		pending.push(ci);
		++filled;
	}

	void respawn(int p) {
		for (int tr = 0; tr < 30; ++tr) {
			int x = rng.next(gw);
			int y = rng.next(gh);
			int z = rng.next(gd);
			int ci = cell_idx(x, y, z);
			if (cells[ci] == 0) {
				hx[p] = x;
				hy[p] = y;
				hz[p] = z;
				hd[p] = rng.next(6);
				cells[ci] = 1 + p * 3;
				pending.push(ci);
				++filled;
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
			respawn(p);   // terminated: a fresh pipe takes over elsewhere
			return;
		}
		// a turn: the head cell becomes an elbow ball — or, rarely, the eye
		int here = cell_idx(x, y, z);
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

	void update(float t, float dt) override {
		tt = t;
		if (phase == 0) {
			step_acc += dt;
			while (phase == 0 && step_acc >= 0.03) {
				step_acc -= 0.03;
				for (int p = 0; p < npipes; ++p) { step_one(p); }
				if (filled >= cap) { phase = 1; phase_t = 0.0; step_acc = 0.0; done_t = t; }
			}
		} else if (phase == 1) {
			phase_t += dt;
			if (phase_t > 1.8) { phase = 2; phase_t = 0.0; }
		} else {
			phase_t += dt;
			if (phase_t > 1.3) { reset_volume(); }
		}
	}

	void reset_volume() {
		for (auto& c : cells) { c = 0; }
		int nn = w * h;
		for (int q = 0; q < nn; ++q) { pz[q] = 0.0; }
		++gen;
		pending.clear();
		eggs.clear();
		filled = 0;
		phase = 0;
		phase_t = 0.0;
		last_wipe = 0;
		step_acc = 0.0;
		set_view(rng.next(628) * 0.01, 0.26 + rng.next(20) * 0.01);   // the canon beat: a new angle
		for (int p = 0; p < npipes; ++p) { respawn(p); }
	}

	// one lit sphere impostor into the persistent frame: per pixel the disc's
	// bulge gives a normal (diffuse + specular) and a depth nudge, so tubes read
	// round and crossing pipes intersect cleanly; joints are fatter balls
	void splat_at(float fx, float fy, float fz, int c) {
		float rx = fx * f_ca + fz * f_sa;
		float rz = fz * f_ca - fx * f_sa;
		float ry = fy * f_ct - rz * f_st;
		float zcam = fy * f_st + rz * f_ct + 30.0;
		float ooz = 1.0 / zcam;
		int kind = (c - 1) % 3;
		int p = (c - 1) / 3;
		float scale = (w < h * 1.3 ? w : h * 1.3) * 1.02 * ooz;
		float rad = scale * (kind == 0 ? 0.40 : 0.58);
		rad = rad < 0.7 ? 0.7 : rad;
		float sx = w * 0.5 + rx * scale;
		float sy = h * 0.5 - ry * scale;
		float fog = 1.25 - zcam * 0.018;
		fog = fog > 1.0 ? 1.0 : (fog < 0.35 ? 0.35 : fog);
		int base = kind == 2 ? egg_base + ((flick + p + itrunc(fx * 3.0 + fz + 40.0)) % 6) * 16 : pipe_base + p * 16;
		int x0 = itrunc(sx - rad);
		int x1 = itrunc(sx + rad) + 1;
		int y0 = itrunc(sy - rad);
		int y1 = itrunc(sy + rad) + 1;
		float ir = 1.0 / rad;
		float r2 = rad * rad;
		float dz_gain = ooz * 0.02;
		var& pg = pgrid;
		var& zz = pz;
		for (int yy = y0; yy <= y1; ++yy) {
			if (yy < 0 || yy >= h) { continue; }
			float dy = yy + 0.5 - sy;
			int rowb = yy * w;
			for (int xx = x0; xx <= x1; ++xx) {
				if (xx < 0 || xx >= w) { continue; }
				float dx = xx + 0.5 - sx;
				float q = r2 - dx * dx - dy * dy;
				if (q < 0.0) { continue; }
				float nz = math::sqrt(q) * ir;
				float depth = ooz + nz * dz_gain;
				if (depth < zz[rowb + xx]) { continue; }
				float nx = dx * ir;
				float ny = dy * ir;
				float d = nx * -0.48 + ny * -0.56 + nz * 0.67;
				d = d < 0.0 ? 0.0 : d;
				float s = nx * -0.25 + ny * -0.29 + nz * 0.92;
				s = s < 0.0 ? 0.0 : s;
				float s2 = s * s;
				float s4 = s2 * s2;
				float s8 = s4 * s4;
				// chrome: the view ray reflected off the normal reads the set; up is
				// the sky, a hard bright line the horizon, down the dark floor
				float up = 0.0 - 2.0 * nz * ny;
				float env = up > 0.08 ? 0.34 + up * 0.32 : (up > -0.06 ? 1.05 : 0.06 - up * 0.1);
				float lit = (env * 0.72 + d * 0.22) * fog + s8 * s8 * (kind == 0 ? 0.55 : 0.85);
				int lv = itrunc(lit * 15.99);
				zz[rowb + xx] = depth;
				pg[rowb + xx] = base + (lv > 15 ? 15 : lv);
			}
		}
	}

	// splat one voxel plus midpoints toward every same-pipe neighbor
	// (midpoints are what keep the tube reading as one solid run)
	void splat_voxel(int vidx) {
		int c = cells[vidx];
		if (c == 0) { return; }
		int ix = vidx % gw;
		int rest = vidx / gw;
		int iz = rest % gd;
		int iy = rest / gd;
		float fx = ix - (gw - 1) * 0.5;
		float fy = iy - (gh - 1) * 0.5;
		float fz = iz - (gd - 1) * 0.5;
		splat_at(fx, fy, fz, c);
		int pp = (c - 1) / 3;
		int seg = 1 + pp * 3;
		if (ix > 0) {
			int n0 = cells[vidx - 1];
			if (n0 != 0 && (n0 - 1) / 3 == pp) { splat_at(fx - 0.25, fy, fz, seg); splat_at(fx - 0.5, fy, fz, seg); }
		}
		if (ix < gw - 1) {
			int n1 = cells[vidx + 1];
			if (n1 != 0 && (n1 - 1) / 3 == pp) { splat_at(fx + 0.25, fy, fz, seg); splat_at(fx + 0.5, fy, fz, seg); }
		}
		if (iz > 0) {
			int n2 = cells[vidx - gw];
			if (n2 != 0 && (n2 - 1) / 3 == pp) { splat_at(fx, fy, fz - 0.25, seg); splat_at(fx, fy, fz - 0.5, seg); }
		}
		if (iz < gd - 1) {
			int n3 = cells[vidx + gw];
			if (n3 != 0 && (n3 - 1) / 3 == pp) { splat_at(fx, fy, fz + 0.25, seg); splat_at(fx, fy, fz + 0.5, seg); }
		}
		if (iy > 0) {
			int n4 = cells[vidx - gw * gd];
			if (n4 != 0 && (n4 - 1) / 3 == pp) { splat_at(fx, fy - 0.25, fz, seg); splat_at(fx, fy - 0.5, fz, seg); }
		}
		if (iy < gh - 1) {
			int n5 = cells[vidx + gw * gd];
			if (n5 != 0 && (n5 - 1) / 3 == pp) { splat_at(fx, fy + 0.25, fz, seg); splat_at(fx, fy + 0.5, fz, seg); }
		}
	}

	void render(var& grid) override {
		if (phase == 2) {
			// generation over: a CRT beam sweeps down and leaves the set behind it
			int target = itrunc(phase_t * h / 1.1);
			target = target > h ? h : target;
			int stop = target * w;
			for (int q = last_wipe * w; q < stop; ++q) { pz[q] = 0.0; }
			last_wipe = target;
		} else {
			flick = itrunc(tt * 8.0);
			for (auto v : pending) { splat_voxel(v); }
			pending.clear();
			for (auto e : eggs) { splat_voxel(e); }   // the eyes shimmer every frame
		}
		// the camera: a slow push-in and drift through each generation, a kick in on the fill
		float grow = phase == 0 ? filled * 1.0 / cap : 1.0;
		float since = tt - done_t;
		float kick = phase == 0 ? 0.0 : (since < 1.8 ? since / 1.8 : 1.0);
		float zoom = 0.86 + grow * 0.16 + kick * kick * 0.12;
		float pan = math::sin(tt * 0.35 + gen * 2.1) * w * 0.03;
		float cx = w * 0.5;
		float cy = h * 0.5;
		for (int x = 0; x < w; ++x) {
			int sx = math::ifloor(cx + (x + 0.5 - cx) / zoom + pan);
			col_map[x] = sx < 0 || sx >= w ? -1 : sx;
		}
		for (int y = 0; y < h; ++y) {
			int sy = math::ifloor(cy + (y + 0.5 - cy) / zoom);
			row_map[y] = sy < 0 || sy >= h ? -1 : sy;
		}
		// the glint: a bright diagonal band crosses every pipe as the volume fills,
		// over a flash that lifts the whole frame and decays
		float glint = since >= 0.0 && since < 0.9 ? since / 0.9 * (w + h) * 1.3 - h * 0.3 : -999.0;
		int lift = since >= 0.0 && since < 0.35 ? itrunc((0.35 - since) * 20.0) : 0;
		int hz = horizon();
		float scroll = tt * 1.6;
		int beam = phase == 2 ? last_wipe : -9;
		var& pg = pgrid;
		var& zz = pz;
		var& bd = backdrop;
		int ramps = egg_base + 96;
		int i = 0;
		for (int y = 0; y < h; ++y) {
			int rb = row_map[y] * w;
			// the floor grid: a line wherever a grid line falls between this pixel and
			// the next, so lines stay one pixel wide; the far floor is haze
			float floor_z = y >= hz ? h * 0.9 / (y - hz + 1.0) : 0.0;
			float next_z = y >= hz ? h * 0.9 / (y - hz + 2.0) : 0.0;
			bool zline = y >= hz && math::floor(floor_z * 0.1 + scroll) != math::floor(next_z * 0.1 + scroll);
			float near = y >= hz ? (y - hz) * 1.0 / (h - hz) : 0.0;
			bool far = (floor_z - next_z) * 0.1 > 0.34;
			int glow = itrunc(near * 7.99);
			int dark = floor_base + itrunc((1.0 - near) * 7.99);
			float gstep = floor_z * 0.045;
			bool xfar = gstep > 0.17;
			for (int x = 0; x < w; ++x) {
				int cm = col_map[x];
				int src = rb + cm;
				int ink = 0;
				if (rb >= 0 && cm >= 0 && zz[src] > 0.0) {
					ink = pg[src];
					if (ink >= pipe_base && ink < ramps) {
						float dg = x + y - glint;
						int up2 = lift + (dg > -4.0 && dg < 4.0 ? 8 : (dg > -14.0 && dg < -9.0 ? 5 : 0));
						if (up2 > 0) {
							int lv = (ink - pipe_base) % 16;
							int lifted = lv + up2;
							ink += (lifted > 15 ? 15 : lifted) - lv;
						}
					}
				} else if (y < hz) {
					ink = bd[i];
				} else {
					float gx = (x - cx) * gstep;
					bool xline = math::floor(gx) != math::floor(gx + gstep);
					ink = (!far && zline) || (!xfar && xline) ? grid_base + glow : dark;
				}
				if (y == beam || y == beam - 1) { ink = grid_base + 7; }
				grid[i] = ink;
				++i;
			}
		}
	}
}
