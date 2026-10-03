-- Gator Quest (Lua port) - definition records and the line DSL the content tables are written in:
-- "Name|kind|key=value|key=value|d=flavor text". parts is a 0-based array of n fields.

function TileDef()
	return { name = "", walk = 0, opaque = false, light = 0, res = 0, station = 0, after = -1, hide = false }
end

function ItemDef()
	return { name = "", kind = 0, slot = -1, lo = 0, hi = 0, armor = 0, speed = 8, skill = 3, range = 1, value = 1,
		tier = 1, eff = 0, pow = 0, pow2 = 0, stack = false, rarity = 0, spr = "", col = "", desc = "",
		aff_code = {}, aff_val = {}, aff_n = 0 }
end

function AffixDef()
	return { name = "", prefix = true, applies = 0, code = 0, lo = 1, hi = 1, tier = 1 }
end

function MonDef()
	return { name = "", hp = 10, lo = 1, hi = 3, armor = 0, speed = 3, aspeed = 8, skill = 300, ai = 2, fac = 2,
		range = 1, spell = -1, move = 1, tier = 1, gold_lo = 0, gold_hi = 0, fame = 1, light = 0, summon = -1,
		split = -1, undead = false, fireproof = false, frostproof = false, poisonous = false, boss = false,
		regen = 0, leech = 0, drop_item = {}, drop_chance = {}, drop_n = 0, spr = "", col = "", snd = "", quip = "", desc = "" }
end

function SpellDef()
	return { name = "", circle = 1, mana = 4, reg = {}, reg_n = 0, target = 0, eff = 0, lo = 0, hi = 0, range = 7,
		radius = 0, fx = 0, snd = "", desc = "" }
end

function RecipeDef()
	return { out = -1, out_qty = 1, station = 0, skill = 0, min = 0, tool = -1, in_item = {}, in_qty = {}, in_n = 0 }
end

function QuestDef()
	return { name = "", giver = 0, type = 0, target = -1, count = 1, gold = 0, reward = -1, reward_qty = 1,
		needs = -1, offer = "", done = "" }
end

function WorldDef()
	return { name = "", w = 64, h = 48, gen = 0, tier = 1, ambient = 0, music = 0, boss = -1, weather = "", banner = "", col = "" }
end

function NpcRole()
	return { name = "", title = "", shop = -1, heals = false, banks = false, lines = "" }
end

function dsl_get(parts, n, key, def)
	local k = key .. "="
	local kl = #k
	for i = 1, n - 1 do
		local p = parts[i]
		if string.sub(p, 1, kl) == k then return string.sub(p, kl + 1) end
	end
	return def
end

function dsl_int(parts, n, key, def)
	local v = dsl_get(parts, n, key, "")
	if v == "" then return def end
	return to_int(v, def)
end

function dsl_has(parts, n, flag)
	for i = 1, n - 1 do
		if parts[i] == flag then return true end
	end
	return false
end

-- "3-9" -> lo, hi
function dsl_range(parts, n, key, lo, hi)
	local v = dsl_get(parts, n, key, "")
	if v == "" then return lo, hi end
	local ab, abn = split(v, "-")
	if abn < 2 then
		local x = to_int(v, lo)
		return x, x
	end
	return to_int(ab[0], lo), to_int(ab[1], hi)
end

function index_of_name(names, n, def)
	for i = 0, alen(names) - 1 do
		if names[i] == n then return i end
	end
	return def
end
