// gloom_chai: ChaiScript adapter for the GLOOM shared host. Binds the host API
// per REFERENCE.md section 2 into a chaiscript::ChaiScript engine, loads the
// ports/chai/scripts game (main.chai use()s the rest), and forwards the six
// gloom_* entry points. ChaiScript core has no math library, so the C-math the
// game needs (sqrt/cos/sin/atan2/floor) is bound here alongside the host API.

#include "gloom_host.hpp"

#include <chaiscript/chaiscript.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

std::string resolve_scripts_dir(const gloom::host_options& opt) {
	return gloom::locate_scripts_dir(opt.scripts_dir, opt.argv0,
#ifdef GLOOM_SOURCE_SCRIPTS_DIR
		GLOOM_SOURCE_SCRIPTS_DIR,
#else
		"",
#endif
		"gloom_scripts_chai", "main.chai");
}

class chai_session : public gloom::script_session {
public:
	chai_session(const std::string& backend, const gloom::host_services& services,
	             const gloom::host_options& opt)
		: opt_(opt), scripts_dir_(resolve_scripts_dir(opt)),
		  chai_(std::vector<std::string>{}, std::vector<std::string>{scripts_dir_ + "/"}) {
		using namespace chaiscript;

		chai_.add(user_type<gloom::gloom_rng>(), "Rng");
		chai_.add(constructor<gloom::gloom_rng(int64_t)>(), "Rng");
		chai_.add(fun(&gloom::gloom_rng::next), "next");
		chai_.add(fun(&gloom::gloom_rng::roll), "roll");
		chai_.add(fun(&gloom::gloom_rng::chance), "chance");
		chai_.add(fun(&gloom::gloom_rng::nextf), "nextf");
		chai_.add(fun(&gloom::gloom_rng::state), "state");

		chai_.add(fun([log = services.log](const std::string& line) { log(line); }), "host_log");
		chai_.add(fun([key_down = services.key_down](const std::string& name) -> bool {
			return key_down(name);
		}), "key_down");
		chai_.add(fun([](double v) -> int64_t { return static_cast<int64_t>(v); }), "itrunc");
		chai_.add(fun([](double v) -> int64_t { return static_cast<int64_t>(std::floor(v)); }), "ifloor");
		chai_.add(fun([](int64_t cp) -> std::string { return gloom::utf8_encode(cp); }), "utf8");
		// ChaiScript has no bundled math lib; these are the game's whitelist.
		chai_.add(fun([](double v) -> double { return std::sqrt(v); }), "sqrt");
		chai_.add(fun([](double v) -> double { return std::cos(v); }), "cos");
		chai_.add(fun([](double v) -> double { return std::sin(v); }), "sin");
		chai_.add(fun([](double y, double x) -> double { return std::atan2(y, x); }), "atan2");
		chai_.add(fun([](double v) -> double { return std::floor(v); }), "floor");

		chai_.add_global_const(const_var(std::string("\x1b")), "ESC");
		chai_.add_global_const(const_var(opt.seed), "HOST_SEED");
		chai_.add_global_const(const_var(opt.smoke), "HOST_SMOKE");
		chai_.add_global_const(const_var(opt.ticks), "HOST_TICKS");
		chai_.add_global_const(const_var(opt.workers), "HOST_WORKERS");
		chai_.add_global_const(const_var(opt.god), "HOST_GOD");
		chai_.add_global_const(const_var(backend), "HOST_BACKEND");
		chai_.add_global_const(const_var(gloom::pix_mode_index(opt.pix)), "HOST_PIX");
	}

	void load_scripts() override {
		guarded([&] { chai_.eval_file(scripts_dir_ + "/main.chai"); });
	}

	void force_autopilot() override {
		guarded([&] { chai_.eval("gloom_force_autopilot();"); });
	}

	void boot(int64_t w, int64_t h) override {
		guarded([&] { chai_.eval("gloom_boot(" + std::to_string(w) + ", " + std::to_string(h) + ");"); });
	}

	std::string frame(double dt, const std::string& key,
	                  double fps, double ms_sim, double ms_draw) override {
		std::string out;
		guarded([&] {
			if (!fn_frame_) {
				fn_frame_ = chai_.eval<std::function<std::string(double, const std::string&,
					double, double, double)>>("gloom_frame");
			}
			out = fn_frame_(dt, key, fps, ms_sim, ms_draw);
		});
		return out;
	}

	int64_t state_hash() override {
		int64_t out = 0;
		guarded([&] {
			chaiscript::Boxed_Value bv = chai_.eval("gloom_state_hash()");
			out = chaiscript::Boxed_Number(bv).get_as<int64_t>();
		});
		return out;
	}

	bool wants_quit() override {
		bool out = true;
		guarded([&] { out = chai_.eval<bool>("gloom_wants_quit()"); });
		return out;
	}

	void summary() override {
		guarded([&] { chai_.eval("gloom_summary();"); });
	}

	std::string stack_trace() override { return last_trace_; }

private:
	template <typename Fn>
	void guarded(Fn&& fn) {
		try {
			fn();
		} catch (const chaiscript::exception::eval_error& e) {
			last_trace_ = e.pretty_print();
			throw std::runtime_error(e.what());   // includes file/line/column
		} catch (const chaiscript::Boxed_Value& bv) {
			// uncaught script-side throw("...")
			std::string msg = "script threw a value";
			try { msg = chaiscript::boxed_cast<std::string>(bv); } catch (...) {}
			last_trace_ = msg;
			throw std::runtime_error(msg);
		}
	}

	gloom::host_options opt_;
	std::string scripts_dir_;
	chaiscript::ChaiScript chai_;
	std::string last_trace_;
	std::function<std::string(double, const std::string&, double, double, double)> fn_frame_;
};

class chai_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_chai"; }
	std::string language() const override { return "ChaiScript"; }
	std::vector<std::string> backends() const override { return {"chai"}; }
	std::unique_ptr<gloom::script_session> make_session(const std::string& backend,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input — services.key_down is already gated*/) override {
		return std::make_unique<chai_session>(backend, services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	chai_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
