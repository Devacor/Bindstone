-- A cell canvas for the showcase HUD and pages. Each cell is two stacked
-- pixels (drawn as an upper half block, square on a 1:2 cell) or one character
-- over the cell's upper pixel. Colors are palette indices.

local concat = table.concat

-- ------------------------------------------------------------ pixel font ----
-- 3x5 capitals, digits and a little punctuation; '#' is ink
FONT5 = {
	["A"] = {"###", "#.#", "###", "#.#", "#.#"}, ["B"] = {"##.", "#.#", "##.", "#.#", "##."},
	["C"] = {"###", "#..", "#..", "#..", "###"}, ["D"] = {"##.", "#.#", "#.#", "#.#", "##."},
	["E"] = {"###", "#..", "##.", "#..", "###"}, ["F"] = {"###", "#..", "##.", "#..", "#.."},
	["G"] = {"###", "#..", "#.#", "#.#", "###"}, ["H"] = {"#.#", "#.#", "###", "#.#", "#.#"},
	["I"] = {"###", ".#.", ".#.", ".#.", "###"}, ["J"] = {"..#", "..#", "..#", "#.#", "###"},
	["K"] = {"#.#", "#.#", "##.", "#.#", "#.#"}, ["L"] = {"#..", "#..", "#..", "#..", "###"},
	["M"] = {"#.#", "###", "###", "#.#", "#.#"}, ["N"] = {"##.", "#.#", "#.#", "#.#", "#.#"},
	["O"] = {"###", "#.#", "#.#", "#.#", "###"}, ["P"] = {"###", "#.#", "###", "#..", "#.."},
	["Q"] = {"###", "#.#", "#.#", "###", "..#"}, ["R"] = {"##.", "#.#", "##.", "#.#", "#.#"},
	["S"] = {"###", "#..", "###", "..#", "###"}, ["T"] = {"###", ".#.", ".#.", ".#.", ".#."},
	["U"] = {"#.#", "#.#", "#.#", "#.#", "###"}, ["V"] = {"#.#", "#.#", "#.#", "#.#", ".#."},
	["W"] = {"#.#", "#.#", "###", "###", "#.#"}, ["X"] = {"#.#", "#.#", ".#.", "#.#", "#.#"},
	["Y"] = {"#.#", "#.#", ".#.", ".#.", ".#."}, ["Z"] = {"###", "..#", ".#.", "#..", "###"},
	["0"] = {"###", "#.#", "#.#", "#.#", "###"}, ["1"] = {".#.", "##.", ".#.", ".#.", "###"},
	["2"] = {"###", "..#", "###", "#..", "###"}, ["3"] = {"###", "..#", ".##", "..#", "###"},
	["4"] = {"#.#", "#.#", "###", "..#", "..#"}, ["5"] = {"###", "#..", "###", "..#", "###"},
	["6"] = {"###", "#..", "###", "#.#", "###"}, ["7"] = {"###", "..#", ".#.", ".#.", ".#."},
	["8"] = {"###", "#.#", "###", "#.#", "###"}, ["9"] = {"###", "#.#", "###", "..#", "###"},
	["%"] = {"#.#", "..#", ".#.", "#..", "#.#"}, [":"] = {"...", ".#.", "...", ".#.", "..."},
	["/"] = {"..#", "..#", ".#.", "#..", "#.."}, ["-"] = {"...", "...", "###", "...", "..."},
	["!"] = {".#.", ".#.", ".#.", "...", ".#."}, ["'"] = {".#.", ".#.", "...", "...", "..."},
	["."] = {"...", "...", "...", "...", ".#."}, [" "] = {"...", "...", "...", "...", "..."},
}

Canvas = {}
Canvas.__index = Canvas

function Canvas.new(w, h)
	local px = {}
	for i = 1, w * h * 2 do px[i] = PAL_BLACK end
	local glyph = {}
	local ink = {}
	for i = 1, w * h do
		glyph[i] = ""
		ink[i] = PAL_BLACK
	end
	return setmetatable({
		w = w,
		h = h,             -- cells; pixel rows are 2 * h
		px = px,           -- palette index per pixel, row-major, w x 2h, [y * w + x + 1]
		glyph = glyph,     -- "" or the character drawn in the cell
		ink = ink,         -- its color
		parts = {},
	}, Canvas)
end

function Canvas:clear(color)
	local px = self.px
	for i = 1, #px do px[i] = color end
	local glyph = self.glyph
	for i = 1, #glyph do glyph[i] = "" end
end

