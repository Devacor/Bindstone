#ifndef __MV_GAMESTATE_H__
#define __MV_GAMESTATE_H__

#include <memory>
#include <vector>
#include "Game/managers.h"

struct BuildingData;
struct CreatureData;
class BuildingCatalog;
class CreatureCatalog;
struct Player;

struct Constants {
	int startHealth = 20;

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Constants>(), "Constants");

		a_script.add(chaiscript::fun([](Constants &a_self) {
			return a_self.startHealth;
		}), "startHealth");

		return a_script;
	}
};

class LocalData {
public:
	LocalData(Managers& a_managers):
		allManagers(a_managers){
		buildingCatalog = std::make_unique<BuildingCatalog>("buildings.json");
		creatureCatalog = std::make_unique<CreatureCatalog>("creatures.json");
	}

	std::shared_ptr<Player> player() const {
		return localPlayer;
	}

	void player(std::shared_ptr<Player> a_localPlayer) {
		localPlayer = a_localPlayer;
	}

	std::shared_ptr<Player> player(const std::string &a_id) {
		return localPlayer; // replace this later with a lookup/download and likely a callback!
	}

	bool isLocal(const std::shared_ptr<Player> &a_other) const;

	BuildingCatalog& buildings() {
		return *(buildingCatalog.get());
	}

	CreatureCatalog& creatures() {
		return *(creatureCatalog.get());
	}

	Constants& constants() {
		return metadataConstants;
	}

	Managers& managers() {
		return allManagers;
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<LocalData>(), "LocalData");

		Constants::hook(a_script);

		a_script.add(chaiscript::fun([](LocalData &a_self) {
			return a_self.buildingCatalog.get();
		}), "buildings");
		a_script.add(chaiscript::fun([](LocalData &a_self) {
			return a_self.creatureCatalog.get();
		}), "creatures");
		a_script.add(chaiscript::fun([](LocalData &a_self) {
			return a_self.metadataConstants;
		}), "constants");

		return a_script;
	}
private:
	std::shared_ptr<Player> localPlayer;
	std::unique_ptr<BuildingCatalog> buildingCatalog;
	std::unique_ptr<CreatureCatalog> creatureCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};

#endif