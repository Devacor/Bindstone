#include "state.h"
#include "player.h"

void JsonNodeLoadBinder::operator()(cereal::JSONInputArchive& a_archive) {
	a_archive.add(
		cereal::make_nvp("services", &managers.services),
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &managers.renderer),
		cereal::make_nvp("textLibrary", &managers.textLibrary),
		cereal::make_nvp("pool", &managers.pool),
		cereal::make_nvp("texture", &managers.textures),
		cereal::make_nvp("script", &script)
	);
}

void BinaryNodeLoadBinder::operator()(cereal::PortableBinaryInputArchive& a_archive) {
	a_archive.add(
		cereal::make_nvp("services", &managers.services),
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &managers.renderer),
		cereal::make_nvp("textLibrary", &managers.textLibrary),
		cereal::make_nvp("pool", &managers.pool),
		cereal::make_nvp("texture", &managers.textures),
		cereal::make_nvp("script", &script)
	);
}

GameData::GameData(Managers& a_managers, bool a_isServer) :
	allManagers(a_managers) {
	buildingCatalog = std::make_unique<Catalog<BuildingData>>("Buildings", a_isServer, BUILDING_CATALOG_VERSION);
	creatureCatalog = std::make_unique<Catalog<CreatureData>>("Creatures", a_isServer, CREATURE_CATALOG_VERSION);
	battleEffectCatalog = std::make_unique<Catalog<BattleEffectData>>("BattleEffects", a_isServer, BATTLE_EFFECT_CATALOG_VERSION);
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
