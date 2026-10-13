-- LAVALAMP: metaballs, lit. Eight blobs sum an inverse-square field per pixel;
-- the same loop accumulates the gradient and a contribution-weighted hue, the
-- surface is lit from a normal built out of the gradient. Rows serial.

local sin, sqrt = math.sin, math.sqrt

local BX, BY, R2, HUE = {}, {}, {}, {}
local LW, GLOW, RR = 0, 0, 1.0

local function lava_row(y, grid, gi)
	local ww = LW
	local n = #BX
	local bxs, bys, r2s, hues = BX, BY, R2, HUE
	local rr = RR
	local lx = -0.45
	local ly = -0.55
	local lz = 0.70
	local hl = 1.0 / sqrt(lx * lx + ly * ly + (lz + 1.0) * (lz + 1.0))
	local hx = lx * hl
	local hy = ly * hl
	local hz = (lz + 1.0) * hl
	local fy = y + 0.5
	for x = 0, ww - 1 do
		local fx = x + 0.5
		local f = 0.0
		local gx = 0.0
		local gy = 0.0
		local hsum = 0.0
		for b = 1, n do
			local dx = fx - bxs[b]
			local dy = fy - bys[b]
			local d2 = dx * dx + dy * dy + 1.0
			local inv = r2s[b] / d2
			f = f + inv
			local q = inv / d2
			gx = gx + q * dx
			gy = gy + q * dy
			hsum = hsum + inv * hues[b]
		end
		local hq = itrunc(hsum / f + 0.5)
		if f >= 1.0 then
			-- height of a blob of radius rr under this field: rr*sqrt(1 - 1/f);
			-- its gradient is the field gradient over f^2, vertical at the rim
			local rim = 1.0 - 1.0 / f
			rim = rim < 0.004 and 0.004 or rim
			local kz = rr / (f * f * sqrt(rim))
			local nx = gx * kz
			local ny = gy * kz
			local il = 1.0 / sqrt(nx * nx + ny * ny + 1.0)
			nx = nx * il; ny = ny * il
			local d = nx * lx + ny * ly + il * lz
			d = d < 0.0 and 0.0 or d
			local s = nx * hx + ny * hy + il * hz
			local s2 = s * s
			local s4 = s2 * s2
			local s8 = s4 * s4
			local s16 = s8 * s8
			local lv = itrunc((d * 0.78 + s16 * s16 * 0.42) * 15.99)
			grid[gi + x] = hq * 16 + (lv > 15 and 15 or lv)
		else
			local gl = itrunc(f * f * 7.99)
			grid[gi + x] = GLOW + hq * 8 + gl
		end
	end
end

Lava = class(Scene)

function Lava:init()
	Scene.init(self)
	self.bx = {}
	self.by = {}
	self.br2 = {}
	self.hue = {}
	self.fa = {}
	self.fb = {}
	self.pa = {}
	self.pb = {}
	self.nb = 8
	self.glow0 = 0
	self.tt = 0.0
end

function Lava:build()
	self.name = "LAVALAMP"
	self.handle = "wax and gradient . by operator overload"
	local pal, rng, h = self.pal, self.rng, self.h
	-- 24 hues x 16 shades (wax), then 24 hues x 8 glow levels
	for hq = 0, 23 do
		local c = gfx:rainbow_rgb(hq * 0.22 + 4.4)
		local cr, cg, cb = c[1], c[2], c[3]
		for s = 0, 15 do
			local f = s / 15.0
			local lit = f < 0.75 and 0.10 + f * 1.2 or 1.0
			local wh = f < 0.75 and 0.0 or (f - 0.75) * 3.6
			wh = wh > 0.9 and 0.9 or wh
			local r = cr * lit
			local g = cg * lit
			local b = cb * lit
			pal:add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
		end
	end
	self.glow0 = pal:size()
	for hq2 = 0, 23 do
		local c2 = gfx:rainbow_rgb(hq2 * 0.22 + 4.4)
		for g2 = 0, 7 do
			local k = g2 / 7.0
			k = k * k * 0.55
			pal:add(10.0 + c2[1] * k, 4.0 + c2[2] * k, 18.0 + c2[3] * k)
		end
	end
	local nb = self.nb
	for i = 1, nb do
		local r = h * (0.075 + rng:next(50) * 0.001)
		self.br2[i] = r * r
		self.hue[i] = (i - 1) * 23.0 / (nb - 1.0)
		self.fa[i] = 0.13 + rng:next(100) * 0.003
		self.fb[i] = 0.17 + rng:next(100) * 0.004
		self.pa[i] = rng:next(628) * 0.01
		self.pb[i] = rng:next(628) * 0.01
		self.bx[i] = 0.0
		self.by[i] = 0.0
	end
end

function Lava:update(t, dt)
	self.tt = t
	local w, h = self.w, self.h
	local bx, by, fa, fb, pa, pb = self.bx, self.by, self.fa, self.fb, self.pa, self.pb
	for i = 1, self.nb do
		bx[i] = w * (0.5 + 0.38 * sin(t * fa[i] * 2.0 + pa[i]))
		by[i] = h * (0.5 + 0.42 * sin(t * fb[i] * 2.0 + pb[i]))
	end
end

function Lava:render(grid)
	BX = self.bx
	BY = self.by
	R2 = self.br2
	HUE = self.hue
	LW = self.w
	RR = self.h * 0.1
	GLOW = self.glow0
	local w = self.w
	for y = 0, self.h - 1 do lava_row(y, grid, y * w + 1) end
end
