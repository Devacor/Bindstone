#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"

class CreatureBehavior;

class Creature : public MV::Scene::Component {
	friend MV::Scene::Node;
	friend cereal::access;
	friend CreatureBehavior;

public:
	struct CombatAttributes {
		int maxHealth;
		int health;

		int speed;

		int magic;
		int focus;

		int power;
		int penetration;

		int resistance;
		int armor;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(maxHealth),
				CEREAL_NVP(health),
				CEREAL_NVP(speed),
				CEREAL_NVP(magic),
				CEREAL_NVP(focus),
				CEREAL_NVP(power),
				CEREAL_NVP(penetration),
				CEREAL_NVP(resistance),
				CEREAL_NVP(armor)
			);
		}
	};

	struct DescriptiveAttributes {
		std::string id;

		std::string name;
		std::string icon;
		std::string description;
		std::string spine;

		float spawn;
		int count;
		int cost;

		int income;
		float incomeMultiplier;

		std::vector<std::string> unlocked;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(id),
				CEREAL_NVP(name),
				CEREAL_NVP(icon),
				CEREAL_NVP(description),
				CEREAL_NVP(spawn),
				CEREAL_NVP(count),
				CEREAL_NVP(cost),
				CEREAL_NVP(income),
				CEREAL_NVP(unlocked)
			);
		}
	};

	ComponentDerivedAccessors(Creature)

	virtual void updateImplementation(double a_delta) override {}
	virtual bool draw() override;

	Creature::CombatAttributes combatAttributes() {
		return actualCombatAttributes;
	}

	Creature::DescriptiveAttributes buildingAttributes() {
		return actualBuildingAttributes;
	}

protected:
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner) :
		Component(a_owner){
	}

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Creature>().self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Creature>(a_clone);
		return a_clone;
	}
protected:
	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			cereal::make_nvp("ai", ai),
			cereal::make_nvp("buildingAttributes", actualCombatAttributes),
			cereal::make_nvp("combatAttributes", actualCombatAttributes),
			cereal::make_nvp("Component", cereal::base_class<Component>(this))
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Creature> &construct) {
		construct(std::shared_ptr<Node>());
		archive(
			cereal::make_nvp("ai", construct->ai),
			cereal::make_nvp("buildingAttributes", construct->actualCombatAttributes),
			cereal::make_nvp("combatAttributes", construct->actualCombatAttributes),
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
		);
		construct->initialize();
	}

	std::string playerId;

	CombatAttributes actualCombatAttributes;
	DescriptiveAttributes actualBuildingAttributes;

	std::unique_ptr<CreatureBehavior> ai;
};

class CreatureBehavior {
public:
	CreatureBehavior(MV::Draw2D& a_renderer) :
		visual(MV::Scene::Node::make(a_renderer)) {
	}

	virtual ~CreatureBehavior() = default;

	virtual void update(double a_delta) {}

	virtual void draw() {
		visual->draw();
	}
protected:
	Creature::CombatAttributes& combatAttributes() {
		return creature.lock()->actualCombatAttributes;
	}

	Creature::DescriptiveAttributes& buildingAttributes() {
		return creature.lock()->actualBuildingAttributes;
	}

	std::weak_ptr<Creature> creature;
	std::shared_ptr<MV::Scene::Node> visual;
};

class CreatureDictionary {
public:
	//MV::Scene::SafeComponent<Creature> spawn()
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
		return cloneHelper(a_parent->attach<Creature>().self());
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
			cereal::make_nvp("shouldDraw", construct->shouldDraw),
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
		);
		construct->initialize();
	}

	std::shared_ptr<CreatureDictionary> spawner;
	std::vector<std::vector<std::string>> templates;
};

#endif
