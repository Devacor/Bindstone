-- GLOBHUNTER (Lua 5.4 port of scenes/globhunter.jai): a Wolfenstein-style grid raycaster, one
-- DDA ray per screen column over an authored keep, textured walls, floor and ceiling cast per
-- pixel, a torch light map rebuilt every frame, billboard globs lit as spheres, goo in 3D, and a
-- scripted camera that hunts them on the beat through a boss fight and an escape out the breach.

local sqrt, sin, cos, atan2, floor = math.sqrt, math.sin, math.cos, math.atan, math.floor
local itrunc, ifloor, idiv = itrunc, ifloor, idiv

local BAYER4 = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}

GlobHunter = setmetatable({}, {__index = Scene})
GlobHunter.__index = GlobHunter

function GlobHunter.new()
	local self = Scene.init(setmetatable({}, GlobHunter))
	self.wmap = {}
	self.mw = 24
	self.mh = 27
	self.wpx = {}
	self.wpy = {}
	self.seglen = {}
	self.nwp = 6
	self.path_len = 0.0
	self.travel = 0.0      -- distance along the route: it crawls through each hit-stop
	self.posx = 2.5
	self.posy = 2.5
	self.dirx = 0.0
	self.diry = 1.0
	self.planex = 0.0
	self.planey = 0.0
	self.aim = 0.0         -- the eased swing of the view toward the current mark
	self.pitch = 0.0       -- the horizon's offset in pixels: walk bob and recoil
	self.tex = {}          -- 3 wall textures (16x32) + floor + ceiling (16x16) + their plain copies, texel indices 0..7
	self.wall_base = 0     -- ((type*2 + side)*8 + light)*8 + texel
	self.banner_base = 0   -- (side*8 + light)*16 + hue
	self.floor_base = 0    -- light*8 + texel
	self.ceil_base = 0
	self.sprite_base = 0
	self.tt = 0.0
	self.fr = 0
	-- the bestiary (parallel arrays): type 0 grublin, 1 orglis
	self.ex = {}
	self.ey = {}
	self.et = {}
	self.es = {}           -- 0 alive, 1 hit-flash, 2 gibbing, 3 down (respawns)
	self.est = {}
	self.stx = {}          -- per-frame camera-space transforms
	self.sty = {}
	self.drawn = {}
	self.zcol = {}         -- per-column wall depth from the last wall pass
	self.wall_top = {}     -- per column: the slice's first row and the row past its last
	self.wall_foot = {}
	self.wall_hit = {}     -- ((cell*2 + side)*16 + texel column)*8 + light
	self.ceil_lo = {}      -- per row: the first and last column whose slice starts there (its ceiling ends)
	self.ceil_hi = {}
	self.floor_lo = {}     -- and whose slice ends there (its floor begins)
	self.floor_hi = {}
	self.gun = {}          -- 16 texels a row: a sprite palette offset, 0 where the sprite is clear
	self.ne = 18           -- seventeen of the garrison (three wait in ambush for the escape), and the boss in the last slot
	self.boss_flash = 0.0  -- the boss shrugs off each hit with a flash until bar nine
	self.kills = 0
	self.cool = 0.6
	self.last_step = -1    -- the last beat (or eighth) the gun was allowed to fire on
	self.muzzle_t = 0.0
	self.tracer_t = 0.0
	self.hot_t = 0.0
	self.kick_t = 0.0
	self.stop_t = 0.0      -- the hit-stop
	self.last_kill = -9.0
	self.combo = 0
	self.call_t = 0.0      -- the callout's life
	self.callout = ""
	self.tr_x = 0
	self.tr_y = 0
	-- torches: positions; the light map's steady sum of them, and which one rules each cell
	self.tox = {}
	self.toy = {}
	self.still = {}
	self.owner = {}
	self.lmap = {}         -- two cells per map cell each way, light levels 0..5
	self.lw = 48
	self.lh = 54
	self.vault = 2.0       -- the walls' height; the eye rides at 0.5
	-- goo: a pool of particles in world space (z up, 0 = floor, 1 = ceiling)
	self.gx = {}
	self.gy = {}
	self.gz = {}
	self.gvx = {}
	self.gvy = {}
	self.gvz = {}
	self.glife = {}
	self.gtype = {}
	self.ngoo = 220
	self.goo_next = 0
	self.vigx = {}         -- the vignette: light levels lost toward the edges
	self.vigy = {}
	self.rubble_base = 0   -- falling stone, 12 levels
	self.breach_x = 23     -- the wall the escape blows open, east at the end of the mid hall
	self.breach_y = 13
	self.last_snare = 0.0
	return self
end

