// jaiscript — the standalone JaiScript runner/REPL, and the reference embedding.
// Three modes: file runner (`jaiscript file.jai`), interactive REPL (bare `jaiscript`),
// and live-coding watch (`jaiscript --watch file.jai`: edit in your editor, save, see
// output). The host is deliberately thin: engine + stdlib + file IO + a console; every
// engine call in here is the canonical shape from docs/site/embedding.

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/signals/signal_property.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/properties/property_serialization.hpp>   // property_manager::load template bodies
#ifdef JAISCRIPT_ENABLE_DEBUGGER
#include <jaiscript/debug/connector.hpp>
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#endif

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- console ---

std::atomic<bool> g_interrupted{false};  // Ctrl+C / Ctrl+Break (host-exe state, not engine state)

#ifdef _WIN32
std::map<std::string, std::string> g_wav_aliases;   // wav path -> its open MCI device
#endif

#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
	if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
		g_interrupted.store(true);
		return TRUE;   // swallow: blocked console reads fail, loops see the flag
	}
	return FALSE;
}
#endif

struct console_state {
	bool vt = false;          // VT escape processing available on stdout
	bool out_tty = false;     // stdout is a real console (not a pipe/file)
	bool in_tty = false;      // stdin is a real console
	bool solo_console = false;  // we own a fresh console window (drag-drop / double-click launch)

	void init() {
#ifdef _WIN32
		HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
			out_tty = true;
			vt = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
		}
		HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
		DWORD in_mode = 0;
		in_tty = in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &in_mode);
		SetConsoleOutputCP(CP_UTF8);
		SetConsoleCP(CP_UTF8);
		DWORD pids[4];
		solo_console = GetConsoleProcessList(pids, 4) == 1;
		SetConsoleCtrlHandler(ctrl_handler, TRUE);
		timeBeginPeriod(1);   // 1ms Sleep granularity for sleep_ms-paced game loops
#else
		out_tty = in_tty = vt = true;
#endif
	}

	bool colors() const { return vt && out_tty; }
	std::string red(const std::string& s) const { return colors() ? "\x1b[31m" + s + "\x1b[0m" : s; }
	std::string dim(const std::string& s) const { return colors() ? "\x1b[90m" + s + "\x1b[0m" : s; }
	std::string green(const std::string& s) const { return colors() ? "\x1b[32m" + s + "\x1b[0m" : s; }

	void clear_screen() const {
		if (!out_tty) { return; }
		std::fputs("\x1b[2J\x1b[H", stdout);
		std::fflush(stdout);
	}
};

// ---------------------------------------------------------------- options ---

struct options {
	std::string script;                    // empty = REPL
	std::string backend = "vm";
	double budget = 0.0;                   // seconds per execute; 0 = unlimited (runner default —
	                                       // the ENGINE default is 1.0s, deliberately overridden:
	                                       // long-running scripts are the user's business here)
	bool watch = false;
	bool time_runs = false;
	bool debug = true;                     // DAP listener on by default (localhost, free until attach)
	int debug_port = 0;                    // 0 = default base (see attach_debugger)
	int pause_mode = -1;                   // -1 auto (solo console), 0 never, 1 always
	bool selftest = false;
	std::vector<std::string> script_args;  // everything after `--`, exposed as global ARGS
};

void print_usage() {
	std::puts(
		"jaiscript - standalone JaiScript runner / REPL / live-coding watcher\n"
		"\n"
		"usage:\n"
		"  jaiscript                          interactive REPL (persistent engine; :help inside)\n"
		"  jaiscript file.jai [-- args...]    run a script and exit (drag-drop a .jai onto the exe\n"
		"                                     works too - the window pauses so you can read output)\n"
		"  jaiscript --watch file.jai         live-coding: runs the file, then re-runs it every time\n"
		"                                     you save - edit in your editor, save, see output.\n"
		"                                     Ctrl+C quits.\n"
		"\n"
		"options:\n"
		"  --backend=vm|interp   execution backend (default vm; interp = tree-walking interpreter)\n"
		"  --budget=N            execution budget in seconds per execute; overruns raise a script\n"
		"                        error (default 0 = unlimited - infinite loops are your business;\n"
		"                        the embedded-engine default of 1.0s is deliberately lifted here)\n"
		"  --time                print wall time for each run\n"
		"  --no-debug            do not stand up the DAP debug listener\n"
		"  --debug-port=N        first port the DAP listener tries (default 52477, probing +10)\n"
		"  --pause / --no-pause  force / suppress the \"press Enter to close\" hold after a file\n"
		"                        run (auto: pauses only when the console window is exclusively\n"
		"                        ours, i.e. drag-drop or double-click launch)\n"
		"  --selftest            run the built-in smoke tests and exit\n"
		"  --version             print the JaiScript version\n"
		"  --help                this text\n"
		"\n"
		"exit code: the script's result if it is an int, otherwise 0; script errors exit 1\n"
		"           (message + stack trace on stderr), usage errors exit 2.\n"
		"\n"
		"environment: the full JaiScript stdlib is always registered (print/format/to_string/\n"
		"  type_of, math, to_json/from_json, containers) plus host file IO: read_file(path),\n"
		"  write_file(path, text), file_exists(path), delete_file(path), and console-game IO:\n"
		"  key_down(name), read_key(), clock_ms(), sleep_ms(n), term_cols(), term_rows(),\n"
		"  play_wav(path) (async, one voice), play_midi(path)/stop_midi()/midi_status()\n"
		"  (MCI sequencer; audio is a no-op off Windows). POSIX terminals synthesize\n"
		"  key_down from auto-repeat; there are no key-up events. Networking: NetChannel\n"
		"  (new NetChannel(); listen(port)/connect(host,port)/send(bytes)/pump()/close();\n"
		"  signals on_packet(bytes)/on_connect()/on_disconnect(reason) fire during pump();\n"
		"  4-byte big-endian length framing, MV-compatible) and NetServer (N clients on one\n"
		"  port: listen/send(id,bytes)/broadcast/pump/kick/client_count; per-client signals\n"
		"  on_client_connect(id)/on_client_packet(id,bytes)/on_client_disconnect(id,reason)).\n"
		"  Args after `--` are the global ARGS (array of strings). include/import resolve\n"
		"  relative to the script's folder.\n"
		"\n"
		"debugging: a DAP listener is on by default (localhost only, costs nothing until a session\n"
		"  attaches). Attach from VS Code with the jaiscript extension (\"Attach to JaiScript\") on\n"
		"  the logged port. Works in file and REPL modes; --watch uses a fresh engine per run, so\n"
		"  the listener is off there.\n");
}

