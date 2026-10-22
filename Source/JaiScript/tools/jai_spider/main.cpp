// jai_spider - a web application host for JaiScript. `jai_spider app.jai` serves the app over HTTP/1.1: each request is
// handed to the handler the app gave spider::spin, on one thread; the app's modules reload on the request after they are
// saved, SQLite is a Database away, and files under --root are served as they are. The host is a consumer of the
// library: sockets and HTTP (http_server.hpp), the text forms of HTTP (http_text.hpp) and SQLite (database.hpp) live
// here, Request and Router are script (lib/spider.jai), Response is the library's spider::Response.
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include "../jaiscript/http_transport.hpp"
#include "../jaiscript/file_io.hpp"
#include "database.hpp"
#include "http_server.hpp"
#include "http_text.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_stop{false};   // Ctrl+C (host-exe state, not engine state)

#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
	if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
		g_stop.store(true);
		return TRUE;
	}
	return FALSE;
}
#else
void stop_signal(int) { g_stop.store(true); }
#endif

struct options {
	std::string app;
	std::string host = "127.0.0.1";
	int port = 8080;
	std::string root;
	std::string backend = "vm";
	std::string check = "strict";
	double budget = 30.0;
	spider_host::http_limits limits;
	bool allow_shutdown = false;
	bool quiet = false;
};

void print_usage() {
	std::puts(
		"jai_spider - a web application host for JaiScript\n"
		"\n"
		"usage:\n"
		"  jai_spider app.jai [options]\n"
		"\n"
		"The app hands its handler to spider::spin(handler) (spider::serve is the same call): a function of a\n"
		"spider::Request, or a spider::Router (lib/spider.jai has Request, Router and the html/json/redirect/\n"
		"file helpers; spider::Response is the library's). Every request reaches the app's imports first, so a\n"
		"saved edit applies to the next request; an edit that does not check is reported here and the app\n"
		"keeps serving the code it had. spider::catch / sling / get / post reach other servers.\n"
		"\n"
		"options:\n"
		"  --port N            port to listen on (default 8080; 0 picks a free one)\n"
		"  --host ADDR         IPv4 address to listen on (default 127.0.0.1; 0.0.0.0 for every interface)\n"
		"  --root DIR          serve the files under DIR for GET and HEAD before the app sees the request\n"
		"                      (default: the app's public/ folder when it exists)\n"
		"  --max-body N        largest request body in bytes, K or M suffix allowed (default 2M); larger is 413\n"
		"  --timeout S         seconds a request may take to arrive and a response to leave (default 30)\n"
		"  --budget S          execution budget per request in seconds (default 30; 0 = unlimited)\n"
		"  --backend vm|interp execution backend (default vm)\n"
		"  --check strict|warn|off\n"
		"                      static checker mode (default strict)\n"
		"  --allow-shutdown    POST /__spider/shutdown from this machine stops the server\n"
		"  --quiet             no request log\n"
		"  --version / --help\n"
		"\n"
		"Scripts run one request at a time on one thread; the network loop serves static files and\n"
		"keeps every connection moving between them. Ctrl+C stops the server.\n");
}

bool parse_size(const std::string& text, size_t& out) {
	if (text.empty()) { return false; }
	size_t multiplier = 1;
	std::string digits = text;
	const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
	if (last == 'K' || last == 'M' || last == 'G') {
		multiplier = last == 'K' ? 1024 : last == 'M' ? 1024 * 1024 : size_t{1024} * 1024 * 1024;
		digits.pop_back();
	}
	if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) { return false; }
	out = static_cast<size_t>(std::stoull(digits)) * multiplier;
	return true;
}

