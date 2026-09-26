// demoreel entry points (Squirrel port of scenes/main.jai). include() is an
// adapter native (Squirrel has no module system); load order = the reference's.

::RESET <- ::ESC + "[0m";

::include("util.nut");
::include("reel.nut");
::include("plasma.nut");
::include("starfield.nut");
::include("tunnel.nut");
::include("donut.nut");
::include("julia.nut");
::include("lava.nut");
::include("pipes.nut");
::include("fire.nut");
::include("sand.nut");
::include("boids.nut");
::include("kinstein.nut");
::include("voxelspace.nut");
::include("marcher.nut");
::include("cloth.nut");
::include("chrome.nut");
::include("skull.nut");
::include("finale.nut");

::gfx <- ::Gfx();
::reel <- null;

function demo_boot(w, h, seed, backend, smoke, hud_on) {
	::reel = ::Reel(w, h, seed, backend, smoke, hud_on);
}

function demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) {
	return ::reel.frame(t, dt, key, fps, ms_sim, ms_draw, ms_other);
}

function demo_scene_index() { return ::reel.cur_idx; }
function demo_scene_time() { return ::reel.scene_t; }
function demo_jump(idx, at_t) { ::reel.jump(idx, at_t); }
function demo_reload() { ::reel.reload_current(); }
function demo_scene_count() { return ::reel.nscenes; }
function demo_scene_name() { return ::reel.cur.name; }
