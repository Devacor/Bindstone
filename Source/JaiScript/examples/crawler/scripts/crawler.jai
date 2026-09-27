// GRUBWELL on the standalone runner - a pure-JaiScript stand-in for the C++ host (../main.cpp).
// Drag THIS file onto jaiscript.exe (it must live in scripts/ so main.jai's sibling imports
// resolve - the runner puts the entry script's folder on the include path; the save file
// goes beside scripts/, so the working directory the shell picked does not matter).
//
//   jaiscript.exe crawler.jai                                interactive run, seed 1337
//   jaiscript.exe crawler.jai -- seed 7 god dev load
//   jaiscript.exe crawler.jai -- smoke turns 250 seed 99     headless autopilot, prints STATE_HASH
//   jaiscript.exe crawler.jai -- input "11^dwwwwwQ" quiet    scripted keys (';'=enter '^'=esc)
//   jaiscript.exe --backend=interp crawler.jai -- smoke backend interp
//
// Flags mirror main.cpp (leading dashes optional): seed N, smoke, turns/frames N, input KEYS,
// quiet, dev, god, load, save PATH, backend LABEL. Shim-only: view N (first-person view rows),
// dump PATH (smoke frames + FRAME_HASH), shots N, bench N.
//
// Runner-keyboard differences from the C++ host's blocking _getch console:
//   - uppercase commands (E M S Q R) = shift+letter (the runner reports lowercase edges;
//     the shim re-cases them while shift is held)
//   - '>' descend = TAB, '.' wait = SPACE (the runner never delivers punctuation keys
//     on Windows; both stand-ins are otherwise unused)
//   - '?' help is unreachable interactively on Windows for the same reason ('c' still
//     shows the character sheet; scripted/--input '?' works)

// ---- capture the runner's host functions BEFORE this file shadows the names ----
import "host_capture.jai";

// ---- flags as globals (main.cpp's host_options, parsed from the runner's ARGS) ----
var HOST_SEED = 1337;
var HOST_SMOKE = false;
var HOST_TURNS = 250;
var HOST_DEV = false;
var HOST_GOD = false;
var HOST_LOAD = false;
var HOST_SCRIPTED = false;
var SAVE_PATH = parent_dir(root_script_dir()) + "/crawler_save.json";   // beside scripts/, whatever the working directory
var HOST_BACKEND = "vm";
var ESC = "\x1b";
var SHIM_QUIET = false;
var SHIM_INPUT = "";
var SHIM_DUMP = "";         // dump PATH: every smoke turn's frame -> PATH.ans (+ FRAME_HASH)
var SHIM_BENCH = 0;         // bench N: re-render the final smoke view N times, print ms/frame
var SHIM_SHOT_EVERY = 0;    // shots N (with dump): also PATH_tT.bmp/.txt every N turns
var SHIM_VIEW_ROWS = 0;     // view N: an N-row first-person view, 16:5 like the default 64x20 (0 = fit the console)

function shim_flag_name(string a) -> string {
	int i = 0;
	while (i < a.size() && a[i] == '-') { i = i + 1; }
	return a.substr(i, a.size() - i);
}

function shim_parse_int(string text, int fallback) -> int {
	if (text.size() == 0) { return fallback; }
	int v = 0;
	int i = 0;
	bool negative = false;
	if (text[0] == '-') { negative = true; i = 1; if (text.size() == 1) { return fallback; } }
	while (i < text.size()) {
		var c = text[i];
		if (c < '0' || c > '9') { return fallback; }
		v = v * 10 + (c - '0');
		i = i + 1;
	}
	return negative ? -v : v;
}

