// Game state, the turn loop, input handling, spawning, saves, and the
// deterministic smoke autopilot (a coroutine key generator in PILOT_CO).

class Game {
	int floor = 1;
	int turn = 0;
	int next_id = 1;
	bool over = false;
	bool victory = false;
	bool boss_slain = false;
	bool quit = false;
	bool saved = false;
	bool level_pending = false;
	bool autopilot = false;
	bool smoke = false;
	string death_cause = "misadventure";
	int hash = 2166136261;
	int last_dmg = 0;           // damage the player took since their last action
	string last_dmg_src = "";   // loudest recent source, shown in the side panel
	int stairs_x = -1;
	int stairs_y = -1;
	bool statue_used = false;
	bool mural_used = false;
	var player = null;
	var msgs = [];
	var monsters = [];
	var ground = [];
	var tiles = [];
	var seen = [];
	var visible = [];
	var rooms = [];
	var known_potions = {};
	var known_scrolls = {};
	var potion_looks = {};
	var scroll_looks = {};

	Game() {}
}

function monster_index_at(int x, int y) -> int {
	for (int i = 0; i < G.monsters.size(); ++i) {
		if (G.monsters[i].alive && G.monsters[i].x == x && G.monsters[i].y == y) { return i; }
	}
	return -1;
}

function ground_index_at(int x, int y) -> int {
	for (int i = 0; i < G.ground.size(); ++i) {
		if (G.ground[i].x == x && G.ground[i].y == y) { return i; }
	}
	return -1;
}

// deterministic per-run identify labels: shuffle looks onto effect ids
function assign_looks() -> void {
	G.potion_looks = {};
	G.scroll_looks = {};
	var looks = [];
	for (auto l : POTION_LOOKS) { looks.push(l); }
	for (auto id : POTIONS.keys()) {
		var pick_i = RNG.next(looks.size());
		G.potion_looks[id] = looks[pick_i];
		looks.remove(pick_i);
	}
	var slooks = [];
	for (auto l : SCROLL_LOOKS) { slooks.push(l); }
	for (auto id : SCROLLS.keys()) {
		var pick_i = RNG.next(slooks.size());
		G.scroll_looks[id] = slooks[pick_i];
		slooks.remove(pick_i);
	}
}

// finish configuring BEFORE the push: the array stores a deep copy,
// and G.monsters[i] is the only real monster from then on
function spawn_monster(string kind, int x, int y) -> void {
	var m = Monster(kind);
	m.id = G.next_id;
	G.next_id = G.next_id + 1;
	m.x = x;
	m.y = y;
	var depth_bonus = imax(0, G.floor - BESTIARY[kind]["floors"][0]);
	m.hp = m.hp + depth_bonus;
	m.max_hp = m.max_hp + depth_bonus;
	G.monsters.push(m);
}

function random_open_tile(int min_dist_from_player) -> var {
	for (int attempt = 0; attempt < 300; ++attempt) {
		var x = RNG.next(MAP_W);
		var y = RNG.next(MAP_H);
		if (tile_at(x, y) != TILE_FLOOR) { continue; }
		if (monster_index_at(x, y) >= 0) { continue; }
		if (cheb(x, y, G.player.x, G.player.y) < min_dist_from_player) { continue; }
		return [x, y];
	}
	return null;
}

function populate_floor() -> void {
	G.monsters = [];
	G.ground = [];
	if (G.floor >= 10) {
		spawn_monster("vexadrach", 50, 10);
		spawn_monster("drakelet", 46, 7);
		spawn_monster("drakelet", 46, 13);
		spawn_monster("drakelet", 52, 10);
		return;
	}
	var table = spawn_table(G.floor);
	var count = 3 + G.floor;
	for (int i = 0; i < count; ++i) {
		var spot = random_open_tile(7);
		if (spot == null) { continue; }
		spawn_monster(pick_weighted(RNG, table), spot[0], spot[1]);
	}
	var item_count = 2 + RNG.next(3);
	for (int i = 0; i < item_count; ++i) {
		var spot = random_open_tile(2);
		if (spot == null) { continue; }
		var item = make_floor_item(G.floor);
		item.x = spot[0];
		item.y = spot[1];
		G.ground.push(item);
	}
	var gold_count = 1 + RNG.next(2);
	for (int i = 0; i < gold_count; ++i) {
		var spot = random_open_tile(2);
		if (spot == null) { continue; }
		var pile = make_gold(G.floor);
		pile.x = spot[0];
		pile.y = spot[1];
		G.ground.push(pile);
	}
}

