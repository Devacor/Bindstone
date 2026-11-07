// gloom_angelscript: AngelScript 2.38 adapter for the GLOOM shared host. Binds the host API
// (reference/README.md section 2) with the native calling convention: the shared gloom_rng
// as the POD value type Rng, host services as methods called as globals, HOST_* flags as
// const global properties. Loads ports/angelscript/scripts through CScriptBuilder (main.as
// #includes the per-file structure) and forwards the six gloom_* entry points.
//
// AngelScript's core has no coroutines, so the adapter binds a small one: a coroutine is a
// suspended script context running a void() delegate, yield() suspends it, resume() runs it
// to the next yield. Enemy brains and the autopilot are coroutine handles held in fields.

#include "gloom_host.hpp"

#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scripthelper/scripthelper.h>
#include <scriptmath/scriptmath.h>
#include <scriptstdstring/scriptstdstring.h>

#include <cmath>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr asPWORD COROUTINE_CONTEXT_TAG = 0x474c4f4f;

class angelscript_session;

void rng_construct(int64_t a_seed, void* a_memory) {
	new (a_memory) gloom::gloom_rng(a_seed);
}

int itrunc(double a_value) {
	return static_cast<int>(a_value);
}

int ifloor(double a_value) {
	return static_cast<int>(std::floor(a_value));
}

std::string utf8(int a_codepoint) {
	return gloom::utf8_encode(a_codepoint);
}

std::string context_location(asIScriptContext* a_context, asUINT a_level) {
	const char* section = nullptr;
	int column = 0;
	int line = a_context->GetLineNumber(a_level, &column, &section);
	asIScriptFunction* function = a_context->GetFunction(a_level);
	return std::string(section ? section : "?") + ":" + std::to_string(line) + " in " +
		(function ? function->GetDeclaration(true, true) : "?");
}

std::string exception_text(asIScriptContext* a_context) {
	const char* section = nullptr;
	int column = 0;
	int line = a_context->GetExceptionLineNumber(&column, &section);
	asIScriptFunction* function = a_context->GetExceptionFunction();
	return std::string(a_context->GetExceptionString()) + " at " + (section ? section : "?") + ":" +
		std::to_string(line) + " in " + (function ? function->GetDeclaration(true, true) : "?");
}

std::string call_stack(asIScriptContext* a_context) {
	std::string result;
	for (asUINT level = 0; level < a_context->GetCallstackSize(); ++level) {
		result += "  " + context_location(a_context, level) + "\n";
	}
	return result;
}

void script_yield() {
	asIScriptContext* context = asGetActiveContext();
	if (!context) {
		return;
	}
	if (context->GetUserData(COROUTINE_CONTEXT_TAG) == nullptr) {
		context->SetException("yield() outside a coroutine");
		return;
	}
	context->Suspend();
}

// A script coroutine: one pooled context suspended inside a void() delegate.
class script_coroutine {
public:
	script_coroutine(angelscript_session* a_owner, asIScriptFunction* a_body);
	~script_coroutine() { abandon(); }

	void addRef() { ++references; }
	void release() {
		if (--references == 0) {
			delete this;
		}
	}

	bool resume();
	bool done() const { return context == nullptr; }
	void abandon();

private:
	int references {1};
	angelscript_session* owner;
	asIScriptContext* context {nullptr};
};

