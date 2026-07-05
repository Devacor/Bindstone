// Item generation (rarity + affixes), inventory operations, potions & scrolls.

function roll_rarity(int floor) -> var {
	// deeper floors skew the weights toward the shiny end
	var adjusted = [];
	for (auto row : RARITIES) {
		var weight = row[0];
		if (row[1] == "common") { weight = imax(10, weight - floor * 3); }
		if (row[1] == "rare" || row[1] == "epic") { weight = weight + floor; }
		if (row[1] == "legendary" && floor >= 6) { weight = weight + 2; }
		adjusted.push([weight, row]);
	}
	return pick_weighted(RNG, adjusted);
}

function make_equipment(int floor) -> var {
	var item = Item();
	var slot_roll = RNG.next(100);
	var base = null;
	if (slot_roll < 40) {
		item.kind = "weapon";
		item.glyph = ")";
		base = WEAPON_BASES[imin(WEAPON_BASES.size() - 1, RNG.next(1 + imin(floor, 8) / 2))];
		item.atk = base["atk"];
	} else if (slot_roll < 75) {
		item.kind = "armor";
		item.glyph = "[";
		base = ARMOR_BASES[imin(ARMOR_BASES.size() - 1, RNG.next(1 + imin(floor, 7) / 2))];
		item.def = base["def"];
	} else {
		item.kind = "trinket";
		item.glyph = "=";
		base = TRINKET_BASES[RNG.next(TRINKET_BASES.size())];
		item.bonus[base["bonus"]] = base["power"];
	}
	item.name = base["name"];

	var rarity_row = roll_rarity(floor);
	item.rarity = rarity_row[1];
	var affix_count = rarity_row[2];
	var stat_bump = rarity_row[3];
	item.atk = item.atk + (item.kind == "weapon" ? stat_bump : 0);
	item.def = item.def + (item.kind == "armor" ? stat_bump : 0);

	if (item.rarity == "legendary") {
		item.name = LEGENDARY_NAMES[item.kind];
		item.bonus["crit"] = item.bonus_of("crit") + 5;
		item.bonus["hp"] = item.bonus_of("hp") + 5;
	} else if (affix_count > 0) {
		var prefix = PREFIXES[RNG.next(PREFIXES.size())];
		item.name = prefix["name"] + " " + item.name;
		item.atk = item.atk + prefix["atk"];
		item.def = item.def + prefix["def"];
		if (affix_count > 1) {
			var suffix = SUFFIXES[RNG.next(SUFFIXES.size())];
			item.name = item.name + " " + suffix["name"];
			// hoisted: passing a container-element read to a script-class METHOD's
			// typed param throws "No matching overload found" (both backends, 2026-07)
			var bonus_key = suffix["bonus"];
			if (bonus_key == "atk") { item.atk = item.atk + suffix["power"]; }
			else if (bonus_key == "def") { item.def = item.def + suffix["power"]; }
			else { item.bonus[bonus_key] = item.bonus_of(bonus_key) + suffix["power"]; }
		}
	}
	item.color = RARITY_COLORS[item.rarity];
	return item;
}

function make_potion() -> var {
	var item = Item();
	item.kind = "potion";
	item.glyph = "!";
	var ids = POTIONS.keys();
	item.effect = ids[RNG.next(ids.size())];
	item.color = "95";
	item.name = POTIONS[item.effect]["name"];
	return item;
}

function make_scroll() -> var {
	var item = Item();
	item.kind = "scroll";
	item.glyph = "~";
	var ids = SCROLLS.keys();
	item.effect = ids[RNG.next(ids.size())];
	item.color = "94";
	item.name = SCROLLS[item.effect]["name"];
	return item;
}

function make_gold(int floor) -> var {
	var item = Item();
	item.kind = "gold";
	item.glyph = "$";
	item.color = "33";
	item.amount = RNG.roll(3, 8 + floor * 3);
	item.name = "gold";
	return item;
}