function enter_floor() -> void {
	var layout = generate_floor(G.floor);
	G.player.x = layout["start"][0];
	G.player.y = layout["start"][1];
	G.stairs_x = layout["stairs"][0];
	G.stairs_y = layout["stairs"][1];
	G.statue_used = false;
	G.mural_used = false;
	G.player.stubborn_spent = false;
	BOSS_CO = null;
	populate_floor();
	compute_fov();
	if (G.floor == 10) {
		log_msg(col("91", "The air tastes of ash and unspent color. HE is here."));
	} else {
		log_msg(col("90", `You descend to floor ${G.floor}.`));
	}
}

function start_new_game(var species_row, var class_row, string hero_name) -> void {
	G.player = Player(species_row, class_row);
	if (hero_name != "") { G.player.name = hero_name; }
	G.player.god = HOST_GOD;
	if (G.player.god) {
		G.player.atk = 40;
		G.player.max_hp = 400;
		G.player.hp = 400;
	}
	assign_looks();
	var ration = make_potion();
	ration.effect = "mend";
	ration.name = POTIONS["mend"]["name"];
	G.player.inventory.push(ration);
	log_msg(`${G.player.name} the ${species_name(G.player.species_id)} ${class_name(G.player.class_id)} enters the Descent.`);
	log_msg(col("90", "(? for help. The stairs down look like '>'.)"));
	enter_floor();
}

// ---------------------------------------------------------- interaction ----
function player_move(int dx, int dy) -> bool {
	var nx = G.player.x + dx;
	var ny = G.player.y + dy;
	var ti = monster_index_at(nx, ny);
	if (ti >= 0) {
		if (G.monsters[ti].disguised) {
			G.monsters[ti].disguised = false;
			log_msg(col("91", "That loot has TEETH. Grinnest!"));
		}
		attack(G.player, G.monsters[ti], player_attack_verb());
		return true;
	}
	if (!is_walkable(nx, ny)) { return false; }
	G.player.x = nx;
	G.player.y = ny;
	on_step_tile();
	return true;
}

function on_step_tile() -> void {
	var t = tile_at(G.player.x, G.player.y);
	if (t == TILE_STAIRS) {
		log_msg("Stairs down. Press > to descend.");
	} else if (t == TILE_STATUE) {
		log_msg(col("93", pick(RNG, STATUE_JAI)));
		if (!G.statue_used) {
			G.statue_used = true;
			add_effect(G.player, "blessed", 40, 10);
			log_msg(col("93", "JAI's grin warms you: +10 evasion for a while."));
		}
	} else if (t == TILE_MURAL) {
		log_msg(col("95", pick(RNG, MURAL_DEVACOR)));
		if (!G.mural_used) {
			G.mural_used = true;
			var healed = restore_stat(G.player.hp, G.player.total_max_hp(), 999);
			if (healed > 0) { log_msg(col("92", `Painted warmth closes your wounds (+${healed} HP).`)); }
			else { log_msg(col("90", "The colors hum, content that you are whole.")); }
		}
	}
	var gi = ground_index_at(G.player.x, G.player.y);
	if (gi >= 0) {
		log_msg(`You see ${item_label(G.ground[gi])} here. (g to grab)`);
	}
}

function pickup_here() -> bool {
	var gi = ground_index_at(G.player.x, G.player.y);
	if (gi < 0) { log_msg("Nothing here but dungeon."); return false; }
	if (give_item(G.player, G.ground[gi])) {
		G.ground.remove(gi);
		return true;
	}
	return false;
}

function quaff_first_potion() -> bool {
	for (int i = 0; i < G.player.inventory.size(); ++i) {
		if (G.player.inventory[i].kind == "potion") {
			drink_potion(G.player, i);
			return true;
		}
	}
	log_msg("No potions in your pack.");
	return false;
}

