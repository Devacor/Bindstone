// INTRO — the cold open, four bars before the first scene. A dead signal flickers
// over the riser and a CRT line powers on; on the first downbeat JAISCRIPT slams in
// split into red and cyan ghosts that snap together, a shockwave racing out over a
// starfield that starts to warp on the kick, and the tagline types itself; the logo
// glitches on the hats, then on the second impact bursts into sparks that stream into
// the warp until the screen whites out into DONUT. It has no title card and plays
// once, when the reel starts.

class Intro : Scene {
	array<float> star_x = [];
	array<float> star_y = [];
	array<float> star_z = [];
	array<float> spark_x = [];
	array<float> spark_y = [];
	array<float> spark_vx = [];
	array<float> spark_vy = [];
	array<int> logo_at = [];
	int nstars = 260;
	int gray = 0;        // 16 levels black to white
	int red = 0;         // 8 levels
	int cyan = 0;
	int hot = 0;         // 8 levels of the burst's sparks
	int logo_ink = 0;    // 8 levels, gold at the top of the letters to magenta at the foot
	int logo_x = 0;
	int logo_y = 0;
	int logo_w = 0;
	int logo_h = 0;
	int scale = 1;
	float tt = 0.0;
	float slam_t = 1.93548388;      // bar 1
	float burst_t = 5.80645164;     // bar 3
	bool burst_done = false;

	void build() override {
		name = "JAISCRIPT";
		handle = "";
		replay = 8.0;
		gray = pal.ramp(16, 0.0, 0.0, 0.0, 255.0, 255.0, 255.0);
		red = pal.ramp(8, 30.0, 0.0, 10.0, 255.0, 40.0, 70.0);
		cyan = pal.ramp(8, 0.0, 20.0, 30.0, 60.0, 230.0, 255.0);
		hot = pal.gradient(8, [[60.0, 10.0, 40.0], [255.0, 80.0, 60.0], [255.0, 210.0, 90.0], [255.0, 255.0, 240.0]]);
		logo_ink = pal.gradient(8, [[255.0, 250.0, 220.0], [255.0, 214.0, 110.0], [255.0, 128.0, 70.0], [230.0, 60.0, 140.0]]);
		for (int i = 0; i < nstars; ++i) {
			star_x.push((rng.next(2000) - 1000) / 1000.0);
			star_y.push((rng.next(2000) - 1000) / 1000.0);
			star_z.push(0.05 + rng.next(1000) / 1000.0);
		}
		scale = 1;
		while (gfx.text_width("JAISCRIPT", scale + 1) <= w * 9 / 10 && scale < 8) { ++scale; }
		logo_w = gfx.text_width("JAISCRIPT", scale);
		logo_h = 7 * scale;
		logo_x = (w - logo_w) / 2;
		logo_y = h / 2 - logo_h / 2 - h / 10;
		array<int> mask = gfx.make_grid(w * h);
		gfx.draw_text(mask, w, h, "JAISCRIPT", logo_x, logo_y, scale, scale, 1, 1, -1, 9);
		for (int i2 = 0; i2 < w * h; ++i2) {
			if (mask[i2] == 1) { logo_at.push(i2); }
		}
	}

	array<float> hit_times() override { return [slam_t, burst_t]; }
	array<int> hit_levels() override { return [2, 2]; }

	void update(float t, float dt) override {
		tt = t;
		float warp = t < slam_t ? 0.0 : 0.25 + (cue != null ? cue.kick_env(t) * 0.5 : 0.0) + (t > burst_t ? (t - burst_t) * 1.8 : 0.0);
		for (int i = 0; i < nstars; ++i) {
			star_z[i] = star_z[i] - dt * warp;
			if (star_z[i] <= 0.03) {
				star_z[i] = 1.0;
				star_x[i] = (rng.next(2000) - 1000) / 1000.0;
				star_y[i] = (rng.next(2000) - 1000) / 1000.0;
			}
		}
		if (t >= burst_t && !burst_done) {
			burst_done = true;
			for (int k = 0; k < logo_at.size(); k += 3) {
				int i3 = logo_at[k];
				float px = i3 % w;
				float py = i3 / w;
				float dx = px - w * 0.5;
				float dy = py - h * 0.5;
				float l = math::sqrt(dx * dx + dy * dy) + 1.0;
				float speed = 40.0 + rng.next(100) * 1.2;
				spark_x.push(px);
				spark_y.push(py);
				spark_vx.push(dx / l * speed + (rng.next(40) - 20));
				spark_vy.push(dy / l * speed + (rng.next(40) - 20));
			}
		}
		for (int s = 0; s < spark_x.size(); ++s) {
			spark_x[s] = spark_x[s] + spark_vx[s] * dt;
			spark_y[s] = spark_y[s] + spark_vy[s] * dt;
			spark_vx[s] = spark_vx[s] * 1.03;
			spark_vy[s] = spark_vy[s] * 1.03;
		}
	}

