// jai_gloom host: a deliberately thin console shell around a JaiScript engine.
// GLOOM is a real-time 2.5D raycast shooter; everything that makes it a game
// (raycaster, enemies, weapons, particles, HUD, episode) lives in scripts/*.jai.
// The host only provides: a VT console, held-key state + key events, a monotonic
// clock, a seeded rng class, and command-line flags as globals.
//
//   --smoke runs N deterministic autopilot ticks on BOTH backends and compares
//   the sim STATE_HASH and the frame-stream hash (byte parity, demoreel-style).

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace {

struct host_options {
	int64_t seed = 666;
	std::string backend = "vm";
	bool smoke = false;
	int64_t ticks = 2000;            // smoke tick budget
	int64_t workers = -1;            // parallel_transform worker count; -1 = engine default, 0 = serial script loop
	int64_t width = 0;               // 0 = autodetect
	int64_t height = 0;
	double target_fps = 30.0;
	std::string scripts_dir;
	int64_t bench = 0;               // run N frames of the real loop headless-input, report sim/draw split
	int64_t dump_frame = -1;         // smoke: write frame N to gloom_frame_<backend>.txt
	bool god = false;
	std::string pix = "quad";        // sub-cell pixel mode: half (1x2) | quad (2x2) | sext (2x3)
};

double now_seconds() {
	using clock = std::chrono::steady_clock;
	static const clock::time_point start = clock::now();
	return std::chrono::duration<double>(clock::now() - start).count();
}

struct console_host {
	bool active = false;
	double last_draw_ms = 0.0;
#ifdef _WIN32
	HANDLE hout = INVALID_HANDLE_VALUE;
	HWND console_window = nullptr;
#endif

	void init() {
#ifdef _WIN32
		hout = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode_flags = 0;
		if (hout != INVALID_HANDLE_VALUE && GetConsoleMode(hout, &mode_flags)) {
			SetConsoleMode(hout, mode_flags | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
		}
		SetConsoleOutputCP(CP_UTF8);
		console_window = GetConsoleWindow();
		timeBeginPeriod(1);
#endif
		std::fputs("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H", stdout);
		std::fflush(stdout);
		active = true;
	}

	void shutdown() {
		if (!active) { return; }
		std::fputs("\x1b[0m\x1b[?1049l\x1b[?25h", stdout);
		std::fflush(stdout);
#ifdef _WIN32
		timeEndPeriod(1);
#endif
		active = false;
	}

	void draw(const std::string& frame) {
		double t0 = now_seconds();
		std::string out;
		out.reserve(frame.size() + 16);
		out += "\x1b[H";
		out += frame;
		out += "\x1b[0m";
#ifdef _WIN32
		DWORD written = 0;
		WriteConsoleA(hout, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
#else
		std::fwrite(out.data(), 1, out.size(), stdout);
		std::fflush(stdout);
#endif
		last_draw_ms = (now_seconds() - t0) * 1000.0;
	}

	void size(int& w, int& h) {
		w = 100; h = 40;
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
			w = info.srWindow.Right - info.srWindow.Left;      // one column spare: no wrap
			h = info.srWindow.Bottom - info.srWindow.Top;      // one row spare: no scroll
		}
#endif
		if (w > 120) { w = 120; }
		if (h > 46) { h = 46; }
		if (w < 70) { w = 70; }
		if (h < 30) { h = 30; }
	}

	// held-key state for continuous movement (only while our window has focus)
	bool key_down(const std::string& name) {
#ifdef _WIN32
		if (console_window && GetForegroundWindow() != console_window) { return false; }
		int vk = 0;
		if (name.size() == 1) {
			char c = name[0];
			if (c >= 'a' && c <= 'z') { vk = 'A' + (c - 'a'); }
			else if (c >= '0' && c <= '9') { vk = c; }
		}
		else if (name == "left") { vk = VK_LEFT; }
		else if (name == "right") { vk = VK_RIGHT; }
		else if (name == "up") { vk = VK_UP; }
		else if (name == "down") { vk = VK_DOWN; }
		else if (name == "space") { vk = VK_SPACE; }
		else if (name == "shift") { vk = VK_SHIFT; }
		else if (name == "ctrl") { vk = VK_CONTROL; }
		if (vk == 0) { return false; }
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
		(void)name;
		return false;
#endif
	}

	// "" when no key pending; letters lowercased; specials by name (edge events)
	std::string poll_key() {
#ifdef _WIN32
		if (!_kbhit()) { return ""; }
		int c = _getch();
		if (c == 0 || c == 0xE0) {
			int c2 = _kbhit() ? _getch() : 0;
			switch (c2) {
				case 72: return "up";
				case 80: return "down";
				case 75: return "left";
				case 77: return "right";
				default: return "";
			}
		}
		switch (c) {
			case 9: return "tab";
			case 13: return "enter";
			case 27: return "esc";
			default: break;
		}
		if (c >= 'A' && c <= 'Z') { c += 32; }
		if (c >= 32 && c < 127) { return std::string(1, static_cast<char>(c)); }
#endif
		return "";
	}
};

// Deterministic xorshift64* — the ONLY randomness the game may touch, so runs
// replay identically from a seed on both backends (that is the parity test).
struct gloom_rng {
	uint64_t s;

	explicit gloom_rng(jai::script_int seed)
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

	jai::script_float nextf() {
		return static_cast<jai::script_float>(next_raw() >> 11) * (1.0 / 9007199254740992.0);
	}

	jai::script_int state() const { return static_cast<jai::script_int>(s & 0x3FFFFFFFFFFFFFFFull); }
};

std::string locate_scripts_dir(const std::string& override_dir, const char* argv0) {
	namespace fs = std::filesystem;
	if (!override_dir.empty()) { return override_dir; }
#ifdef JAI_GLOOM_SOURCE_SCRIPTS_DIR
	if (fs::exists(fs::path(JAI_GLOOM_SOURCE_SCRIPTS_DIR) / "main.jai")) {
		return JAI_GLOOM_SOURCE_SCRIPTS_DIR;
	}
#endif
	fs::path exe_dir = fs::absolute(fs::path(argv0)).parent_path();
	fs::path local = exe_dir / "gloom_scripts";
	if (fs::exists(local / "main.jai")) { return local.string(); }
	return "scripts";
}

uint64_t fnv1a(uint64_t hash, const std::string& bytes) {
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

std::shared_ptr<jai::engine> make_gloom_engine(const std::string& backend, console_host& console,
                                               const host_options& opt, bool live_input) {
	auto eng = jai::engine::make();
	eng->set_backend(backend == "vm" ? jai::backend_type::vm : jai::backend_type::interpreter);
	eng->execution_budget(0.0);
	if (opt.workers > 0) { eng->parallel_thread_count(static_cast<size_t>(opt.workers)); }
	jai::stdlib::register_all(eng);

	jai::dynamic_binder<gloom_rng>(*eng, "Rng")
		.constructor<jai::script_int>()
		.method("next", &gloom_rng::next)
		.method("roll", &gloom_rng::roll)
		.method("chance", &gloom_rng::chance)
		.method("nextf", &gloom_rng::nextf)
		.method("state", &gloom_rng::state)
		.build();

	eng->add_function("host_log", [](const std::string& line) {
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	});
	eng->add_function("key_down", [&console, live_input](const std::string& name) -> bool {
		if (!live_input) { return false; }
		return console.key_down(name);
	});
	// float->int casts for the places typed-local truncation can't reach
	eng->add_function("itrunc", [](jai::script_float v) -> jai::script_int {
		return static_cast<jai::script_int>(v);
	});
	eng->add_function("ifloor", [](jai::script_float v) -> jai::script_int {
		return static_cast<jai::script_int>(std::floor(v));
	});
	// codepoint -> UTF-8 text (the sub-cell block glyphs live above the lexer's
	// escape set; U+1FB00 sextants are astral, so 4-byte sequences too)
	eng->add_function("utf8", [](jai::script_int cp) -> std::string {
		std::string out;
		uint32_t c = static_cast<uint32_t>(cp);
		if (c < 0x80) { out += static_cast<char>(c); }
		else if (c < 0x800) {
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		return out;
	});
	eng->add_global("ESC", eng->make_value(std::string("\x1b")));
	eng->add_global("HOST_SEED", eng->make_value(opt.seed));
	eng->add_global("HOST_SMOKE", eng->make_value(opt.smoke));
	eng->add_global("HOST_TICKS", eng->make_value(opt.ticks));
	eng->add_global("HOST_WORKERS", eng->make_value(opt.workers));
	eng->add_global("HOST_GOD", eng->make_value(opt.god));
	eng->add_global("HOST_BACKEND", eng->make_value(backend));
	// 0 half (1x2), 1 quad (2x2), 2 sext (2x3) pixels per cell
	int64_t pix_mode = opt.pix == "half" ? 0 : (opt.pix == "sext" ? 2 : 1);
	eng->add_global("HOST_PIX", eng->make_value(pix_mode));
	return eng;
}

jai::script_value call_script(jai::engine& eng, const char* fn_name,
                              const std::vector<jai::script_value>& args) {
	jai::script_value fn = eng.get_variable(fn_name);
	auto result = fn.as_function()(args);
	if (!result.has_value()) {
		throw std::runtime_error(std::string(fn_name) + ": " + std::string(result.message()));
	}
	return result.value();
}

int run_smoke(const std::string& scripts_dir, const host_options& opt, console_host& console) {
	const int w = 100, h = 40;
	const double dt = 1.0 / 30.0;
	std::printf("jai_gloom --smoke: %lld ticks @ %dx%d, seed %lld, fixed dt %.5f, workers %lld\n\n",
		static_cast<long long>(opt.ticks), w, h, static_cast<long long>(opt.seed),
		dt, static_cast<long long>(opt.workers));
	std::printf("%-12s | %10s | %9s | %-18s | %s\n",
		"backend", "total ms", "ms/tick", "frame hash", "STATE_HASH");
	std::printf("-------------+------------+-----------+--------------------+------------\n");

	uint64_t frame_hashes[2] = {0, 0};
	int64_t state_hashes[2] = {0, 0};
	double per_tick[2] = {0, 0};
	const char* names[2] = {"interpreter", "vm"};
	for (int b = 0; b < 2; ++b) {
		auto eng = make_gloom_engine(names[b], console, opt, false);
		eng->add_include_path(scripts_dir);
		eng->set_import_behavior(jai::engine::import_behavior::always);
		eng->execute("import \"main.jai\";");
		call_script(*eng, "gloom_boot", {
			jai::script_value(static_cast<int64_t>(w), eng.get()),
			jai::script_value(static_cast<int64_t>(h), eng.get()),
		});

		uint64_t hash = 14695981039346656037ull;
		double t0 = now_seconds();
		for (int64_t i = 0; i < opt.ticks; ++i) {
			std::string text;
			try {
				jai::script_value frame = call_script(*eng, "gloom_frame", {
					jai::script_value(dt, eng.get()),
					eng->make_value(std::string("")),
					jai::script_value(0.0, eng.get()),
					jai::script_value(0.0, eng.get()),
					jai::script_value(0.0, eng.get()),
				});
				text = frame.to_string();
			} catch (const std::exception& ex) {
				std::string trace = eng->format_stack_trace();
				throw std::runtime_error("tick " + std::to_string(i) + " [" + names[b] + "]: " +
					ex.what() + (trace.empty() ? "" : "\n" + trace));
			}
			if (i == opt.dump_frame) {
				std::ofstream out(std::string("gloom_frame_") + names[b] + ".txt",
					std::ios::binary | std::ios::trunc);
				out.write(text.data(), static_cast<std::streamsize>(text.size()));
			}
			hash = fnv1a(hash, text);
		}
		double total_ms = (now_seconds() - t0) * 1000.0;
		per_tick[b] = total_ms / opt.ticks;
		frame_hashes[b] = hash;
		state_hashes[b] = call_script(*eng, "gloom_state_hash", {}).as_int();
		std::printf("%-12s | %10.1f | %9.3f | %016llx   | %lld\n",
			names[b], total_ms, per_tick[b],
			static_cast<unsigned long long>(hash),
			static_cast<long long>(state_hashes[b]));
		call_script(*eng, "gloom_summary", {});
	}

	bool state_ok = state_hashes[0] == state_hashes[1];
	bool frame_ok = frame_hashes[0] == frame_hashes[1];
	std::printf("\nstate parity: %s | frame parity: %s\n",
		state_ok ? "OK" : "MISMATCH", frame_ok ? "OK (byte-identical)" : "MISMATCH");
	if (state_ok && frame_ok && per_tick[1] > 0.0) {
		std::printf("vm speedup over interpreter: %.2fx\n", per_tick[0] / per_tick[1]);
	}
	return (state_ok && frame_ok) ? 0 : 1;
}

void print_usage() {
	std::puts(
		"jai_gloom - GLOOM, a terminal raycast shooter written in JaiScript\n"
		"  --seed N        run seed (default 666)\n"
		"  --backend B     vm | interpreter (default vm)\n"
		"  --smoke         headless deterministic autoplay on BOTH backends; parity check\n"
		"  --ticks N       smoke tick budget (default 2000)\n"
		"  --workers N     parallel_transform workers (0 = serial script loop; default engine)\n"
		"  --dump-frame N  smoke: dump frame N per backend to gloom_frame_<backend>.txt\n"
		"  --bench N       run N frames autopilot with live rendering, report sim/draw split\n"
		"  --fps N         frame pacing target (default 30)\n"
		"  --w N / --h N   force console dimensions\n"
		"  --god           god mode\n"
		"  --pix MODE      sub-cell pixels: half (1x2) | quad (2x2, default) | sext (2x3,\n"
		"                  needs a Unicode-13 font e.g. Cascadia Mono in Windows Terminal)\n"
		"  --scripts DIR   override the scripts directory\n"
		"\nkeys: w/s move  a/d strafe  left/right turn  space/ctrl fire  e use\n"
		"      shift run  1/2/3 weapons  m automap  esc quit");
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
		else if (a == "--ticks") { opt.ticks = std::stoll(next_arg("--ticks")); }
		else if (a == "--workers") { opt.workers = std::stoll(next_arg("--workers")); }
		else if (a == "--dump-frame") { opt.dump_frame = std::stoll(next_arg("--dump-frame")); }
		else if (a == "--bench") { opt.bench = std::stoll(next_arg("--bench")); }
		else if (a == "--fps") { opt.target_fps = std::stod(next_arg("--fps")); }
		else if (a == "--w") { opt.width = std::stoll(next_arg("--w")); }
		else if (a == "--h") { opt.height = std::stoll(next_arg("--h")); }
		else if (a == "--god") { opt.god = true; }
		else if (a == "--scripts") { opt.scripts_dir = next_arg("--scripts"); }
		else if (a == "--pix") { opt.pix = next_arg("--pix"); }
		else if (a == "--help" || a == "-h") { print_usage(); return 0; }
		else { std::fprintf(stderr, "unknown flag: %s\n", a.c_str()); print_usage(); return 2; }
	}
	if (opt.backend == "interp") { opt.backend = "interpreter"; }
	if (opt.backend != "vm" && opt.backend != "interpreter") {
		std::fprintf(stderr, "unknown backend: %s\n", opt.backend.c_str());
		return 2;
	}
	if (opt.pix != "half" && opt.pix != "quad" && opt.pix != "sext") {
		std::fprintf(stderr, "unknown --pix mode: %s (half|quad|sext)\n", opt.pix.c_str());
		return 2;
	}

	std::string scripts_dir = locate_scripts_dir(opt.scripts_dir, argv[0]);
	console_host console;

	try {
		if (opt.smoke) { return run_smoke(scripts_dir, opt, console); }
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "jai_gloom: script error: %s\n", ex.what());
		return 1;
	}

	console.init();
	int w = 100, h = 40;
	console.size(w, h);
	if (opt.width > 0) { w = static_cast<int>(opt.width); }
	if (opt.height > 0) { h = static_cast<int>(opt.height); }

	const bool bench_pilot = opt.bench > 0;
	auto eng = make_gloom_engine(opt.backend, console, opt, !bench_pilot);
	int exit_code = 0;
	double sim_ms_sum = 0.0, draw_ms_sum = 0.0;
	int64_t bench_done = 0;
	try {
		eng->add_include_path(scripts_dir);
		eng->set_import_behavior(jai::engine::import_behavior::always);
		eng->execute("import \"main.jai\";");
		if (bench_pilot) { eng->execute("gloom_force_autopilot();"); }
		call_script(*eng, "gloom_boot", {
			jai::script_value(static_cast<int64_t>(w), eng.get()),
			jai::script_value(static_cast<int64_t>(h), eng.get()),
		});

		double t_prev = now_seconds();
		double fps_ema = opt.target_fps;
		double last_sim_ms = 0.0;
		for (;;) {
			double frame_start = now_seconds();
			double dt = frame_start - t_prev;
			t_prev = frame_start;
			if (dt > 0.1) { dt = 0.1; }
			if (dt > 0.0001) { fps_ema = fps_ema * 0.92 + (1.0 / dt) * 0.08; }

			std::string key = bench_pilot ? "" : console.poll_key();
			if (key == "esc") { break; }

			double sim_t0 = now_seconds();
			jai::script_value frame = call_script(*eng, "gloom_frame", {
				jai::script_value(dt, eng.get()),
				eng->make_value(key),
				jai::script_value(fps_ema, eng.get()),
				jai::script_value(last_sim_ms, eng.get()),
				jai::script_value(console.last_draw_ms, eng.get()),
			});
			std::string text = frame.to_string();
			last_sim_ms = (now_seconds() - sim_t0) * 1000.0;
			console.draw(text);

			if (bench_pilot) {
				sim_ms_sum += last_sim_ms;
				draw_ms_sum += console.last_draw_ms;
				if (++bench_done >= opt.bench) { break; }
			}
			if (call_script(*eng, "gloom_wants_quit", {}).as<bool>()) { break; }

			double budget = 1.0 / opt.target_fps;
			double used = now_seconds() - frame_start;
			if (used < budget) {
#ifdef _WIN32
				Sleep(static_cast<DWORD>((budget - used) * 1000.0));
#endif
			}
		}
	} catch (const std::exception& ex) {
		console.shutdown();
		std::fprintf(stderr, "jai_gloom: script error: %s\n", ex.what());
		std::string trace = eng->format_stack_trace();
		if (!trace.empty()) { std::fprintf(stderr, "%s\n", trace.c_str()); }
		return 1;
	}
	console.shutdown();
	if (bench_pilot && bench_done > 0) {
		double fr = static_cast<double>(bench_done);
		std::fprintf(stderr,
			"bench: %lld frames @ %dx%d | backend=%s workers=%lld\n"
			"  sim   %8.2f ms/frame  (script tick + frame string)\n"
			"  draw  %8.2f ms/frame  (console write)\n"
			"  total %8.2f ms/frame  (%.1f fps uncapped)\n",
			static_cast<long long>(bench_done), w, h, opt.backend.c_str(),
			static_cast<long long>(opt.workers),
			sim_ms_sum / fr, draw_ms_sum / fr,
			(sim_ms_sum + draw_ms_sum) / fr,
			1000.0 / ((sim_ms_sum + draw_ms_sum) / fr));
	}
	try { call_script(*eng, "gloom_summary", {}); } catch (...) {}
	return exit_code;
}
