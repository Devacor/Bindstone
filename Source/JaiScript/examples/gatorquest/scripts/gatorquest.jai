// Gator Quest entry point.
//
//   jaiscript gatorquest.jai                         play (single player)
//   jaiscript gatorquest.jai -- smoke [ticks N] [seed S] [bots K]
//                                                    headless autopilot session, prints STATE_HASH

import "gq_types.jai";
import "gq_state.jai";
import "gq_util.jai";
import "gq_defs.jai";
import "gq_data.jai";
import "gq_world.jai";
import "gq_mobs.jai";
import "gq_items.jai";
import "gq_combat.jai";
import "gq_ai.jai";
import "gq_magic.jai";
import "gq_quests.jai";
import "gq_actions.jai";
import "gq_sim.jai";
import "gq_bot.jai";

string arg_value(string key, string def) {
	for (int i = 0; i + 1 < ARGS.size(); ++i) {
		if (ARGS[i] == key) { return ARGS[i + 1]; }
	}
	return def;
}

bool arg_flag(string key) {
	for (int i = 0; i < ARGS.size(); ++i) {
		if (ARGS[i] == key) { return true; }
	}
	return false;
}

void print_char_summary(int slot) {
	Character& c = CHARS[slot];
	int m = c.mob;
	string where = mob_live(m) ? `${WORLDS[MOB_WORLD[m]].name} (${MOB_X[m]},${MOB_Y[m]})` : "nowhere";
	print(`  ${c.name} ${player_title(slot)} | hp ${MOB_HP[m]}/${MOB_HPMAX[m]} gold ${c.gold} fame ${c.fame} kills ${c.kills} deaths ${c.deaths} | ${where}`);
	string skills = "";
	for (int k = 0; k < c.skill.size(); ++k) {
		if (c.skill[k] > 0) { skills = skills + `${SKILL_KEY[k]} ${tenths(c.skill[k])}  `; }
	}
	print("  skills: " + skills);
	string pack = "";
	for (int k = 0; k < c.pack.size(); ++k) { pack = pack + item_name(c.pack[k]) + ", "; }
	print("  pack: " + pack);
	string worn = "";
	for (int k = 0; k < c.worn.size(); ++k) {
		if (c.worn[k].base >= 0) { worn = worn + SLOT_NAME[k] + "=" + item_name(c.worn[k]) + ", "; }
	}
	print("  worn: " + worn);
	array<string> j = journal_lines(slot);
	for (int k = 0; k < j.size(); ++k) { print("  journal: " + j[k]); }
}

int run_smoke() {
	int ticks = arg_value("ticks", "3000").to_int(3000);
	int seed = arg_value("seed", "1").to_int(1);
	int bots = arg_value("bots", "2").to_int(2);
	bool verbose = arg_flag("verbose");
	int t0 = clock_ms();
	sim_init(seed);
	int t1 = clock_ms();
	print(`world: seed ${seed}, ${TILES.size()} tiles in ${WORLD_COUNT} realms, ${MOB_LIVE} mobiles, ${SP_W.size()} spawners, ${GATE_W.size()} gates (gen ${t1 - t0} ms)`);
	BOTS = [];
	array<string> names = ["Chomps", "Snappy", "Gumbo", "Bayou Bill"];
	for (int b = 0; b < bots; ++b) {
		player_create(b, names[b % names.size()], b);
		bot_add(b);
	}
	int logs = 0;
	for (int t = 0; t < ticks; ++t) {
		bots_tick();
		sim_tick();
		for (int e = 0; e < EV_KIND.size(); ++e) {
			if (EV_KIND[e] == Ev.log && verbose && logs < 4000) {
				logs = logs + 1;
				print(`[${TICK}] ${EV_TO[e] >= 0 ? CHARS[EV_TO[e]].name : "near"}: ${EV_TEXT[e]}`);
			}
		}
	}
	int t2 = clock_ms();
	print(`ran ${ticks} ticks in ${t2 - t1} ms (${(t2 - t1) * 1000 / imax(1, ticks)} us/tick), ${MOB_LIVE} mobiles alive, kills ${STAT_KILLS}, spawns ${STAT_SPAWNS}`);
	for (int b = 0; b < bots; ++b) {
		print_char_summary(b);
		print("  plan: " + BOTS[b].log);
	}
	print(`STATE_HASH: ${state_hash()}`);
	return 0;
}

int main() {
	if (arg_flag("smoke")) { return run_smoke(); }
	print("Gator Quest: interactive play is not wired yet. Try -- smoke");
	return 0;
}
main();
