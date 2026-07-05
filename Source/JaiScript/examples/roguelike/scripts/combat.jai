// Combat resolution, damage/healing, XP, and class skills.
// Everything that mutates an entity takes it by var& — value semantics means
// a plain `var` param would quietly maul a deep copy instead.

function entity_atk(var& e) -> int {
	if (e.glyph == "@") { return e.total_atk(); }
	return e.atk;
}

function entity_def(var& e) -> int {
	if (e.glyph == "@") { return e.total_def(); }
	return e.def;
}

function entity_eva(var& e) -> int {
	if (e.glyph == "@") { return e.total_eva(); }
	return e.eva;
}

function entity_crit(var& e) -> int {
	if (e.glyph == "@") { return e.total_crit(); }
	return e.crit;
}

// Damage funnel. Every hit point lost in the game passes through here.
function hurt_entity(var& target, int amount, string source) -> void {
	if (!target.alive) { return; }
	if (target.glyph == "@") {
		if (target.god) { return; }
		target.hp = target.hp - amount;
		if (target.trait == "curl" && target.hp * 3 < target.total_max_hp() && !target.has_effect("curl")) {
			add_effect(target, "curl", 5, 3);
			log_msg(col("90", "You curl: scales lock into a living shield. (+3 def)"));
		}
		if (target.hp <= 0) {
			if (target.trait == "stubborn" && !target.stubborn_spent) {
				target.stubborn_spent = true;
				target.hp = 1;
				log_msg(col("93", "Too stubborn to die! You refuse, loudly, at 1 HP."));
				return;
			}
			target.hp = 0;
			target.alive = false;
			G.over = true;
			G.death_cause = source;
		}
		return;
	}
	target.hp = target.hp - amount;
	if (target.hp <= 0) {
		target.hp = 0;
		target.alive = false;
		on_monster_death(target, source);
	}
}

function heal_entity(var& target, int amount) -> void {
	if (!target.alive) { return; }
	var cap = target.glyph == "@" ? target.total_max_hp() : target.max_hp;
	target.hp = imin(cap, target.hp + amount);
}

function on_monster_death(var& monster, string source) -> void {
	log_msg(col("92", `The ${monster.name} is destroyed!`));
	if (monster.behavior == "boss") {
		G.boss_slain = true;
		return; // victory ceremony runs from the game loop
	}
	G.player.kills = G.player.kills + 1;
	gain_xp(monster.xp_reward());
	var coins = monster.gold_reward(RNG);
	if (coins > 0) {
		var pile = make_gold(G.floor);
		pile.amount = coins;
		pile.x = monster.x;
		pile.y = monster.y;
		G.ground.push(pile);
	}
	if (RNG.chance(18 + monster.tier * 3)) {
		var drop = make_floor_item(G.floor);
		drop.x = monster.x;
		drop.y = monster.y;
		G.ground.push(drop);
		log_msg(`The ${monster.name} drops something.`);
	}
}

function gain_xp(int amount) -> void {
	G.player.xp = G.player.xp + amount;
	while (G.player.xp >= G.player.xp_next()) {
		G.player.xp = G.player.xp - G.player.xp_next();
		G.player.level = G.player.level + 1;
		G.player.max_hp = G.player.max_hp + 2;
		G.player.hp = imin(G.player.hp + 2, G.player.total_max_hp());
		G.level_pending = true;
		log_msg(col("93", `Welcome to level ${G.player.level}! Choose a boon.`));
	}
}