	void render(var& grid) override {
		int n = w * h;
		float t = tt;
		float kick = cue != null ? cue.kick_env(t) : 0.0;
		float hat = cue != null ? cue.hat_env(t) : 0.0;
		// before the slam: static that thickens over the riser, then the CRT line
		if (t < slam_t) {
			float rise = t / slam_t;
			int seed_frame = itrunc(t * 30.0);
			for (int i = 0; i < n; ++i) {
				int hsh = ((i * 1103515245 + seed_frame * 12345) >> 8) & 255;
				grid[i] = hsh < itrunc(rise * rise * 90.0) ? gray + (hsh % 6) : gray;
			}
			if (t > slam_t * 0.72) {
				float open = (t - slam_t * 0.72) / (slam_t * 0.28);
				int half = itrunc(open * open * w * 0.5);
				int y = h / 2;
				for (int x = w / 2 - half; x < w / 2 + half; ++x) {
					if (x >= 0 && x < w) {
						grid[y * w + x] = gray + 15;
						if (y + 1 < h) { grid[(y + 1) * w + x] = gray + 9; }
					}
				}
			}
			return;
		}
		for (int i = 0; i < n; ++i) { grid[i] = gray; }
		// the warp: stars streak from the centre, longer as the speed climbs
		float speed = 0.25 + kick * 0.5 + (t > burst_t ? (t - burst_t) * 1.8 : 0.0);
		for (int s = 0; s < nstars; ++s) {
			float z = star_z[s];
			float sx = w * 0.5 + star_x[s] / z * w * 0.35;
			float sy = h * 0.5 + star_y[s] / z * h * 0.35;
			float z2 = z + speed * 0.06 + 0.01;
			float tx = w * 0.5 + star_x[s] / z2 * w * 0.35;
			float ty = h * 0.5 + star_y[s] / z2 * h * 0.35;
			int lv = itrunc((1.0 - z) * 15.0);
			lv = lv < 2 ? 2 : (lv > 15 ? 15 : lv);
			streak(grid, tx, ty, sx, sy, gray + lv);
		}
		float since = t - slam_t;
		// the shockwave
		if (since < 1.0) {
			float r = since * w * 0.9;
			ring(grid, w * 0.5, logo_y + logo_h * 0.5, r, 1.5 + since * 4.0, gray + 15);
		}
		if (t < burst_t) {
			// the logo: red and cyan ghosts split apart on the slam and on every hat, white on top
			float split = (since < 0.5 ? (0.5 - since) * 2.0 : 0.0) * scale * 3.0 + hat * scale * 0.8;
			int off = itrunc(split);
			draw_logo(grid, -off, 0, red + 7);
			draw_logo(grid, off, 0, cyan + 7);
			if (since < 0.12) { draw_logo(grid, 0, 0, gray + 15); }
			else { draw_logo_gradient(grid, kick); }
			if (cue != null && hat > 0.6 && t > slam_t + 1.0) { glitch(grid, itrunc(t / MUSIC_STEP)); }
		}
		tagline();
		// the burst
		for (int s2 = 0; s2 < spark_x.size(); ++s2) {
			int x = itrunc(spark_x[s2]);
			int y = itrunc(spark_y[s2]);
			if (x < 0 || x >= w || y < 0 || y >= h) { continue; }
			grid[y * w + x] = hot + 4 + (s2 % 4);
		}
		// the white-out into the first scene
		float white = (t - (burst_t + MUSIC_BAR * 0.75)) / (MUSIC_BAR * 0.25);
		if (white > 0.0) {
			int lv2 = itrunc((white > 1.0 ? 1.0 : white) * 15.0);
			for (int i4 = 0; i4 < n; ++i4) {
				if (grid[i4] < gray + lv2) { grid[i4] = gray + lv2; }
			}
		}
	}