int parse_args(int argc, char** argv, options& opt, const console_state& con) {
	bool args_section = false;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (args_section) { opt.script_args.push_back(a); continue; }
		auto value_of = [&](const std::string& flag) -> std::string {
			if (a.size() > flag.size() && a[flag.size()] == '=') { return a.substr(flag.size() + 1); }
			if (i + 1 < argc) { return argv[++i]; }
			std::fprintf(stderr, "%s\n", con.red("missing value for " + flag).c_str());
			std::exit(2);
		};
		if (a == "--") { args_section = true; }
		else if (a == "--watch") { opt.watch = true; }
		else if (a == "--time") { opt.time_runs = true; }
		else if (a == "--no-debug") { opt.debug = false; }
		else if (a == "--pause") { opt.pause_mode = 1; }
		else if (a == "--no-pause") { opt.pause_mode = 0; }
		else if (a == "--selftest") { opt.selftest = true; }
		else if (a.rfind("--backend", 0) == 0) { opt.backend = value_of("--backend"); }
		else if (a.rfind("--budget", 0) == 0) { opt.budget = std::stod(value_of("--budget")); }
		else if (a.rfind("--debug-port", 0) == 0) { opt.debug_port = static_cast<int>(std::stol(value_of("--debug-port"))); }
		else if (a == "--version" || a == "-v") { std::printf("JaiScript %s\n", jai::version().c_str()); std::exit(0); }
		else if (a == "--help" || a == "-h" || a == "/?") { print_usage(); std::exit(0); }
		else if (!a.empty() && a[0] == '-') {
			std::fprintf(stderr, "%s\n", con.red("unknown flag: " + a).c_str());
			print_usage();
			return 2;
		}
		else if (opt.script.empty()) { opt.script = a; }
		else {
			std::fprintf(stderr, "%s\n", con.red("unexpected extra argument: " + a + " (script args go after --)").c_str());
			return 2;
		}
	}
	if (opt.backend == "interpreter") { opt.backend = "interp"; }
	if (opt.backend != "vm" && opt.backend != "interp") {
		std::fprintf(stderr, "%s\n", con.red("unknown backend: " + opt.backend + " (use vm or interp)").c_str());
		return 2;
	}
	if (opt.watch && opt.script.empty()) {
		std::fprintf(stderr, "%s\n", con.red("--watch needs a script file").c_str());
		return 2;
	}
	return -1;   // keep going
}

// --------------------------------------------------------- console game IO ---
// The four capabilities a real-time terminal script cannot express (input,
// clock, sleep, terminal size), same shapes as the crawler/gloom hosts.
// Windows-first; elsewhere the input pair reports nothing, like those hosts.

#ifdef _WIN32
int vk_for_key_name(const std::string& name) {
	if (name.size() == 1) {
		char c = name[0];
		if (c >= 'a' && c <= 'z') { return c - 'a' + 'A'; }
		if (c >= 'A' && c <= 'Z') { return c; }
		if (c >= '0' && c <= '9') { return c; }
	}
	if (name == "space") { return VK_SPACE; }
	if (name == "left") { return VK_LEFT; }
	if (name == "right") { return VK_RIGHT; }
	if (name == "up") { return VK_UP; }
	if (name == "down") { return VK_DOWN; }
	if (name == "shift") { return VK_SHIFT; }
	if (name == "ctrl") { return VK_CONTROL; }
	if (name == "alt") { return VK_MENU; }
	if (name == "esc") { return VK_ESCAPE; }
	if (name == "enter") { return VK_RETURN; }
	if (name == "tab") { return VK_TAB; }
	return 0;
}

std::string key_name_for_vk(WORD vk) {
	if (vk >= 'A' && vk <= 'Z') { return std::string(1, static_cast<char>(vk - 'A' + 'a')); }
	if (vk >= '0' && vk <= '9') { return std::string(1, static_cast<char>(vk)); }
	switch (vk) {
		case VK_LEFT: return "left";
		case VK_RIGHT: return "right";
		case VK_UP: return "up";
		case VK_DOWN: return "down";
		case VK_SPACE: return "space";
		case VK_SHIFT: return "shift";
		case VK_CONTROL: return "ctrl";
		case VK_MENU: return "alt";
		case VK_ESCAPE: return "esc";
		case VK_RETURN: return "enter";
		case VK_TAB: return "tab";
		case VK_BACK: return "backspace";
		default: return "";
	}
}

// ReadConsoleInput pump: real down/up transitions from the console event queue.
// Works under Windows Terminal/ConPTY, where GetConsoleWindow() is a hidden
// pseudo-console window and foreground tests always fail; focus gating is
// implicit (the terminal only forwards events while focused), and focus-loss
// clears the held table so keys cannot stick.
struct win_input_state {
	std::map<std::string, bool> held;
	std::vector<std::string> edges;
};
win_input_state g_win_input;

void win_pump_input() {
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &mode)) { return; }   // piped stdin: no game input
	DWORD avail = 0;
	while (GetNumberOfConsoleInputEvents(in, &avail) && avail > 0) {
		INPUT_RECORD records[32];
		DWORD got = 0;
		if (!ReadConsoleInput(in, records, 32, &got) || got == 0) { return; }
		for (DWORD i = 0; i < got; ++i) {
			if (records[i].EventType == FOCUS_EVENT && !records[i].Event.FocusEvent.bSetFocus) {
				g_win_input.held.clear();
			} else if (records[i].EventType == KEY_EVENT) {
				const KEY_EVENT_RECORD& k = records[i].Event.KeyEvent;
				std::string name = key_name_for_vk(k.wVirtualKeyCode);
				if (name.empty()) {
					// punctuation and numpad have no vk row: take the console's own
					// character translation ('.', ':', '-', shifted symbols, numpad digits)
					char translated = static_cast<char>(k.uChar.UnicodeChar);
					if (k.uChar.UnicodeChar > 32 && k.uChar.UnicodeChar < 127) {
						name = std::string(1, static_cast<char>(std::tolower(translated)));
					}
				}
				if (name.empty()) { continue; }
				g_win_input.held[name] = k.bKeyDown != FALSE;
				if (k.bKeyDown) {
					g_win_input.edges.push_back(name);
					if (g_win_input.edges.size() > 128) {           // key_down-only scripts must not hoard edges
						g_win_input.edges.erase(g_win_input.edges.begin());
					}
				}
			}
		}
	}
}
#else
// POSIX terminals deliver bytes, not key events: pump stdin in raw mode into
// (a) an edge queue for read_key and (b) a last-press table that auto-repeat
// keeps fresh — key_down = "pressed within the hold window". There are no
// key-up events and no modifier state (shift/ctrl/alt never read down here).
// Linux and macOS share this branch (identical termios/ioctl); macOS
// Terminal.app may send SS3 arrows (ESC O A) — both prefixes are parsed.
struct posix_input_state {
	bool raw = false;
	termios saved{};
	std::vector<std::string> edges;
	std::map<std::string, int64_t> last_press;
};
posix_input_state g_posix_input;
constexpr int64_t posix_hold_window_ms = 500;

int64_t posix_now_ms() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

void posix_restore_terminal() {
	if (g_posix_input.raw) {
		tcsetattr(STDIN_FILENO, TCSANOW, &g_posix_input.saved);
		g_posix_input.raw = false;
	}
}

void posix_signal_restore(int sig) {
	posix_restore_terminal();
	std::signal(sig, SIG_DFL);
	std::raise(sig);
}

void posix_enter_raw() {
	if (g_posix_input.raw || !isatty(STDIN_FILENO)) { return; }
	tcgetattr(STDIN_FILENO, &g_posix_input.saved);
	termios raw = g_posix_input.saved;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;    // polling reads return immediately
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	g_posix_input.raw = true;
	std::atexit(posix_restore_terminal);
	std::signal(SIGINT, posix_signal_restore);
	std::signal(SIGTERM, posix_signal_restore);
}

void posix_note_key(const std::string& key) {
	g_posix_input.edges.push_back(key);
	if (g_posix_input.edges.size() > 128) {              // key_down-only scripts must not hoard edges
		g_posix_input.edges.erase(g_posix_input.edges.begin());
	}
	g_posix_input.last_press[key] = posix_now_ms();
}

