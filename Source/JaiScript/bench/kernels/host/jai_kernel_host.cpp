// Minimal JaiScript host for the kernel suite: the runner's stdlib + clock_ms, plus add_one for
// the interop kernel.   jai_kernel_host [--backend=vm|interp] file.jai
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/properties/property_serialization.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
	std::string backend = "vm";
	std::string script;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a.rfind("--backend=", 0) == 0) { backend = a.substr(10); }
		else { script = a; }
	}
	if (script.empty()) {
		std::fprintf(stderr, "usage: jai_kernel_host [--backend=vm|interp] file.jai\n");
		return 2;
	}
	auto eng = jai::engine::make();
	eng->set_backend(backend == "interp" ? jai::backend_type::interpreter : jai::backend_type::vm);
	eng->execution_budget(0);
	jai::stdlib::register_all(eng);
	eng->add_function("clock_ms", []() -> jai::script_int {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	});
	eng->add_function("add_one", [](jai::script_int x) -> jai::script_int { return x + 1; });

	std::filesystem::path abs = std::filesystem::absolute(script);
	eng->add_include_path(abs.parent_path().string());
	std::ifstream in(abs, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	try {
		eng->execute_file_source(abs.string(), ss.str());
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "error: %s\n%s\n", ex.what(), eng->format_stack_trace().c_str());
		return 1;
	}
	return 0;
}
