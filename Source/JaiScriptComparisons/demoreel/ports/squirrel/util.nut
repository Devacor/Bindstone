// demoreel util (Squirrel port of scenes/util.jai): palettes, the half-block
// presenter, transitions, the pixel font. Squirrel containers are references:
// the reference's var& parameters are plain parameters here; JaiScript's
// value copies become explicit `clone` where the copy is mutated.

// s repeated n times (Squirrel 3.2 has no string repeat)
function rep(s, n) {
	local r = "";
	for (local i = 0; i < n; ++i) { r += s; }
	return r;
}

// join with pairwise merging: O(bytes * log n) copying (no array join in 3.2,
// concat copies both sides); the GLOOM port's helper
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
::HB_W <- 0;
::HB_ROWS <- 0;
::HB_HALF <- "▀";
::HB_FULL <- "█";
::HB_RESET <- ::ESC + "[0m";

function hb_ensure(w, rows) {
	if (::HB_W == w && ::HB_ROWS == rows && ::HB_PIX.len() == w * rows * 2) { return; }
	::HB_W = w;
	::HB_ROWS = rows;
	::HB_PIX = ::array(w * rows * 2, 0);
}

function hb_row(cy) {
	local parts = [];
	local pix = ::HB_PIX;
	local tfg = ::HB_FG;
	local tbg = ::HB_BG;
	local half = ::HB_HALF;
	local full = ::HB_FULL;
	local vw = ::HB_W;
	local fg = -1;
	local bg = -1;
	local ti = cy * 2 * vw;
	local bi = ti + vw;
	for (local x = 0; x < vw; ++x) {
		local top = pix[ti + x];
		local bot = pix[bi + x];
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

// the reference's parallel_transform path is off (HB_WORKERS = 0): serial there too
function hb_present() {
	local rows = [];
	for (local cy = 0; cy < ::HB_ROWS; ++cy) { rows.push(::hb_row(cy)); }
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
		for (local pass = shadow < 0 ? 1 : 0; pass < 2; ++pass) {
			local ox = pass == 0 ? 1 : 0;
			for (local li = 0; li < n; ++li) {
				local ch = text.slice(li, li + 1);
				if (!(ch in ::FONT5)) { continue; }
				local bits = ::FONT5[ch];
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

	// kind: 0 dissolve, 1 slant wipe, 2 blinds, 3 iris, 4 melt
	function mix_grids(ga, gb, gm, w, h, kind, prog, off, edge, melt) {
		local kilo = (prog * 1000.0).tointeger();
		local idx = 0;
		if (kind == 0) {
			for (local y = 0; y < h; ++y) {
				for (local x = 0; x < w; ++x) {
					local k = (((x * 73856093 + y * 19349663) & 1048575) * 1274126177 >> 12) % 1000;
					gm[idx] = k < kilo ? gb[idx] + off : ga[idx];
					idx = idx + 1;
				}
			}
			return;
		}
		if (kind == 1) {
			local e = (prog * (w + h / 2 + 8.0)).tointeger() - 4;
			for (local y = 0; y < h; ++y) {
				local sh = e - (h - y) / 2;
				for (local x = 0; x < w; ++x) {
					local d = x - sh;
					gm[idx] = d < 0 ? gb[idx] + off : (d < 2 ? edge : ga[idx]);
					idx = idx + 1;
				}
			}
			return;
		}
		if (kind == 2) {
			local band = 8;
			local e2 = (prog * (band + 1.0)).tointeger();
			for (local y = 0; y < h; ++y) {
				local yb = (y + (y / band) % 2 * 3) % band;
				for (local x = 0; x < w; ++x) {
					gm[idx] = yb < e2 ? gb[idx] + off : (yb == e2 ? edge : ga[idx]);
					idx = idx + 1;
				}
			}
			return;
		}
		if (kind == 3) {
			local cx = w / 2;
			local cy = h / 2;
			local rmax = cx * cx + cy * cy;
			local r = (prog * prog * (rmax + 400.0)).tointeger();
			local r2 = (::sqrt(r * 1.0) + 2.0).tointeger();
			r2 = r2 * r2;
			for (local y = 0; y < h; ++y) {
				local dy = y - cy;
				for (local x = 0; x < w; ++x) {
					local dx = x - cx;
					local d2 = dx * dx + dy * dy;
					gm[idx] = d2 < r ? gb[idx] + off : (d2 < r2 ? edge : ga[idx]);
					idx = idx + 1;
				}
			}
			return;
		}
		// melt: the departing frame slides down in ragged columns
		local fall = prog * prog * (h + 24.0);
		for (local x = 0; x < w; ++x) {
			local o = (fall - melt[x]).tointeger();
			o = o < 0 ? 0 : o;
			local gi = x;
			for (local y = 0; y < h; ++y) {
				gm[gi] = y < o ? gb[gi] + off : ga[gi - o * w];
				gi += w;
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

	function overlay_left(rows, w, y, text, style) {
		if (y < 0 || y >= rows.len()) { return; }
		local n = text.len();
		if (n > w) {
			text = text.slice(0, w);
			n = w;
		}
		rows[y] = style + text + ::rep(" ", w - n) + ::RESET;
	}
}
