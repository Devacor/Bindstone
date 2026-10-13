// gloom_squirrel: Squirrel adapter for the GLOOM shared host. Binds the host
// API through the raw Squirrel C API (sq_newclosure / sq_setparamscheck stack
// discipline throughout — no binder layer), loads the port's .nut scripts from
// ports/squirrel/scripts, and forwards the six gloom_* entry points.
//
// The scripts use include("<file>.nut") — a native bound here — to mirror the
// reference's per-file import structure (Squirrel has no module system).

#include "gloom_host.hpp"

#include <squirrel.h>
#include <sqstdaux.h>
#include <sqstdmath.h>
#include <sqstdstring.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

// The parity contract needs 64-bit ints (RNG passthrough, bit math) and C
// doubles (IEEE-754 sim math). squirrel.h auto-defines _SQ64 on _WIN64;
// SQUSEDOUBLE must come from the build (CXXFLAGS=/DSQUSEDOUBLE at configure).
static_assert(sizeof(SQInteger) == 8, "gloom_squirrel requires _SQ64 (64-bit SQInteger)");
static_assert(sizeof(SQFloat) == 8, "gloom_squirrel requires SQUSEDOUBLE (double SQFloat)");

namespace {

class squirrel_session;
squirrel_session* session_of(HSQUIRRELVM v);

SQUserPointer rng_tag() {
	return reinterpret_cast<SQUserPointer>(&rng_tag);
}

gloom::gloom_rng* rng_self(HSQUIRRELVM v) {
	SQUserPointer up = nullptr;
	sq_getinstanceup(v, 1, &up, rng_tag(), SQTrue);
	return static_cast<gloom::gloom_rng*>(up);
}

SQInteger rng_construct(HSQUIRRELVM v) {
	SQInteger seed = 0;
	sq_getinteger(v, 2, &seed);
	gloom::gloom_rng* self = rng_self(v);
	if (!self) { return sq_throwerror(v, "Rng: bad instance"); }
	new (self) gloom::gloom_rng(seed);
	return 0;
}

SQInteger rng_next(HSQUIRRELVM v) {
	SQInteger n = 0;
	sq_getinteger(v, 2, &n);
	sq_pushinteger(v, rng_self(v)->next(n));
	return 1;
}

SQInteger rng_roll(HSQUIRRELVM v) {
	SQInteger lo = 0, hi = 0;
	sq_getinteger(v, 2, &lo);
	sq_getinteger(v, 3, &hi);
	sq_pushinteger(v, rng_self(v)->roll(lo, hi));
	return 1;
}

SQInteger rng_chance(HSQUIRRELVM v) {
	SQInteger p = 0;
	sq_getinteger(v, 2, &p);
	sq_pushbool(v, rng_self(v)->chance(p) ? SQTrue : SQFalse);
	return 1;
}

SQInteger rng_nextf(HSQUIRRELVM v) {
	sq_pushfloat(v, static_cast<SQFloat>(rng_self(v)->nextf()));
	return 1;
}

SQInteger rng_state(HSQUIRRELVM v) {
	sq_pushinteger(v, rng_self(v)->state());
	return 1;
}

SQInteger native_itrunc(HSQUIRRELVM v) {
	SQFloat f = 0;
	sq_getfloat(v, 2, &f);
	sq_pushinteger(v, static_cast<SQInteger>(f));
	return 1;
}

SQInteger native_ifloor(HSQUIRRELVM v) {
	SQFloat f = 0;
	sq_getfloat(v, 2, &f);
	sq_pushinteger(v, static_cast<SQInteger>(std::floor(f)));
	return 1;
}

SQInteger native_utf8(HSQUIRRELVM v) {
	SQInteger cp = 0;
	sq_getinteger(v, 2, &cp);
	std::string s = gloom::utf8_encode(cp);
	sq_pushstring(v, s.c_str(), static_cast<SQInteger>(s.size()));
	return 1;
}

// forward decls for natives that need the session
SQInteger native_host_log(HSQUIRRELVM v);
SQInteger native_key_down(HSQUIRRELVM v);
SQInteger native_include(HSQUIRRELVM v);

void bind_closure(HSQUIRRELVM v, const SQChar* name, SQFUNCTION fn,
                  SQInteger nparams, const SQChar* mask) {
	sq_pushstring(v, name, -1);
	sq_newclosure(v, fn, 0);
	if (mask) { sq_setparamscheck(v, nparams, mask); }
	sq_newslot(v, -3, SQFalse);
}

class squirrel_session : public gloom::script_session {
public:
	squirrel_session(const gloom::host_services& services, const gloom::host_options& opt,
	                 const std::string& backend)
		: opt_(opt), services_(services), vm_(sq_open(1024)) {
		sq_setforeignptr(vm_, this);
		sq_setprintfunc(vm_, &squirrel_session::print_func, &squirrel_session::error_func);
		sq_pushroottable(vm_);
		sqstd_register_mathlib(vm_);
		sqstd_register_stringlib(vm_);
		sq_pop(vm_, 1);
		sqstd_seterrorhandlers(vm_);   // default handler prints the callstack via error_func
		// after seterrorhandlers — it installs its own compiler error handler
		sq_setcompilererrorhandler(vm_, &squirrel_session::compile_error_handler);

		sq_pushroottable(vm_);
		bind_closure(vm_, "host_log", native_host_log, 2, ".s");
		bind_closure(vm_, "key_down", native_key_down, 2, ".s");
		bind_closure(vm_, "itrunc", native_itrunc, 2, ".n");
		bind_closure(vm_, "ifloor", native_ifloor, 2, ".n");
		bind_closure(vm_, "utf8", native_utf8, 2, ".n");
		bind_closure(vm_, "include", native_include, 2, ".s");
		set_global_string("ESC", "\x1b");
		set_global_int("HOST_SEED", opt.seed);
		set_global_bool("HOST_SMOKE", opt.smoke);
		set_global_int("HOST_TICKS", opt.ticks);
		set_global_int("HOST_WORKERS", opt.workers);
		set_global_bool("HOST_GOD", opt.god);
		set_global_string("HOST_BACKEND", backend.c_str());
		set_global_int("HOST_PIX", gloom::pix_mode_index(opt.pix));

		// Rng: the shared gloom_rng embedded in the instance's user-data area
		sq_pushstring(vm_, "Rng", -1);
		sq_newclass(vm_, SQFalse);
		sq_settypetag(vm_, -1, rng_tag());
		sq_setclassudsize(vm_, -1, sizeof(gloom::gloom_rng));
		bind_closure(vm_, "constructor", rng_construct, 2, "xn");
		bind_closure(vm_, "next", rng_next, 2, "xn");
		bind_closure(vm_, "roll", rng_roll, 3, "xnn");
		bind_closure(vm_, "chance", rng_chance, 2, "xn");
		bind_closure(vm_, "nextf", rng_nextf, 1, "x");
		bind_closure(vm_, "state", rng_state, 1, "x");
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);   // root
	}

