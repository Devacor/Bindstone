-- The showcase look's light: a grid of light texels, LIGHT_SUB to a tile edge,
-- each holding a light variant (a channel and a level, 0 unlit). Ceiling lamps
-- and the glowing fixtures (the exit switch, the lock lights) are baked when a
-- floor loads or a door opens; the muzzle flash, projectiles and blasts are
-- stamped over the bake every frame. Light flows through open tiles only, so it
-- rounds corners and stops at walls. Surfaces read their texel and look the lit
-- color up in a table per variant (look.lua builds them).

local sqrt, floor = math.sqrt, math.floor
local move = table.move

LIGHT_SUB = 4
LIGHT_LEVELS = 4
LIGHT_LAMP = 0
LIGHT_FIRE = 1
LIGHT_HEX = 2
LIGHT_ACID = 3
LIGHT_RED = 4
LIGHT_BLUE = 5
LIGHT_CHANNELS = 6
LIGHT_VARIANTS = 25     -- unlit + channels x levels

-- ordered dither over the light texels: light falls off in a fine stipple, not in rings
BAYER4 = {0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375,
	0.4375, 0.8125, 0.3125}

function light_level(variant)
	if variant == 0 then return 0 end
	return (variant - 1) % LIGHT_LEVELS + 1
end

-- the four steps between neighboring tiles, in the reference's order
local NEIGHBOR_X = {1, -1, 0, 0}
local NEIGHBOR_Y = {0, 0, 1, -1}

-- a light for one frame
function light_source(x, y, channel, radius, power)
	return {x = x, y = y, channel = channel, radius = radius, power = power}
end

LightGrid = {}
LightGrid.__index = LightGrid

function LightGrid.new()
	return setmetatable({
		gw = 0,            -- texels across
		gh = 0,
		grid = {},         -- this frame's light, [gy * gw + gx + 1]
		lamps = {},        -- per tile: a lamp hangs over it
		baked = {},
		baked_gen = -1,
		seen = {},         -- per tile: the stamp that last reached it
		stamp_id = 0,
		queue = {},
	}, LightGrid)
end

-- a floor's lamps: open tiles, LAMP_SPACING apart, picked in a fixed order from a hash
function LightGrid:load()
	local g = G
	local mw, mh = g.mw, g.mh
	self.gw = mw * LIGHT_SUB
	self.gh = mh * LIGHT_SUB
	local lamps = {}
	local seen = {}
	for i = 1, mw * mh do
		lamps[i] = false
		seen[i] = 0
	end
	self.lamps = lamps
	self.seen = seen
	local tiles = g.tiles
	for ty = 1, mh - 2 do
		for tx = 1, mw - 2 do
			if tiles[ty * mw + tx + 1] == 0 and texel_noise(11, tx, ty) % 3 == 0 and not self:lamp_near(tx, ty) then
				lamps[ty * mw + tx + 1] = true
			end
		end
	end
	self.baked_gen = -1
end

-- the bake, redone when a door or a secret opens
function LightGrid:frame(sources)
	local mapgen = G.mapgen
	if self.baked_gen ~= mapgen then
		self:bake()
		self.baked_gen = mapgen
	end
	local grid = self.grid
	move(self.baked, 1, self.gw * self.gh, 1, grid)
	for i = 1, #sources do
		local s = sources[i]
		self:stamp(grid, s.x, s.y, s.channel, s.radius, s.power)
	end
end

function LightGrid:at(x, y)
	local gx = trunc(x * LIGHT_SUB)
	local gy = trunc(y * LIGHT_SUB)
	if gx < 0 or gy < 0 or gx >= self.gw or gy >= self.gh then return 0 end
	return self.grid[gy * self.gw + gx + 1]
end

