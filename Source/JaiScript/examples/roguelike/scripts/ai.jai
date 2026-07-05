// Monster AI. Per-monster brains are procedural (behavior switch + state in
// fields) because coroutine handles refuse to live inside objects/containers
// under value semantics. The boss is the exception: one monster, one global
// slot, one genuine multi-phase coroutine (BOSS_CO in state.jai).

function sees_player(var& m) -> bool {
	if (cheb(m.x, m.y, G.player.x, G.player.y) > 9) { return false; }
	return line_clear(m.x, m.y, G.player.x, G.player.y);
}

function monster_step(var& m, int dx, int dy) -> bool {
	var nx = m.x + dx;
	var ny = m.y + dy;
	if (!in_bounds(nx, ny) || !is_walkable(nx, ny)) { return false; }
	if (nx == G.player.x && ny == G.player.y) { return false; }
	if (monster_index_at(nx, ny) >= 0) { return false; }
	m.x = nx;
	m.y = ny;
	return true;
}

function sign_of(int v) -> int { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

// greedy step with slide fallbacks; good enough to feel hungry
function step_toward(var& m, int tx, int ty) -> void {
	var dx = sign_of(tx - m.x);
	var dy = sign_of(ty - m.y);
	if (monster_step(m, dx, dy)) { return; }
	if (dx != 0 && monster_step(m, dx, 0)) { return; }
	if (dy != 0 && monster_step(m, 0, dy)) { return; }
	// slide around corners
	if (dx == 0) {
		if (monster_step(m, 1, dy)) { return; }
		if (monster_step(m, -1, dy)) { return; }
	}
	if (dy == 0) {
		if (monster_step(m, dx, 1)) { return; }
		monster_step(m, dx, -1);
	}
}

function step_away(var& m, int tx, int ty) -> void {
	step_toward(m, m.x + (m.x - tx) * 2, m.y + (m.y - ty) * 2);
}

function wander(var& m) -> void {
	var d = EIGHT_DIRS[RNG.next(8)];
	monster_step(m, d[0], d[1]);
}

function adjacent_to_player(var& m) -> bool {
	return cheb(m.x, m.y, G.player.x, G.player.y) <= 1;
}

// one action for one monster; m is a live reference into G.monsters
function monster_act(var& m) -> void {
	switch (m.behavior) {
		case "chase": {
			if (adjacent_to_player(m)) { attack(m, G.player, "claws"); }
			else if (m.aggro || sees_player(m)) {
				m.aggro = true;
				step_toward(m, G.player.x, G.player.y);
			}
			break;
		}
		case "ranged": {
			var dist = cheb(m.x, m.y, G.player.x, G.player.y);
			var in_sight = sees_player(m);
			if (in_sight) { m.aggro = true; }
			if (adjacent_to_player(m)) {
				if (RNG.chance(50)) { attack(m, G.player, "swats"); }
				else { step_away(m, G.player.x, G.player.y); }
			} else if (in_sight && dist <= m.range) {
				attack(m, G.player, "pelts");
			} else if (m.aggro) {
				step_toward(m, G.player.x, G.player.y);
			}
			break;
		}
		case "pack": {
			if (adjacent_to_player(m)) {
				attack(m, G.player, "bites");
			} else if (sees_player(m)) {
				// braver when a packmate also has eyes on the prize
				var packmates = 0;
				for (auto& other : G.monsters) {
					if (other.alive && other.behavior == "pack" && other.id != m.id && sees_player(other)) {
						packmates = packmates + 1;
					}
				}
				if (packmates >= 1 || m.aggro || m.hp < m.max_hp) {
					m.aggro = true;
					step_toward(m, G.player.x, G.player.y);
				} else {
					wander(m); // yip nervously and circle
				}
			}
			break;
		}
		case "erratic": {
			if (adjacent_to_player(m) && RNG.chance(70)) { attack(m, G.player, "rakes"); }
			else if (RNG.chance(40)) { wander(m); }
			else if (sees_player(m)) { step_toward(m, G.player.x, G.player.y); }
			break;
		}
		case "sluggish": {
			m.move_clock = m.move_clock + 1;
			if (adjacent_to_player(m)) { attack(m, G.player, "engulfs"); }
			else if (m.move_clock % 2 == 0 && sees_player(m)) {
				step_toward(m, G.player.x, G.player.y);
			}
			break;
		}
		case "mimic": {
			if (m.disguised) {
				if (adjacent_to_player(m)) {
					m.disguised = false;
					log_msg(col("91", "The loot grows teeth. It was a Grinnest all along!"));
					attack(m, G.player, "chomps");
				}
			} else if (adjacent_to_player(m)) {
				attack(m, G.player, "chomps");
			} else if (sees_player(m)) {
				step_toward(m, G.player.x, G.player.y);
			}
			break;
		}
		case "boss": {
			if (BOSS_CO == null || BOSS_CO.done()) { BOSS_CO = boss_script(m.id); }
			BOSS_CO.resume();
			break;
		}
	}
}

// ------------------------------------------------------------- the boss ----
// A genuine coroutine: suspended phase state across turns. It re-finds its
// body via id each resume (the handle may outlive a save/load or reload) and
// mutates the world through var& helpers and global chains.
function monster_index_by_id(int mid) -> int {
	for (int i = 0; i < G.monsters.size(); ++i) {
		if (G.monsters[i].alive && G.monsters[i].id == mid) { return i; }
	}
	return -1;
}

coroutine void boss_script(int boss_id) {
	var enraged = false;
	while (true) {
		var bi = monster_index_by_id(boss_id);
		if (bi < 0) { return; }

		if (!enraged && G.monsters[bi].hp * 10 < G.monsters[bi].max_hp * 3) {
			enraged = true;
			G.monsters[bi].atk = G.monsters[bi].atk + 3;
			log_msg(col("91", "Vexadrach ROARS. The gray fire in his gullet flickers... almost color."));
		}

		var dist = cheb(G.monsters[bi].x, G.monsters[bi].y, G.player.x, G.player.y);
		if (dist <= G.monsters[bi].range && dist > 1 && sees_player(G.monsters[bi])) {
			// telegraph, then breathe: two turns of committed drama
			log_msg(col("90", "Vexadrach's throat glows the color of a dead television."));
			yield;

			bi = monster_index_by_id(boss_id);
			if (bi < 0) { return; }
			if (cheb(G.monsters[bi].x, G.monsters[bi].y, G.player.x, G.player.y) <= G.monsters[bi].range
					&& sees_player(G.monsters[bi])) {
				log_msg(col("90", "HOLLOW FIRE: heatless light, lightless heat."));
				var dmg = imax(2, G.monsters[bi].atk - entity_def(G.player) + RNG.roll(0, 3));
				hurt_entity(G.player, dmg, "hollow fire");
				if (G.player.alive) { add_effect(G.player, "burn", 3, 2); }
			} else {
				log_msg(col("90", "The gray torrent scorches empty stone. You were elsewhere."));
			}
			yield;

			// recover turn (his one weakness: cardio)
			if (!enraged) { yield; }
			continue;
		}

		if (adjacent_to_player(G.monsters[bi])) {
			attack(G.monsters[bi], G.player, "savages");
		} else if (sees_player(G.monsters[bi]) || G.monsters[bi].aggro) {
			G.monsters[bi].aggro = true;
			step_toward(G.monsters[bi], G.player.x, G.player.y);
		}
		yield;
	}
}