// one short retry distinguishes a lone ESC press from an escape sequence
// arriving split across reads (ssh latency)
bool posix_read_byte(unsigned char& b) {
	if (read(STDIN_FILENO, &b, 1) == 1) { return true; }
	std::this_thread::sleep_for(std::chrono::milliseconds(4));
	return read(STDIN_FILENO, &b, 1) == 1;
}

void posix_pump_input() {
	posix_enter_raw();
	if (!g_posix_input.raw) { return; }
	unsigned char b = 0;
	while (read(STDIN_FILENO, &b, 1) == 1) {
		if (b == 0x1B) {
			unsigned char b2 = 0;
			if (!posix_read_byte(b2)) { posix_note_key("esc"); continue; }
			if (b2 == '[' || b2 == 'O') {
				// CSI (ESC [) or SS3 (ESC O, macOS application cursor mode):
				// consume parameter/intermediate bytes so modified sequences
				// (ESC [ 1;5A etc.) never leak phantom keypresses
				unsigned char fin = 0;
				bool have = posix_read_byte(fin);
				while (have && b2 == '[' && fin >= 0x20 && fin <= 0x3F) { have = posix_read_byte(fin); }
				if (!have) { continue; }
				switch (fin) {
					case 'A': posix_note_key("up"); break;
					case 'B': posix_note_key("down"); break;
					case 'C': posix_note_key("right"); break;
					case 'D': posix_note_key("left"); break;
					default: break;                      // unknown sequence: swallowed
				}
				continue;
			}
			posix_note_key("esc");
			b = b2;                                      // ESC then a plain byte: classify it below
		}
		if (b == '\n' || b == '\r') { posix_note_key("enter"); continue; }
		if (b == '\t') { posix_note_key("tab"); continue; }
		if (b == 127 || b == 8) { posix_note_key("backspace"); continue; }
		if (b == ' ') { posix_note_key("space"); continue; }
		if (b > 32 && b < 127) { posix_note_key(std::string(1, static_cast<char>(std::tolower(b)))); }
	}
}
#endif

void register_console_game_io(jai::engine& eng) {
	eng.add_function("clock_ms", []() -> jai::script_int {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	});
	eng.add_function("sleep_ms", [](jai::script_int ms) {
		if (ms > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
	});
	// held-key state from console events, cross-checked against the physical
	// key so a release delivered to another window (alt-tab mid-press) unsticks
	eng.add_function("key_down", [](const std::string& name) -> bool {
#ifdef _WIN32
		win_pump_input();
		auto it = g_win_input.held.find(name);
		if (it == g_win_input.held.end() || !it->second) { return false; }
		int vk = vk_for_key_name(name);
		return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
		posix_pump_input();
		auto it = g_posix_input.last_press.find(name);
		return it != g_posix_input.last_press.end()
			&& posix_now_ms() - it->second < posix_hold_window_ms;
#endif
	});
	// non-blocking edge events: "" if none; arrows/esc/enter/tab named, letters lowercased
	eng.add_function("read_key", []() -> std::string {
#ifdef _WIN32
		win_pump_input();
		if (g_win_input.edges.empty()) { return ""; }
		std::string key = g_win_input.edges.front();
		g_win_input.edges.erase(g_win_input.edges.begin());
		return key;
#else
		posix_pump_input();
		if (g_posix_input.edges.empty()) { return ""; }
		std::string key = g_posix_input.edges.front();
		g_posix_input.edges.erase(g_posix_input.edges.begin());
		return key;
#endif
	});
	eng.add_function("term_cols", []() -> jai::script_int {
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
			return info.srWindow.Right - info.srWindow.Left + 1;
		}
#else
		winsize ws{};
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) { return ws.ws_col; }
#endif
		return 80;
	});
	eng.add_function("term_rows", []() -> jai::script_int {
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
			return info.srWindow.Bottom - info.srWindow.Top + 1;
		}
#else
		winsize ws{};
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) { return ws.ws_row; }
#endif
		return 25;
	});
	// fire-and-forget WAV playback through winmm's MCI (already linked).
	// Each distinct file gets its own waveaudio device, so DIFFERENT sounds
	// overlap (the OS mixes); replaying a sound restarts it. Registered
	// everywhere, a graceful no-op off-Windows so scripts stay portable.
	eng.add_function("play_wav", [](const std::string& path) -> bool {
#ifdef _WIN32
		auto it = g_wav_aliases.find(path);
		if (it == g_wav_aliases.end()) {
			std::string alias = "jaiwav" + std::to_string(g_wav_aliases.size());
			std::string open = "open \"" + path + "\" type waveaudio alias " + alias;
			if (::mciSendStringA(open.c_str(), nullptr, 0, nullptr) != 0) {
				return PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != 0;
			}
			it = g_wav_aliases.emplace(path, alias).first;
		}
		std::string cmd = "stop " + it->second;
		::mciSendStringA(cmd.c_str(), nullptr, 0, nullptr);
		cmd = "seek " + it->second + " to start";
		::mciSendStringA(cmd.c_str(), nullptr, 0, nullptr);
		cmd = "play " + it->second;
		return ::mciSendStringA(cmd.c_str(), nullptr, 0, nullptr) == 0;
#else
		(void)path;
		return false;
#endif
	});
	// background MIDI through winmm's MCI sequencer (mixes freely with
	// play_wav). One track at a time; scripts poll midi_status() == "stopped"
	// to loop. Same portability contract: no-ops off Windows.
	eng.add_function("play_midi", [](const std::string& path) -> bool {
#ifdef _WIN32
		::mciSendStringA("close jaimidi", nullptr, 0, nullptr);
		std::string open = "open \"" + path + "\" type sequencer alias jaimidi";
		if (::mciSendStringA(open.c_str(), nullptr, 0, nullptr) != 0) { return false; }
		return ::mciSendStringA("play jaimidi", nullptr, 0, nullptr) == 0;
#else
		(void)path;
		return false;
#endif
	});
	eng.add_function("stop_midi", []() -> bool {
#ifdef _WIN32
		::mciSendStringA("stop jaimidi", nullptr, 0, nullptr);
		return ::mciSendStringA("close jaimidi", nullptr, 0, nullptr) == 0;
#else
		return false;
#endif
	});
	eng.add_function("midi_status", []() -> std::string {
#ifdef _WIN32
		char buf[64] = {0};
		if (::mciSendStringA("status jaimidi mode", buf, 63, nullptr) != 0) { return ""; }
		return buf;
#else
		return "";
#endif
	});
}

// -------------------------------------------------------------- networking ---
// A generic length-framed TCP channel for scripts, shaped like Bindstone's
// network layer: MV's 4-byte big-endian header + content framing on the wire
// (a jaiscript peer can talk to an MV::Client/Server), and Clickable's
// property_owner + JAI_SIGNAL_PROPERTY pattern on the API. No threads: the
// channel owns non-blocking sockets and a script-called pump() drains them,
// firing signals inside the script's own call stack. Protocol semantics
// (actions, lockstep, handshakes) live entirely in script.