int parse_args(int argc, char** argv, options& opt) {
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto value_of = [&](const std::string& flag) -> std::string {
			if (a.size() > flag.size() && a[flag.size()] == '=') { return a.substr(flag.size() + 1); }
			if (i + 1 < argc) { return argv[++i]; }
			std::fprintf(stderr, "jai_spider: missing value for %s\n", flag.c_str());
			std::exit(2);
		};
		auto is_flag = [&](const char* flag) {
			const size_t n = std::strlen(flag);
			return a.compare(0, n, flag) == 0 && (a.size() == n || a[n] == '=');
		};
		try {
			if (is_flag("--port")) { opt.port = std::stoi(value_of("--port")); }
			else if (is_flag("--host")) { opt.host = value_of("--host"); }
			else if (is_flag("--root")) { opt.root = value_of("--root"); }
			else if (is_flag("--backend")) { opt.backend = value_of("--backend"); }
			else if (is_flag("--check")) { opt.check = value_of("--check"); }
			else if (is_flag("--budget")) { opt.budget = std::stod(value_of("--budget")); }
			else if (is_flag("--timeout")) { opt.limits.timeout_seconds = std::stod(value_of("--timeout")); }
			else if (is_flag("--max-body")) {
				if (!parse_size(value_of("--max-body"), opt.limits.max_body_bytes)) {
					std::fprintf(stderr, "jai_spider: --max-body takes a byte count (2097152, 512K, 2M)\n");
					return 2;
				}
			}
			else if (a == "--allow-shutdown") { opt.allow_shutdown = true; }
			else if (a == "--quiet") { opt.quiet = true; }
			else if (a == "--version" || a == "-v") { std::printf("jai_spider (JaiScript %s, SQLite %s)\n", jai::version().c_str(), sqlite3_libversion()); std::exit(0); }
			else if (a == "--help" || a == "-h" || a == "/?") { print_usage(); std::exit(0); }
			else if (!a.empty() && a[0] == '-') {
				std::fprintf(stderr, "jai_spider: unknown flag: %s\n", a.c_str());
				return 2;
			}
			else if (opt.app.empty()) { opt.app = a; }
			else {
				std::fprintf(stderr, "jai_spider: unexpected extra argument: %s\n", a.c_str());
				return 2;
			}
		} catch (const std::exception&) {
			std::fprintf(stderr, "jai_spider: bad value in %s\n", a.c_str());
			return 2;
		}
	}
	if (opt.backend == "interpreter") { opt.backend = "interp"; }
	if (opt.app.empty()) {
		print_usage();
		return 2;
	}
	if (opt.backend != "vm" && opt.backend != "interp") {
		std::fprintf(stderr, "jai_spider: unknown backend: %s (use vm or interp)\n", opt.backend.c_str());
		return 2;
	}
	if (opt.check != "strict" && opt.check != "warn" && opt.check != "off") {
		std::fprintf(stderr, "jai_spider: unknown check mode: %s (use strict, warn or off)\n", opt.check.c_str());
		return 2;
	}
	if (opt.port < 0 || opt.port > 65535) {
		std::fprintf(stderr, "jai_spider: --port takes 0 to 65535\n");
		return 2;
	}
	return -1;
}

struct console_colors {
	bool enabled = false;

	void init() {
#ifdef _WIN32
		HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
			enabled = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
		}
		SetConsoleOutputCP(CP_UTF8);
#else
		enabled = isatty(STDOUT_FILENO) != 0;
#endif
	}
	std::string paint(const char* code, const std::string& text) const { return enabled ? std::string("\x1b[") + code + "m" + text + "\x1b[0m" : text; }
	std::string red(const std::string& text) const { return paint("31", text); }
	std::string green(const std::string& text) const { return paint("32", text); }
	std::string yellow(const std::string& text) const { return paint("33", text); }
	std::string dim(const std::string& text) const { return paint("90", text); }
};

std::string html_text(std::string_view text) {
	std::string result;
	result.reserve(text.size());
	for (char c : text) {
		switch (c) {
			case '&': result += "&amp;"; break;
			case '<': result += "&lt;"; break;
			case '>': result += "&gt;"; break;
			case '"': result += "&quot;"; break;
			case '\'': result += "&#39;"; break;
			default: result += c;
		}
	}
	return result;
}

