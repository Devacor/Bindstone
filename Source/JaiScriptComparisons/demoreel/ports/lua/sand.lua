-- POWDER: falling-sand mini toy: sand, water, ember, plant, smoke, each a class
-- owning its behavior. Spawners keep it evolving unattended. Cell arrays are
-- 1-based (cell i lives at c[i + 1]); coordinates stay 0-based.
-- Elements draw from the scene Rng itself (the reference's step takes it as var& r).

Element = class()
function Element:init() end
function Element:step(c, a, w, h, i, x, y, r, fr) end

SandEl = class(Element)
function SandEl:step(c, a, w, h, i, x, y, r, fr)
	if y + 1 >= h then return end
	local b = i + w
	local t = c[b + 1]
	if t == 0 then c[b + 1] = 2; c[i + 1] = 0; return end
	if t == 3 then c[b + 1] = 2; c[i + 1] = 3; return end   -- heavier than water: swap
	local dir = ((x + y + fr) & 1) * 2 - 1
	local nx = x + dir
	if nx >= 0 and nx < w and c[b + dir + 1] == 0 and c[i + dir + 1] == 0 then c[b + dir + 1] = 2; c[i + 1] = 0; return end
	nx = x - dir
	if nx >= 0 and nx < w and c[b - dir + 1] == 0 and c[i - dir + 1] == 0 then c[b - dir + 1] = 2; c[i + 1] = 0 end
end

WaterEl = class(Element)
function WaterEl:step(c, a, w, h, i, x, y, r, fr)
	if y + 1 < h then
		local b = i + w
		if c[b + 1] == 0 then c[b + 1] = 3; c[i + 1] = 0; return end
		local dir = ((x * 3 + y + fr * 5) & 1) * 2 - 1
		local nx = x + dir
		if nx >= 0 and nx < w and c[b + dir + 1] == 0 and c[i + dir + 1] == 0 then c[b + dir + 1] = 3; c[i + 1] = 0; return end
	end
	-- flow up to four cells sideways per step, so pools level out
	local dir = ((x + fr * 3 + y * 7) & 1) * 2 - 1
	for side = 0, 1 do
		local far = 0
		for k = 1, 4 do
			local nx = x + dir * k
			if nx < 0 or nx >= w or c[i + dir * k + 1] ~= 0 then break end
			far = k
		end
		if far > 0 then c[i + dir * far + 1] = 3; c[i + 1] = 0; return end
		dir = 0 - dir
	end
end

EmberEl = class(Element)
function EmberEl:step(c, a, w, h, i, x, y, r, fr)
	local j = i + 1
	a[j] = a[j] + 1
	-- ignite any neighboring plant
	if x > 0 and c[j - 1] == 5 then c[j - 1] = 4; a[j - 1] = 0 end
	if x < w - 1 and c[j + 1] == 5 then c[j + 1] = 4; a[j + 1] = 0 end
	if y > 0 and c[j - w] == 5 then c[j - w] = 4; a[j - w] = 0 end
	if y < h - 1 and c[j + w] == 5 then c[j + w] = 4; a[j + w] = 0 end
	if a[j] > 22 + r:next(26) then c[j] = 6; a[j] = 0; return end   -- burn out to smoke
	if y + 1 >= h then return end
	local b = j + w
	if c[b] == 3 then c[j] = 6; a[j] = 0; return end   -- doused
	if (fr & 1) == 0 and c[b] == 0 then c[b] = 4; a[b] = a[j]; c[j] = 0; a[j] = 0 end
end

PlantEl = class(Element)
function PlantEl:step(c, a, w, h, i, x, y, r, fr)
	local j = i + 1
	if a[j] > 30 then return end             -- generation cap: stop creeping forever
	if r:next(100) >= 6 then return end
	-- drink an adjacent water cell, then sprout into an empty neighbor
	local drink = -1
	if x > 0 and c[j - 1] == 3 then drink = j - 1
	elseif x < w - 1 and c[j + 1] == 3 then drink = j + 1
	elseif y > 0 and c[j - w] == 3 then drink = j - w
	elseif y < h - 1 and c[j + w] == 3 then drink = j + w end
	if drink < 0 then return end
	c[drink] = 0
	local sprout = -1
	if y > 0 and c[j - w] == 0 then sprout = j - w
	elseif x > 0 and y > 0 and c[j - w - 1] == 0 then sprout = j - w - 1
	elseif x < w - 1 and y > 0 and c[j - w + 1] == 0 then sprout = j - w + 1
	elseif x > 0 and c[j - 1] == 0 then sprout = j - 1
	elseif x < w - 1 and c[j + 1] == 0 then sprout = j + 1 end
	if sprout >= 0 then
		c[sprout] = 5
		a[sprout] = a[j] + 1
	end
end

SmokeEl = class(Element)
function SmokeEl:step(c, a, w, h, i, x, y, r, fr)
	local j = i + 1
	a[j] = a[j] + 1
	if a[j] > 26 then c[j] = 0; a[j] = 0; return end
	if y == 0 then c[j] = 0; a[j] = 0; return end
	local wob = ((x + fr + a[j] * 3) % 3) - 1
	local nx = x + wob
	if nx >= 0 and nx < w and c[j - w + wob] == 0 then c[j - w + wob] = 6; a[j - w + wob] = a[j]; c[j] = 0; a[j] = 0; return end
	if c[j - w] == 0 then c[j - w] = 6; a[j - w] = a[j]; c[j] = 0; a[j] = 0 end
end

SandBox = class(Scene)

function SandBox:init()
	Scene.init(self)
	self.cells = nil
	self.aux = nil
	self.depth = {}
	self.fr = 0
	self.p_sand = 0
	self.p_water = 0
	self.p_ember = 0
	self.p_plant = 0
	self.p_smoke = 0
	self.p_rock = 0
	self.p_glow = 0
end

function SandBox:build()
	self.name = "POWDER"
	self.handle = "sand.water.ember.plant . by ref_lvalue"
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	self.cells = make_grid(w * h)
	self.aux = make_grid(w * h)
	self.depth = make_grid(w)
	self.el_sand = SandEl.new()
	self.el_water = WaterEl.new()
	self.el_ember = EmberEl.new()
	self.el_plant = PlantEl.new()
	self.el_smoke = SmokeEl.new()

	-- 0..h-1: the empty air, a dusk gradient by row
	for y = 0, h - 1 do
		local f = y / (h - 1.0)
		pal:add(8.0 + 18.0 * f, 6.0 + 10.0 * f, 18.0 + 16.0 * f)
	end
	self.p_rock = pal:ramp(4, 50.0, 46.0, 58.0, 118.0, 112.0, 126.0)
	self.p_sand = pal:ramp(6, 170.0, 130.0, 66.0, 250.0, 222.0, 150.0)
	self.p_water = pal:ramp(8, 90.0, 170.0, 255.0, 14.0, 40.0, 130.0)
	self.p_ember = pal:gradient(6, {{150.0, 30.0, 10.0}, {255.0, 90.0, 20.0}, {255.0, 190.0, 60.0}, {255.0, 255.0, 200.0}})
	self.p_plant = pal:ramp(5, 28.0, 96.0, 40.0, 120.0, 220.0, 100.0)
	self.p_smoke = pal:ramp(4, 96.0, 94.0, 104.0, 34.0, 32.0, 42.0)
	self.p_glow = pal:ramp(3, 60.0, 26.0, 16.0, 140.0, 60.0, 24.0)

	-- rock floor + two shelves making basins
	local cells = self.cells
	local ground = (h - 1) * w + 1
	for x = 0, w - 1 do cells[ground + x] = 1; cells[ground - w + x] = 1 end
	local shelf1 = (h - h * 9 // 40) * w + 1
	for x = w // 8, w // 8 + w // 4 - 1 do cells[shelf1 + x] = 1; cells[shelf1 + w + x] = 1 end
	local shelf2 = (h - h * 15 // 40) * w + 1
	for x = w - w // 3 - w // 8, w - w // 8 - 1 do cells[shelf2 + x] = 1; cells[shelf2 + w + x] = 1 end
	for k = 0, 13 do
		local x = 4 + rng:next(w - 8)
		cells[ground - w - w + x] = 5
	end
end

function SandBox:update(t, dt)
	for sub = 0, 1 do
		self.fr = self.fr + 1
		self:do_spawn()
		self:do_fall()
		self:do_smoke()
	end
end

function SandBox:do_spawn()
	local w, rng, cells, aux = self.w, self.rng, self.cells, self.aux
	for k = 0, 1 do
		local sx = w // 5 + rng:next(7) - 3
		if cells[sx + 1] == 0 then cells[sx + 1] = 2 end
		local wx = (w * 4) // 5 + rng:next(7) - 3
		if cells[wx + 1] == 0 then cells[wx + 1] = 3 end
	end
	if self.fr % 90 == 0 then
		for k2 = 0, 5 do
			local ex = w // 2 - 5 + rng:next(11)
			if cells[ex + 1] == 0 then cells[ex + 1] = 4; aux[ex + 1] = 0 end
		end
	end
end

-- bottom-up: falling and static matter (alternate x sweep per frame)
function SandBox:do_fall()
	local w, h, fr, rng = self.w, self.h, self.fr, self.rng
	local cells, aux = self.cells, self.aux
	local el_sand, el_water, el_ember, el_plant = self.el_sand, self.el_water, self.el_ember, self.el_plant
	for y = h - 2, 0, -1 do
		local row = y * w
		if (fr & 1) == 0 then
			for x = 0, w - 1 do
				local i = row + x
				local t2 = cells[i + 1]
				if t2 == 2 then el_sand:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 3 then el_water:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 4 then el_ember:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 5 then el_plant:step(cells, aux, w, h, i, x, y, rng, fr) end
			end
		else
			for x = w - 1, 0, -1 do
				local i = row + x
				local t2 = cells[i + 1]
				if t2 == 2 then el_sand:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 3 then el_water:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 4 then el_ember:step(cells, aux, w, h, i, x, y, rng, fr)
				elseif t2 == 5 then el_plant:step(cells, aux, w, h, i, x, y, rng, fr) end
			end
		end
	end
end

-- top-down: rising smoke
function SandBox:do_smoke()
	local w, h, fr, rng = self.w, self.h, self.fr, self.rng
	local cells, aux, el_smoke = self.cells, self.aux, self.el_smoke
	for i = 0, w * h - 1 do
		if cells[i + 1] == 6 then
			el_smoke:step(cells, aux, w, h, i, i % w, i // w, rng, fr)
		end
	end
end

-- surfaces catch the light, water darkens with the depth of the column above it,
-- and embers throw a warm glow onto the air around them
function SandBox:render(grid)
	local ww = self.w
	local hh = self.h
	local c = self.cells
	local a = self.aux
	local dp = self.depth
	local fr = self.fr
	local p_water, p_sand, p_rock, p_plant, p_ember, p_smoke = self.p_water, self.p_sand, self.p_rock, self.p_plant, self.p_ember, self.p_smoke
	for x0 = 1, ww do dp[x0] = 0 end
	local idx = 1
	for y = 0, hh - 1 do
		for x = 0, ww - 1 do
			local t = c[idx]
			local g = y
			local open = y == 0 or c[idx - ww] == 0
			if t == 3 then
				local d = dp[x + 1] + 1
				dp[x + 1] = d
				local wl = open and 0 or 1 + d // 3 + (x * 3 + y + fr // 5) % 2
				g = p_water + (wl > 7 and 7 or wl)
			else
				dp[x + 1] = 0
				if t == 2 then g = p_sand + (open and 5 or ((x * 7 + y * 13) * 2971 // 7) % 4 + (y % 3 == 0 and 0 or 1))
				elseif t == 1 then g = p_rock + (open and 3 or (x * 5 + y * 3) % 3)
				elseif t == 5 then g = p_plant + (open and 4 or (x * 7 + y * 3) % 4)
				elseif t == 4 then g = p_ember + (a[idx] * 3 + x + fr) % 6
				elseif t == 6 then local sa = a[idx] // 7; g = p_smoke + (sa > 3 and 3 or sa) end
			end
			grid[idx] = g
			idx = idx + 1
		end
	end
	local p_glow = self.p_glow
	for i = ww + 1, ww * hh - ww do
		if c[i] == 4 then
			local gl = p_glow + (fr + i - 1) % 3
			if c[i - 1] == 0 then grid[i - 1] = gl end
			if c[i + 1] == 0 then grid[i + 1] = gl end
			if c[i - ww] == 0 then grid[i - ww] = gl end
			if c[i + ww] == 0 then grid[i + ww] = gl end
		end
	end
end
