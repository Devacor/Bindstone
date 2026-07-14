// GLOOM drag-drop shim for the standalone runner — no C++ host needed:
//
//     jaiscript.exe gloom.jai                        play (esc quits)
//     jaiscript.exe gloom.jai -- --smoke --ticks 300 headless parity self-test
//     jaiscript.exe gloom.jai -- --bench 300         autopilot + live render split
//
// The shared C++ host (gloom_host.cpp) owned the console, input, clock, Rng,
// and flags; the runner already provides the console half (key_down/read_key/
// clock_ms/sleep_ms/term_cols/term_rows/print). This file supplies the rest of
// the REFERENCE.md section 2 contract in pure script — Rng (bit-exact
// xorshift64* under checked overflow), host_log/itrunc/ifloor/utf8/ESC, the
// HOST_* globals from ARGS — then loads scripts/ and runs the host pump.
// key_down/read_key are NOT redefined: the runner's console-event versions
// pass through (a script definition would shadow them; verified).
//
// scripts/main.jai's sibling imports ("state.jai") resolve against CWD +
// include paths only — from this directory they cannot be reached through
// "scripts/main.jai", so its import list + entry glue are replicated below
// verbatim. The game files themselves are untouched.

// ------------------------------------------------------- host API: globals --
var ESC = "\x1b";
int HOST_SEED = 666;
bool HOST_SMOKE = false;
int HOST_TICKS = 2000;
int HOST_WORKERS = -1;      // <= 0: the scripts run their serial loops
bool HOST_GOD = false;
string HOST_BACKEND = "vm";  // label only; the real backend is the runner's --backend (before --)
int HOST_PIX = 1;           // 0 half (1x2) | 1 quad (2x2) | 2 sext (2x3)

// ---------------------------------------------------------- host API: Rng ---
// xorshift64* per gloom_host.hpp, the ONLY randomness (STATE_HASH folds
// state() every tick). s ^= s>>12; s ^= s<<25; s ^= s>>27; return s * M —
// all uint64. Script ints are checked int64: >> is arithmetic (mask after),
// << wraps (verified), and the M-multiply wraps via 16-bit limb products.
// Proven bit-exact against an independent wrap-arithmetic reference for
// seed 666 (state + first 6 raws) and by the smoke checkpoint table below.
class Rng {
	int s = 0;

	Rng(int seed) {
		s = seed != 0 ? seed : ((0x9E3779B9 << 32) | 0x7F4A7C15);
		for (int i = 0; i < 4; ++i) { step(); }
	}

	// one raw step: returns the uint64 bit pattern in an int64
	int step() {
		s = s ^ ((s >> 12) & 0x000FFFFFFFFFFFFF);
		s = s ^ (s << 25);
		s = s ^ ((s >> 27) & 0x0000001FFFFFFFFF);
		// s * 0x2545F4914F6CDD1D mod 2^64: limbs of the constant are
		// 0x2545 0xF491 0x4F6C 0xDD1D high->low
		int a0 = s & 0xFFFF;
		int a1 = (s >> 16) & 0xFFFF;
		int a2 = (s >> 32) & 0xFFFF;
		int a3 = (s >> 48) & 0xFFFF;
		int c0 = a0 * 0xDD1D;
		int c1 = a1 * 0xDD1D + a0 * 0x4F6C;
		int c2 = a2 * 0xDD1D + a1 * 0x4F6C + a0 * 0xF491;
		int c3 = a3 * 0xDD1D + a2 * 0x4F6C + a1 * 0xF491 + a0 * 0x2545;
		int r0 = c0 & 0xFFFF;
		c1 = c1 + (c0 >> 16);
		int r1 = c1 & 0xFFFF;
		c2 = c2 + (c1 >> 16);
		int r2 = c2 & 0xFFFF;
		c3 = c3 + (c2 >> 16);
		return r0 | (r1 << 16) | (r2 << 32) | ((c3 & 0xFFFF) << 48);
	}

	// unsigned mod: fold 2^63 back in when the bit pattern reads negative
	int next(int n) {
		if (n <= 0) { return 0; }
		int r = step();
		if (r >= 0) { return r % n; }
		int h = 0x4000000000000000 % n;
		int m = (h + h) % n;
		return ((r & 0x7FFFFFFFFFFFFFFF) % n + m) % n;
	}

	int roll(int lo, int hi) {
		if (hi <= lo) { return lo; }
		return lo + next(hi - lo + 1);
	}

	bool chance(int p) { return next(100) < p; }

