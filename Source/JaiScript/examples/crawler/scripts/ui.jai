// Frame composition and modal screens for the first-person crawler. The 3D
// viewport comes from view.jai; this file wraps it in the Moraff-style chrome:
// stats panel with a compass, the message log, the minimap, and the modals.
// Every line ends with ESC[K so frames overdraw each other without a clear.

var PANEL_W = 15;

function eol() -> string { return ESC + "[K"; }

function present(var lines) -> string {
	var line_end = eol() + "\n";
	var frame = lines.join(line_end) + line_end + ESC + "[J";
	draw(frame);
	return frame;
}

function bar(int cur, int cap, int width, string color_on) -> string {
	if (cap <= 0) { cap = 1; }
	var filled = iclamp((cur * width) / cap, 0, width);
	var out = col(color_on, "#".repeat(filled));
	return out + col("90", "-".repeat(width - filled));
}

// --------------------------------------------------------------- frames ----
var MAP_DIVIDER = col("90", "|");

function title_line() -> string {
	var god_tag = G.player.god ? col("95", " [GOD]") : "";
	return col("93", " GRUBWELL") + col("90", " -- dungeons of the unwashed --")
		+ `  Floor ${G.floor}/10  Turn ${G.turn}  ` + col("90", `~ ${theme_name(G.floor)} ~`) + god_tag;
}

function compose_frame(var view_lines) -> void {
	var lines = [];
	lines.push(title_line());
	var panel = build_panel();
	for (int y = 0; y < VIEW_H; ++y) {
		var side = y < panel.size() ? panel[y] : "";
		lines.push(view_lines[y] + MAP_DIVIDER + side);
	}
	var start = imax(0, G.msgs.size() - 5);
	for (int i = start; i < G.msgs.size(); ++i) {
		lines.push(" " + G.msgs[i]);
	}
	present(lines);
}

function render_game() -> void {
	frame_mark();   // --time probe: turn logic ends here, frame build begins
	if (G.show_map) {
		compose_frame(minimap_lines());
	} else {
		compose_frame(render_view(FACE_DX[G.facing], FACE_DY[G.facing]));
	}
}

// A fast 3-frame sweep between cardinals: the cheap trick that makes 90-degree
// snap turns read as motion. rot is -1 (left) or +1 (right).
function animate_turn(int old_facing, int rot) -> void {
	if (G.smoke || HOST_SCRIPTED || G.show_map) { return; }
	float dx0 = FACE_DX[old_facing];
	float dy0 = FACE_DY[old_facing];
	var sweep = [0.5236, 1.0472];   // 30, 60 degrees; the loop draws the 90 frame
	for (auto a : sweep) {
		float ang = rot * a;
		float ca = cos(ang);
		float sa = sin(ang);
		compose_frame(render_view(dx0 * ca - dy0 * sa, dx0 * sa + dy0 * ca));
		sleep_ms(26);
	}
}

// -------------------------------------------------------------- minimap ----
// Explored-tiles overhead view (M toggles). The genre staple: seen geometry,
// live monsters where you can see them, and your facing arrow.
var TILE_CELL_VIS = [
	col("37", "#"), col("90", "."), col("96", ">"), col("93", "&"), col("95", "%")
];
var TILE_CELL_SEEN = [
	col("90", "#"), col("90", " "), col("90", ">"), col("90", "&"), col("90", "%")
];
var CELL_UNSEEN = " ";
var FACE_ARROW = ["^", ">", "v", "<"];

