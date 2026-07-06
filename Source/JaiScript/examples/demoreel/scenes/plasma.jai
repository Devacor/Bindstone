// PLASMA — the classic: four summed sine fields, cycling truecolor palette.
// Everything is integer table lookups per cell; sin() only runs at build time.

class Plasma : Scene {
	var st = [];        // sine table, 256 entries scaled to +-512
	var dist = [];      // per-cell radial term, precomputed
	int ncolors = 96;
	float tt = 0.0;

	void build() override {
		name = "PLASMA";
		handle = "warmup loop . by the symbolizer";
		for (int i = 0; i < 256; ++i) {
			st.push(ifloor(sin(i * 0.0245436926) * 512.0));
		}
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float dx = x - w * 0.5;
				float dy = (y - h * 0.5) * 2.0;
				int d = itrunc(sqrt(dx * dx + dy * dy) * 3.0);
				dist.push(d & 255);
			}
		}
		for (int i = 0; i < ncolors; ++i) {
			float ph = i * 6.28318530718 / ncolors;
			var c = gfx.rainbow_rgb(ph);
			// raw element args into typed params (finding 5 FIXED: exercises overload deref)
			pal.add_bg(c[0], c[1], c[2]);
		}
	}

	void update(float t, float dt) override {
		tt = t;
	}

	void render(var& grid) override {
		int t1 = itrunc(tt * 37.0);
		int t2 = itrunc(tt * 23.0);
		int t3 = itrunc(tt * 15.0);
		int phase = itrunc(tt * 41.0);
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			int ys = st[(y * 7 + t2) & 255];
			for (int x = 0; x < w; ++x) {
				int v = st[(x * 5 + t1) & 255] + ys + st[(dist[idx] + t3) & 255] + st[(x * 3 + y * 4 + phase) & 255];
				int ci = v / 32 + phase;
				grid[idx] = ((ci % ncolors) + ncolors) % ncolors;
				idx = idx + 1;
			}
		}
	}
}
