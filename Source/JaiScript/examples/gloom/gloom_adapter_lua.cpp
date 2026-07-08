// gloom_lua: Lua/sol2 adapter for the GLOOM shared host. SCAFFOLD ONLY —
// it compiles and links against Lua + sol2 (proving the opt-in CMake plumbing)
// and errors at script-load until the port lands. See PORTING.md for the
// contract and gloom_adapter_jai.cpp for the reference adapter shape.

#include "gloom_host.hpp"

#include <sol/sol.hpp>

#include <stdexcept>

namespace {

class lua_session : public gloom::script_session {
public:
	lua_session(const gloom::host_services& services, const gloom::host_options& opt)
		: opt_(opt), services_(services) {
		lua_.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
		// TODO(port): bind the host API per REFERENCE.md section 2 —
		// gloom::gloom_rng as "Rng", host_log/key_down from services_,
		// itrunc/ifloor, utf8 via gloom::utf8_encode, ESC, HOST_* globals
		// (HOST_PIX via gloom::pix_mode_index(opt.pix)).
	}

	void load_scripts() override {
		// TODO(port): gloom::locate_scripts_dir(opt_.scripts_dir, opt_.argv0,
		//     GLOOM_SOURCE_SCRIPTS_DIR, "gloom_scripts_lua", "main.lua") then dofile.
		throw std::runtime_error("Lua port not yet implemented (see examples/gloom/PORTING.md)");
	}

	void force_autopilot() override {}
	void boot(int64_t, int64_t) override {}
	std::string frame(double, const std::string&, double, double, double) override { return ""; }
	int64_t state_hash() override { return 0; }
	bool wants_quit() override { return true; }
	void summary() override {}

private:
	gloom::host_options opt_;
	gloom::host_services services_;
	sol::state lua_;
};

class lua_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_lua"; }
	std::string language() const override { return "Lua/sol2"; }
	std::vector<std::string> backends() const override { return {"lua"}; }
	std::unique_ptr<gloom::script_session> make_session(const std::string& /*backend*/,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input*/) override {
		return std::make_unique<lua_session>(services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	lua_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
