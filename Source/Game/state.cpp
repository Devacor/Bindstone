#include "state.h"
#include "player.h"

GameData::GameData(Managers& a_managers, bool a_isServer) :
	allManagers(a_managers) {
	buildingCatalog = std::make_unique<Catalog<BuildingData>>("Buildings"s, a_isServer, BUILDING_CATALOG_VERSION);
	creatureCatalog = std::make_unique<Catalog<CreatureData>>("Creatures"s, a_isServer, CREATURE_CATALOG_VERSION);
	battleEffectCatalog = std::make_unique<Catalog<BattleEffectData>>("BattleEffects"s, a_isServer, BATTLE_EFFECT_CATALOG_VERSION);
}

chaiscript::ChaiScript& GameData::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<GameData>(), "GameData");

	Constants::hook(a_script);

	a_script.add(chaiscript::fun([](GameData &a_self) {
		return a_self.buildingCatalog.get();
	}), "buildings");
	a_script.add(chaiscript::fun([](GameData &a_self) {
		return a_self.creatureCatalog.get();
	}), "creatures");
	a_script.add(chaiscript::fun([](GameData &a_self) {
		return a_self.metadataConstants;
	}), "constants");

	return a_script;
}
