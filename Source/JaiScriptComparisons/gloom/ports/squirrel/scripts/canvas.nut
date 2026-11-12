// A cell canvas for the showcase HUD and pages. Each cell is two stacked
// pixels (drawn as an upper half block, square on a 1:2 cell) or one character
// over the cell's upper pixel. Colors are palette indices.

// ------------------------------------------------------------ pixel font ----
// 3x5 capitals, digits and a little punctuation; '#' is ink. Keyed by character
// code, as the port's art parser keys its palettes.
::FONT5 <- {};
foreach (ch, glyph_rows in {
	A = ["###", "#.#", "###", "#.#", "#.#"], B = ["##.", "#.#", "##.", "#.#", "##."],
	C = ["###", "#..", "#..", "#..", "###"], D = ["##.", "#.#", "#.#", "#.#", "##."],
	E = ["###", "#..", "##.", "#..", "###"], F = ["###", "#..", "##.", "#..", "#.."],
	G = ["###", "#..", "#.#", "#.#", "###"], H = ["#.#", "#.#", "###", "#.#", "#.#"],
	I = ["###", ".#.", ".#.", ".#.", "###"], J = ["..#", "..#", "..#", "#.#", "###"],
	K = ["#.#", "#.#", "##.", "#.#", "#.#"], L = ["#..", "#..", "#..", "#..", "###"],
	M = ["#.#", "###", "###", "#.#", "#.#"], N = ["##.", "#.#", "#.#", "#.#", "#.#"],
	O = ["###", "#.#", "#.#", "#.#", "###"], P = ["###", "#.#", "###", "#..", "#.."],
	Q = ["###", "#.#", "#.#", "###", "..#"], R = ["##.", "#.#", "##.", "#.#", "#.#"],
	S = ["###", "#..", "###", "..#", "###"], T = ["###", ".#.", ".#.", ".#.", ".#."],
	U = ["#.#", "#.#", "#.#", "#.#", "###"], V = ["#.#", "#.#", "#.#", "#.#", ".#."],
	W = ["#.#", "#.#", "###", "###", "#.#"], X = ["#.#", "#.#", ".#.", "#.#", "#.#"],
	Y = ["#.#", "#.#", ".#.", ".#.", ".#."], Z = ["###", "..#", ".#.", "#..", "###"],
	["0"] = ["###", "#.#", "#.#", "#.#", "###"], ["1"] = [".#.", "##.", ".#.", ".#.", "###"],
	["2"] = ["###", "..#", "###", "#..", "###"], ["3"] = ["###", "..#", ".##", "..#", "###"],
	["4"] = ["#.#", "#.#", "###", "..#", "..#"], ["5"] = ["###", "#..", "###", "..#", "###"],
	["6"] = ["###", "#..", "###", "#.#", "###"], ["7"] = ["###", "..#", ".#.", ".#.", ".#."],
	["8"] = ["###", "#.#", "###", "#.#", "###"], ["9"] = ["###", "#.#", "###", "..#", "###"],
	["%"] = ["#.#", "..#", ".#.", "#..", "#.#"], [":"] = ["...", ".#.", "...", ".#.", "..."],
	["/"] = ["..#", "..#", ".#.", "#..", "#.."], ["-"] = ["...", "...", "###", "...", "..."],
	["!"] = [".#.", ".#.", ".#.", "...", ".#."], ["'"] = [".#.", ".#.", "...", "...", "..."],
	["."] = ["...", "...", "...", "...", ".#."], [" "] = ["...", "...", "...", "...", "..."]
}) {
	::FONT5[ch[0]] <- glyph_rows;
}

class Canvas {
	w = 0;
	h = 0;            // cells; pixel rows are 2 * h
	px = null;        // palette index per pixel, row-major, w x 2h
	glyph = null;     // "" or the character drawn in the cell
	ink = null;       // its color

	constructor(w_, h_) {
		w = w_;
		h = h_;
		px = ::array(w_ * h_ * 2, 0);
		glyph = ::array(w_ * h_, "");
		ink = ::array(w_ * h_, 0);
	}

	function clear(col) {
		local p = px;
		for (local i = 0; i < p.len(); ++i) { p[i] = col; }
		local gl = glyph;
		for (local i = 0; i < gl.len(); ++i) { gl[i] = ""; }
	}