	// (step() >> 11 logical) * 2^-53 — 53 bits, exact double
	float nextf() {
		return ((step() >> 11) & 0x001FFFFFFFFFFFFF) * (1.0 / 9007199254740992.0);
	}

	int state() { return s & 0x3FFFFFFFFFFFFFFF; }
}

// ------------------------------------------------------ host API: functions --
function host_log(string line) -> void { print(line); }
function itrunc(float v) -> int { return math::itrunc(v); }
function ifloor(float v) -> int { return math::ifloor(v); }

function utf8(int cp) -> string {
	if (cp < 0x80) { return "" + to_char(cp); }
	if (cp < 0x800) { return "" + to_char(0xC0 | (cp >> 6)) + to_char(0x80 | (cp & 0x3F)); }
	if (cp < 0x10000) { return "" + to_char(0xE0 | (cp >> 12)) + to_char(0x80 | ((cp >> 6) & 0x3F)) + to_char(0x80 | (cp & 0x3F)); }
	return "" + to_char(0xF0 | (cp >> 18)) + to_char(0x80 | ((cp >> 12) & 0x3F)) + to_char(0x80 | ((cp >> 6) & 0x3F)) + to_char(0x80 | (cp & 0x3F));
}

// ------------------------------------------------------------------ the game --
// scripts/main.jai's import list, in its exact order
import "scripts/state.jai";
import "scripts/util.jai";
import "scripts/defs.jai";
import "scripts/maps.jai";
import "scripts/pure.jai";
import "scripts/particles.jai";
import "scripts/enemies.jai";
import "scripts/combat.jai";
import "scripts/sim.jai";
import "scripts/render.jai";
import "scripts/hud.jai";
import "scripts/game.jai";

// scripts/main.jai's entry glue, replicated verbatim
var FORCE_PILOT = false;

function gloom_force_autopilot() -> void { FORCE_PILOT = true; }

function gloom_boot(int w, int h) -> void {
	RNG = Rng(HOST_SEED);
	if (HOST_PIX == 0) { PIXW = 1; PIXH = 2; }
	else if (HOST_PIX == 2) { PIXW = 2; PIXH = 3; }
	else { PIXW = 2; PIXH = 2; }
	parse_arts();
	validate_all_maps();
	G = new Game(w, h);
	init_render();
	G.autopilot = HOST_SMOKE || FORCE_PILOT;
}

// dt accumulates into fixed TICKs (smoke feeds exactly TICK: one tick/frame).
// The edge key is consumed by the first tick of the frame.
function gloom_frame(float dt, string key, float fps, float ms_sim, float ms_draw) -> string {
	G.frame_key = key;
	G.accum += dt;
	int steps = 0;
	while (G.accum >= TICK - 0.0000001 && steps < 3) {
		G.accum -= TICK;
		G.run_tick();
		G.frame_key = "";
		steps = steps + 1;
	}
	if (steps == 3) { G.accum = 0.0; }   // stall recovery: never spiral
	return render_frame(fps, ms_sim, ms_draw);
}

function gloom_state_hash() -> int { return G.hash; }

function gloom_wants_quit() -> bool { return G.quit; }

function gloom_summary() -> void {
	host_log("");
	host_log("=== GLOOM RESULT ===");
	host_log(`backend: ${HOST_BACKEND}  seed: ${HOST_SEED}  workers: ${HOST_WORKERS}  ticks: ${G.tick}`);
	host_log(`mode: ${G.mode}  map: ${G.map_name}  deaths: ${G.deaths}`);
	host_log(`hp: ${G.hp}  armor: ${G.armor}  ammo: ${G.ammo[0]}/${G.ammo[1]}/${G.ammo[2]}`);
	host_log(`map kills: ${G.kills}/${G.kill_total}  secrets: ${G.secrets}/${G.secret_total}`);
	host_log(`episode kills: ${G.ep_kills}/${G.ep_kill_total}  secrets: ${G.ep_secrets}/${G.ep_secret_total}`);
	host_log(`warden down: ${G.warden_down}`);
	host_log(`STATE_HASH: ${G.hash}`);
}

// ------------------------------------------------------------- the host pump --
bool SHIM_SMOKE = false;
int SHIM_BENCH = 0;
int SHIM_W = 0;
int SHIM_H = 0;
float SHIM_FPS = 30.0;
int SHIM_DUMP = -1;