bool random_bytes(unsigned char* out, size_t count) {
#ifdef _WIN32
	return BCryptGenRandom(nullptr, out, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
	std::FILE* source = std::fopen("/dev/urandom", "rb");
	if (!source) { return false; }
	const bool ok = std::fread(out, 1, count, source) == count;
	std::fclose(source);
	return ok;
#endif
}

jai::script_value script_map_of(jai::engine* eng, std::vector<std::pair<std::string, jai::script_value>> entries) {
	jai::script_value result = jai::script_value::make_map(nullptr, nullptr, eng);
	auto& fields = result.as_map();
	for (auto& [key, value] : entries) { fields[jai::script_value(key, eng)] = std::move(value); }
	return result;
}

// The HTTP text forms the standard library leaves to a web host (lib/spider.jai builds Request on them)
void register_http_text(jai::engine& eng) {
	eng.add_function("parse_cookies", [](const std::string& header) { return spider_host::parse_cookies(header); });
	jai::engine* engine_pointer = &eng;
	eng.add_function("parse_multipart", [engine_pointer](const std::string& body, const std::string& content_type) {
		std::vector<spider_host::multipart_part> parts;
		if (!spider_host::parse_multipart(body, content_type, parts)) { throw std::runtime_error("parse_multipart: the body is not multipart/form-data with this boundary"); }
		jai::script_value result = jai::script_value::make_array(nullptr, engine_pointer);
		for (auto& part : parts) {
			result.as_array().push_back(script_map_of(engine_pointer, {
				{"name", jai::script_value(part.name, engine_pointer)},
				{"filename", jai::script_value(part.filename, engine_pointer)},
				{"content_type", jai::script_value(part.content_type, engine_pointer)},
				{"data", jai::script_value(std::move(part.data), engine_pointer)},
				{"is_file", jai::script_value(part.has_filename, engine_pointer)},
			}));
		}
		return result;
	});
	eng.add_function("random_token", [](jai::script_int byte_count) {
		if (byte_count < 1 || byte_count > 1024) { throw std::runtime_error("random_token takes 1 to 1024 bytes"); }
		std::vector<unsigned char> bytes(static_cast<size_t>(byte_count));
		if (!random_bytes(bytes.data(), bytes.size())) { throw std::runtime_error("random_token: the system random source failed"); }
		static const char hex[] = "0123456789abcdef";
		std::string result;
		for (unsigned char b : bytes) {
			result += hex[b >> 4];
			result += hex[b & 15];
		}
		return result;
	});
	eng.add_function("http_date", [](jai::script_int unix_seconds) { return spider_host::http_date(static_cast<std::time_t>(unix_seconds)); });
	eng.add_function("content_type_for", [](const std::string& path) { return spider_host::content_type_for(fs::path(path).extension().string()); });
}

bool is_loopback(const std::string& address) { return address.rfind("127.", 0) == 0 || address == "::1"; }

class app_host {
public:
	app_host(const options& chosen, const console_colors& palette) :
		opt_(chosen),
		colors_(palette) {
	}

	bool start() {
		std::error_code ec;
		app_path_ = fs::weakly_canonical(fs::absolute(opt_.app, ec), ec);
		if (!fs::is_regular_file(app_path_, ec)) {
			std::cerr << colors_.red("jai_spider: no such file: " + opt_.app) << "\n";
			return false;
		}
		app_dir_ = app_path_.parent_path();
		if (!opt_.root.empty()) {
			static_root_ = fs::weakly_canonical(fs::absolute(opt_.root, ec), ec);
			if (!fs::is_directory(static_root_, ec)) {
				std::cerr << colors_.red("jai_spider: --root is not a folder: " + opt_.root) << "\n";
				return false;
			}
		} else if (fs::is_directory(app_dir_ / "public", ec)) {
			static_root_ = fs::weakly_canonical(app_dir_ / "public", ec);
		}
		eng_ = jai::engine::make();
		eng_->set_backend(opt_.backend == "interp" ? jai::backend_type::interpreter : jai::backend_type::vm);
		eng_->static_checking(opt_.check == "off" ? jai::check_mode::off : opt_.check == "warn" ? jai::check_mode::warn : jai::check_mode::strict);
		eng_->execution_budget(opt_.budget);
		eng_->import_recheck_interval(0);   // every request asks whether a module changed: a save applies to the next request
		jai::stdlib::register_all(eng_);
		jai::stdlib::register_web(eng_);
		jai_runner::register_http_client(*eng_);
		jai_runner::register_file_io(*eng_);
		register_http_text(*eng_);
		spider_host::register_database(*eng_, app_dir_);
		eng_->add_include_path(app_dir_.generic_string());
		eng_->add_include_path(library_dir().generic_string());
		try {
			eng_->execute("import \"spider.jai\";");
		} catch (const std::exception& e) {
			std::cerr << colors_.red("jai_spider: lib/spider.jai did not load:") << "\n" << e.what() << "\n";
			return false;
		}
		std::string escaped;
		for (char c : app_path_.generic_string()) {
			if (c == '"' || c == '\\') { escaped += '\\'; }
			escaped += c;
		}
		reload_source_ = "import(\"" + escaped + "\")";
		reload();
		return true;
	}

	spider_host::http_response handle(const spider_host::http_request& request) {
		const auto start = std::chrono::steady_clock::now();
		spider_host::http_response response;
		bool from_disk = false;
		if (opt_.allow_shutdown && request.path == "/__spider/shutdown" && request.method == "POST" && is_loopback(request.remote_address)) {
			response = spider_host::plain_response(200, "jai_spider: stopping");
			response.stop_server = true;
			response.close = true;
		} else if (auto file = static_file(request)) {
			response = std::move(*file);
			from_disk = true;
		} else {
			response = run_app(request);
		}
		if (!opt_.quiet) {
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			char timing[32];
			std::snprintf(timing, sizeof(timing), "%.2f ms", ms);
			const std::string status = std::to_string(response.status);
			const std::string painted = response.status >= 500 ? colors_.red(status) : response.status >= 400 ? colors_.yellow(status) : colors_.green(status);
			std::cout << request.method << " " << request.target << " " << painted << " " << colors_.dim(std::string(timing) + (from_disk ? " (file)" : "")) << "\n";
		}
		std::cout.flush();
		return response;
	}

	const fs::path& root() const { return static_root_; }

	// the server whose body limit spider::spin's max_body moves
	void attach(spider_host::http_server& a_server) { server_ = &a_server; }

private:

	// the checkout's lib/ while it is there, so an edit to spider.jai applies like an edit to the app; else the copy
	// the build put beside the executable
	fs::path library_dir() const {
		std::error_code ec;
		const fs::path source = fs::path(JAI_SPIDER_SOURCE_LIBRARY_DIR);
		if (fs::is_regular_file(source / "spider.jai", ec)) { return source; }
		return fs::path(jai::engine::executable_path()).parent_path() / "jai_spider_lib";
	}

	// reaches the app's import: a changed module is checked, then runs; a rejected edit keeps the code the app had
	void reload() {
		try {
			jai::script_value ran = eng_->execute(reload_source_);
			const bool any_ran = ran.is_bool() && ran.as_bool();
			if (any_ran) {
				std::string names;
				for (const std::string& name : modules_changed_since_last_run()) { names += (names.empty() ? "" : ", ") + name; }
				if (loaded_) {
					std::cout << colors_.green("jai_spider: reloaded " + (names.empty() ? app_path_.filename().string() : names)) << "\n";
				} else if (!failure_.empty()) {
					std::cout << colors_.green("jai_spider: the app loads") << "\n";
				}
			}
			failure_.clear();
			loaded_ = loaded_ || any_ran;
		} catch (const std::exception& e) {
			const std::string text = e.what();
			if (text != failure_) {
				std::cerr << colors_.red(loaded_ ? "jai_spider: the edit was rejected; serving the code the app had:" : "jai_spider: the app did not load:") << "\n" << text << "\n";
				std::cerr.flush();
			}
			failure_ = text;
		}
	}

	// the imported files whose stamp moved since the last run, by file name; asked only after a module ran
	std::vector<std::string> modules_changed_since_last_run() {
		std::vector<std::string> changed;
		for (const std::string& path : eng_->get_imported_files()) {
			std::error_code ec;
			const auto stamp = std::make_pair(fs::last_write_time(path, ec), fs::file_size(path, ec));
			auto [it, inserted] = module_stamps_.emplace(path, stamp);
			if (!inserted && it->second != stamp) { changed.push_back(fs::path(path).filename().string()); }
			it->second = stamp;
		}
		return changed;
	}

	// the error and its trace go to the log; the page shows them to a browser on this machine only
	spider_host::http_response error_page(const spider_host::http_request& request, const std::string& detail) {
		spider_host::http_response response;
		response.status = 500;
		response.set_header("Content-Type", "text/html; charset=utf-8");
		response.set_header("Cache-Control", "no-store");
		response.body = "<!doctype html><meta charset=\"utf-8\"><title>500 Internal Server Error</title>"
			"<style>body{font:15px/1.5 system-ui,sans-serif;margin:2rem;color:#222}pre{background:#f6f6f6;padding:1rem;overflow:auto;"
			"border-left:4px solid #c33}</style><h1>500 Internal Server Error</h1>";
		if (is_loopback(request.remote_address)) { response.body += "<pre>" + html_text(detail) + "</pre>"; }
		response.body += "\n";
		return response;
	}

	spider_host::http_response run_app(const spider_host::http_request& request) {
		reload();
		if (!loaded_) { return error_page(request, "The app did not load:\n" + failure_); }
		apply_spin_options();
		jai::script_value raw = script_map_of(eng_.get(), {
			{"method", jai::script_value(request.method, eng_.get())},
			{"target", jai::script_value(request.target, eng_.get())},
			{"path", jai::script_value(request.path, eng_.get())},
			{"query_string", jai::script_value(request.query_string, eng_.get())},
			{"version", jai::script_value(request.version, eng_.get())},
			{"headers", eng_->make_value(request.headers)},
			{"body", jai::script_value(request.body, eng_.get())},
			{"remote_addr", jai::script_value(request.remote_address, eng_.get())},
		});
		std::string error;
		try {
			jai::script_value serve = eng_->get_variable("spider_host_serve");
			auto result = serve.as_function()({raw});
			if (result) { return response_from(request, result.value()); }
			error = eng_->format_error(result) + "\n" + eng_->format_stack_trace();
		} catch (const std::exception& e) {
			error = e.what();
		}
		while (!error.empty() && (error.back() == '\n' || error.back() == ' ')) { error.pop_back(); }
		std::cerr << colors_.red("jai_spider: " + request.method + " " + request.path + " failed:") << "\n" << error << "\n";
		std::cerr.flush();
		return error_page(request, error);
	}

	// spider::spin(handler, {"budget": s, "max_body": n}) over --budget and --max-body, from the next request on
	void apply_spin_options() {
		double budget = opt_.budget;
		size_t max_body = opt_.limits.max_body_bytes;
		try {
			auto spun = eng_->get_variable("spider_host_options").as_function()({});
			if (spun && spun.value().is_map()) {
				for (const auto& [key, value] : spun.value().as_map()) {
					const std::string name = key.as_string();
					const jai::script_value& setting = value.deref();
					if (name == "budget" && (setting.is_int() || setting.is_float())) { budget = setting.is_int() ? static_cast<double>(setting.as_int()) : setting.as_float(); }
					if (name == "max_body" && setting.is_int() && setting.as_int() > 0) { max_body = static_cast<size_t>(setting.as_int()); }
				}
			}
		} catch (const std::exception&) {
		}
		eng_->execution_budget(budget);
		if (server_) { server_->max_body(max_body); }
	}

	jai::script_value field_of(const jai::script_value& parts, const char* key) const {
		const auto& fields = parts.as_map();
		auto it = fields.find(jai::script_value(std::string(key), eng_.get()));
		if (it == fields.end()) { throw std::runtime_error(std::string("spider_host_serve's answer has no '") + key + "'"); }
		return it->second;
	}

	spider_host::http_response response_from(const spider_host::http_request& request, const jai::script_value& parts) {
		spider_host::http_response response;
		response.status = static_cast<int>(field_of(parts, "status").as_int());
		if (response.status < 100 || response.status > 999) {
			throw std::runtime_error("Response status " + std::to_string(response.status) + " is not an HTTP status");
		}
		std::string file;
		for (const auto& [name, text] : field_of(parts, "headers").as_map()) {
			if (spider_host::iequals(name.as_string(), "x-sendfile")) {
				file = text.as_string();
				continue;
			}
			response.set_header(name.as_string(), text.as_string());
		}
		field_of(parts, "cookies").unchecked_array_node()->for_each(eng_.get(), [&](const jai::script_value& cookie) {
			response.headers.emplace_back("Set-Cookie", cookie.as_string());
		});
		if (!file.empty()) {
			fs::path path = fs::path(file);
			if (path.is_relative()) { path = app_dir_ / path; }
			auto served = file_response(path, request, response.status);
			if (!served) {
				std::cerr << colors_.red("jai_spider: file(\"" + file + "\"): no such file") << "\n";
				return spider_host::plain_response(404, "Not Found");
			}
			for (auto& [name, text] : response.headers) { served->set_header(name, text); }
			return std::move(*served);
		}
		response.body = field_of(parts, "body").as_string();
		if (!response.body.empty() && !response.has_header("content-type")) { response.set_header("Content-Type", "text/html; charset=utf-8"); }
		return response;
	}

	// a file under --root for GET and HEAD; dot-files, ".." and anything outside the root are never served
	std::optional<spider_host::http_response> static_file(const spider_host::http_request& request) {
		if (static_root_.empty() || (request.method != "GET" && request.method != "HEAD")) { return std::nullopt; }
		fs::path candidate = static_root_;
		size_t start = 1;
		while (start <= request.path.size()) {
			size_t end = request.path.find('/', start);
			if (end == std::string::npos) { end = request.path.size(); }
			const std::string segment = request.path.substr(start, end - start);
			if (!segment.empty()) {
				if (segment[0] == '.' || segment.find_first_of("\\:") != std::string::npos) { return std::nullopt; }
				candidate /= fs::path(reinterpret_cast<const char8_t*>(segment.c_str()));
			}
			start = end + 1;
		}
		std::error_code ec;
		if (fs::is_directory(candidate, ec)) { candidate /= "index.html"; }
		if (!fs::is_regular_file(candidate, ec)) { return std::nullopt; }
		const fs::path resolved = fs::weakly_canonical(candidate, ec);
		const std::string root_text = static_root_.generic_string() + "/";
		if (ec || resolved.generic_string().rfind(root_text, 0) != 0) { return std::nullopt; }
		return file_response(resolved, request, 200);
	}

	std::optional<spider_host::http_response> file_response(const fs::path& path, const spider_host::http_request& request, int status) {
		std::error_code ec;
		if (!fs::is_regular_file(path, ec)) { return std::nullopt; }
		const uint64_t size = fs::file_size(path, ec);
		const auto modified = fs::last_write_time(path, ec);
#ifdef _MSC_VER
		const auto modified_utc = std::chrono::clock_cast<std::chrono::system_clock>(modified);
#else
		const auto modified_utc = fs::file_time_type::clock::to_sys(modified);
#endif
		const auto since_epoch = std::chrono::duration_cast<std::chrono::seconds>(modified_utc.time_since_epoch()).count();
		char tag[64];
		std::snprintf(tag, sizeof(tag), "W/\"%llx-%llx\"", static_cast<unsigned long long>(size),
			static_cast<unsigned long long>(modified.time_since_epoch().count()));
		const std::string last_modified = spider_host::http_date(static_cast<std::time_t>(since_epoch));
		spider_host::http_response response;
		response.status = status;
		response.set_header("Content-Type", spider_host::content_type_for(path.extension().string()));
		response.set_header("Last-Modified", last_modified);
		response.set_header("ETag", tag);
		response.set_header("Cache-Control", "no-cache");
		if (status == 200) {
			const std::string* match = request.header("if-none-match");
			const std::string* since = request.header("if-modified-since");
			if ((match && *match == tag) || (!match && since && *since == last_modified)) {
				response.status = 304;
				return response;
			}
		}
		response.file_path = path.string();
		response.file_size = size;
		return response;
	}

	const options& opt_;
	const console_colors& colors_;
	std::shared_ptr<jai::engine> eng_;
	fs::path app_path_;
	fs::path app_dir_;
	fs::path static_root_;
	std::string reload_source_;
	std::string failure_;
	std::map<std::string, std::pair<fs::file_time_type, uintmax_t>> module_stamps_;
	spider_host::http_server* server_ = nullptr;
	bool loaded_ = false;
};

} // namespace

