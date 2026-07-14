// demoreel.jai — drag-drop shim: runs the demoreel on the standalone jaiscript
// runner with zero C++ beyond the runner itself. Recreates the C++ host contract
// from main.cpp in pure script — the ESC global, itrunc/ifloor casts, host_log,
// and the seeded xorshift64* Rng class (bit-exact, checked-int64-safe: 16-bit-limb
// mul, mask-after-shift logical shifts) — then imports the scene files in the
// host's load order and pumps demo_frame at 30 fps.
//
// Deliberately NOT ported from main.cpp: the VT escape filter, xterm-256 rewrite,
// and row-diff redraw (frames are presented whole; the scripts' own run-length
// escape elision still applies), the TAB dual-engine backend swap (one engine
// here — TAB is swallowed), and 'r' hot reload (no script-side eval; swallowed).
//
//   jaiscript demoreel.jai                        the reel (q or esc quits)
//   jaiscript demoreel.jai -- frames 60           headless self-test: N smoke
//                                                 frames (100x40, seed 20260705,
//                                                 fixed dt 1/30), prints sizes,
//                                                 a stream hash, and timing
//   jaiscript demoreel.jai -- frames 60 dump P    also write frames 0/N-2/N-1
//                                                 to P_<i>.txt for byte diffing
//   other args: seed N | fps N | w N | h N | scene I (jump after boot)
//   keys: h HUD | f freeze | n/p/space/arrows scene skip | 1-9,0 jump | q/esc quit

// ---------------------------------------------------------------- host API ---

var ESC = "" + to_char(27);
var RUNNER_BACKEND = "vm";   // HUD label only; the reel never branches on it

function itrunc(float v) -> int { return math::itrunc(v); }
function ifloor(float v) -> int { return math::ifloor(v); }
function host_log(string line) -> void { print(line); }

// xorshift64* — bit-exact port of main.cpp's demo_rng under checked int64:
// shifts wrap mod 2^64 (masked after >> for the logical form), the 64x64 low
// multiply runs in 16-bit limbs so no intermediate exceeds int64.
function u64_mul(int a, int b) -> int {
	int a0 = a & 0xFFFF; int a1 = (a >> 16) & 0xFFFF; int a2 = (a >> 32) & 0xFFFF; int a3 = (a >> 48) & 0xFFFF;
	int b0 = b & 0xFFFF; int b1 = (b >> 16) & 0xFFFF; int b2 = (b >> 32) & 0xFFFF; int b3 = (b >> 48) & 0xFFFF;
	int r0 = a0 * b0;
	int r1 = a0 * b1 + a1 * b0 + (r0 >> 16);
	int r2 = a0 * b2 + a1 * b1 + a2 * b0 + (r1 >> 16);
	int r3 = a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0 + (r2 >> 16);
	return (r0 & 0xFFFF) | ((r1 & 0xFFFF) << 16) | ((r2 & 0xFFFF) << 32) | ((r3 & 0xFFFF) << 48);
}

function u64_shr(int x, int k) -> int {
	return (x >> k) & ((1 << (64 - k)) - 1);
}

class Rng {
	int s = 0;
	Rng(int seed) {
		s = seed != 0 ? seed : -7046029254386353131;   // 0x9E3779B97F4A7C15
		for (int i = 0; i < 4; ++i) { raw(); }
	}
	int raw() {
		int v = s;
		v = v ^ u64_shr(v, 12);
		v = v ^ (v << 25);
		v = v ^ u64_shr(v, 27);
		s = v;
		return u64_mul(v, 2685821657736338717);        // 0x2545F4914F6CDD1D
	}
	int next(int n) {
		if (n <= 0) { return 0; }
		int x = raw();
		if (x >= 0) { return x % n; }
		int m = ((((4611686018427387904 % n) * 2) % n) * 2) % n;   // 2^64 mod n
		int res = (x % n + m) % n;
		return res < 0 ? res + n : res;
	}
	int roll(int lo, int hi) {
		if (hi <= lo) { return lo; }
		return lo + next(hi - lo + 1);
	}
	bool chance(int percent) { return next(100) < percent; }
	float nextf() {
		return u64_shr(raw(), 11) * (1.0 / 9007199254740992.0);
	}
}

// ------------------------------------------------------------------ scenes ---
// main.cpp's kSceneFiles order: base classes before derived, main.jai last
// (it reads ESC at import time, so the host API above must already exist).

import "scenes/util.jai";
import "scenes/reel.jai";
import "scenes/plasma.jai";
import "scenes/starfield.jai";
import "scenes/donut.jai";
import "scenes/julia.jai";
import "scenes/pipes.jai";
import "scenes/fire.jai";
import "scenes/sand.jai";
import "scenes/boids.jai";
import "scenes/kinstein.jai";
import "scenes/voxelspace.jai";
import "scenes/marcher.jai";
import "scenes/cloth.jai";
import "scenes/finale.jai";
import "scenes/main.jai";

// -------------------------------------------------------------------- args ---

var OPT_FRAMES = 0;          // 0 = interactive
var OPT_SEED = 20260705;
var OPT_FPS = 30.0;
var OPT_W = 0;               // 0 = autodetect
var OPT_H = 0;
var OPT_SCENE = -1;
var OPT_DUMP = "";

