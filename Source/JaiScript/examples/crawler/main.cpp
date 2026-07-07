// jai_crawler host: a deliberately thin console shell around a JaiScript engine.
// GRUBWELL is a first-person, facing-locked dungeon crawler; everything that
// makes it a game (raycast view, dungeon gen, combat, AI, items, UI text) lives
// in scripts/*.jai. The host only provides: a terminal, a blocking keyboard,
// a seeded rng class, file IO for saves, and command-line flags as globals.

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#ifdef JAISCRIPT_ENABLE_DEBUGGER
#include <jaiscript/debug/connector.hpp>
#endif

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace {

struct host_options {
	int64_t seed = 1337;
	std::string backend = "vm";
	bool smoke = false;
	bool dev = false;
	bool god = false;
	bool load = false;
	bool quiet = false;          // suppress frame drawing (smoke implies quiet)
	bool time_turns = false;     // measure key->frame latency; summary on exit
	int64_t turns = 250;         // smoke turn budget
	std::string scripted_input;  // keys fed to read_key(); empty = interactive
	std::string scripts_dir;
	std::string save_path = "jai_crawler_save.json";
	bool debug = false;          // stand up the DAP debug_connector (forces the interpreter)
	bool debug_wait = false;     // also hold boot until a debugger attaches (--debug-wait)
	int64_t debug_port = 52472;   // port VS Code attaches to
};

// --time latency probe: read_key return -> frame_mark (turn logic) ->
// draw entry (frame string build) -> fwrite+fflush (console blit).
struct turn_perf {
	using clock = std::chrono::steady_clock;
	bool enabled = false;
	bool key_pending = false;
	bool frame_pending = false;
	clock::time_point key_stamp{};
	clock::time_point frame_stamp{};
	int turns = 0;
	double logic_sum = 0, logic_max = 0, logic_min = 1e9;
	double build_sum = 0, build_max = 0, build_min = 1e9;
	double blit_sum = 0, blit_max = 0, blit_min = 1e9;

	static double ms_between(clock::time_point a, clock::time_point b) {
		return std::chrono::duration<double, std::milli>(b - a).count();
	}

	void on_key() {
		if (!enabled) { return; }
		key_stamp = clock::now();
		key_pending = true;
		frame_pending = false;
	}

	void on_frame_mark() {
		if (!enabled || !key_pending) { return; }
		frame_stamp = clock::now();
		frame_pending = true;
	}

	// returns true if this draw closes a measured turn (caller then times the blit)
	bool on_draw_begin() {
		if (!enabled || !key_pending) { return false; }
		auto now = clock::now();
		double logic = frame_pending ? ms_between(key_stamp, frame_stamp) : ms_between(key_stamp, now);
		double build = frame_pending ? ms_between(frame_stamp, now) : 0.0;
		logic_sum += logic; if (logic > logic_max) { logic_max = logic; } if (logic < logic_min) { logic_min = logic; }
		build_sum += build; if (build > build_max) { build_max = build; } if (build < build_min) { build_min = build; }
		++turns;
		key_pending = false;
		frame_pending = false;
		return true;
	}

	void on_blit(double ms) {
		blit_sum += ms;
		if (ms > blit_max) { blit_max = ms; }
		if (ms < blit_min) { blit_min = ms; }
	}

	void report() const {
		if (!enabled || turns == 0) { return; }
		std::fprintf(stderr,
			"\n--time over %d key->frame turns (ms):\n"
			"  turn logic   min %7.3f  avg %7.3f  max %8.3f\n"
			"  frame build  min %7.3f  avg %7.3f  max %8.3f\n"
			"  console blit min %7.3f  avg %7.3f  max %8.3f\n"
			"  total        avg %7.3f\n",
			turns,
			logic_min, logic_sum / turns, logic_max,
			build_min, build_sum / turns, build_max,
			blit_min, blit_sum / turns, blit_max,
			(logic_sum + build_sum + blit_sum) / turns);
	}
};

struct console_host {
	bool vt_enabled = false;
	bool quiet = false;
	std::deque<std::string> scripted;
	bool input_was_scripted = false;
	turn_perf perf;

	void init() {
#ifdef _WIN32
		HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
			vt_enabled = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
		}
		SetConsoleOutputCP(CP_UTF8);
#endif
		if (!quiet) {
			std::fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
			std::fflush(stdout);
		}
	}

	void shutdown() {
		if (!quiet) {
			std::fputs("\x1b[0m\x1b[?25h\n", stdout);
			std::fflush(stdout);
		}
	}

	void draw(const std::string& frame) {
		if (quiet) { return; }
		bool measured = perf.on_draw_begin();
		auto blit_start = turn_perf::clock::now();
		std::string out;
		out.reserve(frame.size() + 8);
		out += "\x1b[H";
		out += frame;
		out += "\x1b[0m";
		std::fwrite(out.data(), 1, out.size(), stdout);
		std::fflush(stdout);
		if (measured) { perf.on_blit(turn_perf::ms_between(blit_start, turn_perf::clock::now())); }
	}

	void clear() {
		if (quiet) { return; }
		std::fputs("\x1b[2J\x1b[H", stdout);
		std::fflush(stdout);
	}

	std::string read_key() {
		std::string k = read_key_impl();
		perf.on_key();
		return k;
	}

	std::string read_key_impl() {
		if (input_was_scripted) {
			if (scripted.empty()) { return "Q"; }  // scripted run finished: hard quit
			std::string k = scripted.front();
			scripted.pop_front();
			if (k == ";") { return "enter"; }  // scripted stand-ins for special keys
			if (k == "^") { return "esc"; }
			return k;
		}
#ifdef _WIN32
		int c = _getch();
		if (c == 0 || c == 0xE0) {
			int c2 = _getch();
			switch (c2) {
				case 72: return "up";
				case 80: return "down";
				case 75: return "left";
				case 77: return "right";
				default: return "";
			}
		}
		switch (c) {
			case 13: return "enter";
			case 27: return "esc";
			case 8: return "backspace";
			case 9: return "tab";
			default: break;
		}
		if (c >= 32 && c < 127) { return std::string(1, static_cast<char>(c)); }
		return "";
#else
		int c = std::getchar();
		if (c == EOF) { return "Q"; }
		if (c == '\n') { return "enter"; }
		return std::string(1, static_cast<char>(c));
#endif
	}
};

