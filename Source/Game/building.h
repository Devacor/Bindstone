#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "MV/Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "MV/Utility/chaiscriptUtility.h"
#include "MV/Utility/signal.hpp"
#include <string>
#include <memory>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include "Game/creature.h"
#include "MV/Network/networkObject.h"

class GameInstance;
struct Player;

struct WaveCreature {
	std::string id;
	double delay;
	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
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
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
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
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
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
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
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
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
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
        MV::require<MV::ResourceException>(instream, "Failed to load BuildingCatalog: ", a_filename);
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
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("buildings", buildingList)
		);
	}

	std::vector<BuildingData> buildingList;
};

class Building : public MV::Scene::Component {
	friend MV::Scene::Node;
	friend cereal::access;
	MV::Signal<void(std::shared_ptr<Building>)> onUpgradedSignal;

public:
	MV::SignalRegister<void(std::shared_ptr<Building>)> onUpgraded;

	ComponentDerivedAccessors(Building)

	std::vector<size_t> buildTreeIndices;

	std::shared_ptr<Player> player() const { return owningPlayer; }

	const BuildTree* current() const;

	bool waveHasCreatures(size_t a_waveIndex = 0, size_t a_creatureIndex = 0) const;

	void upgrade(size_t a_index);
	void requestUpgrade(size_t a_index);

	void spawnNetworkCreature(std::shared_ptr<MV::NetworkObject<Creature::NetworkState>> a_synchronizedCreature);

	std::string assetPath() const {
		return "Assets/Buildings/" + buildingData.id + "/" + (skin().empty() ? "Default" : skin()) + "/building.prefab";
	}

	std::string skin() const;

	int slotIndex() const {
		return slot;
	}

	int loadoutIndex() const {
		return loadoutSlot;
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/) {
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

	MV::Point<> spawnPositionWorld() const {
		return owner()->worldFromLocal(spawnPoint);
	}

	const WaveCreature& currentCreature();
protected:
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner, int a_slot, int a_loadoutSlot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance);

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>(slot, loadoutSlot, owningPlayer, gameInstance).self());
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

	BuildingData buildingData;

	double countdown = 0;
	size_t waveIndex = 0;
	size_t creatureIndex = 0;

	int slot;
	int loadoutSlot;
	MV::Point<> spawnPoint;
	std::shared_ptr<Player> owningPlayer;
	GameInstance& gameInstance;
};


#endif
