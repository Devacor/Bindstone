-- JULIA (Lua 5.4 port of scenes/julia.jai): the whole Julia set, morphing in frame. The
-- constant is steered through its fixed point's multiplier lambda = rho * e^(i theta),
-- c = lambda / 2 - lambda^2 / 4, from fat connected sets through the bulbs to dendrites.
-- Every pixel iterates z and its derivative: the smooth escape count picks the hue, the
-- distance estimate draws the boundary as a white-hot rim, the potential's gradient lights
-- the bands as a relief, and the inside is banded by where its orbit ends up round the fixed
-- point. Every view is centred on the origin, so a frame computes half its rows and mirrors them.

local sin, cos, sqrt, log, atan2 = math.sin, math.cos, math.sqrt, math.log, math.atan

local JU_CA = 0.0          -- c
local JU_CB = 0.0
local JU_FX = 0.0          -- the fixed point alpha = lambda / 2
local JU_FY = 0.0
local JU_TRAP = 0.0        -- squared radius round alpha inside which an orbit is home; 0 when alpha repels
local JU_DRIFT = 0.0       -- what an iteration adds to the inside's potential, so the trap's early exit leaves no seam
local JU_MAXIT = 40
local JU_W = 0
local JU_H = 0
local JU_UX = 0.0          -- the view, centred on the origin: a pixel's step right and down
local JU_UY = 0.0
local JU_VX = 0.0
local JU_VY = 0.0
local JU_PIXELS = 1.0      -- pixels per plane unit
local JU_UNRESOLVED = 0.0  -- squared stretch past which a pixel that never escaped is boundary, not inside
local JU_LX = 0.0          -- the light's direction in the plane
local JU_LY = 0.0
local JU_PHASE = 0.0       -- the palette's turn, outside and inside
local JU_INPHASE = 0.0
local JU_LIFT = 0.0        -- light added on the beat
local JU_RIM = 1.0         -- the rim's width in pixels
local JU_INSIDE = 0
local JU_ROWS = {}

local JU_HUES = 64
local JU_LEVELS = 12
local JU_INHUES = 32

local JU_RHO = 0           -- the path's tracks
local JU_THETA = 1
local JU_DIVE = 2
local JU_SPIN = 3