// Deterministic xorshift64* — the ONLY randomness the game is allowed to touch.
// Bound as script class `Rng` so dungeons replay exactly from a seed on both backends.
struct crawler_rng {
	uint64_t s;

	explicit crawler_rng(jai::script_int seed)
		: s(seed != 0 ? static_cast<uint64_t>(seed) : 0x9E3779B97F4A7C15ull) {
		for (int i = 0; i < 4; ++i) { next_raw(); }
	}

	uint64_t next_raw() {
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		return s * 0x2545F4914F6CDD1Dull;
	}

	jai::script_int next(jai::script_int n) {
		if (n <= 0) { return 0; }
		return static_cast<jai::script_int>(next_raw() % static_cast<uint64_t>(n));
	}

	jai::script_int roll(jai::script_int lo, jai::script_int hi) {
		if (hi <= lo) { return lo; }
		return lo + next(hi - lo + 1);
	}

	bool chance(jai::script_int percent) { return next(100) < percent; }

	// Masked to 62 bits so the value survives a script-side int64 round trip via JSON.
	jai::script_int state() const { return static_cast<jai::script_int>(s & 0x3FFFFFFFFFFFFFFFull); }
	void set_state(jai::script_int v) {
		s = static_cast<uint64_t>(v);
		if (s == 0) { s = 0x9E3779B97F4A7C15ull; }
	}
};

std::string slurp_file(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) { return ""; }
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool spill_file(const std::string& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) { return false; }
	out.write(content.data(), static_cast<std::streamsize>(content.size()));
	return out.good();
}

std::string locate_scripts_dir(const std::string& override_dir, const char* argv0) {
	namespace fs = std::filesystem;
	if (!override_dir.empty()) { return override_dir; }
#ifdef JAI_CRAWLER_SOURCE_SCRIPTS_DIR
	if (fs::exists(fs::path(JAI_CRAWLER_SOURCE_SCRIPTS_DIR) / "main.jai")) {
		return JAI_CRAWLER_SOURCE_SCRIPTS_DIR;
	}
#endif
	fs::path exe_dir = fs::absolute(fs::path(argv0)).parent_path();
	fs::path local = exe_dir / "crawler_scripts";
	if (fs::exists(local / "main.jai")) { return local.string(); }
	return "scripts";
}

