// demoreel_chai: the ChaiScript 6.1 adapter for the demoreel shared host. Binds the shared demo_rng, host_log, ESC,
// itrunc/ifloor and the C math the scenes call (ChaiScript ships no math library), loads ports/chai/main.chai (which
// use()s the rest), and forwards the demo_* entry points.

#include "demoreel_host.hpp"

#include <chaiscript/chaiscript.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

std::string resolve_scenes_dir(const demoreel::host_options& opt) {
	return demoreel::locate_scenes_dir(opt.scenes_dir, opt.argv0,
#ifdef DEMOREEL_CHAI_SOURCE_DIR
		DEMOREEL_CHAI_SOURCE_DIR,
#else
		"",
#endif
		"demoreel_chai_scenes", "main.chai");
}

class chai_session : public demoreel::script_session {
public:
	chai_session(const demoreel::host_services& services, const demoreel::host_options& opt)
		: dir_(resolve_scenes_dir(opt)),
		  chai_(std::vector<std::string>{}, std::vector<std::string>{dir_ + "/"}) {
		using namespace chaiscript;
		chai_.add(user_type<demoreel::demo_rng>(), "Rng");
		chai_.add(constructor<demoreel::demo_rng(int64_t)>(), "Rng");
		chai_.add(fun(&demoreel::demo_rng::next), "next");
		chai_.add(fun(&demoreel::demo_rng::roll), "roll");
		chai_.add(fun(&demoreel::demo_rng::chance), "chance");
		chai_.add(fun(&demoreel::demo_rng::nextf), "nextf");

		chai_.add(fun([log = services.log](const std::string& line) { log(line); }), "host_log");
		chai_.add(fun([](double v) -> int64_t { return static_cast<int64_t>(v); }), "itrunc");
		chai_.add(fun([](double v) -> int64_t { return static_cast<int64_t>(std::floor(v)); }), "ifloor");
		chai_.add(fun([](double v) -> double { return std::sqrt(v); }), "sqrt");
		chai_.add(fun([](double v) -> double { return std::sin(v); }), "sin");
		chai_.add(fun([](double v) -> double { return std::cos(v); }), "cos");
		chai_.add(fun([](double v) -> double { return std::floor(v); }), "floor");
		chai_.add(fun([](double v) -> double { return std::exp(v); }), "exp");
		chai_.add(fun([](double v) -> double { return std::log(v); }), "log");
		chai_.add(fun([](double v) -> double { return std::log2(v); }), "log2");
		chai_.add(fun([](double v) -> double { return std::fabs(v); }), "fabs");
		chai_.add(fun([](double y, double x) -> double { return std::atan2(y, x); }), "atan2");
		chai_.add(fun([](double a, double b) -> double { return std::pow(a, b); }), "pow");
		chai_.add_global_const(const_var(std::string("\x1b")), "ESC");
	}

	void load() override { guarded([&] { chai_.eval_file(dir_ + "/main.chai"); }); }

	std::string load_kind() override { return "chai"; }

	void boot(int64_t w, int64_t h, int64_t seed, const std::string& backend, bool smoke, bool hud) override {
		guarded([&] {
			chai_.eval("demo_boot(" + std::to_string(w) + ", " + std::to_string(h) + ", " + std::to_string(seed) +
				", \"" + backend + "\", " + (smoke ? "true" : "false") + ", " + (hud ? "true" : "false") + ");");
		});
	}

	std::string frame(const demoreel::frame_args& a) override {
		std::string out;
		guarded([&] {
			if (!frame_fn_) {
				frame_fn_ = chai_.eval<std::function<std::string(double, double, const std::string&, double, double, double, double)>>("demo_frame");
			}
			out = frame_fn_(a.t, a.dt, a.key, a.fps, a.ms_sim, a.ms_draw, a.ms_other);
		});
		return out;
	}

	int64_t scene_index() override { return number("demo_scene_index()"); }
	double scene_time() override {
		double out = 0.0;
		guarded([&] { out = chaiscript::Boxed_Number(chai_.eval("demo_scene_time()")).get_as<double>(); });
		return out;
	}
	int64_t scene_count() override { return number("demo_scene_count()"); }
	std::string scene_name() override {
		std::string out;
		guarded([&] { out = chai_.eval<std::string>("demo_scene_name()"); });
		return out;
	}
	void jump(int64_t idx, double at_t) override {
		guarded([&] { chai_.eval<std::function<void(int64_t, double)>>("demo_jump")(idx, at_t); });
	}

	std::string stack_trace() override { return last_trace_; }

private:
	int64_t number(const std::string& expr) {
		int64_t out = 0;
		guarded([&] { out = chaiscript::Boxed_Number(chai_.eval(expr)).get_as<int64_t>(); });
		return out;
	}

	template <typename Fn>
	void guarded(Fn&& fn) {
		try {
			fn();
		} catch (const chaiscript::exception::eval_error& e) {
			last_trace_ = e.pretty_print();
			throw std::runtime_error(e.what());
		} catch (const chaiscript::Boxed_Value& bv) {
			std::string msg = "script threw a value";
			try { msg = chaiscript::boxed_cast<std::string>(bv); } catch (...) {}
			last_trace_ = msg;
			throw std::runtime_error(msg);
		}
	}

	std::string dir_;
	chaiscript::ChaiScript chai_;
	std::string last_trace_;
	std::function<std::string(double, double, const std::string&, double, double, double, double)> frame_fn_;
};

class chai_adapter : public demoreel::script_adapter {
public:
	std::string program_name() const override { return "demoreel_chai"; }
	std::string language() const override { return "ChaiScript 6.1"; }
	std::vector<std::string> backends() const override { return {"chai"}; }
	std::unique_ptr<demoreel::script_session> make_session(const std::string& /*backend*/,
			const demoreel::host_services& services, const demoreel::host_options& opt) override {
		return std::make_unique<chai_session>(services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	chai_adapter adapter;
	return demoreel::run_demoreel(argc, argv, adapter);
}
