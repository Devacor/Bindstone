#ifndef __MV_GAMESTATE_H__
#define __MV_GAMESTATE_H__

#include <memory>
#include <vector>
#include "creature.h"
#include "Game/managers.h"

struct CreatureData;
struct BuildingData;
struct BattleEffectData;
struct InGamePlayer;

struct Constants {
	int startHealth = 20;
};

class GameData {
	friend MV::Script;
public:
	GameData(Managers& a_managers, bool a_isServer);

	Catalog<BuildingData>& buildings() {
		return *(buildingCatalog.get());
	}

	Catalog<CreatureData>& creatures() {
		return *(creatureCatalog.get());
	}

	Catalog<BattleEffectData>& battleEffects() {
		return *(battleEffectCatalog.get());
	}

	Constants& constants() {
		return metadataConstants;
	}

	Managers& managers() {
		return allManagers;
	}
private:
	std::unique_ptr<Catalog<BuildingData>> buildingCatalog;
	std::unique_ptr<Catalog<CreatureData>> creatureCatalog;
	std::unique_ptr<Catalog<BattleEffectData>> battleEffectCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};


#endif