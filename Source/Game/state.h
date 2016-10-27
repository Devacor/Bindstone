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
	JsonNodeLoadBinder(Managers& a_managers, MV::MouseState& a_mouse, chaiscript::ChaiScript &a_script) :
		managers(a_managers),
		mouse(a_mouse),
		script(a_script){
	}

	void operator()(cereal::JSONInputArchive& a_archive);
private:
	Managers& managers;
	MV::MouseState& mouse;
	chaiscript::ChaiScript &script;
};

class BinaryNodeLoadBinder {
public:
	BinaryNodeLoadBinder(Managers& a_managers, MV::MouseState& a_mouse, chaiscript::ChaiScript &a_script) :
		managers(a_managers),
		mouse(a_mouse),
		script(a_script){
	}

	void operator()(cereal::PortableBinaryInputArchive& a_archive);
private:
	Managers& managers;
	MV::MouseState& mouse;
	chaiscript::ChaiScript &script;
};

class GameData {
public:
	GameData(Managers& a_managers);

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

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
private:
	std::unique_ptr<BuildingCatalog> buildingCatalog;
	std::unique_ptr<CreatureCatalog> creatureCatalog;
	Constants metadataConstants;
	Managers& allManagers;
};


#endif