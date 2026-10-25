-- Large-entity simulation over a spatial hash: 100k entities (sized past the caches on purpose), a cell -> bucket map
-- rebuilt every step, 3x3-cell neighbour queries steering each entity away from its crowd, bouncing position updates.
local ENTITIES = 100000
local STEPS = 4
local CELL = 1024
local GRID = 256
local WORLD = CELL * GRID
local RADIUS = 1024
local MAX_SPEED = 64

local Rng = {}
Rng.__index = Rng

function Rng.new(seed)
	return setmetatable({state = seed}, Rng)
end

function Rng:next()
	self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
	return self.state
end

local function sign(value)
	if value > 0 then
		return 1
	end
	if value < 0 then
		return -1
	end
	return 0
end

local function clamp_speed(value)
	if value > MAX_SPEED then
		return MAX_SPEED
	end
	if value < -MAX_SPEED then
		return -MAX_SPEED
	end
	return value
end

local function build_buckets(entities)
	local buckets = {}
	for i = 1, #entities do
		local e = entities[i]
		local key = (e.y // CELL) * GRID + e.x // CELL
		local bucket = buckets[key]
		if bucket == nil then
			buckets[key] = {i}
		else
			bucket[#bucket + 1] = i
		end
	end
	return buckets
end

local function steer(entities, buckets)
	local radius_squared = RADIUS * RADIUS
	local contacts = 0
	for i = 1, #entities do
		local e = entities[i]
		local cx = e.x // CELL
		local cy = e.y // CELL
		local push_x = 0
		local push_y = 0
		for gy = math.max(cy - 1, 0), math.min(cy + 1, GRID - 1) do
			for gx = math.max(cx - 1, 0), math.min(cx + 1, GRID - 1) do
				local bucket = buckets[gy * GRID + gx]
				if bucket ~= nil then
					for k = 1, #bucket do
						local j = bucket[k]
						if j ~= i then
							local other = entities[j]
							local dx = e.x - other.x
							local dy = e.y - other.y
							if dx * dx + dy * dy < radius_squared then
								contacts = contacts + 1
								push_x = push_x + dx
								push_y = push_y + dy
							end
						end
					end
				end
			end
		end
		e.vx = clamp_speed(e.vx + sign(push_x))
		e.vy = clamp_speed(e.vy + sign(push_y))
	end
	return contacts
end

local function move(entities)
	for i = 1, #entities do
		local e = entities[i]
		e.x = e.x + e.vx
		if e.x < 0 then
			e.x = -e.x
			e.vx = -e.vx
		elseif e.x >= WORLD then
			e.x = 2 * (WORLD - 1) - e.x
			e.vx = -e.vx
		end
		e.y = e.y + e.vy
		if e.y < 0 then
			e.y = -e.y
			e.vy = -e.vy
		elseif e.y >= WORLD then
			e.y = 2 * (WORLD - 1) - e.y
			e.vy = -e.vy
		end
	end
end

local start = os.clock()
local rng = Rng.new(20261024)
local entities = {}
for i = 1, ENTITIES do
	local x = rng:next() % WORLD
	local y = rng:next() % WORLD
	local vx = rng:next() % (2 * MAX_SPEED + 1) - MAX_SPEED
	local vy = rng:next() % (2 * MAX_SPEED + 1) - MAX_SPEED
	entities[i] = {x = x, y = y, vx = vx, vy = vy}
end
local result = 0
for _ = 1, STEPS do
	local buckets = build_buckets(entities)
	result = (result * 31 + steer(entities, buckets)) & 0xFFFFFFFF
	move(entities)
end
for i = 1, #entities do
	local e = entities[i]
	result = (result * 31 + e.x) & 0xFFFFFFFF
	result = (result * 31 + e.y) & 0xFFFFFFFF
	result = (result * 31 + e.vx) & 0xFFFFFFFF
	result = (result * 31 + e.vy) & 0xFFFFFFFF
end
local elapsed = (os.clock() - start) * 1000.0
print(string.format("result %d", result))
print(string.format("time_ms %.3f", elapsed))
