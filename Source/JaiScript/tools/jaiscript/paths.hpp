#pragma once
// Runner-side location primitives: where the executable is, where the program's root script
// is, where the file running right now is, and a per-user writable place for an app's data.
// A script dropped onto jaiscript.exe runs with whatever working directory the shell chose,
// so relative paths in a program resolve against these instead. Registered by
// register_paths() next to the console, net and fetch bindings; a host embedding the same
// surface (Bindstone) passes its own data/cache resolvers for platforms the defaults do not
// know (Android has no home directory).
//
// Script surface (every directory is absolute, forward slashes, no trailing separator):
//   executable_dir() / bin_dir()   folder of the running executable
//   root_script_dir()               folder of the script the run started with (the working
//                                   directory for a REPL or an inline -e program)
//   root_script_path()              that script's full path ("" for a REPL / -e run)
//   current_script_dir()            folder of the file whose code is executing: an imported
//                                   module sees its own folder, its caller sees theirs
//   current_script_path()           that file's full path
//   app_data_dir(name)              per-user data folder for the app `name`, created on demand:
//                                   %LOCALAPPDATA%/name, ~/Library/Application Support/name,
//                                   $XDG_DATA_HOME/name or ~/.local/share/name
//   app_cache_dir(name)             per-user cache folder: %LOCALAPPDATA%/name/cache,
//                                   ~/Library/Caches/name, $XDG_CACHE_HOME/name or ~/.cache/name
//   dir_exists(path) -> bool;  make_dirs(path) -> bool;  list_dir(path) -> array<string> (names, sorted)
//   parent_dir(path) -> the folder above (lexical, normalized);  file_name(path) -> the last component
//   working_dir()  the process working directory (what a bare relative path resolves against)
#include <jaiscript/jaiscript.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace jai_runner {

namespace fs = std::filesystem;

inline std::string generic_dir(fs::path p) {
	std::error_code ec;
	fs::path canon = fs::weakly_canonical(p, ec);
	if (!ec && !canon.empty()) { p = canon; }
	std::string s = p.lexically_normal().generic_string();
	while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':')) { s.pop_back(); }
	return s;
}

inline std::string executable_path() {
#ifdef _WIN32
	std::wstring buf(MAX_PATH, L'\0');
	for (;;) {
		DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
		if (n == 0) { return {}; }
		if (n < buf.size()) { buf.resize(n); break; }
		buf.resize(buf.size() * 2);
	}
	return fs::path(buf).generic_string();
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buf(size, '\0');
	if (_NSGetExecutablePath(buf.data(), &size) != 0) { return {}; }
	buf.resize(std::char_traits<char>::length(buf.c_str()));
	return buf;
#else
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) { return {}; }
	buf[n] = '\0';
	return std::string(buf);
#endif
}

inline std::string executable_dir() {
	std::string exe = executable_path();
	std::error_code ec;
	return generic_dir(exe.empty() ? fs::current_path(ec) : fs::path(exe).parent_path());
}

inline std::string env_or_empty(const char* name) {
	const char* v = std::getenv(name);
	return v && *v ? std::string(v) : std::string();
}

inline std::string home_dir() {
	std::string h = env_or_empty("HOME");
#ifdef _WIN32
	if (h.empty()) { h = env_or_empty("USERPROFILE"); }
#endif
	return h;
}

// Platform default for a per-user data folder; the host may override (see path_hooks)
inline fs::path default_app_data_root() {
#ifdef _WIN32
	std::string local = env_or_empty("LOCALAPPDATA");
	if (local.empty()) { local = env_or_empty("APPDATA"); }
	return local.empty() ? fs::path(home_dir()) : fs::path(local);
#elif defined(__APPLE__)
	return fs::path(home_dir()) / "Library" / "Application Support";
#else
	std::string xdg = env_or_empty("XDG_DATA_HOME");
	return xdg.empty() ? fs::path(home_dir()) / ".local" / "share" : fs::path(xdg);
#endif
}

inline fs::path default_app_cache_root() {
#ifdef _WIN32
	return default_app_data_root();
#elif defined(__APPLE__)
	return fs::path(home_dir()) / "Library" / "Caches";
#else
	std::string xdg = env_or_empty("XDG_CACHE_HOME");
	return xdg.empty() ? fs::path(home_dir()) / ".cache" : fs::path(xdg);
#endif
}