// REFERENCE.md section 3.5 checkpoints, keyed seed/god/ticks (pix-independent)
var SMOKE_REF = {
	"666/0/300": 3580805725,
	"666/0/2000": 319812559,
	"666/0/3000": 4080154357,
	"666/0/16000": 3497451110,
	"7/0/3000": 1696980843,
	"99/0/3000": 1855347375,
	"4242/0/3000": 2848371116,
	"5/1/12000": 576425398
};

function shim_usage() -> void {
	print("gloom.jai - GLOOM on the standalone jaiscript runner (flags go after --)");
	print("  --seed N        run seed (default 666)");
	print("  --smoke         headless deterministic autoplay + checkpoint parity check");
	print("  --ticks N       smoke tick budget (default 2000); frames N / turns N = smoke too");
	print("  --bench N       N frames autopilot with live rendering, sim/draw split");
	print("  --fps N         frame pacing target (default 30)");
	print("  --w N / --h N   force console dimensions");
	print("  --god           god mode");
	print("  --pix MODE      half (1x2) | quad (2x2, default) | sext (2x3)");
	print("  --workers N     >0 enables the parallel_transform paths (default serial)");
	print("  --dump-frame N  smoke: dump frame N to gloom_frame_shim.txt");
	print("keys: w/s move  a/d strafe  left/right turn  space/ctrl fire  e use");
	print("      shift run  1/2/3 weapons  m automap  esc quit");
}

function shim_parse_args() -> int {
	int i = 0;
	while (i < ARGS.size()) {
		var a = ARGS[i];
		bool needs_value = a == "--seed" || a == "--ticks" || a == "ticks" || a == "--frames" || a == "frames"
			|| a == "--turns" || a == "turns" || a == "--bench" || a == "--fps" || a == "--w" || a == "--h"
			|| a == "--pix" || a == "--workers" || a == "--dump-frame" || a == "--backend";
		if (needs_value && i + 1 >= ARGS.size()) {
			print("missing value for " + a);
			return 2;
		}
		if (a == "--smoke" || a == "smoke") { SHIM_SMOKE = true; }
		else if (a == "--god" || a == "god") { HOST_GOD = true; }
		else if (a == "--help" || a == "-h") { shim_usage(); return 0; }
		else if (a == "--seed") { i = i + 1; HOST_SEED = ARGS[i].to_int(HOST_SEED); }
		else if (a == "--ticks" || a == "ticks") { i = i + 1; HOST_TICKS = ARGS[i].to_int(HOST_TICKS); }
		else if (a == "--frames" || a == "frames" || a == "--turns" || a == "turns") {
			i = i + 1;
			HOST_TICKS = ARGS[i].to_int(HOST_TICKS);
			SHIM_SMOKE = true;   // frames/turns N = the headless self-test
		}
		else if (a == "--bench") { i = i + 1; SHIM_BENCH = ARGS[i].to_int(0); }
		else if (a == "--fps") { i = i + 1; SHIM_FPS = 1.0 * ARGS[i].to_int(30); }
		else if (a == "--w") { i = i + 1; SHIM_W = ARGS[i].to_int(0); }
		else if (a == "--h") { i = i + 1; SHIM_H = ARGS[i].to_int(0); }
		else if (a == "--workers") { i = i + 1; HOST_WORKERS = ARGS[i].to_int(-1); }
		else if (a == "--dump-frame") { i = i + 1; SHIM_DUMP = ARGS[i].to_int(-1); }
		else if (a == "--backend") { i = i + 1; HOST_BACKEND = ARGS[i]; }
		else if (a == "--pix") {
			i = i + 1;
			var p = ARGS[i];
			if (p == "half") { HOST_PIX = 0; }
			else if (p == "quad") { HOST_PIX = 1; }
			else if (p == "sext") { HOST_PIX = 2; }
			else { print("unknown --pix mode: " + p + " (half|quad|sext)"); return 2; }
		}
		else {
			print("unknown flag: " + a);
			shim_usage();
			return 2;
		}
		i = i + 1;
	}
	HOST_SMOKE = SHIM_SMOKE;
	return -1;   // keep going
}