function minimap_lines() -> var {
	var overlay = {};
	for (auto it : G.ground) {
		overlay[idx(it.x, it.y)] = [it.glyph, it.color];
	}
	for (auto m : G.monsters) {
		if (!m.alive) { continue; }
		if (m.disguised) { overlay[idx(m.x, m.y)] = ["?", "33"]; }
		else { overlay[idx(m.x, m.y)] = [m.glyph, m.color]; }
	}
	var player_cell = col("93", FACE_ARROW[G.facing]);
	var lines = [];
	for (int y = 0; y < MAP_H; ++y) {
		var row = "";
		for (int x = 0; x < MAP_W; ++x) {
			var i = idx(x, y);
			// hoisted: a BARE element read as an if condition is always truthy
			// (both backends, 2026-07 ref-lvalue regression — reported); a local
			// copy derefs correctly
			var tile_visible = G.visible[i];
			var tile_seen = G.seen[i];
			if (x == G.player.x && y == G.player.y) {
				row = row + player_cell;
			} else if (tile_visible) {
				var over = overlay[i];
				if (over != null) { row = row + col(over[1], over[0]); }
				else { row = row + TILE_CELL_VIS[G.tiles[i]]; }
			} else if (tile_seen) {
				row = row + TILE_CELL_SEEN[G.tiles[i]];
			} else {
				row = row + CELL_UNSEEN;
			}
		}
		lines.push(row);
	}
	return lines;
}

// ---------------------------------------------------------------- panel ----
function compass_letter(int face) -> string {
	if (G.facing == face) { return col("93", FACE_LETTER[face]); }
	return col("90", FACE_LETTER[face]);
}

function build_panel() -> var {
	var p = G.player;
	var lines = [];
	lines.push(" " + col("97", p.name));
	lines.push(" " + col("90", species_name(p.species_id)));
	lines.push(` HP ${p.hp}/${p.total_max_hp()}`);
	lines.push(" " + bar(p.hp, p.total_max_hp(), 12, p.hp * 4 < p.total_max_hp() ? "91" : "92"));
	if (G.last_dmg > 0) {
		lines.push(" " + col("91", `-${G.last_dmg} HP`) + col("90", ` (${G.last_dmg_src})`));
	} else {
		lines.push("");
	}
	lines.push(` Fo ${p.focus}/${p.total_max_focus()}`);
	lines.push(" " + bar(p.focus, p.total_max_focus(), 12, "96"));
	lines.push(` Lv ${p.level}  XP ${p.xp}/${p.xp_next()}`);
	lines.push(` Atk ${p.total_atk()}  Def ${p.total_def()}`);
	lines.push(` Eva ${p.total_eva()}  Crt ${p.total_crit()}`);
	lines.push(" " + col("33", `Gold ${p.gold}`));
	lines.push("");
	lines.push("       " + compass_letter(0));
	lines.push("     " + compass_letter(3) + " " + col("90", "+") + " " + compass_letter(1));
	lines.push("       " + compass_letter(2));
	lines.push(` Face ${col("96", FACE_NAME[G.facing])}`);
	lines.push(` Wpn ${gear_label(p.weapon)}`);
	lines.push(` Arm ${gear_label(p.armor)}`);
	lines.push(` Trk ${gear_label(p.trinket)}`);
	var fx_line = " ";
	for (auto fx : p.effects) { fx_line = fx_line + `${fx["kind"]}:${fx["turns"]} `; }
	lines.push(col("95", fx_line));
	return lines;
}

function gear_label(var item) -> string {
	if (item == null) { return col("90", "-"); }
	var short = item.name;
	if (short.size() > 12) { short = short.substr(0, 12); }
	return col(item.color, short);
}

function species_name(string id) -> string {
	for (auto s : SPECIES) { if (s["id"] == id) { return s["name"]; } }
	return id;
}

function class_name(string id) -> string {
	for (auto c : CLASSES) { if (c["id"] == id) { return c["name"]; } }
	return id;
}

function log_msg(string msg) -> void {
	G.msgs.push(msg);
	if (G.msgs.size() > 60) { G.msgs.remove(0); }
}

