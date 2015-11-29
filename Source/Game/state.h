#ifndef __MV_GAMESTATE_H__
#define __MV_GAMESTATE_H__

#include <memory>
#include <vector>
#include "Game/managers.h"

struct BuildingData;
class BuildingCatalog;
struct Player;

struct Constants {
	int startHealth = 20;
};

class LocalData {
public:
	LocalData(Managers& a_managers):
		allManagers(a_managers){
		buildingCatalog = std::make_unique<BuildingCatalog>("buildings.json");
	}

	std::shared_ptr<Player> player() const {
		return localPlayer;
	}

	bool isLocal(const std::shared_ptr<Player> &a_other) const;

	BuildingCatalog& buildings() {
		return *(buildingCatalog.get());
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
	Constants metadataConstants;
	Managers& allManagers;
};

#endif