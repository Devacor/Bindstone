// demoreel util: palettes, grids, row rendering, transitions, overlays.
// Frames are built entirely in script: each scene fills a flat grid of palette
// indices, and render_rows turns it into escape-coded text. Run-length trick:
// a cell that repeats the previous palette entry emits only its glyph, so smooth
// fields (plasma, fire) cost far fewer escape bytes than cells.
//
// EVERYTHING IS VALUE-SEMANTIC in JaiScript: assignment, params, returns, and
// element reads all deep-copy. Mutation goes through var& reference params /
// ref declarations, or through the owning object's own fields. Every helper
// below that mutates (or reads a 4000-cell grid) takes var&.
//
// NOTE: the free-call/self-scope engine bug this file originally worked around
// (free function or lambda calls poisoning a method's implicit-self) is FIXED;
// the Gfx-class structure is kept because it reads well, not out of necessity.

class Palette {
	var full = [];   // escape prefix + glyph
	var bare = [];   // glyph only, colors inherited from the previous cell

	void wipe() {
		full.clear();
		bare.clear();
	}

	// solid block: colored background + space
	int add_bg(int r, int g, int b) {
		full.push(ESC + "[48;2;" + r + ";" + g + ";" + b + "m ");
		bare.push(" ");
		return full.size() - 1;
	}

	// glyph on default background
	int add_fg(int r, int g, int b, string glyph) {
		full.push(ESC + "[0m" + ESC + "[38;2;" + r + ";" + g + ";" + b + "m" + glyph);
		bare.push(glyph);
		return full.size() - 1;
	}

	// glyph on an explicit background
	int add_fg_bg(int fr, int fg, int fb, int br, int bg, int bb, string glyph) {
		full.push(ESC + "[38;2;" + fr + ";" + fg + ";" + fb + "m" + ESC + "[48;2;" + br + ";" + bg + ";" + bb + "m" + glyph);
		bare.push(glyph);
		return full.size() - 1;
	}

	int add_blank() {
		full.push(ESC + "[0m ");
		bare.push(" ");
		return full.size() - 1;
	}

	// append src's entries to self; returns new size. Lives ON Palette because a
	// script object passed as a var PARAM arrives as a copy — mutations are lost
	// (arrays share storage, objects do not; see README bug list).
	int absorb(var& src) {
		for (auto s : src.full) { full.push(s); }
		for (auto s2 : src.bare) { bare.push(s2); }
		return full.size();
	}
}

class Gfx {
	var make_grid(int n) {
		var g = [];
		for (int i = 0; i < n; ++i) { g.push(0); }
		return g;
	}

	void clear_grid(var& g) {
		for (auto& c : g) { c = 0; }
	}

	// returns floats on purpose: Palette's typed int params convert on the way in
	var rainbow_rgb(float phase) {
		return [128.0 + 127.0 * sin(phase),
			128.0 + 127.0 * sin(phase + 2.094),
			128.0 + 127.0 * sin(phase + 4.188)];
	}

	string fmt1(float v) {
		int t = itrunc(v * 10.0 + 0.5);
		return "" + (t / 10) + "." + (t % 10);
	}

	// grid of palette indices -> array of row strings (parts array reused across rows)
	var render_rows(var& grid, var& pal, int w, int h, var& parts) {
		var full = pal.full;
		var bare = pal.bare;
		var rows = [];
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			parts.clear();
			int prev = -1;   // loop-body local assigned from the nested x-loop (finding 2 FIXED: exercises the slot-reserve fix every frame)
			for (int x = 0; x < w; ++x) {
				int c = grid[idx];
				idx = idx + 1;
				if (c == prev) { parts.push(bare[c]); }
				else { parts.push(full[c]); prev = c; }
			}
			parts.push(RESET);
			rows.push(parts.join(""));
		}
		return rows;
	}

	// transition support: scenes' palettes are concatenated (Palette.absorb) and
	// the two grids merge into one indexed grid rendered by ordinary render_rows.
	// kind: 0 dissolve, 1 wipe, 2 shutter
	void mix_grids(var& ga, var& gb, var& gm, int w, int h, int kind, float prog, int off) {
		int kilo = itrunc(prog * 1000.0);
		int edge = itrunc(prog * (w + 6.0) - 3.0);
		int idx = 0;
		bool from_b = false;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				if (kind == 0) { from_b = ((x * 3083 + y * 7919) * 2971) % 1000 < kilo; }
				else if (kind == 1) { from_b = x < edge; }
				else { from_b = (y % 2 == 0) ? (x < edge) : (x >= w - edge); }
				gm[idx] = from_b ? gb[idx] + off : ga[idx];
				idx = idx + 1;
			}
		}
	}

	// replace a full row with centered styled text (style = escape prefix)
	void overlay_center(var& rows, int w, int y, string text, string style) {
		if (y < 0 || y >= rows.size()) { return; }
		int n = text.size();
		if (n > w) {
			text = text.substr(0, w);
			n = w;
		}
		int pad = (w - n) / 2;
		rows[y] = style + " ".repeat(pad) + text + " ".repeat(w - pad - n) + RESET;
	}

	void overlay_left(var& rows, int w, int y, string text, string style) {
		if (y < 0 || y >= rows.size()) { return; }
		int n = text.size();
		if (n > w) {
			text = text.substr(0, w);
			n = w;
		}
		rows[y] = style + text + " ".repeat(w - n) + RESET;
	}
}
