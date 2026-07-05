// JULIA — morphing Julia set: c orbits inside the Mandelbrot cardioid so the
// set stays connected while it writhes. Escape-count colored, computed at
// half horizontal resolution (console cells are ~1:2, so doubled columns are
// square pixels — and it halves the per-frame work of the heaviest scene).

class Julia : Scene {
	int maxit = 12;
	float tt = 0.0;


	void build() override {
		name = "JULIA";
		handle = "orbit morphing . by the type ladder";
		pal.add_bg(6, 4, 18);                     // 0: inside the set
		for (int k = 1; k <= maxit; ++k) {
			float f = (k - 1.0) / (maxit - 1.0);
			pal.add_bg(20.0 + 235.0 * f * f, 8.0 + 150.0 * f, 60.0 + 195.0 * (1.0 - f) + 60.0 * f);
		}
	}

	void update(float t, float dt) override {
		tt = t;
	}

	void render(var& grid) override {
		float ca = 0.7885 * cos(tt * 0.31 + 2.0);
		float cb = 0.7885 * sin(tt * 0.23 + 2.0);
		float zoom = 1.0 + 0.22 * sin(tt * 0.4);
		float sx = 3.2 / (w * zoom);
		float sy = 2.4 / (h * zoom);
		float x0 = -1.6 / zoom;
		float y0 = -1.2 / zoom;
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			float zy0 = y0 + y * sy;
			for (int x = 0; x < w; x += 2) {
				float zx = x0 + x * sx;
				float zy = zy0;
				int k = 0;
				while (k < maxit) {
					float x2 = zx * zx;
					float y2 = zy * zy;
					if (x2 + y2 > 4.0) { break; }
					zy = 2.0 * zx * zy + cb;
					zx = x2 - y2 + ca;
					k = k + 1;
				}
				int c = (k >= maxit) ? 0 : (k + 1);
				grid[idx] = c;
				idx = idx + 1;
				if (x + 1 < w) {
					grid[idx] = c;
					idx = idx + 1;
				}
			}
		}
	}
}