	~squirrel_session() override {
		if (vm_) { sq_close(vm_); }
	}

	void load_scripts() override {
		scripts_dir_ = gloom::locate_scripts_dir(opt_.scripts_dir, opt_.argv0,
#ifdef GLOOM_SOURCE_SCRIPTS_DIR
			GLOOM_SOURCE_SCRIPTS_DIR,
#else
			"",
#endif
			"gloom_scripts_squirrel", "main.nut");
		std::string err;
		if (!run_file(scripts_dir_ + "/main.nut", err)) { throw std::runtime_error(err); }
	}

	void force_autopilot() override { call_void("gloom_force_autopilot", 0, nullptr); }

	void boot(int64_t w, int64_t h) override {
		SQInteger args[2] = { w, h };
		call_void("gloom_boot", 2, args);
	}

	std::string frame(double dt, const std::string& key,
	                  double fps, double ms_sim, double ms_draw) override {
		begin_call("gloom_frame");
		sq_pushfloat(vm_, static_cast<SQFloat>(dt));
		sq_pushstring(vm_, key.c_str(), static_cast<SQInteger>(key.size()));
		sq_pushfloat(vm_, static_cast<SQFloat>(fps));
		sq_pushfloat(vm_, static_cast<SQFloat>(ms_sim));
		sq_pushfloat(vm_, static_cast<SQFloat>(ms_draw));
		call_with_ret("gloom_frame", 5);
		const SQChar* s = nullptr;
		SQInteger len = 0;
		sq_getstringandsize(vm_, -1, &s, &len);
		std::string result(s ? s : "", s ? static_cast<size_t>(len) : 0);
		sq_pop(vm_, 3);   // ret + closure + root
		return result;
	}

	int64_t state_hash() override {
		begin_call("gloom_state_hash");
		call_with_ret("gloom_state_hash", 0);
		SQInteger h = 0;
		sq_getinteger(vm_, -1, &h);
		sq_pop(vm_, 3);
		return h;
	}

	bool wants_quit() override {
		begin_call("gloom_wants_quit");
		call_with_ret("gloom_wants_quit", 0);
		SQBool b = SQFalse;
		sq_getbool(vm_, -1, &b);
		sq_pop(vm_, 3);
		return b != SQFalse;
	}

	void summary() override { call_void("gloom_summary", 0, nullptr); }

	std::string stack_trace() override { return trace_; }

	// --- native support -----------------------------------------------------
	const gloom::host_services& services() const { return services_; }

	bool run_file(const std::string& path, std::string& err) {
		std::ifstream in(path, std::ios::binary);
		if (!in) { err = "cannot open " + path; return false; }
		std::ostringstream ss;
		ss << in.rdbuf();
		std::string src = ss.str();
		compile_error_.clear();
		if (SQ_FAILED(sq_compilebuffer(vm_, src.c_str(), static_cast<SQInteger>(src.size()),
		                               path.c_str(), SQTrue))) {
			err = compile_error_.empty() ? ("compile failed: " + path) : compile_error_;
			return false;
		}
		sq_pushroottable(vm_);
		if (SQ_FAILED(sq_call(vm_, 1, SQFalse, SQTrue))) {
			err = path + ": " + last_error();
			sq_pop(vm_, 1);   // closure
			return false;
		}
		sq_pop(vm_, 1);   // closure
		return true;
	}