class angelscript_session : public gloom::script_session {
public:
	angelscript_session(const gloom::host_services& a_services, const gloom::host_options& a_options,
		const std::string& a_backend) :
		options(a_options),
		services(a_services),
		seed(a_options.seed),
		smoke(a_options.smoke),
		ticks(a_options.ticks),
		workers(a_options.workers),
		god(a_options.god),
		pix(static_cast<int>(gloom::pix_mode_index(a_options.pix))),
		backend(a_backend),
		escape("\x1b"),
		engine(asCreateScriptEngine()) {

		if (!engine) {
			throw std::runtime_error("asCreateScriptEngine failed");
		}
		engine->SetMessageCallback(asMETHOD(angelscript_session, onMessage), this, asCALL_THISCALL);
		RegisterStdString(engine);
		RegisterScriptArray(engine, true);
		RegisterStdStringUtils(engine);
		RegisterScriptMath(engine);
		RegisterScriptDictionary(engine);
		RegisterExceptionRoutines(engine);

		check(engine->RegisterObjectType("Rng", sizeof(gloom::gloom_rng),
			asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<gloom::gloom_rng>()));
		check(engine->RegisterObjectBehaviour("Rng", asBEHAVE_CONSTRUCT, "void f(int64 seed)",
			asFUNCTION(rng_construct), asCALL_CDECL_OBJLAST));
		check(engine->RegisterObjectMethod("Rng", "int64 next(int64 n)", asMETHOD(gloom::gloom_rng, next), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("Rng", "int64 roll(int64 lo, int64 hi)", asMETHOD(gloom::gloom_rng, roll), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("Rng", "bool chance(int64 percent)", asMETHOD(gloom::gloom_rng, chance), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("Rng", "double nextf()", asMETHOD(gloom::gloom_rng, nextf), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("Rng", "int64 state() const", asMETHOD(gloom::gloom_rng, state), asCALL_THISCALL));

		check(engine->RegisterFuncdef("void coroutine_body()"));
		check(engine->RegisterObjectType("coroutine", 0, asOBJ_REF));
		check(engine->RegisterObjectBehaviour("coroutine", asBEHAVE_FACTORY, "coroutine@ f(coroutine_body@ body)",
			asMETHOD(angelscript_session, makeCoroutine), asCALL_THISCALL_ASGLOBAL, this));
		check(engine->RegisterObjectBehaviour("coroutine", asBEHAVE_ADDREF, "void f()", asMETHOD(script_coroutine, addRef), asCALL_THISCALL));
		check(engine->RegisterObjectBehaviour("coroutine", asBEHAVE_RELEASE, "void f()", asMETHOD(script_coroutine, release), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("coroutine", "bool resume()", asMETHOD(script_coroutine, resume), asCALL_THISCALL));
		check(engine->RegisterObjectMethod("coroutine", "bool done() const", asMETHOD(script_coroutine, done), asCALL_THISCALL));
		check(engine->RegisterGlobalFunction("void yield()", asFUNCTION(script_yield), asCALL_CDECL));

		check(engine->RegisterGlobalFunction("void host_log(const string &in line)",
			asMETHOD(angelscript_session, hostLog), asCALL_THISCALL_ASGLOBAL, this));
		check(engine->RegisterGlobalFunction("bool key_down(const string &in name)",
			asMETHOD(angelscript_session, keyDown), asCALL_THISCALL_ASGLOBAL, this));
		check(engine->RegisterGlobalFunction("int itrunc(double value)", asFUNCTION(itrunc), asCALL_CDECL));
		check(engine->RegisterGlobalFunction("int ifloor(double value)", asFUNCTION(ifloor), asCALL_CDECL));
		check(engine->RegisterGlobalFunction("string utf8(int codepoint)", asFUNCTION(utf8), asCALL_CDECL));
		check(engine->RegisterGlobalProperty("const string ESC", &escape));
		check(engine->RegisterGlobalProperty("const int64 HOST_SEED", &seed));
		check(engine->RegisterGlobalProperty("const bool HOST_SMOKE", &smoke));
		check(engine->RegisterGlobalProperty("const int64 HOST_TICKS", &ticks));
		check(engine->RegisterGlobalProperty("const int64 HOST_WORKERS", &workers));
		check(engine->RegisterGlobalProperty("const bool HOST_GOD", &god));
		check(engine->RegisterGlobalProperty("const string HOST_BACKEND", &backend));
		check(engine->RegisterGlobalProperty("const int HOST_PIX", &pix));
		context = engine->CreateContext();
	}

	~angelscript_session() override {
		// A brain's delegate holds its enemy, which holds the brain: break those cycles first.
		for (script_coroutine* live : std::vector<script_coroutine*>(coroutines.begin(), coroutines.end())) {
			live->abandon();
		}
		if (context) {
			context->Release();
		}
		for (asIScriptContext* pooled : contextPool) {
			pooled->Release();
		}
		contextPool.clear();
		engine->ShutDownAndRelease();
	}

	void load_scripts() override {
		std::string scriptsDir = gloom::locate_scripts_dir(options.scripts_dir, options.argv0,
#ifdef GLOOM_SOURCE_SCRIPTS_DIR
			GLOOM_SOURCE_SCRIPTS_DIR,
#else
			"",
#endif
			"gloom_scripts_angelscript", "main.as");
		std::string entry = scriptsDir + "/main.as";
		CScriptBuilder builder;
		buildLog.clear();
		if (builder.StartNewModule(engine, "gloom") < 0 || builder.AddSectionFromFile(entry.c_str()) < 0 ||
			builder.BuildModule() < 0) {
			throw std::runtime_error("failed to build " + entry + "\n" + buildLog);
		}
		asIScriptModule* module = engine->GetModule("gloom");
		forceAutopilotFunction = entryPoint(module, "void gloom_force_autopilot()");
		bootFunction = entryPoint(module, "void gloom_boot(int, int)");
		frameFunction = entryPoint(module, "string gloom_frame(double, const string &in, double, double, double)");
		stateHashFunction = entryPoint(module, "int64 gloom_state_hash()");
		wantsQuitFunction = entryPoint(module, "bool gloom_wants_quit()");
		summaryFunction = entryPoint(module, "void gloom_summary()");
	}

	void force_autopilot() override {
		prepare(forceAutopilotFunction);
		execute("gloom_force_autopilot");
	}

	void boot(int64_t a_width, int64_t a_height) override {
		prepare(bootFunction);
		context->SetArgDWord(0, static_cast<asDWORD>(a_width));
		context->SetArgDWord(1, static_cast<asDWORD>(a_height));
		execute("gloom_boot");
	}

	std::string frame(double a_dt, const std::string& a_key, double a_fps, double a_msSim, double a_msDraw) override {
		prepare(frameFunction);
		context->SetArgDouble(0, a_dt);
		context->SetArgAddress(1, const_cast<std::string*>(&a_key));
		context->SetArgDouble(2, a_fps);
		context->SetArgDouble(3, a_msSim);
		context->SetArgDouble(4, a_msDraw);
		execute("gloom_frame");
		return *static_cast<std::string*>(context->GetReturnObject());
	}

	int64_t state_hash() override {
		prepare(stateHashFunction);
		execute("gloom_state_hash");
		return static_cast<int64_t>(context->GetReturnQWord());
	}

	bool wants_quit() override {
		prepare(wantsQuitFunction);
		execute("gloom_wants_quit");
		return context->GetReturnByte() != 0;
	}

	void summary() override {
		prepare(summaryFunction);
		execute("gloom_summary");
	}

	std::string stack_trace() override { return trace; }

	script_coroutine* makeCoroutine(asIScriptFunction* a_body) {
		if (!a_body) {
			if (asIScriptContext* active = asGetActiveContext()) {
				active->SetException("coroutine body is null");
			}
			return nullptr;
		}
		script_coroutine* result = new script_coroutine(this, a_body);
		a_body->Release();
		coroutines.insert(result);
		return result;
	}

	void forget(script_coroutine* a_coroutine) { coroutines.erase(a_coroutine); }

	asIScriptContext* acquireContext() {
		asIScriptContext* result = nullptr;
		if (contextPool.empty()) {
			result = engine->CreateContext();
			result->SetUserData(reinterpret_cast<void*>(COROUTINE_CONTEXT_TAG), COROUTINE_CONTEXT_TAG);
		} else {
			result = contextPool.back();
			contextPool.pop_back();
		}
		return result;
	}

	void recycleContext(asIScriptContext* a_context) {
		if (a_context->GetState() == asEXECUTION_SUSPENDED) {
			a_context->Abort();
		}
		a_context->Unprepare();
		contextPool.push_back(a_context);
	}

private:
	void hostLog(const std::string& a_line) { services.log(a_line); }
	bool keyDown(const std::string& a_name) { return services.key_down(a_name); }

	void onMessage(const asSMessageInfo* a_message) {
		const char* kind = a_message->type == asMSGTYPE_ERROR ? "error" :
			(a_message->type == asMSGTYPE_WARNING ? "warning" : "info");
		std::string line = std::string(a_message->section) + " (" + std::to_string(a_message->row) + ", " +
			std::to_string(a_message->col) + ") : " + kind + " : " + a_message->message + "\n";
		buildLog += line;
		if (a_message->type == asMSGTYPE_WARNING) {
			std::fputs(line.c_str(), stderr);
		}
	}

	static void check(int a_result) {
		if (a_result < 0) {
			throw std::runtime_error("AngelScript host registration failed: " + std::to_string(a_result));
		}
	}

	static asIScriptFunction* entryPoint(asIScriptModule* a_module, const char* a_declaration) {
		asIScriptFunction* result = a_module->GetFunctionByDecl(a_declaration);
		if (!result) {
			throw std::runtime_error(std::string("scripts do not define ") + a_declaration);
		}
		return result;
	}

	void prepare(asIScriptFunction* a_function) {
		trace.clear();
		if (context->Prepare(a_function) < 0) {
			throw std::runtime_error(std::string("cannot prepare ") + a_function->GetName());
		}
	}

	void execute(const char* a_name) {
		int status = context->Execute();
		if (status == asEXECUTION_FINISHED) {
			return;
		}
		if (status == asEXECUTION_EXCEPTION) {
			trace = call_stack(context);
			throw std::runtime_error(std::string(a_name) + ": " + exception_text(context));
		}
		if (status == asEXECUTION_SUSPENDED) {
			context->Abort();
		}
		throw std::runtime_error(std::string(a_name) + ": execution ended with status " + std::to_string(status));
	}

	gloom::host_options options;
	gloom::host_services services;
	int64_t seed;
	bool smoke;
	int64_t ticks;
	int64_t workers;
	bool god;
	int pix;
	std::string backend;
	std::string escape;
	asIScriptEngine* engine;
	asIScriptContext* context {nullptr};
	std::vector<asIScriptContext*> contextPool;
	std::unordered_set<script_coroutine*> coroutines;
	asIScriptFunction* forceAutopilotFunction {nullptr};
	asIScriptFunction* bootFunction {nullptr};
	asIScriptFunction* frameFunction {nullptr};
	asIScriptFunction* stateHashFunction {nullptr};
	asIScriptFunction* wantsQuitFunction {nullptr};
	asIScriptFunction* summaryFunction {nullptr};
	std::string buildLog;
	std::string trace;
};

script_coroutine::script_coroutine(angelscript_session* a_owner, asIScriptFunction* a_body) :
	owner(a_owner),
	context(a_owner->acquireContext()) {

	if (context->Prepare(a_body) < 0) {
		owner->recycleContext(context);
		context = nullptr;
	}
}

bool script_coroutine::resume() {
	if (!context) {
		return false;
	}
	asIScriptContext* caller = asGetActiveContext();
	int status = context->Execute();
	if (status == asEXECUTION_SUSPENDED) {
		return true;
	}
	if (status == asEXECUTION_EXCEPTION && caller) {
		std::string text = "in coroutine: " + exception_text(context) + "\n" + call_stack(context);
		caller->SetException(text.c_str());
	}
	abandon();
	return false;
}

void script_coroutine::abandon() {
	if (context) {
		owner->recycleContext(context);
		context = nullptr;
	}
	owner->forget(this);
}

class angelscript_adapter : public gloom::script_adapter {
public:
	std::string program_name() const override { return "gloom_angelscript"; }
	std::string language() const override { return "AngelScript 2.38"; }
	std::vector<std::string> backends() const override { return {"angelscript"}; }
	std::unique_ptr<gloom::script_session> make_session(const std::string& a_backend,
		const gloom::host_services& a_services, const gloom::host_options& a_options, bool) override {
		return std::make_unique<angelscript_session>(a_services, a_options, a_backend);
	}
};

}

int main(int argc, char** argv) {
	angelscript_adapter adapter;
	return gloom::run_gloom(argc, argv, adapter);
}
