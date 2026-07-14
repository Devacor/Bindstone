#include "battleEffect.h"
#include "creature.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/signals/signal_binding.hpp>

static jai::registrar<BattleEffectNetworkState, MV::Services> _hookBattleEffectNetworkState("BattleEffectNetworkState",
	[](jai::dynamic_binder<BattleEffectNetworkState>& builder, const MV::Services&) {
	auto& eng = builder.bound_engine();
	// TargetType constants: enums travel as ints through script
	eng.add_global("TargetType_NONE", eng.make_value(static_cast<int64_t>(TargetType::NONE)));
	eng.add_global("TargetType_CREATURE", eng.make_value(static_cast<int64_t>(TargetType::CREATURE)));
	eng.add_global("TargetType_GROUND", eng.make_value(static_cast<int64_t>(TargetType::GROUND)));
	builder.constructor<GameInstance&, const std::string&, int64_t>();
	builder.property("dying", &BattleEffectNetworkState::dying);
	builder.property("buildingSlot", &BattleEffectNetworkState::buildingSlot);
	builder.property("creatureOwnerId", &BattleEffectNetworkState::creatureOwnerId);
	builder.property("targetCreatureId", &BattleEffectNetworkState::targetCreatureId);
	builder.property("duration", &BattleEffectNetworkState::duration);
	builder.property("targetType",
		[](BattleEffectNetworkState& a_self) { return static_cast<int64_t>(a_self.targetType); },
		[](BattleEffectNetworkState& a_self, int64_t a_value) { a_self.targetType = static_cast<TargetType>(a_value); });
});

static jai::registrar<StandardScriptMethods<BattleEffect>, MV::Services> _hookBattleEffectScriptMethods("BattleEffectScriptMethods",
	[](jai::dynamic_binder<StandardScriptMethods<BattleEffect>>& builder, const MV::Services&) {
	builder.property("spawn", &StandardScriptMethods<BattleEffect>::scriptSpawn);
	builder.property("update", &StandardScriptMethods<BattleEffect>::scriptUpdate);
	builder.property("death", &StandardScriptMethods<BattleEffect>::scriptDeath);
});

void BattleEffect::jai_auto_bind(jai::dynamic_binder<BattleEffect>& builder) {
	builder.property("onArrive", [](BattleEffect& a_self) -> jai::signal<BattleEffect::CallbackSignature>& { return a_self.onArrive; }, nullptr);
	builder.property("onFizzle", [](BattleEffect& a_self) -> jai::signal<BattleEffect::CallbackSignature>& { return a_self.onFizzle; }, nullptr);
	builder.method("stats", [](BattleEffect& a_self) { return a_self.statTemplate; });
	builder.method("ourElapsedTime", [](BattleEffect& a_self) { return a_self.ourElapsedTime; });
	builder.method("sourceCreature", [](BattleEffect& a_self) { return a_self.sourceCreature; });
	builder.method("targetCreature", [](BattleEffect& a_self) { return a_self.targetCreature; });
	builder.method("setVar", [](BattleEffect& a_self, jai::script_value a_key, jai::script_value a_value) {
		a_self.localVariables[a_key.as_string()] = std::move(a_value);
	});
	builder.method("getVar", [](BattleEffect& a_self, jai::script_value a_key) {
		auto found = a_self.localVariables.find(a_key.as_string());
		return found != a_self.localVariables.end() ? found->second : jai::script_value(std::monostate{}, a_key.get_engine());
	});
}

static jai::registrar<BattleEffectData, MV::Services> _hookBattleEffectData("BattleEffectData",
	[](jai::dynamic_binder<BattleEffectData>& builder, const MV::Services&) {
	builder.property("id", &BattleEffectData::id);
});

