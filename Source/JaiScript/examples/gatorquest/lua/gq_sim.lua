-- Gator Quest (Lua port) - the simulation core's front door: world creation, the fixed tick,
-- weather, reagent foraging, and the state hash.

local CHARS, WORLDS, MONS, REAGENTS, MON_MOVE = CHARS, WORLDS, MONS, REAGENTS, MON_MOVE
local W_WEATHER, W_WEATHER_T = W_WEATHER, W_WEATHER_T
local SP_W, SP_X, SP_Y, SP_KIND, SP_MAX, SP_RAD, SP_DELAY, SP_NEXT, SP_ALIVE = SP_W, SP_X, SP_Y, SP_KIND, SP_MAX, SP_RAD, SP_DELAY, SP_NEXT, SP_ALIVE
local MOB_KIND, MOB_SER, MOB_WORLD, MOB_X, MOB_Y, MOB_HP, MOB_FLAGS, MOB_TARGET, MOB_SPAWNER, MOB_HOMEX, MOB_HOMEY =
	MOB_KIND, MOB_SER, MOB_WORLD, MOB_X, MOB_Y, MOB_HP, MOB_FLAGS, MOB_TARGET, MOB_SPAWNER, MOB_HOMEX, MOB_HOMEY
local GI_ITEM, GI_W, GI_X, GI_Y = GI_ITEM, GI_W, GI_X, GI_Y
local IN_MOVE, IN_QUEUE, IN_STEP_SEQ, IN_STEP_DIR, IN_ACK, IN_DROPPED = IN_MOVE, IN_QUEUE, IN_STEP_SEQ, IN_STEP_DIR, IN_ACK, IN_DROPPED
local imax, cheb, rnd, rnd_range, mix32 = imax, cheb, rnd, rnd_range, mix32
local in_world, is_land, walk_at, town_at, mob_at, mob_live, mob_spawn, say_to, make_item, gi_add =
	in_world, is_land, walk_at, town_at, mob_at, mob_live, mob_spawn, say_to, make_item, gi_add
local alen, apush, aclear = alen, apush, aclear
local players_tick, ai_tick, status_tick, spawners_tick, ground_tick, events_clear =
	players_tick, ai_tick, status_tick, spawners_tick, ground_tick, events_clear

TICK_MS = 100
DAY_TICKS = 3600
WEATHER_LETTERS = "crstfa"

local MOB_ARRAYS = { MOB_KIND, MOB_SER, MOB_WORLD, MOB_X, MOB_Y, MOB_PX, MOB_PY, MOB_MOVE_T, MOB_MOVE_D, MOB_DIR, MOB_HP,
	MOB_HPMAX, MOB_MP, MOB_MPMAX, MOB_TARGET, MOB_AI, MOB_NEXT, MOB_ATK, MOB_CAST, MOB_FLAGS, MOB_PLAYER, MOB_SPAWNER,
	MOB_HOMEX, MOB_HOMEY, MOB_POISON, MOB_BURN, MOB_SLOW, MOB_BUFF, MOB_HIT_T, MOB_OWNER, MOB_AUX, MOB_ROLE, MOB_LEVEL }

local function sim_reset()
	TICK = 0
	NEXT_SER = 1
	MOB_LIVE = 0
	for i = 1, #MOB_ARRAYS do aclear(MOB_ARRAYS[i]) end
	MOB_N = 0
	aclear(GI_ITEM); aclear(GI_W); aclear(GI_X); aclear(GI_Y); aclear(GI_SER); aclear(GI_EXPIRE); aclear(GI_OWNER)
	GI_N = 0
	aclear(CHARS)
	CHAR_N = 0
	aclear(IN_MOVE); aclear(IN_QUEUE); aclear(IN_STEP_SEQ); aclear(IN_STEP_DIR); aclear(IN_ACK); aclear(IN_DROPPED); aclear(CHAR_ARCHIVE)
	CAMERA_W = -1
	events_clear()
