-- CHROME: the Amiga Juggler's grandchild: a ray tracer, one ray per pixel.
-- Three chrome spheres bounce on a checkerboard; sphere hits spawn a reflection
-- ray, floor hits a shadow ray. ch_trace returns what a ray SAW as a class; a
-- sphere pixel is that class in its own tinted copy of the class table. Rows serial.

local sin, cos, sqrt = math.sin, math.cos, math.sqrt

local X0, Y0, Z0, R0 = 0.0, 0.0, 0.0, 0.9
local X1, Y1, Z1, R1 = 0.0, 0.0, 0.0, 0.7
local X2, Y2, Z2, R2 = 0.0, 0.0, 0.0, 0.55
local EX, EY, EZ = 0.0, 1.6, 5.0
local FX, FY, FZ = 0.0, 0.0, -1.0
local RX, RZ = 1.0, 0.0
local UX, UY, UZ = 0.0, 1.0, 0.0
local LX, LY, LZ = 0.5, 0.75, 0.43
local CW, CH, FLOOR, BALL, NBASE, GLINT = 0, 0, 0, 0, 0, 0

-- what a ray from (ox,oy,oz) along unit (dx,dy,dz) sees, as a class index;
-- `skip` is the sphere it left
local function ch_trace(ox, oy, oz, dx, dy, dz, skip)
	local best = 1000000.0
	local hit = -1
	local hr = 1.0
	local hx, hy, hz = 0.0, 0.0, 0.0
	if skip ~= 0 then
		local qx = ox - X0; local qy = oy - Y0; local qz = oz - Z0
		local b = qx * dx + qy * dy + qz * dz
		local disc = b * b - (qx * qx + qy * qy + qz * qz - R0 * R0)
		if disc > 0.0 then
			local t = 0.0 - b - sqrt(disc)
			if t > 0.001 and t < best then best = t; hit = 0; hr = R0; hx = X0; hy = Y0; hz = Z0 end
		end
	end
	if skip ~= 1 then
		local qx1 = ox - X1; local qy1 = oy - Y1; local qz1 = oz - Z1
		local b1 = qx1 * dx + qy1 * dy + qz1 * dz
		local disc1 = b1 * b1 - (qx1 * qx1 + qy1 * qy1 + qz1 * qz1 - R1 * R1)
		if disc1 > 0.0 then
			local t1 = 0.0 - b1 - sqrt(disc1)
			if t1 > 0.001 and t1 < best then best = t1; hit = 1; hr = R1; hx = X1; hy = Y1; hz = Z1 end
		end
	end
	if skip ~= 2 then
		local qx2 = ox - X2; local qy2 = oy - Y2; local qz2 = oz - Z2
		local b2 = qx2 * dx + qy2 * dy + qz2 * dz
		local disc2 = b2 * b2 - (qx2 * qx2 + qy2 * qy2 + qz2 * qz2 - R2 * R2)
		if disc2 > 0.0 then
			local t2 = 0.0 - b2 - sqrt(disc2)
			if t2 > 0.001 and t2 < best then best = t2; hit = 2; hr = R2; hx = X2; hy = Y2; hz = Z2 end
		end
	end
	if hit >= 0 then
		local d = ((ox + dx * best - hx) * LX + (oy + dy * best - hy) * LY + (oz + dz * best - hz) * LZ) / hr
		d = d < 0.0 and 0.0 or d
		local lv = itrunc((0.2 + 0.8 * d) * 7.99)
		return BALL + hit * 8 + lv
	end
	if dy < -0.0005 then
		local tf = (0.0 - oy) / dy
		local fx = ox + dx * tf
		local fz = oz + dz * tf
		local ix = itrunc(fx + 4096.0)
		local iz = itrunc(fz + 4096.0)
		local fog = itrunc(11.5 - tf * 0.55)
		fog = fog < 0 and 0 or (fog > 11 and 11 or fog)
		-- shadow: does the ray toward the sun pass through a sphere?
		local shade = 0
		local sx = fx - X0; local sz = fz - Z0
		local sb = sx * LX - Y0 * LY + sz * LZ
		if sb < 0.0 and sb * sb - (sx * sx + Y0 * Y0 + sz * sz - R0 * R0) > 0.0 then shade = 1 end
		local sx1 = fx - X1; local sz1 = fz - Z1
		local sb1 = sx1 * LX - Y1 * LY + sz1 * LZ
		if sb1 < 0.0 and sb1 * sb1 - (sx1 * sx1 + Y1 * Y1 + sz1 * sz1 - R1 * R1) > 0.0 then shade = 1 end
		local sx2 = fx - X2; local sz2 = fz - Z2
		local sb2 = sx2 * LX - Y2 * LY + sz2 * LZ
		if sb2 < 0.0 and sb2 * sb2 - (sx2 * sx2 + Y2 * Y2 + sz2 * sz2 - R2 * R2) > 0.0 then shade = 1 end
		return FLOOR + (((ix + iz) & 1) * 2 + shade) * 12 + fog
	end
	local s = itrunc(dy * 30.0)
	return s < 0 and 0 or (s > 23 and 23 or s)
