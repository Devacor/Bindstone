// JULIA — a morphing Julia set, lit and dived. c orbits just inside the
// Mandelbrot cardioid so the set stays connected while it writhes. Every pixel at
// full resolution, rows across all cores with parallel_transform. The iteration
// carries the derivative dz along with z, which buys two things: a distance
// estimate (filaments thinner than a pixel still glow) and a normal (the
// direction of z/dz), so the set is lit as embossed metal by a light circling
// it, coloured by the continuous escape count (the log-log fraction removes the
// bands) through a graded 64-hue palette; the inside is shaded by an orbit trap.
// Then the camera goes in: c holds still, a point of the set is found by
// refining toward the slowest escape, and the view dives three hundred times deeper into it, spinning,
// the iteration budget rising with the depth; at the bottom the light strobes on
// the beat and the picture folds into a six-way kaleidoscope before the camera
// rubber-bands back out and c starts moving again.

float JU_CA = 0.0;
float JU_CB = 0.0;
float JU_SC = 0.01;
float JU_X0 = 0.0;          // the view's centre
float JU_Y0 = 0.0;
float JU_RC = 1.0;          // and its turn
float JU_RS = 0.0;
float JU_LX = 0.6;          // the light, in the picture's plane
float JU_LY = 0.6;
float JU_LIFT = 0.0;        // added light: the beat and the flash
float JU_DEPTH = 0.0;       // how far into the dive: the colouring moves from escape count to distance
int JU_W = 0;
int JU_H = 0;
int JU_MAXIT = 48;
int JU_PHASE = 0;
int JU_INNER = 0;
int JU_FOLD = 0;            // kaleidoscope sectors, 0 for none
float JU_AX = 0.0;          // the attracting fixed point alpha, which the inside's orbits spiral into
float JU_AY = 0.0;
array<int> JU_ROWS = [];

function julia_row(int y) -> array<int> {
	array<int> out = [];
	int ww = JU_W;
	int mi = JU_MAXIT;
	float ca = JU_CA;
	float cb = JU_CB;
	float sc = JU_SC;
	float rc = JU_RC;
	float rs = JU_RS;
	float lx = JU_LX;
	float ly = JU_LY;
	float lift = JU_LIFT;
	int fold = JU_FOLD;
	float sector = fold > 0 ? 6.2831853 / fold : 1.0;
	float oy = (y + 0.5 - JU_H * 0.5) * sc;
	for (int x = 0; x < ww; ++x) {
		float ox = (x + 0.5 - ww * 0.5) * sc;
		float py = oy;
		if (fold > 0) {
			float r = math::sqrt(ox * ox + py * py);
			float a = math::atan2(py, ox);
			a -= math::floor(a / sector) * sector;
			if (a > sector * 0.5) { a = sector - a; }
			ox = r * math::cos(a);
			py = r * math::sin(a);
		}
		float zx = JU_X0 + ox * rc - py * rs;
		float zy = JU_Y0 + ox * rs + py * rc;
		float dx = 1.0;
		float dy = 0.0;
		float x2 = zx * zx;
		float y2 = zy * zy;
		float trap = 9.0;
		int k = 0;
		while (k < mi && x2 + y2 < 256.0) {
			float ndx = 2.0 * (zx * dx - zy * dy);
			dy = 2.0 * (zx * dy + zy * dx);
			dx = ndx;
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			float m = x2 + y2;
			trap = m < trap ? m : trap;
			++k;
		}
		if (k >= mi) {
			// inside: the angle round alpha the orbit has reached names a petal,
			// the orbit trap its light
			float pa = math::atan2(zy - JU_AY, zx - JU_AX);
			int hue = itrunc((pa + 3.1415927) * 2.546479) + (JU_PHASE >> 2);
			int tl = itrunc(math::sqrt(trap) * 9.0);
			int il = 7 - (tl > 7 ? 7 : tl) + (lift > 0.2 ? 1 : 0);
			out.push(JU_INNER + (hue & 15) * 8 + (il > 7 ? 7 : il));
			continue;
		}
		float m2 = x2 + y2;
		float nu = k + 1.0 - math::log2(math::log2(m2) * 0.5);
		// the normal: the direction of z / dz, lifted out of the plane
		float dd = dx * dx + dy * dy + 0.000000001;
		float ux = (zx * dx + zy * dy) / dd;
		float uy = (zy * dx - zx * dy) / dd;
		float ul = 1.0 / math::sqrt(ux * ux + uy * uy + 0.000000001);
		ux *= ul;
		uy *= ul;
		float shade = (ux * lx + uy * ly + 0.75) / 1.75;
		shade = shade < 0.0 ? 0.0 : shade;
		// the distance estimate: filaments near the set glow
		float de = 0.5 * math::sqrt(m2 / dd) * math::log2(m2) * 0.6931472 / sc;
		if (de < 1.5) { shade += (1.5 - de) * 0.25; }
		// deep in the dive the escape count barely changes across the view, so the
		// hue follows the distance in pixels instead: bands that hug the filaments at any depth
		float depth = JU_DEPTH;
		int ci = itrunc(math::sqrt(nu) * 9.0 * (1.0 - depth) + math::log2(de + 1.0) * 7.0 * depth);
		int lv = itrunc((shade + lift) * 7.99);
		out.push(((ci + JU_PHASE) & 63) * 8 + (lv < 0 ? 0 : (lv > 7 ? 7 : lv)));
	}
	return out;
}

