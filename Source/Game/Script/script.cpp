#include "MV/Script/script.h"

#include <jaiscript/core/engine.hpp>
#include "MV/Utility/services.hpp"
#include "MV/Utility/generalUtility.h"

namespace MV {

	Script::Script(const Services& a_services, const std::vector<std::string>&) :
		engine_(a_services.get<jai::engine>()) {
	}

	bool Script::eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents) const {
		return eval(a_scriptIdentifier, a_scriptContents, {});
	}

	bool Script::eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
		const std::map<std::string, jai::script_value>& a_localVariables) const {

		// Shadow colliding globals for the duration of the eval, then restore them.
		std::vector<std::pair<std::string, jai::script_value>> shadowed;
		for (auto&& [name, value] : a_localVariables) {
			if (engine_->has_variable(name)) {
				shadowed.emplace_back(name, engine_->get_variable(name));
			}
			engine_->add_global(name, value);
		}
		bool success = true;
		try {
			engine_->execute(a_scriptContents);
		} catch (const std::exception& e) {
			MV::error("Script [", a_scriptIdentifier, "] failed: ", e.what());
			success = false;
		}
		for (auto&& [name, value] : shadowed) {
			engine_->add_global(name, value);
		}
		return success;
	}

	bool Script::evalBool(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
		bool a_default) const {
		try {
			auto result = engine_->execute(a_scriptContents);
			return result.is_bool() ? result.as_bool() : a_default;
		} catch (const std::exception& e) {
			MV::error("Script [", a_scriptIdentifier, "] failed: ", e.what());
			return a_default;
		}
	}

}
