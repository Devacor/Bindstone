-- demoreel util (Lua 5.4 port of scenes/util.jai): JaiScript's integer semantics, palettes,
-- the sextant/half-block presenter, the text layer, the pixel font. Arrays are 1-based
-- tables holding the original's 0-based values (palette indices, pixel offsets).

local floor, ceil, sin = math.floor, math.ceil, math.sin
local char, rep, concat = string.char, string.rep, table.concat

-- C truncation toward zero, to an integer (math.floor/ceil return integers when they fit)
function itrunc(v)
	if v >= 0 then return floor(v) end
	return ceil(v)
end

function ifloor(v) return floor(v) end

-- C's int / and %: Lua's // and % floor, so these where an operand can be negative
function idiv(a, b)
	local q = a // b
	if q < 0 and q * b ~= a then q = q + 1 end
	return q
end

function imod(a, b) return a - idiv(a, b) * b end

-- the ports have no parallel row: the same calls, serially, in order
function parallel_transform(items, fn)
	local result = {}
	for i = 1, #items do result[i] = fn(items[i]) end
	return result
end

Palette = {}
Palette.__index = Palette

function Palette.new()
	return setmetatable({fg = {}, bg = {}, rgb = {}}, Palette)
end

function Palette:add(r, g, b)
	local ir = itrunc(r < 0.0 and 0.0 or (r > 255.0 and 255.0 or r))
	local ig = itrunc(g < 0.0 and 0.0 or (g > 255.0 and 255.0 or g))
	local ib = itrunc(b < 0.0 and 0.0 or (b > 255.0 and 255.0 or b))
	local c = ir .. ";" .. ig .. ";" .. ib .. "m"
	local n = #self.rgb + 1
	self.fg[n] = ESC .. "[38;2;" .. c
	self.bg[n] = ESC .. "[48;2;" .. c
	self.rgb[n] = ir * 65536 + ig * 256 + ib
	return n - 1
end

-- n entries lerped from (r0,g0,b0) to (r1,g1,b1); returns the first index
function Palette:ramp(n, r0, g0, b0, r1, g1, b1)
	local first = #self.rgb
	for i = 0, n - 1 do
		local f = n > 1 and i / (n - 1.0) or 0.0
		self:add(r0 + (r1 - r0) * f, g0 + (g1 - g0) * f, b0 + (b1 - b0) * f)
	end
	return first
end

-- n entries through a list of {r,g,b} stops (piecewise linear); returns the first index
function Palette:gradient(n, stops)
	local first = #self.rgb
	local ns = #stops - 1
	for i = 0, n - 1 do
		local f = n > 1 and i * ns / (n - 1.0) or 0.0
		local k = itrunc(f)
		k = k >= ns and ns - 1 or k
		local u = f - k
		local a = stops[k + 1]
		local b = stops[k + 2]
		local ar, ag, ab = a[1] + 0.0, a[2] + 0.0, a[3] + 0.0
		local br, bg2, bb = b[1] + 0.0, b[2] + 0.0, b[3] + 0.0
		self:add(ar + (br - ar) * u, ag + (bg2 - ag) * u, ab + (bb - ab) * u)
	end
	return first
end

function Palette:size() return #self.rgb end

-- ------------------------------------------------------------ presenter ---
-- Globals: the reel rebinds the tables (bind_palettes), so readers fetch them per call.

HB_PIX = {}
HB_FG = {}
HB_BG = {}
HB_RGB = {}
HB_LUM = {}
HB_ROWIDX = {}
HB_SRC = {}       -- sextant mode: per glyph pixel column, the grid column it shows
HB_SEXTG = {}     -- sextant glyphs by TL | TR<<1 | ML<<2 | MR<<3 | BL<<4 | BR<<5, at [bits + 1]
TXT_CP = {}
TXT_RGB = {}
TXT_USED = false
HB_SEXT = false
HB_W = 0          -- grid pixels across
HB_PH = 0         -- and down
HB_COLS = 0       -- cells
HB_ROWS = 0
HB_WORKERS = 0
HB_HALF = "▀"
HB_FULL = "█"
HB_RESET = ESC .. "[0m"

function utf8(cp)
	if cp < 128 then return char(cp) end
	if cp < 2048 then return char(192 + (cp >> 6), 128 + (cp & 63)) end
	if cp < 65536 then return char(224 + (cp >> 12), 128 + ((cp >> 6) & 63), 128 + (cp & 63)) end
	return char(240 + (cp >> 18), 128 + ((cp >> 12) & 63), 128 + ((cp >> 6) & 63), 128 + (cp & 63))
end

-- the grid's pixel size for cols x rows cells
function hb_pixel_w(cols, sext) return sext and cols * 3 // 2 or cols end
function hb_pixel_h(rows, sext) return sext and rows * 3 or rows * 2 end

