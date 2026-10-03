-- Gator Quest (Lua port) - quests and the journal. A quest is 0 (not taken), 1 (active) or 99 (done).

local CHARS, QUESTS, ROLES, RELICS = CHARS, QUESTS, ROLES, RELICS
local MOB_WORLD, MOB_X, MOB_Y = MOB_WORLD, MOB_X, MOB_Y
local rnd, ev, say_to, sound_at, fx_at = rnd, ev, say_to, sound_at, fx_at
local pack_has, pack_count, pack_take, pack_add, make_item, item_name, drop_near =
	pack_has, pack_count, pack_take, pack_add, make_item, item_name, drop_near
local alen, apush, split = alen, apush, split
local EV_BANNER, FX_GOLD = Ev.banner, Fx.gold
local Q_SIX_SCALES = Q_SIX_SCALES

local function quest_ensure(slot)
	local c = CHARS[slot]
	while alen(c.quest_stage) < QUEST_N do apush(c.quest_stage, 0) end
	while alen(c.quest_count) < QUEST_N do apush(c.quest_count, 0) end
end

local function quest_active(slot, q) return CHARS[slot].quest_stage[q] == 1 end

local function relics_held(slot)
	local n = 0
	for i = 0, alen(RELICS) - 1 do
		if pack_has(slot, RELICS[i], 1) then n = n + 1 end
	end
	return n
end

-- progress toward the goal (for the journal and turn-in)
local function quest_progress(slot, q)
	local d = QUESTS[q]
	local c = CHARS[slot]
	if q == Q_SIX_SCALES then return relics_held(slot) end
	if d.type == 1 then return pack_count(slot, d.target) end
	if d.type == 3 then
		local n = 0
		for i = 0, alen(c.visited) - 1 do n = n + c.visited[i] end
		return n
	end
	return c.quest_count[q]
end

local function quest_goal(q) if q == Q_SIX_SCALES then return 6 end return QUESTS[q].count end

local function quest_ready(slot, q) return quest_active(slot, q) and quest_progress(slot, q) >= quest_goal(q) end

local function quest_bump(slot, q, by)
	local c = CHARS[slot]
	if not quest_active(slot, q) then return end
	local before = c.quest_count[q]
	c.quest_count[q] = c.quest_count[q] + by
	c.version = c.version + 1
	local qd = QUESTS[q]
	if before < qd.count and c.quest_count[q] >= qd.count then
		say_to(slot, "Quest complete: " .. qd.name .. ". Return to " .. ROLES[qd.giver].name .. ".", C_YELLOW)
		sound_at(MOB_WORLD[c.mob], MOB_X[c.mob], MOB_Y[c.mob], "quest")
	elseif c.quest_count[q] < qd.count then
		say_to(slot, qd.name .. ": " .. c.quest_count[q] .. "/" .. qd.count, C_GRAY)
	end
end

function quest_on_kill(slot, kind)
	quest_ensure(slot)
	for q = 0, QUEST_N - 1 do
		local d = QUESTS[q]
		if (d.type == 0 or d.type == 2) and d.target == kind then quest_bump(slot, q, 1) end
	end
end

function quest_on_visit(slot)
	quest_ensure(slot)
	for q = 0, QUEST_N - 1 do
		if QUESTS[q].type == 3 and quest_ready(slot, q) then
			say_to(slot, "Quest complete: " .. QUESTS[q].name .. ". Return to " .. ROLES[QUESTS[q].giver].name .. ".", C_YELLOW)
		end
	end
end

function quest_on_craft(slot)
	quest_ensure(slot)
	for q = 0, QUEST_N - 1 do
		if QUESTS[q].type == 4 then quest_bump(slot, q, 1) end
	end
end

function quest_on_fish(slot)
	quest_ensure(slot)
	for q = 0, QUEST_N - 1 do
		if QUESTS[q].type == 5 then quest_bump(slot, q, 1) end
	end
end

-- quests an npc role can hand out right now (-1 = none)
local function quest_offer(slot, role)
	quest_ensure(slot)
	local c = CHARS[slot]
	for q = 0, QUEST_N - 1 do
		local d = QUESTS[q]
		if not (d.giver ~= role or c.quest_stage[q] ~= 0) and not (d.needs >= 0 and c.quest_stage[d.needs] ~= 99) then
			return q
		end
	end
	return -1