function Canvas:dot(x, y, color)
	if x >= 0 and x < self.w and y >= 0 and y < self.h * 2 then self.px[y * self.w + x + 1] = color end
end

function Canvas:rect(x, y, rw, rh, color)
	local w = self.w
	local px = self.px
	for yy = imax(0, y), imin(self.h * 2, y + rh) - 1 do
		for xx = imax(0, x), imin(w, x + rw) - 1 do px[yy * w + xx + 1] = color end
	end
end

-- one color per pixel row from top to bottom, from rows y0 to y1 (exclusive)
function Canvas:gradient(y0, y1, top, bottom)
	local w = self.w
	local px = self.px
	for y = imax(0, y0), imin(self.h * 2, y1) - 1 do
		local c = color_idx(cmix(top, bottom, 1.0 * (y - y0) / imax(1, y1 - y0 - 1)))
		for x = 0, w - 1 do px[y * w + x + 1] = c end
	end
end

-- text at cell (x, row); characters past the edge are dropped
function Canvas:text(x, row, s, color)
	if row < 0 or row >= self.h then return end
	local w = self.w
	local glyph = self.glyph
	local ink = self.ink
	for i = 0, #s - 1 do
		local cx = x + i
		if cx >= 0 and cx < w then
			glyph[row * w + cx + 1] = s:sub(i + 1, i + 1)
			ink[row * w + cx + 1] = color
		end
	end
end

function Canvas:text_centered(row, s, color) self:text(idivt(self.w - #s, 2), row, s, color) end

function Canvas:text_right(x_end, row, s, color) self:text(x_end - #s, row, s, color) end

-- an art (defs.lua) at pixel (x, y), through a code -> color table
function Canvas:art(a, x, y, lut)
	local aw = a.w
	local apix = a.pix
	for ay = 0, a.h - 1 do
		for ax = 0, aw - 1 do
			local code = apix[ay * aw + ax + 1]
			if code > 0 then self:dot(x + ax, y + ay, lut[code]) end
		end
	end
end

-- s in the 3x5 font with its top-left at pixel (x, y), a color per font row, a shadow below right
function Canvas:lettering(s, x, y, scale, shades, shadow)
	for pass = 0, 1 do
		for i = 0, #s - 1 do
			local glyph_rows = FONT5[s:sub(i + 1, i + 1)]
			if glyph_rows then
				local gx = x + i * 4 * scale
				for ry = 0, 4 do
					local grow = glyph_rows[ry + 1]
					for rx = 0, 2 do
						if grow:sub(rx + 1, rx + 1) == "#" then
							if pass == 0 and shadow >= 0 then self:rect(gx + (rx + 1) * scale, y + (ry + 1) * scale, scale, scale, shadow) end
							if pass == 1 then self:rect(gx + rx * scale, y + ry * scale, scale, scale, shades[ry + 1]) end
						end
					end
				end
			end
		end
	end
end

-- rows first .. last (cells) as terminal lines
function Canvas:rows(first, last)
	local out = {}
	local parts = self.parts
	local w = self.w
	local px = self.px
	local glyph = self.glyph
	local ink = self.ink
	local fgs = PAL_FG
	local bgs = PAL_BG
	local hb = HALFG
	for row = first, last - 1 do
		local n = 0
		local fg = -1
		local bg = -1
		for x = 0, w - 1 do
			local cell = row * w + x + 1
			local upper = px[row * 2 * w + x + 1]
			local lower = px[(row * 2 + 1) * w + x + 1]
			local ch = glyph[cell]
			if ch ~= "" then
				local k = ink[cell]
				if k ~= fg then fg = k; n = n + 1; parts[n] = fgs[fg] end
				if upper ~= bg then bg = upper; n = n + 1; parts[n] = bgs[bg] end
				n = n + 1
				parts[n] = ch
			elseif upper == lower then
				if upper ~= bg then bg = upper; n = n + 1; parts[n] = bgs[bg] end
				n = n + 1
				parts[n] = " "
			else
				if upper ~= fg then fg = upper; n = n + 1; parts[n] = fgs[fg] end
				if lower ~= bg then bg = lower; n = n + 1; parts[n] = bgs[bg] end
				n = n + 1
				parts[n] = hb
			end
		end
		n = n + 1
		parts[n] = RESET
		out[#out + 1] = concat(parts, "", 1, n)
	end
	return out
end

-- width in pixels of s in the 3x5 font at a scale, one pixel of spacing per letter
function lettering_width(s, scale) return #s * 4 * scale - scale end
