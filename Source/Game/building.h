#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include "Game/wallet.h"
#include "Game/player.h"
#include "chaiscript/chaiscript.hpp"
#include <string>

struct MissileData {
	std::string launchAsset;
	std::string asset;
	std::string landAsset;

	float range;
	float damage;
	float speed;
};

struct CreatureStats {
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

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
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
			CEREAL_NVP(ability)
		);
	}
};

struct WaveData {
	double spawnDelay;
	std::vector<std::pair<CreatureStats, double>> creatures;

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

	int64_t cost;

	std::vector<std::shared_ptr<BuildTree>> path;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(cost),
			CEREAL_NVP(icon),
			CEREAL_NVP(asset),
			CEREAL_NVP(wave),
			CEREAL_NVP(path)
		);
	}
};

struct BuildingData {
	std::vector<Wallet> storeCosts;
	std::string icon;

	BuildTree upgrades;

	std::string id;

	std::string name;
	std::string description;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(icon), 
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(storeCosts)
		);
	}
};

class BuildingIndex {
public:
	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			cereal::make_nvp("buildings", buildingList)
		);
	}
private:
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
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner) :
		Component(a_owner) {
	}

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>().self());
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
		return cloneHelper(a_parent->attach<Building>().self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Creature>(a_clone);
		return a_clone;
	}

	void upgrade(int index = 0) {

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

class PlayerInGame {
	std::shared_ptr<Player> player;
	std::vector<std::shared_ptr<Building>> buildings;

};

#endif