end

local function ch_row(y, grid, gi)
	local ww = CW
	local sc = 2.0 / ww
	local v = (CH * 0.5 - y - 0.5) * sc
	local ex, ey, ez = EX, EY, EZ
	local bx = FX * 1.35 + UX * v
	local by = FY * 1.35 + UY * v
	local bz = FZ * 1.35 + UZ * v
	for x = 0, ww - 1 do
		local u = (x + 0.5 - ww * 0.5) * sc
		local dx = bx + RX * u
		local dy = by
		local dz = bz + RZ * u
		local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
		dx = dx * il; dy = dy * il; dz = dz * il
		local best = 1000000.0
		local hit = -1
		local cx, cy, cz, cr = 0.0, 0.0, 0.0, 1.0
		local qx = ex - X0; local qy = ey - Y0; local qz = ez - Z0
		local b = qx * dx + qy * dy + qz * dz
		local disc = b * b - (qx * qx + qy * qy + qz * qz - R0 * R0)
		if disc > 0.0 then
			local t = 0.0 - b - sqrt(disc)
			if t > 0.001 then best = t; hit = 0; cx = X0; cy = Y0; cz = Z0; cr = R0 end
		end
		local qx1 = ex - X1; local qy1 = ey - Y1; local qz1 = ez - Z1
		local b1 = qx1 * dx + qy1 * dy + qz1 * dz
		local disc1 = b1 * b1 - (qx1 * qx1 + qy1 * qy1 + qz1 * qz1 - R1 * R1)
		if disc1 > 0.0 then
			local t1 = 0.0 - b1 - sqrt(disc1)
			if t1 > 0.001 and t1 < best then best = t1; hit = 1; cx = X1; cy = Y1; cz = Z1; cr = R1 end
		end
		local qx2 = ex - X2; local qy2 = ey - Y2; local qz2 = ez - Z2
		local b2 = qx2 * dx + qy2 * dy + qz2 * dz
		local disc2 = b2 * b2 - (qx2 * qx2 + qy2 * qy2 + qz2 * qz2 - R2 * R2)
		if disc2 > 0.0 then
			local t2 = 0.0 - b2 - sqrt(disc2)
			if t2 > 0.001 and t2 < best then best = t2; hit = 2; cx = X2; cy = Y2; cz = Z2; cr = R2 end
		end
		if hit < 0 then
			grid[gi + x] = ch_trace(ex, ey, ez, dx, dy, dz, -1)
		else
			local hx = ex + dx * best
			local hy = ey + dy * best
			local hz = ez + dz * best
			local nx = (hx - cx) / cr
			local ny = (hy - cy) / cr
			local nz = (hz - cz) / cr
			local dn = 2.0 * (dx * nx + dy * ny + dz * nz)
			local mx = dx - nx * dn
			local my = dy - ny * dn
			local mz = dz - nz * dn
			local g = mx * LX + my * LY + mz * LZ
			if g > 0.975 then
				local gl = itrunc((g - 0.975) * 100.0)
				grid[gi + x] = GLINT + (gl > 2 and 2 or gl)
			else
				grid[gi + x] = NBASE * (hit + 1) + ch_trace(hx, hy, hz, mx, my, mz, hit)
			end
		end
	end
end

Chrome = class(Scene)

function Chrome:init()
	Scene.init(self)
	self.rad = {0.9, 0.7, 0.55}
	self.bx = {0.0, 0.0, 0.0}
	self.by = {0.0, 0.0, 0.0}
	self.bz = {0.0, 0.0, 0.0}
	self.nsky = 24
	self.nfog = 12
	self.floor0 = 0
	self.ball0 = 0
	self.nbase = 0
	self.glint = 0
	self.ex = 0.0
	self.ey = 1.6
	self.ez = 5.0
	self.fx = 0.0
	self.fy = 0.0
	self.fz = -1.0
	self.rx = 1.0
	self.rz = 0.0
	self.ux = 0.0
	self.uy = 1.0
	self.uz = 0.0
	self.tt = 0.0
end

