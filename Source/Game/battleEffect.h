#ifndef __BATTLE_EFFECT_H__
#define __BATTLE_EFFECT_H__

#include "MV/Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "MV/Utility/chaiscriptUtility.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/signal.hpp"
#include <string>
#include <memory>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include "Game/Instance/team.h"
#include "Game/standardScriptMethods.h"

#include "MV/Network/dynamicVariable.h"
#include "MV/Network/networkObject.h"

#define BATTLE_EFFECT_CATALOG_VERSION 0

class BattleEffect;

struct BattleEffectData {
	std::string id;

	bool isServer;

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		StandardScriptMethods<BattleEffect>::hook(a_script, "BattleEffect");

		a_script.add(chaiscript::fun(&BattleEffectData::id), "id");

		return a_script;
	}

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id)
		);
	}

	StandardScriptMethods<BattleEffect>& script(chaiscript::ChaiScript &a_script) const {
		return scriptMethods.loadScript(a_script, "BattleEffects", id, isServer);
	}

private:
	mutable StandardScriptMethods<BattleEffect> scriptMethods;
};

enum class TargetType { NONE, CREATURE, GROUND };

class BattleEffectNetworkState {
public:
	uint64_t creatureOwnerId = 0;

	std::function<void()> onNetworkDeath;
	std::function<void()> onNetworkSynchronize;

	int32_t buildingSlot = 0;

	std::string effectTypeId;
	MV::Point<> position;

	std::map<std::string, MV::DynamicVariable> variables;

	TargetType targetType = TargetType::NONE;
	uint64_t targetCreature = 0;
	MV::Point<> targetPosition;


	uint64_t netId = 0;
	bool dying = false;

	void synchronize(std::shared_ptr<BattleEffectNetworkState> a_other) {
		position = a_other->position;
		variables = a_other->variables;
		targetType = a_other->targetType;
		targetCreature = a_other->targetCreature;
		targetPosition = a_other->targetPosition;

		if (onNetworkSynchronize) {
			onNetworkSynchronize();
		}
	}

	void destroy(std::shared_ptr<BattleEffectNetworkState> a_other) {
		std::cout << "Effect Expired: " << effectTypeId << std::endl;
		dying = true;
		if (onNetworkDeath) {
			onNetworkDeath();
		} else {
			MV::warning("Failed to call onNetworkDeath for effect [", effectTypeId, "]!");
		}
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("effectTypeId", effectTypeId),
			cereal::make_nvp("creatureOwnerId", creatureOwnerId),
			cereal::make_nvp("position", position),
			cereal::make_nvp("variables", variables)
		);
	}
};

class BattleEffect : public MV::Scene::Component {
public:
	typedef void CallbackSignature(std::shared_ptr<BattleEffect>);
	typedef MV::SignalRegister<CallbackSignature>::SharedRecieverType SharedRecieverType;

protected:
	MV::Signal<CallbackSignature> onArriveSignal;
	MV::Signal<CallbackSignature> onFizzleSignal;

public:
	MV::SignalRegister<CallbackSignature> onArrive;
	MV::SignalRegister<CallbackSignature> onFizzle;

	ComponentDerivedAccessors(BattleEffect)
		
	~BattleEffect() { MV::info("Battle Effect Died: [", netId(), "]"); }

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance);

	std::string assetPath() const;

	bool alive() const {
		return !state->self()->dying;
	}

	uint64_t netId() const {
		return state->id();
	}
	 
	MV::Point<MV::PointPrecision> gridPosition() const {
		return state->self()->position;
	}

protected:
	BattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const BattleEffectData& a_statTemplate, std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_state);

	std::string skin;

	GameInstance& gameInstance;

	const BattleEffectData& statTemplate;

	std::map<std::string, chaiscript::Boxed_Value> localVariables;

	std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> state;
};


class ServerBattleEffect : public BattleEffect {
	friend MV::Scene::Node;
	friend cereal::access;
	friend TargetPolicy;

public:
	ComponentDerivedAccessors(ServerBattleEffect)

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance);

protected:
	ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int a_buildingSlot, GameInstance& a_gameInstance);
	ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const BattleEffectData& a_statTemplate, int a_buildingSlot, GameInstance& a_gameInstance);
	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<ServerBattleEffect>(statTemplate.id, state->self()->buildingSlot, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<ServerBattleEffect>(a_clone);
		return a_clone;
	}

private:

	void flagForDeath() {
		if (!state->destroyed()) {
			auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
			state->self()->dying = true;
			state->destroy();

			animateDeathAndRemove();
		}
	}

	void animateDeathAndRemove() {
		owner()->removeFromParent();
	}

	virtual void updateImplementation(double a_delta) override;
};

class ClientBattleEffect : public BattleEffect {
	friend MV::Scene::Node;
	friend cereal::access;
public:
	ComponentDerivedAccessors(ClientBattleEffect)

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance);

protected:
	ClientBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_state, GameInstance& a_gameInstance);

	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<ClientBattleEffect>(state, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<ClientCreature>(a_clone);
		return a_clone;
	}

private:
	void animateDeathAndRemove() {
		owner()->removeFromParent();
	}

	void onNetworkSynchronize();

	virtual void updateImplementation(double a_delta) override;

	MV::Point<> startClientPosition;
	double accumulatedDuration = 0.0f;
	MV::TimeDeltaAggregate networkDelta;
	MV::PointPrecision offsetDepth = 0.0f;
};


#endif