class Julia : Scene {
	int inner = 0;
	float tt = 0.0;
	float beat = 0.48387097;
	float dive_t = 3.6;      // c freezes and the camera goes in
	float bottom_t = 8.0;    // the deepest point: strobe and kaleidoscope
	float climb_t = 9.4;     // the rubber band back out
	float tx = 0.0;          // the boundary point the dive is aimed at
	float ty = 0.0;
	bool aimed = false;

	void build() override {
		name = "JULIA";
		handle = "a dive into the boundary . by the type ladder";
		// 64 graded hues x 8 light levels, the top levels catching a white glint
		var stops = [[10.0, 12.0, 48.0], [40.0, 30.0, 130.0], [150.0, 40.0, 180.0], [255.0, 70.0, 120.0],
			[255.0, 150.0, 60.0], [255.0, 230.0, 140.0], [120.0, 230.0, 255.0], [30.0, 110.0, 200.0], [10.0, 12.0, 48.0]];
		int ns = stops.size() - 1;
		for (int hq = 0; hq < 64; ++hq) {
			float f = hq * ns / 64.0;
			int k = itrunc(f);
			float u = f - k;
			var a = stops[k];
			var b = stops[k + 1];
			float cr = a[0] + (b[0] - a[0]) * u;
			float cg = a[1] + (b[1] - a[1]) * u;
			float cb = a[2] + (b[2] - a[2]) * u;
			for (int l = 0; l < 8; ++l) {
				float lit = 0.12 + l / 7.0 * 0.95;
				float wh = l >= 6 ? (l - 5) * 0.22 : 0.0;
				float r = cr * lit;
				float g = cg * lit;
				float bl = cb * lit;
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, bl + (255.0 - bl) * wh);
			}
		}
		// inside: 16 jewel hues round alpha x 8 trap levels, darker than the outside
		inner = pal.size();
		for (int ih = 0; ih < 16; ++ih) {
			var c = gfx.rainbow_rgb(ih * 0.3927 + 3.6);
			for (int il = 0; il < 8; ++il) {
				float f = 0.02 + il / 7.0 * 0.3;
				pal.add(4.0 + c[0] * f * 0.8, 4.0 + c[1] * f * 0.7, 12.0 + c[2] * f);
			}
		}
	}

	void update(float t, float dt) override {
		tt = t;
	}

	float smooth(float a, float b, float t) {
		float k = (t - a) / (b - a);
		k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
		return k * k * (3.0 - 2.0 * k);
	}


	int escape_count(float zx0, float zy0, int its) {
		float zx = zx0;
		float zy = zy0;
		for (int k = 0; k < its; ++k) {
			float x2 = zx * zx;
			float y2 = zy * zy;
			if (x2 + y2 > 16.0) { return k; }
			zy = 2.0 * zx * zy + JU_CB;
			zx = x2 - y2 + JU_CA;
		}
		return its;
	}

	// a point on the set, dust or dendrite alike: the slowest-escaping point of a
	// small grid, then a grid four times finer round it, five times over
	void aim() {
		float cx = 0.35;
		float cy = 0.3;
		float span = 2.4;
		for (int round = 0; round < 6; ++round) {
			int best = -1;
			float bx = cx;
			float by = cy;
			for (int gy = 0; gy < 20; ++gy) {
				for (int gx = 0; gx < 20; ++gx) {
					float px = cx + (gx / 19.0 - 0.5) * span;
					float py = cy + (gy / 19.0 - 0.5) * span;
					int k = escape_count(px, py, 120 + round * 60);
					if (k > best) {
						best = k;
						bx = px;
						by = py;
					}
				}
			}
			cx = bx;
			cy = by;
			span *= 0.25;
		}
		tx = cx;
		ty = cy;
		aimed = true;
	}

	float pulse(float t) {
		float ph = t / beat;
		float f = ph - math::floor(ph);
		return (1.0 - f) * (1.0 - f);
	}

	void render(var& grid) override {
		float t = tt;
		// c orbits, holds still for the dive, then moves on faster
		float ot = t < dive_t ? t : (t < climb_t ? dive_t : dive_t + (t - climb_t) * 1.8);
		JU_CA = 0.7885 * math::cos(ot * 0.31 + 2.0);
		JU_CB = 0.7885 * math::sin(ot * 0.23 + 2.0);
		if (t >= dive_t && !aimed) { aim(); }
		// alpha = (1 - sqrt(1 - 4c)) / 2
		float qa = 1.0 - 4.0 * JU_CA;
		float qb = -4.0 * JU_CB;
		float qm = math::sqrt(math::sqrt(qa * qa + qb * qb));
		float qh = math::atan2(qb, qa) * 0.5;
		JU_AX = (1.0 - qm * math::cos(qh)) * 0.5;
		JU_AY = (0.0 - qm * math::sin(qh)) * 0.5;
		// depth: wide, the dive (three hundredfold, exponential), the bottom, the climb
		float into = smooth(dive_t, bottom_t, t) * (1.0 - smooth(climb_t, climb_t + 1.3, t));
		float zoom = (1.05 + 0.3 * math::sin(t * 0.4)) * math::pow(300.0, into);
		float spin = t * 0.15 + into * 9.4;
		JU_SC = 3.3 / (w * zoom);
		// zoom about the target: it keeps its place on screen while everything grows round it
		float keep = 1.0 - 1.0 / math::pow(300.0, into);
		JU_X0 = tx * keep;
		JU_Y0 = ty * keep;
		JU_RC = math::cos(spin);
		JU_RS = math::sin(spin);
		JU_MAXIT = 48 + itrunc(into * 170.0);
		JU_DEPTH = into;
		// the light circles, faster as the dive deepens, and strobes at the bottom
		float la = t * (0.9 + into * 1.6);
		JU_LX = math::cos(la);
		JU_LY = math::sin(la);
		bool at_bottom = t > bottom_t && t < climb_t + 0.4;
		JU_LIFT = at_bottom ? pulse(t) * 0.45 : pulse(t) * 0.08 * into;
		JU_FOLD = t > bottom_t + 0.25 && t < climb_t + 0.15 ? 6 : 0;
		// the hues roll, faster as the dive deepens, racing at the bottom
		float roll = t * 9.0 + into * into * 40.0 + (at_bottom ? (t - bottom_t) * 60.0 : 0.0);
		JU_PHASE = itrunc(roll);
		JU_W = w;
		JU_H = h;
		JU_INNER = inner;
		if (JU_ROWS.size() != h) {
			JU_ROWS = [];
			for (int r = 0; r < h; ++r) { JU_ROWS.push(r); }
		}
		var rows = parallel_transform(JU_ROWS, julia_row);
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			var& row = rows[y];
			for (int x = 0; x < w; ++x) {
				grid[idx] = row[x];
				++idx;
			}
		}
	}
}
