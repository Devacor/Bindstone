// GRUBWELL entry point. The host executes: import "main.jai"; crawler_main();
// state.jai holds the persistent globals (G, RNG). reload_scripts() below deliberately
// skips it: reloading would re-run its top-level declarations and reset the live run.
// (Not a language mechanism - just don't re-execute files whose side effects you want kept.)

import "state.jai";
import "util.jai";
import "data.jai";
import "entities.jai";
import "dungeon.jai";
import "items.jai";
import "combat.jai";
import "ai.jai";
import "view.jai";
import "ui.jai";
import "game.jai";

// Re-execute every rule file mid-run ('R' with --dev). Classes hot-reload with
// instance migration; live coroutines keep their old bodies until rebuilt.
// G and RNG live in state.jai, which is deliberately skipped.
function reload_scripts() -> void {
	include "util.jai";
	include "data.jai";
	include "entities.jai";
	include "dungeon.jai";
	include "items.jai";
	include "combat.jai";
	include "ai.jai";
	include "view.jai";
	include "ui.jai";
	include "game.jai";
}

function creation_screen() -> var {
	// species
	var species_index = -1;
	while (species_index < 0) {
		var lines = [];
		lines.push(col("93", " Who goes down the Well?") + col("90", "  (choose your Kin)"));
		lines.push("");
		for (int i = 0; i < SPECIES.size(); ++i) {
			var s = SPECIES[i];
			lines.push(`  ${i + 1}) ${col("97", s["name"])}`);
			lines.push(col("90", `     ${s["blurb"]}`));
			lines.push(col("36", `     ${s["trait_name"]}: `) + col("90", s["trait_blurb"]));
		}
		present(lines);
		var key = read_key();
		if (key == "Q") { return null; }
		for (int i = 0; i < SPECIES.size(); ++i) {
			if (key == to_string(i + 1)) { species_index = i; }
		}
	}
	// class
	var class_index = -1;
	while (class_index < 0) {
		var lines = [];
		lines.push(col("93", " And what did you train as?"));
		lines.push("");
		for (int i = 0; i < CLASSES.size(); ++i) {
			var c = CLASSES[i];
			lines.push(`  ${i + 1}) ${col("97", c["name"])}`);
			lines.push(col("90", `     ${c["blurb"]}`));
			lines.push(col("36", `     ${c["skill_name"]} (${c["skill_cost"]} focus): `) + col("90", c["skill_blurb"]));
		}
		present(lines);
		var key = read_key();
		if (key == "Q") { return null; }
		for (int i = 0; i < CLASSES.size(); ++i) {
			if (key == to_string(i + 1)) { class_index = i; }
		}
	}
	// name
	var hero_name = "";
	while (true) {
		var lines = [];
		lines.push(col("93", " Your name, wanderer?") + col("90", "  (letters, enter to finish, esc for a nameless run)"));
		lines.push("");
		lines.push(`   > ${col("97", hero_name)}${col("90", "_")}`);
		present(lines);
		var key = read_key();
		if (key == "enter") { break; }
		if (key == "esc") { hero_name = ""; break; }
		if (key == "backspace") {
			if (hero_name.size() > 0) { hero_name = hero_name.substr(0, hero_name.size() - 1); }
			continue;
		}
		if (key.size() == 1 && hero_name.size() < 12) { hero_name = hero_name + key; }
	}
	if (hero_name == "") { hero_name = "Wanderer"; }
	return [SPECIES[species_index], CLASSES[class_index], hero_name];
}

function crawler_main() -> void {
	RNG = Rng(HOST_SEED);
	G = Game();
	G.smoke = HOST_SMOKE;
	G.autopilot = HOST_SMOKE;

	if (HOST_LOAD && file_exists(SAVE_PATH)) {
		if (!load_game()) {
			host_log("Save file unreadable; starting fresh.");
			G = Game();
			G.smoke = HOST_SMOKE;
			G.autopilot = HOST_SMOKE;
		}
	}

	if (G.player == null) {
		if (G.autopilot) {
			// deterministic pilot: species/class from the seed, same key path as a human after that
			var species_row = SPECIES[RNG.next(SPECIES.size())];
			var class_row = CLASSES[RNG.next(CLASSES.size())];
			start_new_game(species_row, class_row, "Testpilot");
		} else {
			var chosen = creation_screen();
			if (chosen == null) { return; }
			start_new_game(chosen[0], chosen[1], chosen[2]);
		}
	}

	game_loop();

	if (G.smoke) { print_run_summary("SMOKE RESULT"); }
	else if (HOST_SCRIPTED) { print_run_summary("SCRIPTED RESULT"); }
}