for (int i = 0; i + 1 < ARGS.size(); i += 2) {
	string k = ARGS[i];
	string v = ARGS[i + 1];
	if (k == "frames" || k == "turns") { OPT_FRAMES = v.to_int(); }
	else if (k == "seed") { OPT_SEED = v.to_int(); }
	else if (k == "fps") { OPT_FPS = v.to_int() * 1.0; }
	else if (k == "w") { OPT_W = v.to_int(); }
	else if (k == "h") { OPT_H = v.to_int(); }
	else if (k == "scene") { OPT_SCENE = v.to_int(); }
	else if (k == "dump") { OPT_DUMP = v; }
	else { print("demoreel.jai: unknown arg '" + k + "' (frames/seed/fps/w/h/scene/dump)"); }
}

// ---------------------------------------------------------------- headless ---
// Mirrors main.cpp run_smoke: 100x40, fixed dt 1/30, smoke=true, hud off,
// key "" and zeroed timing args every frame. The rolling hash makes runs
// comparable across runner backends (vm vs interp) without keeping frames.

function run_headless(int frames) -> int {
	int w = OPT_W > 0 ? OPT_W : 100;
	int h = OPT_H > 0 ? OPT_H : 40;
	float dt = 1.0 / 30.0;
	demo_boot(w, h, OPT_SEED, "vm", true, false);
	if (OPT_SCENE >= 0) { demo_jump(OPT_SCENE, 5.0); }
	print(format("demoreel shim headless: {0} frames @ {1}x{2}, seed {3}, dt {4}", frames, w, h, OPT_SEED, dt));
	int hash = 0;
	int total_bytes = 0;
	int min_len = 0; int max_len = 0;
	int t0 = clock_ms();
	for (int f = 0; f < frames; ++f) {
		string frame = demo_frame(f * dt, dt, "", 0.0, 0.0, 0.0, 0.0);
		int len = frame.size();
		total_bytes += len;
		if (f == 0 || len < min_len) { min_len = len; }
		if (len > max_len) { max_len = len; }
		for (int i = 0; i < len; ++i) { hash = (hash * 31 + frame[i]) & 0xFFFFFFFF; }
		if (OPT_DUMP != "" && (f == 0 || f == frames - 2 || f == frames - 1)) {
			write_file(OPT_DUMP + "_" + to_string(f) + ".txt", frame);
		}
	}
	int elapsed = clock_ms() - t0;
	print(format("frames {0} | bytes {1} (len {2}..{3}) | stream hash {4} | {5} ms total, {6} ms/frame incl. hashing",
		frames, total_bytes, min_len, max_len, hash, elapsed, (elapsed * 1.0) / frames));
	return 0;
}

// -------------------------------------------------------------------- pump ---
// main.cpp's interactive loop: clock dt (clamped 0.1s) + fps EMA, poll one key
// edge, run demo_frame, present with a home escape, sleep off the fps budget.

function run_interactive() -> int {
	int w = OPT_W > 0 ? OPT_W : term_cols() - 1;   // one column spare: no wrap
	int h = OPT_H > 0 ? OPT_H : term_rows() - 1;   // one row spare: no scroll
	w = w > 110 ? 110 : (w < 60 ? 60 : w);
	h = h > 44 ? 44 : (h < 24 ? 24 : h);
	demo_boot(w, h, OPT_SEED, RUNNER_BACKEND, false, true);
	if (OPT_SCENE >= 0) { demo_jump(OPT_SCENE, 5.0); }

	print(ESC + "[?1049h" + ESC + "[?25l" + ESC + "[2J" + ESC + "[H", skip_newline);
	int start_ms = clock_ms();
	int prev_ms = start_ms;
	float fps_ema = OPT_FPS;
	float sim_ms = 0.0;
	float draw_ms = 0.0;
	int budget_ms = itrunc(1000.0 / OPT_FPS);
	while (true) {
		int frame_start = clock_ms();
		float dt = (frame_start - prev_ms) * 0.001;
		prev_ms = frame_start;
		if (dt > 0.1) { dt = 0.1; }
		if (dt > 0.0001) { fps_ema = fps_ema * 0.92 + (1.0 / dt) * 0.08; }

		string key = read_key();
		if (key == "q" || key == "esc") { break; }
		if (key == "tab" || key == "r") { key = ""; }   // single engine, no eval: unsupported here

		float t = (frame_start - start_ms) * 0.001;
		string frame = demo_frame(t, dt, key, fps_ema, sim_ms, draw_ms, 0.0);
		int sim_end = clock_ms();
		sim_ms = (sim_end - frame_start) * 1.0;
		print(ESC + "[H" + frame + ESC + "[0m", skip_newline);
		draw_ms = (clock_ms() - sim_end) * 1.0;

		int used = clock_ms() - frame_start;
		if (used < budget_ms) { sleep_ms(budget_ms - used); }
	}
	print(ESC + "[0m" + ESC + "[?1049l" + ESC + "[?25h", skip_newline);
	return 0;
}

if (OPT_FRAMES > 0) { run_headless(OPT_FRAMES); } else { run_interactive(); }
0
