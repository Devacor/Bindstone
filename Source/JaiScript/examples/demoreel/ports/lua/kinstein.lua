-- KINSTEIN 3D: a Wolfenstein-style grid raycaster with textured walls, cast
-- floors and ceilings, fog, sphere-lit billboard enemies occluded per column,
-- and a scripted camera that auto-fires at whatever sits in the crosshair.

local sin, cos, sqrt, floor = math.sin, math.cos, math.sqrt, math.floor

Kinstein = class(Scene)

function Kinstein:init()
	Scene.init(self)
	self.wmap = {}
	self.mw = 24
	self.mh = 24
	self.wpx = {}
	self.wpy = {}
	self.seglen = {}
	self.nwp = 6
	self.path_len = 0.0
	self.posx = 2.5
	self.posy = 2.5
	self.dirx = 0.0
	self.diry = 1.0
	self.planex = 0.0
	self.planey = 0.0
	self.tex = {}
	self.wall_base = 0
	self.banner_base = 0
	self.floor_base = 0
	self.ceil_base = 0
	self.sprite_base = 0
	self.tt = 0.0
	self.ex = {}
	self.ey = {}
	self.et = {}
	self.es = {}
	self.est = {}
	self.stx = {}
	self.sty = {}
	self.drawn = {}
	self.zcol = {}
	self.gun = {}
	self.ne = 11
	self.kills = 0
	self.cool = 0.6
	self.muzzle_t = 0.0
	self.tracer_t = 0.0
	self.hot_t = 0.0
	self.tr_x = 0
	self.tr_y = 0
end

