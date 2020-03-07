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

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Constants>(), "Constants");

		a_script.add(chaiscript::fun([](Constants &a_self) {
			return a_self.startHealth;
		}), "startHealth");

		return a_script;
	}
};

class GameData {
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

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
private:
	std::unique_ptr<Catalog<BuildingData>> buildingCatalog;
	std::unique_ptr<Catalog<CreatureData>> creatureCatalog;
	std::unique_ptr<Catalog<BattleEffectData>> battleEffectCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};


#endif