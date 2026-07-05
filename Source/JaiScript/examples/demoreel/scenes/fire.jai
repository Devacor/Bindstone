// FIRE — bottom-seeded convection blur, the house classic. Midway through the
// scene it ignites into RAINBOW fire (the palette carries six precomputed
// hue-rolled banks; render just offsets into them), because this codebase is
// patrolled by a wingless rainbow-fire dragon.

class Fire : Scene {
	var heat = [];       // (h+2) rows: two hidden seed rows below the screen
	var cool = [];       // prebuilt 0/1 cooling noise, indexed cheaply per cell
	int levels = 37;
	int phases = 6;      // rainbow hue-roll banks
	int fr = 0;
	int rainbow_on = 0;
	float hue_t = 0.0;

	void build() override {
		name = "FIRE";
		handle = "ember chapel -> rainbow mode . for devacor";
		heat = gfx.make_grid(w * (h + 2));
		for (int i = 0; i < 1024; ++i) {
			cool.push(rng.next(100) < 42 ? 1 : 0);
		}
		// entries 0..36: classic fire ramp
		for (int i = 0; i < levels; ++i) {
			float f = i / 36.0;
			float r = 255.0 * f * 2.2;
			r = r > 255.0 ? 255.0 : r;
			float g = f > 0.28 ? 255.0 * (f - 0.28) * 1.7 : 0.0;
			g = g > 255.0 ? 255.0 : g;
			float b = f > 0.75 ? 255.0 * (f - 0.75) * 3.2 : 0.0;
			b = b > 255.0 ? 255.0 : b;
			pal.add_bg(r, g, b);
		}
		// entries 37+p*37 .. : rainbow banks, hue rolled per bank
		for (int p = 0; p < phases; ++p) {
			float base_hue = p * 6.28318530718 / phases;
			for (int i = 0; i < levels; ++i) {
				float f = i / 36.0;
				var c = gfx.rainbow_rgb(base_hue + f * 3.6);
				float cr = c[0];
				float cg = c[1];
				float cb = c[2];
				float glow = 0.12 + 0.88 * f;
				pal.add_bg(cr * glow, cg * glow, cb * glow);
			}
		}
	}

	void update(float t, float dt) override {
		fr = fr + 1;
		rainbow_on = t > 5.5 ? 1 : 0;
		if (rainbow_on == 1) { hue_t += dt * 3.0; }

		// seed the two hidden bottom rows: mostly furious, occasionally gapped
		int base = h * w;
		for (int x = 0; x < w; ++x) {
			int v = 20 + rng.next(17);
			v = rng.next(100) < 7 ? rng.next(9) : v;
			heat[base + x] = v;
			heat[base + w + x] = v;
		}

		// convection: each cell pulls from the three below and one two-below
		int coolbase = fr * 101;
		for (int y = 0; y < h; ++y) {
			int row = y * w;
			int below = row + w;
			int below2 = row + w + w;
			for (int x = 0; x < w; ++x) {
				int xl = (x > 0) ? x - 1 : 0;
				int xr = (x < w - 1) ? x + 1 : w - 1;
				int v = (heat[below + xl] + heat[below + x] + heat[below + xr] + heat[below2 + x]) / 4;
				v -= cool[(row + x * 13 + coolbase) & 1023];
				heat[row + x] = (v > 0) ? v : 0;
			}
		}
	}

	void render(var& grid) override {
		int bank = 0;
		if (rainbow_on == 1) { bank = 37 + (itrunc(hue_t * 4.0) % phases) * 37; }
		int n = w * h;
		for (int i = 0; i < n; ++i) {
			grid[i] = heat[i] + bank;
		}
	}
}
