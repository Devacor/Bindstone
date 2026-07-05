// BOIDS — murmuration with fading trails and one hungry hawk. Each boid is a
// script class instance (the object-y counterpoint to starfield's flat arrays).

class Boid {
	float x = 0.0;
	float y = 0.0;
	float vx = 0.0;
	float vy = 0.0;
}

class Boids : Scene {
	var flock = [];
	var trail = null;
	int nb = 36;
	float px = 10.0;    // the hawk
	float py = 10.0;
	float pvx = 4.0;
	float pvy = 0.0;
	float tt = 0.0;

	void build() override {
		name = "BOIDS";
		handle = "murmuration . by the vm crew";
		trail = gfx.make_grid(w * h);
		pal.add_bg(4, 5, 14);                            // 0 sky
		pal.add_fg_bg(20, 40, 70, 4, 5, 14, ".");        // 1 oldest trail
		pal.add_fg_bg(24, 62, 100, 4, 5, 14, ".");       // 2
		pal.add_fg_bg(28, 88, 130, 4, 5, 14, ":");       // 3
		pal.add_fg_bg(36, 118, 160, 4, 5, 14, ":");      // 4
		pal.add_fg_bg(48, 152, 190, 4, 5, 14, "*");      // 5
		pal.add_fg_bg(80, 200, 220, 4, 5, 14, "*");      // 6 newest trail
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, ">");     // 7 E
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "/");     // 8 NE
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "^");     // 9 N
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "\\");    // 10 NW
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "<");     // 11 W
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "/");     // 12 SW
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "v");     // 13 S
		pal.add_fg_bg(235, 245, 255, 4, 5, 14, "\\");    // 14 SE
		pal.add_fg_bg(255, 90, 60, 4, 5, 14, "@");       // 15 hawk
		for (int i = 0; i < nb; ++i) {
			var b = Boid();
			b.x = rng.next(w * 10) * 0.1;
			b.y = rng.next(h * 10) * 0.1;
			float a = rng.next(628) * 0.01;
			b.vx = cos(a) * 6.0;
			b.vy = sin(a) * 3.0;
			flock.push(b);
		}
	}

	void update(float t, float dt) override {
		tt = t;
		if (dt <= 0.0) { return; }
		// wander target keeps the flock touring the screen
		float wx = w * (0.5 + 0.34 * sin(t * 0.37));
		float wy = h * (0.5 + 0.3 * sin(t * 0.53 + 1.7));
		hawk_update(dt);
		for (int i = 0; i < nb; ++i) {
			steer_one(i, wx, wy, dt);
		}
	}

	void hawk_update(float dt) {
		float cx = 0.0;
		float cy = 0.0;
		for (int i = 0; i < nb; ++i) {
			var& b = flock[i];
			cx += b.x;
			cy += b.y;
		}
		cx /= nb;
		cy /= nb;
		pvx += (cx - px) * 0.55 * dt;
		pvy += (cy - py) * 0.3 * dt;
		float psp = sqrt(pvx * pvx + pvy * pvy * 4.0);
		if (psp > 9.0) { pvx *= 9.0 / psp; pvy *= 9.0 / psp; }
		px += pvx * dt;
		py += pvy * dt;
		if (px < 1.0) { px = 1.0; pvx = -pvx; }
		if (px > w - 2.0) { px = w - 2.0; pvx = -pvx; }
		if (py < 1.0) { py = 1.0; pvy = -pvy; }
		if (py > h - 2.0) { py = h - 2.0; pvy = -pvy; }
	}

	// classic three rules + wander + hawk fear, one boid per call
	void steer_one(int i, float wx, float wy, float dt) {
		var& b = flock[i];
		float bx = b.x;
		float by = b.y;
		float ax = 0.0;
		float ay = 0.0;
		float mx = 0.0;
		float my = 0.0;
		float vx_sum = 0.0;
		float vy_sum = 0.0;
		int near = 0;
		for (int j = 0; j < nb; ++j) {
			if (j == i) { continue; }
			var& o = flock[j];
			float dx = o.x - bx;
			float dy = (o.y - by) * 2.0;
			float d2 = dx * dx + dy * dy;
			if (d2 > 140.0) { continue; }
			near = near + 1;
			mx += o.x;
			my += o.y;
			vx_sum += o.vx;
			vy_sum += o.vy;
			if (d2 < 9.0 && d2 > 0.0001) {
				ax -= dx / d2 * 10.0;
				ay -= dy / d2 * 5.0;
			}
		}
		if (near > 0) {
			ax += (mx / near - bx) * 0.9;             // cohesion
			ay += (my / near - by) * 0.9;
			ax += (vx_sum / near - b.vx) * 1.4;       // alignment
			ay += (vy_sum / near - b.vy) * 1.4;
		}
		ax += (wx - bx) * 0.22;                       // wander
		ay += (wy - by) * 0.22;
		float hx = bx - px;                           // flee the hawk
		float hy = (by - py) * 2.0;
		float hd2 = hx * hx + hy * hy;
		if (hd2 < 110.0 && hd2 > 0.001) {
			ax += hx / hd2 * 160.0;
			ay += hy / hd2 * 80.0;
		}
		b.vx += ax * dt;
		b.vy += ay * dt;
		float sp = sqrt(b.vx * b.vx + b.vy * b.vy * 4.0);
		if (sp > 11.0) { b.vx *= 11.0 / sp; b.vy *= 11.0 / sp; }
		if (sp < 3.0 && sp > 0.001) { b.vx *= 3.0 / sp; b.vy *= 3.0 / sp; }
		b.x += b.vx * dt;
		b.y += b.vy * dt;
		if (b.x < 0.0) { b.x += w; }
		if (b.x >= w) { b.x -= w; }
		if (b.y < 0.0) { b.y += h; }
		if (b.y >= h) { b.y -= h; }
	}

	int dir_glyph(float vx, float vy) {
		float avx = vx < 0.0 ? -vx : vx;
		float avy = (vy < 0.0 ? -vy : vy) * 2.0;
		if (avx > avy * 2.4) { return vx > 0.0 ? 7 : 11; }
		if (avy > avx * 2.4) { return vy > 0.0 ? 13 : 9; }
		if (vx > 0.0) { return vy > 0.0 ? 14 : 8; }
		return vy > 0.0 ? 12 : 10;
	}

	void render(var& grid) override {
		int n = w * h;
		for (int i = 0; i < n; ++i) {
			int tv = trail[i];
			tv = tv > 0 ? tv - 1 : tv;
			trail[i] = tv;
			grid[i] = tv;
		}
		for (int k = 0; k < nb; ++k) {
			var& b = flock[k];
			int x = ifloor(b.x);
			int y = ifloor(b.y);
			if (x < 0 || x >= w || y < 0 || y >= h) { continue; }
			int i = y * w + x;
			trail[i] = 6;
			grid[i] = dir_glyph(b.vx, b.vy);
		}
		int hx = ifloor(px);
		int hy = ifloor(py);
		if (hx >= 0 && hx < w && hy >= 0 && hy < h) {
			grid[hy * w + hx] = 15;
		}
	}
}
