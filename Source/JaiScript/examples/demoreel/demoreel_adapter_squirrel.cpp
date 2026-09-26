// demoreel_squirrel: the Squirrel 3.2 adapter for the demoreel shared host.
// Raw Squirrel C API (as gloom_adapter_squirrel.cpp): binds the host API,
// executes ports/squirrel/main.nut (which include()s the rest), forwards the
// demo_* entry points.

#include "demoreel_host.hpp"

#include <squirrel.h>
#include <sqstdaux.h>
#include <sqstdmath.h>
#include <sqstdstring.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>

// int64 C semantics and IEEE doubles are the parity contract
static_assert(sizeof(SQInteger) == 8, "demoreel_squirrel requires _SQ64 (64-bit SQInteger)");
static_assert(sizeof(SQFloat) == 8, "demoreel_squirrel requires SQUSEDOUBLE (double SQFloat)");

namespace {

class squirrel_session;
squirrel_session* session_of(HSQUIRRELVM v);

SQUserPointer rng_tag() { return reinterpret_cast<SQUserPointer>(&rng_tag); }

demoreel::demo_rng* rng_self(HSQUIRRELVM v) {
	SQUserPointer up = nullptr;
	sq_getinstanceup(v, 1, &up, rng_tag(), SQTrue);
	return static_cast<demoreel::demo_rng*>(up);
}

SQInteger rng_construct(HSQUIRRELVM v) {
	SQInteger seed = 0;
	sq_getinteger(v, 2, &seed);
	demoreel::demo_rng* self = rng_self(v);
	if (!self) { return sq_throwerror(v, "Rng: bad instance"); }
	new (self) demoreel::demo_rng(seed);
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

// JaiScript copies a bound Rng passed by value (sand.jai's `var r`); Squirrel's
// clone does not copy class user data, so the port copies explicitly
SQInteger rng_copy(HSQUIRRELVM v) {
	demoreel::demo_rng state = *rng_self(v);
	sq_getclass(v, 1);
	if (SQ_FAILED(sq_createinstance(v, -1))) { return sq_throwerror(v, "Rng.copy: cannot create"); }
	SQUserPointer up = nullptr;
	sq_getinstanceup(v, -1, &up, rng_tag(), SQTrue);
	new (up) demoreel::demo_rng(state);
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

// sqstdmath has no log2; log(x)/log(2) would not be bit-identical to JaiScript's
SQInteger native_log2(HSQUIRRELVM v) {
	SQFloat f = 0;
	sq_getfloat(v, 2, &f);
	sq_pushfloat(v, std::log2(f));
	return 1;
}

SQInteger native_host_log(HSQUIRRELVM v);
SQInteger native_include(HSQUIRRELVM v);

void bind_closure(HSQUIRRELVM v, const SQChar* name, SQFUNCTION fn, SQInteger nparams, const SQChar* mask) {
	sq_pushstring(v, name, -1);
	sq_newclosure(v, fn, 0);
	if (mask) { sq_setparamscheck(v, nparams, mask); }
	sq_newslot(v, -3, SQFalse);
}

struct sq_flags {
	int64_t only = -1;   // porting aid: every other scene index builds as an empty stub
};

class squirrel_session : public demoreel::script_session {
public:
	squirrel_session(const demoreel::host_services& services, const demoreel::host_options& opt,
	                 const sq_flags& flags)
		: opt_(opt), services_(services), vm_(sq_open(1024)) {
		sq_setforeignptr(vm_, this);
		sq_setprintfunc(vm_, &squirrel_session::print_func, &squirrel_session::error_func);
		sq_pushroottable(vm_);
		sqstd_register_mathlib(vm_);
		sqstd_register_stringlib(vm_);
		sq_pop(vm_, 1);
		sqstd_seterrorhandlers(vm_);
		// after seterrorhandlers, which installs its own compiler error handler
		sq_setcompilererrorhandler(vm_, &squirrel_session::compile_error_handler);

		sq_pushroottable(vm_);
		bind_closure(vm_, "host_log", native_host_log, 2, ".s");
		bind_closure(vm_, "itrunc", native_itrunc, 2, ".n");
		bind_closure(vm_, "ifloor", native_ifloor, 2, ".n");
		bind_closure(vm_, "log2", native_log2, 2, ".n");
		bind_closure(vm_, "include", native_include, 2, ".s");
		sq_pushstring(vm_, "ESC", -1);
		sq_pushstring(vm_, "\x1b", 1);
		sq_newslot(vm_, -3, SQFalse);
		sq_pushstring(vm_, "SQ_ONLY_SCENE", -1);
		sq_pushinteger(vm_, flags.only);
		sq_newslot(vm_, -3, SQFalse);

		sq_pushstring(vm_, "Rng", -1);
		sq_newclass(vm_, SQFalse);
		sq_settypetag(vm_, -1, rng_tag());
		sq_setclassudsize(vm_, -1, sizeof(demoreel::demo_rng));
		bind_closure(vm_, "constructor", rng_construct, 2, "xn");
		bind_closure(vm_, "next", rng_next, 2, "xn");
		bind_closure(vm_, "roll", rng_roll, 3, "xnn");
		bind_closure(vm_, "chance", rng_chance, 2, "xn");
		bind_closure(vm_, "nextf", rng_nextf, 1, "x");
		bind_closure(vm_, "copy", rng_copy, 1, "x");
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);
	}

	~squirrel_session() override {
		if (vm_) { sq_close(vm_); }
	}

	void load() override {
		dir_ = demoreel::locate_scenes_dir(opt_.scenes_dir, opt_.argv0,
#ifdef DEMOREEL_SQUIRREL_SOURCE_DIR
			DEMOREEL_SQUIRREL_SOURCE_DIR,
#else
			"",
#endif
			"demoreel_squirrel_scenes", "main.nut");
		std::string err;
		if (!run_file(dir_ + "/main.nut", err)) { throw std::runtime_error(err); }
	}

	std::string load_kind() override { return "nut"; }

	void boot(int64_t w, int64_t h, int64_t seed, const std::string& backend, bool smoke, bool hud) override {
		begin_call("demo_boot");
		sq_pushinteger(vm_, w);
		sq_pushinteger(vm_, h);
		sq_pushinteger(vm_, seed);
		sq_pushstring(vm_, backend.c_str(), static_cast<SQInteger>(backend.size()));
		sq_pushbool(vm_, smoke ? SQTrue : SQFalse);
		sq_pushbool(vm_, hud ? SQTrue : SQFalse);
		call("demo_boot", 6, false);
		sq_pop(vm_, 2);
	}

	std::string frame(const demoreel::frame_args& a) override {
		begin_call("demo_frame");
		sq_pushfloat(vm_, a.t);
		sq_pushfloat(vm_, a.dt);
		sq_pushstring(vm_, a.key.c_str(), static_cast<SQInteger>(a.key.size()));
		sq_pushfloat(vm_, a.fps);
		sq_pushfloat(vm_, a.ms_sim);
		sq_pushfloat(vm_, a.ms_draw);
		sq_pushfloat(vm_, a.ms_other);
		call("demo_frame", 7, true);
		const SQChar* s = nullptr;
		SQInteger len = 0;
		sq_getstringandsize(vm_, -1, &s, &len);
		std::string result(s ? s : "", s ? static_cast<size_t>(len) : 0);
		sq_pop(vm_, 3);
		return result;
	}

	int64_t scene_index() override { return get_int("demo_scene_index"); }
	int64_t scene_count() override { return get_int("demo_scene_count"); }

	double scene_time() override {
		begin_call("demo_scene_time");
		call("demo_scene_time", 0, true);
		SQFloat f = 0;
		sq_getfloat(vm_, -1, &f);
		sq_pop(vm_, 3);
		return f;
	}

	std::string scene_name() override {
		begin_call("demo_scene_name");
		call("demo_scene_name", 0, true);
		const SQChar* s = nullptr;
		sq_getstring(vm_, -1, &s);
		std::string r = s ? s : "";
		sq_pop(vm_, 3);
		return r;
	}

	void jump(int64_t idx, double at_t) override {
		begin_call("demo_jump");
		sq_pushinteger(vm_, idx);
		sq_pushfloat(vm_, at_t);
		call("demo_jump", 2, false);
		sq_pop(vm_, 2);
	}

	std::string stack_trace() override { return trace_; }

	const demoreel::host_services& services() const { return services_; }

	bool run_file(const std::string& path, std::string& err) {
		std::string src = demoreel::slurp_file(path);
		if (src.empty()) { err = "cannot open " + path; return false; }
		compile_error_.clear();
		if (SQ_FAILED(sq_compilebuffer(vm_, src.c_str(), static_cast<SQInteger>(src.size()), path.c_str(), SQTrue))) {
			err = compile_error_.empty() ? ("compile failed: " + path) : compile_error_;
			return false;
		}
		sq_pushroottable(vm_);
		if (SQ_FAILED(sq_call(vm_, 1, SQFalse, SQTrue))) {
			err = path + ": " + last_error();
			sq_pop(vm_, 1);
			return false;
		}
		sq_pop(vm_, 1);
		return true;
	}

	bool include(const std::string& name, std::string& err) { return run_file(dir_ + "/" + name, err); }
	void append_trace(const std::string& text) { trace_ += text; }
	void set_compile_error(const std::string& text) { compile_error_ = text; }

private:
	int64_t get_int(const char* name) {
		begin_call(name);
		call(name, 0, true);
		SQInteger i = 0;
		sq_getinteger(vm_, -1, &i);
		sq_pop(vm_, 3);
		return i;
	}

	// stack after: root, closure, root(this); push args, then call()
	void begin_call(const char* name) {
		sq_pushroottable(vm_);
		sq_pushstring(vm_, name, -1);
		if (SQ_FAILED(sq_get(vm_, -2))) {
			sq_pop(vm_, 1);
			throw std::runtime_error(std::string(name) + ": not defined by scripts");
		}
		sq_pushroottable(vm_);
	}

	void call(const char* name, SQInteger nargs, bool ret) {
		trace_.clear();
		if (SQ_FAILED(sq_call(vm_, 1 + nargs, ret ? SQTrue : SQFalse, SQTrue))) {
			std::string err = last_error();
			sq_settop(vm_, 0);
			throw std::runtime_error(std::string(name) + ": " + err);
		}
	}

	std::string last_error() {
		std::string err = "unknown error";
		sq_getlasterror(vm_);
		if (SQ_SUCCEEDED(sq_tostring(vm_, -1))) {
			const SQChar* s = nullptr;
			sq_getstring(vm_, -1, &s);
			if (s) { err = s; }
			sq_pop(vm_, 1);
		}
		sq_pop(vm_, 1);
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
		snprintf(buf, sizeof(buf), "%s:%lld:%lld: %s", source ? source : "<script>",
		         static_cast<long long>(line), static_cast<long long>(column), desc ? desc : "compile error");
		session_of(v)->set_compile_error(buf);
	}

	demoreel::host_options opt_;
	demoreel::host_services services_;
	HSQUIRRELVM vm_ = nullptr;
	std::string dir_;
	std::string trace_;
	std::string compile_error_;
};

squirrel_session* session_of(HSQUIRRELVM v) { return static_cast<squirrel_session*>(sq_getforeignptr(v)); }

SQInteger native_host_log(HSQUIRRELVM v) {
	const SQChar* s = nullptr;
	sq_getstring(v, 2, &s);
	session_of(v)->services().log(s ? s : "");
	return 0;
}

SQInteger native_include(HSQUIRRELVM v) {
	const SQChar* name = nullptr;
	sq_getstring(v, 2, &name);
	std::string err;
	if (!session_of(v)->include(name ? name : "", err)) { return sq_throwerror(v, err.c_str()); }
	return 0;
}

class squirrel_adapter : public demoreel::script_adapter {
public:
	std::string program_name() const override { return "demoreel_squirrel"; }
	std::string language() const override { return "Squirrel 3.2"; }
	std::vector<std::string> backends() const override { return {"squirrel"}; }

	int parse_flag(int argc, char** argv, int i) override {
		if (std::string(argv[i]) == "--only" && i + 1 < argc) { flags_.only = std::stoll(argv[i + 1]); return 2; }
		return 0;
	}

	std::string usage() const override {
		return "  --only I         porting aid: scenes other than index I build as empty stubs\n";
	}

	std::unique_ptr<demoreel::script_session> make_session(const std::string&,
			const demoreel::host_services& services, const demoreel::host_options& opt) override {
		return std::make_unique<squirrel_session>(services, opt, flags_);
	}

private:
	sq_flags flags_;
};

} // namespace

int main(int argc, char** argv) {
	squirrel_adapter adapter;
	return demoreel::run_demoreel(argc, argv, adapter);
}
