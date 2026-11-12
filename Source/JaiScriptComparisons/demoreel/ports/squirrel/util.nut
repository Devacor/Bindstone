// demoreel util (Squirrel port of scenes/util.jai): palettes, the sextant/half-block presenter, the text
// layer, the pixel font. Squirrel containers are references: a var& parameter is a plain parameter here,
// and a JaiScript value copy that is later mutated becomes an explicit copy.

// s repeated n times (Squirrel has no string repeat)
function rep(s, n) {
	local result = "";
	for (local i = 0; i < n; ++i) { result += s; }
	return result;
}

// join with pairwise merging: O(bytes * log n) copying, where a += loop would copy O(n^2) (GLOOM's helper)
function join_arr(parts, sep) {
	local n = parts.len();
	if (n == 0) { return ""; }
	local arr = parts;
	if (sep != "") {
		arr = ::array(n * 2 - 1, sep);
		for (local i = 0; i < n; ++i) { arr[i * 2] = parts[i]; }
	}
	while (arr.len() > 1) {
		local m = arr.len();
		local half = (m + 1) / 2;
		local nxt = ::array(half, "");
		for (local i = 0; i + 1 < m; i += 2) { nxt[i / 2] = arr[i] + arr[i + 1]; }
		if (m % 2 == 1) { nxt[half - 1] = arr[m - 1]; }
		arr = nxt;
	}
	return arr[0];
}

class Palette {
	fg = null;
	bg = null;
	rgb = null;

	constructor() {
		fg = [];
		bg = [];
		rgb = [];
	}

	function add(r, g, b) {
		local ir = (r < 0.0 ? 0.0 : (r > 255.0 ? 255.0 : r)).tointeger();
		local ig = (g < 0.0 ? 0.0 : (g > 255.0 ? 255.0 : g)).tointeger();
		local ib = (b < 0.0 ? 0.0 : (b > 255.0 ? 255.0 : b)).tointeger();
		local c = "" + ir + ";" + ig + ";" + ib + "m";
		fg.push(::ESC + "[38;2;" + c);
		bg.push(::ESC + "[48;2;" + c);
		rgb.push(ir * 65536 + ig * 256 + ib);
		return rgb.len() - 1;
	}

	// n entries lerped from (r0,g0,b0) to (r1,g1,b1); returns the first index
	function ramp(n, r0, g0, b0, r1, g1, b1) {
		r0 = r0.tofloat(); g0 = g0.tofloat(); b0 = b0.tofloat();
		r1 = r1.tofloat(); g1 = g1.tofloat(); b1 = b1.tofloat();
		local first = rgb.len();
		for (local i = 0; i < n; ++i) {
			local f = n > 1 ? i / (n - 1.0) : 0.0;
			add(r0 + (r1 - r0) * f, g0 + (g1 - g0) * f, b0 + (b1 - b0) * f);
		}
		return first;
	}

	// n entries through a list of [r,g,b] stops (piecewise linear); returns the first index
	function gradient(n, stops) {
		local first = rgb.len();
		local ns = stops.len() - 1;
		for (local i = 0; i < n; ++i) {
			local f = n > 1 ? i * ns / (n - 1.0) : 0.0;
			local k = f.tointeger();
			k = k >= ns ? ns - 1 : k;
			local u = f - k;
			local a = stops[k];
			local b = stops[k + 1];
			local ar = a[0].tofloat(), ag = a[1].tofloat(), ab = a[2].tofloat();
			local br = b[0].tofloat(), bg2 = b[1].tofloat(), bb = b[2].tofloat();
			add(ar + (br - ar) * u, ag + (bg2 - ag) * u, ab + (bb - ab) * u);
		}
		return first;
	}

	function size() { return rgb.len(); }
}

// ------------------------------------------------------------ presenter ---

