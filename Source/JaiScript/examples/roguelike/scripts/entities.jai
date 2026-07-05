// Entity hierarchy (script classes with inheritance) and the timed-effect
// system. JaiScript is value-semantic: objects deep-copy on assignment and
// plain parameter passing, so every mutating function takes `var&` and the
// canonical entities live in exactly one place (G.player / G.monsters[i]).
// Effects are plain data records {"kind","turns","power"} — serializable,
// copy-safe, ticked procedurally.

class Entity {
	int id = 0;
	string name = "";
	string glyph = "?";
	string color = "37";
	int x = 0;
	int y = 0;
	int hp = 1;
	int max_hp = 1;
	int atk = 1;
	int def = 0;
	int eva = 0;       // dodge %, pre-trait
	int crit = 5;      // crit %, pre-trait
	bool alive = true;
	var effects = [];  // array of {"kind","turns","power"} maps

	Entity() {}

	function has_effect(string effect_kind) -> bool {
		for (auto fx : effects) {
			if (fx["kind"] == effect_kind) { return true; }
		}
		return false;
	}
}

class Monster : Entity {
	string kind = "";
	string behavior = "chase";
	int tier = 1;
	int range = 1;          // attack reach for ranged behaviors
	bool aggro = false;
	bool disguised = false; // mimics
	int move_clock = 0;     // sluggish things move every other tick
	bool withers = false;
	bool burns = false;

	Monster(string monster_kind) : super() {
		kind = monster_kind;
		var def_row = BESTIARY[monster_kind];   // NOT auto: heterogeneous map values
		name = def_row["name"];
		glyph = def_row["glyph"];
		color = def_row["color"];
		max_hp = def_row["hp"];
		hp = max_hp;
		atk = def_row["atk"];
		def = def_row["def"];
		eva = def_row["eva"];
		behavior = def_row["behavior"];
		tier = def_row["tier"];
		// unconditional: `if (row.has(k)) { int_field = row[k]; }` inside a ctor
		// trips an interpreter-only "Type mismatch in assignment" (VM converts)
		range = def_row["range"];
		withers = def_row["withers"];
		burns = def_row["burns"];
		if (behavior == "mimic") { disguised = true; }
	}

	function xp_reward() -> int { return tier * 5 + 2; }
	function gold_reward(var rng) -> int { return rng.roll(0, tier * 4); }
}

class Item {
	string name = "";
	string kind = "weapon";   // weapon | armor | trinket | potion | scroll | gold
	string glyph = ")";
	string color = "37";
	string rarity = "common";
	int atk = 0;
	int def = 0;
	var bonus = {};           // e.g. {"crit": 5, "burn": 10, "eva": 4, "hp": 4, "focus": 5}
	string effect = "";       // potion/scroll id
	int amount = 0;           // gold piles
	int x = 0;
	int y = 0;

	Item() {}

	function bonus_of(string key) -> int {
		if (bonus.has(key)) { return bonus[key]; }
		return 0;
	}
}

class Player : Entity {
	string species_id = "";
	string class_id = "";
	string trait = "";
	string skill = "";
	string skill_name = "";
	int skill_cost = 0;
	int level = 1;
	int xp = 0;
	int gold = 0;
	int focus = 8;
	int max_focus = 8;
	int kills = 0;
	int fov_radius = 8;
	bool god = false;
	bool stubborn_spent = false;  // Brockin trait, refreshes each floor
	var weapon = null;
	var armor = null;
	var trinket = null;
	var inventory = [];

	Player(var species_row, var class_row) : super() {
		name = "Wanderer";
		glyph = "@";
		color = "93";
		species_id = species_row["id"];
		class_id = class_row["id"];
		trait = species_row["trait"];
		skill = class_row["skill"];
		skill_name = class_row["skill_name"];
		skill_cost = class_row["skill_cost"];
		max_hp = 20 + species_row["hp"] + class_row["hp"];
		hp = max_hp;
		atk = 3 + species_row["atk"] + class_row["atk"];
		def = species_row["def"] + class_row["def"];
		eva = 8 + species_row["eva"] + class_row["eva"];
		crit = 5 + species_row["crit"] + class_row["crit"];
		max_focus = 8 + species_row["focus"] + class_row["focus"];
		focus = max_focus;
	}

