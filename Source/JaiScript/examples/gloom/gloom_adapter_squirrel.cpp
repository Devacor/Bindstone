// gloom_squirrel: Squirrel adapter for the GLOOM shared host. SCAFFOLD ONLY —
// it compiles and links against Squirrel (proving the opt-in CMake plumbing)
// and errors at script-load until the port lands. See PORTING.md for the
// contract and gloom_adapter_jai.cpp for the reference adapter shape.

#include "gloom_host.hpp"

#include <squirrel.h>

#include <stdexcept>

namespace {

class squirrel_session : public gloom::script_session {
public:
	squirrel_session(const gloom::host_services& services, const gloom::host_options& opt)
		: opt_(opt), services_(services), vm_(sq_open(1024)) {
		// TODO(port): bind the host API per REFERENCE.md section 2 —
		// gloom::gloom_rng as "Rng", host_log/key_down from services_,
		// itrunc/ifloor, utf8 via gloom::utf8_encode, ESC, HOST_* globals
		// (HOST_PIX via gloom::pix_mode_index(opt.pix)).
	}

	~squirrel_session() override {
		if (vm_) { sq_close(vm_); }
	}

	void load_scripts() override {
		// TODO(port): gloom::locate_scripts_dir(opt_.scripts_dir, opt_.argv0,
		//     GLOOM_SOURCE_SCRIPTS_DIR, "gloom_scripts_squirrel", "main.nut") then compile+run.
		throw std::runtime_error("Squirrel port not yet implemented (see examples/gloom/PORTING.md)");
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
	HSQUIRRELVM vm_ = nullptr;
};

class squirrel_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_squirrel"; }
	std::string language() const override { return "Squirrel"; }
	std::vector<std::string> backends() const override { return {"squirrel"}; }
	std::unique_ptr<gloom::script_session> make_session(const std::string& /*backend*/,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input*/) override {
		return std::make_unique<squirrel_session>(services, opt);
	}
};

} // namespace

int main(int argc, char** argv) {
	squirrel_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
