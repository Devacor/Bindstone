// demoreel_lua: the Lua 5.4 / sol2 adapter for the demoreel shared host. Binds the
// shared demo_rng as a usertype (int64 passthrough via lua_Integer), host_log and
// ESC, loads ports/lua/main.lua, and forwards the demo_* entry points. Hot reload
// re-runs every scene module (main.lua's globals, the live Reel included, persist).

#include "demoreel_host.hpp"

#include <sol/sol.hpp>

#include <stdexcept>
#include <string>

namespace {

const char* kLuaModules[] = {
	"util", "reel", "plasma", "starfield", "tunnel", "donut", "julia", "lava", "pipes", "fire",
	"sand", "boids", "kinstein", "voxelspace", "marcher", "cloth", "chrome", "skull", "finale",
};

class lua_session : public demoreel::script_session {
public:
	lua_session(const demoreel::host_services& services, const demoreel::host_options& opt)
		: opt_(opt) {
		lua_.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
			sol::lib::table, sol::lib::package, sol::lib::debug);
		lua_.new_usertype<demoreel::demo_rng>("Rng",
			sol::constructors<demoreel::demo_rng(int64_t)>(),
			"next", &demoreel::demo_rng::next,
			"roll", &demoreel::demo_rng::roll,
			"chance", &demoreel::demo_rng::chance,
			"nextf", &demoreel::demo_rng::nextf);
		lua_["host_log"] = [log = services.log](const std::string& line) { log(line); };
		lua_["ESC"] = std::string("\x1b");
	}

	void load() override {
		dir_ = demoreel::locate_scenes_dir(opt_.scenes_dir, opt_.argv0,
#ifdef DEMOREEL_LUA_SOURCE_DIR
			DEMOREEL_LUA_SOURCE_DIR,
#else
			"",
#endif
			"demoreel_lua_scenes", "main.lua");
		lua_["package"]["path"] = dir_ + "/?.lua";
		sol::protected_function_result result = lua_.safe_script_file(dir_ + "/main.lua", sol::script_pass_on_error);
		if (!result.valid()) { throw_lua_error(result); }
		sol::protected_function::set_default_handler(sol::object(lua_["debug"]["traceback"]));
		demo_frame_ = lua_["demo_frame"];
	}

	std::string load_kind() override { return "lua"; }

	void boot(int64_t w, int64_t h, int64_t seed, const std::string& backend, bool smoke, bool hud) override {
		call("demo_boot", w, h, seed, backend, smoke, hud);
	}

	std::string frame(const demoreel::frame_args& a) override {
		sol::protected_function_result result = demo_frame_(a.t, a.dt, a.key, a.fps, a.ms_sim, a.ms_draw, a.ms_other);
		if (!result.valid()) { throw_lua_error(result); }
		return result.get<std::string>();
	}

	int64_t scene_index() override { return call("demo_scene_index").get<int64_t>(); }
	double scene_time() override { return call("demo_scene_time").get<double>(); }
	int64_t scene_count() override { return call("demo_scene_count").get<int64_t>(); }
	std::string scene_name() override { return call("demo_scene_name").get<std::string>(); }
	void jump(int64_t idx, double at_t) override { call("demo_jump", idx, at_t); }

	bool reload() override {
		for (const char* m : kLuaModules) {
			sol::protected_function_result result = lua_.safe_script_file(dir_ + "/" + m + ".lua", sol::script_pass_on_error);
			if (!result.valid()) { throw_lua_error(result); }
		}
		call("demo_reload");
		return true;
	}

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

	demoreel::host_options opt_;
	sol::state lua_;
	sol::protected_function demo_frame_;
	std::string dir_;
	std::string last_trace_;
};

class lua_adapter : public demoreel::script_adapter {
public:
	std::string program_name() const override { return "demoreel_lua"; }
	std::string language() const override { return "Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR " / sol2"; }
	std::vector<std::string> backends() const override { return {"lua"}; }
	std::unique_ptr<demoreel::script_session> make_session(const std::string& /*backend*/,
			const demoreel::host_services& services, const demoreel::host_options& opt) override {
		return std::make_unique<lua_session>(services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	lua_adapter adapter;
	return demoreel::run_demoreel(argc, argv, adapter);
}