	function dot(x, y, col) {
		if (x >= 0 && x < w && y >= 0 && y < h * 2) { px[y * w + x] = col; }
	}

	function rect(x, y, rw, rh, col) {
		local p = px;
		local cw = w;
		for (local yy = ::imax(0, y); yy < ::imin(h * 2, y + rh); ++yy) {
			for (local xx = ::imax(0, x); xx < ::imin(cw, x + rw); ++xx) { p[yy * cw + xx] = col; }
		}
	}

	// one color per pixel row from top to bottom, from rows y0 to y1 (exclusive)
	function gradient(y0, y1, top, bottom) {
		local p = px;
		local cw = w;
		for (local y = ::imax(0, y0); y < ::imin(h * 2, y1); ++y) {
			local c = ::color_idx(::mix_rgb(top, bottom, 1.0 * (y - y0) / ::imax(1, y1 - y0 - 1)));
			for (local x = 0; x < cw; ++x) { p[y * cw + x] = c; }
		}
	}

	// text at cell (x, row); characters past the edge are dropped
	function text(x, row, s, col) {
		if (row < 0 || row >= h) { return; }
		for (local i = 0; i < s.len(); ++i) {
			local cx = x + i;
			if (cx < 0 || cx >= w) { continue; }
			glyph[row * w + cx] = s.slice(i, i + 1);
			ink[row * w + cx] = col;
		}
	}

	function text_centered(row, s, col) { text((w - s.len()) / 2, row, s, col); }

	function text_right(x_end, row, s, col) { text(x_end - s.len(), row, s, col); }

	// an art (defs.nut) at pixel (x, y), through a code -> color table
	function art(a, x, y, lut) {
		local aw = a.w;
		local apix = a.pix;
		for (local ay = 0; ay < a.h; ++ay) {
			for (local ax = 0; ax < aw; ++ax) {
				local code = apix[ay * aw + ax];
				if (code > 0) { dot(x + ax, y + ay, lut[code]); }
			}
		}
	}

	// s in the 3x5 font with its top-left at pixel (x, y), a color per font row, a shadow below right
	function lettering(s, x, y, scale, shades, shadow) {
		local font = ::FONT5;
		for (local pass = 0; pass < 2; ++pass) {
			for (local i = 0; i < s.len(); ++i) {
				local ch = s[i];
				if (!(ch in font)) { continue; }
				local glyph_rows = font[ch];
				local gx = x + i * 4 * scale;
				for (local ry = 0; ry < 5; ++ry) {
					for (local rx = 0; rx < 3; ++rx) {
						if (glyph_rows[ry][rx] != '#') { continue; }
						if (pass == 0 && shadow >= 0) { rect(gx + (rx + 1) * scale, y + (ry + 1) * scale, scale, scale, shadow); }
						if (pass == 1) { rect(gx + rx * scale, y + ry * scale, scale, scale, shades[ry]); }
					}
				}
			}
		}
	}

	// rows first .. last (cells) as terminal lines
	function rows(first, last) {
		local out = [];
		local fgs = ::PAL_FG;
		local bgs = ::PAL_BG;
		local hb = ::HALFG;
		local cw = w;
		local p = px;
		local gl = glyph;
		local ik = ink;
		for (local row = first; row < last; ++row) {
			local parts = [];
			local fg = -1;
			local bg = -1;
			for (local x = 0; x < cw; ++x) {
				local cell = row * cw + x;
				local upper = p[row * 2 * cw + x];
				local lower = p[(row * 2 + 1) * cw + x];
				local ch = gl[cell];
				if (ch != "") {
					if (ik[cell] != fg) { fg = ik[cell]; parts.push(fgs[fg]); }
					if (upper != bg) { bg = upper; parts.push(bgs[bg]); }
					parts.push(ch);
				} else if (upper == lower) {
					if (upper != bg) { bg = upper; parts.push(bgs[bg]); }
					parts.push(" ");
				} else {
					if (upper != fg) { fg = upper; parts.push(fgs[fg]); }
					if (lower != bg) { bg = lower; parts.push(bgs[bg]); }
					parts.push(hb);
				}
			}
			parts.push(::RESET);
			out.push(::join_arr(parts, ""));
		}
		return out;
	}
}

// width in pixels of s in the 3x5 font at a scale, one pixel of spacing per letter
function lettering_width(s, scale) { return s.len() * 4 * scale - scale; }