// ------------------------------------------------------------ modals -------
function help_screen() -> void {
	var lines = [];
	lines.push(col("93", " GRUBWELL - keys"));
	lines.push("");
	lines.push("  step forward  w / up      (into a monster = attack)");
	lines.push("  step back     s / down");
	lines.push("  turn          a / d  (or left / right arrows)");
	lines.push("  sidestep      q / e");
	lines.push("  wait          .");
	lines.push("  minimap       m   (explored tiles + facing arrow)");
	lines.push("  on the map    wasd / hjkl / arrows step by compass (no turning;");
	lines.push("                facing follows your step back into first person)");
	lines.push("  pick up       g");
	lines.push("  inventory     i   (letter to inspect, then e=use/equip d=drop)");
	lines.push("  quaff first   p   (first potion in pack)");
	lines.push("  equip best    E   (best upgrade in pack)");
	lines.push("  class skill   z   (fires down your facing)");
	lines.push("  descend       >   (on stairs)");
	lines.push("  character     c");
	lines.push("  save & quit   S");
	lines.push("  quit          Q");
	if (HOST_DEV) { lines.push(col("95", "  hot reload    R   (--dev: re-imports scripts mid-run)")); }
	lines.push("");
	lines.push("  The Well goes down ten floors and gets louder about it. JAI's");
	lines.push("  statues (&) bless your evasion; DEVACOR's murals (%) mend you.");
	lines.push("  Vexadrach squats at the bottom, hoarding the world's stolen color.");
	lines.push("");
	lines.push(col("90", "  any key to return"));
	present(lines);
	read_key();
}

function character_screen() -> void {
	var p = G.player;
	var species_row = null;
	for (auto s : SPECIES) { if (s["id"] == p.species_id) { species_row = s; } }
	var class_row = null;
	for (auto c : CLASSES) { if (c["id"] == p.class_id) { class_row = c; } }
	var lines = [];
	lines.push(col("93", ` ${p.name}`) + `  level ${p.level} ${species_row["name"]} ${class_row["name"]}`);
	lines.push("");
	lines.push(` ${species_row["blurb"]}`);
	lines.push(` Trait: ${species_row["trait_name"]} - ${species_row["trait_blurb"]}`);
	lines.push("");
	lines.push(` ${class_row["blurb"]}`);
	lines.push(` Skill: ${class_row["skill_name"]} (${class_row["skill_cost"]} focus) - ${class_row["skill_blurb"]}`);
	lines.push("");
	lines.push(` HP ${p.hp}/${p.total_max_hp()}   Focus ${p.focus}/${p.total_max_focus()}`);
	lines.push(` Atk ${p.total_atk()}  Def ${p.total_def()}  Eva ${p.total_eva()}  Crit ${p.total_crit()}`);
	lines.push(` Gold ${p.gold}   Kills ${p.kills}   Depth ${G.floor}   Facing ${FACE_NAME[G.facing]}`);
	lines.push("");
	lines.push(col("90", "  any key to return"));
	present(lines);
	read_key();
}

// returns true if a turn was consumed (using/equipping costs a turn)
function inventory_screen() -> bool {
	while (true) {
		var p = G.player;
		var lines = [];
		lines.push(col("93", " Pack") + col("90", `  (${p.inventory.size()}/22)  letter=inspect, esc=back`));
		lines.push("");
		var letters = "abcdefghijklmnopqrstuv";
		for (int i = 0; i < p.inventory.size(); ++i) {
			var it = p.inventory[i];
			lines.push(`  ${letters.at(i)}) ${col(it.color, item_label(it))}${item_stats_label(it)}`);
		}
		if (p.inventory.size() == 0) { lines.push(col("90", "  (lint, mostly)")); }
		present(lines);
		var key = read_key();
		if (key == "esc" || key == "i" || key == "Q") { return false; }
		var pick_index = letters.find(key);
		if (pick_index < 0 || pick_index >= p.inventory.size()) { continue; }
		var chosen = p.inventory[pick_index];
		var verb = chosen.kind == "potion" ? "quaff" : (chosen.kind == "scroll" ? "read" : "equip");
		var sub = [];
		sub.push(` ${col(chosen.color, item_label(chosen))}${item_stats_label(chosen)}`);
		sub.push(col("90", ` rarity: ${chosen.rarity}`));
		sub.push("");
		sub.push(`  e) ${verb}`);
		sub.push("  d) drop");
		sub.push(col("90", "  esc) back"));
		present(sub);
		var act = read_key();
		if (act == "e") { equip_item(G.player, pick_index); return true; }
		if (act == "d") { drop_item(G.player, pick_index); return true; }
	}
	return false;
}

