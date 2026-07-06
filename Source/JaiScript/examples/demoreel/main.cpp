// jai_demoreel host: a deliberately thin console shell around TWO JaiScript engines
// (one tree-walking interpreter, one bytecode VM) running the same scene scripts.
// Every visual effect lives in scenes/*.jai. The host only provides: a VT console,
// a monotonic clock, non-blocking keys, a seeded rng class, and the frame pump.
//
//   TAB  swaps the live backend mid-run (scene state re-synced on the other engine)
//   r    hot-reloads every scene .jai from disk into BOTH engines
//   --smoke runs N deterministic frames on both backends and compares frame hashes.

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
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace {

struct host_options {
	int64_t seed = 20260705;
	std::string backend = "vm";          // initially active backend
	bool smoke = false;
	int64_t frames = 1300;               // smoke frame budget (covers all 10 scene entries)
	bool precompiled = false;            // jaibite save/load boot path
	std::string jaib_path = "demoreel.jaib";
	int64_t capture = -1;                // scene index to text-capture (README material)
	bool reload_test = false;            // headless hot-reload + backend-swap exercise
	int64_t width = 0;                   // 0 = autodetect
	int64_t height = 0;
	double target_fps = 30.0;
	std::string scenes_dir;
	int64_t bench = 0;                   // run N frames of the real loop, report sim/write split
	int64_t scene = -1;                  // jump here after boot (bench targeting)
	std::string bench_out;               // also write the bench report to this file
	int64_t filter_mode = -1;            // -1 auto, 0 off, 1 truecolor+tolerance, 2 xterm-256
	int64_t tol = 8;                     // per-channel merge tolerance for mode 1
	bool diff = false;                   // row-diff redraw (skip unchanged rows)
};

// Load order matters only in that base classes execute before derived ones.
// main.jai is last and is NEVER hot-reloaded (it owns the persistent globals).
const char* kSceneFiles[] = {
	"util.jai", "reel.jai", "plasma.jai", "starfield.jai", "donut.jai",
	"julia.jai", "pipes.jai", "fire.jai", "sand.jai", "boids.jai",
	"kinstein.jai", "finale.jai", "main.jai",
};

double now_seconds() {
	using clock = std::chrono::steady_clock;
	static const clock::time_point start = clock::now();
	return std::chrono::duration<double>(clock::now() - start).count();
}

// ---- VT output filter: trims escape bytes host-side so every scene benefits.
// The scripts already emit a color escape only when the palette INDEX changes;
// this pass merges cells whose COLORS are indistinguishable. A sequence is only
// dropped when reapplying it would leave terminal state unchanged (SGR is
// idempotent), so mode 1 is exact within `tol` per channel and mode 2 is exact
// within the xterm-256 cube quanta.

struct sgr_color {
	int kind = 0;   // 0 unknown, 1 default, 2 rgb, 3 indexed
	int r = 0, g = 0, b = 0, idx = 0;

	bool matches(const sgr_color& o, int tol) const {
		if (kind != o.kind) { return false; }
		switch (kind) {
			case 1: return true;
			case 2: return std::abs(r - o.r) <= tol && std::abs(g - o.g) <= tol && std::abs(b - o.b) <= tol;
			case 3: return idx == o.idx;
			default: return false;
		}
	}
};

struct sgr_state {
	sgr_color fg, bg;
};

int cube216_axis(int v) {
	if (v < 48) { return 0; }
	if (v < 115) { return 1; }
	return (v - 35) / 40;
}

int cube216_index(int r, int g, int b) {
	return 16 + 36 * cube216_axis(r) + 6 * cube216_axis(g) + cube216_axis(b);
}

// One pass over `in`. Consecutive SGR sequences form a pending group applied to
// `target`; the next glyph settles it: dropped if the live state already matches,
// emitted otherwise (rewritten to 38;5/48;5 in mode 2). Non-SGR escapes pass
// through untouched (they don't affect colors). mode 0 is byte-exact passthrough.
std::string filter_vt(const std::string& in, sgr_state& st, int mode, int tol) {
	if (mode == 0) { return in; }
	std::string out;
	out.reserve(in.size());
	std::string pending;
	sgr_state target = st;
	const int effective_tol = (mode == 2) ? 0 : tol;

	auto flush = [&]() {
		if (pending.empty()) { return; }
		if (target.fg.matches(st.fg, effective_tol) && target.bg.matches(st.bg, effective_tol)) {
			target = st;    // dropped: rewind to the live state
		} else {
			out += pending;
			st = target;
		}
		pending.clear();
	};

	const size_t n = in.size();
	size_t i = 0;
	while (i < n) {
		if (in[i] == '\x1b' && i + 1 < n && in[i + 1] == '[') {
			size_t j = i + 2;
			while (j < n && !(in[j] >= '@' && in[j] <= '~')) { ++j; }
			if (j >= n) { flush(); out.append(in, i, n - i); break; }
			if (in[j] != 'm') {
				flush();
				out.append(in, i, j - i + 1);
				i = j + 1;
				continue;
			}
			int params[16];
			int np = 0;
			bool too_many = false;
			int v = 0;
			for (size_t k = i + 2; k <= j; ++k) {
				char c = in[k];
				if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); }
				else { if (np < 16) { params[np++] = v; } else { too_many = true; } v = 0; }
			}
			std::string rewritten = "\x1b[";
			bool first = true;
			auto put = [&](int pv) {
				if (!first) { rewritten += ';'; }
				rewritten += std::to_string(pv);
				first = false;
			};
			bool opaque = too_many;
			int p = 0;
			while (p < np) {
				int c = params[p];
				if (c == 0) { target.fg = {1}; target.bg = {1}; put(0); ++p; }
				else if ((c == 38 || c == 48) && p + 4 < np && params[p + 1] == 2) {
					sgr_color col;
					if (mode == 2) {
						col.kind = 3;
						col.idx = cube216_index(params[p + 2], params[p + 3], params[p + 4]);
						put(c); put(5); put(col.idx);
					} else {
						col.kind = 2;
						col.r = params[p + 2]; col.g = params[p + 3]; col.b = params[p + 4];
						put(c); put(2); put(col.r); put(col.g); put(col.b);
					}
					(c == 38 ? target.fg : target.bg) = col;
					p += 5;
				}
				else if ((c == 38 || c == 48) && p + 2 < np && params[p + 1] == 5) {
					(c == 38 ? target.fg : target.bg) = {3, 0, 0, 0, params[p + 2]};
					put(c); put(5); put(params[p + 2]);
					p += 3;
				}
				else if ((c >= 30 && c <= 37) || (c >= 90 && c <= 97)) { target.fg = {3, 0, 0, 0, 1000 + c}; put(c); ++p; }
				else if (c == 39) { target.fg = {1}; put(c); ++p; }
				else if ((c >= 40 && c <= 47) || (c >= 100 && c <= 107)) { target.bg = {3, 0, 0, 0, 1000 + c}; put(c); ++p; }
				else if (c == 49) { target.bg = {1}; put(c); ++p; }
				else { opaque = true; put(c); ++p; }
			}
			rewritten += 'm';
			if (opaque) { target.fg.kind = 0; target.bg.kind = 0; }   // never dedupe what we don't model
			pending += rewritten;
			i = j + 1;
			continue;
		}
		flush();
		out += in[i++];
	}
	flush();
	return out;
}