function make_floor_item(int floor) -> var {
	var roll = RNG.next(100);
	if (roll < 35) { return make_equipment(floor); }
	if (roll < 60) { return make_potion(); }
	if (roll < 75) { return make_scroll(); }
	return make_gold(floor);
}

// Display name honors identify-by-use: unknown potions/scrolls show their look.
function item_label(var item) -> string {
	if (item.kind == "potion") {
		if (G.known_potions.has(item.effect)) { return POTIONS[item.effect]["name"]; }
		return "a " + G.potion_looks[item.effect] + " potion";
	}
	if (item.kind == "scroll") {
		if (G.known_scrolls.has(item.effect)) { return SCROLLS[item.effect]["name"]; }
		return `a scroll labeled "${G.scroll_looks[item.effect]}"`;
	}
	if (item.kind == "gold") { return `${item.amount} gold`; }
	return item.name;
}

function item_stats_label(var item) -> string {
	var parts = [];
	if (item.atk != 0) { parts.push(`+${item.atk} atk`); }
	if (item.def != 0) { parts.push(`+${item.def} def`); }
	for (auto kv : item.bonus) {
		if (kv.first == "burn") { parts.push(`${kv.second}% burn`); }
		else { parts.push(`+${kv.second} ${kv.first}`); }
	}
	if (parts.size() == 0) { return ""; }
	var out = " (";
	for (int i = 0; i < parts.size(); ++i) {
		if (i > 0) { out = out + ", "; }
		out = out + parts[i];
	}
	return out + ")";
}

// ----------------------------------------------------------- inventory -----
function give_item(var& player, var item) -> bool {
	if (item.kind == "gold") {
		player.gold = player.gold + item.amount;
		log_msg(col("33", `You pocket ${item.amount} gold.`));
		return true;
	}
	if (player.inventory.size() >= 22) {
		log_msg("Your pack is stuffed full.");
		return false;
	}
	player.inventory.push(item);
	log_msg(`You pick up ${item_label(item)}${item_stats_label(item)}.`);
	return true;
}

function equip_item(var& player, int slot_index) -> void {
	var item = player.inventory[slot_index];
	if (item.kind == "potion") { drink_potion(player, slot_index); return; }
	if (item.kind == "scroll") { read_scroll(player, slot_index); return; }
	var swapped = null;
	if (item.kind == "weapon") { swapped = player.weapon; player.weapon = item; }
	else if (item.kind == "armor") { swapped = player.armor; player.armor = item; }
	else if (item.kind == "trinket") { swapped = player.trinket; player.trinket = item; }
	else { log_msg("You can't wear that."); return; }
	player.inventory.remove(slot_index);
	if (swapped != null) { player.inventory.push(swapped); }
	log_msg(`You equip ${item_label(item)}${item_stats_label(item)}.`);
	player.hp = imin(player.hp, player.total_max_hp());
	player.focus = imin(player.focus, player.total_max_focus());
}

function drop_item(var& player, int slot_index) -> void {
	var item = player.inventory[slot_index];
	player.inventory.remove(slot_index);
	item.x = player.x;
	item.y = player.y;
	G.ground.push(item);
	log_msg(`You drop ${item_label(item)}.`);
}

// restore through a reference: the call sites read like the mutation they are
function restore_stat(int& stat, int cap, int amount) -> int {
	var before = stat;
	stat = imin(cap, stat + amount);
	return stat - before;
}

