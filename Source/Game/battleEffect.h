#ifndef __BATTLE_EFFECT_H__
#define __BATTLE_EFFECT_H__

#include "MV/Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/signal.hpp"
#include <string>
#include <memory>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include "Game/Instance/team.h"
#include "Game/standardScriptMethods.h"

#include "MV/Network/dynamicVariable.h"
#include "MV/Network/networkObject.h"

#include "creature.h"

#define BATTLE_EFFECT_CATALOG_VERSION 0

class BattleEffect;

struct BattleEffectData {
	std::string id;

	bool isServer = false;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id)
		);
	}

	StandardScriptMethods<BattleEffect>& script(MV::Script&a_script) const {
		return scriptMethods.loadScript(a_script, "BattleEffects", id, isServer);
	}

private:
	mutable StandardScriptMethods<BattleEffect> scriptMethods;
};

enum class TargetType { NONE, CREATURE, GROUND };

class BattleEffectNetworkState {
public:
	BattleEffectNetworkState() {}
	BattleEffectNetworkState(GameInstance& a_gameInstance, const std::string &a_effectTypeId, int64_t a_creatureOwnerId, const std::string &a_creatureAttachPosition = "");

	int64_t creatureOwnerId = 0;

	std::function<void()> onNetworkDeath;
	std::function<void()> onNetworkSynchronize;

	int32_t buildingSlot = 0;

	std::string effectTypeId;
	MV::Point<> position;

	std::map<std::string, MV::DynamicVariable> variables;

	TargetType targetType = TargetType::NONE;
	int64_t targetCreatureId = 0;
	MV::Point<> targetPosition;
	double duration = 0.0;

	int64_t netId = 0;
	bool dying = false;

	void synchronize(std::shared_ptr<BattleEffectNetworkState> a_other) {
		position = a_other->position;
		variables = a_other->variables;
		targetType = a_other->targetType;
		targetCreatureId = a_other->targetCreatureId;
		targetPosition = a_other->targetPosition;
		buildingSlot = a_other->buildingSlot;
		duration = a_other->duration;

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
			cereal::make_nvp("targetCreatureId", targetCreatureId),
			cereal::make_nvp("buildingSlot", buildingSlot),
			cereal::make_nvp("targetPosition", targetPosition),
			cereal::make_nvp("duration", duration),
			cereal::make_nvp("targetType", targetType),
			cereal::make_nvp("position", position),
			cereal::make_nvp("variables", variables)
		);
	}
};

class BattleEffect : public MV::Scene::Component {
	friend MV::Script;
public:
	typedef void CallbackSignature(std::shared_ptr<BattleEffect>);
	typedef MV::SignalRegister<CallbackSignature>::SharedReceiverType SharedReceiverType;

protected:
	MV::Signal<CallbackSignature> onArriveSignal;
	MV::Signal<CallbackSignature> onFizzleSignal;

public:
	MV::SignalRegister<CallbackSignature> onArrive;
	MV::SignalRegister<CallbackSignature> onFizzle;

	ComponentDerivedAccessors(BattleEffect)
		
	~BattleEffect() { MV::info("Battle Effect Died: [", netId(), "]"); }

	std::string assetPath() const;

	bool alive() const {
		return !state->self()->dying;
	}

	int64_t netId() const {
		return state->id();
	}
	 
	MV::Point<MV::PointPrecision> gridPosition() const {
		return state->self()->position;
	}

	GameInstance& game() {
		return gameInstance;
	}

protected:
	BattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const BattleEffectData& a_statTemplate, std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_state);
	virtual void initialize() override;

	std::string skin;

	GameInstance& gameInstance;

	const BattleEffectData& statTemplate;

	double ourElapsedTime = 0.0;

	std::map<std::string, chaiscript::Boxed_Value> localVariables;

	std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> state;

	std::shared_ptr<Creature> sourceCreature;
	std::shared_ptr<Creature> targetCreature;

	ServerCreature::SharedReceiverType targetDeathWatcher;
	ServerCreature::SharedReceiverType sourceDeathWatcher;
};


class ServerBattleEffect : public BattleEffect {
	friend MV::Scene::Node;
	friend cereal::access;

public:
	ComponentDerivedAccessors(ServerBattleEffect)

protected:
	ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int64_t a_creatureId, const std::string &a_creatureBoneAttachment, GameInstance& a_gameInstance);
	ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const BattleEffectData& a_statTemplate, int64_t a_creatureId, const std::string &a_creatureBoneAttachment, GameInstance& a_gameInstance);
	ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_suppliedState, GameInstance& a_gameInstance);
	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		MV::require<MV::LogicException>(false, "CloneImplementation not implemented for ServerBattleEffect");
		return cloneHelper(a_parent->attach<ServerBattleEffect>(statTemplate.id, state->self()->creatureOwnerId, "", gameInstance).self());
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

protected:
	ClientBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_state, GameInstance& a_gameInstance);

	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<ClientBattleEffect>(state, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<ClientBattleEffect>(a_clone);
		return a_clone;
	}

private:
	void animateExplodeAndRemove() {
		owner()->removeFromParent();
	}

	void onNetworkSynchronize();

	virtual void updateImplementation(double a_delta) override;

	MV::Point<> startClientPosition;
	MV::Point<> activeTargetPosition;
	double discardedDuration = 0.0f;
	double accumulatedDuration = 0.0f;
	MV::TimeDeltaAggregate networkDelta;
	MV::PointPrecision offsetDepth = 0.0f;
};


#endif