// jaiscript_lsp: the JaiScript language server. Speaks the Language Server Protocol over
// stdin/stdout and answers from the engine's own lexer, parser and strict checker.
//
//   jaiscript_lsp [--host=runner|stdlib|bare] [--include=<dir>]... [--log]
//
// --host picks the registration surface scripts are checked against: `runner` (default) is the
// jaiscript runner's (stdlib, file IO, console, networking, ARGS), `stdlib` is
// jai::stdlib::register_all alone, `bare` is a fresh engine.

#include "server.hpp"

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>

std::shared_ptr<jai::engine> jaiscript_runner_host_engine();

namespace {

std::shared_ptr<jai::engine> make_engine(const std::string& host) {
	std::shared_ptr<jai::engine> eng;
	if (host == "bare") {
		eng = jai::engine::make();
	} else if (host == "stdlib") {
		eng = jai::engine::make();
		jai::stdlib::register_all(eng);
	} else {
		eng = jaiscript_runner_host_engine();
	}
	eng->static_checking(jai::check_mode::off);
	eng->jaibite_cache(false);
	return eng;
}

void usage() {
	std::cout << "jaiscript_lsp - the JaiScript language server (LSP over stdio)\n"
	             "  --host=runner|stdlib|bare   registration surface to check against (default runner)\n"
	             "  --include=<dir>             extra include path for import/include resolution (repeatable)\n"
	             "  --log                       log requests and timings to stderr\n";
}

} // namespace

int main(int argc, char** argv) {
	std::string host = "runner";
	std::vector<std::string> includes;
	bool log = false;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a.rfind("--host=", 0) == 0) { host = a.substr(7); }
		else if (a.rfind("--include=", 0) == 0) { includes.push_back(a.substr(10)); }
		else if (a == "--log") { log = true; }
		else if (a == "--stdio") {}
		else if (a == "--help" || a == "-h") { usage(); return 0; }
		else if (a == "--version") { std::cout << "jaiscript_lsp 0.5.0-beta\n"; return 0; }
	}
	jai_lsp::server s(make_engine, host, includes, log);
	int code = s.run();
	std::fflush(stdout);
	std::_Exit(code);   // the stdin reader may still be blocked in a read: skip teardown rather than wait on its stream lock
}