#ifdef _WIN32
using net_socket = SOCKET;
constexpr net_socket net_bad_socket = INVALID_SOCKET;
bool net_ready() {
	static const bool ready = [] { WSADATA wsa; return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }();
	return ready;
}
bool net_would_block() {
	int e = ::WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void net_close_socket(net_socket s) { ::closesocket(s); }
void net_set_nonblocking(net_socket s) {
	u_long mode = 1;
	::ioctlsocket(s, FIONBIO, &mode);
}
#else
using net_socket = int;
constexpr net_socket net_bad_socket = -1;
bool net_ready() { return true; }
bool net_would_block() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
void net_close_socket(net_socket s) { ::close(s); }
void net_set_nonblocking(net_socket s) { ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

class net_channel : public jai::property_owner<net_channel> {
	friend jai::access;
public:
	typedef void PacketSignature(const std::string&);
	typedef void ConnectSignature();
	typedef void DisconnectSignature(const std::string&);

	JAI_SIGNAL_PROPERTY(PacketSignature, on_packet);
	JAI_SIGNAL_PROPERTY(ConnectSignature, on_connect);
	JAI_SIGNAL_PROPERTY(DisconnectSignature, on_disconnect);

	net_channel() = default;
	~net_channel() { drop_sockets(); }

	bool listen(jai::script_int port) {
		if (!net_ready()) { return fail("winsock unavailable"); }
		drop_sockets();
		listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener_ == net_bad_socket) { return fail("socket() failed"); }
		int yes = 1;
		::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(static_cast<unsigned short>(port));
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
				|| ::listen(listener_, 1) != 0) {
			drop_sockets();
			return fail("bind/listen failed (port taken?)");
		}
		net_set_nonblocking(listener_);
		state_ = "listening";
		return true;
	}

	bool connect(const std::string& host, jai::script_int port) {
		if (!net_ready()) { return fail("winsock unavailable"); }
		drop_sockets();
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* found = nullptr;
		if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
			return fail("host not found: " + host);
		}
		peer_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (peer_ == net_bad_socket) {
			::freeaddrinfo(found);
			return fail("socket() failed");
		}
		net_set_nonblocking(peer_);
		int result = ::connect(peer_, found->ai_addr, static_cast<int>(found->ai_addrlen));
		::freeaddrinfo(found);
		if (result == 0) {
			state_ = "connected";
			announce_connect_ = true;
		} else if (net_would_block()) {
			state_ = "connecting";
		} else {
			drop_sockets();
			return fail("connect failed");
		}
		return true;
	}

	// frames and queues; pump() flushes whatever the socket won't take now
	bool send(const std::string& bytes) {
		if (state_ != "connected" && state_ != "connecting") { return false; }
		char header[4] = {
			static_cast<char>((bytes.size() >> 24) & 0xFF), static_cast<char>((bytes.size() >> 16) & 0xFF),
			static_cast<char>((bytes.size() >> 8) & 0xFF), static_cast<char>(bytes.size() & 0xFF) };
		outbox_.append(header, 4);
		outbox_.append(bytes);
		if (state_ == "connected") { flush(); }
		return true;
	}

	jai::script_int pump() {
		jai::script_int fired = 0;
		if (listener_ != net_bad_socket && peer_ == net_bad_socket) {
			net_socket accepted = ::accept(listener_, nullptr, nullptr);
			if (accepted != net_bad_socket) {
				net_set_nonblocking(accepted);
				peer_ = accepted;
				state_ = "connected";
				announce_connect_ = true;
			}
		}
		if (state_ == "connecting" && peer_ != net_bad_socket) {
			// refusal lands in the EXCEPT set on Windows (writability never fires)
			fd_set writable;
			fd_set failed;
			FD_ZERO(&writable);
			FD_ZERO(&failed);
			FD_SET(peer_, &writable);
			FD_SET(peer_, &failed);
			timeval instant{0, 0};
			if (::select(static_cast<int>(peer_ + 1), nullptr, &writable, &failed, &instant) > 0) {
				int error = 0;
				socklen_t len = sizeof(error);
				::getsockopt(peer_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
				if (error == 0 && !FD_ISSET(peer_, &failed)) {
					state_ = "connected";
					announce_connect_ = true;
				} else {
					disconnect("connect refused");
					return fired;
				}
			}
		}
		if (announce_connect_) {
			announce_connect_ = false;
			flush();
			on_connectSignal();
		}
		if (state_ != "connected" || peer_ == net_bad_socket) { return fired; }
		flush();
		char chunk[4096];
		std::string drop_reason;                          // close/error noted, HELD until queued
		while (peer_ != net_bad_socket) {                 // frames deliver first (a goodbye packet
			int got = static_cast<int>(::recv(peer_, chunk, sizeof(chunk), 0));   // often rides the same recv as the close)
			if (got > 0) { inbox_.append(chunk, static_cast<size_t>(got)); continue; }
			if (got == 0) { drop_reason = "closed by peer"; }
			else if (!net_would_block()) { drop_reason = "recv error"; }
			break;
		}
		while (inbox_.size() >= 4) {
			size_t length = (static_cast<size_t>(static_cast<unsigned char>(inbox_[0])) << 24)
				| (static_cast<size_t>(static_cast<unsigned char>(inbox_[1])) << 16)
				| (static_cast<size_t>(static_cast<unsigned char>(inbox_[2])) << 8)
				| static_cast<size_t>(static_cast<unsigned char>(inbox_[3]));
			if (length > 64u * 1024u * 1024u) {
				disconnect("oversized frame");
				break;
			}
			if (inbox_.size() < 4 + length) { break; }
			std::string content = inbox_.substr(4, length);
			inbox_.erase(0, 4 + length);
			++fired;
			on_packetSignal(content);              // may reentrantly send()/close(): state re-checked per loop
			if (state_ != "connected") { break; }
		}
		if (!drop_reason.empty() && state_ == "connected") { disconnect(drop_reason); }
		return fired;
	}

	void close() {
		drop_sockets();
		state_ = "idle";
	}

	bool connected() const { return state_ == "connected"; }
	std::string status() const { return state_; }

private:
	bool fail(const std::string& why) {
		state_ = "error: " + why;
		return false;
	}

	void disconnect(const std::string& reason) {
		drop_sockets();
		state_ = "closed: " + reason;
		on_disconnectSignal(reason);
	}

	void flush() {
		while (!outbox_.empty() && peer_ != net_bad_socket && state_ == "connected") {
			int sent = static_cast<int>(::send(peer_, outbox_.data(), static_cast<int>(outbox_.size()), 0));
			if (sent > 0) { outbox_.erase(0, static_cast<size_t>(sent)); continue; }
			if (!net_would_block()) { disconnect("send error"); }
			break;
		}
	}

	void drop_sockets() {
		if (peer_ != net_bad_socket) { net_close_socket(peer_); peer_ = net_bad_socket; }
		if (listener_ != net_bad_socket) { net_close_socket(listener_); listener_ = net_bad_socket; }
		inbox_.clear();
		outbox_.clear();
	}

	net_socket peer_ = net_bad_socket;
	net_socket listener_ = net_bad_socket;
	std::string inbox_;
	std::string outbox_;
	std::string state_ = "idle";
	bool announce_connect_ = false;
};

// The MV::Server shape for scripts: persistent listener, N framed connections,
// per-client signals, broadcast. Same wire format as net_channel; same no-thread
// pump contract. Client ids are stable, never reused within a server's lifetime.
class net_server : public jai::property_owner<net_server> {
	friend jai::access;
public:
	typedef void ClientSignature(jai::script_int);
	typedef void ClientPacketSignature(jai::script_int, const std::string&);
	typedef void ClientDropSignature(jai::script_int, const std::string&);

	JAI_SIGNAL_PROPERTY(ClientSignature, on_client_connect);
	JAI_SIGNAL_PROPERTY(ClientPacketSignature, on_client_packet);
	JAI_SIGNAL_PROPERTY(ClientDropSignature, on_client_disconnect);

	net_server() = default;
	~net_server() { close(); }

