-- TUNNEL: the table-driven texture tunnel. atan2 and 1/r run once per pixel of a
-- double-size table at build; a frame is four integer lookups per pixel.

local sin, sqrt, atan = math.sin, math.sqrt, math.atan

Tunnel = class(Scene)

function Tunnel:init()
	Scene.init(self)
	self.ang = {}
	self.dep = {}
	self.fog = {}
	self.tex = {}
	self.tw = 0
	self.tt = 0.0
	self.travel = 0.0
	self.spin = 0.0
end

function Tunnel:build()
	self.name = "TUNNEL"
	self.handle = "down the wormhole . by lookup table"
	local w, h, pal = self.w, self.h, self.pal
	self.tw = w * 2
	local th = h * 2
	local rmax = w * 0.62
	local ang, dep, fog = self.ang, self.dep, self.fog
	local n = 0
	for y = 0, th - 1 do
		for x = 0, self.tw - 1 do
			local dx = x - w + 0.5
			local dy = y - h + 0.5
			local r = sqrt(dx * dx + dy * dy)
			r = r < 1.0 and 1.0 or r
			n = n + 1
			ang[n] = itrunc((atan(dy, dx) + 3.14159265) * 40.7436654) & 255
			dep[n] = itrunc(2600.0 / r)
			local s = itrunc(r * 12.0 / rmax)
			fog[n] = (s > 11 and 11 or s) * 16
		end
	end
	-- 16 texel levels: steel plate ramp 0..11, neon 12..15; 12 fog banks
	for s2 = 0, 11 do
		local f = s2 / 11.0
		local k = f * f * (3.0 - 2.0 * f)
		for lv = 0, 15 do
			local r0, g0, b0 = 0.0, 0.0, 0.0
			if lv < 12 then
				local m = lv / 11.0
				r0 = 18.0 + 120.0 * m; g0 = 26.0 + 150.0 * m; b0 = 48.0 + 190.0 * m
			else
				local m2 = (lv - 11) / 4.0
				r0 = 120.0 + 135.0 * m2; g0 = 40.0 + 160.0 * m2 * m2; b0 = 200.0 + 55.0 * m2
			end
			-- far walls dissolve into the glow at the tunnel's end
			local glow = lv >= 12 and 0.45 + 0.55 * k or k
			local haze = (1.0 - k) * (1.0 - k)
			pal:add(r0 * glow + 255.0 * haze, g0 * glow + 150.0 * haze, b0 * glow + 120.0 * haze)
		end
	end
	local tex = self.tex
	local nt = 0
	for v = 0, 63 do
		for u = 0, 63 do
			local row = v // 16
			local bu = (u + (row % 2) * 8) % 16
			local bv = v % 16
			local plate = ((u + (row % 2) * 8) // 16 + row * 5) % 7
			local lv = 5 + plate % 4
			if bu == 0 or bv == 0 then lv = 1
			elseif bu == 1 or bv == 1 then lv = lv + 3
			elseif bu == 15 or bv == 15 then lv = lv - 3 end
			if (bu == 3 or bu == 12) and (bv == 3 or bv == 12) then lv = 11 end
			if v >= 28 and v < 36 then
				local d = v < 32 and v - 28 or 35 - v
				lv = 12 + d
			end
			nt = nt + 1
			tex[nt] = lv < 0 and 0 or lv
		end
	end
end

function Tunnel:update(t, dt)
	self.tt = t
	local sp = 34.0 + 22.0 * sin(t * 0.37)
	self.travel = self.travel + dt * sp
	self.spin = self.spin + dt * (0.9 * sin(t * 0.29))
end

function Tunnel:render(grid)
	local ww = self.w
	local hh = self.h
	local tww = self.tw
	local an, de, fo, tx = self.ang, self.dep, self.fog, self.tex
	local tt = self.tt
	local w, h = self.w, self.h
	local ox = itrunc(w * 0.5 + w * 0.34 * sin(tt * 0.53))
	local oy = itrunc(h * 0.5 + h * 0.30 * sin(tt * 0.71 + 0.9))
	local mv = itrunc(self.travel)
	local rot = itrunc(self.spin * 40.0) + 4096
	local twist = itrunc(6.0 + 5.0 * sin(tt * 0.23))
	local idx = 1
	for y = 0, hh - 1 do
		local trow = (y + oy) * tww + ox + 1
		for x = 0, ww - 1 do
			local ti = trow + x
			local d = de[ti]
			local u = (an[ti] + rot + ((d * twist) >> 5)) & 63
			local v = (d + mv) & 63
			grid[idx] = fo[ti] + tx[v * 64 + u + 1]
			idx = idx + 1
		end
	end
end