function try_descend() -> bool {
	var on_stairs = tile_at(G.player.x, G.player.y) == TILE_STAIRS;
	if (!on_stairs && !G.player.god) { log_msg("You need to stand on stairs (>) to descend."); return false; }
	if (G.floor >= 10) { log_msg("There is nothing below floor 10 but the story's end."); return false; }
	G.floor = G.floor + 1;
	enter_floor();
	return true;
}

function handle_key(string key) -> bool {
	if (DIR_KEYS.has(key)) {
		var d = DIR_KEYS[key];
		return player_move(d[0], d[1]);
	}
	switch (key) {
		case ".": return true;
		case "g": return pickup_here();
		case "i": return inventory_screen();
		case "q": return quaff_first_potion();
		case "z": return use_skill();
		case "E": return smart_equip(G.player);
		case ">": return try_descend();
		case "c": character_screen(); return false;
		case "?": help_screen(); return false;
		case "S": {
			save_game();
			G.over = true;
			G.saved = true;
			G.quit = true;
			return false;
		}
		case "Q": {
			G.over = true;
			G.quit = true;
			return false;
		}
		case "R": {
			if (HOST_DEV) {
				reload_scripts();
				log_msg(col("95", "Scripts reloaded. The dungeon shivers as its rules are rewritten."));
			}
			return false;
		}
	}
	return false;
}

// -------------------------------------------------------------- turns ------
function monsters_act() -> void {
	for (auto& m : G.monsters) {
		if (G.over) { return; }
		if (!m.alive) { continue; }
		if (!m.aggro && cheb(m.x, m.y, G.player.x, G.player.y) > 14) { continue; }
		monster_act(m);
	}
	// sweep the fallen
	var kept = [];
	for (auto m : G.monsters) {
		if (m.alive) { kept.push(m); }
	}
	G.monsters = kept;
}

function world_tick() -> void {
	tick_effects(G.player);
	if (G.over) { return; }
	for (auto& m : G.monsters) {
		if (m.alive) { tick_effects(m); }
	}
	if (G.turn % 4 == 0) {
		restore_stat(G.player.focus, G.player.total_max_focus(), 1);
	}
	if (G.turn % 5 == 0 && G.player.hp < G.player.total_max_hp() && nearest_visible_monster_index(99) < 0) {
		heal_entity(G.player, 1);   // catching your breath, out of sight of trouble
	}
}

function accumulate_hash() -> void {
	var h = G.hash;
	h = mix32(h, G.turn);
	h = mix32(h, G.floor);
	h = mix32(h, G.player.x * 256 + G.player.y);
	h = mix32(h, G.player.hp * 1024 + G.player.focus);
	h = mix32(h, G.player.xp * 64 + G.player.level);
	h = mix32(h, G.player.gold);
	h = mix32(h, G.player.inventory.size() * 16 + G.player.effects.size());
	var live = 0;
	var acc = 0;
	for (auto m : G.monsters) {
		if (!m.alive) { continue; }
		live = live + 1;
		acc = (acc + m.x * 977 + m.y * 331 + m.hp * 7) & 0xFFFFFFFF;
	}
	h = mix32(h, live);
	h = mix32(h, acc);
	h = mix32(h, G.ground.size());
	h = mix32(h, RNG.state());
	G.hash = h;
}

function game_loop() -> void {
	while (!G.over) {
		compute_fov();
		if (!G.smoke) { render_game(); }
		if (G.level_pending) {
			level_up_screen();
			if (G.over) { break; }
			if (!G.smoke) { render_game(); }
		}
		var key = G.autopilot ? autopilot_key() : read_key();
		var acted = handle_key(key);
		if (G.over) { break; }
		if (acted) {
			G.last_dmg = 0;
			G.last_dmg_src = "";
			monsters_act();
			if (!G.over) { world_tick(); }
			G.turn = G.turn + 1;
			if (G.smoke) { accumulate_hash(); }
			if (G.smoke && G.turn >= HOST_TURNS) { G.over = true; G.quit = true; }
		}
		if (G.boss_slain && !G.victory) {
			G.victory = true;
			G.over = true;
		}
	}

	if (G.victory) {
		compute_fov();
		victory_ceremony(G.player.x, G.player.y);
		victory_screen();
		delete_file(SAVE_PATH);
	} else if (!G.quit) {
		death_screen();
		delete_file(SAVE_PATH);
	} else if (G.saved) {
		host_log("Saved. Rest well; the Descent keeps your place.");
	}
}

