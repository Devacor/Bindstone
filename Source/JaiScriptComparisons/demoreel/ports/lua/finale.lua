-- FINALE: the word JAISCRIPT ignites out of a wall of fire and burns in rolling
-- rainbow flame; then the fire dies, the letters cool to a neon rainbow and the
-- credits roll up beneath them. It IS the fire scene (a subclass): the letters
-- are heat sources stamped from the pixel font.

local sin = math.sin

Finale = class(Fire)

function Finale:init()
	Fire.init(self)
	self.letter_at = {}
	self.letter_of = {}
	self.rim_at = {}
	self.fuel = {}
	self.word_x = 0
	self.word_w = 0
	self.word_top = 0
	self.word_rows = 0
	self.ignite_t = 2.2
	self.rainbow_t = 4.5
	self.wall_out_t = 10.5
	self.embers_t = 11.5
	self.neon_t = 14.0
	self.credits_t = 13.0
	self.credit_rows_per_s = 5.0
	self.credits = {
		{"title", "THE JAISCRIPT DEMOREEL", ""},
		{"", "", ""},
		{"body", "seventeen scenes . five transitions", ""},
		{"body", "every pixel computed by the jaiscript vm", ""},
		{"", "", ""},
		{"role", "PLASMA", "four sine fields"},
		{"role", "STARFIELD", "a warp run through a nebula"},
		{"role", "TUNNEL", "a table-driven texture tunnel"},
		{"role", "DONUT", "a sphere-traced torus"},
		{"role", "JULIA", "a morphing julia set"},
		{"role", "LAVALAMP", "lit metaballs"},
		{"role", "PIPEDREAM", "the 3d pipes screensaver"},
		{"role", "POWDER", "sand, water, ember and plant"},
		{"role", "BOIDS", "a murmuration and a hawk"},
		{"role", "KINSTEIN 3D", "a textured raycaster"},
		{"role", "CANYONRUN", "voxelspace terrain"},
		{"role", "SPONGEWORKS", "a raymarched menger sponge"},
		{"role", "BANNERFALL", "verlet cloth in a gale"},
		{"role", "CHROME", "a ray tracer"},
		{"role", "FIRE", "the doom fire, in rainbow"},
		{"role", "OSSUARY", "a skull spider on a morphing gyroid"},
		{"role", "FINALE", "you are here"},
		{"", "", ""},
		{"body", "half-block pixels, two to a cell", ""},
		{"body", "deterministic to the last byte", ""},
		{"", "", ""},
		{"", "", ""},
		{"title", "thanks for watching", ""},
		{"link", "jaiscript.com", ""}
	}
end

function Finale:build()
	self.name = "FINALE"
	self.handle = "jaiscript forever . rainbow-fire salute"
	self:build_fire()
	self.fuel = make_grid(self.w)
	self:stamp_word("JAISCRIPT")
end

