-- Gator Quest (Lua port) - the value records. JaiScript classes are value types (assignment copies);
-- tables are references, so the port copies an Item wherever the script's copy is later mutated
-- or stored into a container (copy_item).

function Item(b, q)
	return { base = b or -1, qty = q or 0, rarity = 0, prefix = -1, suffix = -1, pmag = 0, smag = 0, bonus = 0, ser = 0 }
end

function copy_item(it)
	return { base = it.base, qty = it.qty, rarity = it.rarity, prefix = it.prefix, suffix = it.suffix,
		pmag = it.pmag, smag = it.smag, bonus = it.bonus, ser = it.ser }
end

function item_empty(it) return it.base < 0 or it.qty <= 0 end

-- an autopilot driving one player slot
function Bot()
	return { slot = -1, phase = 0, phase_t = 0, goal_w = -1, goal_x = 0, goal_y = 0, stuck = 0, last_x = -1, last_y = -1,
		done = 0, kills_at = 0, step = 0, log = "", path = {}, path_n = 0, path_i = 0, path_w = -1, path_tx = -1, path_ty = -1 }
end

-- arrays inside are 0-based like the script's (skill[Sk.swords] is skill[0]); *_n fields are not used:
-- sizes come from alen() on these small arrays
function Character()
	return {
		active = false, connected = false, name = "", mob = -1, hue = 0, str = 30, dex = 20, intel = 20,
		skill = {}, pack = {}, worn = {}, bank = {},
		gold = 0, bank_gold = 0, fame = 0, karma = 0, kills = 0, deaths = 0, murders = 0,
		spells = {}, quest_stage = {}, quest_count = {}, visited = {},
		boss_mask = 0, pvp = false, party = -1, invite = -1, home_world = 0, home_x = 0, home_y = 0,
		last_spell = -1, talk_npc = -1, talk_t = -1000, action = 0, action_t = 0, action_x = 0, action_y = 0,
		action_arg = 0, potion_t = 0, buff_night = 0, buff_fire = 0, buff_str = 0, buff_regen = 0,
		fish_caught = 0, crafted = 0, stat_t = 0, version = 1, played_ticks = 0,
	}
end
