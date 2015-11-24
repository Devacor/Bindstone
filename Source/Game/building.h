#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include "Game/wallet.h"
#include "chaiscript/chaiscript.hpp"
#include <string>

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

struct WaveData {
	double spawnDelay = 0.0;
	std::vector<std::pair<std::string, double>> creatures;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(spawnDelay),
			CEREAL_NVP(creatures)
		);
	}
};

struct BuildTree {
	WaveData wave;
	std::string icon;
	std::string asset;

	std::string name;
	std::string description;

	std::string script;

	int64_t cost = 0;

	std::vector<std::unique_ptr<BuildTree>> upgrades;

	BuildTree(){}
	BuildTree(const BuildTree& a_rhs) {
		wave = a_rhs.wave;
		icon = a_rhs.icon;
		asset = a_rhs.asset;
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
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(cost),
			CEREAL_NVP(icon),
			CEREAL_NVP(asset),
			CEREAL_NVP(wave),
			CEREAL_NVP(upgrades),
			CEREAL_NVP(script)
		);
	}
};

struct SkinData {
	std::string id;
	std::vector<Wallet> costs;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(costs)
		);
	}
};

struct BuildingData {
	std::string id;

	std::string icon;

	BuildTree game;

	std::string name;
	std::string description;

	std::vector<Wallet> costs;

	std::vector<SkinData> skins;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(icon), 
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(costs),
			CEREAL_NVP(game),
			CEREAL_NVP(skins)
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

	virtual void updateImplementation(double a_delta) override {};

protected:
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data) :
		Component(a_owner) {
		if (!a_owner.expired()) {
			auto spawnObject = a_owner.lock()->get("spawn", false);
			if (spawnObject) {
				spawnPoint = spawnObject->position();
			}
		}
	}

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>(data).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Building>(a_clone);
		return a_clone;
	}

	void upgrade(int index = 0) {
		
	}
private:

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
			cereal::make_nvp("data", data),
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
			);
		construct->initialize();
	}
	BuildingData data;
	MV::Point<> spawnPoint;
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