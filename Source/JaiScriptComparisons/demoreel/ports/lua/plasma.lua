-- PLASMA: four summed sine fields over a 256-entry cyclic palette. Integer table
-- lookups per pixel; the radial term reads a double-size distance table through a
-- sliding window, so the ripple's center orbits the screen at zero per-pixel cost.

local sin, sqrt = math.sin, math.sqrt

Plasma = class(Scene)

function Plasma:init()
	Scene.init(self)
	self.st = {}
	self.dist = {}
	self.dw = 0
	self.tt = 0.0
end

function Plasma:build()
	self.name = "PLASMA"
	self.handle = "warmup loop . by the symbolizer"
	local w, h = self.w, self.h
	local st = self.st
	for i = 0, 1023 do
		st[i + 1] = itrunc(sin(i * 0.00613592315) * 1024.0)
	end
	self.dw = w * 2
	local dist = self.dist
	local n = 0
	for y = 0, h * 2 - 1 do
		for x = 0, self.dw - 1 do
			local dx = x - w * 1.0
			local dy = y - h * 1.0
			n = n + 1
			dist[n] = itrunc(sqrt(dx * dx + dy * dy) * 26.0) & 1023
		end
	end
	self.pal:gradient(256, {{12.0, 6.0, 40.0}, {68.0, 20.0, 150.0}, {214.0, 40.0, 150.0}, {255.0, 120.0, 60.0},
		{255.0, 226.0, 110.0}, {120.0, 240.0, 200.0}, {30.0, 140.0, 230.0}, {20.0, 30.0, 110.0}, {12.0, 6.0, 40.0}})
end

function Plasma:update(t, dt)
	self.tt = t
end

function Plasma:render(grid)
	local ww = self.w
	local hh = self.h
	local dww = self.dw
	local s = self.st
	local dt_ = self.dist
	local tt = self.tt
	local w, h = self.w, self.h
	local t1 = itrunc(tt * 150.0)
	local t2 = itrunc(tt * 97.0)
	local t3 = itrunc(tt * -210.0) + 4096
	local t4 = itrunc(tt * 61.0)
	local phase = itrunc(tt * 38.0)
	local ox = itrunc(w * 0.5 + w * 0.42 * sin(tt * 0.61))
	local oy = itrunc(h * 0.5 + h * 0.40 * sin(tt * 0.83 + 1.3))
	local zoom = 17 + itrunc(6.0 * sin(tt * 0.35))
	local idx = 1
	for y = 0, hh - 1 do
		local ys = s[((y * 21 + t2) & 1023) + 1]
		local drow = (y + oy) * dww + ox + 1
		local yx = y * 13 + t4
		for x = 0, ww - 1 do
			local v = s[((x * zoom + t1) & 1023) + 1] + ys + s[((dt_[drow + x] + t3) & 1023) + 1] + s[((x * 11 + yx) & 1023) + 1]
			grid[idx] = ((v // 16) + phase) & 255
			idx = idx + 1
		end
	end
end
