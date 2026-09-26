// FINALE — the word JAISCRIPT ignites out of a wall of fire and burns in
// rolling rainbow flame, then the fire dies down and leaves the word glowing
// while the credits come up. It IS the fire scene (a subclass): the letters
// are a heat-source mask stamped from the pixel font at double size, fed to
// the same convection pass, with sparks thrown while the wall roars.

class Finale : Fire {
	float cap_t = 13.0;

	void build() override {
		name = "FINALE";
		handle = "jaiscript forever . rainbow-fire salute";
		build_fire();
		mask = gfx.make_grid(w * (h + 2));
		string text = "JAISCRIPT";
		int s = w >= 98 ? 2 : 1;
		int pitch = s == 2 ? 11 : 6;
		int x0 = (w - (text.size() * pitch - 1)) / 2;
		int y0 = h / 2 - 7 * s - 2;
		for (int li = 0; li < text.size(); ++li) {
			var bits = FONT5[text.substr(li, 1)];
			for (int ry = 0; ry < 7 * s; ++ry) {
				int b = bits[ry / s];
				for (int rx = 0; rx < 5 * s; ++rx) {
					if (((b >> (4 - rx / s)) & 1) == 1) {
						mask[(y0 + ry) * w + x0 + li * pitch + rx] = 1;
					}
				}
			}
		}
	}

	void update(float t, float dt) override {
		tt = t;
		fr = fr + 1;
		rainbow_on = t > 4.5 ? 1 : 0;             // the word burns rainbow
		if (rainbow_on == 1) { hue_t += dt * 3.0; }
		// the fire wall roars for the reveal, then dies down for the afterglow
		int amp = 42;
		amp = t > 10.5 ? itrunc(42.0 - (t - 10.5) * 19.0) : amp;
		amp = amp < 0 ? 0 : amp;
		seed_rows(amp, amp / 2 + 2);
		convect(t > 2.2);
		move_sparks(dt, amp > 12);
	}

	// the letters themselves burn white-hot over their own flames so the word
	// reads; a dark rim of one pixel separates them from the fire around them
	void render(var& grid) override {
		int b = bank();
		int n = w * h;
		var& ht = heat;
		var& mk = mask;
		bool lit = tt > 2.2;
		for (int i = 0; i < n; ++i) {
			int v = ht[i];
			grid[i] = (v > 72 ? 72 : v) + b;
		}
		if (lit) {
			int ww = w;
			for (int i2 = ww; i2 < n - ww; ++i2) {
				if (mk[i2] == 1) {
					grid[i2] = b + 64 + ((i2 * 7 + fr) & 7);
				} else if (mk[i2 - 1] == 1 || mk[i2 + 1] == 1 || mk[i2 - ww] == 1 || mk[i2 + ww] == 1) {
					int v2 = ht[i2] / 3;
					grid[i2] = b + v2;
				}
			}
		}
		draw_sparks(grid, b);
	}

	string caption() override {
		if (tt > cap_t + 1.8) { return "vm + interpreter . byte-identical frames . 16 scenes"; }
		if (tt > cap_t) { return "every pixel of this reel is jaiscript"; }
		return "";
	}
}