function hb_ensure(cols, rows, sext)
	HB_SEXT = sext
	local pw = hb_pixel_w(cols, sext)
	local ph = hb_pixel_h(rows, sext)
	if HB_COLS == cols and HB_ROWS == rows and HB_W == pw and HB_PH == ph and #HB_PIX == pw * ph then return end
	HB_COLS = cols
	HB_ROWS = rows
	HB_W = pw
	HB_PH = ph
	HB_PIX = {}
	for i = 1, pw * ph do HB_PIX[i] = 0 end
	HB_ROWIDX = {}
	for r = 0, rows - 1 do HB_ROWIDX[r + 1] = r end
	HB_SRC = {}
	for g = 0, cols * 2 - 1 do HB_SRC[g + 1] = g * pw // (cols * 2) end
	TXT_CP = {}
	TXT_RGB = {}
	for c = 1, cols * rows do
		TXT_CP[c] = 0
		TXT_RGB[c] = 0
	end
	TXT_USED = false
	HB_SEXTG = {}
	for b = 0, 63 do
		local glyph
		if b == 0 then glyph = " "
		elseif b == 21 then glyph = utf8(0x258C)
		elseif b == 42 then glyph = utf8(0x2590)
		elseif b == 63 then glyph = HB_FULL
		else glyph = utf8(0x1FB00 + b - 1 - (b > 21 and 1 or 0) - (b > 42 and 1 or 0)) end
		HB_SEXTG[b + 1] = glyph
	end
end

-- the text layer: cells, and the pixels they cover
function txt_clear()
	if not TXT_USED then return end
	local cps = TXT_CP
	for i = 1, #cps do cps[i] = 0 end
	TXT_USED = false
end

function txt_glyph(col, row, cp, rgb)
	if col < 0 or col >= HB_COLS or row < 0 or row >= HB_ROWS then return end
	TXT_CP[row * HB_COLS + col + 1] = cp
	TXT_RGB[row * HB_COLS + col + 1] = rgb
	TXT_USED = true
end

-- ASCII text from (col, row); spaces are written too, so a line can clear what lies under it
function txt_put(col, row, s, rgb)
	for i = 1, #s do txt_glyph(col + i - 1, row, s:byte(i), rgb) end
end

function cell_col(px) return px * HB_COLS // HB_W end
function cell_row(py) return py * HB_ROWS // HB_PH end
function col_px(col) return col * HB_W // HB_COLS end
function row_px(row) return row * HB_PH // HB_ROWS end

-- a text cell: its glyph over the darker of the pixels under it; returns it and the new fg, bg
function hb_text(cell, dark, fg, bg)
	local result = ""
	if dark ~= bg then
		result = HB_BG[dark + 1]
		bg = dark
	end
	local c = TXT_RGB[cell + 1]
	local cp = TXT_CP[cell + 1]
	return result .. ESC .. "[38;2;" .. (c >> 16) .. ";" .. ((c >> 8) & 255) .. ";" .. (c & 255) .. "m"
		.. (cp < 128 and char(cp) or utf8(cp)), -1, bg
end

function hb_row_half(cy)
	local pix, lum, fgs, bgs, tcp = HB_PIX, HB_LUM, HB_FG, HB_BG, TXT_CP
	local half, full = HB_HALF, HB_FULL
	local buf = {}
	local n = 0
	local vw = HB_W
	local fg = -1
	local bg = -1
	local ti = cy * 2 * vw + 1
	local bi = ti + vw
	local cell = cy * vw
	local text = TXT_USED
	for x = 0, vw - 1 do
		local top = pix[ti + x]
		local bot = pix[bi + x]
		if text and tcp[cell + x + 1] ~= 0 then
			local s
			s, fg, bg = hb_text(cell + x, lum[top + 1] < lum[bot + 1] and top or bot, fg, bg)
			n = n + 1; buf[n] = s
		elseif top == bot then
			if bot == bg then n = n + 1; buf[n] = " "
			elseif top == fg then n = n + 1; buf[n] = full
			else
				n = n + 1; buf[n] = bgs[bot + 1]
				bg = bot
				n = n + 1; buf[n] = " "
			end
		else
			if top ~= fg then n = n + 1; buf[n] = fgs[top + 1]; fg = top end
			if bot ~= bg then n = n + 1; buf[n] = bgs[bot + 1]; bg = bot end
			n = n + 1; buf[n] = half
		end
	end
	n = n + 1; buf[n] = HB_RESET
	return concat(buf, "", 1, n)
end

