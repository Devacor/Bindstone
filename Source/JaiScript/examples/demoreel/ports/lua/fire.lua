-- FIRE: bottom-seeded convection with the Doom fire's random sideways jitter and
-- cooling, 73 heat levels, sparks, and six precomputed hue-rolled rainbow banks.

local sin = math.sin

Fire = class(Scene)

function Fire:init()
	Scene.init(self)
	self.heat = {}
	self.cool = {}
	self.mask = {}
	self.spx = {}
	self.spy = {}
	self.svx = {}
	self.svy = {}
	self.slife = {}
	self.nsparks = 48
	self.levels = 73
	self.phases = 6
	self.fr = 0
	self.rainbow_on = 0
	self.hue_t = 0.0
	self.tt = 0.0
end

function Fire:build()
	self.name = "FIRE"
	self.handle = "ember chapel -> rainbow mode . for devacor"
	self:build_fire()
end

function Fire:build_fire()
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	self.heat = make_grid(w * (h + 2))
	local cool = self.cool
	for i = 1, 1024 do cool[i] = rng:next(4) end
	local levels, phases = self.levels, self.phases
	-- entries 0..72: classic fire ramp
	for i = 0, levels - 1 do
		local f = i / (levels - 1.0)
		local r = 255.0 * f * 2.2
		local g = f > 0.28 and 255.0 * (f - 0.28) * 1.7 or 0.0
		local b = f > 0.75 and 255.0 * (f - 0.75) * 3.2 or 0.0
		pal:add(r, g, b)
	end
	-- entries 73+p*73 .. : rainbow banks, hue rolled per bank
	for p = 0, phases - 1 do
		local base_hue = p * 6.28318530718 / phases
		for i = 0, levels - 1 do
			local f = i / (levels - 1.0)
			local c = gfx:rainbow_rgb(base_hue + f * 1.4)
			local glow = f * 1.8 > 1.0 and 1.0 or f * 1.8
			local wh = f > 0.85 and (f - 0.85) * 4.0 or 0.0
			local cr = c[1] * glow
			local cg = c[2] * glow
			local cb = c[3] * glow
			pal:add(cr + (255.0 - cr) * wh, cg + (255.0 - cg) * wh, cb + (255.0 - cb) * wh)
		end
	end
	for s = 1, self.nsparks do
		self.spx[s] = 0.0
		self.spy[s] = 0.0
		self.svx[s] = 0.0
		self.svy[s] = 0.0
		self.slife[s] = 0.0
	end
end

function Fire:update(t, dt)
	self.tt = t
	self.fr = self.fr + 1
	self.rainbow_on = t > 5.5 and 1 or 0
	if self.rainbow_on == 1 then self.hue_t = self.hue_t + dt * 3.0 end
	self:seed_rows(40, 33)
	self:convect(false)
	self:move_sparks(dt, true)
end

-- the two hidden bottom rows: mostly furious, occasionally gapped
function Fire:seed_rows(lo, span)
	local w, rng, heat = self.w, self.rng, self.heat
	local base = self.h * w + 1
	for x = 0, w - 1 do
		local v = lo + rng:next(span)
		v = rng:next(100) < 13 and rng:next(lo // 3 + 2) or v
		heat[base + x] = v
		heat[base + w + x] = v
	end
end

-- convection with the Doom fire's jitter; with `lit` the mask pixels are heat
-- sources that never drop below a glow
function Fire:convect(lit)
	local ww = self.w
	local hh = self.h
	local ht = self.heat
	local rn = self.cool
	local mk = self.mask
	local fr = self.fr
	local salt = fr * 131
	for y = 0, hh - 1 do
		local row = y * ww
		local below = row + ww + 1
		local below2 = below + ww
		for x = 0, ww - 1 do
			local r = rn[((row + x * 7 + salt) & 1023) + 1]
			local xs = x + (r == 3 and 0 or r - 1)
			xs = xs < 1 and 1 or (xs > ww - 2 and ww - 2 or xs)
			local v = ((ht[below + xs - 1] + ht[below + xs] + ht[below + xs + 1] + ht[below2 + xs]) >> 2) - ((r + 1) >> 1)
			ht[row + x + 1] = v > 0 and v or 0
		end
	end
	if lit then
		local n = ww * hh
		for i = 0, n - 1 do
			if mk[i + 1] == 1 then
				local glow = 58 + (rn[((i * 7 + fr) & 1023) + 1] & 1) * 8
				if ht[i + 1] < glow then ht[i + 1] = glow end
			end
		end
	end
end

function Fire:move_sparks(dt, spawn)
	local w, h, rng, tt = self.w, self.h, self.rng, self.tt
	local spx, spy, svx, svy, slife = self.spx, self.spy, self.svx, self.svy, self.slife
	for s = 0, self.nsparks - 1 do
		local i = s + 1
		local life = slife[i] - dt
		local alive = true
		if life <= 0.0 then
			if not spawn or rng:next(100) >= 12 then
				slife[i] = 0.0
				alive = false
			else
				spx[i] = rng:next(w * 10) * 0.1
				spy[i] = h * (0.62 + rng:next(30) * 0.01)
				svx[i] = (rng:next(200) - 100) * 0.08
				svy[i] = 0.0 - (18.0 + rng:next(220) * 0.1)
				life = 0.8 + rng:next(100) * 0.012
			end
		end
		if alive then
			slife[i] = life
			svx[i] = svx[i] + sin(tt * 3.0 + s) * 6.0 * dt
			spx[i] = spx[i] + svx[i] * dt
			spy[i] = spy[i] + svy[i] * dt
		end
	end
end

function Fire:bank()
	return self.rainbow_on == 1 and self.levels + (itrunc(self.hue_t * 4.0) % self.phases) * self.levels or 0
end

function Fire:render(grid)
	local b = self:bank()
	local n = self.w * self.h
	local ht = self.heat
	for i = 1, n do
		local v = ht[i]
		grid[i] = (v > 72 and 72 or v) + b
	end
	self:draw_sparks(grid, b)
end

function Fire:draw_sparks(grid, b)
	local w, h, fr = self.w, self.h, self.fr
	local spx, spy, slife = self.spx, self.spy, self.slife
	for s = 0, self.nsparks - 1 do
		local life = slife[s + 1]
		if life > 0.0 then
			local x = itrunc(spx[s + 1])
			local y = itrunc(spy[s + 1])
			if not (x < 0 or x >= w or y < 0 or y >= h) then
				local lv = 40 + itrunc(life * 30.0) + ((fr + s) & 3) * 3
				grid[y * w + x + 1] = (lv > 72 and 72 or lv) + b
			end
		end
	end
end