// ----------------------------------------------------------- autopilot -----
// A coroutine key generator: BFS to the stairs, punch what blocks, sip when
// hurt. It feeds keys into the same handle_key() a human uses.
var KEY_OF_DIR = {
	"-1,0": "h", "1,0": "l", "0,-1": "k", "0,1": "j",
	"-1,-1": "y", "1,-1": "u", "-1,1": "b", "1,1": "n"
};

function dir_key(int dx, int dy) -> string {
	return KEY_OF_DIR[`${dx},${dy}`];
}

function pilot_has_upgrade() -> bool {
	for (auto it : G.player.inventory) {
		if (it.kind != "weapon" && it.kind != "armor" && it.kind != "trinket") { continue; }
		var current = null;
		if (it.kind == "weapon") { current = G.player.weapon; }
		else if (it.kind == "armor") { current = G.player.armor; }
		else { current = G.player.trinket; }
		if (current == null || gear_score(it) > gear_score(current)) { return true; }
	}
	return false;
}

function autopilot_key() -> string {
	if (PILOT_CO == null || PILOT_CO.done()) { PILOT_CO = pilot_brain(); }
	var key = PILOT_CO.resume();
	if (key == null) { return "."; }
	return key;
}

coroutine string pilot_brain() {
	// committed loot target lives in the coroutine's suspended state — without
	// it the goal flickers at the detour-range boundary and the pilot paces
	var loot_x = -1;
	var loot_y = -1;
	var loot_floor = -1;
	while (true) {
		// sip when scared
		var wants_potion = G.player.hp * 3 < G.player.total_max_hp();
		if (wants_potion) {
			var has_potion = false;
			for (auto it : G.player.inventory) {
				if (it.kind == "potion") { has_potion = true; }
			}
			if (has_potion) { yield "q"; continue; }
		}
		// grab loot underfoot
		if (ground_index_at(G.player.x, G.player.y) >= 0 && G.player.inventory.size() < 22) {
			yield "g";
			continue;
		}
		// stairs underfoot: descend
		if (tile_at(G.player.x, G.player.y) == TILE_STAIRS) { yield ">"; continue; }
		// wear upgrades as soon as they're in the pack
		if (pilot_has_upgrade()) { yield "E"; continue; }
		// skill on a visible target sometimes
		if (G.player.focus >= G.player.skill_cost && RNG.chance(25)) {
			var ti = nearest_visible_monster_index(G.player.skill == "bulwark" ? 1 : 5);
			if (ti >= 0) { yield "z"; continue; }
		}
		// adjacent enemy: swing at it
		var swung = false;
		for (auto m : G.monsters) {
			if (m.alive && cheb(m.x, m.y, G.player.x, G.player.y) == 1) {
				yield dir_key(sign_of(m.x - G.player.x), sign_of(m.y - G.player.y));
				swung = true;
				break;
			}
		}
		if (swung) { continue; }
		// walk toward the goal: committed loot first, then stairs (boss on 10)
		if (loot_floor != G.floor || ground_index_at(loot_x, loot_y) < 0) {
			loot_x = -1;   // picked up, gone, or stale floor
		}
		if (loot_x < 0 && G.player.inventory.size() < 20) {
			var best_d = 14;
			for (auto it : G.ground) {
				var d = cheb(it.x, it.y, G.player.x, G.player.y);
				if (d < best_d) { best_d = d; loot_x = it.x; loot_y = it.y; loot_floor = G.floor; }
			}
		}
		var gx = loot_x >= 0 ? loot_x : G.stairs_x;
		var gy = loot_x >= 0 ? loot_y : G.stairs_y;
		if (G.floor >= 10) {
			gx = -1;
			for (auto m : G.monsters) {
				if (m.alive && m.behavior == "boss") { gx = m.x; gy = m.y; }
			}
			if (gx < 0) { yield "."; continue; }
		}
		var step = bfs_step(G.player.x, G.player.y, gx, gy);
		if (step != null) {
			// stepping into a monster is an attack; that's fine
			yield dir_key(step[0], step[1]);
			continue;
		}
		// no path (shouldn't happen): drop the goal and shuffle
		loot_x = -1;
		var d = EIGHT_DIRS[RNG.next(8)];
		yield dir_key(d[0], d[1]);
	}
}