function hb_row_sext(cy)
	local pix, src, lum, fgs, bgs, glyphs, tcp = HB_PIX, HB_SRC, HB_LUM, HB_FG, HB_BG, HB_SEXTG, TXT_CP
	local buf = {}
	local n = 0
	local vw = HB_W
	local fg = -1
	local bg = -1
	local r0 = cy * 3 * vw + 1
	local r1 = r0 + vw
	local r2 = r1 + vw
	local cols = HB_COLS
	local cell = cy * cols
	local text = TXT_USED
	for cx = 0, cols - 1 do
		local xa = src[cx * 2 + 1]
		local xb = src[cx * 2 + 2]
		local p0 = pix[r0 + xa]
		local p1 = pix[r0 + xb]
		local p2 = pix[r1 + xa]
		local p3 = pix[r1 + xb]
		local p4 = pix[r2 + xa]
		local p5 = pix[r2 + xb]
		local flat = p0 == p1 and p0 == p2 and p0 == p3 and p0 == p4 and p0 == p5
		if flat and not (text and tcp[cell + 1] ~= 0) then
			if p0 ~= bg then n = n + 1; buf[n] = bgs[p0 + 1]; bg = p0 end
			n = n + 1; buf[n] = " "
		else
			local l0 = lum[p0 + 1]
			local l1 = lum[p1 + 1]
			local l2 = lum[p2 + 1]
			local l3 = lum[p3 + 1]
			local l4 = lum[p4 + 1]
			local l5 = lum[p5 + 1]
			local lmin = l0
			local lmax = l0
			local fgp = p0
			local bgp = p0
			if l1 < lmin then lmin = l1; bgp = p1 end
			if l1 > lmax then lmax = l1; fgp = p1 end
			if l2 < lmin then lmin = l2; bgp = p2 end
			if l2 > lmax then lmax = l2; fgp = p2 end
			if l3 < lmin then lmin = l3; bgp = p3 end
			if l3 > lmax then lmax = l3; fgp = p3 end
			if l4 < lmin then lmin = l4; bgp = p4 end
			if l4 > lmax then lmax = l4; fgp = p4 end
			if l5 < lmin then lmin = l5; bgp = p5 end
			if l5 > lmax then lmax = l5; fgp = p5 end
			if text and tcp[cell + 1] ~= 0 then
				local s
				s, fg, bg = hb_text(cell, bgp, fg, bg)
				n = n + 1; buf[n] = s
			else
				local thr = (lmin + lmax + 1) // 2
				local bits = 0
				if l0 >= thr then bits = 1 end
				if l1 >= thr then bits = bits + 2 end
				if l2 >= thr then bits = bits + 4 end
				if l3 >= thr then bits = bits + 8 end
				if l4 >= thr then bits = bits + 16 end
				if l5 >= thr then bits = bits + 32 end
				if fgp ~= fg then n = n + 1; buf[n] = fgs[fgp + 1]; fg = fgp end
				if bits ~= 63 and bgp ~= bg then n = n + 1; buf[n] = bgs[bgp + 1]; bg = bgp end
				n = n + 1; buf[n] = glyphs[bits + 1]
			end
		end
		cell = cell + 1
	end
	n = n + 1; buf[n] = HB_RESET
	return concat(buf, "", 1, n)
end

function hb_row(cy)
	if HB_SEXT then return hb_row_sext(cy) end
	return hb_row_half(cy)
end

function hb_present()
	local rows = {}
	for cy = 0, HB_ROWS - 1 do rows[cy + 1] = hb_row(cy) end
	return rows
end

-- ------------------------------------------------------------ pixel font ---
-- 5x7, one int per row, bit 4 = leftmost column

