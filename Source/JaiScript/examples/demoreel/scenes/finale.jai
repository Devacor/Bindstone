// FINALE — an eight-dimensional climax. An 8-cube (256 vertices, 1024 edges)
// unfolds out of a point one dimension at a time, a line, a square, a cube, on
// up to eight, while it turns through eight planes of 8D rotation and projects
// down to the screen, every edge glowing in the hue of the axis it runs along.
// On the beat it shatters: its vertices burst into sparks that swirl onto the
// pixels of the word JAISCRIPT, and the word slams in with a white flash, two
// shockwaves and a screen shake as the fire wall erupts beneath it. The flames
// pump on every beat and burn into rainbow with the hypercube still turning
// behind them; then the wall dies, the letters cool to neon and the credits
// roll. It is the fire scene's subclass: the letters are heat sources stamped
// from the pixel font and fed to the same convection pass.

class Finale : Fire {
	array<int> letter_at = [];   // the letters' pixels
	array<int> letter_of = [];   // and which letter each belongs to
	array<int> rim_at = [];      // the pixels around the letters, drawn dark
	array<int> fuel = [];        // per column: the heat the letters feed this frame
	int word_x = 0;
	int word_w = 0;
	int word_top = 0;
	int word_rows = 0;

	array<float> cube = [];      // the 8-cube's vertices, 8 coordinates each, rotated and unfolded
	array<float> sx = [];        // their screen positions and depths
	array<float> sy = [];
	array<float> sz = [];
	array<float> glow = [];      // the hypercube's light, per pixel
	array<int> glow_hue = [];    // and the axis of its brightest edge there
	array<int> shake_row = [];
	int hyper = 0;               // palette: 8 axes x 12 levels
	int white = 0;               // 8 levels of flash, dark to white
	float beat = 0.48387097;     // the soundtrack's 124 bpm

	array<float> pfx = [];       // the shatter: one spark per letter pixel, from a vertex
	array<float> pfy = [];
	array<int> pfaxis = [];

	float unfold_t = 0.8;        // the first dimension opens
	float shatter_t = 6.3;       // the cube bursts
	float slam_t = 7.75;         // the word lands and the fire erupts
	float rainbow_t = 9.7;
	float wall_out_t = 14.5;     // the wall starts dying
	float embers_t = 15.5;       // the letters stop feeding the flames
	float neon_t = 17.5;         // ... and have cooled to neon
	float credits_t = 16.5;
	float credit_rows_per_s = 5.0;

	// [kind, left column, right column]: a "role" row splits at the gutter
	var credits = [
		["title", "THE JAISCRIPT DEMOREEL", ""],
		["", "", ""],
		["body", "eleven scenes . five transitions", ""],
		["body", "every pixel computed by the jaiscript vm", ""],
		["", "", ""],
		["role", "DONUT", "a sphere-traced torus"],
		["role", "JULIA", "a morphing julia set"],
		["role", "LAVALAMP", "lit metaballs"],
		["role", "PIPEDREAM", "the 3d pipes screensaver"],
		["role", "BOIDS", "a murmuration and a falcon"],
		["role", "KINSTEIN 3D", "a textured raycaster"],
		["role", "CANYONRUN", "voxelspace terrain"],
		["role", "CHROME", "a ray tracer"],
		["role", "OSSUARY", "a skull spider on a morphing gyroid"],
		["role", "MAELSTROM", "navier-stokes ink"],
		["role", "FINALE", "an 8-cube, and you are here"],
		["", "", ""],
		["body", "half-block pixels, two to a cell", ""],
		["body", "deterministic to the last byte", ""],
		["", "", ""],
		["", "", ""],
		["title", "thanks for watching", ""],
		["link", "jaiscript.com", ""]
	];

