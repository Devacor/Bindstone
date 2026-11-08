// JULIA — the Julia set as a landscape, flown low and played loud. Every frame a
// height field is iterated across all cores: the smooth escape count of a morphing
// c (orbiting just inside the Mandelbrot cardioid, so the set stays connected)
// raises ridges along the set's boundary, and the set's inside is a low lake
// they wall in, its floor rippled by the orbit trap. Then each screen column is cast voxelspace-style
// across the field from a camera skimming the ridges: slope light, fog toward the
// horizon, a sky above. It is scored on the beat: kicks punch the terrain up,
// snares flash the sky, the chord turns the palette, the section's energy drives
// the speed; the riser climbs high over the set and the drop plunges into a low,
// fast canyon run as c jumps to a new set. The set's inside is still water: it
// mirrors the ridges and the sky (ripples wobbling the mirror on the kick), a moon
// lays a glitter path across it, stars turn overhead, the crystal ridge tops spark
// in the light, and the whole frame banks into the flight's turns.

float JU_CA = 0.0;
float JU_CB = 0.0;
int JU_N = 200;             // the height field's cells a side
float JU_GX0 = -1.6;        // its world box
float JU_GY0 = -1.6;
float JU_CELL = 0.016;
int JU_MAXIT = 40;
array<float> JU_HT = [];    // per cell: height 0..1 and hue
array<int> JU_HUE = [];
array<int> JU_HROWS = [];
array<int> JU_COLS = [];
int JU_W = 0;
int JU_H = 0;
float JU_CAMX = 0.0;        // the camera: position, altitude, heading, horizon
float JU_CAMY = 0.0;
float JU_ALT = 0.4;
float JU_HEAD = 0.0;
float JU_HORIZON = 30.0;
float JU_HSCALE = 0.25;     // world units of height at 1.0
float JU_FOV = 1.1;
float JU_LIFT = 0.0;        // light added on the beat
int JU_PHASE = 0;           // the palette's turn
int JU_SKY = 0;
int JU_SKYLIFT = 0;
float JU_ZFAR = 2.6;
float JU_WATER = 0.13;      // the lake's surface, in field height
float JU_T = 0.0;
float JU_RIPPLE = 0.0;

// one row of the height field: the smooth escape count rises to the boundary, the inside is a terraced mesa
function julia_hrow(int gy) -> array<int> {
	array<int> out = [];
	int n = JU_N;
	int mi = JU_MAXIT;
	float ca = JU_CA;
	float cb = JU_CB;
	float y0 = JU_GY0 + (gy + 0.5) * JU_CELL;
	float x00 = JU_GX0;
	float cell = JU_CELL;
	for (int gx = 0; gx < n; ++gx) {
		float zx = x00 + (gx + 0.5) * cell;
		float zy = y0;
		float x2 = zx * zx;
		float y2 = zy * zy;
		float trap = 9.0;
		int k = 0;
		while (k < mi && x2 + y2 < 64.0) {
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			float m = x2 + y2;
			trap = m < trap ? m : trap;
			++k;
		}
		int ht = 0;
		int hue = 0;
		if (k >= mi) {
			// the inside: a low lake the boundary's ridges wall in, its floor rippled by the orbit trap
			float tl = math::sqrt(trap);
			ht = 60 + itrunc(120.0 * math::pow(2.718281828, 0.0 - tl * 3.0));
			hue = 17 + itrunc(tl * 9.0);
		} else {
			float nu = k + 1.0 - math::log2(math::log2(x2 + y2) * 0.5);
			float f = nu / mi;
			f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
			ht = itrunc(math::sqrt(f) * 1000.0);
			hue = itrunc(math::sqrt(nu) * 5.0);
		}
		out.push(ht * 64 + (hue & 63));
	}
	return out;
}

