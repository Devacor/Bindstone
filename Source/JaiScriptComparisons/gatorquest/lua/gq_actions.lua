-- Gator Quest (Lua port) - what players can do. Every intent lands here as a short string, processed by the tick.

local CHARS, ITEMS, ROLES, TILES_DEF = CHARS, ITEMS, ROLES, TILES_DEF
local MOB_KIND, MOB_SER, MOB_WORLD, MOB_X, MOB_Y, MOB_PX, MOB_PY, MOB_MOVE_T, MOB_MOVE_D, MOB_DIR =
	MOB_KIND, MOB_SER, MOB_WORLD, MOB_X, MOB_Y, MOB_PX, MOB_PY, MOB_MOVE_T, MOB_MOVE_D, MOB_DIR
local MOB_HP, MOB_HPMAX, MOB_MP, MOB_MPMAX, MOB_TARGET, MOB_NEXT, MOB_ATK, MOB_FLAGS, MOB_PLAYER, MOB_OWNER =
	MOB_HP, MOB_HPMAX, MOB_MP, MOB_MPMAX, MOB_TARGET, MOB_NEXT, MOB_ATK, MOB_FLAGS, MOB_PLAYER, MOB_OWNER
local MOB_POISON, MOB_BURN, MOB_AUX, MOB_ROLE = MOB_POISON, MOB_BURN, MOB_AUX, MOB_ROLE
local IN_MOVE, IN_QUEUE, IN_STEP_SEQ, IN_STEP_DIR, IN_ACK, IN_DROPPED = IN_MOVE, IN_QUEUE, IN_STEP_SEQ, IN_STEP_DIR, IN_ACK, IN_DROPPED
local DX, DY = DX, DY
local imin, imax, cheb, rnd = imin, imax, cheb, rnd
local tile_now, ev, say_to, fx_at, sound_at = tile_now, ev, say_to, fx_at, sound_at
local mob_live, mob_ghost, mob_alive, mob_at, mob_step, los = mob_live, mob_ghost, mob_alive, mob_at, mob_step, los
local hostile, attack, attack_range, pack_has, pack_add, make_item, item_name = hostile, attack, attack_range, pack_has, pack_add, make_item, item_name
local alen, apush, aremove, split, to_int = alen, apush, aremove, split, to_int
local MF_HIDDEN, MF_SWIM = MF_HIDDEN, MF_SWIM
local SK, FX = Sk, Fx

ACT_FISH = 1
ACT_MINE = 2
ACT_CHOP = 3
ACT_BANDAGE = 4
ACT_CRAFT = 5
local ACT_FISH, ACT_MINE, ACT_CHOP, ACT_BANDAGE, ACT_CRAFT = ACT_FISH, ACT_MINE, ACT_CHOP, ACT_BANDAGE, ACT_CRAFT

local player_spawn, party_leave, npc_talk, open_chest, start_action, craft_finish, chat_command

-- ---------------------------------------------------------------- joining ---
function player_create(slot, name, hue)
	while CHAR_N <= slot do CHARS[CHAR_N] = Character(); CHAR_N = CHAR_N + 1 end
	while alen(IN_MOVE) <= slot do apush(IN_MOVE, -1) end
	while alen(IN_QUEUE) <= slot do apush(IN_QUEUE, {}) end
	local c = Character()
	c.active = true
	c.connected = true
	c.name = name
	c.hue = hue
	c.str = 35
	c.dex = 25
	c.intel = 25
	for i = 0, SK.count - 1 do c.skill[i] = 0 end
	c.skill[SK.swords] = 300
	c.skill[SK.wrestling] = 300
	c.skill[SK.tactics] = 250
	c.skill[SK.swimming] = 400
	c.skill[SK.fishing] = 200
	c.skill[SK.magery] = 250
	c.skill[SK.healing] = 200
	c.skill[SK.mining] = 100
	c.skill[SK.lumber] = 100
	c.skill[SK.cooking] = 150
	for i = 0, Slot.count - 1 do c.worn[i] = Item() end
	for i = 0, SPELL_N - 1 do c.spells[i] = 0 end
	c.spells[0] = 1
	c.spells[1] = 1
	for i = 0, WORLD_COUNT - 1 do c.visited[i] = 0 end
	c.visited[0] = 1
	c.gold = 50
	c.home_world = 0
	c.home_x = W_ARRIVE_X[0]
	c.home_y = W_ARRIVE_Y[0]
	CHARS[slot] = c
	quest_ensure(slot)
	c.worn[Slot.weapon] = make_item(I_DAGGER, 1)
	c.worn[Slot.body] = make_item(I_LEATHERTUNIC, 1)
	c.worn[Slot.head] = make_item(I_STRAWHAT, 1)
	c.worn[Slot.feet] = make_item(I_SANDALS, 1)
	pack_add(slot, make_item(I_SPELLBOOK, 1))
	pack_add(slot, make_item(I_MINORHEAL, 3))
	pack_add(slot, make_item(I_BANDAGE, 8))
	pack_add(slot, make_item(I_FISHPOLE, 1))
	pack_add(slot, make_item(I_GARLIC, 6))
	pack_add(slot, make_item(I_GINSENG, 6))
	pack_add(slot, make_item(I_ASH, 8))
	pack_add(slot, make_item(I_BREAD, 3))
	player_spawn(slot)
	return slot
end

-- put the character's gator into the world at its home
player_spawn = function(slot)
	local c = CHARS[slot]
	local px, py = find_free_spot(c.home_world, c.home_x, c.home_y, 8, LAND_MASK)
	local m = mob_spawn(M_GATOR_NPC, c.home_world, px, py)
	MOB_PLAYER[m] = slot
	MOB_FLAGS[m] = 0
	MOB_ROLE[m] = -1
	c.mob = m
	player_refresh(slot)
	MOB_HP[m] = MOB_HPMAX[m]
	MOB_MP[m] = MOB_MPMAX[m]
	if c.pvp then MOB_FLAGS[m] = MOB_FLAGS[m] | MF_PVP end
	fx_at(c.home_world, px, py, FX.warp, 16)
end

function player_leave(slot)
	if slot < 0 or slot >= CHAR_N or not CHARS[slot].active then return end
	local c = CHARS[slot]
	local m = c.mob
	for i = 0, MOB_N - 1 do
		if MOB_KIND[i] >= 0 and MOB_OWNER[i] == m and (MOB_FLAGS[i] & MF_SUMMON) ~= 0 then mob_free(i) end
	end
	if mob_live(m) then
		fx_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m], FX.smoke, 10)
		MOB_PLAYER[m] = -1
		mob_free(m)
	end
	for s = 0, CHAR_N - 1 do
		if CHARS[s].invite == slot then CHARS[s].invite = -1 end
	end
	party_leave(slot)
	c.mob = -1
	c.active = false
	c.connected = false
	IN_MOVE[slot] = -1
	IN_QUEUE[slot] = {}
end

-- ------------------------------------------------------------- movement ---
local function facing_x(m) return MOB_X[m] + DX[MOB_DIR[m]] end
local function facing_y(m) return MOB_Y[m] + DY[MOB_DIR[m]] end

