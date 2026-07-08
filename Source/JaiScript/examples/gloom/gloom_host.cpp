// GLOOM shared host implementation: a deliberately thin console shell around a
// script_adapter. GLOOM is a real-time 2.5D raycast shooter; everything that
// makes it a game (raycaster, enemies, weapons, particles, HUD, episode) lives
// in each language's scripts. The host only provides: a VT console, held-key
// state + key events, a monotonic clock, the seeded rng class, command-line
// flags, and the harnesses:
//
//   --smoke runs N deterministic autopilot ticks on every adapter backend and
//   compares the sim STATE_HASH and the frame-stream hash (byte parity).
//   --bench runs the real loop on autopilot and reports the sim/draw split.

#include "gloom_host.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace gloom {

double now_seconds() {
	using clock = std::chrono::steady_clock;
	static const clock::time_point start = clock::now();
	return std::chrono::duration<double>(clock::now() - start).count();
}

uint64_t fnv1a(uint64_t hash, const std::string& bytes) {
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

// codepoint -> UTF-8 text (the sub-cell block glyphs live above most lexers'
// escape sets; U+1FB00 sextants are astral, so 4-byte sequences too)
std::string utf8_encode(int64_t codepoint) {
	std::string out;
	uint32_t c = static_cast<uint32_t>(codepoint);
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
}

std::string locate_scripts_dir(const std::string& override_dir, const char* argv0,
                               const char* source_scripts_dir, const char* local_dirname,
                               const char* entry_file) {
	namespace fs = std::filesystem;
	if (!override_dir.empty()) { return override_dir; }
	if (source_scripts_dir && *source_scripts_dir &&
	    fs::exists(fs::path(source_scripts_dir) / entry_file)) {
		return source_scripts_dir;
	}
	fs::path exe_dir = fs::absolute(fs::path(argv0)).parent_path();
	fs::path local = exe_dir / local_dirname;
	if (fs::exists(local / entry_file)) { return local.string(); }
	return local_dirname;
}

namespace {

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

host_services make_services(console_host& console, bool live_input) {
	host_services services;
	services.key_down = [&console, live_input](const std::string& name) -> bool {
		if (!live_input) { return false; }
		return console.key_down(name);
	};
	services.log = [](const std::string& line) {
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	};
	return services;
}

int run_smoke(script_adapter& adapter, const host_options& opt, console_host& console) {
	const int w = 100, h = 40;
	const double dt = 1.0 / 30.0;
	const std::vector<std::string> names = adapter.backends();
	std::printf("%s --smoke: %lld ticks @ %dx%d, seed %lld, fixed dt %.5f, workers %lld\n\n",
		adapter.program_name().c_str(),
		static_cast<long long>(opt.ticks), w, h, static_cast<long long>(opt.seed),
		dt, static_cast<long long>(opt.workers));
	std::printf("%-12s | %10s | %9s | %-18s | %s\n",
		"backend", "total ms", "ms/tick", "frame hash", "STATE_HASH");
	std::printf("-------------+------------+-----------+--------------------+------------\n");

	std::vector<uint64_t> frame_hashes(names.size(), 0);
	std::vector<int64_t> state_hashes(names.size(), 0);
	std::vector<double> per_tick(names.size(), 0.0);
	for (size_t b = 0; b < names.size(); ++b) {
		host_services services = make_services(console, false);
		auto session = adapter.make_session(names[b], services, opt, false);
		session->load_scripts();
		session->boot(w, h);

		uint64_t hash = 14695981039346656037ull;
		double t0 = now_seconds();
		for (int64_t i = 0; i < opt.ticks; ++i) {
			std::string text;
			try {
				text = session->frame(dt, "", 0.0, 0.0, 0.0);
			} catch (const std::exception& ex) {
				std::string trace = session->stack_trace();
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
		state_hashes[b] = session->state_hash();
		std::printf("%-12s | %10.1f | %9.3f | %016llx   | %lld\n",
			names[b].c_str(), total_ms, per_tick[b],
			static_cast<unsigned long long>(hash),
			static_cast<long long>(state_hashes[b]));
		session->summary();
	}

	bool state_ok = true, frame_ok = true;
	for (size_t b = 1; b < names.size(); ++b) {
		state_ok = state_ok && state_hashes[b] == state_hashes[0];
		frame_ok = frame_ok && frame_hashes[b] == frame_hashes[0];
	}
	std::printf("\nstate parity: %s | frame parity: %s\n",
		state_ok ? "OK" : "MISMATCH", frame_ok ? "OK (byte-identical)" : "MISMATCH");
	if (names.size() == 2 && state_ok && frame_ok && per_tick[1] > 0.0) {
		std::printf("%s speedup over %s: %.2fx\n",
			names[1].c_str(), names[0].c_str(), per_tick[0] / per_tick[1]);
	}
	return (state_ok && frame_ok) ? 0 : 1;
}

void print_usage(script_adapter& adapter) {
	std::string backends_line = "  --backend B     ";
	const std::vector<std::string> names = adapter.backends();
	for (size_t b = 0; b < names.size(); ++b) {
		if (b) { backends_line += " | "; }
		backends_line += names[b];
	}
	backends_line += " (default " + adapter.default_backend() + ")";
	std::printf(
		"%s\n"
		"  --seed N        run seed (default 666)\n"
		"%s\n"
		"  --smoke         headless deterministic autoplay on every backend; parity check\n"
		"  --ticks N       smoke tick budget (default 2000)\n"
		"  --workers N     parallel workers (0 = serial loop; default engine; ports: serial)\n"
		"  --dump-frame N  smoke: dump frame N per backend to gloom_frame_<backend>.txt\n"
		"  --bench N       run N frames autopilot with live rendering, report sim/draw split\n"
		"  --fps N         frame pacing target (default 30)\n"
		"  --w N / --h N   force console dimensions\n"
		"  --god           god mode\n"
		"  --pix MODE      sub-cell pixels: half (1x2) | quad (2x2, default) | sext (2x3,\n"
		"                  needs a Unicode-13 font e.g. Cascadia Mono in Windows Terminal)\n"
		"  --scripts DIR   override the scripts directory\n"
		"\nkeys: w/s move  a/d strafe  left/right turn  space/ctrl fire  e use\n"
		"      shift run  1/2/3 weapons  m automap  esc quit\n",
		adapter.title().c_str(), backends_line.c_str());
}

} // namespace

int run_gloom(int argc, char** argv, script_adapter& adapter) {
	host_options opt;
	opt.argv0 = argc > 0 ? argv[0] : "";
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
		else if (a == "--help" || a == "-h") { print_usage(adapter); return 0; }
		else { std::fprintf(stderr, "unknown flag: %s\n", a.c_str()); print_usage(adapter); return 2; }
	}
	if (opt.backend.empty()) { opt.backend = adapter.default_backend(); }
	opt.backend = adapter.normalize_backend(opt.backend);
	{
		const std::vector<std::string> names = adapter.backends();
		bool known = false;
		for (const std::string& n : names) { known = known || n == opt.backend; }
		if (!known) {
			std::fprintf(stderr, "unknown backend: %s\n", opt.backend.c_str());
			return 2;
		}
	}
	if (opt.pix != "half" && opt.pix != "quad" && opt.pix != "sext") {
		std::fprintf(stderr, "unknown --pix mode: %s (half|quad|sext)\n", opt.pix.c_str());
		return 2;
	}

	console_host console;
	const std::string prog = adapter.program_name();

	try {
		if (opt.smoke) { return run_smoke(adapter, opt, console); }
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "%s: script error: %s\n", prog.c_str(), ex.what());
		return 1;
	}

	console.init();
	int w = 100, h = 40;
	console.size(w, h);
	if (opt.width > 0) { w = static_cast<int>(opt.width); }
	if (opt.height > 0) { h = static_cast<int>(opt.height); }

	const bool bench_pilot = opt.bench > 0;
	host_services services = make_services(console, !bench_pilot);
	auto session = adapter.make_session(opt.backend, services, opt, !bench_pilot);
	int exit_code = 0;
	double sim_ms_sum = 0.0, draw_ms_sum = 0.0;
	int64_t bench_done = 0;
	try {
		session->load_scripts();
		if (bench_pilot) { session->force_autopilot(); }
		session->boot(w, h);

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
			std::string text = session->frame(dt, key, fps_ema, last_sim_ms, console.last_draw_ms);
			last_sim_ms = (now_seconds() - sim_t0) * 1000.0;
			console.draw(text);

			if (bench_pilot) {
				sim_ms_sum += last_sim_ms;
				draw_ms_sum += console.last_draw_ms;
				if (++bench_done >= opt.bench) { break; }
			}
			if (session->wants_quit()) { break; }

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
		std::fprintf(stderr, "%s: script error: %s\n", prog.c_str(), ex.what());
		std::string trace = session->stack_trace();
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
	try { session->summary(); } catch (...) {}
	return exit_code;
}

} // namespace gloom
