// jai_gloom: the JaiScript adapter for the GLOOM shared host — the reference
// implementation of the PORTING.md contract. Binds the host API into a
// jai::engine, imports scripts/main.jai, and forwards the six gloom_* entry
// points. --smoke runs both engine backends (interpreter, vm) and the shared
// host byte-compares their frame streams + STATE_HASH.

#include "gloom_host.hpp"

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <cmath>
#include <stdexcept>

namespace {

jai::script_value call_script(jai::engine& eng, const char* fn_name,
                              const std::vector<jai::script_value>& args) {
	jai::script_value fn = eng.get_variable(fn_name);
	auto result = fn.as_function()(args);
	if (!result.has_value()) {
		throw std::runtime_error(std::string(fn_name) + ": " + std::string(result.message()));
	}
	return result.value();
}

class jai_session : public gloom::script_session {
public:
	jai_session(const std::string& backend, const gloom::host_services& services,
	            const gloom::host_options& opt)
		: opt_(opt), eng_(jai::engine::make()) {
		eng_->set_backend(backend == "vm" ? jai::backend_type::vm : jai::backend_type::interpreter);
		eng_->execution_budget(0.0);
		if (opt.workers > 0) { eng_->parallel_thread_count(static_cast<size_t>(opt.workers)); }
		jai::stdlib::register_all(eng_);

		jai::dynamic_binder<gloom::gloom_rng>(*eng_, "Rng")
			.constructor<jai::script_int>()
			.method("next", &gloom::gloom_rng::next)
			.method("roll", &gloom::gloom_rng::roll)
			.method("chance", &gloom::gloom_rng::chance)
			.method("nextf", &gloom::gloom_rng::nextf)
			.method("state", &gloom::gloom_rng::state)
			.build();

		eng_->add_function("host_log", [log = services.log](const std::string& line) { log(line); });
		eng_->add_function("key_down", [key_down = services.key_down](const std::string& name) -> bool {
			return key_down(name);
		});
		// float->int casts for the places typed-local truncation can't reach
		eng_->add_function("itrunc", [](jai::script_float v) -> jai::script_int {
			return static_cast<jai::script_int>(v);
		});
		eng_->add_function("ifloor", [](jai::script_float v) -> jai::script_int {
			return static_cast<jai::script_int>(std::floor(v));
		});
		eng_->add_function("utf8", [](jai::script_int cp) -> std::string {
			return gloom::utf8_encode(cp);
		});
		eng_->add_global("ESC", eng_->make_value(std::string("\x1b")));
		eng_->add_global("HOST_SEED", eng_->make_value(opt.seed));
		eng_->add_global("HOST_SMOKE", eng_->make_value(opt.smoke));
		eng_->add_global("HOST_TICKS", eng_->make_value(opt.ticks));
		eng_->add_global("HOST_WORKERS", eng_->make_value(opt.workers));
		eng_->add_global("HOST_GOD", eng_->make_value(opt.god));
		eng_->add_global("HOST_BACKEND", eng_->make_value(backend));
		eng_->add_global("HOST_PIX", eng_->make_value(gloom::pix_mode_index(opt.pix)));
	}

	void load_scripts() override {
		std::string dir = gloom::locate_scripts_dir(opt_.scripts_dir, opt_.argv0,
#ifdef GLOOM_SOURCE_SCRIPTS_DIR
			GLOOM_SOURCE_SCRIPTS_DIR,
#else
			"",
#endif
			"gloom_scripts", "main.jai");
		eng_->add_include_path(dir);
		eng_->set_import_behavior(jai::engine::import_behavior::always);
		eng_->execute("import \"main.jai\";");
	}

	void force_autopilot() override { eng_->execute("gloom_force_autopilot();"); }

	void boot(int64_t w, int64_t h) override {
		call_script(*eng_, "gloom_boot", {
			jai::script_value(w, eng_.get()),
			jai::script_value(h, eng_.get()),
		});
	}

	std::string frame(double dt, const std::string& key,
	                  double fps, double ms_sim, double ms_draw) override {
		jai::script_value result = call_script(*eng_, "gloom_frame", {
			jai::script_value(dt, eng_.get()),
			eng_->make_value(key),
			jai::script_value(fps, eng_.get()),
			jai::script_value(ms_sim, eng_.get()),
			jai::script_value(ms_draw, eng_.get()),
		});
		return result.to_string();
	}

	int64_t state_hash() override { return call_script(*eng_, "gloom_state_hash", {}).as_int(); }

	bool wants_quit() override { return call_script(*eng_, "gloom_wants_quit", {}).as<bool>(); }

	void summary() override { call_script(*eng_, "gloom_summary", {}); }

	std::string stack_trace() override { return eng_->format_stack_trace(); }

private:
	gloom::host_options opt_;
	std::shared_ptr<jai::engine> eng_;
};

class jai_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "jai_gloom"; }
	std::string language() const override { return "JaiScript"; }
	std::vector<std::string> backends() const override { return {"interpreter", "vm"}; }
	std::string normalize_backend(const std::string& b) const override {
		return b == "interp" ? "interpreter" : b;
	}
	std::string title() const override {
		return "jai_gloom - GLOOM, a terminal raycast shooter written in JaiScript";
	}
	std::unique_ptr<gloom::script_session> make_session(const std::string& backend,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input — services.key_down is already gated*/) override {
		return std::make_unique<jai_session>(backend, services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	jai_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