local function fall_from_sky(m)
	local slot = MOB_PLAYER[m]
	say_to(slot, "AAAAAAAAAAAA-", C_RED)
	ev(Ev.banner, -1, 0, 0, -5, 0, 0, slot, "WHEEEEEE!")
	local x = rnd_range(20, W_W[0] - 20)
	local y = rnd_range(15, W_H[0] - 15)
	travel(m, 0, x, y)
	sound_at(0, MOB_X[m], MOB_Y[m], "boom")
	fx_at(0, MOB_X[m], MOB_Y[m], FX.splash, 20)
	deal(m, MOB_HPMAX[m] // 4, -1, DMG_PHYS, false)
	say_to(slot, "-AAAAAA! You plummet out of the clouds and land in Gatorland with a SPLAT.", C_RED)
end

-- stepping onto a tile triggers what the tile does
local function on_enter(m)
	local w = MOB_WORLD[m]
	local x = MOB_X[m]
	local y = MOB_Y[m]
	local t = tile_now(w, x, y)
	local st = TILES_DEF[t].station
	local slot = MOB_PLAYER[m]
	if st == 7 or st == 8 then
		for g = 0, GATE_N - 1 do
			if GATE_W[g] == w and GATE_X[g] == x and GATE_Y[g] == y then
				local dest = GATE_DEST[g]
				local px, py = gate_arrival(w, dest)
				travel(m, dest, px, py)
				return
			end
		end
	end
	if T_WALK[t] == 3 and slot >= 0 then fall_from_sky(m); return end
	if T_WALK[t] == 4 and slot >= 0 and not mob_ghost(m) then
		-- ice: keep sliding the way you were going
		MOB_AUX[m] = MOB_DIR[m] + 1
	elseif slot >= 0 then
		MOB_AUX[m] = 0
	end
	if t == T_GOLDPILE and slot >= 0 and REGROW[W_OFF[w] + y * W_W[w] + x] <= TICK then
		local amount = 20 + WORLDS[w].tier * 25 + rnd(40)
		CHARS[slot].gold = CHARS[slot].gold + amount
		CHARS[slot].version = CHARS[slot].version + 1
		REGROW[W_OFF[w] + y * W_W[w] + x] = TICK + 6000
		say_to(slot, "You scoop up " .. amount .. " gold from the glittering pile!", C_YELLOW)
		fx_at(w, x, y, FX.gold, 18)
		sound_at(w, x, y, "coin")
	end
end

-- a player moving into a tile: attack what is there, talk, open, or step
local function player_move(slot, dir)
	local c = CHARS[slot]
	local m = c.mob
	if not mob_live(m) or TICK < MOB_NEXT[m] then return end
	local w = MOB_WORLD[m]
	local nx = MOB_X[m] + DX[dir]
	local ny = MOB_Y[m] + DY[dir]
	MOB_DIR[m] = dir
	c.action = 0
	local o = mob_at(w, nx, ny)
	if o >= 0 and o ~= m then
		if mob_ghost(m) then
			local role = MOB_ROLE[o]
			if role >= 0 and ROLES[role].heals then
				say_to(slot, ROLES[role].name .. ": \"Back to the land of the living with you, dear.\"", C_WHITE)
				player_resurrect(slot, 60)
				MOB_NEXT[m] = TICK + 4
				return
			end
		elseif hostile(m, o) then
			MOB_TARGET[m] = o
			if TICK >= MOB_ATK[m] and attack_range(m) >= 1 then attack(m, o) end
			MOB_NEXT[m] = TICK + 2
			return
		elseif MOB_ROLE[o] >= 0 then
			-- bumping a shopkeeper greets them once; leaning on them does not repeat it
			if c.talk_npc ~= o or TICK - c.talk_t > 60 then
				c.talk_npc = o
				npc_talk(slot, o)
			end
			c.talk_t = TICK
			MOB_NEXT[m] = TICK + 4
			return
		elseif MOB_PLAYER[o] >= 0 or mob_fac(o) == Fac.npc or mob_fac(o) == Fac.guard then
			-- swap places with friendly folk so nobody gets stuck in a doorway
			if MOB_PLAYER[o] < 0 and mob_can_enter(o, MOB_X[m], MOB_Y[m]) == false then
				local ox = MOB_X[o]
				local oy = MOB_Y[o]
				occ_clear(o)
				occ_clear(m)
				MOB_X[o] = MOB_X[m]
				MOB_Y[o] = MOB_Y[m]
				MOB_PX[o] = MOB_X[o]
				MOB_PY[o] = MOB_Y[o]
				occ_set(o)
				MOB_PX[m] = MOB_X[m]
				MOB_PY[m] = MOB_Y[m]
				MOB_X[m] = ox
				MOB_Y[m] = oy
				MOB_MOVE_T[m] = TICK
				MOB_MOVE_D[m] = 2
				MOB_NEXT[m] = TICK + 2
				occ_set(m)
				return
			end
			MOB_NEXT[m] = TICK + 1
			return
		end
	end
	local t = tile_now(w, nx, ny)
	if TILES_DEF[t].station == 9 and not mob_ghost(m) then open_chest(slot, w, nx, ny); MOB_NEXT[m] = TICK + 3; return end
	if TILES_DEF[t].station == 6 and mob_ghost(m) then
		say_to(slot, "The shrine's light washes over your ghost.", C_WHITE)
		player_resurrect(slot, 40)
		MOB_NEXT[m] = TICK + 4
		return
	end
	if mob_step(m, dir) then
		-- stealth: moving while hidden needs a check
		if (MOB_FLAGS[m] & MF_HIDDEN) ~= 0 then
			if not skill_check(slot, SK.stealth, 300) then
				MOB_FLAGS[m] = MOB_FLAGS[m] & ~MF_HIDDEN
				say_to(slot, "You have been revealed!", C_ORANGE)
			end
		end
		if (MOB_FLAGS[m] & MF_SWIM) ~= 0 and rnd(12) == 0 then skill_gain(slot, SK.swimming, true) end
		on_enter(m)
	end
end

-- ------------------------------------------------------------------ npcs ---
npc_talk = function(slot, npc)
	local role = MOB_ROLE[npc]
	if role < 0 then return end
	local m = CHARS[slot].mob
	MOB_DIR[npc] = dir_of(MOB_X[m] - MOB_X[npc], MOB_Y[m] - MOB_Y[npc])
	quest_talk(slot, role)
	ev(Ev.dialog, MOB_WORLD[npc], MOB_X[npc], MOB_Y[npc], MOB_SER[npc], role, 0, slot, ROLES[role].name)
end

local function find_npc_ser(ser)
	for i = 0, MOB_N - 1 do
		if MOB_KIND[i] >= 0 and MOB_SER[i] == ser then return i end
	end
	return -1
end

local function npc_in_reach(slot, npc)
	local m = CHARS[slot].mob
	return mob_live(npc) and MOB_WORLD[npc] == MOB_WORLD[m] and cheb(MOB_X[m], MOB_Y[m], MOB_X[npc], MOB_Y[npc]) <= 3
end

local function buy_price(item) return imax(1, ITEMS[item].value * 11 // 10) end
local function sell_price(it)
	local v = idiv(item_value(it), 3)
	if it.base == I_GOLDCARP then v = 750 end
	return imax(1, v)
end

local function shop_buy(slot, npc, index)
	if not npc_in_reach(slot, npc) then return end
	local role = MOB_ROLE[npc]
	local shop = role >= 0 and ROLES[role].shop or -1
	if shop < 0 or index < 0 or index >= alen(SHOPS[shop]) then return end
	local item = SHOPS[shop][index]
	local price = buy_price(item)
	local c = CHARS[slot]
	if c.gold < price then say_to(slot, ROLES[role].name .. ": \"That is " .. price .. " gold, sugar. You are short.\"", C_RED); return end
	local qty = ITEMS[item].kind == Kind.ammo and 20 or 1
	if qty > 1 then price = price * qty // 2 end
	if c.gold < price then qty = 1; price = buy_price(item) end
	local it = make_item(item, qty)
	if not pack_add(slot, it) then say_to(slot, "Your backpack is full.", C_RED); return end
	c.gold = c.gold - price
	c.version = c.version + 1
	say_to(slot, "You buy " .. item_name(it) .. " for " .. price .. " gold.", C_GREEN)
	sound_at(MOB_WORLD[npc], MOB_X[npc], MOB_Y[npc], "coin")
end

local function shop_sell(slot, npc, index)
	if not npc_in_reach(slot, npc) then return end
	local role = MOB_ROLE[npc]
	if role < 0 or ROLES[role].shop < 0 then return end
	local c = CHARS[slot]
	if index < 0 or index >= alen(c.pack) then return end
	local one = c.pack[index]
	if ITEMS[one.base].kind == Kind.quest or one.base == I_SPELLBOOK then say_to(slot, "You would rather keep that.", C_GRAY); return end
	local sold = pack_take_at(slot, index, 1)
	local price = sell_price(sold)
	c.gold = c.gold + price
	if sold.base == I_GOLDCARP then
		say_to(slot, ROLES[role].name .. ": \"THE GOLDEN CARP! I am weeping! Take this fortune!\"", C_YELLOW)
		fx_at(MOB_WORLD[npc], MOB_X[npc], MOB_Y[npc], FX.gold, 30)
	end
	say_to(slot, "You sell " .. item_name(sold) .. " for " .. price .. " gold.", C_GREEN)
	sound_at(MOB_WORLD[npc], MOB_X[npc], MOB_Y[npc], "coin")
end

local function healer_service(slot, npc)
	if not npc_in_reach(slot, npc) then return end
	local role = MOB_ROLE[npc]
	if role < 0 or not ROLES[role].heals then return end
	local c = CHARS[slot]
	local m = c.mob
	if mob_ghost(m) then player_resurrect(slot, 60); return end
	if MOB_HP[m] >= MOB_HPMAX[m] and MOB_POISON[m] == 0 then say_to(slot, ROLES[role].name .. ": \"You look healthy as a hog, dear.\"", C_WHITE); return end
	if c.gold < 15 then say_to(slot, ROLES[role].name .. ": \"Fifteen gold for a healing, dear.\"", C_RED); return end
	c.gold = c.gold - 15
	MOB_POISON[m] = 0
	heal_mob(m, MOB_HPMAX[m], npc)
	fx_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m], FX.heal, 14)
	sound_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m], "heal")
	say_to(slot, ROLES[role].name .. " heals your wounds for 15 gold.", C_GREEN)
	c.version = c.version + 1
