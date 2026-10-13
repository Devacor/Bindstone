-- STARFIELD: a warp run. A periodic value-noise nebula scrolled with wrap masks,
-- 700 stars in four color temperatures drawn as motion streaks, an ADDITIVE
-- palette (nebula level + star light). Flat parallel arrays on purpose.

local sin, cos = math.sin, math.cos

Starfield = class(Scene)

function Starfield:init()
	Scene.init(self)
	self.sx = {}
	self.sy = {}
	self.sz = {}
	self.temp = {}
	self.neb = {}
	self.nstars = 700
	self.nlev = 20
	self.per = 33
	self.tt = 0.0
	self.speed = 0.3
	self.roll = 0.0
	self.flash = 0.0
end

function Starfield:build()
	self.name = "STARFIELD"
	self.handle = "warp run . greets: parser crew"
	local pal, rng = self.pal, self.rng
	local nlev = self.nlev
	local stops = {{1.0, 1.0, 6.0}, {10.0, 6.0, 30.0}, {40.0, 12.0, 60.0}, {84.0, 22.0, 86.0}, {36.0, 70.0, 110.0}, {120.0, 150.0, 190.0}}
	local temps = {{150.0, 180.0, 255.0}, {255.0, 255.0, 255.0}, {255.0, 236.0, 170.0}, {255.0, 170.0, 110.0}}
	for l = 0, nlev - 1 do
		local f = l * 5.0 / (nlev - 1.0)
		local k = itrunc(f)
		k = k > 4 and 4 or k
		local u = f - k
		local a = stops[k + 1]
		local b = stops[k + 2]
		local ar, ag, ab = a[1], a[2], a[3]
		local nr = ar + (b[1] - ar) * u
		local ng = ag + (b[2] - ag) * u
		local nb = ab + (b[3] - ab) * u
		pal:add(nr, ng, nb)
		for tc = 1, 4 do
			local c = temps[tc]
			local cr, cg, cb = c[1], c[2], c[3]
			for lv = 0, 7 do
				local s = (lv + 1) / 8.0
				s = s * (0.6 + 0.4 * s)
				pal:add(nr + cr * s, ng + cg * s, nb + cb * s)
			end
		end
	end
	self:grow_nebula()
	local sx, sy, sz, temp = self.sx, self.sy, self.sz, self.temp
	for i = 1, self.nstars do
		sx[i] = (rng:next(2000) - 1000) * 0.0016
		sy[i] = (rng:next(2000) - 1000) * 0.0016
		sz[i] = 0.05 + rng:next(1000) * 0.0011
		local r = rng:next(100)
		temp[i] = r < 30 and 0 or (r < 70 and 1 or (r < 90 and 2 or 3))
	end
end

-- three octaves of periodic value noise, squared for wispy contrast
function Starfield:grow_nebula()
	local lat = {}
	local sd = self.seed & 65535
	for i = 0, 32 * 32 - 1 do
		local hs = (i * 73856093 + sd * 19349663) & 1048575
		lat[i + 1] = ((hs * 1274126177) >> 12) & 255
	end
	local nlev = self.nlev
	local neb = self.neb
	local nn = 0
	for y = 0, 127 do
		for x = 0, 127 do
			local acc = 0
			local cell = 32
			local n = 4
			local wgt = 8
			for o = 0, 2 do
				local ix = x // cell
				local iy = y // cell
				local fx = (x % cell) * 256 // cell
				local fy = (y % cell) * 256 // cell
				local ix1 = (ix + 1) % n
				local iy1 = (iy + 1) % n
				local base = o * 5
				local a = lat[((iy + base) & 31) * 32 + ((ix + base * 3) & 31) + 1]
				local b = lat[((iy + base) & 31) * 32 + ((ix1 + base * 3) & 31) + 1]
				local c = lat[((iy1 + base) & 31) * 32 + ((ix + base * 3) & 31) + 1]
				local d = lat[((iy1 + base) & 31) * 32 + ((ix1 + base * 3) & 31) + 1]
				local top = a + idiv((b - a) * fx, 256)
				local bot = c + idiv((d - c) * fx, 256)
				acc = acc + (top + idiv((bot - top) * fy, 256)) * wgt
				cell = cell // 2
				n = n * 2
				wgt = wgt // 2
			end
			local v = acc // 14
			v = v - 60
			v = v < 0 and 0 or v
			v = v * v // 110
			v = v * (nlev - 1) // 255
			nn = nn + 1
			neb[nn] = v > nlev - 1 and nlev - 1 or v
		end
	end
end