{
	int i = 0;
	while (i < ARGS.size()) {
		var flag = shim_flag_name(ARGS[i]);
		var value = i + 1 < ARGS.size() ? ARGS[i + 1] : "";
		if (flag == "seed") { HOST_SEED = shim_parse_int(value, HOST_SEED); i = i + 2; }
		else if (flag == "turns" || flag == "frames") { HOST_TURNS = shim_parse_int(value, HOST_TURNS); i = i + 2; }
		else if (flag == "input") { SHIM_INPUT = value; i = i + 2; }
		else if (flag == "save") { SAVE_PATH = value; i = i + 2; }
		else if (flag == "backend") { HOST_BACKEND = value; i = i + 2; }
		else if (flag == "dump") { SHIM_DUMP = value; HOST_SMOKE = true; i = i + 2; }
		else if (flag == "shots") { SHIM_SHOT_EVERY = shim_parse_int(value, 0); i = i + 2; }
		else if (flag == "view") { SHIM_VIEW_ROWS = shim_parse_int(value, 20); i = i + 2; }
		else if (flag == "bench") { SHIM_BENCH = shim_parse_int(value, 200); HOST_SMOKE = true; i = i + 2; }
		else if (flag == "smoke") { HOST_SMOKE = true; i = i + 1; }
		else if (flag == "quiet") { SHIM_QUIET = true; i = i + 1; }
		else if (flag == "dev") { HOST_DEV = true; i = i + 1; }
		else if (flag == "god") { HOST_GOD = true; i = i + 1; }
		else if (flag == "load") { HOST_LOAD = true; i = i + 1; }
		else {
			print("crawler shim: unknown arg '" + ARGS[i] + "' (seed/turns/input/save/backend/smoke/quiet/dev/god/load/view/dump/shots/bench)");
			i = i + 1;
		}
	}
}
if (HOST_SMOKE) { SHIM_QUIET = true; }
if (SHIM_INPUT != "") { HOST_SCRIPTED = true; }

// ---- console bindings (main.cpp's console_host) ----
function draw(string frame) -> void {
	if (SHIM_QUIET) { return; }
	print(ESC + "[H" + frame + ESC + "[0m", skip_newline);
}

function clear_screen() -> void {
	if (SHIM_QUIET) { return; }
	print(ESC + "[2J" + ESC + "[H", skip_newline);
}

function frame_mark() -> void { }   // host-side --time latency probe; nothing to measure here

function chr(int code) -> string { return "" + to_char(code & 0x7F); }

function host_log(string line) -> void { print(line); }

function sleep_ms(int ms) -> void {
	if (SHIM_QUIET || HOST_SCRIPTED) { return; }
	HOST_SLEEP_MS(ms);
}

// Blocking read_key over the runner's non-blocking poll. Scripted input replays
// main.cpp's stand-ins (';'=enter '^'=esc) and hard-quits with "Q" when exhausted.
var SHIM_KEYS = [];
var SHIM_KEY_INDEX = 0;
{
	for (int i = 0; i < SHIM_INPUT.size(); ++i) { SHIM_KEYS.push(SHIM_INPUT.substr(i, 1)); }
}

function shim_upper(string k) -> string {
	var c = k[0];
	if (c >= 'a' && c <= 'z') { return "" + to_char(c - 32); }
	return k;
}

function read_key() -> string {
	if (HOST_SCRIPTED) {
		if (SHIM_KEY_INDEX >= SHIM_KEYS.size()) { return "Q"; }
		var queued = SHIM_KEYS[SHIM_KEY_INDEX];
		SHIM_KEY_INDEX = SHIM_KEY_INDEX + 1;
		if (queued == ";") { return "enter"; }
		if (queued == "^") { return "esc"; }
		return queued;
	}
	while (true) {
		var k = HOST_READ_KEY_RAW();
		if (k == "") { HOST_SLEEP_MS(10); continue; }
		if (k.size() == 1) {
			return key_down("shift") ? shim_upper(k) : k;
		}
		if (k == "shift" || k == "ctrl" || k == "alt") { continue; }
		if (k == "tab") { return ">"; }
		if (k == "space") { return "."; }
		return k;   // up/down/left/right/enter/esc/backspace pass through
	}
}

