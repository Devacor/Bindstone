#ifndef __STANDARD_SCRIPT_METHODS_H__
#define __STANDARD_SCRIPT_METHODS_H__

#include <memory>
#include <functional>
#include "MV/Script/script.h"
#include "MV/Utility/generalUtility.h"

template <typename DataType>
struct StandardScriptMethods {
	friend DataType;
	friend MV::Script;

	void spawn(std::shared_ptr<DataType> a_object) const {
		if (scriptSpawn) { scriptSpawn(a_object); }
	}

	void update(std::shared_ptr<DataType> a_object, double a_dt) const {
		if (scriptUpdate) { scriptUpdate(a_object, a_dt); }
	}

	void death(std::shared_ptr<DataType> a_object) const {
		if (scriptDeath) { scriptDeath(a_object); }
	}

	StandardScriptMethods<DataType>& loadScript(MV::Script &a_script, const std::string &a_assetType, const std::string &a_id, bool a_isServer) {
		std::string filePath = a_assetType + "/" + a_id + (a_isServer ? "/main.script" : "/mainClient.script");
#ifdef WIN32
//#define MV_SCRIPT_HOTLOAD
#endif
#ifdef MV_SCRIPT_HOTLOAD
		time_t currentWriteTime = MV::lastFileWriteTime(filePath);
		bool shouldLoad = loadedFileTimestamp == 0 || loadedFileTimestamp != currentWriteTime;
		loadedFileTimestamp = currentWriteTime;
#else
		bool shouldLoad = loadedFileTimestamp == 0;
		loadedFileTimestamp = 1;
#endif
		if (shouldLoad) {
			scriptContents = MV::fileContents(filePath);
			if (!scriptContents.empty()) {
				MV::info("Loaded Script: [", filePath, "]");
				auto localVariables = std::map<std::string, chaiscript::Boxed_Value>{
					{ "self", chaiscript::Boxed_Value(this) }
				};
				a_script.eval(filePath, scriptContents, localVariables);
			} else {
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
