#include "battleEffect.h"
#include "creature.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"

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
	return "BattleEffects/" + MV::toUpperFirstChar(statTemplate.id) + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
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
