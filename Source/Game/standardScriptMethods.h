#ifndef __STANDARD_SCRIPT_METHODS_H__
#define __STANDARD_SCRIPT_METHODS_H__

#include <memory>
#include <functional>
#include "MV/Utility/generalUtility.h"

namespace chaiscript { class ChaiScript; }

template <typename DataType>
struct StandardScriptMethods {
	friend DataType;

	void spawn(std::shared_ptr<DataType> a_object) const {
		if (scriptSpawn) { scriptSpawn(a_object); }
	}

	void update(std::shared_ptr<DataType> a_object, double a_dt) const {
		if (scriptUpdate) { scriptUpdate(a_object, a_dt); }
	}

	void death(std::shared_ptr<DataType> a_object) const {
		if (scriptDeath) { scriptDeath(a_object); }
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, const std::string &a_name) {
		a_script.add(chaiscript::user_type<StandardScriptMethods<DataType>>(), a_name + "ScriptMethods");

		a_script.add(chaiscript::fun(&StandardScriptMethods<DataType>::scriptSpawn), "spawn");
		a_script.add(chaiscript::fun(&StandardScriptMethods<DataType>::scriptUpdate), "update");
		a_script.add(chaiscript::fun(&StandardScriptMethods<DataType>::scriptDeath), "death");

		return a_script;
	}

	StandardScriptMethods<DataType>& loadScript(chaiscript::ChaiScript &a_script, const std::string &a_assetType, const std::string &a_id, bool a_isServer) {
		std::string filePath = a_assetType + "/" + a_id + (a_isServer ? "/main.script" : "/mainClient.script");
		time_t currentWriteTime = MV::lastFileWriteTime(filePath);
		if (loadedFileTimestamp == 0 || loadedFileTimestamp != currentWriteTime) {
			scriptContents = MV::fileContents(filePath);
			if (!scriptContents.empty()) {
				MV::info("Loaded Script: [", filePath, "]");
				loadedFileTimestamp = currentWriteTime;
				auto localVariables = std::map<std::string, chaiscript::Boxed_Value>{
					{ "self", chaiscript::Boxed_Value(this) }
				};
				auto resetLocals = a_script.get_locals();
				a_script.set_locals(localVariables);
				SCOPE_EXIT{ a_script.set_locals(resetLocals); };
				a_script.eval(scriptContents);
			} else {
				loadedFileTimestamp = 1;
				MV::error("Failed to load script for ", a_assetType, ": ", a_id);
			}
		}
		return *this;
	}
private:
	std::function<void(std::shared_ptr<DataType>)> scriptSpawn;
	std::function<void(std::shared_ptr<DataType>, double)> scriptUpdate;
	std::function<void(std::shared_ptr<DataType>)> scriptDeath;

	std::string scriptContents;
	std::time_t loadedFileTimestamp = 0;
};

#endif