// ------------------------------------------------------------- saving ------
// The save JSON must contain NO nulls and NO empty containers: from_json
// builds those without an engine pointer and any later deep copy of the
// tree throws "Cannot clone script_value: missing engine pointer" (2026-07).
// So: sentinel "_pad" rows/keys on save, filtered back out on load.
function pad_effect_row() -> var { return {"kind": "_pad", "turns": 0, "power": 0}; }

function padded_map(var m) -> var {
	var out = {"_pad": true};
	for (auto kv : m) { out[kv.first] = kv.second; }
	return out;
}

function padded_effects(var fx_list) -> var {
	var rows = [pad_effect_row()];
	for (auto fx : fx_list) { rows.push(fx); }
	return rows;
}

function item_to_map(var item) -> var {
	var bonus_out = {"_pad": 0};
	for (auto kv : item.bonus) { bonus_out[kv.first] = kv.second; }
	return {
		"name": item.name, "kind": item.kind, "glyph": item.glyph, "color": item.color,
		"rarity": item.rarity, "atk": item.atk, "def": item.def, "bonus": bonus_out,
		"effect": item.effect, "amount": item.amount, "x": item.x, "y": item.y
	};
}

function save_game() -> void {
	var inv = [{"kind": "_pad"}];
	for (auto it : G.player.inventory) { inv.push(item_to_map(it)); }
	var ground_rows = [{"kind": "_pad"}];
	for (auto it : G.ground) { ground_rows.push(item_to_map(it)); }
	var monster_rows = [{"kind": "_pad"}];
	for (auto m : G.monsters) {
		if (!m.alive) { continue; }
		monster_rows.push({
			"kind": m.kind, "id": m.id, "x": m.x, "y": m.y, "hp": m.hp, "max_hp": m.max_hp,
			"aggro": m.aggro, "disguised": m.disguised, "atk": m.atk, "def": m.def,
			"move_clock": m.move_clock, "effects": padded_effects(m.effects)
		});
	}
	var p = G.player;   // read-only snapshot; fine for serializing
	var state = {
		"version": 2,
		"seed": HOST_SEED,
		"rng": RNG.state(),
		"turn": G.turn,
		"next_id": G.next_id,
		"floor": G.floor,
		"hash": G.hash,
		"stairs": [G.stairs_x, G.stairs_y],
		"statue_used": G.statue_used,
		"mural_used": G.mural_used,
		"known_potions": padded_map(G.known_potions),
		"known_scrolls": padded_map(G.known_scrolls),
		"potion_looks": G.potion_looks,
		"scroll_looks": G.scroll_looks,
		"tiles": G.tiles,
		"seen": G.seen,
		"monsters": monster_rows,
		"ground": ground_rows,
		"player": {
			"name": p.name, "species": p.species_id, "class": p.class_id,
			"x": p.x, "y": p.y, "hp": p.hp, "max_hp": p.max_hp,
			"atk": p.atk, "def": p.def, "eva": p.eva, "crit": p.crit,
			"level": p.level, "xp": p.xp, "gold": p.gold,
			"focus": p.focus, "max_focus": p.max_focus,
			"kills": p.kills, "stubborn_spent": p.stubborn_spent,
			"god": p.god,
			"weapon": p.weapon == null ? {"kind": "none"} : item_to_map(p.weapon),
			"armor": p.armor == null ? {"kind": "none"} : item_to_map(p.armor),
			"trinket": p.trinket == null ? {"kind": "none"} : item_to_map(p.trinket),
			"inventory": inv,
			"effects": padded_effects(p.effects)
		}
	};
	write_file(SAVE_PATH, to_json(state));
}

