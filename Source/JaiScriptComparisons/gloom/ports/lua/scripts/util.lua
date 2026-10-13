-- Small helpers: math, hashing, the truecolor palette intern, text padding.

TICK = 1.0 / 30.0   -- fixed sim timestep; --smoke feeds exactly this dt

local floor, ceil = math.floor, math.ceil

-- C-cast truncation toward zero (same result as the bound host itrunc, without
-- the C++ round-trip in hot loops). math.floor/ceil return Lua INTEGERS.
function trunc(v)
	if v >= 0 then return floor(v) end
	return ceil(v)
end

-- int/int division truncating toward zero (Lua // floors); for the few sites
-- where the numerator can be negative.
function idivt(a, b)
	local q = a // b
	if q < 0 and q * b ~= a then q = q + 1 end
	return q
end

function iabs(v) if v < 0 then return -v end return v end
function imax(a, b) if a > b then return a end return b end
function imin(a, b) if a < b then return a end return b end
function iclamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
function fclamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end

-- FNV-1a, 32-bit lane inside an int64 (every folded lane is non-negative, so
-- Lua's logical >> matches the reference's arithmetic shift)
function mix32(h, v)
	local x = h ~ (v & 0xFFFFFFFF)
	x = x ~ ((v >> 32) & 0xFFFFFFFF)
	return ((x & 0xFFFFFFFF) * 16777619) & 0xFFFFFFFF
end

-- ------------------------------------------------ truecolor palette intern --
-- Interns exact 24-bit colors: pixel grids store small palette indices (1-based
-- here), the row builder emits prebuilt truecolor escapes, and PAL_LUM feeds
-- the quadrant/sextant bright/dark cell partition.
PAL_KEYS = {}    -- r<<16|g<<8|b -> index
PAL_FG = {}      -- ESC[38;2;r;g;bm per index
PAL_BG = {}      -- ESC[48;2;r;g;bm per index
PAL_LUM = {}     -- 2r+3g+b per index

function rgb_idx(r, g, b)
	if r < 0 then r = 0 elseif r > 255 then r = 255 end
	if g < 0 then g = 0 elseif g > 255 then g = 255 end
	if b < 0 then b = 0 elseif b > 255 then b = 255 end
	local key = r * 65536 + g * 256 + b
	local idx = PAL_KEYS[key]
	if idx then return idx end
	idx = #PAL_FG + 1
	PAL_KEYS[key] = idx
	PAL_FG[idx] = ESC .. "[38;2;" .. r .. ";" .. g .. ";" .. b .. "m"
	PAL_BG[idx] = ESC .. "[48;2;" .. r .. ";" .. g .. ";" .. b .. "m"
	PAL_LUM[idx] = r * 2 + g * 3 + b
	return idx
end

function col(code, text)
	return ESC .. "[" .. code .. "m" .. text .. ESC .. "[0m"
end

-- pad/truncate plain text to n columns
function pad_to(s, n)
	if #s > n then return s:sub(1, n) end
	return s .. string.rep(" ", n - #s)
end

-- smallest signed angle from a to b, in (-pi, pi]
function ang_diff(a, b)
	local d = b - a
	while d > 3.14159265 do d = d - 6.2831853 end
	while d < -3.14159265 do d = d + 6.2831853 end
	return d
end

function dist2(ax, ay, bx, by)
	local dx = ax - bx
	local dy = ay - by
	return dx * dx + dy * dy
end
