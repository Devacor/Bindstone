#ifndef __STANDARD_SCRIPT_METHODS_H__
#define __STANDARD_SCRIPT_METHODS_H__

#include <memory>
#include <functional>
#include <map>
#include <string>
#include "MV/Script/script.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/stopwatch.h"
#include <jaiscript/core/engine.hpp>

template <typename DataType>
struct StandardScriptMethods {
	void spawn(std::shared_ptr<DataType> a_object) const {
		if (scriptSpawn) { scriptSpawn(a_object); }
	}

	void update(std::shared_ptr<DataType> a_object, double a_dt) const {
		if (scriptUpdate) { scriptUpdate(a_object, a_dt); }
	}

	void death(std::shared_ptr<DataType> a_object) const {
		if (scriptDeath) { scriptDeath(a_object); }
	}

	// Parse-once by default; with MV_SCRIPT_HOT_RELOAD=1 an mtime change re-evals the file
	// on the live engine (redefinition semantics keep existing instances working).
	StandardScriptMethods<DataType>& loadScript(MV::Script &a_script, const std::string &a_assetType, const std::string &a_id, bool a_isServer) {
		bool firstLoad = loadedFileTimestamp == 0;
		if (!firstLoad) {
			if (!MV::scriptHotReloadEnabled()) { return *this; }
			auto now = MV::Stopwatch::programTime(); // called per update; re-stat at most twice a second
			if (now < nextHotReloadCheck) { return *this; }
			nextHotReloadCheck = now + 0.5;
		}
		std::string filePath = a_assetType + "/" + a_id + (a_isServer ? "/main.script" : "/mainClient.script");
		std::time_t currentTimestamp = MV::lastFileWriteTime(filePath);
		if (!firstLoad && (currentTimestamp <= 0 || currentTimestamp == loadedFileTimestamp)) { return *this; }
		loadedFileTimestamp = currentTimestamp > 0 ? currentTimestamp : 1;
		scriptContents = MV::fileContents(filePath);
		if (!scriptContents.empty()) {
			MV::info(firstLoad ? "Loaded Script: [" : "Reloaded Script: [", filePath, "]");
			// Non-owning alias: `self` only has to outlive the eval (we own `this`,
			// and the catalog entry outlives the engine's use of it).
			auto self = a_script.engine().make_object(
				std::shared_ptr<StandardScriptMethods<DataType>>(this, [](StandardScriptMethods<DataType>*) {}));
			a_script.eval(filePath, scriptContents, { { "self", self } });
		} else {
			MV::error("Failed to load script for ", a_assetType, ": ", a_id);
		}
		return *this;
	}

	// Script-assigned hooks; registered as properties named "spawn"/"update"/"death"
	std::function<void(std::shared_ptr<DataType>)> scriptSpawn;
	std::function<void(std::shared_ptr<DataType>, double)> scriptUpdate;
	std::function<void(std::shared_ptr<DataType>)> scriptDeath;

private:
	std::string scriptContents;
	std::time_t loadedFileTimestamp = 0;
	double nextHotReloadCheck = 0.0;
};

#endif
