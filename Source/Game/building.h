#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include <string>

class Wallet {
public:
	enum CurrencyType { GAME, SOFT, HARD, TOTAL };

	std::string name(CurrencyType a_type) {
		return names[static_cast<int>(a_type)];
	}

	int64_t value(CurrencyType a_type) {
		return values[static_cast<int>(a_type)];
	}

	int64_t add(CurrencyType a_type, size_t a_amount) {
		values[static_cast<int>(a_type)] += a_amount;
		return values[static_cast<int>(a_type)];
	}

	bool remove(CurrencyType a_type, size_t a_amount) {
		if (hasEnough(a_type, a_amount)) {
			values[static_cast<int>(a_type)] -= a_amount;
			return true;
		}
		return false;
	}

	bool hasEnough(CurrencyType a_type, size_t a_amount) const {
		return (values[static_cast<int>(a_type)] - a_amount) >= 0;
	}

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(values)
		);
	}
private:
	std::vector<int64_t> values = {0, 0, 0};
	std::vector<std::string> names = {"Gold", "Sweat", "Blood"};
};

struct BuildingData {
	double timeToSpawn;
	//std::vector<CharacterDefinition> characters;
	double gameCost;
	double
};

class Player {
	Wallet balance;
	std::vector<BuildingData> unlockedBuildings;
	std::wstring name;
	std::string passwordHash;
};

struct BuildingUpgradeData {
	Wallet cost;

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

#endif