// one screen column, cast across the field front to back; returns its pixels top to bottom
function julia_col(int x) -> array<int> {
	int hh = JU_H;
	array<int> out = [];
	for (int i = 0; i < hh; ++i) { out.push(-1); }
	int n = JU_N;
	float rel = (x + 0.5 - JU_W * 0.5) / JU_W * JU_FOV;
	float a = JU_HEAD + rel;
	float dx = math::cos(a);
	float dy = math::sin(a);
	float corr = math::cos(rel);
	float focal = hh * 0.95;
	float horizon = JU_HORIZON;
	float alt = JU_ALT;
	float hs = JU_HSCALE;
	float lift = JU_LIFT;
	int phase = JU_PHASE;
	float gx0 = JU_GX0;
	float gy0 = JU_GY0;
	float icell = 1.0 / JU_CELL;
	float zfar = JU_ZFAR;
	int ybuf = hh;
	float z = 0.012;
	float dz = 0.003;
	while (z < zfar && ybuf > 0) {
		float wx = JU_CAMX + dx * z;
		float wy = JU_CAMY + dy * z;
		float fx = (wx - gx0) * icell;
		float fy = (wy - gy0) * icell;
		int cx = itrunc(fx);
		int cy = itrunc(fy);
		float ht = 0.0;
		int hue = 8;
		float light = 0.4;
		if (fx >= 1.0 && fy >= 1.0 && cx < n - 1 && cy < n - 1) {
			int idx = cy * n + cx;
			// bilinear between the four cells round the sample, so the land is smooth up close
			float ux = fx - cx;
			float uy = fy - cy;
			float h00 = JU_HT[idx];
			float h10 = JU_HT[idx + 1];
			float h01 = JU_HT[idx + n];
			float h11 = JU_HT[idx + n + 1];
			ht = (h00 + (h10 - h00) * ux) * (1.0 - uy) + (h01 + (h11 - h01) * ux) * uy;
			hue = JU_HUE[idx];
			// slope light from the up-left, the light's side
			float back = JU_HT[idx - n - 1];
			light = 0.5 + (ht - back) * 7.0 + ht * 0.35;
		}
		bool wet = ht < JU_WATER;
		if (wet) { ht = JU_WATER; }
		int sy = itrunc(horizon + (alt - ht * hs) / (z * corr) * focal);
		if (sy < ybuf && wet) {
			// water: marked with its surface row, mirrored once the column is done
			int top2 = sy < 0 ? 0 : sy;
			for (int y2 = top2; y2 < ybuf; ++y2) { out[y2] = -2 - (sy < 0 ? 0 : sy); }
			ybuf = top2;
		} else if (sy < ybuf) {
			float fog = z / zfar;
			int fl = itrunc(fog * fog * 3.99);
			// the crystal ridge tops catch the light in sparks
			float spark = ht > 0.82 && light > 0.95 ? 0.6 : 0.0;
			int lv = itrunc((light + lift + spark) * 7.99);
			lv = lv < 0 ? 0 : (lv > 7 ? 7 : lv);
			int c = ((hue + phase) & 31) * 32 + lv * 4 + fl;
			int top = sy < 0 ? 0 : sy;
			for (int y = top; y < ybuf; ++y) { out[y] = c; }
			ybuf = top;
		}
		z += dz;
		dz *= 1.012;
	}
	// the mirror: each water pixel shows the column mirrored about its surface row, a little
	// darker and hazier, wobbling with the ripples; past the top it shows the sky (-10 - row)
	float ripple = JU_RIPPLE;
	float tw = JU_T;
	for (int y3 = 0; y3 < hh; ++y3) {
		int v = out[y3];
		if (v > -2) { continue; }
		int sw = -2 - v;
		int m = 2 * sw - y3 + itrunc(math::sin(y3 * 1.9 + tw * 7.0 + x * 0.21) * (1.0 + ripple * 2.5));
		if (m < 0 || m >= hh) {
			out[y3] = -10 - (m < 0 ? 0 : hh - 1);
			continue;
		}
		int r = out[m];
		if (r >= 0) {
			int lv2 = (r >> 2) & 7;
			int fl2 = r & 3;
			lv2 = lv2 > 2 ? lv2 - 2 : 0;
			fl2 = fl2 < 3 ? fl2 + 1 : 3;
			out[y3] = ((r >> 5) << 5) + lv2 * 4 + fl2;
		} else {
			out[y3] = -10 - m;
		}
	}
	return out;
}