FONT5 = {
	["A"] = {14, 17, 17, 31, 17, 17, 17}, ["B"] = {30, 17, 17, 30, 17, 17, 30}, ["C"] = {15, 16, 16, 16, 16, 16, 15},
	["D"] = {30, 17, 17, 17, 17, 17, 30}, ["E"] = {31, 16, 16, 30, 16, 16, 31}, ["F"] = {31, 16, 16, 30, 16, 16, 16},
	["G"] = {15, 16, 16, 19, 17, 17, 15}, ["H"] = {17, 17, 17, 31, 17, 17, 17}, ["I"] = {31, 4, 4, 4, 4, 4, 31},
	["J"] = {31, 2, 2, 2, 18, 18, 12}, ["K"] = {17, 18, 20, 24, 20, 18, 17}, ["L"] = {16, 16, 16, 16, 16, 16, 31},
	["M"] = {17, 27, 21, 21, 17, 17, 17}, ["N"] = {17, 25, 21, 19, 17, 17, 17}, ["O"] = {14, 17, 17, 17, 17, 17, 14},
	["P"] = {30, 17, 17, 30, 16, 16, 16}, ["Q"] = {14, 17, 17, 17, 21, 18, 13}, ["R"] = {30, 17, 17, 30, 20, 18, 17},
	["S"] = {15, 16, 16, 14, 1, 1, 30}, ["T"] = {31, 4, 4, 4, 4, 4, 4}, ["U"] = {17, 17, 17, 17, 17, 17, 14},
	["V"] = {17, 17, 17, 17, 17, 10, 4}, ["W"] = {17, 17, 17, 21, 21, 21, 10}, ["X"] = {17, 17, 10, 4, 10, 17, 17},
	["Y"] = {17, 17, 10, 4, 4, 4, 4}, ["Z"] = {31, 1, 2, 4, 8, 16, 31},
	["0"] = {14, 17, 19, 21, 25, 17, 14}, ["1"] = {4, 12, 4, 4, 4, 4, 14}, ["2"] = {14, 17, 1, 2, 4, 8, 31},
	["3"] = {30, 1, 1, 14, 1, 1, 30}, ["4"] = {2, 6, 10, 18, 31, 2, 2}, ["5"] = {31, 16, 30, 1, 1, 17, 14},
	["6"] = {14, 16, 16, 30, 17, 17, 14}, ["7"] = {31, 1, 2, 4, 8, 8, 8}, ["8"] = {14, 17, 17, 14, 17, 17, 14},
	["9"] = {14, 17, 17, 15, 1, 1, 14}, ["."] = {0, 0, 0, 0, 0, 12, 12}, ["!"] = {4, 4, 4, 4, 4, 0, 4},
	["-"] = {0, 0, 0, 14, 0, 0, 0}, [":"] = {0, 12, 12, 0, 12, 12, 0}, ["/"] = {1, 1, 2, 4, 8, 16, 16},
	["+"] = {0, 4, 4, 31, 4, 4, 0}, ["?"] = {14, 17, 1, 2, 4, 0, 4}, ["'"] = {4, 4, 8, 0, 0, 0, 0}
}

Gfx = {}
Gfx.__index = Gfx

function Gfx.new() return setmetatable({}, Gfx) end

function Gfx:make_grid(n)
	local g = {}
	for i = 1, n do g[i] = 0 end
	return g
end

function Gfx:make_fgrid(n)
	local g = {}
	for i = 1, n do g[i] = 0.0 end
	return g
end

function Gfx:rainbow_rgb(phase)
	return {128.0 + 127.0 * sin(phase),
		128.0 + 127.0 * sin(phase + 2.094),
		128.0 + 127.0 * sin(phase + 4.188)}
end

function Gfx:fmt1(v)
	local t = itrunc(v * 10.0 + 0.5)
	return idiv(t, 10) .. "." .. imod(t, 10)
end

function Gfx:text_width(text, sx)
	return #text * 6 * sx - sx
end

-- 5x7 text into a pixel grid; rows [row0, row0 + nramp) of the palette shade
-- the glyphs top to bottom, shadow < 0 draws none; only the first `limit` glyphs
function Gfx:draw_text(pix, w, h, text, x0, y0, sx, sy, row0, nramp, shadow, limit)
	local n = #text < limit and #text or limit
	local th = 7 * sy
	for pass = shadow < 0 and 1 or 0, 1 do
		local ox = pass == 0 and 1 or 0
		for li = 0, n - 1 do
			local bits = FONT5[text:sub(li + 1, li + 1)]
			if bits then
				local lx = x0 + li * 6 * sx + ox
				for ry = 0, 6 do
					local b = bits[ry + 1]
					if b ~= 0 then
						for dy = 0, sy - 1 do
							local yy = y0 + ry * sy + dy + ox
							if yy >= 0 and yy < h then
								local c = pass == 0 and shadow or row0 + ((ry * sy + dy) * nramp) // th
								for rx = 0, 4 do
									if ((b >> (4 - rx)) & 1) ~= 0 then
										for dx = 0, sx - 1 do
											local xx = lx + rx * sx + dx
											if xx >= 0 and xx < w then pix[yy * w + xx + 1] = c end
										end
									end
								end
							end
						end
					end
				end
			end
		end
	end
end

-- replace a full row with centered styled text (style = escape prefix)
function Gfx:overlay_center(rows, w, y, text, style)
	if y < 0 or y >= #rows then return end
	local n = #text
	if n > w then
		text = text:sub(1, w)
		n = w
	end
	local pad = (w - n) // 2
	rows[y + 1] = style .. rep(" ", pad) .. text .. rep(" ", w - pad - n) .. RESET
end

function sgr_fg(r, g, b) return ESC .. "[38;2;" .. r .. ";" .. g .. ";" .. b .. "m" end
function sgr_bg(r, g, b) return ESC .. "[48;2;" .. r .. ";" .. g .. ";" .. b .. "m" end