// attacker swings at defender; melee and ranged share this resolution
function attack(var& attacker, var& defender, string verb) -> void {
	var dodge = entity_eva(defender);
	if (RNG.chance(iclamp(dodge, 0, 60))) {
		if (defender.glyph == "@") { log_msg(`You slip aside from the ${attacker.name}.`); }
		else { log_msg(`The ${defender.name} skitters out of reach.`); }
		return;
	}
	var dmg = entity_atk(attacker) + RNG.roll(-1, 2) - entity_def(defender);
	dmg = imax(1, dmg);
	var crit = RNG.chance(iclamp(entity_crit(attacker), 0, 60));
	if (crit) { dmg = dmg * 2; }
	if (attacker.glyph == "@") {
		var crit_note = crit ? col("93", " Critical!") : "";
		log_msg(`You ${verb} the ${defender.name} for ${dmg}.${crit_note}`);
		// weapon burn affix
		if (attacker.equip_bonus("burn") > 0 && RNG.chance(attacker.equip_bonus("burn"))) {
			add_effect(defender, "burn", 3, 2);
			log_msg(col("91", `The ${defender.name} catches fire!`));
		}
	} else {
		var crit_note = crit ? " It hurts extra." : "";
		log_msg(`The ${attacker.name} ${verb} you for ${dmg}.${crit_note}`);
	}
	hurt_entity(defender, dmg, attacker.name);
	if (attacker.glyph != "@" && attacker.burns && defender.alive && RNG.chance(25)) {
		add_effect(defender, "burn", 3, 2);
		log_msg(col("91", "Gray fire clings to you!"));
	}
	if (attacker.glyph != "@" && attacker.withers && defender.alive && RNG.chance(35)) {
		add_effect(defender, "wither", 8, 2);
		log_msg(col("95", "Curse-dust settles into your joints. (-atk/-def)"));
	}
}

// ------------------------------------------------------------- skills ------
// index into G.monsters, or -1 — returning the object would return a COPY
function nearest_visible_monster_index(int max_range) -> int {
	var best = -1;
	var best_dist = max_range + 1;
	for (int i = 0; i < G.monsters.size(); ++i) {
		if (!G.monsters[i].alive || G.monsters[i].disguised) { continue; }
		if (!is_visible(G.monsters[i].x, G.monsters[i].y)) { continue; }
		var d = cheb(G.player.x, G.player.y, G.monsters[i].x, G.monsters[i].y);
		if (d < best_dist) { best_dist = d; best = i; }
	}
	return best;
}

// spend through a reference into the player object — mutation in place
function spend_focus(int& pool, int cost) -> bool {
	if (pool < cost) { return false; }
	pool = pool - cost;
	return true;
}

function use_skill() -> bool {
	switch (G.player.skill) {
		case "bulwark": {
			var ti = nearest_visible_monster_index(1);
			if (ti < 0) { log_msg("Nothing in slamming range."); return false; }
			if (!spend_focus(G.player.focus, G.player.skill_cost)) { log_msg("Not enough focus."); return false; }
			var dmg = imax(1, (G.player.total_atk() * 5) / 2 - entity_def(G.monsters[ti]));
			log_msg(col("96", `BULWARK SLAM! The ${G.monsters[ti].name} takes ${dmg}.`));
			hurt_entity(G.monsters[ti], dmg, "a bulwark slam");
			return true;
		}
		case "emberbolt": {
			var ti = nearest_visible_monster_index(6);
			if (ti < 0) { log_msg("No target in sight."); return false; }
			if (!spend_focus(G.player.focus, G.player.skill_cost)) { log_msg("Not enough focus."); return false; }
			var dmg = imax(1, G.player.total_atk() + 3 - entity_def(G.monsters[ti]));
			log_msg(col("91", `Emberbolt sears the ${G.monsters[ti].name} for ${dmg}!`));
			hurt_entity(G.monsters[ti], dmg, "an emberbolt");
			if (G.monsters[ti].alive) { add_effect(G.monsters[ti], "burn", 3, 2); }
			return true;
		}
		case "thornvolley": {
			var ti = nearest_visible_monster_index(5);
			if (ti < 0) { log_msg("No target in sight."); return false; }
			if (!spend_focus(G.player.focus, G.player.skill_cost)) { log_msg("Not enough focus."); return false; }
			var dmg = imax(1, G.player.total_atk() - entity_def(G.monsters[ti]));
			log_msg(col("92", `Thorns skewer the ${G.monsters[ti].name} for ${dmg}!`));
			hurt_entity(G.monsters[ti], dmg, "a thorn volley");
			return true;
		}
		case "wither": {
			var ti = nearest_visible_monster_index(6);
			if (ti < 0) { log_msg("No target in sight."); return false; }
			if (!spend_focus(G.player.focus, G.player.skill_cost)) { log_msg("Not enough focus."); return false; }
			add_effect(G.monsters[ti], "wither", 8, 2);
			heal_entity(G.player, 3);
			log_msg(col("95", `The ${G.monsters[ti].name} wilts; you sip its vigor. (+3 HP)`));
			return true;
		}
	}
	return false;
}
