#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "chaiscript/chaiscript.hpp"
#include "Utility/signal.hpp"
#include <string>
#include <memory>
#include "ArtificialIntelligence/pathfinding.h"

class GameInstance;
struct Player;
struct CreatureData;
class Creature;

struct WaveCreature {
	std::string id;
	double delay;
	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(delay),
			CEREAL_NVP(id)
		);
	}
};

struct WaveData {
	int iterations;
	std::vector<WaveCreature> creatures;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(iterations),
			CEREAL_NVP(creatures)
		);
	}
};

struct BuildTree {
	std::vector<WaveData> waves;
	std::string id;

	std::string name;
	std::string description;

	int64_t cost = 0;
	int64_t income = 0;

	std::vector<std::unique_ptr<BuildTree>> upgrades;

	BuildTree(){}
	BuildTree(const BuildTree& a_rhs) {
		waves = a_rhs.waves;
		id = a_rhs.id;
		name = a_rhs.name;
		description = a_rhs.description;
		cost = a_rhs.cost;
		for (auto&& upgrade : a_rhs.upgrades) {
			upgrades.emplace_back(std::make_unique<BuildTree>(*upgrade));
		}
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(cost),
			CEREAL_NVP(income),
			CEREAL_NVP(waves),
			CEREAL_NVP(upgrades)
		);
	}
};

struct SkinData {
	std::string id;
	std::string name;
	std::vector<Wallet> costs;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(costs)
		);
	}
};

struct BuildingData {
	std::string id;

	BuildTree game;

	std::string name;
	std::string description;

	std::vector<SkinData> skins;

	std::vector<Wallet> costs;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(skins),
			CEREAL_NVP(costs),
			CEREAL_NVP(game)
		);
	}
};

class BuildingCatalog {
	friend cereal::access;
public:
	BuildingCatalog(const std::string &a_filename) {
		std::ifstream instream(a_filename);
		cereal::JSONInputArchive archive(instream);

		archive(cereal::make_nvp("buildings", buildingList));
	}

	BuildingData data(const std::string &a_id) const {
		for (auto&& building : buildingList) {
			if (building.id == a_id) {
				return building;
			}
		}
		MV::require<MV::ResourceException>(false, "Failed to locate building: ", a_id);
		return BuildingData();
	}
private:
	BuildingCatalog() {
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			cereal::make_nvp("buildings", buildingList)
		);
	}

	std::vector<BuildingData> buildingList;
};

class Gem {

};

class Building : public MV::Scene::Component {
	friend MV::Scene::Node;
	friend cereal::access;
	MV::Signal<void(std::shared_ptr<Building>)> onUpgradedSignal;

public:
	MV::SignalRegister<void(std::shared_ptr<Building>)> onUpgraded;

	ComponentDerivedAccessors(Building)

	std::vector<size_t> buildTreeIndices;

	const BuildTree* current() const;

	bool waveHasCreatures(size_t a_waveIndex = 0, size_t a_creatureIndex = 0) const;

	void upgrade(size_t a_index);

	std::string assetPath() const {
		return "Assets/Prefabs/Buildings/" + buildingData.id + "/" + (skin.empty() ? buildingData.id : skin) + ".prefab";
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance) {
		a_script.add(chaiscript::user_type<Building>(), "Building");
		a_script.add(chaiscript::base_class<Component, Building>());

		a_script.add(chaiscript::fun(&Building::current), "current");
		a_script.add(chaiscript::fun(&Building::upgrade), "upgrade");

		a_script.add(chaiscript::fun(&Building::buildingData), "data");
		a_script.add(chaiscript::fun(&Building::skin), "skin");
		a_script.add(chaiscript::fun(&Building::slot), "slot");
		a_script.add(chaiscript::fun(&Building::owningPlayer), "player");

		a_script.add(chaiscript::fun(&Building::assetPath), "assetPath");

		a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<Building>>([](const MV::Scene::SafeComponent<Building> &a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Building> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

		return a_script;
	}
protected:
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance);

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>(buildingData, skin, slot, owningPlayer, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Building>(a_clone);
		return a_clone;
	}

	virtual void updateImplementation(double a_dt) override;
private:
	virtual void initialize() override;

	void initializeBuildingButton(const std::shared_ptr<MV::Scene::Node> &a_newNode);

	void incrementCreatureIndex();

	const WaveCreature& currentCreature();
	void spawnCurrentCreature();

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			cereal::make_nvp("data", buildingData),
			cereal::make_nvp("skin", skin),
			cereal::make_nvp("slot", slot),
			cereal::make_nvp("player", owningPlayer->id),
			cereal::make_nvp("Component", cereal::base_class<Component>(this))
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Building> &construct, std::uint32_t const version) {
		std::string skin;
		BuildingData buildingData;
		int slot;
		std::string playerId;

		archive(
			cereal::make_nvp("data", buildingData),
			cereal::make_nvp("skin", skin),
			cereal::make_nvp("slot", slot),
			cereal::make_nvp("player", playerId)
		);
		GameInstance *gameInstance = nullptr;
		archive.extract(cereal::make_nvp("game", gameInstance));
		MV::require<PointerException>(gameInstance != nullptr, "Null gameInstance in Building::load_and_construct.");
		gameInstance->data().player()
		construct(std::shared_ptr<Node>(), buildingData, skin, slot, player, *gameInstance);
		archive(
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
		);
		construct->initialize();
	}

	BuildingData buildingData;
	std::string skin;

	double countdown = 0;
	size_t waveIndex = 0;
	size_t creatureIndex = 0;

	int slot;
	MV::Point<> spawnPoint;
	std::shared_ptr<Player> owningPlayer;
	GameInstance& gameInstance;
};


#endif