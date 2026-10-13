-- Gator Quest (Lua port) - enums and flag bits. Values are the JaiScript enum ordinals (0-based).

local function enum(names)
	local t = {}
	for i = 1, #names do t[names[i]] = i - 1 end
	return t
end

Dir = enum({ "n", "ne", "e", "se", "s", "sw", "w", "nw" })
Sk = enum({ "swords", "maces", "archery", "wrestling", "tactics", "parrying", "magery", "meditation",
	"resist", "healing", "fishing", "mining", "lumber", "smithing", "cooking", "alchemy",
	"stealth", "swimming", "count" })
Slot = enum({ "head", "neck", "body", "hands", "legs", "feet", "weapon", "offhand", "ring", "trinket", "count" })
Kind = enum({ "weapon", "armor", "shield", "potion", "scroll", "food", "reagent", "tool", "resource", "ammo", "trinket", "junk", "gem", "quest", "gold" })
Rar = enum({ "common", "uncommon", "rare", "epic", "legendary" })
Fac = enum({ "player", "npc", "monster", "animal", "guard" })
Ai = enum({ "none", "wander", "melee", "pack", "ranged", "caster", "coward", "thief", "ambush", "boss", "guard", "critter", "summon" })
Ev = enum({ "dmg", "heal", "miss", "death", "proj", "fx", "sound", "log", "shake", "banner", "chat", "gain", "pickup", "spawn", "warp", "dialog" })
Fx = enum({ "sparks", "blood", "splash", "fire", "frost", "zap", "heal", "smoke", "poison", "magic", "gold", "loot", "levelup", "warp", "bubbles", "bones", "dust", "holy" })

MF_GHOST = 1
MF_PVP = 2
MF_BOSS = 4
MF_HIDDEN = 8
MF_INVULN = 16
MF_SUMMON = 32
MF_CRIMINAL = 64
MF_ALERT = 128
MF_FLEEING = 256
MF_SWIM = 512
MF_ELITE = 1024
