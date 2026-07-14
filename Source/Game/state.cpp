#include "state.h"
#include "player.h"
#include "building.h"
#include "battleEffect.h"

#include <jaiscript/core/registrar.hpp>

// Template instantiations have no portable auto-name; each catalog registers explicitly.
// Enumeration = ids() + data(id); the data types register in their own TUs.
template <typename DataType>
static void bindCatalog(jai::dynamic_binder<Catalog<DataType>>& builder) {
	builder.method("data", &Catalog<DataType>::data);
	builder.method("has", &Catalog<DataType>::has);
	builder.method("ids", &Catalog<DataType>::ids);
	builder.method("size", [](Catalog<DataType>& a_self) { return static_cast<int64_t>(a_self.size()); });
}

static jai::registrar<Catalog<CreatureData>, MV::Services> _hookCreatureCatalog("CreatureCatalog",
	[](jai::dynamic_binder<Catalog<CreatureData>>& builder, const MV::Services&) { bindCatalog(builder); });

static jai::registrar<Catalog<BuildingData>, MV::Services> _hookBuildingCatalog("BuildingCatalog",
	[](jai::dynamic_binder<Catalog<BuildingData>>& builder, const MV::Services&) { bindCatalog(builder); });

static jai::registrar<Catalog<BattleEffectData>, MV::Services> _hookBattleEffectCatalog("BattleEffectCatalog",
	[](jai::dynamic_binder<Catalog<BattleEffectData>>& builder, const MV::Services&) { bindCatalog(builder); });

static jai::registrar<Constants, MV::Services> _hookConstants("Constants",
	[](jai::dynamic_binder<Constants>& builder, const MV::Services&) {
	builder.property("startHealth", &Constants::startHealth);
});

static jai::registrar<GameData, MV::Services> _hookGameData("GameData",
	[](jai::dynamic_binder<GameData>& builder, const MV::Services&) {
	builder.method("buildings", [](GameData& a_self) -> Catalog<BuildingData>& { return a_self.buildings(); });
	builder.method("creatures", [](GameData& a_self) -> Catalog<CreatureData>& { return a_self.creatures(); });
	builder.method("battleEffects", [](GameData& a_self) -> Catalog<BattleEffectData>& { return a_self.battleEffects(); });
	builder.method("constants", [](GameData& a_self) -> Constants& { return a_self.constants(); });
});

GameData::GameData(Managers& a_managers, bool a_isServer) :
	allManagers(a_managers) {
	buildingCatalog = std::make_unique<Catalog<BuildingData>>("Buildings"s, a_isServer);
	creatureCatalog = std::make_unique<Catalog<CreatureData>>("Creatures"s, a_isServer);
	battleEffectCatalog = std::make_unique<Catalog<BattleEffectData>>("BattleEffects"s, a_isServer);
}
