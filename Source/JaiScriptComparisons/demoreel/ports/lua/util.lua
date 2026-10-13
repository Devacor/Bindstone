-- demoreel util: class helper, JaiScript integer semantics, palettes, the
-- half-block presenter, transitions, the pixel font. Every scene paints a
-- w x (2*rows) grid of palette indices (1-based Lua arrays); the presenter packs
-- two pixels into each terminal cell through the upper-half-block glyph and
-- emits an escape only when a color changes along the row.

local floor, ceil, sin, sqrt = math.floor, math.ceil, math.sin, math.sqrt

function class(base)
	local c = {}
	c.__index = c
	if base then setmetatable(c, {__index = base}) end
	c.new = function(...)
		local o = setmetatable({}, c)
		o:init(...)
		return o
	end
	return c
end

-- C semantics: a float stored into an int truncates toward zero; int / int
-- truncates toward zero (Lua's // floors); only these helpers where an operand
-- can be negative, plain // and floor() where it cannot
function itrunc(v)
	if v >= 0 then return floor(v) end
	return ceil(v)
end

function ifloor(v) return floor(v) end

function idiv(a, b)
	if a < 0 then return -((-a) // b) end
	return a // b
end

function make_grid(n)
	local g = {}
	for i = 1, n do g[i] = 0 end
	return g
end

function make_fgrid(n)
	local g = {}
	for i = 1, n do g[i] = 0.0 end
	return g
end

Palette = class()

function Palette:init()
	self.fg = {}
	self.bg = {}
	self.rgb = {}
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
		local ar, ag, ab = a[1], a[2], a[3]
		local br, bg2, bb = b[1], b[2], b[3]
		self:add(ar + (br - ar) * u, ag + (bg2 - ag) * u, ab + (bb - ab) * u)
	end
	return first
end

function Palette:size() return #self.rgb end

-- ------------------------------------------------------------ presenter ---

HB_PIX = {}
HB_FG = {}
HB_BG = {}
HB_RGB = {}
HB_W = 0
HB_ROWS = 0
HB_HALF = "▀"
HB_FULL = "█"
HB_RESET = ESC .. "[0m"

function hb_ensure(w, rows)
	if HB_W == w and HB_ROWS == rows and #HB_PIX == w * rows * 2 then return end
	HB_W = w
	HB_ROWS = rows
	HB_PIX = make_grid(w * rows * 2)
end

local function hb_row(cy, pix, fgs, bgs, vw, buf)
	local n = 0
	local fg = -1
	local bg = -1
	local ti = cy * 2 * vw
	local bi = ti + vw
	local half, full = HB_HALF, HB_FULL
	for x = 1, vw do
		local top = pix[ti + x]
		local bot = pix[bi + x]
		if top == bot then
			if bot == bg then n = n + 1; buf[n] = " "
			elseif top == fg then n = n + 1; buf[n] = full
			else n = n + 1; buf[n] = bgs[bot + 1]; bg = bot; n = n + 1; buf[n] = " " end
		else
			if top ~= fg then n = n + 1; buf[n] = fgs[top + 1]; fg = top end
			if bot ~= bg then n = n + 1; buf[n] = bgs[bot + 1]; bg = bot end
			n = n + 1; buf[n] = half
		end
	end
	n = n + 1; buf[n] = HB_RESET
	return table.concat(buf, "", 1, n)
end

function hb_present()
	local rows = {}
	local pix, fgs, bgs, vw = HB_PIX, HB_FG, HB_BG, HB_W
	local buf = {}
	for cy = 0, HB_ROWS - 1 do rows[cy + 1] = hb_row(cy, pix, fgs, bgs, vw, buf) end
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

Gfx = class()

function Gfx:init() end

function Gfx:rainbow_rgb(phase)
	return {128.0 + 127.0 * sin(phase),
		128.0 + 127.0 * sin(phase + 2.094),
		128.0 + 127.0 * sin(phase + 4.188)}
end

function Gfx:fmt1(v)
	local t = itrunc(v * 10.0 + 0.5)
	return idiv(t, 10) .. "." .. math.fmod(t, 10)
end

function Gfx:text_width(text, sx)
	return #text * 6 * sx - sx
end

-- 5x7 text into a pixel grid; rows [row0, row0 + nramp) of the palette shade
-- the glyphs top to bottom, shadow < 0 draws none; only the first `limit` glyphs
function Gfx:draw_text(pix, w, h, text, x0, y0, sx, sy, row0, nramp, shadow, limit)
	local n = #text < limit and #text or limit
	local th = 7 * sy
	for pass = (shadow < 0 and 1 or 0), 1 do
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

-- transitions between two pixel grids; gb's indices shift by `off` into the
-- merged palette, `edge` is the palette index of a glowing seam
-- kind: 0 dissolve, 1 slant wipe, 2 blinds, 3 iris, 4 melt
function Gfx:mix_grids(ga, gb, gm, w, h, kind, prog, off, edge, melt)
	local kilo = itrunc(prog * 1000.0)
	local idx = 1
	if kind == 0 then
		for y = 0, h - 1 do
			for x = 0, w - 1 do
				local k = ((((x * 73856093 + y * 19349663) & 1048575) * 1274126177) >> 12) % 1000
				gm[idx] = k < kilo and gb[idx] + off or ga[idx]
				idx = idx + 1
			end
		end
		return
	end
	if kind == 1 then
		local e = itrunc(prog * (w + h // 2 + 8.0)) - 4
		for y = 0, h - 1 do
			local sh = e - (h - y) // 2
			for x = 0, w - 1 do
				local d = x - sh
				gm[idx] = d < 0 and gb[idx] + off or (d < 2 and edge or ga[idx])
				idx = idx + 1
			end
		end
		return
	end
	if kind == 2 then
		local band = 8
		local e2 = itrunc(prog * (band + 1.0))
		for y = 0, h - 1 do
			local yb = (y + (y // band) % 2 * 3) % band
			for x = 0, w - 1 do
				gm[idx] = yb < e2 and gb[idx] + off or (yb == e2 and edge or ga[idx])
				idx = idx + 1
			end
		end
		return
	end
	if kind == 3 then
		local cx = w // 2
		local cy = h // 2
		local rmax = cx * cx + cy * cy
		local r = itrunc(prog * prog * (rmax + 400.0))
		local r2 = itrunc(sqrt(r * 1.0) + 2.0)
		r2 = r2 * r2
		for y = 0, h - 1 do
			local dy = y - cy
			for x = 0, w - 1 do
				local dx = x - cx
				local d2 = dx * dx + dy * dy
				gm[idx] = d2 < r and gb[idx] + off or (d2 < r2 and edge or ga[idx])
				idx = idx + 1
			end
		end
		return
	end
	-- melt: the departing frame slides down in ragged columns
	local fall = prog * prog * (h + 24.0)
	for x = 0, w - 1 do
		local o = itrunc(fall - melt[x + 1])
		o = o < 0 and 0 or o
		local gi = x + 1
		for y = 0, h - 1 do
			gm[gi] = y < o and gb[gi] + off or ga[gi - o * w]
			gi = gi + w
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
	rows[y + 1] = style .. string.rep(" ", pad) .. text .. string.rep(" ", w - pad - n) .. RESET
end

function Gfx:overlay_left(rows, w, y, text, style)
	if y < 0 or y >= #rows then return end
	local n = #text
	if n > w then
		text = text:sub(1, w)
		n = w
	end
	rows[y + 1] = style .. text .. string.rep(" ", w - n) .. RESET
end
