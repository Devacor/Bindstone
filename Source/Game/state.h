#ifndef __MV_GAMESTATE_H__
#define __MV_GAMESTATE_H__

#include <memory>
#include <vector>
#include "creature.h"
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

class JsonNodeLoadBinder {
public:
	JsonNodeLoadBinder(Managers& a_managers, MV::MouseState& a_mouse) :
		managers(a_managers),
		mouse(a_mouse) {
	}

	void operator()(cereal::JSONInputArchive& a_archive);
private:
	Managers& managers;
	MV::MouseState& mouse;
};

class BinaryNodeLoadBinder {
public:
	BinaryNodeLoadBinder(Managers& a_managers, MV::MouseState& a_mouse) :
		managers(a_managers),
		mouse(a_mouse) {
	}

	void operator()(cereal::PortableBinaryInputArchive& a_archive);
private:
	Managers& managers;
	MV::MouseState& mouse;
};

class GameData {
public:
	GameData(Managers& a_managers) :
		allManagers(a_managers) {
		buildingCatalog = std::make_unique<BuildingCatalog>("buildings.json");
		creatureCatalog = std::make_unique<CreatureCatalog>("creatures.json");
	}

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
private:
	std::unique_ptr<BuildingCatalog> buildingCatalog;
	std::unique_ptr<CreatureCatalog> creatureCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};


#endif