	void tagline() {
		if (tt < slam_t + 0.35 || tt > burst_t) { return; }
		string a = "a demoreel written in a scripting language";
		string b = "every pixel . every note . script";
		int shown_a = itrunc((tt - slam_t - 0.35) * 34.0);
		int shown_b = itrunc((tt - slam_t - 1.4) * 30.0);
		int row = cell_row(logo_y + logo_h) + 2;
		if (shown_a > 0) { txt_put((HB_COLS - a.size()) / 2, row, a.substr(0, shown_a > a.size() ? a.size() : shown_a), rgb(214, 208, 255)); }
		if (shown_b > 0 && row + 2 < HB_ROWS) { txt_put((HB_COLS - b.size()) / 2, row + 2, b.substr(0, shown_b > b.size() ? b.size() : shown_b), rgb(255, 206, 110)); }
	}

	// the letters shaded top to bottom, the kick lifting them toward white
	void draw_logo_gradient(var& grid, float kick) {
		int lift = itrunc(kick * 3.0);
		for (int k = 0; k < logo_at.size(); ++k) {
			int i = logo_at[k];
			int lv = (i / w - logo_y) * 8 / logo_h - lift;
			grid[i] = logo_ink + (lv < 0 ? 0 : (lv > 7 ? 7 : lv));
		}
	}

	void draw_logo(var& grid, int dx, int dy, int ink) {
		for (int k = 0; k < logo_at.size(); ++k) {
			int i = logo_at[k];
			int x = i % w + dx;
			int y = i / w + dy;
			if (x >= 0 && x < w && y >= 0 && y < h) { grid[y * w + x] = ink; }
		}
	}

	void glitch(var& grid, int step) {
		array<int> row = [];
		for (int x = 0; x < w; ++x) { row.push(0); }
		for (int y = logo_y - 2; y < logo_y + logo_h + 2; ++y) {
			if (y < 0 || y >= h) { continue; }
			int slice = (y / 3 + step * 5) % 6;
			if (slice > 1) { continue; }
			int off = slice == 0 ? scale * 2 : -scale * 2;
			int base = y * w;
			for (int x2 = 0; x2 < w; ++x2) { row[x2] = grid[base + x2]; }
			for (int x3 = 0; x3 < w; ++x3) {
				int from = x3 - off;
				from = from < 0 ? 0 : (from >= w ? w - 1 : from);
				grid[base + x3] = row[from];
			}
		}
	}

	void streak(var& grid, float x0, float y0, float x1, float y1, int ink) {
		float dx = x1 - x0;
		float dy = y1 - y0;
		float adx = dx < 0.0 ? -dx : dx;
		float ady = dy < 0.0 ? -dy : dy;
		int steps = itrunc((adx > ady ? adx : ady) + 1.0);
		steps = steps > 60 ? 60 : steps;
		for (int s = 0; s <= steps; ++s) {
			int x = itrunc(x0 + dx * s / steps);
			int y = itrunc(y0 + dy * s / steps);
			if (x >= 0 && x < w && y >= 0 && y < h) { grid[y * w + x] = ink; }
		}
	}

	void ring(var& grid, float cx, float cy, float r, float width, int ink) {
		int y0 = itrunc(cy - r - width) - 1;
		int y1 = itrunc(cy + r + width) + 1;
		y0 = y0 < 0 ? 0 : y0;
		y1 = y1 > h - 1 ? h - 1 : y1;
		for (int y = y0; y <= y1; ++y) {
			float ey = y - cy;
			for (int x = 0; x < w; ++x) {
				float ex = x - cx;
				float d = math::sqrt(ex * ex + ey * ey) - r;
				if (d < width && d > -width) { grid[y * w + x] = ink; }
			}
		}
	}
}