::HB_PIX <- [];
::HB_FG <- [];
::HB_BG <- [];
::HB_RGB <- [];
::HB_LUM <- [];
::HB_ROWIDX <- [];
::HB_SRC <- [];       // sextant mode: per glyph pixel column, the grid column it shows
::HB_SEXTG <- [];     // sextant glyphs by TL | TR<<1 | ML<<2 | MR<<3 | BL<<4 | BR<<5
::TXT_CP <- [];
::TXT_RGB <- [];
::TXT_USED <- false;
::HB_SEXT <- false;
::HB_W <- 0;
::HB_PH <- 0;
::HB_COLS <- 0;
::HB_ROWS <- 0;
::HB_HALF <- "▀";
::HB_FULL <- "█";
::HB_RESET <- ::ESC + "[0m";

function utf8(cp) {
	if (cp < 128) { return cp.tochar(); }
	if (cp < 2048) { return (192 + (cp >> 6)).tochar() + (128 + (cp & 63)).tochar(); }
	if (cp < 65536) { return (224 + (cp >> 12)).tochar() + (128 + ((cp >> 6) & 63)).tochar() + (128 + (cp & 63)).tochar(); }
	return (240 + (cp >> 18)).tochar() + (128 + ((cp >> 12) & 63)).tochar() + (128 + ((cp >> 6) & 63)).tochar() + (128 + (cp & 63)).tochar();
}

// the grid's pixel size for cols x rows cells
function hb_pixel_w(cols, sext) { return sext ? cols * 3 / 2 : cols; }
function hb_pixel_h(rows, sext) { return sext ? rows * 3 : rows * 2; }

function hb_ensure(cols, rows, sext) {
	::HB_SEXT = sext;
	local pw = ::hb_pixel_w(cols, sext);
	local ph = ::hb_pixel_h(rows, sext);
	if (::HB_COLS == cols && ::HB_ROWS == rows && ::HB_W == pw && ::HB_PH == ph && ::HB_PIX.len() == pw * ph) { return; }
	::HB_COLS = cols;
	::HB_ROWS = rows;
	::HB_W = pw;
	::HB_PH = ph;
	::HB_PIX = ::array(pw * ph, 0);
	::HB_ROWIDX = [];
	for (local r = 0; r < rows; ++r) { ::HB_ROWIDX.push(r); }
	::HB_SRC = [];
	for (local g = 0; g < cols * 2; ++g) { ::HB_SRC.push(g * pw / (cols * 2)); }
	::TXT_CP = ::array(cols * rows, 0);
	::TXT_RGB = ::array(cols * rows, 0);
	::TXT_USED = false;
	::HB_SEXTG = [];
	for (local b = 0; b < 64; ++b) {
		if (b == 0) { ::HB_SEXTG.push(" "); }
		else if (b == 21) { ::HB_SEXTG.push(::utf8(0x258C)); }
		else if (b == 42) { ::HB_SEXTG.push(::utf8(0x2590)); }
		else if (b == 63) { ::HB_SEXTG.push(::HB_FULL); }
		else { ::HB_SEXTG.push(::utf8(0x1FB00 + b - 1 - (b > 21 ? 1 : 0) - (b > 42 ? 1 : 0))); }
	}
}

// the text layer: cells, and the pixels they cover
function txt_clear() {
	if (!::TXT_USED) { return; }
	local cps = ::TXT_CP;
	local n = cps.len();
	for (local i = 0; i < n; ++i) { cps[i] = 0; }
	::TXT_USED = false;
}

function txt_glyph(col, row, cp, rgb) {
	if (col < 0 || col >= ::HB_COLS || row < 0 || row >= ::HB_ROWS) { return; }
	::TXT_CP[row * ::HB_COLS + col] = cp;
	::TXT_RGB[row * ::HB_COLS + col] = rgb;
	::TXT_USED = true;
}

// ASCII text from (col, row); spaces are written too, so a line can clear what lies under it
function txt_put(col, row, s, rgb) {
	local n = s.len();
	for (local i = 0; i < n; ++i) { ::txt_glyph(col + i, row, s[i] & 255, rgb); }
}

