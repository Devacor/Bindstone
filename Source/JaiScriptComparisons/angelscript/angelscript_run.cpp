// angelscript_run: the stand-alone AngelScript host the benchmark apps and the kernel suite run under.
//   angelscript_run file.as
// Builds the file (with #include) into one module and calls its main(), `void main()` or `int main()`; an int result
// is the exit code. Registers std::string, array<T>, the math functions, dictionary, string utilities and
//   void print(const string &in)    the string plus a newline, on stdout
//   double clock()                  QueryPerformanceCounter seconds (the Lua kernel host's profile_native.clock)
//   int add_one(int)                the interop kernel's host call
//   double exp(double)              missing from the scriptmath add-on
//   void throw(const string &in)    raises a script exception (the scripthelper add-on)
// through the native calling convention.

#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scripthelper/scripthelper.h>
#include <scriptmath/scriptmath.h>
#include <scriptstdstring/scriptstdstring.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

void messageCallback(const asSMessageInfo* a_message, void*) {
	const char* kind = a_message->type == asMSGTYPE_ERROR ? "error" : a_message->type == asMSGTYPE_WARNING ? "warning" : "info";
	std::fprintf(stderr, "%s (%d, %d) : %s : %s\n", a_message->section, a_message->row, a_message->col, kind, a_message->message);
}

void print(const std::string& a_text) {
	std::fwrite(a_text.data(), 1, a_text.size(), stdout);
	std::fputc('\n', stdout);
}

double clockSeconds() {
#ifdef _WIN32
	LARGE_INTEGER frequency;
	LARGE_INTEGER now;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
#else
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

int addOne(int a_value) {
	return a_value + 1;
}

void registerHost(asIScriptEngine* a_engine) {
	RegisterStdString(a_engine);
	RegisterScriptArray(a_engine, true);
	RegisterStdStringUtils(a_engine);
	RegisterScriptMath(a_engine);
	RegisterScriptDictionary(a_engine);
	RegisterExceptionRoutines(a_engine);
	a_engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(print), asCALL_CDECL);
	a_engine->RegisterGlobalFunction("double clock()", asFUNCTION(clockSeconds), asCALL_CDECL);
	a_engine->RegisterGlobalFunction("int add_one(int)", asFUNCTION(addOne), asCALL_CDECL);
	a_engine->RegisterGlobalFunction("double exp(double)", asFUNCTIONPR(std::exp, (double), double), asCALL_CDECL);
}

}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: angelscript_run file.as\n");
		return 2;
	}
	asIScriptEngine* engine = asCreateScriptEngine();
	engine->SetMessageCallback(asFUNCTION(messageCallback), nullptr, asCALL_CDECL);
	registerHost(engine);

	CScriptBuilder builder;
	if (builder.StartNewModule(engine, "main") < 0 || builder.AddSectionFromFile(argv[1]) < 0 || builder.BuildModule() < 0) {
		std::fprintf(stderr, "angelscript_run: %s failed to build\n", argv[1]);
		engine->ShutDownAndRelease();
		return 1;
	}
	asIScriptModule* module = engine->GetModule("main");
	asIScriptFunction* entry = module->GetFunctionByDecl("void main()");
	if (!entry) {
		entry = module->GetFunctionByDecl("int main()");
	}
	if (!entry) {
		std::fprintf(stderr, "angelscript_run: %s has no main()\n", argv[1]);
		engine->ShutDownAndRelease();
		return 1;
	}

	int result = 0;
	asIScriptContext* context = engine->CreateContext();
	context->Prepare(entry);
	int status = context->Execute();
	if (status == asEXECUTION_EXCEPTION) {
		std::fprintf(stderr, "angelscript_run: %s\n", GetExceptionInfo(context, true).c_str());
		result = 1;
	} else if (status != asEXECUTION_FINISHED) {
		std::fprintf(stderr, "angelscript_run: execution ended with status %d\n", status);
		result = 1;
	} else if (entry->GetReturnTypeId() == asTYPEID_INT32) {
		result = static_cast<int>(context->GetReturnDWord());
	}
	std::fflush(stdout);
	context->Release();
	engine->ShutDownAndRelease();
	return result;
}
