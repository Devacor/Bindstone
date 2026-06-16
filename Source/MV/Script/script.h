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
