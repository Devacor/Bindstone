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

class BattleEffectNetworkState {
public:
	uint64_t creatureOwnerId = 0;

	std::function<void()> onNetworkDeath;
	std::function<void()> onNetworkSynchronize;

	int32_t buildingSlot = 0;

	std::string effectTypeId;
	MV::Point<MV::PointPrecision> position;

	std::map<std::string, MV::DynamicVariable> variables;

	uint64_t netId = 0;
	bool dying = false;

	void synchronize(std::shared_ptr<BattleEffectNetworkState> a_other, bool destroying) {
		position = a_other->position;
		variables = a_other->variables;

		if (destroying) {
			std::cout << "Effect Expired: " << effectTypeId << std::endl;
			dying = true;
			if (onNetworkDeath) {
				onNetworkDeath();
			} else {
				MV::warning("Failed to call onNetworkDeath for effect [", effectTypeId, "]!");
			}
		} else {
			if (onNetworkSynchronize) {
				onNetworkSynchronize();
			}
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
	MV::Signal<CallbackSignature> onDeathSignal;

public:
	MV::SignalRegister<CallbackSignature> onDeath;

	ComponentDerivedAccessors(BattleEffect)

	~BattleEffect() { MV::info("Battle Effect Died: [", netId(), "]"); }

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance);

	std::string assetPath() const;

	bool alive() const {
		return !state->self()->dying;
	}

	virtual bool changeHealth(int amount) = 0;

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

#endif