	void build() override {
		name = "FINALE";
		handle = "eight dimensions . jaiscript forever";
		replay = 24.0;
		build_fire();
		fuel = gfx.make_grid(w);
		stamp_word("JAISCRIPT");
		hyper = pal.size();
		for (int d = 0; d < 8; ++d) {
			var c = gfx.rainbow_rgb(d * 0.7853982 + 0.4);
			float cr = c[0]; float cg = c[1]; float cb = c[2];
			for (int l = 0; l < 12; ++l) {
				float f = (l + 1) / 12.0;
				float wh = f > 0.7 ? (f - 0.7) * 2.4 : 0.0;
				float k = f < 0.7 ? f / 0.7 : 1.0;
				pal.add(cr * k + (255.0 - cr * k) * wh, cg * k + (255.0 - cg * k) * wh, cb * k + (255.0 - cb * k) * wh);
			}
		}
		white = pal.ramp(8, 30.0, 24.0, 60.0, 255.0, 255.0, 255.0);
		glow = gfx.make_fgrid(w * h);
		glow_hue = gfx.make_grid(w * h);
		shake_row = gfx.make_grid(w);
		cube = gfx.make_fgrid(256 * 8);
		sx = gfx.make_fgrid(256);
		sy = gfx.make_fgrid(256);
		sz = gfx.make_fgrid(256);
		for (int k2 = 0; k2 < letter_at.size(); ++k2) {
			pfx.push(0.0);
			pfy.push(0.0);
			pfaxis.push(rng.next(8));
		}
	}

	// the 5x7 font at double size, its second column kept single so a glyph is
	// 9 px wide on a 10 px pitch; narrow screens get it at single size
	void stamp_word(string text) {
		bool big = w >= 95;
		array<int> font_col = [0, 1, 2, 3, 4];
		if (big) { font_col = [0, 0, 1, 2, 2, 3, 3, 4, 4]; }
		int sy_ = big ? 2 : 1;
		int pitch = font_col.size() + 1;
		word_w = text.size() * pitch - 1;
		word_x = (w - word_w) / 2;
		word_rows = 7 * sy_;
		word_top = h / 2 - word_rows - 2;
		array<int> mask = gfx.make_grid(w * h);
		for (int li = 0; li < text.size(); ++li) {
			var bits = FONT5[text.substr(li, 1)];
			for (int ry = 0; ry < word_rows; ++ry) {
				int b = bits[ry / sy_];
				for (int rx = 0; rx < font_col.size(); ++rx) {
					if (((b >> (4 - font_col[rx])) & 1) == 1) {
						int i = (word_top + ry) * w + word_x + li * pitch + rx;
						mask[i] = 1;
						letter_at.push(i);
						letter_of.push(li);
					}
				}
			}
		}
		for (int i = w; i < w * (h - 1); ++i) {
			if (mask[i] == 0 && (mask[i - 1] == 1 || mask[i + 1] == 1 || mask[i - w] == 1 || mask[i + w] == 1)) { rim_at.push(i); }
		}
	}

	// how hard the current beat hits: 1 on the beat, falling away before the next
	float pulse(float t) {
		float ph = t / beat;
		float f = ph - math::floor(ph);
		return (1.0 - f) * (1.0 - f);
	}

	void update(float t, float dt) override {
		tt = t;
		++fr;
		rainbow_on = t > rainbow_t ? 1 : 0;
		if (rainbow_on == 1) { hue_t = math::floor(t / beat) * 0.25; }
		int amp = 0;
		if (t > slam_t) {
			float erupt = (t - slam_t) * 3.0;
			erupt = erupt > 1.0 ? 1.0 : erupt;
			float pump = t < wall_out_t ? pulse(t) * 16.0 : 0.0;
			amp = itrunc((40.0 + pump) * erupt);
			if (t > wall_out_t) { amp = itrunc(42.0 - (t - wall_out_t) * 19.0); }
			amp = amp < 0 ? 0 : amp;
		}
		seed_rows(amp, amp / 2 + 2);
		convect();
		if (t > slam_t) { burn_letters(t); }
		move_sparks(dt, amp > 12);
		turn_cube(t);
		if (t > shatter_t && t < slam_t) { fly_sparks(t); }
	}

