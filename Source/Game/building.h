#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "chaiscript/chaiscript.hpp"
#include "Utility/signal.hpp"
#include <string>
#include <memory>

class GameInstance;
struct Player;

struct CreatureStats {
	std::string id;

	std::string name;
	std::string description;

	std::string icon;
	std::string asset;

	float speed;

	int maxHealth;
	int health;
	float defense;
	float resistance;
	float strength;
	float ability;

	std::string script;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(icon),
			CEREAL_NVP(asset),
			CEREAL_NVP(speed),
			CEREAL_NVP(maxHealth),
			CEREAL_NVP(health),
			CEREAL_NVP(defense),
			CEREAL_NVP(resistance),
			CEREAL_NVP(strength),
			CEREAL_NVP(ability),
			CEREAL_NVP(script)
		);
	}
};

struct WaveCreature {
	std::string id;
	double delay;
	template <class Archive>
	void serialize(Archive & archive) {
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
	void serialize(Archive & archive) {
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
	void serialize(Archive & archive) {
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
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(costs)
		);
	}
};

struct BuildingData {
private:
	MV::Signal<void(BuildingData*)> onUpgradedSignal;

public:
	MV::SignalRegister<void (BuildingData*)> onUpgraded;

	BuildingData() :
		onUpgraded(onUpgradedSignal) {
	}

	BuildingData(const BuildingData &a_other) :
		onUpgraded(onUpgradedSignal),
		id(a_other.id),
		game(a_other.game),
		buildTreeIndices(a_other.buildTreeIndices),
		name(a_other.name),
		description(a_other.description),
		skins(a_other.skins),
		costs(a_other.costs){
	}

	BuildingData(BuildingData &&a_other) :
		onUpgraded(onUpgradedSignal),
		onUpgradedSignal(std::move(a_other.onUpgradedSignal)),
		id(std::move(a_other.id)),
		game(std::move(a_other.game)),
		buildTreeIndices(std::move(a_other.buildTreeIndices)),
		name(std::move(a_other.name)),
		description(std::move(a_other.description)),
		skins(std::move(a_other.skins)),
		costs(std::move(a_other.costs)) {
		a_other.onUpgradedSignal = MV::Signal<void(BuildingData*)>();
	}

	std::string id;

	BuildTree game;
	std::vector<size_t> buildTreeIndices;

	BuildTree* current() {
		BuildTree* currentBuildTree = &game;
		for (auto& index : buildTreeIndices) {
			currentBuildTree = currentBuildTree->upgrades[index].get();
		}
		return currentBuildTree;
	}

	void upgrade(size_t a_index) {
		buildTreeIndices.push_back(a_index);
		onUpgradedSignal(this);
	}

	std::string skinAssetPath(const std::string &a_skinId) {
		return "Assets/Prefabs/Buildings/" + id + "/" + (a_skinId.empty() ? id : a_skinId) + ".prefab";
	}

	std::string name;
	std::string description;

	std::vector<SkinData> skins;

	std::vector<Wallet> costs;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(skins),
			CEREAL_NVP(costs),
			CEREAL_NVP(buildTreeIndices),
			CEREAL_NVP(game)
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<BuildingData> &construct) {
		construct();
		archive(
			cereal::make_nvp("id", construct->id),
			cereal::make_nvp("name", construct->name),
			cereal::make_nvp("description", construct->description),
			cereal::make_nvp("skins", construct->skins),
			cereal::make_nvp("costs", construct->costs),
			cereal::make_nvp("buildTreeIndices", construct->buildTreeIndices),
			cereal::make_nvp("game", construct->game)
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
	void serialize(Archive & archive) {
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

public:
	ComponentDerivedAccessors(Building)

	BuildingData& data() {
		return buildingData;
	}

protected:
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance);

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>(data(), skin, slot, owningPlayer, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Building>(a_clone);
		return a_clone;
	}

	virtual void updateImplementation(double a_dt) override;
private:

	virtual void initialize() override;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(data),
			cereal::make_nvp("Component", cereal::base_class<Component>(this))
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Building> &construct) {
		construct(std::shared_ptr<Node>());
		archive(
			cereal::make_nvp("data", buildingData),
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
			);
		construct->initialize();
	}

	BuildingData buildingData;
	std::string skin;
	double countdown = 0;
	int slot;
	MV::Point<> spawnPoint;
	std::shared_ptr<Player> owningPlayer;
	GameInstance& gameInstance;
};

class Creature : public MV::Scene::Component {
	friend MV::Scene::Node;
	friend cereal::access;

public:
	ComponentDerivedAccessors(Creature)

	virtual void updateImplementation(double a_delta) override {};

protected:
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner) :
		Component(a_owner) {
	}

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Creature>().self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Creature>(a_clone);
		return a_clone;
	}

private:

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			//CEREAL_NVP(shouldDraw),
			cereal::make_nvp("Component", cereal::base_class<Component>(this))
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Building> &construct) {
		construct(std::shared_ptr<Node>());
		archive(
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
		);
		construct->initialize();
	}

};

#endif