// ---- deterministic rng: bit-exact xorshift64* twin of main.cpp's crawler_rng ----
// Checked int64 arithmetic can't wrap, so the mod-2^64 multiply is done in 16/32-bit
// limbs; >> is arithmetic, so logical shifts mask after shifting. Verified against an
// exact-uint64 reference: seed 1337 -> raws -5812238713413946495, 5678468593443823633,
// 5268740346836264482; next(100) 21,33,82; seed 0 next(100) 77; masked set_state replay 755.
var RNG_GOLDEN = -7046029254386353131;   // 0x9E3779B97F4A7C15 as signed int64

function u64_shr(int x, int n) -> int {
	return (x >> n) & ((1 << (64 - n)) - 1);
}

function u64_add(int x, int y) -> int {
	var lo = (x & 0xFFFFFFFF) + (y & 0xFFFFFFFF);
	var hi = u64_shr(x, 32) + u64_shr(y, 32) + (lo >> 32);
	return (lo & 0xFFFFFFFF) | (hi << 32);
}

function u32_mul_lo(int x, int y) -> int {
	var xl = x & 0xFFFF;
	var xh = x >> 16;
	var yl = y & 0xFFFF;
	var yh = y >> 16;
	return (xl * yl + ((xl * yh + xh * yl) << 16)) & 0xFFFFFFFF;
}

function u64_mul(int a, int b) -> int {
	var a0 = a & 0xFFFFFFFF;
	var a1 = u64_shr(a, 32);
	var b0 = b & 0xFFFFFFFF;
	var b1 = u64_shr(b, 32);
	var lo = u64_add(a0 * (b0 & 0xFFFF), (a0 * (b0 >> 16)) << 16);
	var mid = (u32_mul_lo(a0, b1) + u32_mul_lo(a1, b0)) & 0xFFFFFFFF;
	return u64_add(lo, mid << 32);
}

function u64_mod_small(int x, int n) -> int {
	var r = (x & 0x7FFFFFFFFFFFFFFF) % n;
	if (x < 0) { r = (r + (((0x4000000000000000 % n) * 2) % n)) % n; }
	return r;
}

class Rng {
	int s = 0;
	Rng(int seed) {
		s = seed != 0 ? seed : RNG_GOLDEN;
		for (int i = 0; i < 4; ++i) { step(); }
	}
	void step() {
		s = s ^ u64_shr(s, 12);
		s = s ^ (s << 25);
		s = s ^ u64_shr(s, 27);
	}
	int next_raw() {
		step();
		return u64_mul(s, 0x2545F4914F6CDD1D);
	}
	int next(int n) {
		if (n <= 0) { return 0; }
		return u64_mod_small(next_raw(), n);
	}
	int roll(int lo, int hi) {
		if (hi <= lo) { return lo; }
		return lo + next(hi - lo + 1);
	}
	bool chance(int percent) { return next(100) < percent; }
	int state() { return s & 0x3FFFFFFFFFFFFFFF; }
	void set_state(int v) {
		s = v;
		if (s == 0) { s = RNG_GOLDEN; }
	}
}

// ---- boot: same sequence as main.cpp (console init, import, crawler_main) ----
import "main.jai";

var SHIM_FRAMES = [];
var SHIM_FRAME_MS = 0;

function shim_view_probe() -> void {
	int t0 = clock_ms();
	string frame = frame_join(frame_lines(game_view_lines()));
	SHIM_FRAME_MS = SHIM_FRAME_MS + clock_ms() - t0;
	SHIM_FRAMES.push(frame);
	if (SHIM_SHOT_EVERY > 0 && G.turn % SHIM_SHOT_EVERY == 0) { shim_dump_view(`${SHIM_DUMP}_t${G.turn}`); }
}

function shim_le(int v, int bytes) -> string {
	string out = "";
	for (int i = 0; i < bytes; ++i) { out += to_char((v >> (8 * i)) & 255); }
	return out;
}