class Julia : Scene {
	float tt = 0.0;
	float drop_t = 0.0;       // the big hit: bar 3
	float cx = 0.0;           // where the flight is
	float cy = 0.0;
	float travel = 0.0;
	int water_sky = 0;
	int moon = 0;
	int glitter = 0;
	int stars = 0;
	array<int> frame = [];
	float moon_az = 0.6;     // the moon's bearing in the world

	array<float> hit_times() override { return [MUSIC_BAR * 3.0, MUSIC_BAR * 5.0]; }
	array<int> hit_levels() override { return [2, 1]; }

	void build() override {
		name = "JULIA";
		handle = "a fractal flown low . by the type ladder";
		drop_t = MUSIC_BAR * 3.0;
		// 32 graded hues x 8 light levels x 4 fog steps toward the horizon's haze
		var stops = [[10.0, 12.0, 48.0], [40.0, 30.0, 130.0], [150.0, 40.0, 180.0], [255.0, 70.0, 120.0],
			[255.0, 150.0, 60.0], [255.0, 230.0, 140.0], [120.0, 230.0, 255.0], [30.0, 110.0, 200.0], [10.0, 12.0, 48.0]];
		int ns = stops.size() - 1;
		float hr = 34.0; float hg = 14.0; float hb = 58.0;
		for (int hq = 0; hq < 32; ++hq) {
			float f = hq * ns / 32.0;
			int k = itrunc(f);
			float u = f - k;
			var a = stops[k];
			var b = stops[k + 1];
			float cr = a[0] + (b[0] - a[0]) * u;
			float cg = a[1] + (b[1] - a[1]) * u;
			float cb = a[2] + (b[2] - a[2]) * u;
			for (int l = 0; l < 8; ++l) {
				float lit = 0.1 + l / 7.0 * 1.0;
				float wh = l >= 6 ? (l - 5) * 0.2 : 0.0;
				float r = cr * lit + (255.0 - cr * lit) * wh;
				float g = cg * lit + (255.0 - cg * lit) * wh;
				float bl = cb * lit + (255.0 - cb * lit) * wh;
				for (int fg = 0; fg < 4; ++fg) {
					float m = fg / 3.0 * 0.85;
					pal.add(r + (hr - r) * m, g + (hg - g) * m, bl + (hb - bl) * m);
				}
			}
		}
		// the sky: 24 steps from the horizon's haze up to night, and 8 flash levels over it
		JU_SKY = pal.size();
		for (int fl = 0; fl < 4; ++fl) {
			for (int s = 0; s < 24; ++s) {
				float k2 = s / 23.0;
				float f2 = fl * 0.22;
				float r2 = hr + (4.0 - hr) * k2;
				float g2 = hg + (2.0 - hg) * k2;
				float b2 = hb + (16.0 - hb) * k2;
				pal.add(r2 + (220.0 - r2) * f2, g2 + (160.0 - g2) * f2, b2 + (255.0 - b2) * f2);
			}
		}
		// the sky as the lake mirrors it: 24 darker, bluer steps; then the moon (8 levels), the
		// moon's glitter on the water (4) and the stars (4)
		water_sky = pal.size();
		for (int s3 = 0; s3 < 24; ++s3) {
			float k3 = s3 / 23.0;
			pal.add(60.0 - 50.0 * k3, 46.0 - 38.0 * k3, 120.0 - 90.0 * k3);
		}
		moon = pal.ramp(8, 120.0, 90.0, 120.0, 255.0, 246.0, 220.0);
		glitter = pal.ramp(4, 120.0, 110.0, 150.0, 255.0, 250.0, 230.0);
		stars = pal.ramp(4, 60.0, 60.0, 100.0, 255.0, 255.0, 255.0);
		frame = gfx.make_grid(w * h);
		JU_HT = gfx.make_fgrid(JU_N * JU_N);
		JU_HUE = gfx.make_grid(JU_N * JU_N);
	}