end

-- the initial fill ignores the "no spawning next to players" rule (there are none yet)
local function spawners_tick_force()
	for s = 0, SP_N - 1 do
		if SP_ALIVE[s] < SP_MAX[s] then
			local w = SP_W[s]
			local kind = SP_KIND[s]
			for _ = 0, 5 do
				local x = SP_X[s] + rnd_range(-SP_RAD[s], SP_RAD[s])
				local y = SP_Y[s] + rnd_range(-SP_RAD[s], SP_RAD[s])
				if MONS[kind].boss then x = SP_X[s]; y = SP_Y[s] end
				if in_world(w, x, y) and mob_at(w, x, y) < 0 then
					local k = walk_at(w, x, y)
					local mv = MON_MOVE[kind]
					local ok
					if mv == 4 then ok = k ~= 0
					elseif mv == 2 then ok = k == 2
					else ok = k == 1 or k == 6 or k == 4 or (k == 2 and mv == 3) end
					if ok then
						local m = mob_spawn(kind, w, x, y)
						MOB_SPAWNER[m] = s
						MOB_HOMEX[m] = SP_X[s]
						MOB_HOMEY[m] = SP_Y[s]
						SP_ALIVE[s] = SP_ALIVE[s] + 1
						SP_NEXT[s] = SP_DELAY[s]
						break
					end
				end
			end
		end
	end
end

function sim_init(seed)
	sim_reset()
	world_generate(seed)
	populate_npcs()
	rng_seed(seed * 31 + 7)
	for w = 0, WORLD_COUNT - 1 do
		W_WEATHER[w] = 0
		W_WEATHER_T[w] = 600 + rnd(1200)
	end
	-- fill the spawners once so the world starts inhabited
	for pass = 0, 2 do
		for s = 0, SP_N - 1 do SP_NEXT[s] = 0 end
		local saved = TICK
		TICK = pass * 10
		spawners_tick_force()
		TICK = saved
	end
	WORLD_READY = true
end

