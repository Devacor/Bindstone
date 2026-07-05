// POWDER — falling-sand mini toy: sand, water, ember, plant, smoke, each a
// script class owning its behavior. Spawners keep it evolving unattended:
// sand and water pour in, plants drink and grow, embers burn them back down.

class Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) {}
}

class SandEl : Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) override {
		if (y + 1 >= h) { return; }
		int b = i + w;
		int t = c[b];
		if (t == 0) { c[b] = 2; c[i] = 0; return; }
		if (t == 3) { c[b] = 2; c[i] = 3; return; }   // heavier than water: swap
		int dir = ((x + y + fr) & 1) * 2 - 1;
		int nx = x + dir;
		if (nx >= 0 && nx < w && c[b + dir] == 0 && c[i + dir] == 0) { c[b + dir] = 2; c[i] = 0; return; }
		nx = x - dir;
		if (nx >= 0 && nx < w && c[b - dir] == 0 && c[i - dir] == 0) { c[b - dir] = 2; c[i] = 0; }
	}
}

class WaterEl : Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) override {
		if (y + 1 < h) {
			int b = i + w;
			if (c[b] == 0) { c[b] = 3; c[i] = 0; return; }
			int dir = ((x * 3 + y + fr * 5) & 1) * 2 - 1;
			int nx = x + dir;
			if (nx >= 0 && nx < w && c[b + dir] == 0 && c[i + dir] == 0) { c[b + dir] = 3; c[i] = 0; return; }
		}
		int dir = ((x + fr * 3 + y * 7) & 1) * 2 - 1;
		int nx = x + dir;
		if (nx >= 0 && nx < w && c[i + dir] == 0) { c[i + dir] = 3; c[i] = 0; return; }
		nx = x - dir;
		if (nx >= 0 && nx < w && c[i - dir] == 0) { c[i - dir] = 3; c[i] = 0; }
	}
}

class EmberEl : Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) override {
		a[i] = a[i] + 1;
		// ignite any neighboring plant
		if (x > 0 && c[i - 1] == 5) { c[i - 1] = 4; a[i - 1] = 0; }
		if (x < w - 1 && c[i + 1] == 5) { c[i + 1] = 4; a[i + 1] = 0; }
		if (y > 0 && c[i - w] == 5) { c[i - w] = 4; a[i - w] = 0; }
		if (y < h - 1 && c[i + w] == 5) { c[i + w] = 4; a[i + w] = 0; }
		if (a[i] > 22 + r.next(26)) { c[i] = 6; a[i] = 0; return; }   // burn out to smoke
		if (y + 1 >= h) { return; }
		int b = i + w;
		if (c[b] == 3) { c[i] = 6; a[i] = 0; return; }   // doused
		if ((fr & 1) == 0 && c[b] == 0) { c[b] = 4; a[b] = a[i]; c[i] = 0; a[i] = 0; }
	}
}

class PlantEl : Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) override {
		if (a[i] > 30) { return; }             // generation cap: stop creeping forever
		if (r.next(100) >= 6) { return; }
		// drink an adjacent water cell, then sprout into an empty neighbor
		int drink = -1;
		if (x > 0 && c[i - 1] == 3) { drink = i - 1; }
		else if (x < w - 1 && c[i + 1] == 3) { drink = i + 1; }
		else if (y > 0 && c[i - w] == 3) { drink = i - w; }
		else if (y < h - 1 && c[i + w] == 3) { drink = i + w; }
		if (drink < 0) { return; }
		c[drink] = 0;
		int sprout = -1;
		if (y > 0 && c[i - w] == 0) { sprout = i - w; }
		else if (x > 0 && y > 0 && c[i - w - 1] == 0) { sprout = i - w - 1; }
		else if (x < w - 1 && y > 0 && c[i - w + 1] == 0) { sprout = i - w + 1; }
		else if (x > 0 && c[i - 1] == 0) { sprout = i - 1; }
		else if (x < w - 1 && c[i + 1] == 0) { sprout = i + 1; }
		if (sprout >= 0) {
			c[sprout] = 5;
			a[sprout] = a[i] + 1;
		}
	}
}

class SmokeEl : Element {
	void step(var& c, var& a, int w, int h, int i, int x, int y, var r, int fr) override {
		a[i] = a[i] + 1;
		if (a[i] > 26) { c[i] = 0; a[i] = 0; return; }
		if (y == 0) { c[i] = 0; a[i] = 0; return; }
		int wob = ((x + fr + a[i] * 3) % 3) - 1;
		int nx = x + wob;
		if (nx >= 0 && nx < w && c[i - w + wob] == 0) { c[i - w + wob] = 6; a[i - w + wob] = a[i]; c[i] = 0; a[i] = 0; return; }
		if (c[i - w] == 0) { c[i - w] = 6; a[i - w] = a[i]; c[i] = 0; a[i] = 0; }
	}
}

class SandBox : Scene {
	var cells = null;
	var aux = null;
	var el_sand = null;      // element singletons live in fields, dispatch by if-chain:
	var el_water = null;     // object-in-array-literal construction trips the same
	var el_ember = null;     // interpreter corruption family the README documents
	var el_plant = null;
	var el_smoke = null;
	int fr = 0;

