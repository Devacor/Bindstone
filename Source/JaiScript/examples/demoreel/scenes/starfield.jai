// STARFIELD — perspective star streaming with speed-scaled trails.
// Parallel arrays instead of a Star class on purpose: this is the "hot inner
// loop" counterpoint to boids.jai's class-per-particle style.

class Starfield : Scene {
	var sx = [];
	var sy = [];
	var sz = [];
	int nstars = 150;
	float tt = 0.0;
	float speed = 0.5;

	void build() override {
		name = "STARFIELD";
		handle = "hyperspace bound . greets: parser crew";
		pal.add_bg(2, 2, 10);                    // 0: deep space
		pal.add_fg_bg(70, 80, 140, 2, 2, 10, ".");     // 1: far
		pal.add_fg_bg(110, 120, 190, 2, 2, 10, ".");   // 2
		pal.add_fg_bg(150, 165, 230, 2, 2, 10, "+");   // 3
		pal.add_fg_bg(200, 210, 250, 2, 2, 10, "*");   // 4
		pal.add_fg_bg(240, 244, 255, 2, 2, 10, "o");   // 5: near
		pal.add_fg_bg(255, 255, 255, 2, 2, 10, "@");   // 6: in your face
		pal.add_fg_bg(90, 100, 170, 2, 2, 10, "-");    // 7: trail
		for (int i = 0; i < nstars; ++i) {
			sx.push((rng.next(2000) - 1000) * 0.0011);
			sy.push((rng.next(2000) - 1000) * 0.0011);
			sz.push(0.08 + rng.next(1000) * 0.00125);
		}
	}

	void update(float t, float dt) override {
		tt = t;
		speed = 0.55 + 0.4 * sin(t * 0.45);
		if (speed < 0.25) { speed = 0.25; }
		for (int i = 0; i < nstars; ++i) {
			sz[i] = sz[i] - dt * speed * sz[i];   // exponential rush
			if (sz[i] < 0.045) {
				sx[i] = (rng.next(2000) - 1000) * 0.0011;
				sy[i] = (rng.next(2000) - 1000) * 0.0011;
				sz[i] = 1.15 + rng.next(1000) * 0.0002;
			}
		}
	}

	void render(var& grid) override {
		gfx.clear_grid(grid);
		float cx = w * 0.5;
		float cy = h * 0.5;
		float fx = w * 0.42;
		float fy = h * 0.42;
		for (int i = 0; i < nstars; ++i) {
			float z = sz[i];
			int px = ifloor(cx + sx[i] / z * fx);
			int py = ifloor(cy + sy[i] / z * fy);
			if (px < 0 || px >= w || py < 0 || py >= h) { continue; }
			int level = itrunc(6.5 - z * 5.5);
			level = level < 1 ? 1 : level;
			level = level > 6 ? 6 : level;
			// trail: where the star was a moment ago, drawn dimmer
			if (level >= 4) {
				float zb = z * (1.0 + speed * 0.06);
				int qx = ifloor(cx + sx[i] / zb * fx);
				int qy = ifloor(cy + sy[i] / zb * fy);
				if (qx >= 0 && qx < w && qy >= 0 && qy < h && (qx != px || qy != py)) {
					int ti = qy * w + qx;
					if (grid[ti] == 0) { grid[ti] = 7; }
				}
			}
			grid[py * w + px] = level;
		}
	}
}