function Starfield:update(t, dt)
	self.tt = t
	-- cruise -> spool -> warp -> drop out
	local target = 0.3
	if t > 2.5 then target = 0.3 + (t - 2.5) * 0.5 end
	if t > 6.0 then target = 2.1 + (t - 6.0) * 1.1 end
	if t > 8.5 then target = 0.35 end
	target = target > 3.6 and 3.6 or target
	local mixv = dt * (t > 8.5 and 6.0 or 1.5)
	mixv = mixv > 1.0 and 1.0 or mixv
	local speed = self.speed + (target - self.speed) * mixv
	self.speed = speed
	self.flash = (t > 8.5 and t < 9.4) and 1.0 - (t - 8.5) / 0.9 or 0.0
	self.roll = self.roll + dt * (0.05 + speed * 0.07)
	local sx, sy, sz, rng = self.sx, self.sy, self.sz, self.rng
	for i = 1, self.nstars do
		local z = sz[i] - dt * speed * 0.9
		if z < 0.03 then
			sx[i] = (rng:next(2000) - 1000) * 0.0016
			sy[i] = (rng:next(2000) - 1000) * 0.0016
			z = 1.1 + rng:next(1000) * 0.0003
		end
		sz[i] = z
	end
end

function Starfield:render(grid)
	local ww = self.w
	local hh = self.h
	local pr = self.per
	local nb = self.neb
	local tt = self.tt
	local boost = itrunc(self.flash * 12.0)
	local ox = itrunc(tt * 3.0)
	local oy = itrunc(tt * 1.3)
	local nl1 = self.nlev - 1
	local idx = 1
	for y = 0, hh - 1 do
		local row = ((y + oy) & 127) * 128 + 1
		for x = 0, ww - 1 do
			local l = nb[row + ((x + ox) & 127)] + boost
			grid[idx] = (l > nl1 and nl1 or l) * pr
			idx = idx + 1
		end
	end
	local cx = ww * 0.5
	local cy = hh * 0.5
	local f = ww * 0.45
	local ca = cos(self.roll)
	local sa = sin(self.roll)
	local trail = 0.02 + self.speed * 0.05
	local xs, ys, zs, tp = self.sx, self.sy, self.sz, self.temp
	for i = 1, self.nstars do
		local z = zs[i]
		local rx = xs[i] * ca - ys[i] * sa
		local ry = xs[i] * sa + ys[i] * ca
		local hx = cx + rx / z * f
		local hy = cy + ry / z * f
		if not (hx < 0.0 or hx >= ww or hy < 0.0 or hy >= hh) then
			local zb = z + trail
			local tx = cx + rx / zb * f
			local ty = cy + ry / zb * f
			local lvl = itrunc(8.8 - z * 7.0)
			lvl = lvl < 0 and 0 or (lvl > 7 and 7 or lvl)
			local tbase = 1 + tp[i] * 8
			local ddx = hx - tx
			local ddy = hy - ty
			local adx = ddx < 0.0 and -ddx or ddx
			local ady = ddy < 0.0 and -ddy or ddy
			local n = itrunc(adx > ady and adx or ady)
			n = n > 48 and 48 or n
			for k = 0, n do
				local u = n > 0 and k * 1.0 / n or 1.0
				local px = itrunc(tx + ddx * u)
				local py = itrunc(ty + ddy * u)
				if not (px < 0 or px >= ww or py < 0 or py >= hh) then
					local sl = itrunc(lvl * (0.25 + 0.75 * u))
					local gi = py * ww + px + 1
					local cur = grid[gi]
					local had = cur % pr
					local bare = cur - had
					local had_l = had > 0 and (had - 1) % 8 or -1
					if sl > had_l then grid[gi] = bare + tbase + sl end
				end
			end
			-- near stars bloom: a dim cross around the head
			if lvl >= 6 then
				local hxi = itrunc(hx)
				local hyi = itrunc(hy)
				for q = 0, 3 do
					local gx = hxi + (q == 0 and 1 or (q == 1 and -1 or 0))
					local gy = hyi + (q == 2 and 1 or (q == 3 and -1 or 0))
					if not (gx < 0 or gx >= ww or gy < 0 or gy >= hh) then
						local gi2 = gy * ww + gx + 1
						local cur2 = grid[gi2]
						local had2 = cur2 % pr
						local hl2 = had2 > 0 and (had2 - 1) % 8 or -1
						if lvl - 4 > hl2 then grid[gi2] = cur2 - had2 + tbase + lvl - 4 end
					end
				end
			end
		end
	end
end
