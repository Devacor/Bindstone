// demoreel entry points (Squirrel port of scenes/main.jai); include() is an adapter native.
//
// Scene file contract: a scene file transliterates its whole .jai (globals as ::NAME <- value, free
// functions, the class) and ends by registering its class: ::SCENES[idx] <- ::ClassName (JULIA 2,
// GLOBHUNTER 6, OSSUARY 9, MAELSTROM 10). The Reel composes a cue and probes length_bars()/hit_times()/
// hit_levels() only for registered indices; any other index raises "scene I is not ported".
// - The class extends ::Scene. Array/table members are declared null and built in constructor() (a class
//   body's [] is one array shared by every instance). pixel_budget is set in constructor(), replay in
//   build(), as the .jai does; setup() calls build() after w, h, seed, rng (::Rng) and pal (::Palette) are set.
// - Inside methods members resolve bare (w, pal, cue, rng); reach globals with :: (::gfx, ::HB_PIX, ::MUSIC_BAR,
//   ::sin): ::gfx is the Gfx, cue the scene's Cue (cue.kick_env(t), cue.kick.at, ...), rng the host's
//   xorshift (next, roll, chance, nextf), the text layer ::txt_put/::cell_col/..., the presenter tables ::HB_*.
// - render(grid) writes palette indices into the grid it is given (a reference: no var& needed).
// - Ints are int64: / truncates and % keeps the dividend's sign, as in C. Mirror the .jai's typed slots:
//   .tointeger() is math::itrunc, ::ifloor is math::ifloor, ::floor returns a float, .tofloat() converts.
// - sqstdmath's ::sin ::cos ::sqrt ::atan2 ::pow ::exp ::log ::floor are the C double functions, ::log2 is
//   a host native; sqstdmath's ::abs casts to a 32-bit int (use ::fabs for a float, a ternary for an int).
// - s[i] on a string is a signed char (& 255 for the byte); a float concatenated into a string prints %g.
// - parallel_transform(a, fn) is a.map(fn) with fn a one-parameter function.

::RESET <- ::ESC + "[0m";
// the cell shape: "half" blocks, "sext"ants, or "" for sextants while the grid stays within PIX_BUDGET pixels
::PIX_MODE <- "";
::PIX_BUDGET <- 48000;

::include("util.nut");
::include("music.nut");
::include("reel.nut");
::include("julia.nut");
::include("globhunter.nut");
::include("skull.nut");
::include("fluid.nut");

::gfx <- ::Gfx();
::reel <- null;

function demo_boot(w, h, seed, backend, smoke, hud_on) {
	::reel = ::Reel(w, h, seed, backend, smoke, hud_on);
}

function demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) {
	return ::reel.frame(t, dt, key, fps, ms_sim, ms_draw, ms_other);
}

function demo_pixels(mode) { ::PIX_MODE = mode == "auto" ? "" : mode; }
function demo_scene_index() { return ::reel.cur_idx; }
function demo_scene_time() { return ::reel.scene_t; }
function demo_jump(idx, at_t) { ::reel.jump(idx, at_t); }
function demo_reload(changed, failed) {}
function demo_scene_count() { return ::reel.nscenes; }
function demo_scene_name() { return ::reel.cur.name; }
