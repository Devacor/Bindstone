// GLOOM entry point (AngelScript port). The adapter builds main.as with its
// #includes into one module, then calls gloom_boot(w, h) once and
// gloom_frame(...) per frame; --smoke additionally calls gloom_state_hash() /
// gloom_summary().

#include "state.as"
#include "util.as"
#include "defs.as"
#include "maps.as"
#include "pure.as"
#include "particles.as"
#include "enemies.as"
#include "combat.as"
#include "sim.as"
#include "render.as"
#include "hud.as"
#include "game.as"

bool FORCE_PILOT = false;

void gloom_force_autopilot() { FORCE_PILOT = true; }

void gloom_boot(int w, int h) {
	RNG = Rng(HOST_SEED);
	if (HOST_PIX == 0) {
		PIXW = 1;
		PIXH = 2;
	} else if (HOST_PIX == 2) {
		PIXW = 2;
		PIXH = 3;
	} else {
		PIXW = 2;
		PIXH = 2;
	}
	parse_arts();
	validate_all_maps();
	@G = Game(w, h);
	init_render();
	G.autopilot = HOST_SMOKE || FORCE_PILOT;
}

// dt accumulates into fixed TICKs (smoke feeds exactly TICK: one tick/frame).
// The edge key is consumed by the first tick of the frame.
string gloom_frame(double dt, const string &in key, double fps, double ms_sim, double ms_draw) {
	G.frame_key = key;
	G.accum += dt;
	int steps = 0;
	while (G.accum >= TICK - (1.0 / 10000000.0) && steps < 3) {
		G.accum -= TICK;
		G.run_tick();
		G.frame_key = "";
		steps = steps + 1;
	}
	if (steps == 3) { G.accum = 0.0; }   // stall recovery: never spiral
	return render_frame(fps, ms_sim, ms_draw);
}

int64 gloom_state_hash() { return G.hash; }

bool gloom_wants_quit() { return G.quit; }

void gloom_summary() {
	host_log("");
	host_log("=== GLOOM RESULT ===");
	host_log("backend: " + HOST_BACKEND + "  seed: " + HOST_SEED + "  workers: " + HOST_WORKERS + "  ticks: " + G.tick);
	host_log("mode: " + G.mode + "  map: " + G.map_name + "  deaths: " + G.deaths);
	host_log("hp: " + G.hp + "  armor: " + G.armor + "  ammo: " + G.ammo[0] + "/" + G.ammo[1] + "/" + G.ammo[2]);
	host_log("map kills: " + G.kills + "/" + G.kill_total + "  secrets: " + G.secrets + "/" + G.secret_total);
	host_log("episode kills: " + G.ep_kills + "/" + G.ep_kill_total + "  secrets: " + G.ep_secrets + "/" + G.ep_secret_total);
	host_log("warden down: " + G.warden_down);
	host_log("STATE_HASH: " + G.hash);
}