	bool listen(jai::script_int port) {
		if (!net_ready()) { return false; }
		close();
		listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener_ == net_bad_socket) { return false; }
		int yes = 1;
		::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(static_cast<unsigned short>(port));
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
				|| ::listen(listener_, 8) != 0) {
			net_close_socket(listener_);
			listener_ = net_bad_socket;
			return false;
		}
		net_set_nonblocking(listener_);
		return true;
	}

	bool send(jai::script_int id, const std::string& bytes) {
		client* c = find(id);
		if (!c) { return false; }
		frame_into(c->outbox, bytes);
		flush(*c);
		return true;
	}

	jai::script_int broadcast(const std::string& bytes) {
		jai::script_int reached = 0;
		for (auto& c : clients_) {
			if (c.sock == net_bad_socket) { continue; }
			frame_into(c.outbox, bytes);
			flush(c);
			++reached;
		}
		return reached;
	}

	jai::script_int pump() {
		jai::script_int fired = 0;
		if (listener_ != net_bad_socket) {
			for (;;) {
				net_socket accepted = ::accept(listener_, nullptr, nullptr);
				if (accepted == net_bad_socket) { break; }
				net_set_nonblocking(accepted);
				clients_.push_back(client{next_id_++, accepted});
				on_client_connectSignal(clients_.back().id);
			}
		}
		for (size_t i = 0; i < clients_.size(); ++i) {   // index loop: receivers may connect more clients
			client& c = clients_[i];
			if (c.sock == net_bad_socket) { continue; }
			flush(c);
			std::string drop_reason;                      // held until queued frames deliver
			char chunk[4096];
			while (c.sock != net_bad_socket) {
				int got = static_cast<int>(::recv(c.sock, chunk, sizeof(chunk), 0));
				if (got > 0) { c.inbox.append(chunk, static_cast<size_t>(got)); continue; }
				if (got == 0) { drop_reason = "closed by peer"; }
				else if (!net_would_block()) { drop_reason = "recv error"; }
				break;
			}
			while (c.sock != net_bad_socket && c.inbox.size() >= 4) {
				size_t length = (static_cast<size_t>(static_cast<unsigned char>(c.inbox[0])) << 24)
					| (static_cast<size_t>(static_cast<unsigned char>(c.inbox[1])) << 16)
					| (static_cast<size_t>(static_cast<unsigned char>(c.inbox[2])) << 8)
					| static_cast<size_t>(static_cast<unsigned char>(c.inbox[3]));
				if (length > 64u * 1024u * 1024u) {
					drop_reason = "oversized frame";
					break;
				}
				if (c.inbox.size() < 4 + length) { break; }
				std::string content = c.inbox.substr(4, length);
				c.inbox.erase(0, 4 + length);
				++fired;
				on_client_packetSignal(c.id, content);
			}
			if (!drop_reason.empty() && c.sock != net_bad_socket) {
				drop(c, drop_reason);
			}
		}
		clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
			[](const client& c) { return c.sock == net_bad_socket; }), clients_.end());
		return fired;
	}

	void kick(jai::script_int id) {
		if (client* c = find(id)) { drop(*c, "kicked"); }
	}

	jai::script_int client_count() const {
		return static_cast<jai::script_int>(clients_.size());
	}

	bool listening() const { return listener_ != net_bad_socket; }

	void close() {
		for (auto& c : clients_) {
			if (c.sock != net_bad_socket) { net_close_socket(c.sock); c.sock = net_bad_socket; }
		}
		clients_.clear();
		if (listener_ != net_bad_socket) { net_close_socket(listener_); listener_ = net_bad_socket; }
	}

private:
	struct client {
		jai::script_int id = 0;
		net_socket sock = net_bad_socket;
		std::string inbox;
		std::string outbox;
	};

	client* find(jai::script_int id) {
		for (auto& c : clients_) {
			if (c.id == id && c.sock != net_bad_socket) { return &c; }
		}
		return nullptr;
	}

	static void frame_into(std::string& outbox, const std::string& bytes) {
		char header[4] = {
			static_cast<char>((bytes.size() >> 24) & 0xFF), static_cast<char>((bytes.size() >> 16) & 0xFF),
			static_cast<char>((bytes.size() >> 8) & 0xFF), static_cast<char>(bytes.size() & 0xFF) };
		outbox.append(header, 4);
		outbox.append(bytes);
	}

	void flush(client& c) {
		while (!c.outbox.empty() && c.sock != net_bad_socket) {
			int sent = static_cast<int>(::send(c.sock, c.outbox.data(), static_cast<int>(c.outbox.size()), 0));
			if (sent > 0) { c.outbox.erase(0, static_cast<size_t>(sent)); continue; }
			if (!net_would_block()) { drop(c, "send error"); }
			break;
		}
	}

	void drop(client& c, const std::string& reason) {
		if (c.sock == net_bad_socket) { return; }
		net_close_socket(c.sock);
		c.sock = net_bad_socket;
		on_client_disconnectSignal(c.id, reason);
	}

	net_socket listener_ = net_bad_socket;
	std::vector<client> clients_;
	jai::script_int next_id_ = 1;
};

void register_net_channel(jai::engine& eng) {
	jai::dynamic_binder<net_server>(eng, "NetServer")
		.constructor<>()
		.method("listen", &net_server::listen)
		.method("send", &net_server::send)
		.method("broadcast", &net_server::broadcast)
		.method("pump", &net_server::pump)
		.method("kick", &net_server::kick)
		.method("client_count", &net_server::client_count)
		.method("listening", &net_server::listening)
		.method("close", &net_server::close)
		.auto_bind()
		.build();
	jai::dynamic_binder<net_channel>(eng, "NetChannel")
		.constructor<>()
		.method("listen", &net_channel::listen)
		.method("connect", &net_channel::connect)
		.method("send", &net_channel::send)
		.method("pump", &net_channel::pump)
		.method("close", &net_channel::close)
		.method("connected", &net_channel::connected)
		.method("status", &net_channel::status)
		.auto_bind()   // JAI_SIGNAL_PROPERTYs expose the generic Signal view
		.build();
}

// ----------------------------------------------------------------- engine ---

std::string trim(const std::string& s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) { return ""; }
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

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

// The reference embedding: engine + backend + budget + full stdlib + host file IO.
std::shared_ptr<jai::engine> make_runner_engine(const options& opt, std::ostream* engine_out) {
	auto eng = jai::engine::make();
	eng->set_backend(opt.backend == "interp" ? jai::backend_type::interpreter : jai::backend_type::vm);
	eng->execution_budget(opt.budget);
	if (engine_out) {
		eng->set_output_stream(std::shared_ptr<std::ostream>(engine_out, [](std::ostream*) {}));
	}
	jai::stdlib::register_all(eng);

	// Host file IO — a runner without it can't save anything (same shapes as the example games).
	eng->add_function("read_file", [](const std::string& path) -> std::string { return slurp_file(path); });
	eng->add_function("write_file", [](const std::string& path, const std::string& content) -> bool {
		return spill_file(path, content);
	});
	eng->add_function("file_exists", [](const std::string& path) -> bool { return fs::exists(path); });
	eng->add_function("delete_file", [](const std::string& path) -> bool {
		std::error_code ec;
		return fs::remove(path, ec);
	});
	register_console_game_io(*eng);
	register_net_channel(*eng);

	jai::script_value args = jai::script_value::make_array(nullptr, eng.get());
	auto& arg_storage = args.as_array();
	arg_storage.reserve(opt.script_args.size());
	for (const auto& s : opt.script_args) { arg_storage.emplace_back(s, eng.get()); }
	eng->add_global("ARGS", std::move(args));
	return eng;
}