	void build() override {
		name = "POWDER";
		handle = "sand.water.ember.plant . by ref_lvalue";
		cells = gfx.make_grid(w * h);
		aux = gfx.make_grid(w * h);
		el_sand = SandEl();
		el_water = WaterEl();
		el_ember = EmberEl();
		el_plant = PlantEl();
		el_smoke = SmokeEl();

		pal.add_bg(9, 7, 14);                    // 0 empty
		pal.add_bg(88, 84, 96);                  // 1 rock
		pal.add_bg(70, 66, 78);                  // 2 rock alt
		pal.add_bg(214, 178, 96);                // 3 sand
		pal.add_bg(196, 158, 82);                // 4 sand alt
		pal.add_bg(230, 198, 120);               // 5 sand bright
		pal.add_bg(38, 92, 210);                 // 6 water
		pal.add_bg(30, 76, 182);                 // 7 water deep
		pal.add_bg(70, 130, 235);                // 8 water light
		pal.add_bg(255, 140, 30);                // 9 ember
		pal.add_bg(255, 90, 20);                 // 10 ember hot
		pal.add_bg(255, 200, 60);                // 11 ember flash
		pal.add_bg(200, 60, 16);                 // 12 ember dim
		pal.add_bg(52, 150, 60);                 // 13 plant
		pal.add_bg(38, 120, 48);                 // 14 plant dark
		pal.add_bg(80, 190, 84);                 // 15 plant light
		pal.add_bg(70, 70, 80);                  // 16 smoke
		pal.add_bg(52, 52, 62);                  // 17 smoke mid
		pal.add_bg(36, 36, 44);                  // 18 smoke faint

		// rock floor + two shelves making basins
		int ground = (h - 1) * w;
		for (int x = 0; x < w; ++x) { cells[ground + x] = 1; }
		int shelf1 = (h - 9) * w;
		for (int x = w / 8; x < w / 8 + w / 4; ++x) { cells[shelf1 + x] = 1; }
		int shelf2 = (h - 15) * w;
		for (int x = w - w / 3 - w / 8; x < w - w / 8; ++x) { cells[shelf2 + x] = 1; }
		// plant seeds along the floor
		for (int k = 0; k < 10; ++k) {
			int x = 4 + rng.next(w - 8);
			cells[ground - w + x] = 5;
		}
	}

	void update(float t, float dt) override {
		fr = fr + 1;
		do_spawn();
		do_fall();
		do_smoke();
	}

	void do_spawn() {
		int sx = w / 5 + rng.next(5) - 2;
		if (cells[sx] == 0) { cells[sx] = 2; }
		sx = w / 5 + 2 + rng.next(5);
		if (cells[sx] == 0) { cells[sx] = 2; }
		int wx = (w * 4) / 5 + rng.next(5) - 2;
		if (cells[wx] == 0) { cells[wx] = 3; }
		wx = (w * 4) / 5 - 2 - rng.next(5);
		if (cells[wx] == 0) { cells[wx] = 3; }
		if (fr % 70 == 0) {
			for (int k = 0; k < 4; ++k) {
				int ex = w / 2 - 4 + rng.next(9);
				if (cells[ex] == 0) { cells[ex] = 4; aux[ex] = 0; }
			}
		}
	}

	// bottom-up: falling and static matter (alternate x sweep per frame)
	void do_fall() {
		for (int y = h - 2; y >= 0; --y) {
			int row = y * w;
			if ((fr & 1) == 0) {
				for (int x = 0; x < w; ++x) {
					int i = row + x;
					int t2 = cells[i];
					if (t2 == 2) { el_sand.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 3) { el_water.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 4) { el_ember.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 5) { el_plant.step(cells, aux, w, h, i, x, y, rng, fr); }
				}
			} else {
				for (int x = w - 1; x >= 0; --x) {
					int i = row + x;
					int t2 = cells[i];
					if (t2 == 2) { el_sand.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 3) { el_water.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 4) { el_ember.step(cells, aux, w, h, i, x, y, rng, fr); }
					else if (t2 == 5) { el_plant.step(cells, aux, w, h, i, x, y, rng, fr); }
				}
			}
		}
	}

	// top-down: rising smoke
	void do_smoke() {
		int n = w * h;
		for (int i = 0; i < n; ++i) {
			if (cells[i] == 6) {
				el_smoke.step(cells, aux, w, h, i, i % w, i / w, rng, fr);
			}
		}
	}

	void render(var& grid) override {
		int idx = 0;
		int g = 0;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int t = cells[idx];
				g = 0;
				if (t == 2) { g = 3 + (x * 3 + y * 5) % 3; }
				else if (t == 3) { g = 6 + (x + y + fr / 6) % 3; }
				else if (t == 5) { g = 13 + (x * 7 + y * 3) % 3; }
				else if (t == 4) { g = 9 + (aux[idx] + x) % 4; }
				else if (t == 1) { g = 1 + (x + y) % 2; }
				else if (t == 6) { g = 16 + aux[idx] / 9; }
				grid[idx] = g;
				idx = idx + 1;
			}
		}
	}
}