static jai::registrar<BattleEffect, MV::Services> _hookBattleEffect("BattleEffect",
	[](jai::dynamic_binder<BattleEffect>& builder, const MV::Services&) {
	jai::bind_signal_type<BattleEffect::CallbackSignature>(builder.bound_engine(), "SignalBattleEffect");
	builder.method("game", &BattleEffect::game);
	builder.method("alive", &BattleEffect::alive);
	builder.method("networkId", &BattleEffect::netId);
	builder.method("assetPath", &BattleEffect::assetPath);
});

static jai::registrar<ServerBattleEffect, MV::Services> _hookServerBattleEffect("ServerBattleEffect",
	[](jai::dynamic_binder<ServerBattleEffect>& builder, const MV::Services&) {
	builder.base_class<BattleEffect>();
});

static jai::registrar<ClientBattleEffect, MV::Services> _hookClientBattleEffect("ClientBattleEffect",
	[](jai::dynamic_binder<ClientBattleEffect>& builder, const MV::Services&) {
	builder.base_class<BattleEffect>();
});

BattleEffectNetworkState::BattleEffectNetworkState(GameInstance& a_gameInstance, const std::string &a_effectTypeId, int64_t a_creatureOwnerId, const std::string &a_creatureAttachPosition) :
	creatureOwnerId(a_creatureOwnerId),
	effectTypeId(a_effectTypeId){

	auto ourOwner = a_gameInstance.creature(a_creatureOwnerId);
	if (ourOwner) {
		position = ourOwner->owner()->position() + ourOwner->spine()->slotPosition(a_creatureAttachPosition);
		buildingSlot = ourOwner->buildingSlot();
	}
}

std::string BattleEffect::assetPath() const {
	return "BattleEffects/" + MV::toUpperFirstChar(statTemplate.id) + "/" + (skin.empty() ? "Default" : skin) + "/unit.bindsnap";
}

BattleEffect::BattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const BattleEffectData& a_statTemplate, std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_state) :
	Component(a_owner),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onFizzle(onFizzleSignal),
	skin(a_skin),
	statTemplate(a_statTemplate),
	state(a_state),
	sourceCreature(a_gameInstance.creature(a_state->self()->creatureOwnerId)),
	targetCreature(a_gameInstance.creature(a_state->self()->targetCreatureId)){
	
	if (sourceCreature) {
		sourceDeathWatcher = sourceCreature->onDeath.connect([&](std::shared_ptr<Creature> a_self) {
			sourceCreature.reset();
			sourceDeathWatcher.reset();
		});
	}

	if (targetCreature) {
		targetDeathWatcher = targetCreature->onDeath.connect([&](std::shared_ptr<Creature> a_self) {
			targetCreature.reset();
			targetDeathWatcher.reset();
		});
	}
}

void BattleEffect::initialize() {
	Component::initialize();
}

ServerBattleEffect::ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int64_t a_creatureId, const std::string &a_creatureBoneAttachment, GameInstance& a_gameInstance) :
	ServerBattleEffect(a_owner, a_gameInstance.data().battleEffects().data(a_id), a_creatureId, a_creatureBoneAttachment, a_gameInstance) {
}

ServerBattleEffect::ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const BattleEffectData &a_statTemplate, int64_t a_creatureId, const std::string &a_creatureBoneAttachment, GameInstance& a_gameInstance) :
	BattleEffect(a_owner, a_gameInstance, a_gameInstance.building(a_gameInstance.creature(a_creatureId)->buildingSlot())->skin(), a_statTemplate, a_gameInstance.networkPool().spawn(std::make_shared<BattleEffectNetworkState>(a_gameInstance, a_statTemplate.id, a_creatureId, a_creatureBoneAttachment))) {
}

ServerBattleEffect::ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_suppliedState, GameInstance& a_gameInstance) :
	BattleEffect(a_owner, a_gameInstance, a_gameInstance.building(a_suppliedState->self()->buildingSlot)->skin(), a_gameInstance.data().battleEffects().data(a_suppliedState->self()->effectTypeId), a_suppliedState) {
}