// --------------------------------------------------------------- debugger ---

#ifdef JAISCRIPT_ENABLE_DEBUGGER
// Exclusive-bind probe: the connector's listener sets SO_REUSEADDR, which on Windows lets a
// second bind on a taken port "succeed" — probe without it (same pattern as the Bindstone hosts).
bool debug_port_free(int port) {
#ifdef _WIN32
	static const bool wsa_ready = [] { WSADATA wsa; return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }();
	if (!wsa_ready) { return false; }
#endif
	auto probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
	if (probe == INVALID_SOCKET) { return false; }
#else
	if (probe < 0) { return false; }
#endif
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bool available = ::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
#ifdef _WIN32
	::closesocket(probe);
#else
	::close(probe);
#endif
	return available;
}

// Base 52477 keeps clear of the Bindstone processes (52472/3/4) while staying in the family;
// +10 steps like they do. Returns the bound port, 0 if nothing bound.
int attach_debugger(jai::engine& eng, int requested_port) {
	int base = requested_port > 0 ? requested_port : jai::debug::default_port + 5;
	for (int candidate = base; candidate < base + 80; candidate += 10) {
		if (!debug_port_free(candidate)) { continue; }
		auto connector = jai::debug::listen(eng, candidate);
		if (connector->port() == candidate) { return candidate; }
		eng.set_debug_connector(nullptr);
	}
	return 0;
}
#endif

int wire_debugger(const options& opt, jai::engine& eng, const console_state& con, std::ostream& out) {
	if (!opt.debug) { return 0; }
#ifdef JAISCRIPT_ENABLE_DEBUGGER
	int port = attach_debugger(eng, opt.debug_port);
	if (port != 0) {
		out << con.dim("debugger: DAP listening on 127.0.0.1:" + std::to_string(port)
			+ " (VS Code: Attach to JaiScript)") << "\n";
	} else {
		out << con.dim("debugger: no free port; running without the listener") << "\n";
	}
	return port;
#else
	(void)eng; (void)con; (void)out;
	return 0;
#endif
}

// ------------------------------------------------------------- file runner ---

struct run_result {
	int exit_code = 0;
	bool ok = true;
	double ms = 0.0;
	std::string error;       // one-line message (stack trace already printed by the caller path)
	std::string stack_trace;
};

int result_to_exit_code(const jai::script_value& result) {
	return result.is_int() ? static_cast<int>(result.as_int()) : 0;
}

// execute_file_source keeps the file-path attribution (stack traces, debugger breakpoints)
// while letting the host strip a UTF-8 BOM — Notepad's default, PowerShell's Set-Content —
// which the lexer would otherwise reject as "Unexpected character".
jai::script_value execute_script_file(jai::engine& eng, const fs::path& abs) {
	std::string content = slurp_file(abs.string());
	if (content.rfind("\xEF\xBB\xBF", 0) == 0) { content.erase(0, 3); }
	return eng.execute_file_source(abs.string(), content);
}

// Runs `path` in `eng` with include/import resolving from the script's own directory.
run_result run_file_in_engine(jai::engine& eng, const std::string& path) {
	run_result r;
	std::error_code ec;
	fs::path abs = fs::absolute(path, ec);
	eng.add_include_path(abs.parent_path().string());
	auto start = std::chrono::steady_clock::now();
	try {
		jai::script_value result = execute_script_file(eng, abs);
		r.exit_code = result_to_exit_code(result);
	} catch (const std::exception& ex) {
		r.ok = false;
		r.exit_code = 1;
		r.error = ex.what();
		r.stack_trace = trim(eng.format_stack_trace());
	}
	r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return r;
}

int run_file_mode(const options& opt, const console_state& con) {
	if (!fs::exists(opt.script)) {
		std::fprintf(stderr, "%s\n", con.red("jaiscript: no such file: " + opt.script).c_str());
		return 2;
	}
	auto eng = make_runner_engine(opt, nullptr);
	std::ostringstream debug_note;
	wire_debugger(opt, *eng, con, debug_note);
	if (!debug_note.str().empty()) { std::fputs(debug_note.str().c_str(), stderr); }

	run_result r = run_file_in_engine(*eng, opt.script);
	if (!r.ok) {
		std::fprintf(stderr, "%s\n", con.red("jaiscript: " + r.error).c_str());
		if (!r.stack_trace.empty()) { std::fprintf(stderr, "%s\n", r.stack_trace.c_str()); }
	}
	if (opt.time_runs) {
		char text[64];
		std::snprintf(text, sizeof(text), "[%.3f ms]", r.ms);
		std::fprintf(stderr, "%s\n", con.dim(text).c_str());
	}
	bool pause = opt.pause_mode == 1 || (opt.pause_mode == -1 && con.solo_console);
	if (pause) {
		std::fputs(con.dim("[press Enter to close]").c_str(), stderr);
		std::fputc('\n', stderr);
		std::string line;
		std::getline(std::cin, line);
	}
	return r.exit_code;
}

// ------------------------------------------------------------- watch mode ---

int run_watch_mode(const options& opt, const console_state& con) {
	if (!fs::exists(opt.script)) {
		std::fprintf(stderr, "%s\n", con.red("jaiscript: no such file: " + opt.script).c_str());
		return 2;
	}
	options run_opt = opt;
	run_opt.debug = false;   // fresh engine per run: an attach would drop every save
	int run_count = 0;
	std::error_code ec;
	fs::file_time_type last_mtime = fs::last_write_time(opt.script, ec);
	bool first = true;
	while (!g_interrupted.load()) {
		if (!first) {
			fs::file_time_type now_mtime = fs::last_write_time(opt.script, ec);
			if (ec || now_mtime == last_mtime) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}
			last_mtime = now_mtime;
		}
		first = false;
		++run_count;
		con.clear_screen();
		run_result r;
		{
			auto eng = make_runner_engine(run_opt, nullptr);   // fresh world every save
			r = run_file_in_engine(*eng, opt.script);
		}
		if (!r.ok) {
			std::fprintf(stderr, "%s\n", con.red("jaiscript: " + r.error).c_str());
			if (!r.stack_trace.empty()) { std::fprintf(stderr, "%s\n", r.stack_trace.c_str()); }
		}
		auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm tm_buf{};
#ifdef _WIN32
		localtime_s(&tm_buf, &t);
#else
		localtime_r(&t, &tm_buf);
#endif
		char clock_text[16];
		std::strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &tm_buf);
		char status[512];
		std::snprintf(status, sizeof(status), "[watch] %s  #%d  %.1f ms  %s  %s  (save to re-run, Ctrl+C quits)",
			opt.script.c_str(), run_count, r.ms, r.ok ? "ok" : "ERROR", clock_text);
		std::string line = r.ok ? con.dim(status) : con.red(status);
		std::fprintf(stdout, "%s\n", line.c_str());
		std::fflush(stdout);
	}
	std::fprintf(stdout, "\n%s\n", con.dim("[watch] stopped").c_str());
	return 0;
}

// ---------------------------------------------------------------- balancer ---

// Tracks whether accumulated REPL input is submittable: all of () [] {} closed, no open
// /* */ block comment, no string/char literal left open at end of line.
struct line_balancer {
	int depth = 0;
	bool in_block_comment = false;
	bool in_string = false;
	char string_delim = '"';