function Chrome:build()
	self.name = "CHROME"
	self.handle = "juggler's grandchild . one ray per pixel, every core"
	local tints = {{1.0, 0.82, 0.45}, {0.86, 0.9, 1.0}, {1.0, 0.45, 0.55}}
	-- the class table once plain, then once per sphere tint
	for copy = 0, 3 do
		local tr, tg, tb = 1.0, 1.0, 1.0
		local k = 1.0
		if copy > 0 then
			local tc = tints[copy]
			tr = tc[1]; tg = tc[2]; tb = tc[3]
			k = 0.86
		end
		self:class_table(tr * k, tg * k, tb * k)
		if copy == 0 then self.nbase = self.pal:size() end
	end
	self.glint = self.pal:ramp(3, 255.0, 236.0, 190.0, 255.0, 255.0, 255.0)
end

function Chrome:class_table(tr, tg, tb)
	local pal, nsky, nfog = self.pal, self.nsky, self.nfog
	for s = 0, nsky - 1 do
		local f = s / (nsky - 1.0)
		pal:add((250.0 - 200.0 * f) * tr, (200.0 - 120.0 * f) * tg, (170.0 + 40.0 * f) * tb)
	end
	if self.floor0 == 0 then self.floor0 = pal:size() end
	local tiles = {{235.0, 226.0, 210.0}, {150.0, 36.0, 60.0}}
	for tile = 1, 2 do
		local tc = tiles[tile]
		local r0, g0, b0 = tc[1], tc[2], tc[3]
		for sh = 0, 1 do
			local dk = sh == 1 and 0.38 or 1.0
			for f2 = 0, nfog - 1 do
				local k = f2 / (nfog - 1.0)
				pal:add((r0 * dk * k + 250.0 * (1.0 - k)) * tr, (g0 * dk * k + 200.0 * (1.0 - k)) * tg, (b0 * dk * k + 170.0 * (1.0 - k)) * tb)
			end
		end
	end
	if self.ball0 == 0 then self.ball0 = pal:size() end
	local balls = {{255.0, 200.0, 110.0}, {210.0, 220.0, 240.0}, {255.0, 110.0, 130.0}}
	for b = 1, 3 do
		local bc = balls[b]
		for lv = 0, 7 do
			local k2 = 0.15 + 0.85 * lv / 7.0
			pal:add(bc[1] * k2 * tr, bc[2] * k2 * tg, bc[3] * k2 * tb)
		end
	end
end

function Chrome:update(t, dt)
	self.tt = t
	local bx, by, bz, rad = self.bx, self.by, self.bz, self.rad
	for i = 0, 2 do
		local bounce = sin(t * (1.9 + i * 0.37) + i * 2.1)
		bounce = bounce < 0.0 and -bounce or bounce
		local a = t * 0.45 + i * 2.0944
		bx[i + 1] = cos(a) * 1.45
		bz[i + 1] = sin(a) * 1.45
		by[i + 1] = rad[i + 1] + bounce * (1.1 - i * 0.2)
	end
	local ca = t * 0.18 + 0.4
	local ex = sin(ca) * 5.2
	local ez = cos(ca) * 5.2
	local ey = 1.5 + 0.35 * sin(t * 0.3)
	self.ex, self.ey, self.ez = ex, ey, ez
	local tx = 0.0 - ex
	local ty = 0.8 - ey
	local tz = 0.0 - ez
	local il = 1.0 / sqrt(tx * tx + ty * ty + tz * tz)
	local fx, fy, fz = tx * il, ty * il, tz * il
	self.fx, self.fy, self.fz = fx, fy, fz
	local rl = 1.0 / sqrt(fx * fx + fz * fz)
	local rx = 0.0 - fz * rl
	local rz = fx * rl
	self.rx, self.rz = rx, rz
	self.ux = 0.0 - rz * fy
	self.uy = rz * fx - rx * fz
	self.uz = rx * fy
end

function Chrome:render(grid)
	local bx, by, bz, rad = self.bx, self.by, self.bz, self.rad
	X0 = bx[1]; Y0 = by[1]; Z0 = bz[1]; R0 = rad[1]
	X1 = bx[2]; Y1 = by[2]; Z1 = bz[2]; R1 = rad[2]
	X2 = bx[3]; Y2 = by[3]; Z2 = bz[3]; R2 = rad[3]
	EX = self.ex; EY = self.ey; EZ = self.ez
	FX = self.fx; FY = self.fy; FZ = self.fz
	RX = self.rx; RZ = self.rz
	UX = self.ux; UY = self.uy; UZ = self.uz
	local ll = 1.0 / sqrt(0.5 * 0.5 + 0.75 * 0.75 + 0.43 * 0.43)
	LX = 0.5 * ll; LY = 0.75 * ll; LZ = 0.43 * ll
	CW = self.w
	CH = self.h
	FLOOR = self.floor0
	BALL = self.ball0
	NBASE = self.nbase
	GLINT = self.glint
	local w = self.w
	for y = 0, self.h - 1 do ch_row(y, grid, y * w + 1) end
end
