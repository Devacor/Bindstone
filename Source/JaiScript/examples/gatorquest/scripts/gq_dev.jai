// Gator Quest - dev-mode hot reload. Reaching a module's import again re-runs
// it when its file changed since it last ran: functions and classes are
// replaced in place (live instances migrate), a declaration with an
// initializer takes its new value, one without keeps the live value. The
// module is checked before any of it runs, so a broken edit raises at the
// import and the running code stays as it was. An unchanged module costs a
// stat. The two state files reload like the rest: all they declare is state.

import "gq_save.jai";

bool DEV_ON;
int DEV_RELOADS;
int DEV_SCAN;                    // the next module whose stamp the HUD looks at
map<string, string> DEV_STAMP;   // module -> "modified:size" when last seen

// every module the running game can reload, in load order (this file and the entry file are not)
array<string> DEV_MODULES = ["gq_enums.jai", "gq_types.jai", "gq_state.jai", "gq_cstate.jai", "gq_util.jai", "gq_defs.jai",
	"gq_data.jai", "gq_world.jai", "gq_mobs.jai", "gq_items.jai", "gq_combat.jai", "gq_ai.jai", "gq_magic.jai", "gq_quests.jai",
	"gq_actions.jai", "gq_sim.jai", "gq_bot.jai", "gq_proto.jai", "gq_vid.jai", "gq_art.jai", "gq_render.jai", "gq_audio.jai",
	"gq_predict.jai", "gq_hud.jai", "gq_game.jai", "gq_net.jai", "gq_save.jai", "gq_title.jai", "gq_png.jai", "gq_tests.jai",
	"gq_serve.jai", "gq_endgame.jai", "gq_predtest.jai", "gq_menutest.jai"];

string dev_stamp(string module) {
	var fd = file_data(module);
	return fd == null ? "" : to_string(fd["modified"]) + ":" + to_string(fd["size"]);
}

void dev_start() {
	DEV_STAMP = {};
	DEV_SCAN = 0;
	for (int i = 0; i < DEV_MODULES.size(); ++i) { DEV_STAMP[DEV_MODULES[i]] = dev_stamp(DEV_MODULES[i]); }
}

// reach every import again (a module re-runs if its file changed), then look at `scan` stamps
// to name what reloaded: an import does not say whether it re-ran, and a stamp costs 4x an idle import
array<string> dev_poll(int scan) {
	array<string> msgs;
	for (int i = 0; i < DEV_MODULES.size(); ++i) {
		string module = DEV_MODULES[i];
		try {
			import(module);
		} catch (e) {
			array<string> el = to_string(e).lines();
			msgs.push(`Reload FAILED (${module}): ` + (el.size() > 0 ? el[0] + (el.size() > 1 ? " " + string::trim(el[1]) : "") : "?"));
			DEV_STAMP[module] = dev_stamp(module);
		}
	}
	for (int k = 0; k < scan && k < DEV_MODULES.size(); ++k) {
		string module = DEV_MODULES[DEV_SCAN];
		DEV_SCAN = (DEV_SCAN + 1) % DEV_MODULES.size();
		string stamp = dev_stamp(module);
		if (stamp == DEV_STAMP.get(module, "")) { continue; }
		DEV_STAMP[module] = stamp;
		DEV_RELOADS = DEV_RELOADS + 1;
		msgs.push("Reloaded " + module);
		dev_after_reload(module);
	}
	return msgs;
}

// derived client state that a reloaded file feeds
void dev_after_reload(string name) {
	if (name == "gq_art.jai" || name == "gq_vid.jai" || name == "gq_data.jai") {
		TS = 0;
		SHADE_WORLD = -1;
		if (VW > 0) { layout(SCREEN_COLS, SCREEN_ROWS); }
	}
}