function Kinstein:build()
	self.name = "KINSTEIN 3D"
	self.handle = "grublin keep . one ray per column, zero mercy"
	local w, pal = self.w, self.pal
	-- # stone  M moss  G gold pillar  B banner (rainbow-fire, animated)
	local rows = {
		"##################BBBB##",
		"#......##########......#",
		"#......................#",
		"#......MMMMMMMMMM......#",
		"#...G..##########......#",
		"##.##############......#",
		"##.##################.##",
		"#M.M#################.##",
		"##...B###############.##",
		"##.#################M.M#",
		"##.#######MMMMMM#####.##",
		"##.#######......#####.##",
		"##.#######............##",
		"##.#######...G..########",
		"##.#######......########",
		"##.#######MM.MMM########",
		"##.#########.###########",
		"##.#########.###########",
		"#......#####.###########",
		"#..G.G.#####.###########",
		"#......#####.###########",
		"#............###########",
		"#......#################",
		"##BBBB##################"
	}
	local wmap = self.wmap
	for y = 0, self.mh - 1 do
		local row = rows[y + 1]
		for x = 0, self.mw - 1 do
			local ch = row:sub(x + 1, x + 1)
			local v = 0
			if ch == "#" then v = 1
			elseif ch == "M" then v = 2
			elseif ch == "G" then v = 3
			elseif ch == "B" then v = 4 end
			wmap[#wmap + 1] = v
		end
	end
	local fogc = {9.0, 9.0, 16.0}
	local wall_cols = {
		{{40.0, 36.0, 44.0}, {88.0, 88.0, 100.0}, {104.0, 104.0, 118.0}, {120.0, 120.0, 136.0}, {138.0, 138.0, 152.0}, {156.0, 156.0, 170.0}, {186.0, 186.0, 200.0}, {70.0, 70.0, 82.0}},
		{{34.0, 36.0, 34.0}, {84.0, 90.0, 92.0}, {100.0, 106.0, 108.0}, {118.0, 124.0, 126.0}, {60.0, 118.0, 50.0}, {84.0, 150.0, 62.0}, {120.0, 190.0, 80.0}, {40.0, 86.0, 36.0}},
		{{90.0, 60.0, 20.0}, {140.0, 100.0, 36.0}, {176.0, 130.0, 50.0}, {206.0, 160.0, 64.0}, {232.0, 190.0, 90.0}, {252.0, 222.0, 130.0}, {255.0, 246.0, 200.0}, {118.0, 80.0, 28.0}}}
	self.wall_base = pal:size()
	for t = 1, 3 do
		local cols = wall_cols[t]
		for side = 0, 1 do
			for f = 0, 7 do
				for c = 1, 8 do
					local cc = cols[c]
					self:fogged(cc[1], cc[2], cc[3], f, side, fogc)
				end
			end
		end
	end
	self.banner_base = pal:size()
	for side2 = 0, 1 do
		for f2 = 0, 7 do
			for hu = 0, 3 do
				local rc = gfx:rainbow_rgb(hu * 1.5708)
				local rr, rg, rb = rc[1], rc[2], rc[3]
				self:fogged(40.0, 16.0, 60.0, f2, side2, fogc)
				self:fogged(rr * 0.45, rg * 0.45, rb * 0.45, f2, side2, fogc)
				self:fogged(rr, rg, rb, f2, side2, fogc)
				self:fogged(255.0, 196.0, 80.0, f2, side2, fogc)
			end
		end
	end
	local floor_cols = {{46.0, 38.0, 34.0}, {92.0, 80.0, 70.0}, {104.0, 92.0, 80.0}, {116.0, 102.0, 88.0}, {128.0, 112.0, 96.0}, {140.0, 124.0, 106.0}, {76.0, 66.0, 58.0}, {160.0, 142.0, 120.0}}
	self.floor_base = pal:size()
	for f3 = 0, 7 do
		for c3 = 1, 8 do local fc = floor_cols[c3]; self:fogged(fc[1], fc[2], fc[3], f3, 0, fogc) end
	end
	local ceil_cols = {{20.0, 12.0, 8.0}, {60.0, 38.0, 22.0}, {72.0, 46.0, 26.0}, {84.0, 54.0, 30.0}, {96.0, 62.0, 34.0}, {50.0, 32.0, 18.0}, {110.0, 72.0, 40.0}, {36.0, 24.0, 14.0}}
	self.ceil_base = pal:size()
	for f4 = 0, 7 do
		for c4 = 1, 8 do local kc = ceil_cols[c4]; self:fogged(kc[1] * 0.8, kc[2] * 0.8, kc[3] * 0.8, f4, 0, fogc) end
	end
	-- sprites + combat kit
	self.sprite_base = pal:size()
	pal:ramp(12, 10.0, 34.0, 12.0, 140.0, 240.0, 110.0)      -- +0..11 grublin
	pal:ramp(12, 36.0, 16.0, 10.0, 230.0, 140.0, 80.0)       -- +12..23 orglis
	pal:add(255, 255, 255)                                   -- +24 hit flash
	pal:add(250, 246, 220)                                   -- +25 eye white
	pal:add(20, 8, 8)                                        -- +26 pupil / mouth
	pal:ramp(4, 36.0, 38.0, 48.0, 150.0, 156.0, 176.0)       -- +27..30 gun
	pal:add(255, 250, 200)                                   -- +31 muzzle core
	pal:add(255, 196, 70)                                    -- +32 muzzle rim
	pal:add(255, 110, 30)                                    -- +33 muzzle tip
	for tr = 0, 5 do                                         -- +34..39 rainbow tracer
		local ct = gfx:rainbow_rgb(tr * 1.0471975512)
		pal:add(ct[1], ct[2], ct[3])
	end
	pal:add(130, 255, 150)                                   -- +40 crosshair
	pal:add(255, 84, 64)                                     -- +41 crosshair, target down
	pal:add(255, 226, 100)                                   -- +42 tally ink
	pal:add(255, 140, 50)                                    -- +43 tally ink low
	pal:add(8, 6, 10)                                        -- +44 tally shadow
	pal:add(170, 30, 30)                                     -- +45 grin
	pal:add(226, 170, 128)                                   -- +46 skin
	pal:add(150, 100, 72)                                    -- +47 skin shade
	self:grow_textures()
	self.gun = {"......dlhd......", "......dlmd......", "......dlmd......", ".....ddlmdd.....", ".....dlhmmd.....",
		"....ddlmmmdd....", "....dlhmmmmd....", "...kdlmmmmmdk...", "..kkdmmmmmmdkk..", ".kkkkdmmmmdkkkk.",
		"skkkkkddddkkkkks", "sskkkkkkkkkkkkss"}
	-- the flight loop: west corridor, bottom hall, mid room, east corridor
	self.wpx = {2.5, 2.5, 12.5, 12.5, 21.5, 21.5}
	self.wpy = {2.5, 21.5, 21.5, 12.5, 12.5, 2.5}
	local nwp, wpx, wpy, seglen = self.nwp, self.wpx, self.wpy, self.seglen
	for i = 0, nwp - 1 do
		local ddx = wpx[(i + 1) % nwp + 1] - wpx[i + 1]
		local ddy = wpy[(i + 1) % nwp + 1] - wpy[i + 1]
		seglen[i + 1] = sqrt(ddx * ddx + ddy * ddy)
		self.path_len = self.path_len + seglen[i + 1]
	end
	-- garrison, posted along the route (a couple wait around corners)
	self.ex = {2.5, 2.5, 2.5, 4.5, 5.5, 12.5, 12.5, 14.5, 18.5, 21.5, 20.5}
	self.ey = {6.5, 12.5, 16.5, 19.5, 21.5, 18.5, 13.5, 12.5, 12.5, 8.5, 2.5}
	self.et = {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1}
	for i2 = 1, self.ne do
		self.es[i2] = 0
		self.est[i2] = 0.0
		self.stx[i2] = 0.0
		self.sty[i2] = 0.0
		self.drawn[i2] = 0
	end
	self.zcol = make_fgrid(w)
	for i3 = 1, w do self.zcol[i3] = 99.0 end
end

-- one entry: a color fogged toward the keep's gloom at fog level f (7 = near)
function Kinstein:fogged(r, g, b, f, side, fogc)
	local k = (0.10 + 0.90 * f / 7.0) * (side == 1 and 0.72 or 1.0)
	local fr, fg, fb = fogc[1], fogc[2], fogc[3]
	self.pal:add(r * k + fr * (1.0 - k), g * k + fg * (1.0 - k), b * k + fb * (1.0 - k))
end

function Kinstein:hash2(a, b)
	local hs = (a * 73856093 + b * 19349663 + (self.seed & 255) * 83492791) & 1048575
	return ((hs * 1274126177) >> 12) & 1023
end

function Kinstein:grow_textures()
	-- 0 coursed stone, 1 mossy stone, 2 fluted gold, 3 flagstones, 4 oak planks
	local tex = self.tex
	local n = 0
	for t = 0, 4 do
		for v = 0, 15 do
			for u = 0, 15 do
				local c = 0
				if t <= 1 then
					local row = v // 4
					local bu = (u + (row % 2) * 4) % 8
					local bv = v % 4
					local brick = self:hash2((u + (row % 2) * 4) // 8 + row * 7, t)
					c = 1 + brick % 4
					if bv == 1 then c = c + 1 end
					if bv == 3 and c > 1 then c = c - 1 end
					if bu == 0 or bv == 0 then c = 0 end
					if t == 1 and c ~= 0 then
						local m = self:hash2(u * 3 + v, v * 5 + 11) % 100
						if m < v * 5 then c = 4 + m % 3 end
					end
					if t == 1 and c == 0 and self:hash2(u, v) % 3 == 0 then c = 7 end
				elseif t == 2 then
					local fl = u % 4
					c = fl == 0 and 7 or (fl == 1 and 5 or (fl == 2 and 4 or 2))
					if v % 8 == 0 then c = 6 end
					if v % 8 == 1 then c = 1 end
				elseif t == 3 then
					local su = (u + (v // 8) * 4) % 8
					local sv = v % 8
					local stone = self:hash2((u + (v // 8) * 4) // 8, v // 8 + 40)
					c = 1 + stone % 5
					if sv == 1 or su == 1 then c = 7 end
					if su == 0 or sv == 0 then c = 0 end
					if self:hash2(u + 50, v) % 11 == 0 and c ~= 0 then c = 6 end
				else
					local plank = v // 4
					local pv = v % 4
					c = 2 + (self:hash2(plank, 90) % 3)
					if (u + plank * 5) % 16 == 0 then c = 0 end
					if pv == 0 then c = 5 end
					if pv == 3 then c = 7 end
					if self:hash2(u, v + 90) % 13 == 0 then c = 6 end
				end
				n = n + 1
				tex[n] = c
			end
		end
	end
end

function Kinstein:path_at(s)
	local path_len, nwp, seglen, wpx, wpy = self.path_len, self.nwp, self.seglen, self.wpx, self.wpy
	s = s - floor(s / path_len) * path_len
	for i = 1, nwp do
		local L = seglen[i]
		if s <= L then
			local f = L > 0.0001 and s / L or 0.0
			local j = i % nwp + 1
			return wpx[i] + (wpx[j] - wpx[i]) * f, wpy[i] + (wpy[j] - wpy[i]) * f
		end
		s = s - L
	end
	return wpx[1], wpy[1]
end

-- DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
function Kinstein:los_clear(dx, dy, frac)
	local posx, posy, wmap, mw = self.posx, self.posy, self.wmap, self.mw
	local mx = itrunc(posx)
	local my = itrunc(posy)
	local adx = dx < 0.0 and -dx or dx
	local ady = dy < 0.0 and -dy or dy
	local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
	local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
	local stepx = dx < 0.0 and -1 or 1
	local stepy = dy < 0.0 and -1 or 1
	local sdx = dx < 0.0 and (posx - mx) * ddx or (mx + 1.0 - posx) * ddx
	local sdy = dy < 0.0 and (posy - my) * ddy or (my + 1.0 - posy) * ddy
	for g = 0, 63 do
		local trav = sdx < sdy and sdx or sdy
		if trav >= frac then return true end
		if sdx < sdy then sdx = sdx + ddx; mx = mx + stepx
		else sdy = sdy + ddy; my = my + stepy end
		local cv = wmap[my * mw + mx + 1]
		if cv > 0 then return false end
	end
	return false
end

function Kinstein:update(t, dt)
	self.tt = t
	local s = t * 2.4
	local px, py = self:path_at(s)
	local qx, qy = self:path_at(s + 1.7)   -- look-ahead steers the turns
	self.posx = px
	self.posy = py
	local dx = qx - px
	local dy = qy - py
	local dl = sqrt(dx * dx + dy * dy)
	if dl > 0.0001 then self.dirx = dx / dl; self.diry = dy / dl end
	local dirx, diry = self.dirx, self.diry
	local planex = 0.0 - diry * 0.66
	local planey = dirx * 0.66
	self.planex = planex
	self.planey = planey
	-- enemy state machine
	self.cool = self.cool - dt
	self.muzzle_t = self.muzzle_t - dt
	self.tracer_t = self.tracer_t - dt
	self.hot_t = self.hot_t - dt
	local es, est = self.es, self.est
	for i = 1, self.ne do
		local st = es[i]
		if st == 1 then est[i] = est[i] + dt; if est[i] > 0.14 then es[i] = 2; est[i] = 0.0 end
		elseif st == 2 then est[i] = est[i] + dt; if est[i] > 0.55 then es[i] = 3; est[i] = 0.0 end
		elseif st == 3 then est[i] = est[i] + dt; if est[i] > 16.0 then es[i] = 0; est[i] = 0.0 end end
	end
	-- auto-fire: nearest alive enemy in the crosshair cone with clear LOS
	if self.cool <= 0.0 then
		local posx, posy, ex, ey = self.posx, self.posy, self.ex, self.ey
		local invdet = 1.0 / (planex * diry - dirx * planey)
		local best = -1
		local besty = 99.0
		local bestx = 0.0
		for i2 = 1, self.ne do
			if es[i2] == 0 then
				local rx = ex[i2] - posx
				local ry = ey[i2] - posy
				local txx = invdet * (diry * rx - dirx * ry)
				local tyy = invdet * (planex * ry - planey * rx)
				if not (tyy < 1.0 or tyy > 4.2) then
					local aim = txx < 0.0 and -txx or txx
					if aim <= tyy * 0.14 and self:los_clear(rx, ry, 0.95) then
						if tyy < besty then best = i2; besty = tyy; bestx = txx end
					end
				end
			end
		end
		if best >= 1 then
			es[best] = 1
			est[best] = 0.0
			self.kills = self.kills + 1
			self.cool = 0.7
			self.muzzle_t = 0.12
			self.tracer_t = 0.14
			self.hot_t = 0.35
			self.tr_x = itrunc(self.w * 0.5 * (1.0 + bestx / besty))
			self.tr_y = self.h // 2 + itrunc(self.h * 0.10 / besty)
		end
	end
end

-- floor and ceiling, cast per pixel: each row below the horizon is a fixed
-- distance, stepped across the screen along the camera plane
function Kinstein:cast_planes(grid)
	local ww = self.w
	local hh = self.h
	local half = hh // 2
	local tx = self.tex
	local fb = self.floor_base
	local cb = self.ceil_base
	local dirx, diry, planex, planey, posx, posy = self.dirx, self.diry, self.planex, self.planey, self.posx, self.posy
	local rdx0 = dirx - planex
	local rdy0 = diry - planey
	local rdx1 = dirx + planex
	local rdy1 = diry + planey
	for y = half, hh - 1 do
		local p = y + 0.5 - hh * 0.5
		local rowd = hh * 0.5 / p
		local stx_ = rowd * (rdx1 - rdx0) / ww
		local sty_ = rowd * (rdy1 - rdy0) / ww
		local fx = posx + rowd * rdx0 + 64.0
		local fy = posy + rowd * rdy0 + 64.0
		local fog = itrunc(19.2 / (rowd + 1.4))
		fog = fog > 7 and 7 or fog
		local frow = fb + fog * 8
		local crow = cb + fog * 8
		local gf = y * ww + 1
		local gc = (hh - 1 - y) * ww + 1
		for x = 0, ww - 1 do
			local iu = itrunc(fx * 16.0)
			local iv = itrunc(fy * 16.0)
			local ti = (iv & 15) * 16 + (iu & 15) + 1
			grid[gf + x] = frow + tx[768 + ti]
			grid[gc + x] = crow + tx[1024 + ti]
			fx = fx + stx_
			fy = fy + sty_
		end
	end
end

-- one billboard: an ellipsoid body lit as a sphere per pixel, eyes with
-- pupils, a grin; walls occlude per column; hit-flash paints it white
function Kinstein:draw_blob(grid, i)
	local w, h, zcol, sprite_base = self.w, self.h, self.zcol, self.sprite_base
	local tyy = self.sty[i]
	local txx = self.stx[i]
	local scx = itrunc(w * 0.5 * (1.0 + txx / tyy))
	local typ = self.et[i]
	local size = typ == 1 and 1.1 or 0.78
	local sh = itrunc(h * size / tyy)
	sh = sh < 3 and 3 or sh
	sh = sh > 64 and 64 or sh
	local hwm = (sh + (typ == 1 and 6.0 or 2.0)) * 0.5
	local feet = h // 2 + itrunc(h * 0.5 / tyy)
	local top = feet - sh
	local wob = sin(self.tt * 5.0 + (i - 1) * 2.1) * 0.10
	local flash = self.es[i] == 1
	local body = sprite_base + typ * 12
	local fogk = 1.1 - tyy * 0.06
	fogk = fogk < 0.45 and 0.45 or (fogk > 1.0 and 1.0 or fogk)
	for dy = 0, sh - 1 do
		local rr = top + dy
		if rr >= 0 and rr < h then
			local v = (dy + 0.5) / sh * 2.0 - 1.0
			local vv = 1.0 - v * v
			if vv > 0.0 then
				local hwf = sqrt(vv) * hwm
				local hw = itrunc(hwf)
				local cxr = scx + itrunc(wob * v * hwm)
				local rowb = rr * w + 1
				for dx = 0 - hw, hw do
					local col = cxr + dx
					if col >= 0 and col < w and tyy < zcol[col + 1] then
						local pi = sprite_base + 24
						if not flash then
							local nx = dx / (hwm + 0.5)
							local q = 1.0 - nx * nx - v * v
							local nz = q > 0.0 and sqrt(q) or 0.0
							local li = -0.45 * nx - 0.55 * v + 0.70 * nz
							li = li < 0.0 and 0.0 or li
							local lv = itrunc((0.12 + 0.88 * li) * fogk * 11.99)
							pi = body + lv
							local ax = nx < 0.0 and -nx or nx
							if v > -0.5 and v < -0.12 and ax > 0.14 and ax < 0.52 then
								local ey2 = (v + 0.31) * 5.0
								local ex2 = (ax - 0.33) * 5.0
								if ex2 * ex2 + ey2 * ey2 < 1.0 then
									pi = ex2 * ex2 + ey2 * ey2 < 0.3 and sprite_base + 26 or sprite_base + 25
								end
							end
							if v > 0.18 and v < 0.34 and ax < 0.42 then
								pi = ((dx & 1) == 0 and v < 0.24) and sprite_base + 25 or sprite_base + 45
							end
						end
						grid[rowb + col] = pi
					end
				end
			end
		end
	end
end

function Kinstein:render(grid)
	self:cast_planes(grid)
	local ww = self.w
	local hh = self.h
	local wm = self.wmap
	local tx = self.tex
	local zc = self.zcol
	local wb = self.wall_base
	local bb = self.banner_base
	local mw = self.mw
	local tt = self.tt
	local dirx, diry, planex, planey, posx, posy = self.dirx, self.diry, self.planex, self.planey, self.posx, self.posy
	local tshift = itrunc(tt * 14.0)
	for x = 0, ww - 1 do
		local camx = 2.0 * x / (ww - 1.0) - 1.0
		local rdx = dirx + planex * camx
		local rdy = diry + planey * camx
		local mx = itrunc(posx)
		local my = itrunc(posy)
		local adx = rdx < 0.0 and -rdx or rdx
		local ady = rdy < 0.0 and -rdy or rdy
		local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
		local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
		local stepx = rdx < 0.0 and -1 or 1
		local stepy = rdy < 0.0 and -1 or 1
		local sdx = rdx < 0.0 and (posx - mx) * ddx or (mx + 1.0 - posx) * ddx
		local sdy = rdy < 0.0 and (posy - my) * ddy or (my + 1.0 - posy) * ddy
		local side = 0
		local hitv = 0
		local guard = 0
		while hitv == 0 and guard < 64 do
			if sdx < sdy then sdx = sdx + ddx; mx = mx + stepx; side = 0
			else sdy = sdy + ddy; my = my + stepy; side = 1 end
			hitv = wm[my * mw + mx + 1]
			guard = guard + 1
		end
		hitv = hitv == 0 and 1 or hitv
		local pdist = side == 0 and sdx - ddx or sdy - ddy   -- perpendicular: no fisheye
		pdist = pdist < 0.05 and 0.05 or pdist
		zc[x + 1] = pdist
		local wallx = side == 0 and posy + pdist * rdy or posx + pdist * rdx
		wallx = wallx - floor(wallx)
		local tu = itrunc(wallx * 16.0) & 15
		local lineh = hh / pdist
		local topf = (hh - lineh) * 0.5
		local y0 = itrunc(topf)
		local y1 = itrunc(topf + lineh)
		y0 = y0 < 0 and 0 or y0
		y1 = y1 > hh and hh or y1
		local fog = itrunc(19.2 / (pdist + 1.4))
		fog = fog > 7 and 7 or fog
		local tstep = 16.0 / lineh
		local tpos = (y0 + 0.5 - topf) * tstep
		local gi = y0 * ww + x + 1
		if hitv == 4 then
			local brow = bb + (side * 8 + fog) * 16
			-- rainbow fire: ragged flame tongues per texel column, hue bands scroll up
			local wave = (tu * 3 + tshift) % 12
			local flen = 6 + (wave < 6 and wave or 12 - wave)
			local bank = (((tu >> 2) + (tshift >> 3)) & 3) * 4
			for y = y0, y1 - 1 do
				local tv = itrunc(tpos)
				local up = 15 - (tv & 15)
				local heat = up > flen and 0 or (up * 3 > flen * 2 and 1 or (up * 3 > flen and 2 or 3))
				grid[gi] = brow + bank + heat
				tpos = tpos + tstep
				gi = gi + ww
			end
		else
			local wrow = wb + (((hitv - 1) * 2 + side) * 8 + fog) * 8
			local tbase = (hitv - 1) * 256 + tu + 1
			for y2 = y0, y1 - 1 do
				local tv2 = itrunc(tpos)
				grid[gi] = wrow + tx[tbase + (tv2 & 15) * 16]
				tpos = tpos + tstep
				gi = gi + ww
			end
		end
	end
	-- sprite pass: camera-space transforms, then paint far to near
	local ne, ex, ey, stx, sty, drawn, es = self.ne, self.ex, self.ey, self.stx, self.sty, self.drawn, self.es
	local invdet = 1.0 / (planex * diry - dirx * planey)
	for i = 1, ne do
		local rx = ex[i] - posx
		local ry = ey[i] - posy
		stx[i] = invdet * (diry * rx - dirx * ry)
		sty[i] = invdet * (planex * ry - planey * rx)
		drawn[i] = 0
	end
	for pass = 1, ne do
		local pick = -1
		local pty = -1.0
		for i2 = 1, ne do
			if drawn[i2] ~= 1 and es[i2] < 2 then
				local sv = sty[i2]
				if sv > 0.25 and sv > pty then pty = sv; pick = i2 end
			end
		end
		if pick < 0 then break end
		drawn[pick] = 1
		self:draw_blob(grid, pick)
	end
	-- gib scatter for the dying
	local w, h, sprite_base, est, et = self.w, self.h, self.sprite_base, self.est, self.et
	local flick = itrunc(tt * 10.0)
	for i3 = 1, ne do
		if es[i3] == 2 then
			local tyy = sty[i3]
			if tyy >= 0.25 then
				local prog = est[i3] * 1.9
				local scx = itrunc(w * 0.5 * (1.0 + stx[i3] / tyy))
				local scy = h // 2 + itrunc(h * 0.22 / tyy)
				local r = prog * (h * 0.45 / tyy)
				local body = sprite_base + et[i3] * 12 + 8
				for k = 0, 17 do
					local a = k * 0.349 + (i3 - 1)
					local gx = scx + itrunc(cos(a) * r * (1.0 + (k % 3) * 0.3))
					local gy = scy + itrunc(sin(a) * r + prog * prog * 10.0)
					if not (gx < 0 or gx >= w - 1 or gy < 1 or gy >= h - 1) and tyy < zc[gx + 1] then
						local gc = k % 4 == 0 and sprite_base + 45 or body - (k % 3) * 2
						grid[gy * w + gx + 1] = gc
						grid[gy * w + gx + 2] = gc
					end
				end
			end
		end
	end
	self:draw_hud(grid, flick)
end

function Kinstein:draw_hud(grid, flick)
	local w, h, sprite_base = self.w, self.h, self.sprite_base
	local gx0 = w // 2
	-- rainbow hitscan tracer from the muzzle to the target
	if self.tracer_t > 0.0 then
		local y0t = h - 22
		local tr_x, tr_y = self.tr_x, self.tr_y
		for k2 = 1, 27 do
			local xx = gx0 + idiv((tr_x - gx0) * k2, 28)
			local yy = y0t + idiv((tr_y - y0t) * k2, 28)
			if not (xx < 0 or xx >= w or yy < 1 or yy >= h) then
				grid[yy * w + xx + 1] = sprite_base + 34 + (k2 // 2 + flick) % 6
			end
		end
	end
	-- the gun, drawn at 2x: bobs with the walk, kicks on the shot
	local gun = self.gun
	local bob = itrunc(sin(self.tt * 7.0) * 2.5)
	local kick = self.muzzle_t > 0.0 and 4 or 0
	local gw = 16
	local gh = #gun
	local gxs = gx0 - gw + bob
	local gys = h - gh * 2 + 4 + kick + (bob < 0 and 0 - bob or bob) // 2
	for ry = 0, gh * 2 - 1 do
		local row = gun[ry // 2 + 1]
		local yy2 = gys + ry
		if yy2 >= 0 and yy2 < h then
			for rx = 0, gw * 2 - 1 do
				local ch = row:sub(rx // 2 + 1, rx // 2 + 1)
				if ch ~= "." then
					local c = ch == "d" and 27 or (ch == "m" and 28 or (ch == "l" and 29 or (ch == "h" and 30 or (ch == "k" and 46 or 47))))
					local xx2 = gxs + rx
					if xx2 >= 0 and xx2 < w then grid[yy2 * w + xx2 + 1] = sprite_base + c end
				end
			end
		end
	end
	if self.muzzle_t > 0.0 then
		local mx = gxs + 15
		local my = gys - 4
		for q = -5, 5 do
			local aq = q < 0 and 0 - q or q
			local c2 = aq <= 1 and 31 or (aq <= 3 and 32 or 33)
			for th = 0, 1 do
				if my + q >= 0 and my + q < h then grid[(my + q) * w + mx + th + 1] = sprite_base + c2 end
				if mx + q >= 0 and mx + q < w and my + th >= 0 then grid[(my + th) * w + mx + q + 1] = sprite_base + c2 end
			end
			if aq <= 3 and mx + q >= 0 and mx + q < w and my + q >= 0 and my + q < h then grid[(my + q) * w + mx + q + 1] = sprite_base + 32 end
			if aq <= 3 and mx - q >= 0 and mx - q < w and my + q >= 0 and my + q < h then grid[(my + q) * w + mx - q + 1] = sprite_base + 32 end
		end
	end
	local chc = self.hot_t > 0.0 and sprite_base + 41 or sprite_base + 40
	local cy = h // 2
	for d = 2, 3 do
		grid[cy * w + gx0 - d + 1] = chc
		grid[cy * w + gx0 + d + 1] = chc
		grid[(cy - d) * w + gx0 + 1] = chc
		grid[(cy + d) * w + gx0 + 1] = chc
	end
	local kills = self.kills
	local tally = "KILLS " .. (kills < 10 and "0" or "") .. kills
	gfx:draw_text(grid, w, h, tally, 2, 2, 1, 1, sprite_base + 42, 2, sprite_base + 44, 99)
end