int main(int argc, char** argv) {
	options opt;
	if (int code = parse_args(argc, argv, opt); code >= 0) { return code; }
	console_colors colors;
	colors.init();
#ifdef _WIN32
	SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
	std::signal(SIGINT, stop_signal);
	std::signal(SIGTERM, stop_signal);
	std::signal(SIGPIPE, SIG_IGN);
#endif
	try {
		app_host host(opt, colors);
		if (!host.start()) { return 1; }
		spider_host::http_server server(opt.limits);
		std::string error;
		if (!server.listen(opt.host, opt.port, error)) {
			std::cerr << colors.red("jai_spider: " + error) << "\n";
			return 1;
		}
		host.attach(server);
		if (!opt.quiet) { std::cout << colors.dim("/\\(o.o)/\\  jai_spider " + jai::version()) << "\n"; }
		std::cout << "jai_spider: spinning " << opt.app << " on http://" << opt.host << ":" << server.port() << "/";
		if (!host.root().empty()) { std::cout << " (files from " << host.root().string() << ")"; }
		std::cout << "\n" << colors.dim("jai_spider: edits apply on the next request; Ctrl+C stops") << std::endl;
		server.on_rejected([&](const std::string& remote, int status, const std::string& why) {
			if (!opt.quiet) { std::cout << colors.yellow("jai_spider: " + std::to_string(status) + " to " + remote + ": " + why) << std::endl; }
		});
		server.run(g_stop, [&](const spider_host::http_request& request) { return host.handle(request); });
		std::cout << "jai_spider: stopped" << std::endl;
	} catch (const std::exception& e) {
		std::cerr << "jai_spider: " << e.what() << "\n";
		return 1;
	}
	return 0;
}