	void reset() { depth = 0; in_block_comment = false; in_string = false; }

	void feed(const std::string& line) {
		for (size_t i = 0; i < line.size(); ++i) {
			char c = line[i];
			if (in_block_comment) {
				if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') { in_block_comment = false; ++i; }
				continue;
			}
			if (in_string) {
				if (c == '\\') { ++i; }
				else if (c == string_delim) { in_string = false; }
				continue;
			}
			switch (c) {
				case '/':
					if (i + 1 < line.size() && line[i + 1] == '/') { return; }   // line comment
					if (i + 1 < line.size() && line[i + 1] == '*') { in_block_comment = true; ++i; }
					break;
				case '"': case '\'': in_string = true; string_delim = c; break;
				case '(': case '[': case '{': ++depth; break;
				case ')': case ']': case '}': if (depth > 0) { --depth; } break;
				default: break;
			}
		}
	}

	bool balanced() const { return depth == 0 && !in_block_comment && !in_string; }
};

// ------------------------------------------------------------------- REPL ---

// A pasted-or-dropped script path: optionally quoted, ends in .jai, exists on disk.
bool looks_like_script_path(const std::string& line, std::string& path_out) {
	std::string t = trim(line);
	if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\''))) {
		t = t.substr(1, t.size() - 2);
	}
	if (t.size() < 5) { return false; }
	std::string ext = t.substr(t.size() - 4);
	for (auto& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	if (ext != ".jai") { return false; }
	std::error_code ec;
	if (!fs::exists(t, ec)) { return false; }
	path_out = t;
	return true;
}

const char* repl_help =
	":help                 this text\n"
	":load <path>          run a .jai file into this session (pasting/dropping a path works too)\n"
	":reset                fresh engine (keeps backend, budget, debugger)\n"
	":backend vm|interp    switch backend (resets the engine)\n"
	":time on|off          print wall time per execute\n"
	":quit / :exit         leave (Ctrl+Z+Enter or Ctrl+C also work)\n"
	"\n"
	"multi-line: input keeps collecting while (), [], {}, /* */ or a string is open\n"
	"            (the prompt becomes ...>); a blank line force-submits what you have.\n"
	"auto-print: a non-null result prints; strings print quoted so \"5\" and 5 differ.\n"
	"budget:     off by default in the REPL (long loops are your business); restart with\n"
	"            --budget=N to restore one, e.g. --budget=1.\n";

struct repl_session {
	options opt;
	const console_state* con = nullptr;
	std::ostream* engine_out = nullptr;    // selftest capture; null = engine default (stdout)
	std::shared_ptr<jai::engine> eng;
	bool show_time = false;
	bool debugger_wired = false;

	// The engine holds the connector (jai::debug::listen attaches it), so releasing the old
	// engine frees the port before the new listener binds.
	void build(std::ostream& out, bool announce) {
#ifdef JAISCRIPT_ENABLE_DEBUGGER
		if (eng) { eng->set_debug_connector(nullptr); }
#endif
		eng.reset();
		eng = make_runner_engine(opt, engine_out);
		eng->add_include_path(fs::current_path().string());
		if (opt.debug) {
#ifdef JAISCRIPT_ENABLE_DEBUGGER
			int port = attach_debugger(*eng, opt.debug_port);
			if (port != 0 && announce) {
				out << con->dim("debugger: DAP listening on 127.0.0.1:" + std::to_string(port)
					+ " (VS Code: Attach to JaiScript)") << "\n";
			}
			debugger_wired = port != 0;
#else
			if (announce) { out << con->dim("debugger: this build has JAISCRIPT_ENABLE_DEBUGGER off") << "\n"; }
#endif
		}
	}

	void print_result(const jai::script_value& result, std::ostream& out) {
		if (result.is_null()) { return; }
		if (result.is_string()) { out << '"' << result.to_string() << '"' << "\n"; }
		else if (result.is_char()) { out << '\'' << result.to_string() << '\'' << "\n"; }
		else if (result.is_array() || result.is_map()) {
			// to_string says only "[array]"/"[map]" — show the contents like a REPL should
			out << jai::stdlib::to_json_impl(result, -1, 0) << "\n";   // -1 = compact one-liner
		}
		else { out << result.to_string() << "\n"; }
	}

	void execute_chunk(const std::string& source, std::ostream& out) {
		auto start = std::chrono::steady_clock::now();
		try {
			jai::script_value result = eng->execute(source);
			print_result(result, out);
		} catch (const std::exception& ex) {
			out << con->red(std::string("error: ") + ex.what()) << "\n";
			std::string trace = trim(eng->format_stack_trace());
			if (!trace.empty()) { out << con->dim(trace) << "\n"; }
		}
		if (show_time) {
			double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			char text[64];
			std::snprintf(text, sizeof(text), "[%.3f ms]", ms);
			out << con->dim(text) << "\n";
		}
	}

	void load_file(const std::string& path, std::ostream& out) {
		if (!fs::exists(path)) {
			out << con->red("no such file: " + path) << "\n";
			return;
		}
		auto start = std::chrono::steady_clock::now();
		std::error_code ec;
		fs::path abs = fs::absolute(path, ec);
		eng->add_include_path(abs.parent_path().string());
		try {
			jai::script_value result = execute_script_file(*eng, abs);
			out << con->dim("[loaded " + path + "]") << "\n";
			print_result(result, out);
		} catch (const std::exception& ex) {
			out << con->red(std::string("error: ") + ex.what()) << "\n";
			std::string trace = trim(eng->format_stack_trace());
			if (!trace.empty()) { out << con->dim(trace) << "\n"; }
		}
		if (show_time) {
			double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			char text[64];
			std::snprintf(text, sizeof(text), "[%.3f ms]", ms);
			out << con->dim(text) << "\n";
		}
	}

	// returns false when the session should end
	bool meta_command(const std::string& line, std::ostream& out) {
		std::istringstream ss(line);
		std::string cmd, arg;
		ss >> cmd;
		std::getline(ss, arg);
		arg = trim(arg);
		if (cmd == ":quit" || cmd == ":exit" || cmd == ":q") { return false; }
		if (cmd == ":help" || cmd == ":h") { out << repl_help; return true; }
		if (cmd == ":time") {
			if (arg == "on") { show_time = true; }
			else if (arg == "off") { show_time = false; }
			out << con->dim(std::string("time: ") + (show_time ? "on" : "off")) << "\n";
			return true;
		}
		if (cmd == ":reset") {
			build(out, true);
			out << con->dim("engine reset (" + opt.backend + " backend)") << "\n";
			return true;
		}
		if (cmd == ":backend") {
			if (arg == "interpreter") { arg = "interp"; }
			if (arg != "vm" && arg != "interp") {
				out << con->red("usage: :backend vm|interp (switching resets the engine)") << "\n";
				return true;
			}
			opt.backend = arg;
			build(out, true);
			out << con->dim("backend: " + opt.backend + " (fresh engine)") << "\n";
			return true;
		}
		if (cmd == ":load") {
			if (arg.empty()) { out << con->red("usage: :load <path.jai>") << "\n"; return true; }
			std::string path = arg;
			std::string unquoted;
			if (looks_like_script_path(arg, unquoted)) { path = unquoted; }
			load_file(path, out);
			return true;
		}
		out << con->red("unknown command " + cmd + " (:help lists them)") << "\n";
		return true;
	}

