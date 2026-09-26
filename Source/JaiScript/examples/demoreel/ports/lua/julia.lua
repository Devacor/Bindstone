-- JULIA: morphing Julia set, c orbiting just inside the Mandelbrot cardioid, with
-- continuous escape-time coloring and an orbit trap for the inside. Rows serial.

local sin, cos, sqrt, log = math.sin, math.cos, math.sqrt, math.log

local CA, CB, SC, X0, Y0 = 0.0, 0.0, 0.01, 0.0, 0.0
local JW, MAXIT, PHASE, INNER = 0, 48, 0, 0

local function julia_row(y, grid, gi)
	local ww = JW
	local mi = MAXIT
	local ca = CA
	local cb = CB
	local sc = SC
	local zy0 = Y0 + y * sc
	for x = 0, ww - 1 do
		local zx = X0 + x * sc
		local zy = zy0
		local x2 = zx * zx
		local y2 = zy * zy
		local trap = 9.0
		local k = 0
		while k < mi and x2 + y2 < 64.0 do
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			local m = x2 + y2
			trap = m < trap and m or trap
			k = k + 1
		end
		if k >= mi then
			local tl = itrunc(sqrt(trap) * 40.0)
			grid[gi + x] = INNER + 31 - (tl > 31 and 31 or tl)
		else
			local nu = k + 1.0 - log(log(x2 + y2, 2) * 0.5, 2)
			local ci = itrunc(sqrt(nu) * 52.0)
			grid[gi + x] = (ci + PHASE) & 255
		end
	end
end

Julia = class(Scene)

function Julia:init()
	Scene.init(self)
	self.inner = 0
	self.tt = 0.0
end

function Julia:build()
	self.name = "JULIA"
	self.handle = "orbit morphing . by the type ladder"
	self.pal:gradient(256, {{8.0, 4.0, 30.0}, {40.0, 20.0, 110.0}, {170.0, 40.0, 170.0}, {255.0, 110.0, 80.0},
		{255.0, 230.0, 130.0}, {255.0, 255.0, 240.0}, {100.0, 210.0, 255.0}, {30.0, 70.0, 180.0}, {8.0, 4.0, 30.0}})
	self.inner = self.pal:gradient(32, {{2.0, 2.0, 10.0}, {10.0, 26.0, 60.0}, {20.0, 90.0, 120.0}, {150.0, 240.0, 230.0}})
end

function Julia:update(t, dt)
	self.tt = t
end

function Julia:render(grid)
	local tt, w, h = self.tt, self.w, self.h
	local zoom = 1.05 + 0.3 * sin(tt * 0.4)
	CA = 0.7885 * cos(tt * 0.31 + 2.0)
	CB = 0.7885 * sin(tt * 0.23 + 2.0)
	SC = 3.3 / (w * zoom)
	X0 = 0.0 - w * 0.5 * SC
	Y0 = 0.0 - h * 0.5 * SC
	JW = w
	PHASE = itrunc(tt * 22.0)
	INNER = self.inner
	for y = 0, h - 1 do julia_row(y, grid, y * w + 1) end
end