function cell_col(px) { return px * ::HB_COLS / ::HB_W; }
function cell_row(py) { return py * ::HB_ROWS / ::HB_PH; }
function col_px(col) { return col * ::HB_W / ::HB_COLS; }
function row_px(row) { return row * ::HB_PH / ::HB_ROWS; }

// a text cell: its glyph over the darker of the pixels under it
function hb_text(cell, dark, state) {
	local result = "";
	if (dark != state[1]) {
		result += ::HB_BG[dark];
		state[1] = dark;
	}
	local c = ::TXT_RGB[cell];
	state[0] = -1;
	local cp = ::TXT_CP[cell];
	return result + ::ESC + "[38;2;" + (c >> 16) + ";" + ((c >> 8) & 255) + ";" + (c & 255) + "m" + (cp < 128 ? cp.tochar() : ::utf8(cp));
}

function hb_row(cy) { return ::HB_SEXT ? ::hb_row_sext(cy) : ::hb_row_half(cy); }

function hb_row_half(cy) {
	local parts = [];
	local pix = ::HB_PIX;
	local tfg = ::HB_FG;
	local tbg = ::HB_BG;
	local lum = ::HB_LUM;
	local tcp = ::TXT_CP;
	local half = ::HB_HALF;
	local full = ::HB_FULL;
	local vw = ::HB_W;
	local fg = -1;
	local bg = -1;
	local ti = cy * 2 * vw;
	local bi = ti + vw;
	local cell = cy * vw;
	local text = ::TXT_USED;
	for (local x = 0; x < vw; ++x) {
		local top = pix[ti + x];
		local bot = pix[bi + x];
		if (text && tcp[cell + x] != 0) {
			local state = [fg, bg];
			parts.push(::hb_text(cell + x, lum[top] < lum[bot] ? top : bot, state));
			fg = state[0];
			bg = state[1];
			continue;
		}
		if (top == bot) {
			if (bot == bg) { parts.push(" "); }
			else if (top == fg) { parts.push(full); }
			else { parts.push(tbg[bot]); bg = bot; parts.push(" "); }
		} else {
			if (top != fg) { parts.push(tfg[top]); fg = top; }
			if (bot != bg) { parts.push(tbg[bot]); bg = bot; }
			parts.push(half);
		}
	}
	parts.push(::HB_RESET);
	return ::join_arr(parts, "");
}

function hb_row_sext(cy) {
	local parts = [];
	local pix = ::HB_PIX;
	local tfg = ::HB_FG;
	local tbg = ::HB_BG;
	local lum = ::HB_LUM;
	local src = ::HB_SRC;
	local glyphs = ::HB_SEXTG;
	local tcp = ::TXT_CP;
	local vw = ::HB_W;
	local fg = -1;
	local bg = -1;
	local r0 = cy * 3 * vw;
	local r1 = r0 + vw;
	local r2 = r1 + vw;
	local cols = ::HB_COLS;
	local cell = cy * cols;
	local text = ::TXT_USED;
	for (local cx = 0; cx < cols; ++cx) {
		local xa = src[cx * 2];
		local xb = src[cx * 2 + 1];
		local p0 = pix[r0 + xa];
		local p1 = pix[r0 + xb];
		local p2 = pix[r1 + xa];
		local p3 = pix[r1 + xb];
		local p4 = pix[r2 + xa];
		local p5 = pix[r2 + xb];
		local flat = p0 == p1 && p0 == p2 && p0 == p3 && p0 == p4 && p0 == p5;
		if (flat && !(text && tcp[cell] != 0)) {
			if (p0 != bg) { parts.push(tbg[p0]); bg = p0; }
			parts.push(" ");
			++cell;
			continue;
		}
		local l0 = lum[p0];
		local l1 = lum[p1];
		local l2 = lum[p2];
		local l3 = lum[p3];
		local l4 = lum[p4];
		local l5 = lum[p5];
		local lmin = l0;
		local lmax = l0;
		local fgp = p0;
		local bgp = p0;
		if (l1 < lmin) { lmin = l1; bgp = p1; }
		if (l1 > lmax) { lmax = l1; fgp = p1; }
		if (l2 < lmin) { lmin = l2; bgp = p2; }
		if (l2 > lmax) { lmax = l2; fgp = p2; }
		if (l3 < lmin) { lmin = l3; bgp = p3; }
		if (l3 > lmax) { lmax = l3; fgp = p3; }
		if (l4 < lmin) { lmin = l4; bgp = p4; }
		if (l4 > lmax) { lmax = l4; fgp = p4; }
		if (l5 < lmin) { lmin = l5; bgp = p5; }
		if (l5 > lmax) { lmax = l5; fgp = p5; }
		if (text && tcp[cell] != 0) {
			local state = [fg, bg];
			parts.push(::hb_text(cell, bgp, state));
			fg = state[0];
			bg = state[1];
			++cell;
			continue;
		}
		local thr = (lmin + lmax + 1) / 2;
		local bits = 0;
		if (l0 >= thr) { bits = 1; }
		if (l1 >= thr) { bits += 2; }
		if (l2 >= thr) { bits += 4; }
		if (l3 >= thr) { bits += 8; }
		if (l4 >= thr) { bits += 16; }
		if (l5 >= thr) { bits += 32; }
		if (fgp != fg) { parts.push(tfg[fgp]); fg = fgp; }
		if (bits != 63 && bgp != bg) { parts.push(tbg[bgp]); bg = bgp; }
		parts.push(glyphs[bits]);
		++cell;
	}
	parts.push(::HB_RESET);
	return ::join_arr(parts, "");
}