-- the 5x7 font at double size, its second column kept single so a glyph is
-- 9 px wide on a 10 px pitch; narrow screens get it at single size
function Finale:stamp_word(text)
	local w, h = self.w, self.h
	local big = w >= 95
	local font_col = big and {0, 0, 1, 2, 2, 3, 3, 4, 4} or {0, 1, 2, 3, 4}
	local sy = big and 2 or 1
	local pitch = #font_col + 1
	self.word_w = #text * pitch - 1
	self.word_x = (w - self.word_w) // 2
	self.word_rows = 7 * sy
	self.word_top = h // 2 - self.word_rows - 2
	local mask = make_grid(w * h)
	local letter_at, letter_of = self.letter_at, self.letter_of
	for li = 0, #text - 1 do
		local bits = FONT5[text:sub(li + 1, li + 1)]
		for ry = 0, self.word_rows - 1 do
			local b = bits[ry // sy + 1]
			for rx = 0, #font_col - 1 do
				if ((b >> (4 - font_col[rx + 1])) & 1) == 1 then
					local i = (self.word_top + ry) * w + self.word_x + li * pitch + rx
					mask[i + 1] = 1
					letter_at[#letter_at + 1] = i
					letter_of[#letter_of + 1] = li
				end
			end
		end
	end
	local rim_at = self.rim_at
	for i = w, w * (h - 1) - 1 do
		local j = i + 1
		if mask[j] == 0 and (mask[j - 1] == 1 or mask[j + 1] == 1 or mask[j - w] == 1 or mask[j + w] == 1) then
			rim_at[#rim_at + 1] = i
		end
	end
end

function Finale:update(t, dt)
	self.tt = t
	self.fr = self.fr + 1
	self.rainbow_on = t > self.rainbow_t and 1 or 0
	if self.rainbow_on == 1 then self.hue_t = self.hue_t + dt * 3.0 end
	local amp = t > self.wall_out_t and itrunc(42.0 - (t - self.wall_out_t) * 19.0) or 42
	amp = amp < 0 and 0 or amp
	self:seed_rows(amp, amp // 2 + 2)
	self:convect()
	if t > self.ignite_t then self:burn_letters(t) end
	self:move_sparks(dt, amp > 12)
end

-- each column's fuel swells and ebbs where two waves cross, and the air above
-- the word cools fast, so the flames rise as separate tongues; once the letters
-- have cooled, the last embers anywhere go out the same way
function Finale:burn_letters(t)
	local w = self.w
	local embers_t, neon_t = self.embers_t, self.neon_t
	local strength = t < embers_t and 1.0 or (t > neon_t and 0.0 or (neon_t - t) / (neon_t - embers_t))
	local fuel = self.fuel
	for x = 0, w - 1 do
		local wave = sin(x * 0.61 + t * 6.1) + sin(x * 0.23 - t * 3.7 + 1.7)
		fuel[x + 1] = itrunc((46.0 + wave * 15.0) * strength)
	end
	local ht = self.heat
	local rn = self.cool
	local at = self.letter_at
	local fr = self.fr
	for k = 1, #at do
		local i = at[k]
		local v = fuel[i % w + 1] + (rn[((i * 7 + fr) & 1023) + 1] & 1) * 8
		if ht[i + 1] < v then ht[i + 1] = v end
	end
	local quenched = t > neon_t and w * self.h or self.word_top * w
	for i = 1, quenched do
		local v = ht[i] - 2
		ht[i] = v > 0 and v or 0
	end
end

-- the letters burn white-hot over their own flames, ringed by a dark rim; once
-- the fire is out each cools to its own hue of the rainbow, and a glint sweeps
-- across the word every few seconds
function Finale:render(grid)
	local b = self:bank()
	local n = self.w * self.h
	local ht = self.heat
	for i = 1, n do
		local v = ht[i]
		grid[i] = (v > 72 and 72 or v) + b
	end
	if self.tt > self.ignite_t then
		local rim = self.rim_at
		for k = 1, #rim do
			local j = rim[k] + 1
			grid[j] = b + ht[j] // 3
		end
		self:draw_letters(grid, b)
	end
	self:draw_sparks(grid, b)
end

function Finale:draw_letters(grid, b)
	local at = self.letter_at
	local fr = self.fr
	local tt, embers_t, neon_t = self.tt, self.embers_t, self.neon_t
	if tt < embers_t then
		for k = 1, #at do
			local i = at[k]
			grid[i + 1] = b + 64 + ((i * 7 + fr) & 7)
		end
		return
	end
	local w, levels, phases = self.w, self.levels, self.phases
	local letter_of = self.letter_of
	local cooled = tt > neon_t and 1.0 or (tt - embers_t) / (neon_t - embers_t)
	local level = 64 - itrunc(cooled * 12.0)
	local roll = itrunc(tt * 2.0)
	local glint = tt > neon_t and itrunc((tt - neon_t) * 60.0) % (self.word_w + 160) - 20 or -20
	local word_x, word_top = self.word_x, self.word_top
	for k = 1, #at do
		local i = at[k]
		local d = i % w - word_x + i // w - word_top - glint
		local lv = (d >= 0 and d < 6) and 72 - d or level + ((i * 7 + fr) & 3)
		grid[i + 1] = levels + ((letter_of[k] + roll) % phases) * levels + lv
	end
end

-- the credits roll up from the bottom row, under the word, and stop with the
-- last lines resting midway; rows near the edges of the band dim
function Finale:text_rows(rows)
	if self.tt < self.credits_t then return end
	local top = (self.word_top + self.word_rows) // 2 + 2
	local bottom = self.h // 2 - 2
	if bottom - top < 2 then return end
	local credits = self.credits
	local last = #credits - 1
	local rest = bottom + last - (top + bottom) // 2
	local scroll = itrunc((self.tt - self.credits_t) * self.credit_rows_per_s)
	scroll = scroll > rest and rest or scroll
	for k = 0, last do
		local row = bottom + 1 + k - scroll
		local line = credits[k + 1]
		if row >= top and row <= bottom and line[1] ~= "" then
			local edge = row - top < bottom - row and row - top or bottom - row
			rows[row + 1] = self:credit_row(line, edge >= 2 and 0 or 2 - edge)
		end
	end
end

local function credit_ink(r, g, b, dim)
	return ESC .. "[38;2;" .. (r * (3 - dim) // 3) .. ";" .. (g * (3 - dim) // 3) .. ";" .. (b * (3 - dim) // 3) .. "m"
end

function Finale:credit_row(line, dim)
	local w = self.w
	local kind, left, right = line[1], line[2], line[3]
	local result = ESC .. "[48;2;0;0;0m"
	if kind == "role" then
		local gutter = w // 2
		left = #left > gutter - 2 and left:sub(1, gutter - 2) or left
		right = #right > w - gutter - 1 and right:sub(1, w - gutter - 1) or right
		result = result .. string.rep(" ", gutter - 1 - #left) .. credit_ink(255, 206, 110, dim) .. left
			.. "  " .. credit_ink(186, 180, 236, dim) .. right .. string.rep(" ", w - gutter - 1 - #right)
	else
		left = #left > w and left:sub(1, w) or left
		local pad = (w - #left) // 2
		local ink = credit_ink(214, 208, 255, dim)
		if kind == "title" then ink = credit_ink(255, 236, 190, dim) end
		if kind == "link" then ink = credit_ink(120, 200, 255, dim) end
		result = result .. string.rep(" ", pad) .. ink .. left .. string.rep(" ", w - pad - #left)
	end
	return result .. RESET
end