end

local function bank_deposit(slot, npc, index)
	if not npc_in_reach(slot, npc) or MOB_ROLE[npc] < 0 or not ROLES[MOB_ROLE[npc]].banks then return end
	local c = CHARS[slot]
	if index == -1 then
		c.bank_gold = c.bank_gold + c.gold
		say_to(slot, "You deposit " .. c.gold .. " gold. Balance: " .. c.bank_gold .. ".", C_GREEN)
		c.gold = 0
	elseif index >= 0 and index < alen(c.pack) then
		if alen(c.bank) >= 60 then say_to(slot, "Your bank box is full.", C_RED); return end
		local it = c.pack[index]
		aremove(c.pack, index)
		apush(c.bank, it)
		say_to(slot, "You place " .. item_name(it) .. " in your bank box.", C_GREEN)
	end
	c.version = c.version + 1
	sound_at(MOB_WORLD[npc], MOB_X[npc], MOB_Y[npc], "coin")
end

local function bank_withdraw(slot, npc, index)
	if not npc_in_reach(slot, npc) or MOB_ROLE[npc] < 0 or not ROLES[MOB_ROLE[npc]].banks then return end
	local c = CHARS[slot]
	if index == -1 then
		c.gold = c.gold + c.bank_gold
		say_to(slot, "You withdraw " .. c.bank_gold .. " gold.", C_GREEN)
		c.bank_gold = 0
	elseif index >= 0 and index < alen(c.bank) then
		if alen(c.pack) >= PACK_MAX then say_to(slot, "Your backpack is full.", C_RED); return end
		local it = c.bank[index]
		aremove(c.bank, index)
		apush(c.pack, it)
		say_to(slot, "You take " .. item_name(it) .. " from your bank box.", C_GREEN)
	end
	c.version = c.version + 1
end

-- ---------------------------------------------------------------- chests ---
local GEMS = { [0] = "Amethyst", "Ruby", "Sapphire", "Emerald", "Diamond" }
open_chest = function(slot, w, x, y)
	for k = 0, CHEST_N - 1 do
		if CHEST_W[k] == w and CHEST_X[k] == x and CHEST_Y[k] == y then
			if CHEST_T[k] > TICK then say_to(slot, "The chest is empty. Someone has been here recently.", C_GRAY); return end
			local tier = CHEST_TIER[k]
			CHEST_T[k] = TICK + 4000
			local luck = player_luck(slot)
			say_to(slot, "You pry open the chest...", C_YELLOW)
			sound_at(w, x, y, "chest")
			drop_near(make_item(I_GOLD, 15 * tier + rnd(30 * tier)), w, x, y + 1, -1, 3000)
			drop_near(roll_equipment(imin(5, tier + 1), luck, tier >= 3 and 1 or 0), w, x, y + 1, -1, 3000)
			local i = 0
			while i < 1 + rnd(3) do   -- the script re-rolls the bound every iteration
				drop_near(roll_consumable(tier), w, x, y + 1, -1, 3000)
				i = i + 1
			end
			if rnd(100) < 10 * tier then drop_near(make_item(iid(GEMS[imin(4, tier - 1)]), 1), w, x, y + 1, -1, 3000) end
			fx_at(w, x, y, FX.gold, 14)
			return
		end
	end
end

-- ------------------------------------------------------------ gathering ---
local JUNK
local function fish_for(w, t, skill)
	local r = rnd(1000)
	if r < 60 then
		JUNK = JUNK or { [0] = I_OLDBOOT, I_TINCAN, I_SOGGYHAT, I_BOTTLE }
		return JUNK[rnd(4)]
	end
	if t == T_LAVA then return I_LAVAEEL end
	if t == T_SKY then return I_CLOUDTROUT end
	if w == 1 and skill >= 500 and rnd(1000) < 8 + (skill - 500) // 20 then return I_GOLDCARP end
	if w == 1 or t == T_SWAMP then return rnd(2) == 0 and I_CATFISH or I_RAWFISH end
	if w == 3 then return I_GHOSTFISH end
	if w == 4 then return I_ICEFISH end
	if w == 2 then return rnd(3) == 0 and I_KELP or I_RAWFISH end
	return rnd(3) == 0 and I_CATFISH or I_RAWFISH