// the reference's parallel_transform branch never runs (HB_WORKERS stays 0): rows are built serially there too
function hb_present() {
	local rows = [];
	local n = ::HB_ROWS;
	for (local cy = 0; cy < n; ++cy) { rows.push(::hb_row(cy)); }
	return rows;
}

// ------------------------------------------------------------ pixel font ---
// 5x7, one int per row, bit 4 = leftmost column

::FONT5 <- {
	["A"] = [14, 17, 17, 31, 17, 17, 17], ["B"] = [30, 17, 17, 30, 17, 17, 30], ["C"] = [15, 16, 16, 16, 16, 16, 15],
	["D"] = [30, 17, 17, 17, 17, 17, 30], ["E"] = [31, 16, 16, 30, 16, 16, 31], ["F"] = [31, 16, 16, 30, 16, 16, 16],
	["G"] = [15, 16, 16, 19, 17, 17, 15], ["H"] = [17, 17, 17, 31, 17, 17, 17], ["I"] = [31, 4, 4, 4, 4, 4, 31],
	["J"] = [31, 2, 2, 2, 18, 18, 12], ["K"] = [17, 18, 20, 24, 20, 18, 17], ["L"] = [16, 16, 16, 16, 16, 16, 31],
	["M"] = [17, 27, 21, 21, 17, 17, 17], ["N"] = [17, 25, 21, 19, 17, 17, 17], ["O"] = [14, 17, 17, 17, 17, 17, 14],
	["P"] = [30, 17, 17, 30, 16, 16, 16], ["Q"] = [14, 17, 17, 17, 21, 18, 13], ["R"] = [30, 17, 17, 30, 20, 18, 17],
	["S"] = [15, 16, 16, 14, 1, 1, 30], ["T"] = [31, 4, 4, 4, 4, 4, 4], ["U"] = [17, 17, 17, 17, 17, 17, 14],
	["V"] = [17, 17, 17, 17, 17, 10, 4], ["W"] = [17, 17, 17, 21, 21, 21, 10], ["X"] = [17, 17, 10, 4, 10, 17, 17],
	["Y"] = [17, 17, 10, 4, 4, 4, 4], ["Z"] = [31, 1, 2, 4, 8, 16, 31],
	["0"] = [14, 17, 19, 21, 25, 17, 14], ["1"] = [4, 12, 4, 4, 4, 4, 14], ["2"] = [14, 17, 1, 2, 4, 8, 31],
	["3"] = [30, 1, 1, 14, 1, 1, 30], ["4"] = [2, 6, 10, 18, 31, 2, 2], ["5"] = [31, 16, 30, 1, 1, 17, 14],
	["6"] = [14, 16, 16, 30, 17, 17, 14], ["7"] = [31, 1, 2, 4, 8, 8, 8], ["8"] = [14, 17, 17, 14, 17, 17, 14],
	["9"] = [14, 17, 17, 15, 1, 1, 14], ["."] = [0, 0, 0, 0, 0, 12, 12], ["!"] = [4, 4, 4, 4, 4, 0, 4],
	["-"] = [0, 0, 0, 14, 0, 0, 0], [":"] = [0, 12, 12, 0, 12, 12, 0], ["/"] = [1, 1, 2, 4, 8, 16, 16],
	["+"] = [0, 4, 4, 31, 4, 4, 0], ["?"] = [14, 17, 1, 2, 4, 0, 4], ["'"] = [4, 4, 8, 0, 0, 0, 0]
};