function LightGrid:lamp_near(tx, ty)
	local g = G
	local mw, mh = g.mw, g.mh
	local lamps = self.lamps
	for y = imax(0, ty - LAMP_SPACING + 1), imin(mh, ty + LAMP_SPACING) - 1 do
		for x = imax(0, tx - LAMP_SPACING + 1), imin(mw, tx + LAMP_SPACING) - 1 do
			if lamps[y * mw + x + 1] then return true end
		end
	end
	return false
end

function LightGrid:bake()
	local g = G
	local mw, mh = g.mw, g.mh
	local baked = {}
	for i = 1, self.gw * self.gh do baked[i] = 0 end
	self.baked = baked
	local tiles = g.tiles
	local lamps = self.lamps
	for ty = 0, mh - 1 do
		for tx = 0, mw - 1 do
			local t = tiles[ty * mw + tx + 1]
			if lamps[ty * mw + tx + 1] then self:stamp(baked, tx + 0.5, ty + 0.5, LIGHT_LAMP, LAMP_RADIUS, LAMP_POWER) end
			local channel = -1
			if t == 6 then channel = LIGHT_ACID elseif t == 8 then channel = LIGHT_RED elseif t == 9 then channel = LIGHT_BLUE end
			if channel >= 0 then
				for k = 1, 4 do
					local sx, sy = NEIGHBOR_X[k], NEIGHBOR_Y[k]
					if tile_at(tx + sx, ty + sy) == 0 then
						self:stamp(baked, tx + 0.5 + sx * 0.55, ty + 0.5 + sy * 0.55, channel, 2.4, 0.75)
					end
				end
			end
		end
	end
end

-- light from (lx, ly) over the open tiles a walk of the radius reaches; a
-- texel keeps the brighter of what it had and what this light gives it
function LightGrid:stamp(into, lx, ly, channel, radius, power)
	local g = G
	local mw = g.mw
	local sx = trunc(lx)
	local sy = trunc(ly)
	if tile_at(sx, sy) ~= 0 then return end
	local stamp_id = self.stamp_id + 1
	self.stamp_id = stamp_id
	local seen = self.seen
	local queue = self.queue
	local gw = self.gw
	local bayer = BAYER4
	local steps = trunc(radius) + 1
	local qn = 1
	queue[1] = sy * mw + sx
	seen[sy * mw + sx + 1] = stamp_id
	local head = 1
	local inv_r = 1.0 / radius
	local base = 1 + channel * LIGHT_LEVELS
	while head <= qn do
		local tile = queue[head]
		head = head + 1
		local tx = tile % mw
		local ty = tile // mw
		for sv = 0, LIGHT_SUB - 1 do
			local wy = ty + (sv + 0.5) / LIGHT_SUB - ly
			local row = (ty * LIGHT_SUB + sv) * gw + tx * LIGHT_SUB
			for su = 0, LIGHT_SUB - 1 do
				local wx = tx + (su + 0.5) / LIGHT_SUB - lx
				local d = sqrt(wx * wx + wy * wy) * inv_r
				if d < 1.0 then
					local f = power * (1.0 - d) * (1.0 - d * 0.5)
					local level = floor(f * LIGHT_LEVELS + bayer[((ty * LIGHT_SUB + sv) & 3) * 4 + ((tx * LIGHT_SUB + su) & 3) + 1])
					if level > 0 then
						if level > LIGHT_LEVELS then level = LIGHT_LEVELS end
						local i = row + su + 1
						if level >= light_level(into[i]) then into[i] = base + level - 1 end
					end
				end
			end
		end
		if iabs(tx - sx) + iabs(ty - sy) < steps then
			for k = 1, 4 do
				local nx = tx + NEIGHBOR_X[k]
				local ny = ty + NEIGHBOR_Y[k]
				if tile_at(nx, ny) == 0 then
					local nxt = ny * mw + nx
					if seen[nxt + 1] ~= stamp_id then
						seen[nxt + 1] = stamp_id
						qn = qn + 1
						queue[qn] = nxt
					end
				end
			end
		end
	end
end