function level_up_screen() -> void {
	var p = G.player;
	while (G.level_pending) {
		var lines = [];
		lines.push(col("93", ` LEVEL ${p.level}!`) + "  Pick a boon:");
		lines.push("");
		lines.push("  1) Vigor   +4 max HP (and heal 4)");
		lines.push("  2) Might   +1 attack");
		lines.push("  3) Guard   +1 defense");
		lines.push("  4) Wits    +3 max focus");
		present(lines);
		var key = G.autopilot ? to_string(1 + RNG.next(4)) : read_key();
		switch (key) {
			case "1": G.player.max_hp = G.player.max_hp + 4; heal_entity(G.player, 4); G.level_pending = false; break;
			case "2": G.player.atk = G.player.atk + 1; G.level_pending = false; break;
			case "3": G.player.def = G.player.def + 1; G.level_pending = false; break;
			case "4": G.player.max_focus = G.player.max_focus + 3; G.player.focus = G.player.focus + 3; G.level_pending = false; break;
			case "Q": G.over = true; G.death_cause = "quit"; return;
		}
	}
}

function death_screen() -> void {
	var p = G.player;
	var lines = [];
	lines.push("");
	lines.push(col("91", "   Y O U   H A V E   F A L L E N"));
	lines.push("");
	lines.push(`   Slain by ${G.death_cause} on floor ${G.floor} of the Well.`);
	lines.push(`   ${p.name}, level ${p.level} ${species_name(p.species_id)} ${class_name(p.class_id)}`);
	lines.push(`   ${p.kills} kills, ${p.gold} gold, ${G.turn} turns.`);
	lines.push("");
	lines.push("   " + col("90", pick(RNG, DEATH_QUIPS)));
	lines.push("");
	lines.push(col("90", "   any key to exit"));
	present(lines);
	if (!G.autopilot && !HOST_SCRIPTED) { read_key(); }
}

var RAINBOW_CODES = [196, 208, 226, 46, 51, 99, 201];

function rainbow_text(string text) -> string {
	var out = "";
	for (int i = 0; i < text.size(); ++i) {
		var c = text.at(i);
		if (c == ' ') { out = out + " "; continue; }
		out = out + col256(RAINBOW_CODES[i % RAINBOW_CODES.size()], to_string(c));
	}
	return out;
}

// The stolen fire comes home. The camera pulls up out of the Well for the
// epilogue: expanding rings of cycling color across the whole arena map.
coroutine string rainbow_frames(int bx, int by) {
	for (int frame = 0; frame < 26; ++frame) {
		var lines = [];
		lines.push("");
		for (int y = 0; y < MAP_H; ++y) {
			var row = "  ";
			for (int x = 0; x < MAP_W; ++x) {
				var d = cheb(x, y, bx, by);
				if (!is_walkable(x, y)) { row = row + " "; continue; }
				if (d <= frame) {
					var code = RAINBOW_CODES[(d + frame) % RAINBOW_CODES.size()];
					row = row + col256(code, "*");
				} else {
					row = row + " ";
				}
			}
			lines.push(row);
		}
		lines.push("");
		lines.push("  " + rainbow_text("THE RAINBOW FIRE REMEMBERS ITS MAKER"));
		var line_end = eol() + "\n";
		yield lines.join(line_end) + line_end;
	}
	return "";
}

function victory_ceremony(int bx, int by) -> void {
	for (auto frame : rainbow_frames(bx, by)) {
		if (frame == "") { continue; }
		draw(frame + ESC + "[J");
		sleep_ms(55);
	}
}

function victory_screen() -> void {
	var p = G.player;
	var lines = [];
	lines.push("");
	lines.push("   " + rainbow_text("V I C T O R Y"));
	lines.push("");
	lines.push("   Vexadrach unravels. The swallowed fire pours out of him in seven");
	lines.push("   colors and climbs the Well like sunrise practicing.");
	lines.push("");
	lines.push(`   ${p.name}, level ${p.level} ${species_name(p.species_id)} ${class_name(p.class_id)}`);
	lines.push(`   ${p.kills} kills, ${p.gold} gold, ${G.turn} turns, 10 floors.`);
	lines.push("");
	lines.push("   " + col("93", "Somewhere above, a stone jackodile grins approvingly."));
	lines.push("");
	lines.push(col("90", "   any key to exit"));
	present(lines);
	if (!G.autopilot && !HOST_SCRIPTED) { read_key(); }
}