// host run_smoke: fixed 100x40, dt = exactly TICK, autopilot, no frame blits
function shim_run_smoke() -> int {
	gloom_force_autopilot();
	gloom_boot(100, 40);
	float dt = 1.0 / 30.0;
	string pix_name = HOST_PIX == 0 ? "half" : (HOST_PIX == 2 ? "sext" : "quad");
	print(`gloom.jai --smoke: ${HOST_TICKS} ticks @ 100x40, seed ${HOST_SEED}, pix ${pix_name}, workers ${HOST_WORKERS}`);
	int t0 = clock_ms();
	for (int i = 0; i < HOST_TICKS; ++i) {
		var text = gloom_frame(dt, "", 0.0, 0.0, 0.0);
		if (i == SHIM_DUMP) { write_file("gloom_frame_shim.txt", text); }
	}
	int total = clock_ms() - t0;
	float per_tick = 1.0 * total / HOST_TICKS;
	int hash = gloom_state_hash();
	print(`total ${total} ms | ${per_tick:.3f} ms/tick | STATE_HASH: ${hash}`);
	gloom_summary();
	string ref_key = `${HOST_SEED}/${HOST_GOD ? 1 : 0}/${HOST_TICKS}`;
	if (SMOKE_REF.has(ref_key)) {
		int expected = SMOKE_REF[ref_key];
		if (hash == expected) {
			print("checkpoint parity: OK (matches the shared-host reference " + to_string(expected) + ")");
			return 0;
		}
		print(`checkpoint parity: MISMATCH (expected ${expected}, got ${hash})`);
		return 1;
	}
	print("no reference checkpoint for seed " + to_string(HOST_SEED) + " @ " + to_string(HOST_TICKS) + " ticks (hash informational)");
	return 0;
}

// the interactive/bench loop the C++ host ran (bench_frames 0 = play forever)
function shim_run_live(int bench_frames) -> int {
	// host console.size(): one col/row spare (no wrap/scroll), clamp 70-120 x 30-46
	int w = term_cols() - 1;
	int h = term_rows() - 1;
	if (w > 120) { w = 120; }
	if (h > 46) { h = 46; }
	if (w < 70) { w = 70; }
	if (h < 30) { h = 30; }
	if (SHIM_W > 0) { w = SHIM_W; }
	if (SHIM_H > 0) { h = SHIM_H; }
	bool bench = bench_frames > 0;
	if (bench) { gloom_force_autopilot(); }
	gloom_boot(w, h);
	print(ESC + "[?1049h" + ESC + "[?25l" + ESC + "[2J" + ESC + "[H", true);   // alt screen, hide cursor
	int t_prev = clock_ms();
	float fps_ema = SHIM_FPS;
	float last_sim = 0.0;
	float last_draw = 0.0;
	float sim_sum = 0.0;
	float draw_sum = 0.0;
	int done = 0;
	while (true) {
		int frame_start = clock_ms();
		float dt = (frame_start - t_prev) / 1000.0;
		t_prev = frame_start;
		if (dt > 0.1) { dt = 0.1; }
		if (dt > 0.0001) { fps_ema = fps_ema * 0.92 + (1.0 / dt) * 0.08; }

		var key = bench ? "" : read_key();
		if (key == "esc") { break; }
		if (key == "space") { key = " "; }   // gloom_host's poll_key delivers space as " "

		int sim_t0 = clock_ms();
		var text = gloom_frame(dt, key, fps_ema, last_sim, last_draw);
		last_sim = 1.0 * (clock_ms() - sim_t0);
		int draw_t0 = clock_ms();
		print(ESC + "[H" + text + ESC + "[0m", true);
		last_draw = 1.0 * (clock_ms() - draw_t0);

		if (bench) {
			sim_sum += last_sim;
			draw_sum += last_draw;
			done = done + 1;
			if (done >= bench_frames) { break; }
		}
		if (gloom_wants_quit()) { break; }

		int budget = itrunc(1000.0 / SHIM_FPS);
		int used = clock_ms() - frame_start;
		if (used < budget) { sleep_ms(budget - used); }
	}
	print(ESC + "[0m" + ESC + "[?1049l" + ESC + "[?25h", true);   // restore screen + cursor
	if (bench && done > 0) {
		float fr = 1.0 * done;
		print(`bench: ${done} frames @ ${w}x${h}`);
		print(`  sim   ${sim_sum / fr:.2f} ms/frame  (script tick + frame string)`);
		print(`  draw  ${draw_sum / fr:.2f} ms/frame  (console write)`);
		print(`  total ${(sim_sum + draw_sum) / fr:.2f} ms/frame`);
	}
	gloom_summary();
	return 0;
}

function shim_main() -> int {
	int parsed = shim_parse_args();
	if (parsed >= 0) { return parsed; }
	if (SHIM_SMOKE) { return shim_run_smoke(); }
	return shim_run_live(SHIM_BENCH);
}

shim_main()
