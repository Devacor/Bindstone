-- Gator Quest (Lua port) - the live world. Every array is created once and cleared in place
-- (aclear) where the script assigns [], so modules can keep them in locals. Arrays are 0-based;
-- the hot pools carry explicit sizes (MOB_N, GI_N, SP_N, EV_N, CHAR_N).

SEED = 0
TICK = 0
NEXT_SER = 0
WORLD_READY = false

-- terrain: every world's tiles in one flat array, W_OFF[w] = first index
W_OFF = {}
W_W = {}
W_H = {}
TILES = {}
TVAR = {}
REGROW = {}
OCC = {}
W_WEATHER = {}
W_WEATHER_T = {}
W_ARRIVE_X = {}
W_ARRIVE_Y = {}
W_BOSS_T = {}

-- places
GATE_W = {}
GATE_X = {}
GATE_Y = {}
GATE_DEST = {}
GATE_N = 0
TOWN_W = {}
TOWN_X0 = {}
TOWN_Y0 = {}
TOWN_X1 = {}
TOWN_Y1 = {}
TOWN_NAME = {}
TOWN_N = 0
CHEST_W = {}
CHEST_X = {}
CHEST_Y = {}
CHEST_TIER = {}
CHEST_T = {}
CHEST_N = 0

-- mobiles: parallel arrays, kind < 0 = free slot
MOB_KIND = {}
MOB_SER = {}
MOB_WORLD = {}
MOB_X = {}
MOB_Y = {}
MOB_PX = {}
MOB_PY = {}
MOB_MOVE_T = {}
MOB_MOVE_D = {}
MOB_DIR = {}
MOB_HP = {}
MOB_HPMAX = {}
MOB_MP = {}
MOB_MPMAX = {}
MOB_TARGET = {}
MOB_AI = {}
MOB_NEXT = {}
MOB_ATK = {}
MOB_CAST = {}
MOB_FLAGS = {}
MOB_PLAYER = {}
MOB_SPAWNER = {}
MOB_HOMEX = {}
MOB_HOMEY = {}
MOB_POISON = {}
MOB_BURN = {}
MOB_SLOW = {}
MOB_BUFF = {}
MOB_HIT_T = {}
MOB_OWNER = {}
MOB_AUX = {}
MOB_ROLE = {}
MOB_LEVEL = {}
MOB_N = 0
MOB_LIVE = 0

-- spawners
SP_W = {}
SP_X = {}
SP_Y = {}
SP_KIND = {}
SP_MAX = {}
SP_RAD = {}
SP_DELAY = {}
SP_NEXT = {}
SP_ALIVE = {}
SP_N = 0

-- items on the ground
GI_ITEM = {}
GI_W = {}
GI_X = {}
GI_Y = {}
GI_SER = {}
GI_EXPIRE = {}
GI_OWNER = {}
GI_N = 0

-- players (slot-indexed)
CHARS = {}
CHAR_N = 0
IN_MOVE = {}
IN_QUEUE = {}
CHAR_ARCHIVE = {}
IN_STEP_SEQ = {}
IN_STEP_DIR = {}
IN_ACK = {}
IN_DROPPED = {}

-- events produced by the current tick
EV_KIND = {}
EV_W = {}
EV_X = {}
EV_Y = {}
EV_A = {}
EV_B = {}
EV_C = {}
EV_TO = {}
EV_TEXT = {}
EV_N = 0

CAMERA_W = 0
CAMERA_X = 0
CAMERA_Y = 0

BOTS = {}
BOT_N = 0

STAT_KILLS = 0
STAT_SPAWNS = 0

-- 0-based array helpers
function alen(t)
	local n = #t
	if n == 0 and t[0] == nil then return 0 end
	return n + 1
end

function apush(t, v)
	local n = #t
	if n == 0 and t[0] == nil then t[0] = v else t[n + 1] = v end
end

function aremove(t, i)
	if i == 0 then
		local n = #t
		if n == 0 then t[0] = nil else t[0] = t[1]; table.remove(t, 1) end
	else
		table.remove(t, i)
	end
end

function aclear(t)
	for k in pairs(t) do t[k] = nil end
end

function areverse(t, n)
	local i, j = 0, n - 1
	while i < j do
		t[i], t[j] = t[j], t[i]
		i = i + 1
		j = j - 1
	end
end

function aindex_of(t, v)
	for i = 0, alen(t) - 1 do
		if t[i] == v then return i end
	end
	return -1
end

-- an array literal [a, b, ...] as a 0-based table
function arr(...)
	local t = { ... }
	local out = {}
	for i = 1, #t do out[i - 1] = t[i] end
	return out
end
