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
#include "MV/Utility/stringUtility.h"

class GameInstance;
struct InGamePlayer;
class Building;

struct BuildTree {
	std::string id;
	int64_t cost = 0;
	int64_t income = 0;

	std::vector<std::unique_ptr<BuildTree>> upgrades;

	BuildTree(){}
	BuildTree(const BuildTree& a_rhs) {
		id = a_rhs.id;
		cost = a_rhs.cost;
		income = a_rhs.income;
		for (auto&& upgrade : a_rhs.upgrades) {
			upgrades.emplace_back(std::make_unique<BuildTree>(*upgrade));
		}
	}

	std::string name() const {
		return id;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(cost),
			CEREAL_NVP(income),
			CEREAL_NVP(upgrades)
		);
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript& a_script);
};

struct SkinData {
	std::string id;
	std::vector<Wallet> costs;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(costs)
		);
	}
};

struct BuildingData {
	std::string id;
	BuildTree game;
	std::vector<SkinData> skins;

	std::vector<Wallet> costs;

	bool isServer = false;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(id),
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
	std::function<void()> onNetworkSynchronize;
	std::function<void()> onUpgrade;
	std::function<void()> onAnimationChanged;

	int32_t buildingSlot = 0;

	std::map<std::string, MV::DynamicVariable> variables;

	std::string animationName;
	bool animationLoops = false;

	std::vector<uint32_t> buildTreeIndices;

	BuildingNetworkState() {
	}

	BuildingNetworkState(int32_t a_buildingSlot) :
		buildingSlot(a_buildingSlot) {
	}

	void synchronize(std::shared_ptr<BuildingNetworkState> a_other) {
		variables = a_other->variables;
		if (buildTreeIndices.size() != a_other->buildTreeIndices.size()){
			buildTreeIndices = a_other->buildTreeIndices;
			if (onUpgrade) {
				onUpgrade();
			}
		}
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

	void destroy(std::shared_ptr<BuildingNetworkState> a_other) {
		std::cout << "Buildings don't die... >.>;" << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("slot", buildingSlot),
			cereal::make_nvp("animationName", animationName),
			cereal::make_nvp("animationLoops", animationLoops),
			cereal::make_nvp("variables", variables),
			cereal::make_nvp("buildTreeIndices", buildTreeIndices)
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

	std::shared_ptr<InGamePlayer> player() const { return owningPlayer; }

	const BuildTree* current() const;

	void upgrade(int a_index);
	void requestUpgrade(int a_index);

	void spawnNetworkCreature(std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_synchronizedCreature);

	void initializeNetworkState(std::shared_ptr<MV::NetworkObject<BuildingNetworkState>> a_networkState) {
		state = a_networkState;
		state->self()->onUpgrade = [&]() {
			buildingUpgraded();
		};
		state->self()->onAnimationChanged = [&] {
			spine()->animate(state->self()->animationName, state->self()->animationLoops);
		};
	}

	std::string assetPath() const {
		return "Buildings/" + MV::toUpperFirstChar(buildingData.id) + "/" + (skin().empty() ? "Default" : skin()) + "/building.prefab";
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

	void buildingUpgraded() {
		onUpgradedSignal(std::static_pointer_cast<Building>(shared_from_this()));
	}

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Building>(slot, loadoutSlot, owningPlayer, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Building>(a_clone);
		return a_clone;
	}

	virtual void updateImplementation(double a_dt) override;

	std::shared_ptr<MV::Scene::Spine> spine() {
		return spineAnimator;
	}
private:
	virtual void initialize() override;

	void initializeBuildingButton(const std::shared_ptr<MV::Scene::Node> &a_newNode);

	std::map<std::string, chaiscript::Boxed_Value> localVariables;
	std::shared_ptr<MV::NetworkObject<BuildingNetworkState>> state;

	BuildingData buildingData;

	int slot;
	int loadoutSlot;
	MV::Point<> spawnPoint;
	std::shared_ptr<InGamePlayer> owningPlayer;
	GameInstance& gameInstance;

	std::shared_ptr<MV::Scene::Spine> spineAnimator;
};


#endif