function load_game() -> bool {
	var text = read_file(SAVE_PATH);
	if (text == "") { return false; }
	var state = from_json(text);
	if (state == null) { return false; }

	// SCHEMA-DRIVEN leaf reads only. A from_json tree cannot be deep-copied
	// (nested values carry no engine pointer -> uncatchable "Cannot clone
	// script_value"), and even .keys() on it throws. Single element-read
	// expressions attach the engine, so every field is read individually.
	RNG.set_state(state["rng"]);
	var species_row = null;
	for (auto s : SPECIES) { if (s["id"] == state["player"]["species"]) { species_row = s; } }
	var class_row = null;
	for (auto c : CLASSES) { if (c["id"] == state["player"]["class"]) { class_row = c; } }
	if (species_row == null || class_row == null) { return false; }

	var p = Player(species_row, class_row);
	p.name = state["player"]["name"];
	p.x = state["player"]["x"];
	p.y = state["player"]["y"];
	p.hp = state["player"]["hp"];
	p.max_hp = state["player"]["max_hp"];
	p.atk = state["player"]["atk"];
	p.def = state["player"]["def"];
	p.eva = state["player"]["eva"];
	p.crit = state["player"]["crit"];
	p.level = state["player"]["level"];
	p.xp = state["player"]["xp"];
	p.gold = state["player"]["gold"];
	p.focus = state["player"]["focus"];
	p.max_focus = state["player"]["max_focus"];
	p.kills = state["player"]["kills"];
	p.stubborn_spent = state["player"]["stubborn_spent"];
	p.god = state["player"]["god"];
	p.effects = load_effects(state, "player", -1);
	if (state["player"]["weapon"]["kind"] != "none") { p.weapon = load_item(state, "weapon", -1); }
	if (state["player"]["armor"]["kind"] != "none") { p.armor = load_item(state, "armor", -1); }
	if (state["player"]["trinket"]["kind"] != "none") { p.trinket = load_item(state, "trinket", -1); }
	for (int i = 0; i < state["player"]["inventory"].size(); ++i) {
		if (state["player"]["inventory"][i]["kind"] == "_pad") { continue; }
		p.inventory.push(load_item(state, "inventory", i));
	}

	G.player = p;
	G.turn = state["turn"];
	G.next_id = state["next_id"];
	G.floor = state["floor"];
	G.hash = state["hash"];
	G.stairs_x = state["stairs"][0];
	G.stairs_y = state["stairs"][1];
	G.statue_used = state["statue_used"];
	G.mural_used = state["mural_used"];
	G.known_potions = {};
	for (auto id : POTIONS.keys()) {
		if (state["known_potions"].has(id)) { G.known_potions[id] = true; }
		G.potion_looks[id] = state["potion_looks"][id];
	}
	G.known_scrolls = {};
	for (auto id : SCROLLS.keys()) {
		if (state["known_scrolls"].has(id)) { G.known_scrolls[id] = true; }
		G.scroll_looks[id] = state["scroll_looks"][id];
	}
	G.tiles = [];
	for (int i = 0; i < state["tiles"].size(); ++i) { G.tiles.push(state["tiles"][i]); }
	G.seen = [];
	for (int i = 0; i < state["seen"].size(); ++i) { G.seen.push(state["seen"][i]); }

	G.monsters = [];
	for (int i = 0; i < state["monsters"].size(); ++i) {
		if (state["monsters"][i]["kind"] == "_pad") { continue; }
		var kind_name = state["monsters"][i]["kind"];
		var m = Monster(kind_name);
		m.id = state["monsters"][i]["id"];
		m.x = state["monsters"][i]["x"];
		m.y = state["monsters"][i]["y"];
		m.hp = state["monsters"][i]["hp"];
		m.max_hp = state["monsters"][i]["max_hp"];
		m.aggro = state["monsters"][i]["aggro"];
		m.disguised = state["monsters"][i]["disguised"];
		m.atk = state["monsters"][i]["atk"];
		m.def = state["monsters"][i]["def"];
		m.move_clock = state["monsters"][i]["move_clock"];
		m.effects = load_effects(state, "monster", i);
		G.monsters.push(m);
	}
	G.ground = [];
	for (int i = 0; i < state["ground"].size(); ++i) {
		if (state["ground"][i]["kind"] == "_pad") { continue; }
		G.ground.push(load_item(state, "ground", i));
	}
	BOSS_CO = null;   // the boss script restarts its pattern; his memory is short
	log_msg(col("96", `Welcome back to floor ${G.floor}, ${G.player.name}.`));
	return true;
}