class Gfx {
	function make_grid(n) { return ::array(n, 0); }

	function make_fgrid(n) { return ::array(n, 0.0); }

	function rainbow_rgb(phase) {
		return [128.0 + 127.0 * ::sin(phase),
			128.0 + 127.0 * ::sin(phase + 2.094),
			128.0 + 127.0 * ::sin(phase + 4.188)];
	}

	function fmt1(v) {
		local t = (v * 10.0 + 0.5).tointeger();
		return "" + (t / 10) + "." + (t % 10);
	}

	function text_width(text, sx) {
		return text.len() * 6 * sx - sx;
	}

	// 5x7 text into a pixel grid; rows [row0, row0 + nramp) of the palette shade
	// the glyphs top to bottom, shadow < 0 draws none; only the first `limit` glyphs
	function draw_text(pix, w, h, text, x0, y0, sx, sy, row0, nramp, shadow, limit) {
		local n = text.len() < limit ? text.len() : limit;
		local th = 7 * sy;
		local font = ::FONT5;
		for (local pass = shadow < 0 ? 1 : 0; pass < 2; ++pass) {
			local ox = pass == 0 ? 1 : 0;
			for (local li = 0; li < n; ++li) {
				local ch = text.slice(li, li + 1);
				if (!(ch in font)) { continue; }
				local bits = font[ch];
				local lx = x0 + li * 6 * sx + ox;
				for (local ry = 0; ry < 7; ++ry) {
					local b = bits[ry];
					if (b == 0) { continue; }
					for (local dy = 0; dy < sy; ++dy) {
						local yy = y0 + ry * sy + dy + ox;
						if (yy < 0 || yy >= h) { continue; }
						local c = pass == 0 ? shadow : row0 + ((ry * sy + dy) * nramp) / th;
						for (local rx = 0; rx < 5; ++rx) {
							if (((b >> (4 - rx)) & 1) == 0) { continue; }
							for (local dx = 0; dx < sx; ++dx) {
								local xx = lx + rx * sx + dx;
								if (xx >= 0 && xx < w) { pix[yy * w + xx] = c; }
							}
						}
					}
				}
			}
		}
	}

	// replace a full row with centered styled text (style = escape prefix)
	function overlay_center(rows, w, y, text, style) {
		if (y < 0 || y >= rows.len()) { return; }
		local n = text.len();
		if (n > w) {
			text = text.slice(0, w);
			n = w;
		}
		local pad = (w - n) / 2;
		rows[y] = style + ::rep(" ", pad) + text + ::rep(" ", w - pad - n) + ::RESET;
	}
}

function sgr_fg(r, g, b) { return ::ESC + "[38;2;" + r + ";" + g + ";" + b + "m"; }
function sgr_bg(r, g, b) { return ::ESC + "[48;2;" + r + ";" + g + ";" + b + "m"; }