	int run(std::istream& in, std::ostream& out, bool interactive) {
		if (interactive) {
			out << "JaiScript " << jai::version() << " - " << opt.backend
				<< " backend, stdlib loaded, budget "
				<< (opt.budget > 0 ? std::to_string(opt.budget) + "s" : std::string("off"))
				<< ". :help for commands.\n";
		}
		build(out, interactive);
		std::string buffer;
		line_balancer balance;
		while (true) {
			if (interactive) {
				out << (buffer.empty() ? "jai> " : "...> ");
				out.flush();
			}
			std::string line;
			if (!std::getline(in, line)) {
				if (interactive) { out << "\n"; }
				break;   // EOF (Ctrl+Z+Enter) or Ctrl+C aborting the read
			}
			if (g_interrupted.load()) { break; }
			// piped input arrives with CRLF endings and sometimes a UTF-8 BOM up front
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			if (line.rfind("\xEF\xBB\xBF", 0) == 0) { line.erase(0, 3); }
			if (buffer.empty()) {
				std::string t = trim(line);
				if (t.empty()) { continue; }
				if (t[0] == ':') {
					if (!meta_command(t, out)) { break; }
					continue;
				}
				std::string path;
				if (looks_like_script_path(t, path)) {
					load_file(path, out);
					continue;
				}
			} else if (trim(line).empty()) {
				// blank line in continuation force-submits (escape hatch for a stuck bracket)
				execute_chunk(buffer, out);
				buffer.clear();
				balance.reset();
				continue;
			}
			balance.feed(line);
			buffer += buffer.empty() ? line : "\n" + line;
			if (balance.balanced()) {
				execute_chunk(buffer, out);
				buffer.clear();
				balance.reset();
			}
		}
		return 0;
	}
};

int run_repl_mode(const options& opt, const console_state& con) {
	repl_session session;
	session.opt = opt;
	session.con = &con;
	return session.run(std::cin, std::cout, con.in_tty);
}

// --------------------------------------------------------------- selftest ---

int run_selftest(const console_state& con) {
	int passed = 0, failed = 0;
	auto expect = [&](bool ok, const char* name) {
		if (ok) { ++passed; std::printf("  ok    %s\n", name); }
		else { ++failed; std::printf("  %s  %s\n", con.red("FAIL").c_str(), name); }
	};

	{ // balancer
		line_balancer b;
		b.feed("if (x) {");
		expect(!b.balanced(), "balancer: open brace continues");
		b.feed("}");
		expect(b.balanced(), "balancer: closing brace submits");
		b.reset();
		b.feed("print(\"{ not a brace\");");
		expect(b.balanced(), "balancer: braces inside strings ignored");
		b.reset();
		b.feed("int x = 1; // {");
		expect(b.balanced(), "balancer: line comments ignored");
		b.reset();
		b.feed("/* open comment {");
		expect(!b.balanced(), "balancer: open block comment continues");
		b.feed("} still comment */ 1 + 1");
		expect(b.balanced(), "balancer: closed block comment submits");
		b.reset();
		b.feed("\"unterminated");
		expect(!b.balanced(), "balancer: unterminated string continues");
	}

	// temp workspace WITH SPACES — the drag-drop path shape
	fs::path dir = fs::temp_directory_path() / "jaiscript selftest dir";
	std::error_code ec;
	fs::create_directories(dir, ec);
	options opt;
	opt.debug = false;

	{ // file mode: result int -> exit code, script dir import resolution
		fs::path lib = dir / "answer lib.jai";
		spill_file(lib.string(), "int selftest_answer() { return 6 * 7; }\n");
		fs::path script = dir / "main script.jai";
		spill_file(script.string(), "import \"answer lib.jai\";\nprint(\"selftest file mode\");\nselftest_answer()\n");
		auto eng = make_runner_engine(opt, nullptr);
		std::ostringstream captured;
		eng->set_output_stream(std::shared_ptr<std::ostream>(&captured, [](std::ostream*) {}));
		run_result r = run_file_in_engine(*eng, script.string());
		expect(r.ok, "file: script with spaces-in-path runs");
		expect(r.exit_code == 42, "file: int result becomes the exit code");
		expect(captured.str().find("selftest file mode") != std::string::npos, "file: print output captured");
	}

	{ // file mode: runtime error -> exit 1 + message
		fs::path bad = dir / "boom.jai";
		spill_file(bad.string(), "throw \"boom\";\n");
		auto eng = make_runner_engine(opt, nullptr);
		run_result r = run_file_in_engine(*eng, bad.string());
		expect(!r.ok && r.exit_code == 1, "file: runtime error exits 1");
		expect(r.error.find("boom") != std::string::npos, "file: error message surfaced");
	}

	{ // REPL scripted session: multi-line, auto-print, typo recovery, meta-commands, path paste
		fs::path pasted = dir / "pasted.jai";
		spill_file(pasted.string(), "int pasted_marker = 777;\nprint(\"pasted ran\");\n");
		std::ostringstream out;
		std::istringstream in(
			"int a = 2;\n"
			"a + 3\n"
			"int add(int x, int y) {\n"
			"return x + y;\n"
			"}\n"
			"add(4, 5)\n"
			"\"quoted string\"\n"
			"this is a deliberate typo(\n"
			")\n"
			"a + 40\n"
			":time on\n"
			"1 + 1\n"
			":backend interp\n"
			"2 + 2\n"
			"\"" + pasted.string() + "\"\n"
			"pasted_marker\n"
			":reset\n"
			":help\n"
			":quit\n");
		repl_session session;
		session.opt = opt;
		session.con = &con;
		session.engine_out = &out;
		session.run(in, out, false);
		std::string text = out.str();
		expect(text.find("5") != std::string::npos, "repl: expression auto-prints");
		expect(text.find("9") != std::string::npos, "repl: multi-line function defined and called");
		expect(text.find("\"quoted string\"") != std::string::npos, "repl: strings auto-print quoted");
		expect(text.find("error:") != std::string::npos, "repl: typo reports an error");
		expect(text.find("42") != std::string::npos, "repl: engine usable after an error");
		expect(text.find("[0.") != std::string::npos || text.find(" ms]") != std::string::npos, "repl: :time prints a duration");
		expect(text.find("backend: interp") != std::string::npos, "repl: :backend switches");
		expect(text.find("4") != std::string::npos, "repl: post-switch execute works");
		expect(text.find("pasted ran") != std::string::npos, "repl: pasted .jai path runs into the session");
		expect(text.find("777") != std::string::npos, "repl: pasted file's globals visible");
		expect(text.find(":backend vm|interp") != std::string::npos, "repl: :help prints");
	}

	{ // interactive dressing: banner + prompts (piped/captured, so colors are off)
		std::ostringstream out;
		std::istringstream in("1 + 1\n:quit\n");
		repl_session session;
		session.opt = opt;
		session.con = &con;
		session.engine_out = &out;
		session.run(in, out, true);
		std::string text = out.str();
		expect(text.find("JaiScript " + jai::version()) != std::string::npos, "repl: banner shows version");
		expect(text.find("jai> ") != std::string::npos, "repl: prompt prints when interactive");
	}

	std::printf("selftest: %d passed, %d failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
	console_state con;
	con.init();

	options opt;
	int parse_exit = parse_args(argc, argv, opt, con);
	if (parse_exit >= 0) { return parse_exit; }

	if (opt.selftest) { return run_selftest(con); }
	if (opt.watch) { return run_watch_mode(opt, con); }
	if (!opt.script.empty()) { return run_file_mode(opt, con); }
	return run_repl_mode(opt, con);
}