	function equip_bonus(string key) -> int {
		var total = 0;
		if (weapon != null) { total = total + weapon.bonus_of(key); }
		if (armor != null) { total = total + armor.bonus_of(key); }
		if (trinket != null) { total = total + trinket.bonus_of(key); }
		return total;
	}

	function total_atk() -> int {
		var total = atk + equip_bonus("atk");
		if (weapon != null) { total = total + weapon.atk; }
		return total;
	}

	function total_def() -> int {
		var total = def + equip_bonus("def");
		if (armor != null) { total = total + armor.def; }
		return total;
	}

	function total_eva() -> int {
		var total = eva + equip_bonus("eva");
		if (trait == "slipjaw" && hp * 4 < total_max_hp()) { total = total + 20; }
		return total;
	}

	function total_crit() -> int { return crit + equip_bonus("crit"); }
	function total_max_hp() -> int { return max_hp + equip_bonus("hp"); }
	function total_max_focus() -> int { return max_focus + equip_bonus("focus"); }

	function xp_next() -> int { return 8 + level * 7; }
}

// ------------------------------------------------------------- effects -----
// Stat-modifying kinds apply once on add and revert on expiry; the modified
// value is baked into the entity's fields, which makes save/load exact (the
// record rides along and reverts on schedule after loading).

function effect_stat_delta(string kind, int power) -> var {
	switch (kind) {
		case "rage": return ["atk", power];
		case "stoneskin": return ["def", power];
		case "curl": return ["def", power];
		case "blessed": return ["eva", power];
	}
	return null;
}

function apply_stat(var& target, string stat, int delta) -> void {
	switch (stat) {
		case "atk": target.atk = imax(0, target.atk + delta); break;
		case "def": target.def = imax(0, target.def + delta); break;
		case "eva": target.eva = target.eva + delta; break;
	}
}

function add_effect(var& target, string kind, int turns, int power) -> void {
	// refresh instead of stacking
	for (int i = 0; i < target.effects.size(); ++i) {
		if (target.effects[i]["kind"] == kind) {
			target.effects[i]["turns"] = imax(target.effects[i]["turns"], turns);
			return;
		}
	}
	var delta = effect_stat_delta(kind, power);
	if (delta != null) { apply_stat(target, delta[0], delta[1]); }
	if (kind == "wither") {
		apply_stat(target, "atk", -power);
		apply_stat(target, "def", -power);
	}
	target.effects.push({"kind": kind, "turns": turns, "power": power});
}

function expire_effect(var& target, string kind, int power) -> void {
	var delta = effect_stat_delta(kind, power);
	// hoist before negating: unary minus on a container-element read throws
	// ("Unary minus requires numeric operand") on BOTH backends — 2026-07 bug
	if (delta != null) {
		var amount = delta[1];
		apply_stat(target, delta[0], -amount);
	}
	if (kind == "wither") {
		apply_stat(target, "atk", power);
		apply_stat(target, "def", power);
	}
}

function tick_effects(var& target) -> void {
	var kept = [];
	for (int i = 0; i < target.effects.size(); ++i) {
		var kind = target.effects[i]["kind"];
		var power = target.effects[i]["power"];
		switch (kind) {
			case "burn": {
				var dmg = power;
				if (target.glyph == "@" && target.trait == "scaleblood") { dmg = imax(1, dmg / 2); }
				hurt_entity(target, dmg, "burning");
				break;
			}
			case "regen": {
				heal_entity(target, power);
				break;
			}
		}
		if (!target.alive) { return; }
		target.effects[i]["turns"] = target.effects[i]["turns"] - 1;
		if (target.effects[i]["turns"] <= 0) {
			expire_effect(target, kind, power);
		} else {
			kept.push(target.effects[i]);
		}
	}
	target.effects = kept;
}
