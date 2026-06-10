#ifndef _MV_SCRIPT_H_
#define _MV_SCRIPT_H_

#include <string>
#include <map>
#include <vector>

namespace jai {
	class engine;
	class script_value;
}

namespace MV {
	class Services;

	// Thin facade over the services-connected jai::engine (replaces the ChaiScript-era
	// PIMPL; the name survives so `friend MV::Script` and accessor signatures do too).
	// Lightweight on purpose: only forward declarations here, the implementation TU
	// (Source/Game/Script/script.cpp) includes the engine.
	class Script {
	public:
		explicit Script(jai::engine& a_engine) : engine_(&a_engine) {}
		// Fetches the jai::engine connected to services. Paths parameter is vestigial
		// (kept so ChaiScript-era construction sites compile unchanged).
		explicit Script(const Services& a_services, const std::vector<std::string>& = {});

		jai::engine& engine() const { return *engine_; }

		// Evaluates source; errors are logged with a_scriptIdentifier. Locals are
		// temporarily visible as globals (the load-time `self` injection pattern;
		// single-threaded engines only).
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents) const;
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			const std::map<std::string, jai::script_value>& a_localVariables) const;

		// Evaluates source and returns its boolean result (a_default on error/non-bool).
		bool evalBool(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			bool a_default) const;

	private:
		jai::engine* engine_;
	};
}

#endif
