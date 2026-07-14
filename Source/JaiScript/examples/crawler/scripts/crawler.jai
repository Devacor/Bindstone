// GRUBWELL on the standalone runner - a pure-JaiScript stand-in for the C++ host (../main.cpp).
// Drag THIS file onto jaiscript.exe (it must live in scripts/ so main.jai's sibling imports
// resolve - the runner puts the entry script's folder on the include path).
//
//   jaiscript.exe crawler.jai                                interactive run, seed 1337
//   jaiscript.exe crawler.jai -- seed 7 god dev load
//   jaiscript.exe crawler.jai -- smoke turns 250 seed 99     headless autopilot, prints STATE_HASH
//   jaiscript.exe crawler.jai -- input "11^dwwwwwQ" quiet    scripted keys (';'=enter '^'=esc)
//   jaiscript.exe --backend=interp crawler.jai -- smoke backend interp
//
// Flags mirror main.cpp (leading dashes optional): seed N, smoke, turns/frames N, input KEYS,
// quiet, dev, god, load, save PATH, backend LABEL.
//
// Runner-keyboard differences from the C++ host's blocking _getch console:
//   - uppercase commands (E M S Q R) = shift+letter (the runner reports lowercase edges;
//     the shim re-cases them while shift is held)
//   - '>' descend = TAB, '.' wait = SPACE (the runner never delivers punctuation keys
//     on Windows; both stand-ins are otherwise unused)
//   - '?' help is unreachable interactively on Windows for the same reason ('c' still
//     shows the character sheet; scripted/--input '?' works)

// ---- capture the runner's host functions BEFORE this file shadows the names ----
var HOST_READ_KEY_RAW = read_key;   // non-blocking edge poll: "" when no key is waiting
var HOST_SLEEP_MS = sleep_ms;       // real sleep (the shim's sleep_ms no-ops when headless)

// ---- flags as globals (main.cpp's host_options, parsed from the runner's ARGS) ----
var HOST_SEED = 1337;
var HOST_SMOKE = false;
var HOST_TURNS = 250;
var HOST_DEV = false;
var HOST_GOD = false;
var HOST_LOAD = false;
var HOST_SCRIPTED = false;
var SAVE_PATH = "crawler_save.json";
var HOST_BACKEND = "vm";
var ESC = "\x1b";
var SHIM_QUIET = false;
var SHIM_INPUT = "";

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
		else if (flag == "smoke") { HOST_SMOKE = true; i = i + 1; }
		else if (flag == "quiet") { SHIM_QUIET = true; i = i + 1; }
		else if (flag == "dev") { HOST_DEV = true; i = i + 1; }
		else if (flag == "god") { HOST_GOD = true; i = i + 1; }
		else if (flag == "load") { HOST_LOAD = true; i = i + 1; }
		else {
			print("crawler shim: unknown arg '" + ARGS[i] + "' (seed/turns/input/save/backend/smoke/quiet/dev/god/load)");
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

if (!SHIM_QUIET) { print(ESC + "[?25l" + ESC + "[2J" + ESC + "[H", skip_newline); }
try {
	crawler_main();
} catch (e) {
	if (!SHIM_QUIET) { print(ESC + "[0m" + ESC + "[?25h", skip_newline); print(""); }
	print("crawler shim: script error: " + to_string(e));
	throw e;
}
if (!SHIM_QUIET) { print(ESC + "[0m" + ESC + "[?25h", skip_newline); print(""); }