	float smooth(float a, float b, float t) {
		float k = (t - a) / (b - a);
		k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
		return k * k * (3.0 - 2.0 * k);
	}

	void update(float t, float dt) override {
		tt = t;
	}

	void render(var& grid) override {
		float t = tt;
		float kick = cue.kick_env(t);
		float snare = cue.snare_env(t);
		float energy = cue.energy(t);
		float riser = cue.riser(t);
		float hitv = cue.hit(t);
		bool dropped = t >= drop_t;
		// c orbits the cardioid; on the drop it jumps to a new set and moves faster
		float ot = dropped ? t * 0.55 + 2.4 : t * 0.25;
		JU_CA = 0.7885 * math::cos(ot + 2.0);
		JU_CB = 0.7885 * math::sin(ot * 0.8 + 2.0);
		// the flight: a loop round the set, faster with the energy and racing after the drop
		float speed = 0.18 + energy * 0.25 + (dropped ? 0.35 : 0.0);
		float path = t * speed + (dropped ? 1.7 : 0.0);
		float radius = dropped ? 0.55 : 0.95 - riser * 0.3;
		float px = radius * math::cos(path);
		float py = radius * math::sin(path) * 0.8;
		float nx = -radius * math::sin(path);
		float ny = radius * math::cos(path) * 0.8;
		JU_CAMX = px;
		JU_CAMY = py;
		JU_HEAD = math::atan2(ny, nx) + 0.25 * math::sin(t * 0.7);
		// altitude: cruising, climbing high on the riser, skimming the canyon after the drop
		float cruise = 0.32 + 0.06 * math::sin(t * 1.1);
		float alt = dropped ? 0.22 + 0.05 * math::sin(t * 3.0) : cruise + riser * riser * 0.9;
		alt += smooth(MUSIC_BAR * 5.4, MUSIC_BAR * 6.0, t) * 0.8;
		JU_ALT = alt;
		JU_HSCALE = 0.18 * (1.0 + kick * 0.45 + hitv * 0.5);
		JU_HORIZON = h * (0.34 + riser * 0.22) - kick * h * 0.015 + 0.04 * h * math::sin(t * 0.9);
		JU_FOV = 1.05 + kick * 0.08 + hitv * 0.25;
		JU_LIFT = kick * 0.16 + snare * 0.06 + hitv * 0.08 - 0.05;
		JU_PHASE = itrunc(t * 3.0) + cue.chord(t) * 3 + (dropped && hitv > 0.75 ? itrunc(t * 24.0) : 0);
		JU_SKYLIFT = itrunc(snare * 2.0 + hitv * 3.5);
		JU_ZFAR = 2.2 + energy * 0.6;
		// the height field covers the box the camera can see
		JU_GX0 = px - 1.6;
		JU_GY0 = py - 1.6;
		JU_CELL = 3.2 / JU_N;
		JU_MAXIT = 34 + itrunc(energy * 14.0);
		if (JU_HROWS.size() != JU_N) {
			JU_HROWS = [];
			for (int r = 0; r < JU_N; ++r) { JU_HROWS.push(r); }
		}
		var field = parallel_transform(JU_HROWS, julia_hrow);
		var& ht = JU_HT;
		var& hue = JU_HUE;
		int i = 0;
		for (int gy = 0; gy < JU_N; ++gy) {
			var& row = field[gy];
			for (int gx = 0; gx < JU_N; ++gx) {
				int v = row[gx];
				ht[i] = (v >> 6) / 1000.0;
				hue[i] = v & 63;
				++i;
			}
		}
		JU_W = w;
		JU_H = h;
		if (JU_COLS.size() != w) {
			JU_COLS = [];
			for (int c = 0; c < w; ++c) { JU_COLS.push(c); }
		}
		// the camera keeps clear of the ground under it and a little ahead
		float ground = 0.0;
		for (int a = 0; a < 3; ++a) {
			float gx2 = (px + math::cos(JU_HEAD) * a * 0.06 - JU_GX0) / JU_CELL;
			float gy2 = (py + math::sin(JU_HEAD) * a * 0.06 - JU_GY0) / JU_CELL;
			int ci = itrunc(gx2);
			int cj = itrunc(gy2);
			if (ci >= 0 && cj >= 0 && ci < JU_N && cj < JU_N) {
				float g = ht[cj * JU_N + ci];
				ground = g > ground ? g : ground;
			}
		}
		float floor_alt = ground * JU_HSCALE + 0.07;
		if (JU_ALT < floor_alt) { JU_ALT = floor_alt; }
		JU_T = t;
		JU_RIPPLE = kick;
		var cols = parallel_transform(JU_COLS, julia_col);
		int sky = JU_SKY + (JU_SKYLIFT > 3 ? 3 : JU_SKYLIFT) * 24;
		float hz = JU_HORIZON;
		// the moon rides ahead of the flight, drifting across the frame as it banks
		float rel = 0.22 * math::sin(t * 0.35) - 0.12;
		float mx = w * 0.5 + rel / JU_FOV * w;
		float my = hz * 0.4;
		float mr = h * 0.1;
		var& fr = frame;
		for (int x = 0; x < w; ++x) {
			var& col = cols[x];
			float ddx = x - mx;
			bool glitter_col = ddx < mr * 0.7 && ddx > -mr * 0.7;
			for (int y = 0; y < h; ++y) {
				int c = col[y];
				if (c == -1) {
					float up = (hz - y) / (hz + 1.0);
					int s = itrunc((up < 0.0 ? 0.0 : (up > 1.0 ? 1.0 : up)) * 23.0);
					c = sky + s;
					float ddy = y - my;
					float md = ddx * ddx + ddy * ddy;
					if (md < mr * mr) {
						int ml = 4 + itrunc((1.0 - md / (mr * mr)) * 3.99);
						c = moon + (ml > 7 ? 7 : ml);
					} else if (md < mr * mr * 2.6) {
						c = sky + (s > 4 ? s - 4 : 0);
					} else if (up > 0.25) {
						int hs = ((x * 73856093 + y * 19349663) & 1048575) * 1274126177;
						if (((hs >> 12) & 255) < 3) {
							float twk = 0.5 + 0.5 * math::sin(t * 3.0 + (hs >> 20));
							c = stars + itrunc(twk * 3.99);
						}
					}
				} else if (c <= -10) {
					int row = -10 - c;
					float up2 = (hz - row) / (hz + 1.0);
					int s2 = itrunc((up2 < 0.0 ? 0.0 : (up2 > 1.0 ? 1.0 : up2)) * 23.0);
					c = water_sky + s2;
					if (glitter_col) {
						int hs2 = ((x * 92837111 + y * 689287499 + itrunc(t * 12.0) * 15731) & 1048575) * 1274126177;
						if (((hs2 >> 12) & 7) < 3) { c = glitter + ((hs2 >> 16) & 3); }
					}
				}
				fr[y * w + x] = c;
			}
		}
		// the bank: the frame rolls into the flight's turn (harder after the drop), each pixel
		// reading the unrolled frame about the centre
		float bank = 0.0 - 0.12 * math::cos(t * 0.7) - (dropped ? 0.18 * math::sin(t * 1.3) : 0.0) + snare * 0.03;
		float cb2 = math::cos(bank) / 1.12;
		float sb2 = math::sin(bank) / 1.12;
		float ox = w * 0.5;
		float oy = h * 0.5;
		int gi = 0;
		for (int y4 = 0; y4 < h; ++y4) {
			float vy = y4 + 0.5 - oy;
			for (int x4 = 0; x4 < w; ++x4) {
				float vx = x4 + 0.5 - ox;
				int sx = itrunc(ox + vx * cb2 - vy * sb2);
				int sy = itrunc(oy + vx * sb2 + vy * cb2);
				sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
				sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);
				grid[gi] = fr[sy * w + sx];
				++gi;
			}
		}
	}
}
