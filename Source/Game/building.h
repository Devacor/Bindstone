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
struct InGamePlayer;
class Building;

struct BuildTree {
	std::string id;

	std::string name;
	std::string description;

	int64_t cost = 0;
	int64_t income = 0;

	std::vector<std::unique_ptr<BuildTree>> upgrades;

	BuildTree(){}
	BuildTree(const BuildTree& a_rhs) {
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
			CEREAL_NVP(upgrades)
		);
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript& a_script);
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

	bool isServer = false;

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

	StandardScriptMethods<Building>& script(chaiscript::ChaiScript& a_script) const {
		return scriptMethods.loadScript(a_script, "Buildings", id, isServer);
	}

private:
	mutable StandardScriptMethods<Building> scriptMethods;
};

struct BuildingNetworkState {
	int64_t netId;

	std::function<void()> onNetworkSynchronize;
	std::function<void()> onAnimationChanged;

	std::string buildingTypeId;
	int32_t buildingSlot;

	std::map<std::string, MV::DynamicVariable> variables;

	std::string animationName;
	bool animationLoops = false;

	BuildingNetworkState() {
	}

	BuildingNetworkState(const BuildingData& a_template, int32_t a_buildingSlot) :
		buildingTypeId(a_template.id),
		buildingSlot(a_buildingSlot) {
	}

	void synchronize(std::shared_ptr<CreatureNetworkState> a_other) {
		variables = a_other->variables;
		if (animationName != a_other->animationName || animationLoops != a_other->animationLoops) {
			animationName = a_other->animationName;
			animationLoops = a_other->animationLoops;
			if (onAnimationChanged) {
				onAnimationChanged();
			}
		}

		if (onNetworkSynchronize) {
			onNetworkSynchronize();
		}
	}

	void destroy(std::shared_ptr<CreatureNetworkState> a_other) {
		std::cout << "Buildings don't die... >.>;" << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("creatureTypeId", buildingTypeId),
			cereal::make_nvp("slot", buildingSlot),
			cereal::make_nvp("animationName", animationName),
			cereal::make_nvp("animationLoops", animationLoops),
			cereal::make_nvp("variables", variables)
		);
	}

	static void hook(chaiscript::ChaiScript &a_script);
};

class Building : public MV::Scene::Component {
	friend MV::Scene::Node;
	friend cereal::access;
	MV::Signal<void(std::shared_ptr<Building>)> onUpgradedSignal;

public:
	MV::SignalRegister<void(std::shared_ptr<Building>)> onUpgraded;

	ComponentDerivedAccessors(Building)

	std::vector<size_t> buildTreeIndices;

	std::shared_ptr<InGamePlayer> player() const { return owningPlayer; }

	const BuildTree* current() const;

	void upgrade(size_t a_index);
	void requestUpgrade(size_t a_index);

	void spawnNetworkCreature(std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_synchronizedCreature);

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

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/);

	MV::Point<> spawnPositionWorld() const {
		return owner()->worldFromLocal(spawnPoint);
	}

protected:
	Building(const std::weak_ptr<MV::Scene::Node> &a_owner, int a_slot, int a_loadoutSlot, const std::shared_ptr<InGamePlayer> &a_player, GameInstance& a_instance);

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

	std::map<std::string, chaiscript::Boxed_Value> localVariables;

	BuildingData buildingData;

	double countdown = 0;
	size_t waveIndex = 0;
	size_t creatureIndex = 0;

	int slot;
	int loadoutSlot;
	MV::Point<> spawnPoint;
	std::shared_ptr<InGamePlayer> owningPlayer;
	GameInstance& gameInstance;
};


#endif
