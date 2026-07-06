#ifndef _MV_SCRIPT_H_
#define _MV_SCRIPT_H_

#include <string>
#include <map>
#include <memory>
#include <vector>

namespace jai {
	class engine;
	class script_value;
}

namespace MV {
	class Services;

	// One place for engine policy: VM backend (env MV_SCRIPT_INTERPRETER=1 forces the
	// tree-walking interpreter for parity debugging), stdlib, Services-bound registrars,
	// script-error logging via MV::error, a memory cap, and static_checking(warn) in
	// debug builds (diagnostics surface through Script::eval as MV::warning).
	// Callers still connect the engine into their Services themselves.
	std::shared_ptr<jai::engine> makeScriptEngine(const Services& a_services, size_t a_memoryCapBytes = 256 * 1024 * 1024);

	// Env MV_SCRIPT_HOT_RELOAD=1: gameplay .script files re-eval when their mtime changes
	// (see StandardScriptMethods::loadScript). Off = parse-once shipping behavior.
	bool scriptHotReloadEnabled();

	// Forward declarations only here; the implementation TU (Source/Game/Script/script.cpp) includes the engine.
	class Script {
	public:
		explicit Script(jai::engine& a_engine) : engine_(&a_engine) {}
		explicit Script(const Services& a_services, const std::vector<std::string>& = {});

		jai::engine& engine() const { return *engine_; }

		// Locals are injected as temporary globals (load-time `self` pattern; single-threaded only).
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents) const;
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			const std::map<std::string, jai::script_value>& a_localVariables) const;

		bool evalBool(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			bool a_default) const;

	private:
		jai::engine* engine_;
	};
}

#endif