-- one row of the top half; each entry packs the pixel and the one half a turn away,
-- whose orbit is the same and whose gradient points the other way
local function julia_row(y)
	local out = {}
	local w = JU_W
	local mi = JU_MAXIT
	local ca = JU_CA
	local cb = JU_CB
	local ux = JU_UX
	local uy = JU_UY
	local py = y + 0.5 - JU_H * 0.5
	local left = 0.5 - w * 0.5
	local rx = py * JU_VX + left * ux
	local ry = py * JU_VY + left * uy
	local lx = JU_LX
	local ly = JU_LY
	local fx = JU_FX
	local fy = JU_FY
	local trap = JU_TRAP
	local drift = JU_DRIFT
	local pixels = JU_PIXELS
	local phase = JU_PHASE
	local inphase = JU_INPHASE
	local lift = JU_LIFT
	local rim = JU_RIM
	local rimfall = 0.5 / rim
	local inside = JU_INSIDE
	local unresolved = JU_UNRESOLVED
	local hot = (itrunc(mi * 3.5 + phase) & 63) * 12 + 11
	for x = 0, w - 1 do
		local zx = rx + x * ux
		local zy = ry + x * uy
		local x2 = zx * zx
		local y2 = zy * zy
		local dx = 1.0
		local dy = 0.0
		local e2 = 1.0
		local k = 0
		local nd = 0.0
		-- four steps to a test: an escaped orbit's count and distance stay exact however far it runs on
		while k < mi do
			nd = 2.0 * (zx * dx - zy * dy)
			dy = 2.0 * (zx * dy + zy * dx)
			dx = nd
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			nd = 2.0 * (zx * dx - zy * dy)
			dy = 2.0 * (zx * dy + zy * dx)
			dx = nd
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			nd = 2.0 * (zx * dx - zy * dy)
			dy = 2.0 * (zx * dy + zy * dx)
			dx = nd
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			nd = 2.0 * (zx * dx - zy * dy)
			dy = 2.0 * (zx * dy + zy * dx)
			dx = nd
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			k = k + 4
			if x2 + y2 >= 64.0 then break end
			local ex = zx - fx
			local ey = zy - fy
			e2 = ex * ex + ey * ey
			if e2 < trap then break end
		end
		local m2 = x2 + y2
		local d2 = dx * dx + dy * dy
		if m2 < 64.0 then
			local c = hot
			-- an orbit still in after stretching its pixel past the whole set sits on the boundary
			if d2 < unresolved then
				local band = k * drift - log(e2 + 0.000001, 2) * 1.7 + inphase
				c = inside + (itrunc(band + 1024.0) & 31)
			end
			out[x + 1] = c * 1025
		else
			local lg = log(m2, 2)
			local nu = k + 1.0 - log(lg * 0.5, 2)
			local unit = 1.0 / sqrt(m2 * d2 + 1.0e-300)
			-- the distance to the set, in pixels: |z| ln|z| / |z'|
			local de = m2 * unit * lg * 0.34657 * pixels
			-- the potential's gradient runs along z * conj(z')
			local nl = ((zx * dx + zy * dy) * lx + (zy * dx - zx * dy) * ly) * unit
			local glow = 1.0 / (1.0 + de * 0.3)
			local lit = 0.4 + 0.3 * glow + lift + (de < rim and 0.5 - de * rimfall or 0.0)
			local relief = nl * 0.2
			local hue = (itrunc(nu * 3.5 + phase) & 63) * 12
			local lv = itrunc((lit + relief) * 11.99)
			lv = lv < 0 and 0 or (lv > 11 and 11 or lv)
			local lm = itrunc((lit - relief) * 11.99)
			lm = lm < 0 and 0 or (lm > 11 and 11 or lm)
			out[x + 1] = hue + lv + (hue + lm) * 1024
		end
	end
	return out
end

Julia = setmetatable({}, {__index = Scene})
Julia.__index = Julia

function Julia.new()
	local self = Scene.init(setmetatable({}, Julia))
	self.tt = 0.0
	-- the path, in beats: over key_from..key_to a track glides to key_value. rho and theta are
	-- lambda's polar form, dive is octaves of zoom into the origin, spin radians of turn
	self.key_track = {}
	self.key_from = {}
	self.key_to = {}
	self.key_value = {}
	self.key_lands = {}
	return self
end

function Julia:hit_times() return {MUSIC_BAR * 3.0, MUSIC_BAR * 5.0, MUSIC_BAR * 8.0, MUSIC_BAR * 9.0} end
function Julia:hit_levels() return {2, 1, 2, 1} end
function Julia:length_bars() return 10 end

function Julia:glide(track, from, to, value)
	local i = #self.key_track + 1
	self.key_track[i] = track
	self.key_from[i] = from + 0.0
	self.key_to[i] = to + 0.0
	self.key_value[i] = value + 0.0
	self.key_lands[i] = false
end