function GlobHunter:build()
	self.name = "GLOBHUNTER"
	self.handle = "grublin keep . torchlight, one ray per column, zero mercy"
	self.replay = 26.0
	local w, h = self.w, self.h
	local pal = self.pal
	-- # stone  M moss  G gold pillar  B banner (rainbow-fire, animated)
	local rows = {
		"##################BBBB##",
		"#.......#########......#",
		"#......................#",
		"#......................#",
		"#.....G................#",
		"#.......MMMMMMMMM......#",
		"##...###############...#",
		"#M...M##############...#",
		"##...B##############...#",
		"##...##############M...M",
		"##...####MMMMMMMMM##...#",
		"##...####.........##...#",
		"##...####..............#",
		"##...####..............#",
		"##...####..............#",
		"##...####.........######",
		"##...####MMM...MMM######",
		"##...####.........######",
		"##...####.........######",
		"##...####.........######",
		"#.......#.........######",
		"#.................######",
		"#.................######",
		"#.................######",
		"#.G..G..#.........######",
		"#.......#.........######",
		"##BBBB##################"
	}
	local mw, mh = self.mw, self.mh
	local wmap = self.wmap
	for y = 0, mh - 1 do
		local row = rows[y + 1]
		for x = 0, mw - 1 do
			local ch = row:sub(x + 1, x + 1)
			local v = 0
			if ch == "#" then v = 1
			elseif ch == "M" then v = 2
			elseif ch == "G" then v = 3
			elseif ch == "B" then v = 4 end
			wmap[#wmap + 1] = v
		end
	end
	wmap[self.breach_y * mw + self.breach_x + 1] = 5
	local fogc = {5.0, 7.0, 18.0}
	local wall_cols = {
		{{40.0, 36.0, 44.0}, {88.0, 88.0, 100.0}, {104.0, 104.0, 118.0}, {120.0, 120.0, 136.0}, {138.0, 138.0, 152.0}, {156.0, 156.0, 170.0}, {186.0, 186.0, 200.0}, {70.0, 70.0, 82.0}},
		{{34.0, 36.0, 34.0}, {84.0, 90.0, 92.0}, {100.0, 106.0, 108.0}, {118.0, 124.0, 126.0}, {60.0, 118.0, 50.0}, {84.0, 150.0, 62.0}, {120.0, 190.0, 80.0}, {40.0, 86.0, 36.0}},
		{{90.0, 60.0, 20.0}, {140.0, 100.0, 36.0}, {176.0, 130.0, 50.0}, {206.0, 160.0, 64.0}, {232.0, 190.0, 90.0}, {252.0, 222.0, 130.0}, {255.0, 246.0, 200.0}, {118.0, 80.0, 28.0}}}
	self.wall_base = pal:size()
	for t = 0, 2 do
		local cols = wall_cols[t + 1]
		for side = 0, 1 do
			for f = 0, 7 do
				for c = 0, 7 do
					local cc = cols[c + 1]
					self:lit(cc[1], cc[2], cc[3], f, side, fogc)
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
				local f3 = f2 < 3 and 3 or f2   -- the banner's fire glows in the dark
				self:lit(40.0, 16.0, 60.0, f3, side2, fogc)
				self:lit(rr * 0.45, rg * 0.45, rb * 0.45, f3, side2, fogc)
				self:lit(rr, rg, rb, f3, side2, fogc)
				self:lit(255.0, 196.0, 80.0, f3, side2, fogc)
			end
		end
	end
	local floor_cols = {{46.0, 38.0, 34.0}, {92.0, 80.0, 70.0}, {104.0, 92.0, 80.0}, {116.0, 102.0, 88.0}, {128.0, 112.0, 96.0}, {140.0, 124.0, 106.0}, {76.0, 66.0, 58.0}, {160.0, 142.0, 120.0}}
	self.floor_base = pal:size()
	for f4 = 0, 7 do
		for c3 = 0, 7 do
			local fc = floor_cols[c3 + 1]
			self:lit(fc[1], fc[2], fc[3], f4, 0, fogc)
		end
	end
	local ceil_cols = {{20.0, 12.0, 8.0}, {60.0, 38.0, 22.0}, {72.0, 46.0, 26.0}, {84.0, 54.0, 30.0}, {96.0, 62.0, 34.0}, {50.0, 32.0, 18.0}, {110.0, 72.0, 40.0}, {36.0, 24.0, 14.0}}
	self.ceil_base = pal:size()
	for f5 = 0, 7 do
		for c4 = 0, 7 do
			local kc = ceil_cols[c4 + 1]
			self:lit(kc[1] * 0.8, kc[2] * 0.8, kc[3] * 0.8, f5, 0, fogc)
		end
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
	pal:add(255, 60, 40)                                     -- +48 glowing eye
	pal:ramp(4, 120.0, 255.0, 120.0, 230.0, 255.0, 200.0)    -- +49..52 callout ink, grublin goo glint
	pal:ramp(4, 255.0, 120.0, 220.0, 255.0, 240.0, 255.0)    -- +53..56 combo ink
	self.rubble_base = pal:size()
	pal:ramp(12, 22.0, 20.0, 28.0, 196.0, 186.0, 172.0)
	self:grow_textures()
	local gun_rows = {"......dlhd......", "......dlmd......", "......dlmd......", ".....ddlmdd.....", ".....dlhmmd.....",
		"....ddlmmmdd....", "....dlhmmmmd....", "...kdlmmmmmdk...", "..kkdmmmmmmdkk..", ".kkkkdmmmmdkkkk.",
		"skkkkkddddkkkkks", "sskkkkkkkkkkkkss"}
	local gun = self.gun
	for gr = 0, #gun_rows - 1 do
		local gun_row = gun_rows[gr + 1]
		for gc = 0, 15 do
			local ch2 = gun_row:sub(gc + 1, gc + 1)
			gun[#gun + 1] = ch2 == "." and 0 or (ch2 == "d" and 27 or (ch2 == "m" and 28 or (ch2 == "l" and 29 or (ch2 == "h" and 30 or (ch2 == "k" and 46 or 47)))))
		end
	end
	-- the flight loop down the middle of each hall: west hall, south room and arena, mid room, east hall
	self.wpx = {3.5, 3.5, 13.5, 13.5, 22.5, 22.5}
	self.wpy = {3.5, 22.5, 22.5, 13.5, 13.5, 3.5}
	local wpx, wpy, seglen, nwp = self.wpx, self.wpy, self.seglen, self.nwp
	for i = 0, nwp - 1 do
		local ddx = wpx[(i + 1) % nwp + 1] - wpx[i + 1]
		local ddy = wpy[(i + 1) % nwp + 1] - wpy[i + 1]
		seglen[#seglen + 1] = sqrt(ddx * ddx + ddy * ddy)
		self.path_len = self.path_len + seglen[i + 1]
	end
	-- garrison along the route (three hold the south room for the frenzy, three wait along the escape);
	-- every post stands farther from the nearest wall than its kind's reach, the boss in the arena's open floor
	self.ex = {3.5, 3.0, 4.0, 3.0, 7.0, 15.5, 13.5, 14.5, 19.0, 21.5, 20.5, 10.0, 11.5, 12.0, 11.0, 17.5, 21.0, 13.5}
	self.ey = {7.5, 13.5, 17.5, 21.2, 21.0, 21.0, 14.0, 12.5, 13.5, 8.5, 3.0, 22.5, 23.5, 21.8, 12.5, 13.5, 13.0, 18.5}
	self.et = {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1}
	local ne = self.ne
	local es, est = self.es, self.est
	for i2 = 1, ne do
		es[i2] = 0
		est[i2] = 0.0
		self.stx[i2] = 0.0
		self.sty[i2] = 0.0
		self.drawn[i2] = 0
	end
	es[ne] = 3   -- the boss waits for bar seven
	for a = ne - 4, ne - 2 do
		-- the ambush rises (a downed mark stands again after 16 s) as the escape begins
		es[a + 1] = 3
		est[a + 1] = 16.0 - MUSIC_BAR * 10.0
	end
	local zcol = gfx:make_fgrid(w)
	for i3 = 1, w do zcol[i3] = 99.0 end
	self.zcol = zcol
	self.wall_top = gfx:make_grid(w)
	self.wall_foot = gfx:make_grid(w)
	self.wall_hit = gfx:make_grid(w)
	self.ceil_lo = gfx:make_grid(h + 1)
	self.ceil_hi = gfx:make_grid(h + 1)
	self.floor_lo = gfx:make_grid(h + 1)
	self.floor_hi = gfx:make_grid(h + 1)
	-- torches hang beside the walls along the route; each keeps its falloff over the light map
	local spots = {{1.2, 3.5}, {7.5, 1.3}, {2.2, 9.5}, {4.8, 15.5}, {1.2, 22.5}, {7.5, 20.3}, {9.3, 19.5},
		{17.7, 22.5}, {11.4, 17.3}, {15.6, 17.3}, {9.3, 13.5}, {15.5, 11.2}, {20.5, 14.8}, {22.8, 7.5}, {22.8, 2.5}, {17.2, 1.4}}
	local tox, toy = self.tox, self.toy
	for k = 1, #spots do
		local sp = spots[k]
		local px = sp[1]
		local py = sp[2]
		if wmap[itrunc(py) * mw + itrunc(px) + 1] == 0 then
			tox[#tox + 1] = px
			toy[#toy + 1] = py
		end
	end
	local lw, lh = self.lw, self.lh
	local still, owner = self.still, self.owner
	local best = {}
	for c5 = 1, lw * lh do
		still[c5] = 0.0
		owner[c5] = 0
		best[c5] = 0.0
	end
	for k3 = 0, #tox - 1 do
		for c6 = 0, lw * lh - 1 do
			local lx = (c6 % lw + 0.5) * 0.5 - tox[k3 + 1]
			local ly = (c6 // lw + 0.5) * 0.5 - toy[k3 + 1]
			local f = 1.0 / (1.0 + (lx * lx + ly * ly) * 1.7)
			still[c6 + 1] = still[c6 + 1] + f * 5.6 * 0.82
			if f > best[c6 + 1] then
				best[c6 + 1] = f
				owner[c6 + 1] = k3
			end
		end
	end
	self.lmap = gfx:make_grid(lw * lh)
	local gx, gy, gz, gvx, gvy, gvz, glife, gtype = self.gx, self.gy, self.gz, self.gvx, self.gvy, self.gvz, self.glife, self.gtype
	for g = 1, self.ngoo do
		gx[g] = 0.0
		gy[g] = 0.0
		gz[g] = 0.0
		gvx[g] = 0.0
		gvy[g] = 0.0
		gvz[g] = 0.0
		glife[g] = 0.0
		gtype[g] = 0
	end
	local vigx = gfx:make_grid(w)
	local vigy = gfx:make_grid(h)
	for vx = 0, w - 1 do
		local e = 2.0 * vx / (w - 1.0) - 1.0
		vigx[vx + 1] = itrunc(e * e * e * e * 2.4)
	end
	for vy = 0, h - 1 do
		local e2 = 2.0 * vy / (h - 1.0) - 1.0
		vigy[vy + 1] = itrunc(e2 * e2 * e2 * e2 * 1.6)
	end
	self.vigx = vigx
	self.vigy = vigy
end

-- one entry: a colour at light level f (0 black gloom .. 7 full torchlight):
-- the dark sinks into a cold blue, the lit end warms toward the fire
function GlobHunter:lit(r, g, b, f, side, fogc)
	local k = (0.07 + 0.93 * f / 7.0) * (side == 1 and 0.72 or 1.0)
	local warm = f >= 5 and (f - 4) / 3.0 or 0.0
	local fr2, fg, fb = fogc[1], fogc[2], fogc[3]
	local cr = r * (1.0 + 0.32 * warm)
	local cg = g * (1.0 + 0.06 * warm)
	local cb = b * (1.0 - 0.28 * warm)
	self.pal:add(cr * k + fr2 * (1.0 - k), cg * k + fg * (1.0 - k), cb * k + fb * (1.0 - k))
end

function GlobHunter:hash2(a, b)
	local hs = (a * 73856093 + b * 19349663 + (self.seed & 255) * 83492791) & 1048575
	return ((hs * 1274126177) >> 12) & 1023
end

function GlobHunter:grow_textures()
	local tex = self.tex
	-- 0 coursed stone, 1 stone mossy toward its foot, 2 fluted gold (16 wide, the vault's 32 tall), 3 flagstones, 4 oak planks
	for t = 0, 4 do
		local tall = t < 3 and 32 or 16
		for v = 0, tall - 1 do
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
						if m < (v - 13) * 5 then c = 4 + m % 3 end
					end
					if t == 1 and c == 0 and v > 12 and self:hash2(u, v) % 3 == 0 then c = 7 end
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
				tex[#tex + 1] = c
			end
		end
	end
	-- plain copies of the floor and the ceiling for their far rows: the joints over one shade
	for i = 1536, 2047 do
		local texel = tex[i + 1]
		local joint = texel == 0 or (i >= 1792 and texel == 7)
		tex[#tex + 1] = joint and texel or 3
	end
end

function GlobHunter:path_at(s)
	local path_len = self.path_len
	s = s - floor(s / path_len) * path_len
	local wpx, wpy, seglen, nwp = self.wpx, self.wpy, self.seglen, self.nwp
	for i = 0, nwp - 1 do
		local L = seglen[i + 1]
		if s <= L then
			local f = L > 0.0001 and s / L or 0.0
			local j = (i + 1) % nwp
			return {wpx[i + 1] + (wpx[j + 1] - wpx[i + 1]) * f, wpy[i + 1] + (wpy[j + 1] - wpy[i + 1]) * f}
		end
		s = s - L
	end
	return {wpx[1], wpy[1]}
end

-- DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
function GlobHunter:los_clear(dx, dy, frac)
	local posx, posy = self.posx, self.posy
	local wmap, mw = self.wmap, self.mw
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
		if sdx < sdy then
			sdx = sdx + ddx
			mx = mx + stepx
		else
			sdy = sdy + ddy
			my = my + stepy
		end
		local cv = wmap[my * mw + mx + 1]
		if cv > 0 then return false end
	end
	return false
end

function GlobHunter:length_bars() return 13 end
function GlobHunter:hit_times() return {MUSIC_BAR * 3.0, MUSIC_BAR * 5.0, MUSIC_BAR * 7.0, MUSIC_BAR * 9.0, MUSIC_BAR * 10.0, MUSIC_BAR * 12.0} end
function GlobHunter:hit_levels() return {1, 2, 1, 2, 1, 2} end

-- the route's distance from which the walk faces the arena floor where the boss rises
function GlobHunter:arena_stand() return 19.0 + 9.8 end

-- the route's distance at which the escape reaches the breach, facing it
function GlobHunter:breach_stop() return 19.0 + 10.0 + 9.0 + 8.3 end

function GlobHunter:update(t, dt)
	self.tt = t
	self.fr = self.fr + 1
	local fr = self.fr
	local cue = self.cue
	local ne = self.ne
	local aftermath = t > MUSIC_BAR * 9.0 and t < MUSIC_BAR * 10.0
	local escape = t >= MUSIC_BAR * 10.0 and t < MUSIC_BAR * 12.0
	local out = t >= MUSIC_BAR * 12.0
	local fight = t > MUSIC_BAR * 7.0 and t < MUSIC_BAR * 9.0
	local bullet = self:slow_mo(t)
	local path_len = self.path_len
	local lap = self.travel - floor(self.travel / path_len) * path_len
	local pace = self.stop_t > 0.0 and 0.18 or (aftermath and 0.4 or (fight and 0.16 or 1.0))
	if escape then
		-- the run for the breach: always just fast enough to reach it on bar twelve
		local want = (self:breach_stop() - lap) / (MUSIC_BAR * 12.0 - t + 0.05) / 2.4
		want = want < 0.0 and 0.0 or (want > 3.0 and 3.0 or want)
		pace = self.stop_t > 0.0 and want * 0.5 or want
	end
	if t >= MUSIC_BAR * 6.0 and t < MUSIC_BAR * 7.0 then
		-- the walk into the arena: always just fast enough to face the boss's floor on bar seven
		local gait = (self:arena_stand() - lap) / (MUSIC_BAR * 7.0 - t + 0.05) / 2.4
		gait = gait < 0.3 and 0.3 or (gait > 2.0 and 2.0 or gait)
		pace = self.stop_t > 0.0 and gait * 0.5 or gait
	end
	if out then pace = 0.0 end
	pace = pace * bullet
	self.travel = self.travel + dt * 2.4 * pace
	local travel = self.travel
	lap = travel - floor(travel / path_len) * path_len
	local look = 1.7
	if escape or out then
		local to_corner = 19.0 + 10.0 + 9.0 + 9.0 - lap
		look = (to_corner < look and to_corner > 0.0) and to_corner or look
	end
	local p = self:path_at(travel)
	local q = self:path_at(travel + look)   -- look-ahead steers the turns
	local posx = p[1]
	local posy = p[2]
	if out then
		-- into the light: the last bar walks through the breach
		local walk = (t - MUSIC_BAR * 12.0) / MUSIC_BAR
		walk = walk > 1.0 and 1.0 or walk
		posx = posx + walk * (2.0 - walk) * 0.92
	end
	self.posx = posx
	self.posy = posy
	local bx = q[1] - posx
	local by = q[2] - posy
	local bl = sqrt(bx * bx + by * by)
	if out then
		bx = 1.0
		by = 0.0
		bl = 1.0
	end
	if bl > 0.0001 then
		bx = bx / bl
		by = by / bl
	else
		bx = self.dirx
		by = self.diry
	end
	-- the hunt: swing toward the nearest live mark in clear sight ahead
	local es, est, ex, ey = self.es, self.est, self.ex, self.ey
	local want = 0.0
	local near = 5.5
	for m = 1, ne do
		if es[m] == 0 then
			local mx = ex[m] - posx
			local my = ey[m] - posy
			local md = sqrt(mx * mx + my * my)
			if not (md > near or md < 0.6) then
				local ang = atan2(bx * my - by * mx, bx * mx + by * my)
				if not (ang > 0.75 or ang < -0.75) and self:los_clear(mx, my, 0.95) then
					near = md
					want = ang
				end
			end
		end
	end
	if out then want = 0.0 end
	local ease = dt * 7.0
	ease = ease > 1.0 and 1.0 or ease
	self.aim = self.aim + (want - self.aim) * ease
	-- the quake: each kill shakes the view, the big hit hardest, and the keep rumbles as it falls
	local kick_now = cue ~= nil and cue:kick_env(t) or 0.0
	local quake = (self.hot_t > 0.0 and self.hot_t * 0.16 or 0.0) + (cue ~= nil and cue:hit(t) * 0.05 or 0.0)
	if escape then quake = quake + (0.012 + kick_now * 0.03) end
	local view = self.aim + quake * sin(fr * 2.7)
	local ca = cos(view)
	local sa = sin(view)
	local dirx = bx * ca - by * sa
	local diry = bx * sa + by * ca
	self.dirx = dirx
	self.diry = diry
	local planex = 0.0 - diry * 0.66
	local planey = dirx * 0.66
	self.planex = planex
	self.planey = planey
	-- timers and the enemy state machine
	self.cool = self.cool - dt
	self.muzzle_t = self.muzzle_t - dt
	self.tracer_t = self.tracer_t - dt
	self.hot_t = self.hot_t - dt
	self.kick_t = self.kick_t - dt
	self.stop_t = self.stop_t - dt
	self.call_t = self.call_t - dt
	self.boss_flash = self.boss_flash - dt
	for i = 1, ne do
		local st = es[i]
		if st == 1 then
			est[i] = est[i] + dt
			if est[i] > 0.14 then es[i] = 2; est[i] = 0.0 end
		elseif st == 2 then
			est[i] = est[i] + dt
			if est[i] > 0.55 then es[i] = 3; est[i] = 0.0 end
		elseif st == 3 then
			est[i] = est[i] + dt
			if est[i] > 16.0 then es[i] = 0; est[i] = 0.0 end
		end
	end
	-- auto-fire on the beat: nearest alive enemy in the crosshair cone with clear LOS
	local e = cue ~= nil and cue:energy(t) or 0.6
	local eighths = e > 0.92 or (t > MUSIC_BAR * 5.0 and t < MUSIC_BAR * 6.0) or escape
	-- the big hit: the hall floods with light and the frenzy is called
	if t >= MUSIC_BAR * 5.0 and t - dt < MUSIC_BAR * 5.0 then
		self.callout = "FRENZY"
		self.call_t = 1.2
	end
	local step = ifloor(t / (eighths and MUSIC_BEAT * 0.5 or MUSIC_BEAT))
	local on_step = step ~= self.last_step
	self.last_step = step
	if t >= MUSIC_BAR * 10.0 and t - dt < MUSIC_BAR * 10.0 then
		self.callout = "RUN"
		self.call_t = 1.3
		self.hot_t = 0.5
	end
	if t >= MUSIC_BAR * 12.0 and t - dt < MUSIC_BAR * 12.0 then self:blow_breach() end
	if escape then self:falling_stone(t) end
	if self.cool <= 0.0 and on_step and not aftermath and not out and t > MUSIC_BAR then
		local invdet = 1.0 / (planex * diry - dirx * planey)
		local best = -1
		local besty = 99.0
		local bestx = 0.0
		for i2 = 1, ne do
			if es[i2] == 0 then
				local rx = ex[i2] - posx
				local ry = ey[i2] - posy
				local txx = invdet * (diry * rx - dirx * ry)
				local tyy = invdet * (planex * ry - planey * rx)
				if not (tyy < 1.0 or tyy > 4.6) then
					local off = txx < 0.0 and -txx or txx
					if not (off > tyy * 0.12) and self:los_clear(rx, ry, 0.95) then
						if tyy < besty then
							best = i2 - 1
							besty = tyy
							bestx = txx
						end
					end
				end
			end
		end
		if best >= 0 then self:fire(best, bestx, besty, t) end
	end
	-- the boss rises from the arena's open floor on bar seven
	local boss = ne - 1
	if t >= MUSIC_BAR * 7.0 and t - dt < MUSIC_BAR * 7.0 then
		es[boss + 1] = 0
		est[boss + 1] = 0.0
		self.callout = "BOSS GLOB"
		self.call_t = 1.6
		-- the garrison scatters before it
		for m2 = 1, boss do
			local ddx2 = ex[m2] - posx
			local ddy2 = ey[m2] - posy
			if ddx2 * ddx2 + ddy2 * ddy2 < 64.0 then
				es[m2] = 3
				est[m2] = 0.0
			end
		end
	end
	-- bar nine's downbeat: the boss bursts
	if t >= MUSIC_BAR * 9.0 and t - dt < MUSIC_BAR * 9.0 and es[boss + 1] == 0 then
		local invdet2 = 1.0 / (planex * diry - dirx * planey)
		local rbx = ex[boss + 1] - posx
		local rby = ey[boss + 1] - posy
		local bsx = invdet2 * (diry * rbx - dirx * rby)
		local bsy = invdet2 * (planex * rby - planey * rbx)
		self:fire(boss, bsx, bsy > 0.3 and bsy or 0.3, t)
	end
	self:move_goo(dt * bullet)
	-- the flicker is redrawn at 15 Hz, and every frame while the muzzle burns or the kick lands
	local beat_light = cue ~= nil and cue:kick_env(t) + cue:hit(t) or 0.0
	if (fr & 1) == 0 or self.muzzle_t > -0.05 or beat_light > 0.2 or escape or out then self:light_up() end
	-- the view: a footfall on each beat (each eighth on the run), the recoil's kick, a shudder
	-- while the muzzle burns, and the falling keep's tremor
	local h = self.h
	local stride = cue ~= nil and cue:beat_phase(t) or travel * 0.83
	if escape then stride = stride * 2.0 - floor(stride * 2.0) end
	local bob = sin(stride * 3.1415927) * h * (escape and 0.022 or 0.014) * ((aftermath or out) and 0.4 or 1.0)
	local kick = self.kick_t > 0.0 and h * 0.05 * self.kick_t / 0.2 or 0.0
	local shud = self.muzzle_t > 0.0 and ((fr * 7) % 3 - 1) * h * 0.012 or 0.0
	local tremor = escape and sin(fr * 5.3) * h * (0.006 + kick_now * 0.02) or 0.0
	self.pitch = bob + kick + shud + tremor
end

-- the escape: on each snare stones shake loose from the vault ahead and fall
function GlobHunter:falling_stone(t)
	local cue = self.cue
	local sn = cue ~= nil and cue:snare_env(t) or 0.0
	local onset = sn > self.last_snare + 0.25
	self.last_snare = sn
	if not onset then return end
	local rng = self.rng
	local ngoo = self.ngoo
	local posx, posy, dirx, diry = self.posx, self.posy, self.dirx, self.diry
	local wmap, mw = self.wmap, self.mw
	local gx, gy, gz, gvx, gvy, gvz, glife, gtype = self.gx, self.gy, self.gz, self.gvx, self.gvy, self.gvz, self.glife, self.gtype
	for k = 0, 13 do
		local g = self.goo_next + 1
		self.goo_next = (self.goo_next + 1) % ngoo
		local ahead = 1.2 + rng:nextf() * 3.2
		local across = (rng:nextf() - 0.5) * 2.6
		local sx = posx + dirx * ahead - diry * across
		local sy = posy + diry * ahead + dirx * across
		if wmap[itrunc(sy) * mw + itrunc(sx) + 1] == 0 then
			gx[g] = sx
			gy[g] = sy
			gz[g] = self.vault - 0.05
			gvx[g] = (rng:nextf() - 0.5) * 0.6
			gvy[g] = (rng:nextf() - 0.5) * 0.6
			gvz[g] = 0.0 - rng:nextf() * 1.5
			glife[g] = 1.4 + rng:nextf() * 0.8
			gtype[g] = 2
		end
	end
end

-- bar twelve: the east wall bursts, stone flies at the camera and daylight floods in
function GlobHunter:blow_breach()
	self.callout = "KEEP CLEARED"
	self.call_t = 2.6
	self.hot_t = 0.8
	local rng = self.rng
	local ngoo = self.ngoo
	local gx, gy, gz, gvx, gvy, gvz, glife, gtype = self.gx, self.gy, self.gz, self.gvx, self.gvy, self.gvz, self.glife, self.gtype
	for k = 0, 69 do
		local g = self.goo_next + 1
		self.goo_next = (self.goo_next + 1) % ngoo
		gx[g] = self.breach_x - 0.05
		gy[g] = self.breach_y + 0.1 + rng:nextf() * 0.8
		gz[g] = 0.1 + rng:nextf() * 0.8
		gvx[g] = 0.0 - (1.5 + rng:nextf() * 4.5)
		gvy[g] = (rng:nextf() - 0.5) * 2.4
		gvz[g] = 0.5 + rng:nextf() * 2.5
		glife[g] = 1.8 + rng:nextf() * 1.2
		gtype[g] = 2
	end
end

-- 0 a wall, rising to 1 as the cracks of the breach open through the escape, 2 blown open
function GlobHunter:breach_state()
	local tt = self.tt
	if tt >= MUSIC_BAR * 12.0 then return 2.0 end
	local k = (tt - MUSIC_BAR * 10.0) / (MUSIC_BAR * 2.0)
	return k < 0.0 and 0.0 or k
end

-- bullet time: two beats after each big hit play at a fifth of the speed
function GlobHunter:slow_mo(t)
	local since = t - MUSIC_BAR * 5.0
	if t >= MUSIC_BAR * 9.0 then since = t - MUSIC_BAR * 9.0 end
	if t >= MUSIC_BAR * 12.0 then since = t - MUSIC_BAR * 12.0 end
	if since < 0.0 or since > MUSIC_BEAT * 2.5 then return 1.0 end
	local back = (since - MUSIC_BEAT * 2.0) / (MUSIC_BEAT * 0.5)
	back = back < 0.0 and 0.0 or (back > 1.0 and 1.0 or back)
	return 0.2 + 0.8 * back
end

function GlobHunter:fire(i, bx, by, t)
	local boss = i == self.ne - 1
	local boss_down = boss and t >= MUSIC_BAR * 9.0 - 0.01
	local w, h = self.w, self.h
	local rng = self.rng
	local ngoo = self.ngoo
	local ex, ey = self.ex, self.ey
	local gx, gy, gz, gvx, gvy, gvz, glife, gtype = self.gx, self.gy, self.gz, self.gvx, self.gvy, self.gvz, self.glife, self.gtype
	if boss and not boss_down then
		-- a hit the boss takes: the muzzle, the tracer and a flash, but it stands
		self.boss_flash = 0.1
		self.cool = 0.2
		self.muzzle_t = 0.12
		self.tracer_t = 0.14
		self.kick_t = 0.2
		self.tr_x = itrunc(w * 0.5 * (1.0 + bx / by))
		self.tr_y = h // 2 + itrunc(h * 0.10 / by)
		self.callout = ""
		for k0 = 0, 7 do
			local g0 = self.goo_next + 1
			self.goo_next = (self.goo_next + 1) % ngoo
			local a0 = rng:nextf() * 6.2831853
			gx[g0] = ex[i + 1]
			gy[g0] = ey[i + 1]
			gz[g0] = 0.5 + rng:nextf() * 0.4
			gvx[g0] = cos(a0) * 1.2 - self.dirx * 0.8
			gvy[g0] = sin(a0) * 1.2 - self.diry * 0.8
			gvz[g0] = 1.0 + rng:nextf() * 2.0
			glife[g0] = 1.6
			gtype[g0] = 1
		end
		return
	end
	self.es[i + 1] = 1
	self.est[i + 1] = 0.0
	self.kills = self.kills + 1
	self.cool = 0.2
	self.muzzle_t = 0.12
	self.tracer_t = 0.14
	self.hot_t = 0.35
	self.kick_t = 0.2
	self.stop_t = 0.16
	self.tr_x = itrunc(w * 0.5 * (1.0 + bx / by))
	self.tr_y = h // 2 + itrunc(h * 0.10 / by)
	self.combo = t - self.last_kill < 1.8 and self.combo + 1 or 1
	self.last_kill = t
	local words = {"SPLAT", "GLOBBED", "SQUISH", "POP"}
	local combo = self.combo
	self.callout = boss_down and "BOSS DOWN" or (combo >= 3 and "MULTI KILL" or (combo == 2 and "DOUBLE KILL" or words[self.kills % 4 + 1]))
	self.call_t = boss_down and 2.2 or 0.9
	if boss_down then self.hot_t = 0.8 end
	-- the burst: goo flung out of the body, mostly up and back from the shot
	local dirx, diry = self.dirx, self.diry
	local typ = self.et[i + 1]
	for k = 0, (boss_down and 120 or 34) - 1 do
		local g = self.goo_next + 1
		self.goo_next = (self.goo_next + 1) % ngoo
		local a = rng:nextf() * 6.2831853
		local sp = 0.8 + rng:nextf() * 2.2
		gx[g] = ex[i + 1]
		gy[g] = ey[i + 1]
		gz[g] = 0.35 + rng:nextf() * 0.3
		gvx[g] = cos(a) * sp + dirx * 1.2
		gvy[g] = sin(a) * sp + diry * 1.2
		gvz[g] = 1.2 + rng:nextf() * 2.6
		glife[g] = 2.2 + rng:nextf() * 1.5
		gtype[g] = typ
	end
end

-- goo falls, bounces off the flagstones and the walls, then lies there fading
function GlobHunter:move_goo(dt)
	local wmap, mw = self.wmap, self.mw
	local gx, gy, gz, gvx, gvy, gvz, glife = self.gx, self.gy, self.gz, self.gvx, self.gvy, self.gvz, self.glife
	for g = 1, self.ngoo do
		local life = glife[g]
		if life > 0.0 then
			glife[g] = life - dt
			local vz = gvz[g] - 7.5 * dt
			local nz = gz[g] + vz * dt
			if nz < 0.0 then
				nz = 0.0
				vz = vz < -0.6 and 0.0 - vz * 0.3 or 0.0
				gvx[g] = gvx[g] * 0.55
				gvy[g] = gvy[g] * 0.55
			end
			gz[g] = nz
			gvz[g] = vz
			local nx = gx[g] + gvx[g] * dt
			local ny = gy[g] + gvy[g] * dt
			if wmap[itrunc(gy[g]) * mw + itrunc(nx) + 1] ~= 0 then
				gvx[g] = 0.0 - gvx[g] * 0.4
				nx = gx[g]
			end
			if wmap[itrunc(ny) * mw + itrunc(gx[g]) + 1] ~= 0 then
				gvy[g] = 0.0 - gvy[g] * 0.4
				ny = gy[g]
			end
			gx[g] = nx
			gy[g] = ny
		end
	end
end

-- the light map: the torches' steady sum, each cell flickering with the torch
-- that rules it, plus the muzzle near the camera while it burns
function GlobHunter:light_up()
	local lw, lh = self.lw, self.lh
	local n = lw * lh
	local nt = #self.tox
	local flick = {}
	local cue = self.cue
	local tt = self.tt
	local kick = cue ~= nil and cue:kick_env(tt) or 0.0
	-- on the run the torches gutter low and the kick strobes them back up
	local run = tt >= MUSIC_BAR * 10.0 and tt < MUSIC_BAR * 12.0
	local gutter = run and 0.4 + kick * 1.1 or 1.0
	local beat_flare = cue ~= nil and 0.45 * kick + 0.7 * cue:hit(tt) or 0.0
	for k = 0, nt - 1 do
		flick[k + 1] = (1.0 + 0.12 * sin(tt * 13.0 + k * 2.3) + 0.10 * sin(tt * 29.0 + k * 5.1)) * gutter + beat_flare
	end
	local st = self.still
	local ow = self.owner
	local lm = self.lmap
	local flood = cue ~= nil and itrunc(cue:hit(tt) * 4.0) or 0
	for c = 1, n do
		local lv = itrunc(st[c] * flick[ow[c] + 1]) + flood
		lm[c] = lv > 5 and 5 or lv
	end
	-- every burst lights the hall around it while the goo flies
	local es, est, ex, ey = self.es, self.est, self.ex, self.ey
	for b = 1, self.ne do
		local sb = es[b]
		if sb == 1 or sb == 2 then
			local age = sb == 1 and est[b] or 0.14 + est[b]
			local power = 5.0 * (1.0 - age / 0.7)
			if power > 0.0 then
				local bx0 = itrunc(ex[b] * 2.0)
				local by0 = itrunc(ey[b] * 2.0)
				for dy2 = -9, 9 do
					local cy2 = by0 + dy2
					if cy2 >= 0 and cy2 < lh then
						for dx2 = -9, 9 do
							local cx2 = bx0 + dx2
							if cx2 >= 0 and cx2 < lw then
								local dd = (dx2 * dx2 + dy2 * dy2) * 0.25
								local c3 = cy2 * lw + cx2 + 1
								local lv3 = lm[c3] + itrunc(power / (1.0 + dd * 0.5))
								lm[c3] = lv3 > 5 and 5 or lv3
							end
						end
					end
				end
			end
		end
	end
	-- daylight through the breach: a glow through its cracks, then a flood once it is open
	local bs = self:breach_state()
	if bs > 0.0 then
		local power = bs >= 2.0 and 6.0 or bs * (2.0 + kick * 2.0)
		local bx1 = self.breach_x * 2
		local by1 = self.breach_y * 2 + 1
		for dy3 = -12, 12 do
			local cy3 = by1 + dy3
			if cy3 >= 0 and cy3 < lh then
				for dx3 = -24, 0 do
					local cx3 = bx1 + dx3
					if cx3 >= 0 then
						local dd3 = (dx3 * dx3 + dy3 * dy3 * 2.0) * 0.25
						local c4 = cy3 * lw + cx3 + 1
						local lv4 = lm[c4] + itrunc(power / (1.0 + dd3 * 0.12))
						lm[c4] = lv4 > 5 and 5 or lv4
					end
				end
			end
		end
	end
	if self.muzzle_t > 0.0 then
		local cx0 = itrunc(self.posx * 2.0)
		local cy0 = itrunc(self.posy * 2.0)
		for dy = -10, 10 do
			local cy = cy0 + dy
			if cy >= 0 and cy < lh then
				for dx = -10, 10 do
					local cx = cx0 + dx
					if cx >= 0 and cx < lw then
						local d2 = (dx * dx + dy * dy) * 0.25
						local c2 = cy * lw + cx + 1
						local lv2 = itrunc(st[c2] * flick[ow[c2] + 1] + 4.5 / (1.0 + d2 * 0.35))
						lm[c2] = lv2 > 5 and 5 or lv2
					end
				end
			end
		end
	end
end

function GlobHunter:light_at(x, y)
	local lw, lh = self.lw, self.lh
	local cx = itrunc(x * 2.0)
	local cy = itrunc(y * 2.0)
	cx = cx < 0 and 0 or (cx >= lw and lw - 1 or cx)
	cy = cy < 0 and 0 or (cy >= lh and lh - 1 or cy)
	return self.lmap[cy * lw + cx + 1]
end

-- one ray per column: the wall's depth, the rows its slice covers, and what it hit;
-- each column is also filed under the rows where its ceiling ends and its floor begins
function GlobHunter:cast_walls()
	local ww = self.w
	local hh = self.h
	local wm = self.wmap
	local zc = self.zcol
	local vx = self.vigx
	local top = self.wall_top
	local foot = self.wall_foot
	local hit = self.wall_hit
	local clo = self.ceil_lo
	local chi = self.ceil_hi
	local flo = self.floor_lo
	local fhi = self.floor_hi
	local lm = self.lmap
	local lcols = self.lw
	local lrows = self.lh
	local mcols = self.mw
	local px = self.posx
	local py = self.posy
	local dirx, diry, planex, planey = self.dirx, self.diry, self.planex, self.planey
	local hc = hh // 2 + itrunc(self.pitch)
	local rise = self.vault - 0.5
	for v = 1, hh + 1 do
		clo[v] = ww
		chi[v] = -1
		flo[v] = ww
		fhi[v] = -1
	end
	for x = 0, ww - 1 do
		local camx = 2.0 * x / (ww - 1.0) - 1.0
		local rdx = dirx + planex * camx
		local rdy = diry + planey * camx
		local mx = itrunc(px)
		local my = itrunc(py)
		local adx = rdx < 0.0 and -rdx or rdx
		local ady = rdy < 0.0 and -rdy or rdy
		local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
		local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
		local stepx = rdx < 0.0 and -1 or 1
		local stepy = rdy < 0.0 and -1 or 1
		local sdx = rdx < 0.0 and (px - mx) * ddx or (mx + 1.0 - px) * ddx
		local sdy = rdy < 0.0 and (py - my) * ddy or (my + 1.0 - py) * ddy
		local side = 0
		local hitv = 0
		local guard = 0
		while hitv == 0 and guard < 64 do
			if sdx < sdy then
				sdx = sdx + ddx
				mx = mx + stepx
				side = 0
			else
				sdy = sdy + ddy
				my = my + stepy
				side = 1
			end
			hitv = wm[my * mcols + mx + 1]
			guard = guard + 1
		end
		hitv = hitv == 0 and 1 or hitv
		local pdist = side == 0 and sdx - ddx or sdy - ddy   -- perpendicular: no fisheye
		pdist = pdist < 0.05 and 0.05 or pdist
		zc[x + 1] = pdist
		local wallx = side == 0 and py + pdist * rdy or px + pdist * rdx
		wallx = wallx - floor(wallx)
		local tu = itrunc(wallx * 16.0) & 15
		local lineh = hh / pdist
		local y0 = itrunc(hc - lineh * rise)
		local y1 = itrunc(hc + lineh * 0.5)
		y0 = y0 < 0 and 0 or y0
		y1 = y1 > hh and hh or y1
		top[x + 1] = y0
		foot[x + 1] = y1
		if clo[y0 + 1] > x then clo[y0 + 1] = x end
		chi[y0 + 1] = x
		if flo[y1 + 1] > x then flo[y1 + 1] = x end
		fhi[y1 + 1] = x
		-- light: the gloom by distance, the torches at the face, the vignette
		local amb = itrunc(8.0 / (pdist + 1.8))
		amb = amb > 3 and 3 or amb
		local lcx = itrunc((px + rdx * (pdist - 0.05)) * 2.0)
		local lcy = itrunc((py + rdy * (pdist - 0.05)) * 2.0)
		lcx = lcx < 0 and 0 or (lcx >= lcols and lcols - 1 or lcx)
		lcy = lcy < 0 and 0 or (lcy >= lrows and lrows - 1 or lcy)
		local lv = amb + lm[lcy * lcols + lcx + 1] - vx[x + 1]
		lv = lv < 0 and 0 or (lv > 7 and 7 or lv)
		hit[x + 1] = ((hitv * 2 + side) * 16 + tu) * 8 + lv
	end
end

-- floor and ceiling, cast per pixel only where no wall slice covers them: each
-- row off the horizon is a fixed distance, stepped along the camera plane across
-- the columns whose ceiling (or floor) reaches that row
function GlobHunter:cast_planes(grid)
	local hh = self.h
	local h0 = hh // 2 + itrunc(self.pitch)
	local clo = self.ceil_lo
	local chi = self.ceil_hi
	local flo = self.floor_lo
	local fhi = self.floor_hi
	local rise = self.vault - 0.5
	local lo = clo[hh + 1]
	local hi = chi[hh + 1]
	for y = hh - 1, 0, -1 do
		if y < h0 and lo <= hi then
			local up = h0 - y - 0.5
			local ceil_d = rise * hh / up
			-- a texel is up / (16 * ceil_d) pixels tall: under two, its grain would only speckle
			self:cast_plane_row(grid, y, ceil_d, lo, hi, self.ceil_base, up < ceil_d * 32.0 and 2304 or 1792)
		end
		lo = clo[y + 1] < lo and clo[y + 1] or lo
		hi = chi[y + 1] > hi and chi[y + 1] or hi
	end
	lo = self.w
	hi = -1
	for y2 = 0, hh - 1 do
		lo = flo[y2 + 1] < lo and flo[y2 + 1] or lo
		hi = fhi[y2 + 1] > hi and fhi[y2 + 1] or hi
		if y2 >= h0 and lo <= hi then
			local down = y2 - h0 + 0.5
			local floor_d = 0.5 * hh / down
			self:cast_plane_row(grid, y2, floor_d, lo, hi, self.floor_base, down < floor_d * 32.0 and 2048 or 1536)
		end
	end
end

function GlobHunter:cast_plane_row(grid, y, rowd, x0, x1, plane_base, plane_tex)
	local ww = self.w
	local tx = self.tex
	local lm = self.lmap
	local vx = self.vigx
	local lcols = self.lw
	local lrows = self.lh
	local planex, planey = self.planex, self.planey
	local step_x = rowd * 2.0 * planex / ww
	local step_y = rowd * 2.0 * planey / ww
	local fx = self.posx + rowd * (self.dirx - planex) + step_x * x0
	local fy = self.posy + rowd * (self.diry - planey) + step_y * x0
	local amb = itrunc(8.0 / (rowd + 1.8))
	amb = (amb > 3 and 3 or amb) - self.vigy[y + 1]
	local gi = y * ww + 1
	local tbase = plane_tex + 1
	local cell = -1
	local lit = 0
	for x = x0, x1 do
		local iu = itrunc(fx * 16.0)
		local iv = itrunc(fy * 16.0)
		-- the light changes only from one light-map cell to the next; // 8 is the arithmetic >> 3
		local lc = (iv // 8) * 4096 + (iu // 8)
		if lc ~= cell then
			cell = lc
			local lcx = iu // 8
			local lcy = iv // 8
			lcx = lcx < 0 and 0 or (lcx >= lcols and lcols - 1 or lcx)
			lcy = lcy < 0 and 0 or (lcy >= lrows and lrows - 1 or lcy)
			lit = amb + lm[lcy * lcols + lcx + 1]
		end
		local lv = lit - vx[x + 1]
		lv = lv < 0 and 0 or (lv > 7 and 7 or lv)
		grid[gi + x] = plane_base + lv * 8 + tx[tbase + (iv & 15) * 16 + (iu & 15)]
		fx = fx + step_x
		fy = fy + step_y
	end
end

-- one billboard: an ellipsoid body lit as a sphere per pixel, eyes that glow
-- in the gloom, a grin; walls occlude per column; hit-flash paints it white
function GlobHunter:draw_blob(grid, i)
	local w, h = self.w, self.h
	local cue = self.cue
	local tyy = self.sty[i + 1]
	local txx = self.stx[i + 1]
	local scx = itrunc(w * 0.5 * (1.0 + txx / tyy))
	local typ = self.et[i + 1]
	local boss = i == self.ne - 1
	local size = boss and 1.7 + (cue ~= nil and cue:kick_env(self.tt) * 0.22 or 0.0) or (typ == 1 and 1.1 or 0.78)
	local sh = itrunc(h * size / tyy)
	sh = sh < 3 and 3 or sh
	sh = sh > h and h or sh
	-- its half-width never passes its kind's reach in world units, whatever the pixels' shape
	local hwm = sh * (boss and 0.57 or (typ == 1 and 0.6 or 0.55))
	local room = (boss and 1.2 or (typ == 1 and 0.75 or 0.5)) * w / (1.32 * tyy)
	hwm = hwm > room and room or hwm
	local feet = h // 2 + itrunc(self.pitch) + itrunc(h * 0.5 / tyy)
	local top = feet - sh
	local wob = sin(self.tt * 5.0 + i * 2.1) * 0.10
	local flash = self.es[i + 1] == 1 or (boss and self.boss_flash > 0.0)
	local sb = self.sprite_base
	local body = sb + typ * 12
	local fogk = 0.18 + self:light_at(self.ex[i + 1], self.ey[i + 1]) * 0.15 + 0.35 / (tyy + 0.6)
	fogk = fogk < 0.2 and 0.2 or (fogk > 1.0 and 1.0 or fogk)
	local glow = fogk < 0.55 or boss
	local zcol = self.zcol
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
						local pi = sb + 24
						if not flash then
							local nx = dx / (hwm + 0.5)
							local q = 1.0 - nx * nx - v * v
							local nz = q > 0.0 and sqrt(q) or 0.0
							local li = -0.45 * nx - 0.55 * v + 0.70 * nz
							li = li < 0.0 and 0.0 or li
							-- a rim of torchlight on the silhouette's edge
							local rim = (q < 0.12 and li < 0.3) and 0.35 or 0.0
							local lv = itrunc((0.10 + 0.9 * li + rim) * fogk * 11.99)
							pi = body + (lv > 11 and 11 or lv)
							local ax = nx < 0.0 and -nx or nx
							if v > -0.5 and v < -0.12 and ax > 0.14 and ax < 0.52 then
								local ey2 = (v + 0.31) * 5.0
								local ex2 = (ax - 0.33) * 5.0
								if ex2 * ex2 + ey2 * ey2 < 1.0 then
									local core = ex2 * ex2 + ey2 * ey2 < 0.3
									pi = core and (glow and sb + 48 or sb + 26) or (glow and sb + 26 or sb + 25)
								end
							end
							if v > 0.18 and v < 0.34 and ax < 0.42 then
								pi = ((dx & 1) == 0 and v < 0.24) and sb + 25 or sb + 45
							end
						end
						grid[rowb + col] = pi
					end
				end
			end
		end
	end
end

-- a torch's flame: a flickering teardrop, occluded by nearer walls
function GlobHunter:draw_flame(grid, k)
	local planex, planey, dirx, diry = self.planex, self.planey, self.dirx, self.diry
	local invdet = 1.0 / (planex * diry - dirx * planey)
	local rx = self.tox[k + 1] - self.posx
	local ry = self.toy[k + 1] - self.posy
	local tyy = invdet * (planex * ry - planey * rx)
	if tyy < 0.3 then return end
	local txx = invdet * (diry * rx - dirx * ry)
	local w, h = self.w, self.h
	local scx = itrunc(w * 0.5 * (1.0 + txx / tyy))
	local hc = h // 2 + itrunc(self.pitch)
	local fh = itrunc(h * 0.24 / tyy)
	fh = fh < 3 and 3 or fh
	local fy0 = itrunc(hc - h * 0.14 / tyy) - fh
	local flick = sin(self.tt * 17.0 + k * 3.0)
	local zcol = self.zcol
	local sb = self.sprite_base
	for dy = 0, fh - 1 do
		local v = (dy + 0.5) / fh
		local half = (v < 0.65 and v / 0.65 or 1.0 - (v - 0.65) / 0.35 * 0.4) * fh * 0.28
		local sway = itrunc(flick * (1.0 - v) * fh * 0.12)
		local hw = itrunc(half)
		local row = fy0 + dy
		if row >= 0 and row < h then
			for dx = 0 - hw, hw do
				local col = scx + dx + sway
				if not (col < 0 or col >= w or tyy >= zcol[col + 1]) then
					local ax = dx < 0 and 0 - dx or dx
					local e = hw > 0 and ax / hw or 0.0
					local c = (e < 0.4 and v > 0.45) and 31 or (e < 0.75 and 32 or 33)
					grid[row * w + col + 1] = sb + c
				end
			end
		end
	end
end

function GlobHunter:render(grid)
	self:cast_walls()
	self:cast_planes(grid)
	local ww = self.w
	local hh = self.h
	local tx = self.tex
	local zc = self.zcol
	local top = self.wall_top
	local foot = self.wall_foot
	local hit = self.wall_hit
	local wb = self.wall_base
	local bb = self.banner_base
	local tshift = itrunc(self.tt * 14.0)
	local hc = hh // 2 + itrunc(self.pitch)
	local rise = self.vault - 0.5
	for x = 0, ww - 1 do
		local y0 = top[x + 1]
		local y1 = foot[x + 1]
		local packed = hit[x + 1]
		local lv = packed & 7
		local tu = (packed >> 3) & 15
		local side = (packed >> 7) & 1
		local hitv = packed >> 8
		local lineh = hh / zc[x + 1]
		local tstep = 16.0 / lineh
		local tpos = (y0 + 0.5 - (hc - lineh * rise)) * tstep
		local gi = y0 * ww + x + 1
		if hitv == 5 then
			self:breach_slice(grid, x, y0, y1, tpos, tstep, tu, lv, side)
		elseif hitv == 4 then
			local brow = bb + (side * 8 + lv) * 16
			-- rainbow fire: ragged flame tongues per texel column, hue bands scroll up the cloth
			local wave = (tu * 3 + tshift) % 12
			local flen = 12 + (wave < 6 and wave or 12 - wave) * 2
			local bank = (((tu >> 2) + (tshift >> 3)) & 3) * 4
			for y = y0, y1 - 1 do
				local tv = itrunc(tpos)
				local up = 31 - (tv & 31)
				local heat = up > flen and 0 or (up * 3 > flen * 2 and 1 or (up * 3 > flen and 2 or 3))
				grid[gi] = brow + bank + heat
				tpos = tpos + tstep
				gi = gi + ww
			end
		else
			local wrow = wb + (((hitv - 1) * 2 + side) * 8 + lv) * 8
			local tbase = (hitv - 1) * 512 + tu + 1
			for y2 = y0, y1 - 1 do
				local tv2 = itrunc(tpos)
				grid[gi] = wrow + tx[tbase + (tv2 & 31) * 16]
				tpos = tpos + tstep
				gi = gi + ww
			end
		end
	end
	for k = 0, #self.tox - 1 do self:draw_flame(grid, k) end
	-- sprite pass: camera-space transforms, then paint far to near
	local ne = self.ne
	local ex, ey, es, stx, sty, drawn = self.ex, self.ey, self.es, self.stx, self.sty, self.drawn
	local posx, posy, dirx, diry, planex, planey = self.posx, self.posy, self.dirx, self.diry, self.planex, self.planey
	local invdet = 1.0 / (planex * diry - dirx * planey)
	for i = 1, ne do
		local rx = ex[i] - posx
		local ry = ey[i] - posy
		stx[i] = invdet * (diry * rx - dirx * ry)
		sty[i] = invdet * (planex * ry - planey * rx)
		drawn[i] = 0
	end
	for pass = 0, ne - 1 do
		local pick = -1
		local pty = -1.0
		for i2 = 1, ne do
			if drawn[i2] ~= 1 and es[i2] < 2 then
				local sv = sty[i2]
				if sv > 0.25 and sv > pty then
					pty = sv
					pick = i2 - 1
				end
			end
		end
		if pick < 0 then break end
		drawn[pick + 1] = 1
		self:draw_blob(grid, pick)
	end
	self:draw_goo(grid, invdet)
	self:daylight(grid)
	self:draw_hud(grid)
end

-- out through the breach: daylight blooms from the heart of the frame, its edge dithered gold
function GlobHunter:daylight(grid)
	local tt = self.tt
	if tt < MUSIC_BAR * 12.0 then return end
	local w, h = self.w, self.h
	local k = (tt - MUSIC_BAR * 12.0) / MUSIC_BAR
	k = k > 1.0 and 1.0 or k
	-- the blast whites out the frame for a moment, then the opening blooms
	local r = k < 0.06 and w * 1.2 or (0.12 + k * k * 0.6) * w
	local band = 7.0
	local cx = w * 0.5
	local cy = h * 0.5 + self.pitch
	local sb = self.sprite_base
	for y = 0, h - 1 do
		local dy = (y + 0.5 - cy) * 1.5
		local oo = r * r - dy * dy
		if oo > 0.0 then
			-- per row: the white heart, the warm white ring, the dithered gold edge
			local xo = sqrt(oo)
			local ri = r - band
			local ii = ri * ri - dy * dy
			local xi = ii > 0.0 and sqrt(ii) or 0.0
			local hh = r * r * 0.36 - dy * dy
			local xh = hh > 0.0 and sqrt(hh) or 0.0
			local row = y * w + 1
			local x0 = itrunc(cx - xo)
			local x1 = itrunc(cx + xo) + 1
			x0 = x0 < 0 and 0 or x0
			x1 = x1 > w and w or x1
			for x = x0, x1 - 1 do
				local dx = x + 0.5 - cx
				local ax = dx < 0.0 and -dx or dx
				if ax < xh then
					grid[row + x] = sb + 24
				elseif ax < xi then
					grid[row + x] = sb + 31
				else
					local over = (r - sqrt(dx * dx + dy * dy)) / band
					if over * 16.0 > BAYER4[(y & 3) * 4 + (x & 3) + 1] then grid[row + x] = sb + 32 end
				end
			end
		end
	end
end

-- the breach: stone whose cracks glow hotter through the escape, then open daylight
function GlobHunter:breach_slice(grid, x, y0, y1, tpos, tstep, tu, lv, side)
	local bs = self:breach_state()
	local sb = self.sprite_base
	local w = self.w
	local gi = y0 * w + x + 1
	if bs >= 2.0 then
		-- open: white at the heart of the opening, gold toward its edges, rays rippling up it
		local shimmer = itrunc(self.tt * 9.0)
		for y = y0, y1 - 1 do
			local tv = itrunc(tpos)
			-- // 2 is the arithmetic >> 1 (31 - tv can go negative)
			local ev = (tv < 16 and tv or 31 - tv) // 2
			local eu = tu < 8 and tu or 15 - tu
			local edge = ev < eu and ev or eu
			local ray = ((tu * 5 + tv // 2 + shimmer) & 7) == 0 and 1 or 0
			grid[gi] = edge > 3 and sb + 24 or (edge > 1 - ray and sb + 31 or sb + 32)
			tpos = tpos + tstep
			gi = gi + w
		end
		return
	end
	local cue = self.cue
	local kick = cue ~= nil and cue:kick_env(self.tt) or 0.0
	local heat = itrunc((bs + kick * 0.35) * 14.0)
	local wrow = self.wall_base + (side * 8 + lv) * 8
	local tex = self.tex
	for y2 = y0, y1 - 1 do
		local tv2 = itrunc(tpos)
		local ti = (tv2 & 31) * 16 + tu
		local crack = self:hash2(ti, 77) & 15
		local c = tex[ti + 1]
		-- mortar lines and a hashed web of cracks catch fire from the light behind
		local pix = wrow + c
		if (c == 0 and crack < heat) or crack + 6 < heat - 8 then
			pix = crack < heat - 6 and sb + 31 or (crack < heat - 3 and sb + 32 or sb + 33)
		end
		grid[gi] = pix
		tpos = tpos + tstep
		gi = gi + w
	end
end

-- the goo in flight and on the floor: blobs sized by distance, lit by the map
function GlobHunter:draw_goo(grid, invdet)
	local w, h = self.w, self.h
	local hc = h // 2 + itrunc(self.pitch)
	local posx, posy, dirx, diry, planex, planey = self.posx, self.posy, self.dirx, self.diry, self.planex, self.planey
	local gx, gy, gz, glife, gtype = self.gx, self.gy, self.gz, self.glife, self.gtype
	local zcol = self.zcol
	local sb = self.sprite_base
	local rubble_base = self.rubble_base
	for g = 1, self.ngoo do
		local life = glife[g]
		if life > 0.0 then
			local rx = gx[g] - posx
			local ry = gy[g] - posy
			local tyy = invdet * (planex * ry - planey * rx)
			if tyy >= 0.3 then
				local txx = invdet * (diry * rx - dirx * ry)
				local scx = itrunc(w * 0.5 * (1.0 + txx / tyy))
				if not (scx < 0 or scx >= w or tyy >= zcol[scx + 1]) then
					local typ = gtype[g]
					local scy = itrunc(hc + (0.5 - gz[g]) * h / tyy)
					local rad = itrunc(h * (typ == 2 and 0.024 or 0.04) / tyy * (life < 0.6 and life / 0.6 or 1.0))
					local flat = gz[g] <= 0.0 and 1 or 0
					local lk = 0.3 + self:light_at(gx[g], gy[g]) * 0.14
					local base = typ == 2 and rubble_base or sb + typ * 12
					local body = base + itrunc(lk * 10.0)
					body = body > base + 11 and base + 11 or body
					for dy = 0 - rad, rad do
						local row = scy + (flat == 1 and idiv(dy, 2) or dy)
						if row >= 0 and row < h then
							for dx = 0 - rad - flat * rad, rad + flat * rad do
								local col = scx + dx
								if not (col < 0 or col >= w or tyy >= zcol[col + 1]) then
									if dx * dx + dy * dy * (flat == 1 and 4 or 1) <= (rad + 1) * (rad + 1) * (flat == 1 and 4 or 1) then
										grid[row * w + col + 1] = (dy < 0 and dx < 0 and flat == 0 and typ ~= 2) and sb + 52 or body
									end
								end
							end
						end
					end
				end
			end
		end
	end
end

function GlobHunter:draw_hud(grid)
	local w, h = self.w, self.h
	local sb = self.sprite_base
	local gx0 = w // 2
	local gs = h // 40
	gs = gs < 2 and 2 or gs
	local flick = itrunc(self.tt * 10.0)
	-- rainbow hitscan tracer from the muzzle to the target
	if self.tracer_t > 0.0 then
		local tr_x, tr_y = self.tr_x, self.tr_y
		local y0t = h - 11 * gs
		local steps = 14 * gs
		for k2 = 1, steps - 1 do
			local xx = gx0 + idiv((tr_x - gx0) * k2, steps)
			local yy = y0t + idiv((tr_y - y0t) * k2, steps)
			for th = 0, gs // 2 - 1 do
				if not (xx + th < 0 or xx + th >= w or yy < 1 or yy >= h) then
					grid[yy * w + xx + th + 1] = sb + 34 + (k2 // gs + flick) % 6
				end
			end
		end
	end
	-- the gun, drawn at gs x: bobs with the walk, kicks on the shot
	local bob = itrunc(sin(self.travel * 5.2) * 1.25 * gs)
	local kick = self.muzzle_t > 0.0 and 2 * gs or 0
	local gw = 16
	local gn = self.gun
	local gh = #gn // gw
	local gxs = gx0 - gw * gs // 2 + bob
	local gys = h - gh * gs + 2 * gs + kick + (bob < 0 and 0 - bob or bob) // 2
	for ry = 0, gh * gs - 1 do
		local grow = (ry // gs) * gw + 1
		local yy2 = gys + ry
		if yy2 >= 0 and yy2 < h then
			for rx = 0, gw * gs - 1 do
				local c = gn[grow + rx // gs]
				if c ~= 0 then
					local xx2 = gxs + rx
					if xx2 >= 0 and xx2 < w then grid[yy2 * w + xx2 + 1] = sb + c end
				end
			end
		end
	end
	if self.muzzle_t > 0.0 then
		local mx = gxs + gw * gs // 2 - 1
		local my = gys - 2 * gs
		local ray = 3 * gs
		for q = 0 - ray, ray do
			local aq = q < 0 and 0 - q or q
			local c2 = aq * 5 <= ray and 31 or (aq * 5 <= ray * 3 and 32 or 33)
			for th2 = 0, gs - 1 do
				if my + q >= 0 and my + q < h and mx + th2 < w then grid[(my + q) * w + mx + th2 + 1] = sb + c2 end
				if mx + q >= 0 and mx + q < w and my + th2 >= 0 and my + th2 < h then grid[(my + th2) * w + mx + q + 1] = sb + c2 end
			end
			if aq * 5 <= ray * 3 and mx + q >= 0 and mx + q < w and my + q >= 0 and my + q < h then grid[(my + q) * w + mx + q + 1] = sb + 32 end
			if aq * 5 <= ray * 3 and mx - q >= 0 and mx - q < w and my + q >= 0 and my + q < h then grid[(my + q) * w + mx - q + 1] = sb + 32 end
		end
	end
	local chc = self.hot_t > 0.0 and sb + 41 or sb + 40
	local cy = itrunc(h * 0.5)
	local cr = gs // 2
	for d = cr + 1, cr * 2 + 2 do
		grid[cy * w + gx0 - d + 1] = chc
		grid[cy * w + gx0 + d + 1] = chc
		grid[(cy - d) * w + gx0 + 1] = chc
		grid[(cy + d) * w + gx0 + 1] = chc
	end
	local ts = h // 80
	ts = ts < 1 and 1 or ts
	local kills = self.kills
	local tally = "KILLS " .. (kills < 10 and "0" or "") .. kills
	gfx:draw_text(grid, w, h, tally, 2, 2, ts, ts, sb + 42, 2, sb + 44, 99)
	-- the callout: it punches in large and settles, rising as it fades
	local callout = self.callout
	if self.call_t > 0.0 and #callout > 0 then
		local age = 0.9 - self.call_t
		local cs = w * 3 // 5 // (#callout * 6)
		cs = cs < 1 and 1 or (cs > 3 * ts and 3 * ts or cs)
		cs = cs + (age < 0.08 and 1 or 0)
		local tw = #callout * 6 * cs
		local cx = idiv(w - tw, 2)
		local cyy = itrunc(h * 0.22 - age * h * 0.08)
		local ink = self.combo >= 2 and sb + 53 + (flick & 3) or sb + 49 + (flick & 1) * 2
		if cx > 0 then gfx:draw_text(grid, w, h, callout, cx, cyy, cs, cs, ink, 1, sb + 44, 99) end
	end
end

SCENES[6] = GlobHunter