struct console_host {
	bool active = false;
	bool is_console = false;   // stdout is a real console (not redirected)
	bool is_wt = false;        // Windows Terminal (fast VT) vs legacy conhost
	int mode = 1;              // filter mode (see filter_vt)
	int tol = 8;
	bool diff = false;         // row-diff redraw
	std::vector<std::string> prev_rows;
	uint64_t frames_drawn = 0;
	uint64_t bytes_raw = 0;
	uint64_t bytes_emitted = 0;
	double last_draw_ms = 0.0;
#ifdef _WIN32
	HANDLE hout = INVALID_HANDLE_VALUE;
#endif

	void init() {
#ifdef _WIN32
		hout = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode_flags = 0;
		if (hout != INVALID_HANDLE_VALUE && GetConsoleMode(hout, &mode_flags)) {
			is_console = true;
			SetConsoleMode(hout, mode_flags | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
		}
		SetConsoleOutputCP(CP_UTF8);
		timeBeginPeriod(1);
#endif
		is_wt = std::getenv("WT_SESSION") != nullptr;
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

	// single buffered write per frame; on a real console go straight to
	// WriteConsoleA (skips CRT stream locking/chunking)
	void write_out(const std::string& s) {
		bytes_emitted += s.size();
#ifdef _WIN32
		if (is_console) {
			DWORD written = 0;
			WriteConsoleA(hout, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
			return;
		}
#endif
		std::fwrite(s.data(), 1, s.size(), stdout);
		std::fflush(stdout);
	}

	void draw(const std::string& frame) {
		double t0 = now_seconds();
		++frames_drawn;
		bytes_raw += frame.size();
		std::string out;
		out.reserve(frame.size() + 16);
		if (!diff) {
			sgr_state st;
			out += "\x1b[H";
			out += filter_vt(frame, st, mode, tol);
			out += "\x1b[0m";
		} else {
			// rows are self-contained (first cell always a full escape, RESET at the
			// end), so unchanged rows can be skipped. Filtering runs per row with
			// fresh state to keep the cached rows byte-comparable.
			std::vector<std::string> rows;
			size_t start = 0;
			for (;;) {
				size_t nl = frame.find('\n', start);
				sgr_state st;
				rows.push_back(filter_vt(
					frame.substr(start, nl == std::string::npos ? std::string::npos : nl - start),
					st, mode, tol));
				if (nl == std::string::npos) { break; }
				start = nl + 1;
			}
			bool full = rows.size() != prev_rows.size();
			for (size_t y = 0; y < rows.size(); ++y) {
				if (!full && rows[y] == prev_rows[y]) { continue; }
				out += "\x1b[" + std::to_string(y + 1) + ";1H";
				out += rows[y];
			}
			out += "\x1b[0m";
			prev_rows = std::move(rows);
		}
		write_out(out);
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
		if (w > 110) { w = 110; }
		if (h > 44) { h = 44; }
		if (w < 60) { w = 60; }
		if (h < 24) { h = 24; }
	}

	// "" when no key pending; letters lowercased; specials by name.
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

// Deterministic xorshift64* — the only randomness the scenes may touch, so the
// reel replays identically from a seed on both backends (that is the parity test).
struct demo_rng {
	uint64_t s;

	explicit demo_rng(jai::script_int seed)
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
};

std::string slurp_file(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) { return ""; }
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string locate_scenes_dir(const std::string& override_dir, const char* argv0) {
	namespace fs = std::filesystem;
	if (!override_dir.empty()) { return override_dir; }
#ifdef JAI_DEMOREEL_SOURCE_SCENES_DIR
	if (fs::exists(fs::path(JAI_DEMOREEL_SOURCE_SCENES_DIR) / "main.jai")) {
		return JAI_DEMOREEL_SOURCE_SCENES_DIR;
	}
#endif
	fs::path exe_dir = fs::absolute(fs::path(argv0)).parent_path();
	fs::path local = exe_dir / "demoreel_scenes";
	if (fs::exists(local / "main.jai")) { return local.string(); }
	return "scenes";
}

uint64_t fnv1a(uint64_t hash, const std::string& bytes) {
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

std::string strip_ansi(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
			i += 2;
			while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) { ++i; }
			if (i < s.size()) { ++i; }
			continue;
		}
		out += s[i++];
	}
	return out;
}

struct backend_slot {
	std::shared_ptr<jai::engine> eng;
	std::string name;                 // "vm" | "interpreter"
	double last_ms = 0.0;             // last measured demo_frame cost
	double boot_ms = 0.0;             // parse (or .jaib load) time
	std::string boot_kind;            // "parsed" | "loaded .jaib"
};

std::shared_ptr<jai::engine> make_reel_engine(const std::string& backend) {
	auto eng = jai::engine::make();
	eng->set_backend(backend == "vm" ? jai::backend_type::vm : jai::backend_type::interpreter);
	eng->execution_budget(0.0);
	jai::stdlib::register_all(eng);

	jai::dynamic_binder<demo_rng>(*eng, "Rng")
		.constructor<jai::script_int>()
		.method("next", &demo_rng::next)
		.method("roll", &demo_rng::roll)
		.method("chance", &demo_rng::chance)
		.method("nextf", &demo_rng::nextf)
		.build();

	eng->add_function("host_log", [](const std::string& line) {
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	});
	// float->int casts; script-side typed locals don't convert (see README bug list)
	eng->add_function("itrunc", [](jai::script_float v) -> jai::script_int {
		return static_cast<jai::script_int>(v);
	});
	eng->add_function("ifloor", [](jai::script_float v) -> jai::script_int {
		return static_cast<jai::script_int>(std::floor(v));
	});
	eng->add_global("ESC", eng->make_value(std::string("\x1b")));
	return eng;
}

// Parse-once boot: the concatenated scene source goes through engine->jaibite()
// (or a saved .jaib in --precompiled mode), then executes to define everything.
void boot_scripts(backend_slot& slot, const std::string& source, const host_options& opt) {
	namespace fs = std::filesystem;
	double t0 = now_seconds();
	jai::jaibite bite;
	if (opt.precompiled && fs::exists(opt.jaib_path)) {
		bite = slot.eng->jaibite_load(opt.jaib_path);
		slot.boot_kind = "loaded .jaib";
	} else {
		bite = slot.eng->jaibite(source);
		slot.boot_kind = "parsed";
		if (opt.precompiled) { bite.save(opt.jaib_path); }
	}
	slot.boot_ms = (now_seconds() - t0) * 1000.0;
	if (std::getenv("JAI_DEMOREEL_NO_JAIBITE")) { slot.eng->execute(source); return; }
	bite.execute();
}

jai::script_value call_script(backend_slot& slot, const char* fn_name,
                              const std::vector<jai::script_value>& args) {
	jai::script_value fn = slot.eng->get_variable(fn_name);
	auto result = fn.as_function()(args);
	if (!result.has_value()) {
		throw std::runtime_error(std::string(fn_name) + ": " + std::string(result.message()));
	}
	return result.value();
}

void boot_reel(backend_slot& slot, int w, int h, const host_options& opt, bool hud_on) {
	slot.eng->execute("demo_boot(" + std::to_string(w) + ", " + std::to_string(h) + ", " +
		std::to_string(opt.seed) + ", \"" + slot.name + "\", " +
		(opt.smoke ? "true" : "false") + ", " + (hud_on ? "true" : "false") + ");");
}

std::string run_frame(backend_slot& slot, double t, double dt, const std::string& key,
                      double fps, double draw_ms, double ms_other) {
	auto* e = slot.eng.get();
	double t0 = now_seconds();
	jai::script_value frame = call_script(slot, "demo_frame", {
		jai::script_value(t, e),
		jai::script_value(dt, e),
		e->make_value(key),
		jai::script_value(fps, e),
		jai::script_value(slot.last_ms, e),
		jai::script_value(draw_ms, e),
		jai::script_value(ms_other, e),
	});
	std::string text = frame.to_string();
	slot.last_ms = (now_seconds() - t0) * 1000.0;
	return text;
}

int run_smoke(const std::string& source, const host_options& opt) {
	const int w = 100, h = 40;
	const double dt = 1.0 / 30.0;
	std::printf("jai_demoreel --smoke: %lld frames @ %dx%d, seed %lld, fixed dt %.5f\n\n",
		static_cast<long long>(opt.frames), w, h, static_cast<long long>(opt.seed), dt);
	std::printf("%-12s | %-14s | %10s | %9s | %12s | %s\n",
		"backend", "boot", "total ms", "ms/frame", "cells/sec", "frame hash");
	std::printf("-------------+----------------+------------+-----------+--------------+-------------------\n");

	uint64_t hashes[2] = {0, 0};
	double per_frame[2] = {0, 0};
	const char* names[2] = {"interpreter", "vm"};
	for (int b = 0; b < 2; ++b) {
		backend_slot slot;
		slot.name = names[b];
		slot.eng = make_reel_engine(slot.name);
		try {
			boot_scripts(slot, source, opt);
		} catch (const std::exception& ex) {
			throw std::runtime_error(std::string("boot_scripts[") + slot.name + "]: " + ex.what());
		}
		try {
			host_options boot_opt = opt;
			if (std::getenv("JAI_DEMOREEL_SMOKE_FULLDUR")) { boot_opt.smoke = false; }
			boot_reel(slot, w, h, boot_opt, false);
		} catch (const std::exception& ex) {
			throw std::runtime_error(std::string("boot_reel[") + slot.name + "]: " + ex.what());
		}

		uint64_t hash = 14695981039346656037ull;
		const bool trace = std::getenv("JAI_DEMOREEL_TRACE") != nullptr;
		const char* dump_env = std::getenv("JAI_DEMOREEL_DUMP_FRAME");
		int64_t dump_frame = dump_env ? std::stoll(dump_env) : -1;
		double t0 = now_seconds();
		for (int64_t i = 0; i < opt.frames; ++i) {
			if (trace) { std::fprintf(stderr, "[%s f%lld]\n", slot.name.c_str(), static_cast<long long>(i)); std::fflush(stderr); }
			std::string frame;
			try {
				frame = run_frame(slot, i * dt, dt, "", 0.0, 0.0, 0.0);
			} catch (const std::exception& ex) {
				throw std::runtime_error(std::string("frame ") + std::to_string(i) + "[" + slot.name + "]: " + ex.what());
			}
			if (i == dump_frame) {
				std::ofstream out("demoreel_frame_" + slot.name + ".txt", std::ios::binary | std::ios::trunc);
				out.write(frame.data(), static_cast<std::streamsize>(frame.size()));
			}
			hash = fnv1a(hash, frame);
		}
		double total_ms = (now_seconds() - t0) * 1000.0;
		per_frame[b] = total_ms / opt.frames;
		hashes[b] = hash;
		double cells_per_sec = (static_cast<double>(w) * h * opt.frames) / (total_ms / 1000.0);
		std::printf("%-12s | %7.1f ms %-4s | %10.1f | %9.3f | %12.0f | %016llx\n",
			slot.name.c_str(), slot.boot_ms, slot.boot_kind == "parsed" ? "prse" : "jaib",
			total_ms, per_frame[b], cells_per_sec,
			static_cast<unsigned long long>(hash));
	}

	bool match = hashes[0] == hashes[1];
	std::printf("\nbackend parity: %s", match ? "OK (frame streams byte-identical)\n" : "MISMATCH\n");
	if (match && per_frame[0] > 0.0) {
		std::printf("vm speedup over interpreter: %.2fx\n", per_frame[0] / per_frame[1]);
	}
	return match ? 0 : 1;
}

int run_capture(const std::string& source, const host_options& opt) {
	const int w = 100, h = 40;
	const double dt = 1.0 / 30.0;
	backend_slot slot;
	slot.name = "vm";
	slot.eng = make_reel_engine(slot.name);
	boot_scripts(slot, source, opt);
	boot_reel(slot, w, h, opt, false);
	auto* e = slot.eng.get();
	const char* cap_t_env = std::getenv("JAI_DEMOREEL_CAP_T");
	double cap_t = cap_t_env ? std::stod(cap_t_env) : 5.0;
	call_script(slot, "demo_jump", {
		jai::script_value(opt.capture, e),
		jai::script_value(cap_t, e),
	});
	std::string frame;
	for (int i = 0; i < 75; ++i) {
		frame = run_frame(slot, cap_t + i * dt, dt, "", 0.0, 0.0, 0.0);
	}
	std::string plain = strip_ansi(frame);
	std::fwrite(plain.data(), 1, plain.size(), stdout);
	std::fputc('\n', stdout);
	return 0;
}

// Headless exercise of the two interactive tricks: 'r' (hot reload of every
// scene file into both engines) and TAB (state sync onto the other backend).
int run_reload_test(const std::string& source, const std::string& dir, const host_options& opt) {
	const int w = 100, h = 40;
	const double dt = 1.0 / 30.0;
	backend_slot slots[2];
	slots[0].name = "interpreter";
	slots[1].name = "vm";
	for (auto& slot : slots) {
		slot.eng = make_reel_engine(slot.name);
		boot_scripts(slot, source, opt);
		boot_reel(slot, w, h, opt, false);
	}
	for (int i = 0; i < 40; ++i) {
		for (auto& slot : slots) { run_frame(slot, i * dt, dt, "", 0.0, 0.0, 0.0); }
	}
	// live-coding path: re-execute every scene file (main.jai excluded), then demo_reload
	for (const char* f : kSceneFiles) {
		if (std::string(f) == "main.jai") { continue; }
		std::string body = slurp_file(dir + "/" + f);
		if (body.empty()) { return 1; }
		for (auto& slot : slots) { slot.eng->execute(body); }
	}
	for (auto& slot : slots) { call_script(slot, "demo_reload", {}); }
	for (int i = 40; i < 80; ++i) {
		for (auto& slot : slots) { run_frame(slot, i * dt, dt, "", 0.0, 0.0, 0.0); }
	}
	std::puts("reload-test: hot reload OK on both backends");
	// TAB path: pull scene state from A, jump B onto it, keep running B
	jai::script_int idx = call_script(slots[0], "demo_scene_index", {}).as_int();
	double st = call_script(slots[0], "demo_scene_time", {}).as<double>();
	auto* e = slots[1].eng.get();
	call_script(slots[1], "demo_jump", {
		jai::script_value(idx, e),
		jai::script_value(st, e),
	});
	for (int i = 80; i < 110; ++i) { run_frame(slots[1], i * dt, dt, "", 0.0, 0.0, 0.0); }
	std::printf("reload-test: backend swap sync OK (scene %lld t=%.2f)\n",
		static_cast<long long>(idx), st);
	return 0;
}

void print_usage() {
	std::puts(
		"jai_demoreel - a demoscene tech reel written in JaiScript\n"
		"  --backend B      initially active backend: vm | interpreter (default vm)\n"
		"  --seed N         reel seed (default 20260705)\n"
		"  --fps N          frame pacing target (default 30)\n"
		"  --w N / --h N    force console dimensions\n"
		"  --smoke          headless determinism + perf run on BOTH backends\n"
		"  --frames N       smoke frame budget (default 1300)\n"
		"  --capture I      print scene I as a plain-text frame and exit\n"
		"  --reload-test    headless hot-reload + backend-swap self-test\n"
		"  --precompiled    boot from demoreel.jaib (saved on first run)\n"
		"  --jaib PATH      .jaib path for --precompiled\n"
		"  --scenes DIR     override the scenes directory\n"
		"  --bench N        run N frames of the real loop, report sim/write split, exit\n"
		"  --scene I        jump to scene I after boot (bench targeting)\n"
		"  --bench-out P    also write the bench report to file P\n"
		"  --truecolor      24-bit output with tolerance run-merging (default off conhost)\n"
		"  --256color       xterm-256 output (short escapes, long runs; conhost default)\n"
		"  --no-filter      byte-exact legacy output (no escape trimming)\n"
		"  --tol N          per-channel merge tolerance for --truecolor (default 8)\n"
		"  --diff           row-diff redraw: skip rows unchanged since last frame\n"
		"\nkeys: TAB backend swap | r hot reload | h HUD | f freeze | n/p/space scene skip |\n"
		"      1-9,0 jump to scene | q/ESC quit");
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
		else if (a == "--frames") { opt.frames = std::stoll(next_arg("--frames")); }
		else if (a == "--capture") { opt.capture = std::stoll(next_arg("--capture")); }
		else if (a == "--reload-test") { opt.reload_test = true; }
		else if (a == "--precompiled") { opt.precompiled = true; }
		else if (a == "--jaib") { opt.jaib_path = next_arg("--jaib"); }
		else if (a == "--scenes") { opt.scenes_dir = next_arg("--scenes"); }
		else if (a == "--fps") { opt.target_fps = std::stod(next_arg("--fps")); }
		else if (a == "--w") { opt.width = std::stoll(next_arg("--w")); }
		else if (a == "--h") { opt.height = std::stoll(next_arg("--h")); }
		else if (a == "--bench") { opt.bench = std::stoll(next_arg("--bench")); }
		else if (a == "--scene") { opt.scene = std::stoll(next_arg("--scene")); }
		else if (a == "--bench-out") { opt.bench_out = next_arg("--bench-out"); }
		else if (a == "--truecolor") { opt.filter_mode = 1; }
		else if (a == "--256color") { opt.filter_mode = 2; }
		else if (a == "--no-filter") { opt.filter_mode = 0; }
		else if (a == "--tol") { opt.tol = std::stoll(next_arg("--tol")); }
		else if (a == "--diff") { opt.diff = true; }
		else if (a == "--dev") { /* hot reload is always on; accepted for muscle memory */ }
		else if (a == "--help" || a == "-h") { print_usage(); return 0; }
		else { std::fprintf(stderr, "unknown flag: %s\n", a.c_str()); print_usage(); return 2; }
	}
	if (opt.backend != "vm" && opt.backend != "interpreter" && opt.backend != "interp") {
		std::fprintf(stderr, "unknown backend: %s\n", opt.backend.c_str());
		return 2;
	}
	if (opt.backend == "interp") { opt.backend = "interpreter"; }

	std::string dir = locate_scenes_dir(opt.scenes_dir, argv[0]);
	std::string source;
	for (const char* f : kSceneFiles) {
		std::string body = slurp_file(dir + "/" + f);
		if (body.empty()) {
			std::fprintf(stderr, "missing scene script: %s/%s\n", dir.c_str(), f);
			return 2;
		}
		source += body;
		source += "\n";
	}

	try {
		if (opt.smoke) { return run_smoke(source, opt); }
		if (opt.capture >= 0) { return run_capture(source, opt); }
		if (opt.reload_test) { return run_reload_test(source, dir, opt); }
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "jai_demoreel: script error: %s\n", ex.what());
		return 1;
	}