end

-- use your facing tile: fish, mine, chop; or talk to who is there; or open a chest
local function interact(slot)
	local c = CHARS[slot]
	local m = c.mob
	if mob_ghost(m) then say_to(slot, "You are a ghost. Seek a healer or a shrine.", C_GRAY); return end
	local w = MOB_WORLD[m]
	for k = 0, 8 do
		local dir = k == 0 and MOB_DIR[m] or k - 1
		local x = MOB_X[m] + DX[dir]
		local y = MOB_Y[m] + DY[dir]
		local o = mob_at(w, x, y)
		if o >= 0 and MOB_ROLE[o] >= 0 then MOB_DIR[m] = dir; npc_talk(slot, o); return end
		local t = tile_now(w, x, y)
		local td = TILES_DEF[t]
		local res = td.res
		if t == T_LAVA and c.skill[SK.fishing] >= 600 then res = 4 end
		if t == T_SKY and w == 6 then res = 4 end
		if td.station == 9 then MOB_DIR[m] = dir; open_chest(slot, w, x, y); return end
		if not (res == 4 and k > 0 and not pack_has(slot, I_FISHPOLE, 1)) then
			if res == 4 then
				if not pack_has(slot, I_FISHPOLE, 1) then say_to(slot, "You need a fishing pole.", C_GRAY); return end
				start_action(slot, ACT_FISH, x, y, 25 + rnd(20), 0)
				MOB_DIR[m] = dir
				say_to(slot, "You cast your line...", C_CYAN)
				sound_at(w, x, y, "splash")
				return
			end
			if res == 2 or res == 3 or res == 5 then
				if not pack_has(slot, I_PICKAXE, 1) then say_to(slot, "You need a pickaxe to mine that.", C_GRAY); return end
				start_action(slot, ACT_MINE, x, y, 20, 0)
				MOB_DIR[m] = dir
				say_to(slot, "You start swinging your pickaxe.", C_CYAN)
				return
			end
			if res == 1 then
				if not pack_has(slot, I_HATCHET, 1) then say_to(slot, "You need a hatchet to chop that.", C_GRAY); return end
				start_action(slot, ACT_CHOP, x, y, 20, 0)
				MOB_DIR[m] = dir
				say_to(slot, "You start chopping.", C_CYAN)
				return
			end
			if td.station == 6 and k <= 8 then say_to(slot, "The shrine hums. \"" .. quest_progress(slot, Q_SIX_SCALES) .. " of 6 Relic Scales.\"", C_YELLOW); return end
		end
	end
	say_to(slot, "There is nothing here to use.", C_GRAY)
end

start_action = function(slot, kind, x, y, ticks, arg)
	local c = CHARS[slot]
	c.action = kind
	c.action_x = x
	c.action_y = y
	c.action_t = TICK + ticks
	c.action_arg = arg
	c.version = c.version + 1
end

local MINE_GEMS = { [0] = "Amethyst", "Sapphire", "Diamond" }
local function finish_action(slot)
	local c = CHARS[slot]
	local m = c.mob
	local w = MOB_WORLD[m]
	local kind = c.action
	c.action = 0
	c.version = c.version + 1
	local x = c.action_x
	local y = c.action_y
	local t = tile_now(w, x, y)
	if kind == ACT_FISH then
		if not skill_check(slot, SK.fishing, 150 + WORLDS[w].tier * 80) then
			say_to(slot, "The fish got away.", C_GRAY)
			return
		end
		local fish = fish_for(w, t, c.skill[SK.fishing])
		local it = make_item(fish, 1)
		if not pack_add(slot, it) then drop_near(it, w, MOB_X[m], MOB_Y[m], slot, 3000) end
		fx_at(w, x, y, FX.splash, 10)
		sound_at(w, x, y, "splash")
		if fish == I_GOLDCARP then
			say_to(slot, "!!! You reel in the legendary GOLDEN CARP !!!", C_YELLOW)
			ev(Ev.banner, -1, 0, 0, -4, 0, 0, slot, "GOLDEN CARP!")
			fx_at(w, x, y, FX.gold, 30)
		elseif ITEMS[fish].kind == Kind.junk then
			say_to(slot, "You fish up " .. article(ITEMS[fish].name) .. ". Hooray?", C_GRAY)
		else
			say_to(slot, "You catch " .. article(ITEMS[fish].name) .. "!", C_GREEN)
			c.fish_caught = c.fish_caught + 1
			quest_on_fish(slot)
		end
		return
	end
	if kind == ACT_MINE then
		local res = TILES_DEF[t].res
		if res ~= 2 and res ~= 3 and res ~= 5 then return end
		local tier = WORLDS[w].tier
		if not skill_check(slot, SK.mining, 100 + tier * 90 + (res == 3 and 100 or 0)) then
			say_to(slot, "You chip away but find nothing useful.", C_GRAY)
			fx_at(w, x, y, FX.sparks, 4)
			sound_at(w, x, y, "clink")
			return
		end
		local got = I_IRONORE
		local sk = c.skill[SK.mining]
		if res == 5 then
			if rnd(3) == 0 then got = iid("Frost Shard") else got = iid(MINE_GEMS[rnd(3)]) end
		elseif res == 3 then
			if tier >= 4 and sk >= 650 and rnd(3) == 0 then got = I_MITHRILORE
			elseif tier >= 2 and sk >= 300 and rnd(2) == 0 then got = I_SILVERORE end
		end
		local qty = ITEMS[got].kind == Kind.gem and 1 or 1 + rnd(2)
		local it = make_item(got, qty)
		if not pack_add(slot, it) then drop_near(it, w, MOB_X[m], MOB_Y[m], slot, 3000) end
		say_to(slot, "You dig up " .. item_name(it) .. ".", C_GREEN)
		fx_at(w, x, y, FX.sparks, 8)
		sound_at(w, x, y, "clink")
		if rnd(4) == 0 then REGROW[W_OFF[w] + y * W_W[w] + x] = TICK + 3000; say_to(slot, "The vein is exhausted.", C_GRAY) end
		return
	end
	if kind == ACT_CHOP then
		if TILES_DEF[t].res ~= 1 then return end
		if not skill_check(slot, SK.lumber, 150) then
			say_to(slot, "You hack at the tree but get no usable wood.", C_GRAY)
			sound_at(w, x, y, "chop")
			return
		end
		local it = make_item(I_LOG, 2 + rnd(3))
		if not pack_add(slot, it) then drop_near(it, w, MOB_X[m], MOB_Y[m], slot, 3000) end
		say_to(slot, "You chop " .. item_name(it) .. ".", C_GREEN)
		fx_at(w, x, y, FX.dust, 8)
		sound_at(w, x, y, "chop")
		if rnd(3) == 0 then
			REGROW[W_OFF[w] + y * W_W[w] + x] = TICK + 3000
			say_to(slot, "TIMBER! The tree comes crashing down.", C_ORANGE)
			shake(slot, 3)
		end
		return
	end
	if kind == ACT_BANDAGE then
		if not pack_take(slot, I_BANDAGE, 1) then return end
		local amount = 8 + c.skill[SK.healing] // 25 + rnd(6)
		if skill_check(slot, SK.healing, 200) then
			heal_mob(m, amount, m)
			if MOB_POISON[m] > 0 and c.skill[SK.healing] >= 600 then MOB_POISON[m] = 0; say_to(slot, "You cure the poison.", C_GREEN) end
			say_to(slot, "You finish applying the bandage.", C_GREEN)
		else
			heal_mob(m, amount // 3, m)
			say_to(slot, "Your fingers slip; the bandage barely helps.", C_GRAY)
		end
		return
	end
	if kind == ACT_CRAFT then craft_finish(slot, c.action_arg) end
end

local function actions_tick(slot)
	local c = CHARS[slot]
	if c.action ~= 0 and TICK >= c.action_t then finish_action(slot) end
end

-- ------------------------------------------------------------- crafting ---
local function near_station(slot, station)
	if station == 0 then return true end
	local m = CHARS[slot].mob
	local w = MOB_WORLD[m]
	for dy = -2, 2 do
		for dx = -2, 2 do
			if TILES_DEF[tile_now(w, MOB_X[m] + dx, MOB_Y[m] + dy)].station == station then return true end
		end
	end
	return false
end

local function recipe_ok(slot, r, verbose)
	local d = RECIPES[r]
	local c = CHARS[slot]
	if c.skill[d.skill] < d.min then
		if verbose then say_to(slot, "You need " .. tenths(d.min) .. " " .. SKILL_NAME[d.skill] .. " to make that.", C_GRAY) end
		return false
	end
	if not near_station(slot, d.station) then
		if verbose then say_to(slot, "You need to be near a " .. STATION_NAME[d.station] .. ".", C_GRAY) end
		return false
	end
	if d.tool >= 0 and not pack_has(slot, d.tool, 1) then
		if verbose then say_to(slot, "You need " .. article(ITEMS[d.tool].name) .. ".", C_GRAY) end
		return false
	end
	if d.skill == SK.smithing and d.station == 2 and not pack_has(slot, I_HAMMER, 1) then
		if verbose then say_to(slot, "You need a smith hammer.", C_GRAY) end
		return false
	end
	for k = 0, d.in_n - 1 do
		if not pack_has(slot, d.in_item[k], d.in_qty[k]) then
			if verbose then say_to(slot, "You need " .. d.in_qty[k] .. " " .. ITEMS[d.in_item[k]].name .. ".", C_GRAY) end
			return false
		end
	end
	return true
end

local function craft_start(slot, r)
	if r < 0 or r >= RECIPE_N or mob_ghost(CHARS[slot].mob) then return end
	if not recipe_ok(slot, r, true) then return end
	start_action(slot, ACT_CRAFT, 0, 0, 12, r)
	local m = CHARS[slot].mob
	say_to(slot, "You set to work on " .. article(ITEMS[RECIPES[r].out].name) .. "...", C_CYAN)
	sound_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m], RECIPES[r].station == 3 and "sizzle" or "hammer")
