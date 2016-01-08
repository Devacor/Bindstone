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
private:
	std::shared_ptr<Player> localPlayer;
	std::unique_ptr<BuildingCatalog> buildingCatalog;
	std::unique_ptr<CreatureCatalog> creatureCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};

#endif