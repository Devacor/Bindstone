-- Gator Quest (Lua port) - small pure helpers: the sim RNG, hashing, value noise, direction tables,
-- and the C-semantics integer helpers the port needs (JaiScript / truncates, % keeps the dividend's sign).

DX = { [0] = 0, 1, 1, 1, 0, -1, -1, -1 }
DY = { [0] = -1, -1, 0, 1, 1, 1, 0, -1 }
DIR_NAME = { [0] = "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest" }

local DX, DY = DX, DY

-- truncating integer division (C / on integers); Lua's // floors
local function idiv(a, b)
	local q = a // b
	if q < 0 and q * b ~= a then q = q + 1 end
	return q
end
-- C remainder (sign of the dividend): Lua 5.4's math.fmod on two integers
local imod = math.fmod

local function iabs(v) if v < 0 then return -v end return v end
local function imin(a, b) if a < b then return a end return b end
local function imax(a, b) if a > b then return a end return b end
local function iclamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
local function isign(v) if v < 0 then return -1 elseif v > 0 then return 1 end return 0 end
local function cheb(x0, y0, x1, y1)
	local a = x0 - x1
	if a < 0 then a = -a end
	local b = y0 - y1
	if b < 0 then b = -b end
	if a > b then return a end
	return b
end
local function dist2(x0, y0, x1, y1) return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1) end

local function dir_of(dx, dy)
	local sx, sy = isign(dx), isign(dy)
	for d = 0, 7 do
		if DX[d] == sx and DY[d] == sy then return d end
	end
	return 4
end

-- the sim RNG: xorshift32 kept in a 64-bit integer (masked)
local RNG_S = 0
local function rng_next()
	local s = RNG_S
	s = s ~ ((s << 13) & 0xFFFFFFFF)
	s = s ~ (s >> 17)
	s = s ~ ((s << 5) & 0xFFFFFFFF)
	RNG_S = s
	return s
end
local function rnd(n)
	if n <= 1 then return 0 end
	return rng_next() % n
end
local function rnd_range(lo, hi)
	if hi <= lo then return lo end
	return lo + rnd(hi - lo + 1)
end
local function chance(permille) return rnd(1000) < permille end

function rng_seed(seed)
	RNG_S = (seed & 0x7FFFFFFF) ~ 0x2545F491
	if RNG_S == 0 then RNG_S = 1 end
	for _ = 0, 7 do rng_next() end
end
function rng_state() return RNG_S end
function rng_set(s) RNG_S = s end

-- stateless hashing (worldgen, visual variants)
local function hash3(x, y, z)
	local h = (x * 374761393 + y * 668265263 + (z & 0x7FFFFFFF) * 1442695041) & 0xFFFFFFFF
	h = ((h ~ (h >> 13)) * 1274126177) & 0xFFFFFFFF
	return h ~ (h >> 16)
end

-- FNV-style fold for the state hash, 32-bit lane
local function mix32(h, v)
	local x = h ~ (v & 0xFFFFFFFF)
	x = x ~ ((v >> 32) & 0xFFFFFFFF)
	return ((x & 0xFFFFFFFF) * 16777619) & 0xFFFFFFFF
end

function mix_str(h, s)
	local n = #s
	for i = 1, n do h = mix32(h, string.byte(s, i)) end
	return mix32(h, n)
end

-- value noise, fixed point 0..1023, bilinear over a lattice of `cell` tiles (x, y >= 0)
local function vnoise(x, y, cell, salt)
	local gx = x // cell
	local gy = y // cell
	local fx = (x % cell) * 1024 // cell
	local fy = (y % cell) * 1024 // cell
	fx = fx * fx * (3072 - 2 * fx) // 1048576
	fy = fy * fy * (3072 - 2 * fy) // 1048576
	local a = hash3(gx, gy, salt) & 1023
	local b = hash3(gx + 1, gy, salt) & 1023
	local c = hash3(gx, gy + 1, salt) & 1023
	local d = hash3(gx + 1, gy + 1, salt) & 1023
	local top = a + idiv((b - a) * fx, 1024)
	local bot = c + idiv((d - c) * fx, 1024)
	return top + idiv((bot - top) * fy, 1024)
end

local function fractal(x, y, salt)
	return (vnoise(x, y, 24, salt) * 4 + vnoise(x, y, 11, salt + 17) * 2 + vnoise(x, y, 5, salt + 31)) // 7
end

function plural(n, one, many) if n == 1 then return one end return many end

function cap_first(s)
	if #s == 0 then return s end
	return string.upper(string.sub(s, 1, 1)) .. string.sub(s, 2)
end

-- "a sword" / "an apple"
function article(name)
	if #name == 0 then return name end
	local c = string.byte(name, 1)
	if c == 97 or c == 101 or c == 105 or c == 111 or c == 117 or c == 65 or c == 69 or c == 73 or c == 79 or c == 85 then return "an " .. name end
	return "a " .. name
end

-- skill tenths -> "57.3" (non-negative)
function tenths(v) return (v // 10) .. "." .. (v % 10) end

-- JaiScript string.split(sep): exact separator, empties kept; a 0-based array and its size
function split(s, sep)
	local out, n, pos = {}, 0, 1
	local ls = #sep
	while true do
		local i = string.find(s, sep, pos, true)
		if not i then
			out[n] = string.sub(s, pos)
			return out, n + 1
		end
		out[n] = string.sub(s, pos, i - 1)
		n = n + 1
		pos = i + ls
	end
end

-- JaiScript string.to_int(def): a whole decimal integer (surrounding whitespace allowed) or def
function to_int(s, def)
	local v = string.match(s, "^%s*([+-]?%d+)%s*$")
	if v == nil then return def end
	return math.tointeger(tonumber(v)) or def
end

function itrunc(v)
	if v >= 0 then return math.floor(v) end
	return math.ceil(v)
end

for name, fn in pairs({ idiv = idiv, imod = imod, iabs = iabs, imin = imin, imax = imax, iclamp = iclamp, isign = isign,
	cheb = cheb, dist2 = dist2, dir_of = dir_of, rng_next = rng_next, rnd = rnd, rnd_range = rnd_range, chance = chance,
	hash3 = hash3, mix32 = mix32, vnoise = vnoise, fractal = fractal }) do
	_G[name] = fn
end