// the last rendered view as ground truth: a 24-bit BMP and a luminance sketch
function shim_dump_view(string base) -> void {
	int pad = (4 - (PW * 3) % 4) % 4;
	int stride = PW * 3 + pad;
	string bmp = "BM" + shim_le(54 + stride * PH, 4) + shim_le(0, 4) + shim_le(54, 4);
	bmp += shim_le(40, 4) + shim_le(PW, 4) + shim_le(PH, 4) + shim_le(1, 2) + shim_le(24, 2) + shim_le(0, 4);
	bmp += shim_le(stride * PH, 4) + shim_le(2835, 4) + shim_le(2835, 4) + shim_le(0, 4) + shim_le(0, 4);
	string ramp = " .:-=+*#%@";
	var sketch = [];
	for (int y = PH - 1; y >= 0; --y) {
		for (int x = 0; x < PW; ++x) {
			int rgb = VPAL_RGB[VPIX[y * PW + x]];
			bmp += "" + to_char(rgb & 255) + to_char((rgb >> 8) & 255) + to_char((rgb >> 16) & 255);
		}
		bmp += shim_le(0, pad);
	}
	for (int y = 0; y < PH; y += 2) {
		string row = "";
		for (int x = 0; x < PW; ++x) {
			int a = VPAL_RGB[VPIX[y * PW + x]];
			int b = VPAL_RGB[VPIX[(y + 1) * PW + x]];
			int lum = ((a >> 16) & 255) * 2 + ((a >> 8) & 255) * 3 + (a & 255) + ((b >> 16) & 255) * 2 + ((b >> 8) & 255) * 3 + (b & 255);
			row += ramp.substr(imin(9, lum / 250), 1);
		}
		sketch.push(row);
	}
	write_file(base + ".bmp", bmp);
	write_file(base + ".txt", sketch.join("\n") + "\n");
}

function shim_probe_report() -> void {
	if (SHIM_DUMP != "") {
		shim_dump_view(SHIM_DUMP);
		var path = SHIM_DUMP + ".ans";
		write_file(path, SHIM_FRAMES.join(""));
		print(`FRAME_HASH: ${sha256_file(path)} (${SHIM_FRAMES.size()} frames -> ${path})`);
		print(`frame build: ${SHIM_FRAME_MS} ms total, ${(1.0 * SHIM_FRAME_MS) / imax(1, SHIM_FRAMES.size())} ms/frame`);
	}
	if (SHIM_BENCH > 0) {
		int t0 = clock_ms();
		int bytes = 0;
		for (int i = 0; i < SHIM_BENCH; ++i) { bytes = frame_join(frame_lines(game_view_lines())).size(); }
		int spent = clock_ms() - t0;
		print(`bench: ${SHIM_BENCH} frames, ${(1.0 * spent) / SHIM_BENCH} ms/frame, ${bytes} bytes/frame`);
	}
}

if (SHIM_DUMP != "") { VIEW_PROBE = shim_view_probe; }
if (SHIM_VIEW_ROWS == 0 && !SHIM_QUIET && !HOST_SCRIPTED) {
	// a big console gets a big view: title + view + 5 log lines tall, panel beside it
	SHIM_VIEW_ROWS = imin(48, imin(term_rows() - 8, ((term_cols() - 18) * 5) / 16));
}
if (SHIM_VIEW_ROWS > 20) {
	VIEW_H = SHIM_VIEW_ROWS;
	VIEW_W = SHIM_VIEW_ROWS * 16 / 5;
}
if (!SHIM_QUIET) { print(ESC + "[?25l" + ESC + "[2J" + ESC + "[H", skip_newline); }
try {
	crawler_main();
} catch (e) {
	if (!SHIM_QUIET) { print(ESC + "[0m" + ESC + "[?25h", skip_newline); print(""); }
	print("crawler shim: script error: " + to_string(e));
	throw e;
}
shim_probe_report();
if (!SHIM_QUIET) { print(ESC + "[0m" + ESC + "[?25h", skip_newline); print(""); }
