// gloom_lua: Lua 5.4 / sol2 adapter for the GLOOM shared host. Binds the host
// API (REFERENCE.md section 2) into a sol::state — the shared gloom_rng as a
// usertype (int64 passthrough via lua_Integer, no double round-trip), host
// services as free functions, HOST_* flags as globals — loads the Lua game
// from ports/lua/scripts, and forwards the six gloom_* entry points.

#include "gloom_host.hpp"

#include <sol/sol.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

class lua_session : public gloom::script_session {
public:
	lua_session(const std::string& backend, const gloom::host_services& services,
	            const gloom::host_options& opt)
		: opt_(opt), services_(services) {
		lua_.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
			sol::lib::table, sol::lib::coroutine, sol::lib::package, sol::lib::debug);

		lua_.new_usertype<gloom::gloom_rng>("Rng",
			sol::constructors<gloom::gloom_rng(int64_t)>(),
			"next", &gloom::gloom_rng::next,
			"roll", &gloom::gloom_rng::roll,
			"chance", &gloom::gloom_rng::chance,
			"nextf", &gloom::gloom_rng::nextf,
			"state", &gloom::gloom_rng::state);

		lua_["host_log"] = [log = services_.log](const std::string& line) { log(line); };
		lua_["key_down"] = [key_down = services_.key_down](const std::string& name) -> bool {
			return key_down(name);
		};
		lua_["itrunc"] = [](double v) -> int64_t { return static_cast<int64_t>(v); };
		lua_["ifloor"] = [](double v) -> int64_t { return static_cast<int64_t>(std::floor(v)); };
		lua_["utf8"] = [](int64_t cp) -> std::string { return gloom::utf8_encode(cp); };
		lua_["ESC"] = std::string("\x1b");
		lua_["HOST_SEED"] = opt.seed;
		lua_["HOST_SMOKE"] = opt.smoke;
		lua_["HOST_TICKS"] = opt.ticks;
		lua_["HOST_WORKERS"] = opt.workers;
		lua_["HOST_GOD"] = opt.god;
		lua_["HOST_BACKEND"] = backend;
		lua_["HOST_PIX"] = gloom::pix_mode_index(opt.pix);
	}

	void load_scripts() override {
		std::string dir = gloom::locate_scripts_dir(opt_.scripts_dir, opt_.argv0,
#ifdef GLOOM_SOURCE_SCRIPTS_DIR
			GLOOM_SOURCE_SCRIPTS_DIR,
#else
			"",
#endif
			"gloom_scripts_lua", "main.lua");
		lua_["package"]["path"] = dir + "/?.lua";
		sol::protected_function_result result = lua_.safe_script_file(
			dir + "/main.lua", sol::script_pass_on_error);
		if (!result.valid()) { throw_lua_error(result); }
		// every entry-point call reports a full Lua backtrace on error
		sol::protected_function::set_default_handler(sol::object(lua_["debug"]["traceback"]));
	}

	void force_autopilot() override { call("gloom_force_autopilot"); }

	void boot(int64_t w, int64_t h) override { call("gloom_boot", w, h); }

	std::string frame(double dt, const std::string& key,
	                  double fps, double ms_sim, double ms_draw) override {
		return call("gloom_frame", dt, key, fps, ms_sim, ms_draw).get<std::string>();
	}

	int64_t state_hash() override { return call("gloom_state_hash").get<int64_t>(); }

	bool wants_quit() override { return call("gloom_wants_quit").get<bool>(); }

	void summary() override { call("gloom_summary"); }

	std::string stack_trace() override { return last_trace_; }

private:
	template <typename... Args>
	sol::protected_function_result call(const char* fn_name, Args&&... args) {
		sol::protected_function fn = lua_[fn_name];
		sol::protected_function_result result = fn(std::forward<Args>(args)...);
		if (!result.valid()) { throw_lua_error(result); }
		return result;
	}

	[[noreturn]] void throw_lua_error(sol::protected_function_result& result) {
		sol::error err = result;
		last_trace_ = err.what();
		throw std::runtime_error(err.what());
	}

	gloom::host_options opt_;
	gloom::host_services services_;
	sol::state lua_;
	std::string last_trace_;
};

class lua_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_lua"; }
	std::string language() const override { return "Lua/sol2"; }
	std::vector<std::string> backends() const override { return {"lua"}; }
	std::string title() const override {
		return "gloom_lua - GLOOM, a terminal raycast shooter (Lua 5.4 / sol2 port)";
	}
	std::unique_ptr<gloom::script_session> make_session(const std::string& backend,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input — services.key_down is already gated*/) override {
		return std::make_unique<lua_session>(backend, services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	lua_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