void ServerBattleEffect::initialize() {
	BattleEffect::initialize();
	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);

	auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);
}

void ServerBattleEffect::animateDeathAndRemove() {
	auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
	statTemplate.script(gameInstance.script()).death(self);
	owner()->removeFromParent();
}

void ServerBattleEffect::updateImplementation(double a_delta) {
	if (alive()) {
		ourElapsedTime += a_delta;
		bool expiring = state->self()->duration > 0 && ourElapsedTime >= state->self()->duration;
		if (expiring) {
			ourElapsedTime = state->self()->duration;
		}

		auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		if (expiring) {
			onArriveSignal(std::static_pointer_cast<BattleEffect>(self));
		}

		if (state->self()->targetType == TargetType::NONE && state->self()->position != owner()->position()) {
			state->modify()->position = owner()->position();
		}

		if (expiring) {
			animateDeathAndRemove();
		}
	}
}


ClientBattleEffect::ClientBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_state, GameInstance& a_gameInstance) :
	BattleEffect (a_owner, a_gameInstance,
		a_gameInstance.building(a_state->self()->buildingSlot)->skin(),
		a_gameInstance.data().battleEffects().data(a_state->self()->effectTypeId),
		a_state
	) {
	if (state->self()->targetType == TargetType::NONE) {
		state->self()->onNetworkDeath = [&]() {
			animateExplodeAndRemove();
		};
	} else if (state->self()->targetType == TargetType::CREATURE && targetCreature) {
		activeTargetPosition = targetCreature->owner()->position();
	}
	state->self()->onNetworkSynchronize = [&] {
		onNetworkSynchronize();
	};
}

void ClientBattleEffect::initialize() {
	BattleEffect::initialize();
	offsetDepth = owner()->componentInParents<MV::Scene::PathMap>()->cellSize().height / 2.0f;

	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);

	auto self = std::static_pointer_cast<ClientBattleEffect>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);
	
	onNetworkSynchronize();
}

void ClientBattleEffect::animateExplodeAndRemove() {
	auto self = std::static_pointer_cast<ClientBattleEffect>(shared_from_this());
	statTemplate.script(gameInstance.script()).death(self);
	owner()->removeFromParent();
}

void ClientBattleEffect::onNetworkSynchronize() {
	networkDelta.add(MV::Stopwatch::programTime());
	startClientPosition = owner()->position();
	discardedDuration += accumulatedDuration;
	accumulatedDuration = 0;
}

void ClientBattleEffect::updateImplementation(double a_delta) {
	if (alive()) {
		if (state->self()->targetType == TargetType::NONE) {
			accumulatedDuration = std::min(networkDelta.delta(), accumulatedDuration + a_delta);
			double percentNetTimestep = accumulatedDuration / networkDelta.delta();
			MV::info("NetStep: ", percentNetTimestep, "% ==> [", accumulatedDuration, " / ", networkDelta.delta(), "]");
			owner()->position(mix(startClientPosition, state->self()->position, static_cast<MV::PointPrecision>(percentNetTimestep)));
		} else {
			auto targetTotalTime = state->self()->duration - discardedDuration;
			if (state->self()->targetType == TargetType::CREATURE && targetCreature) {
				activeTargetPosition = targetCreature->owner()->position();
			} else if (state->self()->targetType == TargetType::GROUND) {
				activeTargetPosition = state->self()->targetPosition;
			}
			accumulatedDuration = std::min(targetTotalTime, accumulatedDuration + a_delta);
			owner()->position(mix(startClientPosition, activeTargetPosition, static_cast<MV::PointPrecision>(accumulatedDuration / targetTotalTime)));
			if (accumulatedDuration == state->self()->duration) {
				animateExplodeAndRemove();
				return; //animateExplodeAndRemove destroys self :> Let's not continue right now.
			}
		}

		auto self = std::static_pointer_cast<ClientBattleEffect>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		owner()->depth(owner()->position().y - offsetDepth);
	}
}