inline std::string ensure_dir(const fs::path& p) {
	std::error_code ec;
	fs::create_directories(p, ec);
	return generic_dir(p);
}

inline std::string default_app_data_dir(const std::string& app) {
	return ensure_dir(default_app_data_root() / app);
}

inline std::string default_app_cache_dir(const std::string& app) {
#ifdef _WIN32
	return ensure_dir(default_app_cache_root() / app / "cache");
#else
	return ensure_dir(default_app_cache_root() / app);
#endif
}

// A host supplies these where the platform defaults do not apply (Android's files/cache
// directories, a sandboxed store); nullptr keeps the default.
struct path_hooks {
	std::function<std::string(const std::string& app)> app_data_dir;
	std::function<std::string(const std::string& app)> app_cache_dir;
};

inline std::string root_script_path(const jai::engine& eng) {
	std::string root = eng.root_source_file();
	if (root.empty() || root == "<script>") { return {}; }
	std::error_code ec;
	return generic_dir(fs::absolute(root, ec));
}

inline std::string root_script_dir(const jai::engine& eng) {
	std::string root = root_script_path(eng);
	std::error_code ec;
	return root.empty() ? generic_dir(fs::current_path(ec)) : generic_dir(fs::path(root).parent_path());
}

inline std::string current_script_path(const jai::engine& eng) {
	std::string cur = eng.current_source_file();
	if (cur.empty() || cur == "<script>") { return root_script_path(eng); }
	std::error_code ec;
	return generic_dir(fs::absolute(cur, ec));
}

inline std::string current_script_dir(const jai::engine& eng) {
	std::string cur = current_script_path(eng);
	return cur.empty() ? root_script_dir(eng) : generic_dir(fs::path(cur).parent_path());
}

inline void register_paths(jai::engine& eng, path_hooks hooks = {}) {
	jai::engine* e = &eng;
	eng.add_function("executable_dir", []() -> std::string { return executable_dir(); });
	eng.add_function("bin_dir", []() -> std::string { return executable_dir(); });
	eng.add_function("working_dir", []() -> std::string { std::error_code ec; return generic_dir(fs::current_path(ec)); });
	eng.add_function("executable_path", []() -> std::string { return generic_dir(executable_path()); });
	eng.add_function("root_script_dir", [e]() -> std::string { return root_script_dir(*e); });
	eng.add_function("root_script_path", [e]() -> std::string { return root_script_path(*e); });
	eng.add_function("current_script_dir", [e]() -> std::string { return current_script_dir(*e); });
	eng.add_function("current_script_path", [e]() -> std::string { return current_script_path(*e); });
	auto data = hooks.app_data_dir ? std::move(hooks.app_data_dir) : std::function<std::string(const std::string&)>(default_app_data_dir);
	auto cache = hooks.app_cache_dir ? std::move(hooks.app_cache_dir) : std::function<std::string(const std::string&)>(default_app_cache_dir);
	eng.add_function("app_data_dir", [data](const std::string& app) -> std::string { return data(app); });
	eng.add_function("app_cache_dir", [cache](const std::string& app) -> std::string { return cache(app); });
	eng.add_function("parent_dir", [](const std::string& path) -> std::string {
		fs::path p = fs::path(path).lexically_normal();
		if (p.filename().empty()) { p = p.parent_path(); }
		return generic_dir(p.parent_path());
	});
	eng.add_function("file_name", [](const std::string& path) -> std::string { return fs::path(path).filename().generic_string(); });
	eng.add_function("dir_exists", [](const std::string& path) -> bool {
		std::error_code ec;
		return fs::is_directory(path, ec);
	});
	eng.add_function("make_dirs", [](const std::string& path) -> bool {
		std::error_code ec;
		fs::create_directories(path, ec);
		return fs::is_directory(path, ec);
	});
	eng.add_function("list_dir", [](const std::string& path) -> std::vector<std::string> {
		std::vector<std::string> names;
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(path, ec)) { names.push_back(entry.path().filename().generic_string()); }
		std::sort(names.begin(), names.end());
		return names;
	});
}

} // namespace jai_runner