#ifdef JAISCRIPT_ENABLE_DEBUGGER
// Attach a DAP connector for the whole engine lifetime — it listens until the engine is torn
// down, so you can attach from VS Code at ANY time; breakpoints you set bind on the next
// statement. Pass wait_to_boot (--debug-wait) to also hold boot until you press Enter, which
// lets breakpoints in boot-time script (dungeon gen, class init) catch the very first run.
void start_debugger(jai::engine& eng, int port, bool wait_to_boot) {
	jai::debug::listen(eng, port);
	std::fprintf(stderr, "[jai debug] listening on 127.0.0.1:%d - attach from VS Code any time.\n", port);
	if (wait_to_boot) {
		std::fprintf(stderr, "[jai debug] --debug-wait: attach + set breakpoints, then press Enter to boot.\n> ");
		std::fflush(stderr);
		std::string line;
		std::getline(std::cin, line);
	}
	std::fflush(stderr);
}
#endif

void print_usage() {
	std::puts(
		"jai_crawler - GRUBWELL, a first-person dungeon crawler written in JaiScript\n"
		"  --seed N        dungeon seed (default 1337)\n"
		"  --backend B     vm | interpreter (default vm)\n"
		"  --smoke         headless deterministic autoplay; prints a state hash\n"
		"  --turns N       smoke turn budget (default 250)\n"
		"  --input KEYS    scripted keys fed to read_key() (e.g. \"wwdwg>\"; ';'=enter '^'=esc)\n"
		"  --quiet         suppress frame drawing (useful with --input)\n"
		"  --time          measure key->frame latency; prints a summary on exit\n"
		"  --dev           enable in-game script hot reload on 'R'\n"
		"  --god           god mode (no damage, monstrous stats)\n"
		"  --load          continue from the save file\n"
		"  --save PATH     save file path (default jai_crawler_save.json)\n"
		"  --scripts DIR   override the scripts directory\n"
		"  --debug         listen for a VS Code debugger for the whole run (forces interpreter)\n"
		"  --debug-wait    like --debug, but also hold boot until you attach + press Enter\n"
		"  --debug-port N  port for --debug (default 52472)\n");
}

} // namespace

