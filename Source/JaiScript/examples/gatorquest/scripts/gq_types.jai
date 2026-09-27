// Gator Quest - shared enums and the value classes the world is made of.
// Reloadable: a class redefinition migrates live instances (new fields take
// their initializers, existing fields keep their values).

enum Dir { n, ne, e, se, s, sw, w, nw }

enum Sk {
	swords, maces, archery, wrestling, tactics, parrying, magery, meditation,
	resist, healing, fishing, mining, lumber, smithing, cooking, alchemy,
	stealth, swimming, count
}

enum Slot { head, neck, body, hands, legs, feet, weapon, offhand, ring, trinket, count }

enum Kind { weapon, armor, shield, potion, scroll, food, reagent, tool, resource, ammo, trinket, junk, gem, quest, gold }

enum Rar { common, uncommon, rare, epic, legendary }

enum Fac { player, npc, monster, animal, guard }

enum Ai { none, wander, melee, pack, ranged, caster, coward, thief, ambush, boss, guard, critter, summon }

enum Ev { dmg, heal, miss, death, proj, fx, sound, log, shake, banner, chat, gain, pickup, spawn, warp, dialog }

enum Fx { sparks, blood, splash, fire, frost, zap, heal, smoke, poison, magic, gold, loot, levelup, warp, bubbles, bones, dust, holy }

// mobile flag bits
int MF_GHOST = 1;
int MF_PVP = 2;
int MF_BOSS = 4;
int MF_HIDDEN = 8;
int MF_INVULN = 16;
int MF_SUMMON = 32;
int MF_CRIMINAL = 64;
int MF_ALERT = 128;
int MF_FLEEING = 256;
int MF_SWIM = 512;
int MF_ELITE = 1024;

class Item {
	int base = -1;
	int qty = 0;
	int rarity = 0;
	int prefix = -1;
	int suffix = -1;
	int pmag = 0;
	int smag = 0;
	int bonus = 0;
	int ser = 0;

	Item() {}
	Item(int b, int q) { base = b; qty = q; }

	bool empty() { return base < 0 || qty <= 0; }
}

// an autopilot driving one player slot (smoke tests, net tests, idle demo)
class Bot {
	int slot = -1;
	int phase = 0;
	int phase_t = 0;
	int goal_w = -1;
	int goal_x = 0;
	int goal_y = 0;
	int stuck = 0;
	int last_x = -1;
	int last_y = -1;
	int done = 0;
	int kills_at = 0;
	int step = 0;
	string log = "";
	array<int> path;
	int path_i = 0;
	int path_w = -1;
	int path_tx = -1;
	int path_ty = -1;
}

class Character {
	bool active = false;
	bool connected = false;
	string name = "";
	int mob = -1;
	int hue = 0;
	int str = 30;
	int dex = 20;
	int intel = 20;
	array<int> skill;
	array<Item> pack;
	array<Item> worn;
	array<Item> bank;
	int gold = 0;
	int bank_gold = 0;
	int fame = 0;
	int karma = 0;
	int kills = 0;
	int deaths = 0;
	int murders = 0;
	array<int> spells;
	array<int> quest_stage;
	array<int> quest_count;
	array<int> visited;
	int boss_mask = 0;
	bool pvp = false;
	int party = -1;
	int invite = -1;
	int home_world = 0;
	int home_x = 0;
	int home_y = 0;
	int last_spell = -1;
	int talk_npc = -1;
	int talk_t = -1000;
	int action = 0;
	int action_t = 0;
	int action_x = 0;
	int action_y = 0;
	int action_arg = 0;
	int buff_night = 0;
	int buff_fire = 0;
	int buff_str = 0;
	int buff_regen = 0;
	int fish_caught = 0;
	int crafted = 0;
	int stat_t = 0;
	int version = 1;
	int played_ticks = 0;

	Character() {}
}
