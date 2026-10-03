-- Gator Quest (Lua port) - the headless smoke session of scripts/gatorquest.jai.
--
--   lua gatorquest.lua smoke [ticks N] [seed S] [bots K] [verbose]
--
-- Prints the same world line, per-bot summaries and STATE_HASH as
--   jaiscript scripts/gatorquest.jai -- smoke ticks N seed S bots K noroundtrip
-- The mid-run save/load round trip is not ported: the port always runs as `noroundtrip`.
-- Requires Lua 5.4+ (64-bit integers, integer division and bitwise operators).

local dir = (arg and arg[0] or ""):match("^(.*[/\\])") or ""
package.path = dir .. "?.lua;" .. package.path

require("gq_enums")
require("gq_types")
require("gq_state")
require("gq_util")
require("gq_defs")
require("gq_data")
require("gq_world")
require("gq_mobs")
require("gq_items")
require("gq_combat")
require("gq_ai")
require("gq_magic")
require("gq_quests")
require("gq_actions")
require("gq_sim")
require("gq_bot")

local ARGS = {}
for i = 1, #arg do ARGS[i - 1] = arg[i] end
local NARGS = #arg

local function arg_value(key, def)
	for i = 0, NARGS - 2 do
		if ARGS[i] == key then return ARGS[i + 1] end
	end
	return def
end

local function arg_flag(key)
	for i = 0, NARGS - 1 do
		if ARGS[i] == key then return true end
	end
	return false
end

local function print_char_summary(slot)
	local c = CHARS[slot]
	local m = c.mob
	local where = mob_live(m) and (WORLDS[MOB_WORLD[m]].name .. " (" .. MOB_X[m] .. "," .. MOB_Y[m] .. ")") or "nowhere"
	print("  " .. c.name .. " " .. player_title(slot) .. " | hp " .. MOB_HP[m] .. "/" .. MOB_HPMAX[m] .. " gold " .. c.gold
		.. " fame " .. c.fame .. " kills " .. c.kills .. " deaths " .. c.deaths .. " | " .. where)
	local skills = ""
	for k = 0, alen(c.skill) - 1 do
		if c.skill[k] > 0 then skills = skills .. SKILL_KEY[k] .. " " .. tenths(c.skill[k]) .. "  " end
	end
	print("  skills: " .. skills)
	local pack = ""
	for k = 0, alen(c.pack) - 1 do pack = pack .. item_name(c.pack[k]) .. ", " end
	print("  pack: " .. pack)
	local worn = ""
	for k = 0, alen(c.worn) - 1 do
		if c.worn[k].base >= 0 then worn = worn .. SLOT_NAME[k] .. "=" .. item_name(c.worn[k]) .. ", " end
	end
	print("  worn: " .. worn)
	local j, jn = journal_lines(slot)
	for k = 0, jn - 1 do print("  journal: " .. j[k]) end
end

local function run_smoke()
	local ticks = to_int(arg_value("ticks", "3000"), 3000)
	local seed = to_int(arg_value("seed", "1"), 1)
	local bots = to_int(arg_value("bots", "2"), 2)
	local verbose = arg_flag("verbose")
	local t0 = os.clock()
	sim_init(seed)
	local t1 = os.clock()
	local gen_ms = math.floor((t1 - t0) * 1000)
	print("world: seed " .. seed .. ", " .. TILE_N .. " tiles in " .. WORLD_COUNT .. " realms, " .. MOB_LIVE .. " mobiles, "
		.. SP_N .. " spawners, " .. GATE_N .. " gates (gen " .. gen_ms .. " ms)")
	aclear(BOTS)
	BOT_N = 0
	local names = { [0] = "Chomps", "Snappy", "Gumbo", "Bayou Bill" }
	for b = 0, bots - 1 do
		player_create(b, names[b % 4], b)
		bot_add(b)
	end
	local logs = 0
	for _ = 0, ticks - 1 do
		bots_tick()
		sim_tick()
		if verbose then
			for e = 0, EV_N - 1 do
				if EV_KIND[e] == Ev.log and logs < 4000 then
					logs = logs + 1
					print("[" .. TICK .. "] " .. (EV_TO[e] >= 0 and CHARS[EV_TO[e]].name or "near") .. ": " .. EV_TEXT[e])
				end
			end
		end
	end
	local run_us = math.floor((os.clock() - t1) * 1000000)
	print("ran " .. ticks .. " ticks in " .. run_us // 1000 .. " ms (" .. run_us // imax(1, ticks) .. " us/tick), "
		.. MOB_LIVE .. " mobiles alive, kills " .. STAT_KILLS .. ", spawns " .. STAT_SPAWNS)
	for b = 0, bots - 1 do
		print_char_summary(b)
		print("  plan: " .. BOTS[b].log)
	end
	print("STATE_HASH: " .. state_hash())
	return 0
end

if arg_flag("smoke") then
	os.exit(run_smoke())
end
print("usage: lua gatorquest.lua smoke [ticks N] [seed S] [bots K] [verbose]")
os.exit(2)
