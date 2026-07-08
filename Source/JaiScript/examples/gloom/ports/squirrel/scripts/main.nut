// GLOOM entry point (Squirrel port). The adapter executes main.nut, then
// calls gloom_boot(w, h) once and gloom_frame(...) per frame; --smoke
// additionally calls gloom_state_hash() / gloom_summary().
// include() is an adapter native (Squirrel has no module system).

::include("state.nut");
::include("util.nut");
::include("defs.nut");
::include("maps.nut");
::include("pure.nut");
::include("particles.nut");
::include("enemies.nut");
::include("combat.nut");
::include("sim.nut");
::include("render.nut");
::include("hud.nut");
::include("game.nut");

::FORCE_PILOT <- false;

function gloom_force_autopilot() { ::FORCE_PILOT = true; }

function gloom_boot(w, h) {
	::RNG = ::Rng(::HOST_SEED);
	if (::HOST_PIX == 0) { ::PIXW = 1; ::PIXH = 2; }
	else if (::HOST_PIX == 2) { ::PIXW = 2; ::PIXH = 3; }
	else { ::PIXW = 2; ::PIXH = 2; }
	::parse_arts();
	::validate_all_maps();
	::G = ::Game(w, h);
	::init_render();
	::G.autopilot = ::HOST_SMOKE || ::FORCE_PILOT;
}

// dt accumulates into fixed TICKs (smoke feeds exactly TICK: one tick/frame).
// The edge key is consumed by the first tick of the frame.
function gloom_frame(dt, key, fps, ms_sim, ms_draw) {
	local g = ::G;
	g.frame_key = key;
	g.accum += dt;
	local steps = 0;
	while (g.accum >= ::TICK - 0.0000001 && steps < 3) {
		g.accum -= ::TICK;
		g.run_tick();
		g.frame_key = "";
		steps = steps + 1;
	}
	if (steps == 3) { g.accum = 0.0; }   // stall recovery: never spiral
	return ::render_frame(fps, ms_sim, ms_draw);
}

function gloom_state_hash() { return ::G.hash; }

function gloom_wants_quit() { return ::G.quit; }

function gloom_summary() {
	local g = ::G;
	::host_log("");
	::host_log("=== GLOOM RESULT ===");
	::host_log("backend: " + ::HOST_BACKEND + "  seed: " + ::HOST_SEED + "  workers: " + ::HOST_WORKERS + "  ticks: " + g.tick);
	::host_log("mode: " + g.mode + "  map: " + g.map_name + "  deaths: " + g.deaths);
	::host_log("hp: " + g.hp + "  armor: " + g.armor + "  ammo: " + g.ammo[0] + "/" + g.ammo[1] + "/" + g.ammo[2]);
	::host_log("map kills: " + g.kills + "/" + g.kill_total + "  secrets: " + g.secrets + "/" + g.secret_total);
	::host_log("episode kills: " + g.ep_kills + "/" + g.ep_kill_total + "  secrets: " + g.ep_secrets + "/" + g.ep_secret_total);
	::host_log("warden down: " + (g.warden_down ? "true" : "false"));
	::host_log("STATE_HASH: " + g.hash);
}