function drink_potion(var& player, int slot_index) -> void {
	var item = player.inventory[slot_index];
	player.inventory.remove(slot_index);
	var first_taste = !G.known_potions.has(item.effect);
	G.known_potions[item.effect] = true;
	var proper = POTIONS[item.effect]["name"];
	var power = POTIONS[item.effect]["power"];
	if (first_taste) { log_msg(`You quaff it... it was a ${proper}!`); }
	else { log_msg(`You quaff the ${proper}.`); }
	switch (item.effect) {
		case "mend": {
			var healed = restore_stat(player.hp, player.total_max_hp(), power);
			log_msg(col("92", `You feel knitted together (+${healed} HP).`));
			break;
		}
		case "greatmend": {
			var healed = restore_stat(player.hp, player.total_max_hp(), power);
			log_msg(col("92", `Warmth floods you (+${healed} HP).`));
			break;
		}
		case "focus": {
			var gained = restore_stat(player.focus, player.total_max_focus(), power);
			log_msg(col("96", `Your thoughts sharpen (+${gained} focus).`));
			break;
		}
		case "rage": {
			add_effect(player, "rage", 20, power);
			log_msg(col("91", "Your teeth feel three sizes too big. (+atk)"));
			break;
		}
		case "stoneskin": {
			add_effect(player, "stoneskin", 20, power);
			log_msg(col("90", "Your hide sets like mortar. (+def)"));
			break;
		}
		case "regen": {
			add_effect(player, "regen", power, 2);
			log_msg(col("92", "Your scratches start closing on their own."));
			break;
		}
	}
}

function read_scroll(var& player, int slot_index) -> void {
	var item = player.inventory[slot_index];
	player.inventory.remove(slot_index);
	var first_read = !G.known_scrolls.has(item.effect);
	G.known_scrolls[item.effect] = true;
	var proper = SCROLLS[item.effect]["name"];
	if (first_read) { log_msg(`You read it aloud... a ${proper}!`); }
	else { log_msg(`You read the ${proper}.`); }
	switch (item.effect) {
		case "reveal": {
			for (int i = 0; i < MAP_W * MAP_H; ++i) { G.seen[i] = true; }
			log_msg(col("96", "Ink spiders across your mind: the whole floor, mapped."));
			break;
		}
		case "teleport": {
			teleport_player(player);
			break;
		}
		case "enfeeble": {
			var count = 0;
			for (auto& m : G.monsters) {
				if (m.alive && is_visible(m.x, m.y)) {
					add_effect(m, "wither", 12, SCROLLS[item.effect]["power"]);
					count = count + 1;
				}
			}
			log_msg(col("95", `A groan rolls through ${count} nearby horrors.`));
			break;
		}
		case "lore": {
			for (auto k : POTIONS.keys()) { G.known_potions[k] = true; }
			for (auto k : SCROLLS.keys()) { G.known_scrolls[k] = true; }
			log_msg(col("96", "Labels everywhere! You suddenly know all bottles and scrolls."));
			break;
		}
	}
}

function teleport_player(var& player) -> void {
	for (int attempt = 0; attempt < 200; ++attempt) {
		var tx = RNG.next(MAP_W);
		var ty = RNG.next(MAP_H);
		if (!is_walkable(tx, ty)) { continue; }
		if (monster_index_at(tx, ty) >= 0) { continue; }
		player.x = tx;
		player.y = ty;
		log_msg(col("95", "The world hiccups. You are somewhere else."));
		return;
	}
	log_msg("The scroll fizzles; elsewhere was full.");
}

// 'E' key: equip the first carried piece of gear that fills an empty slot or
// beats the current one (atk+def+bonus sum). Returns true if a swap happened.
function smart_equip(var& player) -> bool {
	for (int i = 0; i < player.inventory.size(); ++i) {
		var kind = player.inventory[i].kind;
		if (kind != "weapon" && kind != "armor" && kind != "trinket") { continue; }
		var current = null;
		if (kind == "weapon") { current = player.weapon; }
		else if (kind == "armor") { current = player.armor; }
		else { current = player.trinket; }
		var new_score = gear_score(player.inventory[i]);
		var old_score = current == null ? -1 : gear_score(current);
		if (new_score > old_score) {
			equip_item(player, i);
			return true;
		}
	}
	log_msg("Nothing in the pack beats what you're wearing.");
	return false;
}

function gear_score(var item) -> int {
	var score = item.atk * 2 + item.def * 2;
	for (auto kv : item.bonus) { score = score + kv.second; }
	return score;
}