int main(int argc, char** argv) {
	host_options opt;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next_arg = [&](const char* flag) -> std::string {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "missing value for %s\n", flag);
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--seed") { opt.seed = std::stoll(next_arg("--seed")); }
		else if (a == "--backend") { opt.backend = next_arg("--backend"); }
		else if (a == "--smoke") { opt.smoke = true; }
		else if (a == "--turns") { opt.turns = std::stoll(next_arg("--turns")); }
		else if (a == "--input") { opt.scripted_input = next_arg("--input"); }
		else if (a == "--quiet") { opt.quiet = true; }
		else if (a == "--time") { opt.time_turns = true; }
		else if (a == "--dev") { opt.dev = true; }
		else if (a == "--god") { opt.god = true; }
		else if (a == "--load") { opt.load = true; }
		else if (a == "--save") { opt.save_path = next_arg("--save"); }
		else if (a == "--scripts") { opt.scripts_dir = next_arg("--scripts"); }
		else if (a == "--debug") { opt.debug = true; }
		else if (a == "--debug-wait") { opt.debug = true; opt.debug_wait = true; }
		else if (a == "--debug-port") { opt.debug_port = std::stoll(next_arg("--debug-port")); }
		else if (a == "--help" || a == "-h") { print_usage(); return 0; }
		else { std::fprintf(stderr, "unknown flag: %s\n", a.c_str()); print_usage(); return 2; }
	}

	console_host console;
	console.quiet = opt.smoke || opt.quiet;
	console.perf.enabled = opt.time_turns;
	if (!opt.scripted_input.empty()) {
		console.input_was_scripted = true;
		for (char c : opt.scripted_input) { console.scripted.emplace_back(1, c); }
	}
	// Only --debug-wait holds the alt-screen back (its attach prompt prints to the normal
	// terminal). Plain --debug boots immediately with the connector listening in the
	// background, so init the console now.
	if (!(opt.debug && opt.debug_wait)) { console.init(); }

	auto eng = jai::engine::make();
	if (opt.debug) {
		opt.backend = "interpreter";   // the statement hook is interpreter-only (see DEBUGGER_DESIGN.md)
		eng->set_backend(jai::backend_type::interpreter);
	}
	else if (opt.backend == "vm") { eng->set_backend(jai::backend_type::vm); }
	else if (opt.backend == "interpreter" || opt.backend == "interp") {
		eng->set_backend(jai::backend_type::interpreter);
	} else {
		console.shutdown();
		std::fprintf(stderr, "unknown backend: %s\n", opt.backend.c_str());
		return 2;
	}
	eng->execution_budget(0.0);  // the game blocks on read_key(); wall-clock budgets don't apply
	jai::stdlib::register_all(eng);

	std::string scripts_dir = locate_scripts_dir(opt.scripts_dir, argv[0]);
	eng->add_include_path(scripts_dir);
	eng->set_import_behavior(jai::engine::import_behavior::always);

	// --- console bindings ---
	eng->add_function("draw", [&console](const std::string& frame) { console.draw(frame); });
	eng->add_function("clear_screen", [&console]() { console.clear(); });
	eng->add_function("read_key", [&console]() -> std::string { return console.read_key(); });
	eng->add_function("frame_mark", [&console]() { console.perf.on_frame_mark(); });
	eng->add_function("chr", [](jai::script_int code) -> std::string {
		return std::string(1, static_cast<char>(code & 0x7F));
	});
	eng->add_function("sleep_ms", [&console](jai::script_int ms) {
		if (console.quiet || console.input_was_scripted) { return; }
#ifdef _WIN32
		Sleep(static_cast<DWORD>(ms));
#endif
	});
	// stdout that works even while frames own the screen (smoke summaries, death dumps)
	eng->add_function("host_log", [](const std::string& line) {
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	});

	// --- file IO for saves ---
	eng->add_function("read_file", [](const std::string& path) -> std::string { return slurp_file(path); });
	eng->add_function("write_file", [](const std::string& path, const std::string& content) -> bool {
		return spill_file(path, content);
	});
	eng->add_function("file_exists", [](const std::string& path) -> bool {
		return std::filesystem::exists(path);
	});
	eng->add_function("delete_file", [](const std::string& path) -> bool {
		std::error_code ec;
		return std::filesystem::remove(path, ec);
	});

	// --- deterministic rng ---
	jai::dynamic_binder<crawler_rng>(*eng, "Rng")
		.constructor<jai::script_int>()
		.method("next", &crawler_rng::next)
		.method("roll", &crawler_rng::roll)
		.method("chance", &crawler_rng::chance)
		.method("state", &crawler_rng::state)
		.method("set_state", &crawler_rng::set_state)
		.build();

	// --- flags as globals ---
	eng->add_global("ESC", eng->make_value(std::string("\x1b")));
	eng->add_global("HOST_SEED", eng->make_value(opt.seed));
	eng->add_global("HOST_SMOKE", eng->make_value(opt.smoke));
	eng->add_global("HOST_TURNS", eng->make_value(opt.turns));
	eng->add_global("HOST_DEV", eng->make_value(opt.dev));
	eng->add_global("HOST_GOD", eng->make_value(opt.god));
	eng->add_global("HOST_LOAD", eng->make_value(opt.load));
	eng->add_global("HOST_SCRIPTED", eng->make_value(console.input_was_scripted));
	eng->add_global("SAVE_PATH", eng->make_value(opt.save_path));
	eng->add_global("HOST_BACKEND", eng->make_value(opt.backend));

	if (opt.debug) {
#ifdef JAISCRIPT_ENABLE_DEBUGGER
		start_debugger(*eng, static_cast<int>(opt.debug_port), opt.debug_wait);
#else
		std::fprintf(stderr, "--debug needs a build with -DJAISCRIPT_ENABLE_DEBUGGER=ON\n");
#endif
		if (opt.debug_wait) { console.init(); }
	}

	int exit_code = 0;
	try {
		eng->execute("import \"main.jai\";");
		eng->execute("crawler_main();");
	} catch (const std::exception& ex) {
		console.shutdown();
		std::fprintf(stderr, "\njai_crawler: script error: %s\n", ex.what());
		std::string trace = eng->format_stack_trace();
		if (!trace.empty()) { std::fprintf(stderr, "%s\n", trace.c_str()); }
		return 1;
	}
	console.shutdown();
	console.perf.report();
	return exit_code;
}
