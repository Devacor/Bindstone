// DONUT — the torus homage, z-buffered and luminance-shaded, molten copper.
// All trig outside the point loop comes from build()-time tables; the per-frame
// rotation costs exactly four host trig calls.

class Donut : Scene {
	var costh = [];
	var sinth = [];
	var cosph = [];
	var sinph = [];
	var zbuf = [];
	int nth = 28;
	int nph = 80;
	float tt = 0.0;

	void build() override {
		name = "DONUT";
		handle = "torus and effect . by strong_ptr";
		for (int i = 0; i < nth; ++i) {
			float a = i * 6.28318530718 / nth;
			costh.push(cos(a));
			sinth.push(sin(a));
		}
		for (int j = 0; j < nph; ++j) {
			float a = j * 6.28318530718 / nph;
			cosph.push(cos(a));
			sinph.push(sin(a));
		}
		zbuf = gfx.make_grid(w * h);
		pal.add_bg(4, 2, 8);   // 0: void
		var glyphs = [".", ",", "-", "~", ":", ";", "=", "!", "*", "#", "$", "@"];
		for (int i = 0; i < 12; ++i) {
			float f = i / 11.0;
			string gl = glyphs[i];   // unpack: raw element args miss typed-param overloads
			pal.add_fg_bg(80.0 + f * 175.0, 30.0 + f * 190.0, 10.0 + f * 110.0, 4, 2, 8, gl);
		}
	}

	void update(float t, float dt) override {
		tt = t;
	}

	void render(var& grid) override {
		int nn = w * h;
		for (int q = 0; q < nn; ++q) { grid[q] = 0; zbuf[q] = 0; }
		float ca = cos(tt * 0.9);
		float sa = sin(tt * 0.9);
		float cb = cos(tt * 0.53 + 1.2);
		float sb = sin(tt * 0.53 + 1.2);
		float r1 = 1.0;
		float r2 = 2.0;
		float k2 = 5.0;
		float k1 = w * k2 * 3.0 / (8.0 * (r1 + r2));
		float cx = w * 0.5;
		float cy = h * 0.5;
		for (int j = 0; j < nph; ++j) {
			float cp = cosph[j];
			float sp = sinph[j];
			for (int i = 0; i < nth; ++i) {
				float ct = costh[i];
				float st2 = sinth[i];
				float circx = r2 + r1 * ct;
				float circy = r1 * st2;
				float x3 = circx * (cb * cp + sa * sb * sp) - circy * ca * sb;
				float y3 = circx * (sb * cp - sa * cb * sp) + circy * ca * cb;
				float z3 = k2 + ca * circx * sp + circy * sa;
				float ooz = 1.0 / z3;
				int px = ifloor(cx + k1 * ooz * x3);
				int py = ifloor(cy - k1 * ooz * y3 * 0.5);
				if (px < 0 || px >= w || py < 0 || py >= h) { continue; }
				float lum = cp * ct * sb - ca * ct * sp - sa * st2 + cb * (ca * st2 - ct * sa * sp);
				lum = lum < 0.02 ? 0.02 : lum;    // keep the dark side faintly drawn
				int cell = py * w + px;
				if (ooz > zbuf[cell]) {
					zbuf[cell] = ooz;
					int shade = itrunc(lum * 7.7 + 1.0);
					shade = shade > 12 ? 12 : shade;
					grid[cell] = shade;
				}
			}
		}
	}
}