	console_host console;
	console.init();
	// legacy conhost parses VT slowly; default it to the cheap escape stream
	const bool legacy_conhost = console.is_console && !console.is_wt;
	console.mode = (opt.filter_mode >= 0) ? static_cast<int>(opt.filter_mode)
	                                      : (legacy_conhost ? 2 : 1);
	console.tol = static_cast<int>(opt.tol);
	console.diff = opt.diff;
	int w = 100, h = 40;
	console.size(w, h);
	if (opt.width > 0) { w = static_cast<int>(opt.width); }
	if (opt.height > 0) { h = static_cast<int>(opt.height); }

	backend_slot slots[2];
	slots[0].name = "interpreter";
	slots[1].name = "vm";
	int active = (opt.backend == "vm") ? 1 : 0;

	int exit_code = 0;
	double sim_ms_sum = 0.0, draw_ms_sum = 0.0;
	int64_t bench_done = 0;
	try {
		for (auto& slot : slots) {
			slot.eng = make_reel_engine(slot.name);
			boot_scripts(slot, source, opt);
			boot_reel(slot, w, h, opt, true);
		}
		if (opt.scene >= 0) {
			for (auto& slot : slots) {
				auto* e = slot.eng.get();
				call_script(slot, "demo_jump", {
					jai::script_value(opt.scene, e),
					jai::script_value(5.0, e),
				});
			}
		}

		double t_prev = now_seconds();
		double fps_ema = opt.target_fps;
		std::string host_notice;
		double notice_until = 0.0;
		if (legacy_conhost) {
			host_notice = " tip: Windows Terminal renders this reel much faster (conhost: 256-color mode on, --truecolor to override) ";
			notice_until = now_seconds() + 6.0;
		}

		for (;;) {
			double frame_start = now_seconds();
			double dt = frame_start - t_prev;
			t_prev = frame_start;
			if (dt > 0.1) { dt = 0.1; }
			if (dt > 0.0001) { fps_ema = fps_ema * 0.92 + (1.0 / dt) * 0.08; }

			std::string key = console.poll_key();
			if (key == "q" || key == "esc") { break; }
			if (key == "tab") {
				jai::script_int idx = call_script(slots[active], "demo_scene_index", {}).as_int();
				double st = call_script(slots[active], "demo_scene_time", {}).as<double>();
				active = 1 - active;
				auto* e = slots[active].eng.get();
				call_script(slots[active], "demo_jump", {
					jai::script_value(idx, e),
					jai::script_value(st, e),
				});
				key = "";
			} else if (key == "r") {
				// live coding: re-execute every scene file (not main.jai) on BOTH engines,
				// then rebuild the current scene so init-time changes show immediately.
				try {
					for (const char* f : kSceneFiles) {
						if (std::string(f) == "main.jai") { continue; }
						std::string body = slurp_file(dir + "/" + f);
						if (body.empty()) { continue; }
						for (auto& slot : slots) { slot.eng->execute(body); }
					}
					for (auto& slot : slots) { call_script(slot, "demo_reload", {}); }
					host_notice = " reloaded scenes/*.jai from disk ";
				} catch (const std::exception& ex) {
					host_notice = std::string(" reload error: ") + ex.what() + " ";
				}
				notice_until = now_seconds() + 2.5;
				key = "";
			}

			std::string frame = run_frame(slots[active], frame_start, dt, key,
				fps_ema, console.last_draw_ms, slots[1 - active].last_ms);
			console.draw(frame);
			if (opt.bench > 0) {
				sim_ms_sum += slots[active].last_ms;
				draw_ms_sum += console.last_draw_ms;
				if (++bench_done >= opt.bench) { break; }
			}
			if (now_seconds() < notice_until && !host_notice.empty()) {
				std::string banner = "\x1b[" + std::to_string(h) + ";1H\x1b[30;46m" +
					host_notice + "\x1b[0m";
				std::fwrite(banner.data(), 1, banner.size(), stdout);
				std::fflush(stdout);
			}

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
		std::fprintf(stderr, "jai_demoreel: script error: %s\n", ex.what());
		for (auto& slot : slots) {
			if (!slot.eng) { continue; }
			std::string trace = slot.eng->format_stack_trace();
			if (!trace.empty()) { std::fprintf(stderr, "[%s]\n%s\n", slot.name.c_str(), trace.c_str()); }
		}
		return 1;
	}
	console.shutdown();
	if (opt.bench > 0 && console.frames_drawn > 0) {
		double fr = static_cast<double>(console.frames_drawn);
		double sim = sim_ms_sum / fr, drw = draw_ms_sum / fr;
		char report[512];
		std::snprintf(report, sizeof(report),
			"bench: %llu frames, scene %lld | terminal=%s | mode=%d tol=%d diff=%s\n"
			"  sim   %8.2f ms/frame  (script update + frame string)\n"
			"  write %8.2f ms/frame  (filter + console write)\n"
			"  total %8.2f ms/frame  (%.1f fps uncapped)\n"
			"  bytes/frame %.0f raw -> %.0f written (%.2fx reduction)\n",
			static_cast<unsigned long long>(console.frames_drawn),
			static_cast<long long>(opt.scene),
			console.is_wt ? "windows-terminal" : (console.is_console ? "conhost" : "redirected"),
			console.mode, console.tol, console.diff ? "on" : "off",
			sim, drw, sim + drw, 1000.0 / (sim + drw),
			static_cast<double>(console.bytes_raw) / fr,
			static_cast<double>(console.bytes_emitted) / fr,
			static_cast<double>(console.bytes_raw) / static_cast<double>(console.bytes_emitted ? console.bytes_emitted : 1));
		std::fputs(report, stderr);
		if (!opt.bench_out.empty()) {
			std::ofstream f(opt.bench_out, std::ios::trunc);
			f << report;
		}
	}
	std::printf("jai_demoreel: boot %s %.1fms (%s) / %s %.1fms (%s); last frame %s %.2fms / %s %.2fms\n",
		slots[0].name.c_str(), slots[0].boot_ms, slots[0].boot_kind.c_str(),
		slots[1].name.c_str(), slots[1].boot_ms, slots[1].boot_kind.c_str(),
		slots[0].name.c_str(), slots[0].last_ms, slots[1].name.c_str(), slots[1].last_ms);
	return exit_code;
}
