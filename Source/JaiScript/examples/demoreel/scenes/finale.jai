// FINALE — the word JAISCRIPT ignites out of a wall of fire and burns in
// rolling rainbow flame, then the fire dies down and leaves the word glowing.
// Built entirely from the fire sim (the reel's most battle-proven component):
// the text is a convection heat mask. Earlier drafts used a Spark class driven
// by a coroutine — both shapes trip engine bugs documented in the README.

class Finale : Scene {
	var heat = [];
	var cool = [];
	var mask = [];      // 1 where a glyph pixel of "JAISCRIPT" lives
	int levels = 37;
	int phases = 6;
	int fr = 0;
	int rainbow_on = 0;
	float hue_t = 0.0;
	float tt = 0.0;

	var letter_rows(string ch) {
		if (ch == "J") { return ["#####", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "]; }
		if (ch == "A") { return [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"]; }
		if (ch == "I") { return ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"]; }
		if (ch == "S") { return [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "]; }
		if (ch == "C") { return [" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"]; }
		if (ch == "R") { return ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"]; }
		if (ch == "P") { return ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "]; }
		return ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "];   // T
	}

	void stamp_letter(string ch, int li, int x0, int y0) {
		var rows = letter_rows(ch);
		for (int ry = 0; ry < 7; ++ry) {
			string row = rows[ry];
			stamp_row(row, li, ry, x0, y0);
		}
	}

	void stamp_row(string row, int li, int ry, int x0, int y0) {
		for (int rx = 0; rx < 5; ++rx) {
			if (row.substr(rx, 1) == "#") {
				mask[(y0 + ry) * w + x0 + li * 6 + rx] = 1;
			}
		}
	}

	void build() override {
		name = "FINALE";
		handle = "jaiscript forever . rainbow-fire salute";
		heat = gfx.make_grid(w * (h + 2));
		mask = gfx.make_grid(w * (h + 2));
		for (int i = 0; i < 1024; ++i) {
			cool.push(rng.next(100) < 42 ? 1 : 0);
		}
		// same palette as FIRE: classic ramp + six rainbow hue banks
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
		string text = "JAISCRIPT";
		int x0 = (w - (text.size() * 6 - 1)) / 2;
		int y0 = h / 2 - 6;
		for (int li = 0; li < text.size(); ++li) {
			stamp_letter(text.substr(li, 1), li, x0, y0);
		}
	}

	void update(float t, float dt) override {
		tt = t;
		fr = fr + 1;
		rainbow_on = t > 4.5 ? 1 : 0;             // the word burns rainbow
		if (rainbow_on == 1) { hue_t += dt * 3.0; }

		// fire wall roars for the reveal, then dies down for the afterglow
		int amp = 20;
		amp = t > 10.5 ? itrunc(20.0 - (t - 10.5) * 9.0) : amp;
		amp = amp < 0 ? 0 : amp;
		int base = h * w;
		for (int x = 0; x < w; ++x) {
			int v = amp + rng.next(amp / 2 + 2);
			v = rng.next(100) < 7 ? rng.next(amp / 3 + 2) : v;
			heat[base + x] = v;
			heat[base + w + x] = v;
		}

		int coolbase = fr * 101;
		bool word_lit = t > 2.2;
		for (int y = 0; y < h; ++y) {
			int row = y * w;
			int below = row + w;
			int below2 = row + w + w;
			for (int x = 0; x < w; ++x) {
				int v = (heat[below + xw(x, -1)] + heat[below + x] + heat[below + xw(x, 1)] + heat[below2 + x]) / 4;
				v -= cool[(row + x * 13 + coolbase) & 1023];
				// the text mask is its own heat source: the word emerges and stays lit
				if (word_lit && mask[row + x] == 1) {
					int glow = 30 + (cool[(x * 7 + y * 3 + fr) & 1023] * 6);
					v = v > glow ? v : glow;
				}
				heat[row + x] = (v > 0) ? v : 0;
			}
		}
	}

	int xw(int x, int d) {
		int nx = x + d;
		nx = nx < 0 ? 0 : nx;
		nx = nx > w - 1 ? w - 1 : nx;
		return nx;
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