end

-- talking quest business with an npc: turn in anything finished, else take the next offer
function quest_talk(slot, role)
	quest_ensure(slot)
	local c = CHARS[slot]
	for q = 0, QUEST_N - 1 do
		local d = QUESTS[q]
		if d.giver == role and quest_ready(slot, q) then
			if d.type == 1 then
				local item = q == Q_SIX_SCALES and -1 or d.target
				if item >= 0 then pack_take(slot, item, d.count)
				else
					for i = 0, alen(RELICS) - 1 do pack_take(slot, RELICS[i], 1) end
				end
			end
			c.quest_stage[q] = 99
			c.gold = c.gold + d.gold
			if d.reward >= 0 then
				local it = make_item(d.reward, d.reward_qty)
				if not pack_add(slot, it) then drop_near(it, MOB_WORLD[c.mob], MOB_X[c.mob], MOB_Y[c.mob], slot, 3000) end
			end
			c.fame = c.fame + 50 + d.gold // 2
			c.version = c.version + 1
			say_to(slot, ROLES[role].name .. ": \"" .. d.done .. "\"", C_WHITE)
			local reward = d.reward >= 0 and (" and " .. item_name(make_item(d.reward, d.reward_qty))) or ""
			say_to(slot, "Quest turned in: " .. d.name .. ". You receive " .. d.gold .. " gold" .. reward .. ".", C_YELLOW)
			ev(EV_BANNER, -1, 0, 0, -4, 0, 0, slot, "QUEST COMPLETE")
			sound_at(MOB_WORLD[c.mob], MOB_X[c.mob], MOB_Y[c.mob], "quest")
			fx_at(MOB_WORLD[c.mob], MOB_X[c.mob], MOB_Y[c.mob], FX_GOLD, 16)
			if q == Q_SIX_SCALES then
				for s = 0, CHAR_N - 1 do
					if CHARS[s].active then say_to(s, c.name .. " has become the GATOR AVATAR! All hail!", C_YELLOW) end
				end
			end
			return
		end
	end
	local offer = quest_offer(slot, role)
	if offer >= 0 then
		c.quest_stage[offer] = 1
		c.quest_count[offer] = 0
		c.version = c.version + 1
		say_to(slot, ROLES[role].name .. ": \"" .. QUESTS[offer].offer .. "\"", C_WHITE)
		say_to(slot, "New quest: " .. QUESTS[offer].name .. " (see your journal, J).", C_YELLOW)
		sound_at(MOB_WORLD[c.mob], MOB_X[c.mob], MOB_Y[c.mob], "chime")
		return
	end
	for q = 0, QUEST_N - 1 do
		if QUESTS[q].giver == role and quest_active(slot, q) then
			say_to(slot, ROLES[role].name .. ": \"How goes " .. QUESTS[q].name .. "? " .. quest_progress(slot, q) .. "/" .. quest_goal(q) .. ".\"", C_WHITE)
			return
		end
	end
	local lines, nl = split(ROLES[role].lines, ";")
	say_to(slot, ROLES[role].name .. ": \"" .. lines[rnd(nl)] .. "\"", C_WHITE)
end

-- journal lines for the HUD: active quests first, then finished ones
function journal_lines(slot)
	quest_ensure(slot)
	local out = {}
	local n = 0
	local c = CHARS[slot]
	for q = 0, QUEST_N - 1 do
		if c.quest_stage[q] == 1 then
			local mark = quest_ready(slot, q) and "[DONE] " or ""
			out[n] = mark .. QUESTS[q].name .. " " .. quest_progress(slot, q) .. "/" .. quest_goal(q) .. " - " .. ROLES[QUESTS[q].giver].name
			n = n + 1
		end
	end
	for q = 0, QUEST_N - 1 do
		if c.quest_stage[q] == 99 then
			out[n] = "(done) " .. QUESTS[q].name
			n = n + 1
		end
	end
	return out, n
end

for name, fn in pairs({ quest_ensure = quest_ensure, quest_active = quest_active, relics_held = relics_held,
	quest_progress = quest_progress, quest_goal = quest_goal, quest_ready = quest_ready, quest_bump = quest_bump,
	quest_offer = quest_offer }) do
	_G[name] = fn
end
