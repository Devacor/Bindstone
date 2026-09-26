-- DONUT: the torus homage, sphere-traced per pixel, frosted with a wavy icing
-- edge and hashed sprinkles in the torus's own (phi, theta) coordinates. The
-- JaiScript rows run on parallel_transform; here the same row function runs serially.

local sin, cos, sqrt, atan = math.sin, math.cos, math.sqrt, math.atan

local M00, M01, M02 = 1.0, 0.0, 0.0
local M10, M11, M12 = 0.0, 1.0, 0.0
local M20, M21, M22 = 0.0, 0.0, 1.0
local LX, LY, LZ = 0.0, 0.0, 0.0
local HX, HY, HZ = 0.0, 0.0, 0.0
local CAM = 5.6
local DW, DH, MAT, DSEED = 0, 0, 0, 0

-- one row into grid starting at gi (1-based)
local function donut_row(y, grid, gi)
	local ww = DW
	local hh = DH
	local sc = 1.75 / ww
	local v = (hh * 0.5 - y - 0.5) * sc
	-- camera at (0, 0, -cam) looking down +z, turned into the donut's frame
	local cam = CAM
	local ox = 0.0 - M02 * cam
	local oy = 0.0 - M12 * cam
	local oz = 0.0 - M22 * cam
	local oo = ox * ox + oy * oy + oz * oz
	for x = 0, ww - 1 do
		local u = (x + 0.5 - ww * 0.5) * sc
		local il = 1.0 / sqrt(u * u + v * v + 1.0)
		local wx = u * il
		local wy = v * il
		local wz = il
		local dx = M00 * wx + M01 * wy + M02 * wz
		local dy = M10 * wx + M11 * wy + M12 * wz
		local dz = M20 * wx + M21 * wy + M22 * wz
		-- bounding sphere, radius 3.05
		local b = ox * dx + oy * dy + oz * dz
		local disc = b * b - oo + 9.3
		local out = y
		if disc > 0.0 then
			local sq = sqrt(disc)
			local t = 0.0 - b - sq
			local tfar = 0.0 - b + sq
			local hit = 0
			local px, py, pz, ring = 0.0, 0.0, 0.0, 0.0
			for step = 0, 39 do
				px = ox + dx * t
				py = oy + dy * t
				pz = oz + dz * t
				ring = sqrt(px * px + pz * pz)
				local qx = ring - 2.0
				local d = sqrt(qx * qx + py * py) - 1.0
				if d < 0.004 then hit = 1; break end
				t = t + d
				if t > tfar then break end
			end
			if hit ~= 0 then
				-- exact normal: from the tube's center circle out through the hit
				local rs = 2.0 / (ring > 0.0001 and ring or 0.0001)
				local nx = px - px * rs
				local ny = py
				local nz = pz - pz * rs
				local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz)
				nx = nx * nl; ny = ny * nl; nz = nz * nl
				local dl = nx * LX + ny * LY + nz * LZ
				dl = dl < 0.0 and 0.0 or dl
				local s = nx * HX + ny * HY + nz * HZ
				s = s < 0.0 and 0.0 or s
				local s2 = s * s
				local s4 = s2 * s2
				local s8 = s4 * s4
				local s16 = s8 * s8
				local phi = atan(pz, px)
				local m = 0
				if ny > -0.22 + 0.16 * sin(phi * 5.0 + 2.0 * sin(phi * 3.0)) then
					m = 1
					local th = atan(ny, nx * px / ring + nz * pz / ring)
					local fp = (phi + 3.14159265) * 9.5
					local ft = (th + 3.14159265) * 3.5
					local cp = itrunc(fp)
					local ct = itrunc(ft)
					local hs = ((cp * 73856093 + ct * 19349663 + DSEED) & 1048575) * 1274126177
					local r = (hs >> 12) & 1023
					local ep = fp - cp - 0.5
					local et = ft - ct - 0.5
					local across = (r & 1) == 0
					local la = across and ep or et
					local lb = across and et or ep
					if r < 560 and la * la < 0.13 and lb * lb < 0.024 then m = 2 + r % 5 end
				end
				local di = itrunc(dl * 15.99)
				local si = itrunc(s16 * s8 * 3.99)
				out = MAT + m * 64 + di * 4 + si
			end
		end
		grid[gi + x] = out
	end
end

Donut = class(Scene)

function Donut:init()
	Scene.init(self)
	self.mat0 = 0
	self.tt = 0.0
end

function Donut:build()
	self.name = "DONUT"
	self.handle = "torus and effect . by strong_ptr"
	local h, pal = self.h, self.pal
	-- background: warm bakery dusk, one entry per row
	for y = 0, h - 1 do
		local f = y / (h - 1.0)
		pal:add(34.0 + 30.0 * f, 18.0 + 14.0 * f, 30.0 - 8.0 * f)
	end
	-- materials: 16 diffuse levels x 4 specular levels each
	self.mat0 = pal:size()
	local mats = {{196.0, 128.0, 64.0}, {255.0, 132.0, 186.0}, {255.0, 60.0, 60.0}, {255.0, 220.0, 60.0},
		{70.0, 150.0, 255.0}, {90.0, 230.0, 110.0}, {250.0, 250.0, 250.0}}
	for m = 0, 6 do
		local c = mats[m + 1]
		local cr, cg, cb = c[1], c[2], c[3]
		for d = 0, 15 do
			local df = 0.10 + 0.90 * d / 15.0
			for s = 0, 3 do
				local sp = s / 3.0 * (m == 0 and 0.45 or 0.9)
				pal:add(cr * df + (255.0 - cr * df) * sp, cg * df + (255.0 - cg * df) * sp, cb * df + (255.0 - cb * df) * sp)
			end
		end
	end
end

function Donut:update(t, dt)
	self.tt = t
end

function Donut:render(grid)
	local tt = self.tt
	-- object-from-world rotation: the transpose of Rz(b) * Rx(a)
	local ca = cos(tt * 0.9)
	local sa = sin(tt * 0.9)
	local cb = cos(tt * 0.53 + 1.2)
	local sb = sin(tt * 0.53 + 1.2)
	M00 = cb;            M01 = sb;            M02 = 0.0
	M10 = 0.0 - sb * ca; M11 = cb * ca;       M12 = sa
	M20 = sb * sa;       M21 = 0.0 - cb * sa; M22 = ca
	-- light and half vector, world space (camera looks down +z), into the donut's frame
	local lx, ly, lz = -0.42, 0.66, -0.62
	local hx, hy, hz = lx, ly, lz - 1.0
	local hl = 1.0 / sqrt(hx * hx + hy * hy + hz * hz)
	hx = hx * hl; hy = hy * hl; hz = hz * hl
	LX = M00 * lx + M01 * ly + M02 * lz
	LY = M10 * lx + M11 * ly + M12 * lz
	LZ = M20 * lx + M21 * ly + M22 * lz
	HX = M00 * hx + M01 * hy + M02 * hz
	HY = M10 * hx + M11 * hy + M12 * hz
	HZ = M20 * hx + M21 * hy + M22 * hz
	DW = self.w
	DH = self.h
	MAT = self.mat0
	DSEED = self.seed & 1023
	local w = self.w
	for y = 0, self.h - 1 do donut_row(y, grid, y * w + 1) end
end