	bool include(const std::string& name, std::string& err) {
		return run_file(scripts_dir_ + "/" + name, err);
	}

	void append_trace(const std::string& text) { trace_ += text; }
	void set_compile_error(const std::string& text) { compile_error_ = text; }

private:
	void set_global_int(const SQChar* name, int64_t value) {
		sq_pushstring(vm_, name, -1);
		sq_pushinteger(vm_, value);
		sq_newslot(vm_, -3, SQFalse);
	}
	void set_global_bool(const SQChar* name, bool value) {
		sq_pushstring(vm_, name, -1);
		sq_pushbool(vm_, value ? SQTrue : SQFalse);
		sq_newslot(vm_, -3, SQFalse);
	}
	void set_global_string(const SQChar* name, const SQChar* value) {
		sq_pushstring(vm_, name, -1);
		sq_pushstring(vm_, value, -1);
		sq_newslot(vm_, -3, SQFalse);
	}

	// stack after: root, closure, root(this) — push args, then call_*
	void begin_call(const char* name) {
		sq_pushroottable(vm_);
		sq_pushstring(vm_, name, -1);
		if (SQ_FAILED(sq_get(vm_, -2))) {
			sq_pop(vm_, 1);
			throw std::runtime_error(std::string(name) + ": not defined by scripts");
		}
		sq_pushroottable(vm_);
	}

	void call_with_ret(const char* name, SQInteger nargs) {
		trace_.clear();
		if (SQ_FAILED(sq_call(vm_, 1 + nargs, SQTrue, SQTrue))) { fail(name); }
	}

	void call_void(const char* name, SQInteger nargs, const SQInteger* int_args) {
		begin_call(name);
		for (SQInteger i = 0; i < nargs; ++i) { sq_pushinteger(vm_, int_args[i]); }
		trace_.clear();
		if (SQ_FAILED(sq_call(vm_, 1 + nargs, SQFalse, SQTrue))) { fail(name); }
		sq_pop(vm_, 2);   // closure + root
	}

	[[noreturn]] void fail(const char* name) {
		std::string err = last_error();
		sq_settop(vm_, 0);
		throw std::runtime_error(std::string(name) + ": " + err);
	}

	std::string last_error() {
		std::string err = "unknown error";
		sq_getlasterror(vm_);
		if (SQ_SUCCEEDED(sq_tostring(vm_, -1))) {
			const SQChar* s = nullptr;
			sq_getstring(vm_, -1, &s);
			if (s) { err = s; }
			sq_pop(vm_, 1);   // tostring result
		}
		sq_pop(vm_, 1);   // error object
		return err;
	}

	static void print_func(HSQUIRRELVM v, const SQChar* fmt, ...) {
		char buf[2048];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		session_of(v)->services().log(buf);
	}

	static void error_func(HSQUIRRELVM v, const SQChar* fmt, ...) {
		char buf[2048];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		session_of(v)->append_trace(buf);
	}

	static void compile_error_handler(HSQUIRRELVM v, const SQChar* desc, const SQChar* source,
	                                  SQInteger line, SQInteger column) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s:%lld:%lld: %s",
		         source ? source : "<script>", static_cast<long long>(line),
		         static_cast<long long>(column), desc ? desc : "compile error");
		session_of(v)->set_compile_error(buf);
	}

	gloom::host_options opt_;
	gloom::host_services services_;
	HSQUIRRELVM vm_ = nullptr;
	std::string scripts_dir_;
	std::string trace_;
	std::string compile_error_;
};

squirrel_session* session_of(HSQUIRRELVM v) {
	return static_cast<squirrel_session*>(sq_getforeignptr(v));
}

SQInteger native_host_log(HSQUIRRELVM v) {
	const SQChar* s = nullptr;
	sq_getstring(v, 2, &s);
	session_of(v)->services().log(s ? s : "");
	return 0;
}

SQInteger native_key_down(HSQUIRRELVM v) {
	const SQChar* s = nullptr;
	sq_getstring(v, 2, &s);
	sq_pushbool(v, session_of(v)->services().key_down(s ? s : "") ? SQTrue : SQFalse);
	return 1;
}

SQInteger native_include(HSQUIRRELVM v) {
	const SQChar* name = nullptr;
	sq_getstring(v, 2, &name);
	std::string err;
	if (!session_of(v)->include(name ? name : "", err)) {
		return sq_throwerror(v, err.c_str());
	}
	return 0;
}

class squirrel_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_squirrel"; }
	std::string language() const override { return "Squirrel"; }
	std::vector<std::string> backends() const override { return {"squirrel"}; }
	std::string title() const override {
		return "gloom_squirrel - GLOOM, a terminal raycast shooter (Squirrel port)";
	}
	std::unique_ptr<gloom::script_session> make_session(const std::string& backend,
			const gloom::host_services& services, const gloom::host_options& opt,
			bool /*live_input — services.key_down is already gated*/) override {
		return std::make_unique<squirrel_session>(services, opt, backend);
	}
};

} // namespace

int main(int argc, char** argv) {
	squirrel_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