var BONUS_KEYS = ["atk", "def", "crit", "eva", "hp", "focus", "burn"];

// Readers take the whole parsed tree by var& (a local IS an lvalue; a chained
// element expression is NOT — "Cannot pass non-lvalue to reference parameter")
// and re-walk the path per leaf. Verbose, but every read attaches the engine.
function item_field(var& root, string slot, int idx, string field) -> var {
	if (slot == "inventory") { return root["player"]["inventory"][idx][field]; }
	if (slot == "ground") { return root["ground"][idx][field]; }
	return root["player"][slot][field];
}

function item_bonus_field(var& root, string slot, int idx, string key) -> var {
	if (slot == "inventory") {
		if (!root["player"]["inventory"][idx]["bonus"].has(key)) { return null; }
		return root["player"]["inventory"][idx]["bonus"][key];
	}
	if (slot == "ground") {
		if (!root["ground"][idx]["bonus"].has(key)) { return null; }
		return root["ground"][idx]["bonus"][key];
	}
	if (!root["player"][slot]["bonus"].has(key)) { return null; }
	return root["player"][slot]["bonus"][key];
}

function load_item(var& root, string slot, int idx) -> var {
	var item = Item();
	item.name = item_field(root, slot, idx, "name");
	item.kind = item_field(root, slot, idx, "kind");
	item.glyph = item_field(root, slot, idx, "glyph");
	item.color = item_field(root, slot, idx, "color");
	item.rarity = item_field(root, slot, idx, "rarity");
	item.atk = item_field(root, slot, idx, "atk");
	item.def = item_field(root, slot, idx, "def");
	item.effect = item_field(root, slot, idx, "effect");
	item.amount = item_field(root, slot, idx, "amount");
	item.x = item_field(root, slot, idx, "x");
	item.y = item_field(root, slot, idx, "y");
	for (auto k : BONUS_KEYS) {
		if (k == "_pad") { continue; }
		var v = item_bonus_field(root, slot, idx, k);
		if (v != null) { item.bonus[k] = v; }
	}
	return item;
}

function effect_field(var& root, string owner, int mi, int fi, string field) -> var {
	if (owner == "monster") { return root["monsters"][mi]["effects"][fi][field]; }
	return root["player"]["effects"][fi][field];
}

function load_effects(var& root, string owner, int mi) -> var {
	var count = 0;
	if (owner == "monster") { count = root["monsters"][mi]["effects"].size(); }
	else { count = root["player"]["effects"].size(); }
	var out = [];
	for (int i = 0; i < count; ++i) {
		if (effect_field(root, owner, mi, i, "kind") == "_pad") { continue; }
		out.push({
			"kind": effect_field(root, owner, mi, i, "kind"),
			"turns": effect_field(root, owner, mi, i, "turns"),
			"power": effect_field(root, owner, mi, i, "power")
		});
	}
	return out;
}

// -------------------------------------------------------------- summary ----
function print_run_summary(string label) -> void {
	var p = G.player;
	host_log("");
	host_log(`=== ${label} ===`);
	host_log(`backend: ${HOST_BACKEND}  seed: ${HOST_SEED}`);
	host_log(`turns: ${G.turn}  floor: ${G.floor}  level: ${p.level}`);
	host_log(`hp: ${p.hp}/${p.total_max_hp()}  kills: ${p.kills}  gold: ${p.gold}  items: ${p.inventory.size()}`);
	var wname = p.weapon == null ? "-" : p.weapon.name;
	var aname = p.armor == null ? "-" : p.armor.name;
	host_log(`weapon: ${wname}  armor: ${aname}`);
	host_log(`victory: ${G.victory}  dead: ${!p.alive}  cause: ${G.death_cause}`);
	host_log(`STATE_HASH: ${G.hash}`);
	// last few log lines: lets scripted/smoke runs eyeball the combat narration
	var mstart = imax(0, G.msgs.size() - 6);
	for (int mi = mstart; mi < G.msgs.size(); ++mi) { host_log("  log| " + G.msgs[mi]); }
}