end

craft_finish = function(slot, r)
	if not recipe_ok(slot, r, true) then return end
	local d = RECIPES[r]
	local c = CHARS[slot]
	local m = c.mob
	local w = MOB_WORLD[m]
	local ok = skill_check(slot, d.skill, d.min + 150)
	if not ok then
		-- lose half the materials, rounded up
		for k = 0, d.in_n - 1 do pack_take(slot, d.in_item[k], (d.in_qty[k] + 1) // 2) end
		say_to(slot, "You fail, and some materials are ruined.", C_GRAY)
		fx_at(w, MOB_X[m], MOB_Y[m], FX.smoke, 6)
		return
	end
	for k = 0, d.in_n - 1 do pack_take(slot, d.in_item[k], d.in_qty[k]) end
	local it = make_item(d.out, d.out_qty)
	local what = ""
	if ITEMS[d.out].slot >= 0 then
		local margin = c.skill[d.skill] - d.min
		if rnd(1000) < 60 + idiv(margin, 2) then
			it.bonus = 1 + rnd(3)
			it.rarity = 1
			local weapon = ITEMS[d.out].kind == Kind.weapon
			it.suffix = roll_affix(false, weapon, 2 + idiv(margin, 250))
			if it.suffix >= 0 then it.smag = roll_mag(it.suffix, 1) end
			what = "an exceptional "
		end
	end
	if not pack_add(slot, it) then drop_near(it, w, MOB_X[m], MOB_Y[m], slot, 3000) end
	c.crafted = c.crafted + 1
	if what ~= "" then
		say_to(slot, "You create " .. what .. item_name(it) .. "! A masterpiece!", C_YELLOW)
		fx_at(w, MOB_X[m], MOB_Y[m], FX.levelup, 12)
	else
		say_to(slot, "You create " .. item_name(it) .. ".", C_GREEN)
	end
	fx_at(w, MOB_X[m], MOB_Y[m], d.station == 3 and FX.fire or FX.sparks, 8)
	sound_at(w, MOB_X[m], MOB_Y[m], "craft")
	quest_on_craft(slot)
end

-- --------------------------------------------------------------- using ---
local YUM = { [0] = "Delicious!", "Mmm, swampy.", "Chomp chomp chomp.", "That hit the spot.", "Tastes like chicken." }
local BOTTLE_MSGS = { [0] = "HELP. I am trapped in a bottle factory.", "If found, return to Thibodeaux. He owes me 3 gold.",
	"The Golden Carp bites best in the Bayou, when your Fishing is keen.", "Dear diary, today a gator ate my boat. Again.",
	"Six scales. Six tyrants. One shrine. -M", "There is no treasure. Stop reading bottles.", "Nice try, raccoon." }

local function use_item(slot, index)
	local c = CHARS[slot]
	local m = c.mob
	if index < 0 or index >= alen(c.pack) then return end
	if mob_ghost(m) then say_to(slot, "Ghostly paws pass right through it.", C_GRAY); return end
	local it = c.pack[index]
	local d = ITEMS[it.base]
	local w = MOB_WORLD[m]
	if item_equippable(it) then equip_from_pack(slot, index); return end
	local eff = EFF_NAME[d.eff]
	if d.kind == Kind.potion and eff ~= "bomb" then
		if TICK < c.potion_t then
			say_to(slot, "You must wait " .. (c.potion_t - TICK + 9) // 10 .. "s before drinking another potion.", C_GRAY)
			return
		end
		c.potion_t = TICK + 30
	end
	if eff == "heal" then
		pack_take_at(slot, index, 1)
		heal_mob(m, d.pow, m)
		fx_at(w, MOB_X[m], MOB_Y[m], FX.heal, 10)
		sound_at(w, MOB_X[m], MOB_Y[m], "drink")
		return
	end
	if eff == "mana" then
		pack_take_at(slot, index, 1)
		MOB_MP[m] = imin(MOB_MPMAX[m], MOB_MP[m] + d.pow)
		fx_at(w, MOB_X[m], MOB_Y[m], FX.magic, 8)
		sound_at(w, MOB_X[m], MOB_Y[m], "drink")
		return
	end
	if eff == "cure" then
		pack_take_at(slot, index, 1)
		MOB_POISON[m] = 0
		say_to(slot, "You feel the poison leave your body.", C_GREEN)
		sound_at(w, MOB_X[m], MOB_Y[m], "drink")
		return
	end
	if eff == "str" then
		pack_take_at(slot, index, 1)
		c.buff_str = TICK + 1200
		say_to(slot, "You feel mighty! (not really, but the potion tasted great)", C_GREEN)
		heal_mob(m, 10, m)
		return
	end
	if eff == "nightsight" then pack_take_at(slot, index, 1); c.buff_night = TICK + d.pow; say_to(slot, "The darkness melts away.", C_YELLOW); return end
	if eff == "fireres" then pack_take_at(slot, index, 1); c.buff_fire = TICK + d.pow; say_to(slot, "Your scales feel cool and fireproof.", C_ORANGE); return end
	if eff == "regen" then pack_take_at(slot, index, 1); c.buff_regen = TICK + d.pow; say_to(slot, "You feel your wounds knitting.", C_GREEN); return end
	if eff == "food" then
		pack_take_at(slot, index, 1)
		heal_mob(m, d.pow, m)
		say_to(slot, "You eat the " .. d.name .. ". " .. YUM[rnd(5)], C_GREEN)
		sound_at(w, MOB_X[m], MOB_Y[m], "eat")
		return
	end
	if eff == "learn" then
		local spell = d.pow
		if c.spells[spell] == 0 then
			pack_take_at(slot, index, 1)
			c.spells[spell] = 1
			c.version = c.version + 1
			say_to(slot, "You inscribe " .. SPELLS[spell].name .. " into your spellbook!", C_PURPLE)
			fx_at(w, MOB_X[m], MOB_Y[m], FX.magic, 12)
			sound_at(w, MOB_X[m], MOB_Y[m], "chime")
		else
			say_to(slot, "You already know " .. SPELLS[spell].name .. ". Sell the scroll to Madame Hoodoo.", C_GRAY)
		end
		return
	end
	if eff == "recall" then
		pack_take_at(slot, index, 1)
		local px, py = find_spot(c.home_world, c.home_x, c.home_y, 5, LAND_MASK)
		travel(m, c.home_world, px, py)
		return
	end
	if eff == "bandage" then
		start_action(slot, ACT_BANDAGE, MOB_X[m], MOB_Y[m], 20 - c.skill[SK.healing] // 100, 0)
		say_to(slot, "You begin applying a bandage.", C_CYAN)
		return
	end
	if eff == "random" then
		pack_take_at(slot, index, 1)
		local r = rnd(6)
		if r == 0 then heal_mob(m, 40, m); say_to(slot, "You feel fantastic! The colors! THE COLORS!", C_PURPLE)
		elseif r == 1 then MOB_POISON[m] = 60; say_to(slot, "Oh no. Oh no no no. You feel very poisoned.", C_GREEN)
		elseif r == 2 then c.buff_night = TICK + 900; say_to(slot, "You can see sounds. And also in the dark.", C_YELLOW)
		elseif r == 3 then MOB_MP[m] = MOB_MPMAX[m]; say_to(slot, "Your mind buzzes with arcane energy!", C_BLUE)
		elseif r == 4 then c.gold = c.gold + 25; say_to(slot, "You find 25 gold inside the mushroom. How?", C_YELLOW)
		else say_to(slot, "You feel... exactly the same. Disappointing.", C_GRAY) end
		fx_at(w, MOB_X[m], MOB_Y[m], FX.magic, 10)
		return
	end
	if eff == "firebreath" then
		pack_take_at(slot, index, 1)
		say_to(slot, "You chug the hot sauce and breathe FIRE!", C_ORANGE)
		local dir = MOB_DIR[m]
		for k = 1, 4 do
			local x = MOB_X[m] + DX[dir] * k
			local y = MOB_Y[m] + DY[dir] * k
			fx_at(w, x, y, FX.fire, 6)
			local o = mob_at(w, x, y)
			if o >= 0 and hostile(m, o) then deal(o, d.pow + rnd(10), m, DMG_FIRE, false); if mob_alive(o) then MOB_BURN[o] = 20 end end
		end
		sound_at(w, MOB_X[m], MOB_Y[m], "fire")
		shake(slot, 3)
		return
	end
	if eff == "bomb" then
		pack_take_at(slot, index, 1)
		local tx = MOB_X[m] + DX[MOB_DIR[m]] * 3
		local ty = MOB_Y[m] + DY[MOB_DIR[m]] * 3
		ev(Ev.proj, w, MOB_X[m], MOB_Y[m], tx, ty, 7, -1, "")
		fx_at(w, tx, ty, FX.poison, 30)
		sound_at(w, tx, ty, "boom")
		for dy = -2, 2 do
			for dx = -2, 2 do
				local o = mob_at(w, tx + dx, ty + dy)
				if o >= 0 and hostile(m, o) then deal(o, d.pow, m, DMG_POISON, false); if mob_alive(o) then MOB_POISON[o] = 50 end end
			end
		end
		shake(slot, 4)
		return
	end
	if eff == "bottle" then
		pack_take_at(slot, index, 1)
		say_to(slot, "The message reads: \"" .. BOTTLE_MSGS[rnd(7)] .. "\"", C_CYAN)
		if rnd(4) == 0 then
			local g = 30 + rnd(60)
			c.gold = c.gold + g
			say_to(slot, "Tucked inside: " .. g .. " gold!", C_YELLOW)
		end
		return
	end
	if eff == "squeak" then say_to(slot, "Squeak!", C_YELLOW); sound_at(w, MOB_X[m], MOB_Y[m], "squeak"); return end
	if eff == "music" then
		say_to(slot, "You play the " .. d.name .. ". Nearby creatures pause to listen. (It is not good.)", C_PURPLE)
		sound_at(w, MOB_X[m], MOB_Y[m], "kazoo")
		for dy = -4, 4 do
			for dx = -4, 4 do
				local o = mob_at(w, MOB_X[m] + dx, MOB_Y[m] + dy)
				if o >= 0 and MOB_PLAYER[o] < 0 and not MONS[MOB_KIND[o]].boss then MOB_NEXT[o] = TICK + 30; MOB_TARGET[o] = -1 end
			end
		end
		return
	end
	if eff == "spellbook" then say_to(slot, "Open your spellbook with B.", C_GRAY); return end
	say_to(slot, "You can't think of a use for " .. article(item_base_name(it)) .. ".", C_GRAY)
end

local function drop_item(slot, index)
	local c = CHARS[slot]
	if index < 0 or index >= alen(c.pack) then return end
	local m = c.mob
	local it = c.pack[index]
	aremove(c.pack, index)
	c.version = c.version + 1
	gi_add(it, MOB_WORLD[m], MOB_X[m], MOB_Y[m], -1, 3000)
	say_to(slot, "You drop " .. item_name(it) .. ".", C_GRAY)
end

local function pick_up(slot)
	local c = CHARS[slot]
	local m = c.mob
	local w = MOB_WORLD[m]
	local got = 0
	-- what is in reach, most important first: quest items, gold, then by rarity
	local reach, rank, n = {}, {}, 0
	for i = 0, GI_N - 1 do
		local gi = GI_ITEM[i]
		if not (gi.base < 0 or GI_W[i] ~= w or cheb(GI_X[i], GI_Y[i], MOB_X[m], MOB_Y[m]) > 1)
			and not (GI_OWNER[i] >= 0 and GI_OWNER[i] ~= slot)
			and not (mob_ghost(m) and GI_OWNER[i] ~= slot) then
			local kind = ITEMS[gi.base].kind
			local r = kind == Kind.quest and 100 or (kind == Kind.gold and 90 or gi.rarity * 10)
			local at = n
			while at > 0 and rank[at - 1] < r do at = at - 1 end
			for k = n, at + 1, -1 do
				reach[k] = reach[k - 1]
				rank[k] = rank[k - 1]
			end
			reach[at] = i
			rank[at] = r
			n = n + 1
		end
	end
	for ri = 0, n - 1 do
		local i = reach[ri]
		local it = GI_ITEM[i]
		if not pack_add(slot, it) then say_to(slot, "Your backpack is full.", C_RED); break end
		ev(Ev.pickup, w, GI_X[i], GI_Y[i], GI_SER[i], it.base, 0, -1, "")
		local rare = it.rarity >= 2 and (" (" .. RARITY_NAME[it.rarity] .. ")") or ""
		say_to(slot, "You pick up " .. item_name(it) .. rare .. ".", it.rarity >= 3 and C_PURPLE or (it.rarity >= 2 and C_BLUE or C_WHITE))
		sound_at(w, GI_X[i], GI_Y[i], ITEMS[it.base].kind == Kind.gold and "coin" or "pickup")
		gi_remove(i)
		got = got + 1
	end
	if got == 0 then say_to(slot, "There is nothing here to pick up.", C_GRAY) end
end

-- nearest hostile in sight, cycling from the current target
local function cycle_target(slot)
	local m = CHARS[slot].mob
	local w = MOB_WORLD[m]
	local cur = MOB_TARGET[m]
	local best = -1
	local bestd = 999
	local after = -1
	local afterd = 999
	local curd = mob_live(cur) and cheb(MOB_X[m], MOB_Y[m], MOB_X[cur], MOB_Y[cur]) or -1
	for dy = -9, 9 do
		for dx = -9, 9 do
			local o = mob_at(w, MOB_X[m] + dx, MOB_Y[m] + dy)
			if not (o < 0 or o == m or not hostile(m, o) or (MOB_FLAGS[o] & MF_HIDDEN) ~= 0) then
				local d = cheb(0, 0, dx, dy) * 1000 + MOB_SER[o] % 1000
				if los(w, MOB_X[m], MOB_Y[m], MOB_X[o], MOB_Y[o]) then
					if d < bestd then bestd = d; best = o end
					local cd = curd * 1000 + (mob_live(cur) and MOB_SER[cur] % 1000 or 0)
					if o ~= cur and d > cd and d < afterd then afterd = d; after = o end
				end
			end
		end
	end
	local pick = after >= 0 and after or best
	MOB_TARGET[m] = pick
	if pick >= 0 then say_to(slot, "Target: " .. mob_name(pick), C_ORANGE)
	else say_to(slot, "No enemies in sight.", C_GRAY) end
end

local function toggle_hide(slot)
	local m = CHARS[slot].mob
	if (MOB_FLAGS[m] & MF_HIDDEN) ~= 0 then
		MOB_FLAGS[m] = MOB_FLAGS[m] & ~MF_HIDDEN
		say_to(slot, "You step out of hiding.", C_GRAY)
		return
	end
	local bonus = TILES_DEF[tile_now(MOB_WORLD[m], MOB_X[m], MOB_Y[m])].hide and 200 or 0
	if skill_check(slot, SK.stealth, 250 - bonus) then
		MOB_FLAGS[m] = MOB_FLAGS[m] | MF_HIDDEN
		MOB_TARGET[m] = -1
		say_to(slot, "You have hidden yourself well.", C_GREEN)
		fx_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m], FX.smoke, 6)
	else
		say_to(slot, "You can't seem to hide here.", C_GRAY)
	end
end

local function party_invite(slot, other)
	if other < 0 or other >= CHAR_N or not CHARS[other].active or other == slot then return end
	CHARS[other].invite = slot
	say_to(other, CHARS[slot].name .. " invites you to a party. Type /accept to join.", C_CYAN)
	say_to(slot, "You invite " .. CHARS[other].name .. " to your party.", C_CYAN)
end

local function party_accept(slot)
	local c = CHARS[slot]
	local leader = c.invite
	if leader < 0 or not CHARS[leader].active then say_to(slot, "No party invitation.", C_GRAY); return end
	if CHARS[leader].party < 0 then CHARS[leader].party = leader end
	c.party = CHARS[leader].party
	c.invite = -1
	c.version = c.version + 1
	CHARS[leader].version = CHARS[leader].version + 1
	for s = 0, CHAR_N - 1 do
		if CHARS[s].active and CHARS[s].party == c.party then say_to(s, c.name .. " joins the party!", C_CYAN) end
	end
end

party_leave = function(slot)
	local c = CHARS[slot]
	if c.party < 0 then return end
	local p = c.party
	c.party = -1
	c.version = c.version + 1
	say_to(slot, "You leave the party.", C_CYAN)
	local left = 0
	for s = 0, CHAR_N - 1 do
		if CHARS[s].active and CHARS[s].party == p then left = left + 1; say_to(s, c.name .. " leaves the party.", C_CYAN) end
	end
	if left <= 1 then
		for s = 0, CHAR_N - 1 do
			if CHARS[s].party == p then CHARS[s].party = -1; CHARS[s].version = CHARS[s].version + 1 end
		end
	end
end

local function chat(slot, text)
	local m = CHARS[slot].mob
	local clean = #text > 120 and string.sub(text, 1, 120) or text
	if string.sub(clean, 1, 1) == "/" then chat_command(slot, clean); return end
	ev(Ev.chat, MOB_WORLD[m], MOB_X[m], MOB_Y[m], slot, CHARS[slot].party, 0, -1, CHARS[slot].name .. ": " .. clean)
end

chat_command = function(slot, cmd)
	local p, pn = split(cmd, " ")
	local verb = p[0]
	if verb == "/pvp" then
		local c = CHARS[slot]
		c.pvp = not c.pvp
		local m = c.mob
		if c.pvp then MOB_FLAGS[m] = MOB_FLAGS[m] | MF_PVP else MOB_FLAGS[m] = MOB_FLAGS[m] & ~MF_PVP end
		c.version = c.version + 1
		say_to(slot, c.pvp and "PvP ON: other flagged gators may attack you, and you them." or "PvP OFF: you are safe from other players.", c.pvp and C_RED or C_GREEN)
		return
	end
	if verb == "/accept" then party_accept(slot); return end
	if verb == "/leave" then party_leave(slot); return end
	if verb == "/who" then
		local names = ""
		for s = 0, CHAR_N - 1 do
			if CHARS[s].active then names = names .. (names == "" and "" or ", ") .. CHARS[s].name end
		end
		say_to(slot, "Online: " .. names, C_CYAN)
		return
	end
	if verb == "/invite" and pn > 1 then
		for s = 0, CHAR_N - 1 do
			-- the script's to_lower() lowers in place: the name and the argument stay lowered
			if CHARS[s].active then
				CHARS[s].name = string.lower(CHARS[s].name)
				p[1] = string.lower(p[1])
				if CHARS[s].name == p[1] then party_invite(slot, s); return end
			end
		end
		say_to(slot, "No gator by that name.", C_GRAY)
		return
	end
	if verb == "/p" and pn > 1 then
		local m = CHARS[slot].mob
		local msg = string.sub(cmd, 4)
		ev(Ev.chat, MOB_WORLD[m], MOB_X[m], MOB_Y[m], slot, CHARS[slot].party, 1, -1, CHARS[slot].name .. " (party): " .. msg)
		return
	end
	if verb == "/home" then
		local c = CHARS[slot]
		local m = c.mob
		local t = tile_now(MOB_WORLD[m], MOB_X[m], MOB_Y[m])
		if town_at(MOB_WORLD[m], MOB_X[m], MOB_Y[m]) >= 0 or TILES_DEF[t].station ~= 0 then
			c.home_world = MOB_WORLD[m]
			c.home_x = MOB_X[m]
			c.home_y = MOB_Y[m]
			say_to(slot, "You bind your soul to this place. Recall brings you here.", C_PURPLE)
		else
			say_to(slot, "You can only bind your home in a town.", C_GRAY)
		end
		return
	end
	say_to(slot, "Commands: /pvp /who /invite name /accept /leave /p message /home", C_GRAY)
end

local function use_bandage(slot)
	local c = CHARS[slot]
	for i = 0, alen(c.pack) - 1 do
		if c.pack[i].base == I_BANDAGE then use_item(slot, i); return end
	end
	say_to(slot, "You have no bandages.", C_GRAY)
end

-- ------------------------------------------------------------- dispatch ---
-- one queued action string; the wire format of player intents
local function do_action(slot, a)
	if #a == 0 or slot >= CHAR_N or not CHARS[slot].active then return end
	local m = CHARS[slot].mob
	if not mob_live(m) then return end
	local op = string.sub(a, 1, 1)
	local rest = string.sub(a, 2)
	local args, nargs = split(rest, ",")
	local n0 = nargs > 0 and to_int(args[0], -1) or -1
	local n1 = nargs > 1 and to_int(args[1], -1) or -1
	if op == "A" then
		local t = find_npc_ser(n0)
		if t >= 0 and hostile(m, t) then MOB_TARGET[m] = t end
	elseif op == "T" then cycle_target(slot)
	elseif op == "C" then player_cast(slot, n0)
	elseif op == "U" then use_item(slot, n0)
	elseif op == "R" then unequip(slot, n0)
	elseif op == "D" then drop_item(slot, n0)
	elseif op == "G" then pick_up(slot)
	elseif op == "E" then interact(slot)
	elseif op == "B" then shop_buy(slot, find_npc_ser(n0), n1)
	elseif op == "S" then shop_sell(slot, find_npc_ser(n0), n1)
	elseif op == "K" then craft_start(slot, n0)
	elseif op == "H" then healer_service(slot, find_npc_ser(n0))
	elseif op == "Q" then
		local npc = find_npc_ser(n0)
		if npc >= 0 and npc_in_reach(slot, npc) then quest_talk(slot, MOB_ROLE[npc]) end
	elseif op == "W" then bank_deposit(slot, find_npc_ser(n0), n1)
	elseif op == "X" then bank_withdraw(slot, find_npc_ser(n0), n1)
	elseif op == "Z" then toggle_hide(slot)
	elseif op == "N" then use_bandage(slot)
	elseif op == ":" then chat(slot, rest)
	elseif op == "!" then MOB_TARGET[m] = -1 end
end

-- per-tick player upkeep: queued actions, held movement, ice slides, auto-attack
function players_tick()
	for slot = 0, CHAR_N - 1 do
		local c = CHARS[slot]
		local m = c.mob
		if c.active and mob_live(m) then
			c.played_ticks = c.played_ticks + 1
			local q = IN_QUEUE[slot]
			IN_QUEUE[slot] = {}
			local qn = alen(q)
			if qn > 8 then qn = 8 end
			for i = 0, qn - 1 do do_action(slot, q[i]) end
			if mob_live(m) then
				actions_tick(slot)
				local slid = false
				-- ice keeps you going
				if MOB_AUX[m] > 0 and TICK >= MOB_NEXT[m] then
					local dir = MOB_AUX[m] - 1
					if not mob_step(m, dir) then MOB_AUX[m] = 0
					else on_enter(m) end
					slid = true
				end
				if not slid then
					-- a predicting client's steps, one per step time, in the order it took them
					if slot < alen(IN_STEP_SEQ) and alen(IN_STEP_SEQ[slot]) > 0 then
						if TICK >= MOB_NEXT[m] then
							local seq = IN_STEP_SEQ[slot][0]
							local sdir = IN_STEP_DIR[slot][0]
							aremove(IN_STEP_SEQ[slot], 0)
							aremove(IN_STEP_DIR[slot], 0)
							player_move(slot, sdir)
							IN_ACK[slot] = seq
							if alen(IN_STEP_SEQ[slot]) == 0 and IN_DROPPED[slot] > IN_ACK[slot] then IN_ACK[slot] = IN_DROPPED[slot] end
						end
					else
						local dir = IN_MOVE[slot]
						if dir >= 0 and TICK >= MOB_NEXT[m] then player_move(slot, dir) end
					end
					-- war mode: swing at the target whenever it is in reach
					local t = MOB_TARGET[m]
					if t >= 0 and not mob_ghost(m) then
						if not mob_alive(t) or MOB_WORLD[t] ~= MOB_WORLD[m] or cheb(MOB_X[m], MOB_Y[m], MOB_X[t], MOB_Y[t]) > 16 then
							MOB_TARGET[m] = -1
						elseif TICK >= MOB_ATK[m] and hostile(m, t) then
							local d = cheb(MOB_X[m], MOB_Y[m], MOB_X[t], MOB_Y[t])
							if d <= attack_range(m) and (d <= 1 or los(MOB_WORLD[m], MOB_X[m], MOB_Y[m], MOB_X[t], MOB_Y[t])) then attack(m, t) end
						end
					end
					if c.buff_str > 0 and TICK >= c.buff_str then c.buff_str = 0 end
				end
			end
		end
	end
end

for name, fn in pairs({ player_spawn = player_spawn, facing_x = facing_x, facing_y = facing_y, fall_from_sky = fall_from_sky,
	on_enter = on_enter, player_move = player_move, npc_talk = npc_talk, find_npc_ser = find_npc_ser, npc_in_reach = npc_in_reach,
	buy_price = buy_price, sell_price = sell_price, shop_buy = shop_buy, shop_sell = shop_sell, healer_service = healer_service,
	bank_deposit = bank_deposit, bank_withdraw = bank_withdraw, open_chest = open_chest, fish_for = fish_for, interact = interact,
	start_action = start_action, finish_action = finish_action, actions_tick = actions_tick, near_station = near_station,
	recipe_ok = recipe_ok, craft_start = craft_start, craft_finish = craft_finish, use_item = use_item, drop_item = drop_item,
	pick_up = pick_up, cycle_target = cycle_target, toggle_hide = toggle_hide, party_invite = party_invite,
	party_accept = party_accept, party_leave = party_leave, chat = chat, chat_command = chat_command, do_action = do_action,
	use_bandage = use_bandage }) do
	_G[name] = fn
end