	// the 8-cube: each axis opens in turn (a line, a square, a cube, ...), then
	// the whole turns through eight rotation planes, faster as the cube completes
	void turn_cube(float t) {
		var& c = cube;
		float spin = t < shatter_t ? t * (0.35 + t * 0.06) : shatter_t * (0.35 + shatter_t * 0.06) + (t - shatter_t) * 0.4;
		array<float> ca = [];
		array<float> sa = [];
		for (int p = 0; p < 8; ++p) {
			float a = spin * (0.5 + 0.13 * p) + p * 0.7;
			ca.push(math::cos(a));
			sa.push(math::sin(a));
		}
		array<float> open = [];
		for (int d = 0; d < 8; ++d) {
			float f = (t - unfold_t - d * 0.55) / 0.45;
			open.push(f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f * f * (3.0 - 2.0 * f)));
		}
		// planes (0,1) (2,3) (4,5) (6,7) then (0,4) (1,5) (2,6) (3,7)
		array<int> pa = [0, 2, 4, 6, 0, 1, 2, 3];
		array<int> pb = [1, 3, 5, 7, 4, 5, 6, 7];
		for (int v = 0; v < 256; ++v) {
			int base = v * 8;
			for (int d2 = 0; d2 < 8; ++d2) { c[base + d2] = ((v >> d2) & 1) == 1 ? open[d2] : -open[d2]; }
			for (int p2 = 0; p2 < 8; ++p2) {
				int ia = base + pa[p2];
				int ib = base + pb[p2];
				float xa = c[ia];
				float xb = c[ib];
				c[ia] = xa * ca[p2] - xb * sa[p2];
				c[ib] = xa * sa[p2] + xb * ca[p2];
			}
		}
		// down to the screen: the 16-gon Petrie projection for x and y, another
		// mix for depth, then perspective; the cube breathes on the beat
		float scale = h * 0.13 * (1.0 + (t > 3.9 && t < shatter_t ? pulse(t) * 0.12 : 0.0));
		float cx = w * 0.5;
		float cy = h * 0.5;
		for (int v2 = 0; v2 < 256; ++v2) {
			int b2 = v2 * 8;
			float x = 0.0;
			float y = 0.0;
			float z = 0.0;
			for (int d3 = 0; d3 < 8; ++d3) {
				float q = c[b2 + d3];
				float ang = d3 * 0.3926991;
				x += q * math::cos(ang);
				y += q * math::sin(ang);
				z += q * ((d3 & 1) == 0 ? 0.35 : -0.35);
			}
			float s = scale * 4.0 / (6.0 - z);
			sx[v2] = cx + x * s;
			sy[v2] = cy + y * s;
			sz[v2] = z;
		}
	}

	// the shatter: each letter pixel's spark leaves a vertex of the cube, curls
	// around the centre and lands on its pixel on the slam
	void fly_sparks(float t) {
		float u = (t - shatter_t) / (slam_t - shatter_t);
		u = u > 1.0 ? 1.0 : u;
		float e = u * u * (3.0 - 2.0 * u);
		float curl = math::sin(u * 3.1415927) * 1.6;
		float cc = math::cos(curl);
		float ss = math::sin(curl);
		float cx = w * 0.5;
		float cy = h * 0.5;
		for (int k = 0; k < letter_at.size(); ++k) {
			int v = (k * 37) & 255;
			int i = letter_at[k];
			float tx = i % w;
			float ty = i / w;
			float px = sx[v] + (tx - sx[v]) * e;
			float py = sy[v] + (ty - sy[v]) * e;
			float ox = px - cx;
			float oy = py - cy;
			pfx[k] = cx + ox * cc - oy * ss;
			pfy[k] = cy + ox * ss + oy * cc;
		}
	}

	// each column's fuel swells and ebbs where two waves cross and pumps on the
	// beat, and the air above the word cools fast, so the flames rise as tongues;
	// once the letters have cooled, the last embers anywhere go out the same way
	void burn_letters(float t) {
		float strength = t < embers_t ? 1.0 : (t > neon_t ? 0.0 : (neon_t - t) / (neon_t - embers_t));
		float pump = t < wall_out_t ? 1.0 + pulse(t) * 0.35 : 1.0;
		for (int x = 0; x < w; ++x) {
			float wave = math::sin(x * 0.61 + t * 6.1) + math::sin(x * 0.23 - t * 3.7 + 1.7);
			fuel[x] = itrunc((46.0 + wave * 15.0) * strength * pump);
		}
		var& ht = heat;
		var& rn = cool;
		var& at = letter_at;
		int n = at.size();
		for (int k = 0; k < n; ++k) {
			int i = at[k];
			int v = fuel[i % w] + (rn[(i * 7 + fr) & 1023] & 1) * 8;
			if (ht[i] < v) { ht[i] = v; }
		}
		int quenched = t > neon_t ? w * h : word_top * w;
		for (int i2 = 0; i2 < quenched; ++i2) {
			int v2 = ht[i2] - 2;
			ht[i2] = v2 > 0 ? v2 : 0;
		}
	}

	// the cube's edges into the glow buffer: each edge joins two vertices one
	// axis apart, its hue that axis', nearer edges brighter; crossings add a
	// little, so the dense middle keeps its lattice instead of burning white
	void draw_cube(float gain) {
		var& g = glow;
		var& gh = glow_hue;
		int n = w * h;
		for (int i = 0; i < n; ++i) { g[i] = 0.0; }
		for (int v = 0; v < 256; ++v) {
			for (int d = 0; d < 8; ++d) {
				if (((v >> d) & 1) == 1) { continue; }
				int u = v | (1 << d);
				float x0 = sx[v];
				float y0 = sy[v];
				float dx = sx[u] - x0;
				float dy = sy[u] - y0;
				float adx = dx < 0.0 ? -dx : dx;
				float ady = dy < 0.0 ? -dy : dy;
				int steps = itrunc((adx > ady ? adx : ady) + 1.0);
				float lum = gain * (0.55 + (sz[v] + sz[u]) * 0.18);
				float ix = dx / steps;
				float iy = dy / steps;
				for (int s = 0; s <= steps; ++s) {
					int px = itrunc(x0 + ix * s);
					int py = itrunc(y0 + iy * s);
					if (px < 0 || px >= w || py < 0 || py >= h) { continue; }
					int i2 = py * w + px;
					float was = g[i2];
					if (lum > was) {
						gh[i2] = d;
						g[i2] = lum + was * 0.25;
					} else {
						g[i2] = was + lum * 0.25;
					}
				}
			}
		}
	}

	void render(var& grid) override {
		int n = w * h;
		int b = bank();
		var& ht = heat;
		var& g = glow;
		var& gh = glow_hue;
		float gain = tt < shatter_t ? 0.8 : (tt < slam_t ? 0.8 * (1.0 - (tt - shatter_t) / (slam_t - shatter_t)) : 0.3);
		if (tt > wall_out_t) { gain = 0.3 * (1.0 - (tt - wall_out_t) * 0.5); }
		bool cube_on = gain > 0.01;
		if (cube_on) { draw_cube(gain); }
		for (int i = 0; i < n; ++i) {
			int v = ht[i];
			if (v > 5 || !cube_on || g[i] <= 0.04) {
				grid[i] = (v > 72 ? 72 : v) + b;
				continue;
			}
			int lv = g[i] * 9.0;
			grid[i] = hyper + gh[i] * 12 + (lv > 11 ? 11 : lv);
		}
		if (tt > slam_t) {
			var& rim = rim_at;
			for (int k = 0; k < rim.size(); ++k) {
				int i2 = rim[k];
				grid[i2] = b + ht[i2] / 3;
			}
			draw_letters(grid, b);
		}
		if (tt > shatter_t && tt < slam_t) { draw_flight(grid); }
		draw_sparks(grid, b);
		if (tt < shatter_t) { draw_dimension(grid); }
		slam(grid);
	}

	// the sparks in flight, each in the hue of an axis
	void draw_flight(var& grid) {
		for (int k = 0; k < letter_at.size(); ++k) {
			int x = itrunc(pfx[k]);
			int y = itrunc(pfy[k]);
			if (x < 0 || x >= w || y < 0 || y >= h) { continue; }
			grid[y * w + x] = hyper + pfaxis[k] * 12 + 9 + (k & 1) * 2;
		}
	}

	// the dimension counter in the corner: 1D, 2D, ... 8D as each axis opens
	void draw_dimension(var& grid) {
		int dims = itrunc((tt - unfold_t) / 0.55) + 1;
		if (tt < unfold_t) { return; }
		dims = dims > 8 ? 8 : dims;
		string label = `${dims}D`;
		gfx.draw_text(grid, w, h, label, 3, h - 18, 2, 2, hyper + ((dims - 1) % 8) * 12 + 4, 8, -1, 2);
	}

	// the slam: a white flash, two shockwave rings racing out from the word
	// (white leading edges, a rainbow wake) and a shake that settles over half a second
	void slam(var& grid) {
		float s = tt - slam_t;
		if (s < 0.0 || s > 0.9) { return; }
		int n = w * h;
		if (s < 0.1) {
			int lv = 7 - itrunc(s * 40.0);
			for (int i = 0; i < n; ++i) { grid[i] = white + (lv < 5 ? 5 : lv); }
			return;
		}
		float cx = w * 0.5;
		float cy = word_top + word_rows * 0.5;
		for (int ring = 0; ring < 2; ++ring) {
			float r = (s - 0.1 - ring * 0.12) * 150.0;
			if (r < 1.0) { continue; }
			float width = 1.2 + r * 0.03;
			int ink = hyper + ((ring * 3 + fr) % 8) * 12;
			int y0 = itrunc(cy - r - width) - 1;
			int y1 = itrunc(cy + r + width) + 1;
			y0 = y0 < 0 ? 0 : y0;
			y1 = y1 > h - 1 ? h - 1 : y1;
			for (int y = y0; y <= y1; ++y) {
				float ey = y - cy;
				for (int x = 0; x < w; ++x) {
					float ex = x - cx;
					float d = math::sqrt(ex * ex + ey * ey) - r;
					if (d < width && d > -width) { grid[y * w + x] = d > -width * 0.35 ? white + 7 : ink + 10; }
				}
			}
		}
		if (s < 0.6) {
			float amp = (0.6 - s) * 6.0;
			var& row = shake_row;
			for (int y2 = 0; y2 < h; ++y2) {
				int off = itrunc(math::sin(y2 * 0.9 + fr * 2.3) * amp);
				if (off == 0) { continue; }
				int base = y2 * w;
				for (int x2 = 0; x2 < w; ++x2) { row[x2] = grid[base + x2]; }
				for (int x3 = 0; x3 < w; ++x3) {
					int from = x3 - off;
					from = from < 0 ? 0 : (from >= w ? w - 1 : from);
					grid[base + x3] = row[from];
				}
			}
		}
	}

	// the letters burn white-hot over their own flames, ringed by a dark rim;
	// once the fire is out each cools to its own hue of the rainbow, and a
	// glint sweeps across the word every few seconds
	void draw_letters(var& grid, int b) {
		var& at = letter_at;
		int n = at.size();
		if (tt < embers_t) {
			for (int k = 0; k < n; ++k) {
				int i = at[k];
				grid[i] = b + 64 + ((i * 7 + fr) & 7);
			}
			return;
		}
		float cooled = tt > neon_t ? 1.0 : (tt - embers_t) / (neon_t - embers_t);
		int level = 64 - itrunc(cooled * 12.0);
		int roll = itrunc(tt * 2.0);
		int glint = tt > neon_t ? itrunc((tt - neon_t) * 60.0) % (word_w + 160) - 20 : -20;
		for (int k = 0; k < n; ++k) {
			int i = at[k];
			int d = i % w - word_x + i / w - word_top - glint;
			int lv = d >= 0 && d < 6 ? 72 - d : level + ((i * 7 + fr) & 3);
			grid[i] = levels + ((letter_of[k] + roll) % phases) * levels + lv;
		}
	}

	// the credits roll up from the bottom row, under the word, and stop with the
	// last lines resting midway; rows near the edges of the band dim
	void text_rows(var& rows) override {
		if (tt < credits_t) { return; }
		int top = (word_top + word_rows) / 2 + 2;
		int bottom = h / 2 - 2;
		if (bottom - top < 2) { return; }
		int last = credits.size() - 1;
		int rest = bottom + last - (top + bottom) / 2;
		int scroll = itrunc((tt - credits_t) * credit_rows_per_s);
		scroll = scroll > rest ? rest : scroll;
		for (int k = 0; k <= last; ++k) {
			int row = bottom + 1 + k - scroll;
			if (row < top || row > bottom) { continue; }
			var& line = credits[k];
			if (line[0] == "") { continue; }
			int edge = row - top < bottom - row ? row - top : bottom - row;
			rows[row] = credit_row(line, edge >= 2 ? 0 : 2 - edge);
		}
	}

	string credit_row(var& line, int dim) {
		string kind = line[0];
		string left = line[1];
		string right = line[2];
		string result = sgr_bg(0, 0, 0);
		if (kind == "role") {
			int gutter = w / 2;
			left = left.size() > gutter - 2 ? left.substr(0, gutter - 2) : left;
			right = right.size() > w - gutter - 1 ? right.substr(0, w - gutter - 1) : right;
			result += string::repeat(" ", gutter - 1 - left.size()) + credit_ink(255, 206, 110, dim) + left
				+ "  " + credit_ink(186, 180, 236, dim) + right + string::repeat(" ", w - gutter - 1 - right.size());
		} else {
			left = left.size() > w ? left.substr(0, w) : left;
			int pad = (w - left.size()) / 2;
			string ink = credit_ink(214, 208, 255, dim);
			if (kind == "title") { ink = credit_ink(255, 236, 190, dim); }
			if (kind == "link") { ink = credit_ink(120, 200, 255, dim); }
			result += string::repeat(" ", pad) + ink + left + string::repeat(" ", w - pad - left.size());
		}
		return result + RESET;
	}

	string credit_ink(int r, int g, int b, int dim) {
		return sgr_fg(r * (3 - dim) / 3, g * (3 - dim) / 3, b * (3 - dim) / 3);
	}
}