-- a glide that leaves at full speed and settles: the move a hit throws
function Julia:land(track, from, to, value)
	self:glide(track, from, to, value)
	self.key_lands[#self.key_lands] = true
end

function Julia:build()
	self.name = "JULIA"
	self.handle = "one constant, every shape . by the type ladder"
	local pal = self.pal
	local night = {24.0, 12.0, 66.0}
	local stops = {{40.0, 30.0, 150.0}, {120.0, 50.0, 210.0}, {240.0, 50.0, 150.0}, {255.0, 130.0, 50.0},
		{255.0, 220.0, 110.0}, {80.0, 230.0, 220.0}, {30.0, 120.0, 235.0}, {40.0, 30.0, 150.0}}
	local ns = #stops - 1
	for hq = 0, JU_HUES - 1 do
		local f = hq * ns * 1.0 / JU_HUES
		local k = itrunc(f)
		local u = f - k
		local a = stops[k + 1]
		local b = stops[k + 2]
		-- a hue's ramp runs night, the hue in full, white hot
		for l = 0, JU_LEVELS - 1 do
			local lit = l / (JU_LEVELS - 1.0)
			local sat = lit * 1.8 > 1.0 and 1.0 or lit * 1.8
			local wh = lit > 0.75 and (lit - 0.75) * 3.0 or 0.0
			local rgb = {0.0, 0.0, 0.0}
			for ch = 1, 3 do
				local full = (a[ch] + (b[ch] - a[ch]) * u) * (0.5 + 0.5 * lit)
				local v = night[ch] + (full - night[ch]) * sat
				rgb[ch] = v + (255.0 - v) * wh
			end
			pal:add(rgb[1], rgb[2], rgb[3])
		end
	end
	JU_INSIDE = pal:gradient(JU_INHUES + 1, {{6.0, 4.0, 22.0}, {60.0, 18.0, 96.0}, {10.0, 8.0, 36.0}, {14.0, 62.0, 96.0}, {6.0, 4.0, 22.0}})

	-- a fat set swells four ears by bar 1, trades them for the rabbit's three by bar 2,
	-- and through the riser the rabbit starves to the six-armed dendrite past its bulb's tip
	self:glide(JU_THETA, 0.0, 2.5, 1.5708)
	self:glide(JU_RHO, 2.0, 4.0, 1.0625)
	self:glide(JU_RHO, 4.5, 5.5, 0.96)
	self:glide(JU_THETA, 5.0, 7.0, 2.0944)
	self:glide(JU_RHO, 6.5, 8.0, 1.107)
	self:glide(JU_RHO, 9.0, 12.0, 1.3283)
	self:glide(JU_THETA, 9.0, 12.0, 2.0866)
	self:glide(JU_DIVE, 9.0, 12.0, 0.7)
	-- the bar 3 hit dives into the junction at its heart and turns; the pull back fattens the
	-- arms and slides along the cardioid to San Marco, which the bar 5 accent twists into a dragon
	self:land(JU_DIVE, 12.0, 15.5, 3.7)
	self:land(JU_SPIN, 12.0, 20.0, 3.14159265)
	self:glide(JU_DIVE, 15.5, 17.5, 0.0)
	self:glide(JU_RHO, 16.5, 18.2, 1.0)
	self:glide(JU_THETA, 17.7, 19.7, 3.14159265)
	self:land(JU_RHO, 20.0, 21.5, 1.0723)
	self:land(JU_THETA, 20.0, 21.5, 3.0068)
	-- the basilica on bar 6, a fat set, the Siegel disk on bar 7
	self:glide(JU_RHO, 22.5, 24.0, 1.236)
	self:glide(JU_THETA, 22.5, 24.0, 3.14159265)
	self:glide(JU_RHO, 24.5, 26.0, 0.97)
	self:glide(JU_THETA, 25.5, 28.0, 3.8832)
	self:glide(JU_RHO, 26.5, 28.0, 1.0)
	-- the riser carries it up the quarter bulb to an eight-armed star; the bar 8 hit dives in
	self:glide(JU_THETA, 28.5, 30.3, 4.7124)
	self:glide(JU_RHO, 30.3, 32.0, 1.2679)
	self:glide(JU_THETA, 31.0, 32.0, 4.6648)
	self:glide(JU_DIVE, 29.0, 32.0, 0.6)
	self:land(JU_DIVE, 32.0, 35.5, 3.6)
	self:land(JU_SPIN, 32.0, 39.0, 6.2831853)
	-- bar 9 pulls back and the star fattens to the four-eared set, which holds the frame to the end
	self:glide(JU_DIVE, 36.0, 38.0, 0.0)
	self:glide(JU_THETA, 37.0, 39.0, 4.7124)
	self:glide(JU_RHO, 37.0, 39.0, 1.0625)
end

function Julia:update(t, dt)
	self.tt = t
end

function Julia:render(grid)
	local w, h, cue = self.w, self.h, self.cue
	local t = self.tt
	local beats = t / MUSIC_BEAT
	local kick = cue:kick_env(t)
	local snare = cue:snare_env(t)
	local hitv = cue:hit(t)
	local track = {0.92, 0.9, 0.0, 0.0}
	local key_track, key_from, key_to, key_value, key_lands = self.key_track, self.key_from, self.key_to, self.key_value, self.key_lands
	for i = 1, #key_from do
		local from = key_from[i]
		if beats > from then
			local k = beats >= key_to[i] and 1.0 or (beats - from) / (key_to[i] - from)
			local back = 1.0 - k
			if key_lands[i] then k = 1.0 - back * back * back else k = k * k * (3.0 - 2.0 * k) end
			local which = key_track[i] + 1
			track[which] = track[which] + (key_value[i] - track[which]) * k
		end
	end
	local rho = track[JU_RHO + 1]
	local theta = track[JU_THETA + 1]
	local dive = track[JU_DIVE + 1]
	local spin = track[JU_SPIN + 1] + 0.07 * sin(t * 0.9)
	local lr = rho * cos(theta)
	local li = rho * sin(theta)
	JU_FX = lr * 0.5
	JU_FY = li * 0.5
	JU_CA = lr * 0.5 - (lr * lr - li * li) * 0.25
	JU_CB = li * 0.5 - lr * li * 0.5
	-- near alpha the map is e -> lambda e + e^2, a contraction while |e| < 1 - rho
	local home = rho < 1.0 and (1.0 - rho) * 0.6 or 0.0
	JU_TRAP = home * home
	JU_DRIFT = rho < 1.0 and 3.4 * log(rho, 2) or 0.0
	-- beta = 1 - alpha is the set's far tip: the set spans beta to -beta
	local bx = 1.0 - JU_FX
	local by = 0.0 - JU_FY
	local reach = sqrt(bx * bx + by * by)
	local half_width = reach * 1.2
	half_width = half_width < 1.4 and 1.4 or half_width
	local scale = half_width * 2.0 / w * (1.0 - kick * 0.05) / (2.0 ^ dive)
	local turn = atan2(by, bx) + spin - 0.6 * sin(theta)
	local cs = cos(turn)
	local sn = sin(turn)
	JU_UX = cs * scale
	JU_UY = sn * scale
	JU_VX = 0.0 - sn * scale
	JU_VY = cs * scale
	JU_PIXELS = 1.0 / scale
	-- the critical orbit's mean stretch tells a set with an inside (it shrinks) from a dendrite (it grows)
	local ox = JU_CA
	local oy = JU_CB
	local stretch = 0.0
	local steps = 0
	while steps < 48 and ox * ox + oy * oy < 64.0 do
		stretch = stretch + log(4.0 * (ox * ox + oy * oy) + 0.000000000001)
		local nx = ox * ox - oy * oy + JU_CA
		oy = 2.0 * ox * oy + JU_CB
		ox = nx
		steps = steps + 1
	end
	local thin = steps > 0 and stretch * 0.5 / steps - 0.06 or 1.0
	thin = thin < 0.0 and 0.0 or (thin > 0.25 and 0.25 or thin)
	JU_UNRESOLVED = JU_PIXELS * JU_PIXELS * (2.0 ^ (10.0 - 400.0 * thin))
	JU_LX = -0.6 * cs + 0.8 * sn
	JU_LY = -0.6 * sn - 0.8 * cs
	JU_PHASE = 4096.0 + t * 9.0 + cue:chord(t) * 5.0
	JU_INPHASE = t * 4.0
	JU_LIFT = kick * 0.1 + snare * 0.06 + hitv * 0.14
	JU_RIM = 1.1 + kick * 0.5 + hitv * 1.5
	JU_W = w
	JU_H = h
	local rows_drawn = (h + 1) // 2
	if #JU_ROWS ~= rows_drawn then
		JU_ROWS = {}
		for r = 0, rows_drawn - 1 do JU_ROWS[r + 1] = r end
	end
	local rows = parallel_transform(JU_ROWS, julia_row)
	local mirror = w * h + 1
	local idx = 1
	for y = 1, rows_drawn do
		local row = rows[y]
		for x = 1, w do
			local v = row[x]
			grid[idx] = v & 1023
			grid[mirror - idx] = v >> 10
			idx = idx + 1
		end
	end
end

SCENES[2] = Julia