function time_of_day() return TICK % DAY_TICKS end
function day_number() return TICK // DAY_TICKS + 1 end
function hour_of_day() return (time_of_day() * 24 // DAY_TICKS + 7) % 24 end

local WEATHER_MSG = { [0] = "The sky clears.", "It starts to rain.", "Snow begins to fall.", "Thunder rolls across the sky!", "A thick fog rolls in.", "Ash drifts down from above." }

local function weather_tick()
	if TICK % 50 ~= 0 then return end
	for w = 0, WORLD_COUNT - 1 do
		if TICK >= W_WEATHER_T[w] then
			local opts = WORLDS[w].weather
			local r = rnd(#opts)
			local pick = (string.find(WEATHER_LETTERS, string.sub(opts, r + 1, r + 1), 1, true) or 0) - 1
			if pick < 0 then pick = 0 end
			if pick ~= W_WEATHER[w] then
				W_WEATHER[w] = pick
				for s = 0, CHAR_N - 1 do
					local m = CHARS[s].mob
					if CHARS[s].active and mob_live(m) and MOB_WORLD[m] == w then say_to(s, WEATHER_MSG[pick], C_GRAY) end
				end
			end
			W_WEATHER_T[w] = TICK + 1500 + rnd(2000)
		end
	end
end

-- wild reagents sprout where players roam
local function forage_tick()
	if TICK % 120 ~= 0 then return end
	for s = 0, CHAR_N - 1 do
		local m = CHARS[s].mob
		if CHARS[s].active and mob_live(m) then
			local w = MOB_WORLD[m]
			local x = MOB_X[m] + rnd_range(-14, 14)
			local y = MOB_Y[m] + rnd_range(-10, 10)
			if in_world(w, x, y) and is_land(w, x, y) and town_at(w, x, y) < 0 then
				local near = 0
				for i = 0, GI_N - 1 do
					if GI_ITEM[i].base >= 0 and GI_W[i] == w and cheb(GI_X[i], GI_Y[i], x, y) < 12 then near = near + 1 end
				end
				if near < 10 then
					local reg = REAGENTS[rnd(alen(REAGENTS))]
					gi_add(make_item(reg, 1 + rnd(2)), w, x, y, -1, 2400)
				end
			end
		end
	end
end

-- queue an intent for a slot (local keyboard, network, or a scripted bot)
function intent_move(slot, dir)
	if slot < alen(IN_MOVE) then IN_MOVE[slot] = dir end
end

function intent_act(slot, a)
	if slot < alen(IN_QUEUE) then apush(IN_QUEUE[slot], a) end
end

local function step_queue_ensure(slot)
	while alen(IN_STEP_SEQ) <= slot do
		apush(IN_STEP_SEQ, {})
		apush(IN_STEP_DIR, {})
		apush(IN_ACK, 0)
		apush(IN_DROPPED, 0)
	end
end

function intent_step(slot, seq, dir)
	step_queue_ensure(slot)
	if dir < 0 or dir > 7 or seq <= IN_ACK[slot] then return end
	if alen(IN_STEP_SEQ[slot]) >= 12 then
		IN_DROPPED[slot] = imax(IN_DROPPED[slot], seq)
		return
	end
	apush(IN_STEP_SEQ[slot], seq)
	apush(IN_STEP_DIR[slot], dir)
end

-- steps queued for another place are void after a teleport; they count as processed
function step_queue_flush(slot)
	step_queue_ensure(slot)
	local q = IN_STEP_SEQ[slot]
	for i = 0, alen(q) - 1 do IN_ACK[slot] = imax(IN_ACK[slot], q[i]) end
	IN_ACK[slot] = imax(IN_ACK[slot], IN_DROPPED[slot])
	IN_STEP_SEQ[slot] = {}
	IN_STEP_DIR[slot] = {}
end

function sim_tick()
	events_clear()
	TICK = TICK + 1
	players_tick()
	ai_tick()
	status_tick()
	spawners_tick()
	ground_tick()
	weather_tick()
	forage_tick()
end

-- ------------------------------------------------------------ the gate ---
function state_hash()
	local h = 2166136261
	h = mix32(h, TICK)
	h = mix32(h, rng_state())
	h = mix32(h, NEXT_SER)
	for m = 0, MOB_N - 1 do
		if MOB_KIND[m] >= 0 then
			h = mix32(h, MOB_KIND[m] * 1000003 + MOB_SER[m])
			h = mix32(h, MOB_WORLD[m] * 65536 + MOB_X[m] * 256 + MOB_Y[m])
			h = mix32(h, MOB_HP[m] * 64 + MOB_FLAGS[m])
			h = mix32(h, MOB_TARGET[m] + 1)
		end
	end
	for i = 0, GI_N - 1 do
		local it = GI_ITEM[i]
		if it.base >= 0 then
			h = mix32(h, it.base * 4096 + it.qty)
			h = mix32(h, GI_W[i] * 65536 + GI_X[i] * 256 + GI_Y[i])
		end
	end
	for s = 0, CHAR_N - 1 do
		local c = CHARS[s]
		if c.active then
			h = mix32(h, c.gold)
			h = mix32(h, c.fame)
			h = mix32(h, c.str * 10000 + c.dex * 100 + c.intel)
			for k = 0, alen(c.skill) - 1 do h = mix32(h, c.skill[k]) end
			for k = 0, alen(c.pack) - 1 do h = mix32(h, c.pack[k].base * 4096 + c.pack[k].qty) end
			for k = 0, alen(c.worn) - 1 do h = mix32(h, c.worn[k].base + 1) end
			for k = 0, alen(c.quest_stage) - 1 do h = mix32(h, c.quest_stage[k] * 256 + c.quest_count[k]) end
		end
	end
	for k = 0, SP_N - 1 do h = mix32(h, SP_ALIVE[k] * 131 + SP_NEXT[k]) end
	return h
end
