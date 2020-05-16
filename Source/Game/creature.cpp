#include "creature.h"
#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "MV/Utility/generalUtility.h"

CEREAL_CLASS_VERSION(Catalog<CreatureData>, CREATURE_CATALOG_VERSION);
CEREAL_CLASS_VERSION(Catalog<BuildingData>, BUILDING_CATALOG_VERSION);

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const CreatureData& a_statTemplate, std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_state) :
	Component(a_owner),
	gameInstance(a_gameInstance),
	onStatus(onStatusSignal),
	onDeath(onDeathSignal),
	onFall(onFallSignal),
	onHealthChange(onHealthChangeSignal),
	skin(a_skin),
	statTemplate(a_statTemplate),
	state(a_state){
}

std::string Creature::assetPath() const {
	return "Creatures/" + MV::toUpperFirstChar(statTemplate.id) + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
}

std::shared_ptr<InGamePlayer> Creature::player() {
	return gameInstance.building(buildingSlot())->player();
}

ServerCreature::ServerCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int a_buildingSlot, GameInstance& a_gameInstance) :
	ServerCreature(a_owner, a_gameInstance.data().creatures().data(a_id), a_buildingSlot, a_gameInstance) {
}

ServerCreature::ServerCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const CreatureData &a_statTemplate, int a_buildingSlot, GameInstance& a_gameInstance) :
	Creature(a_owner, a_gameInstance, a_gameInstance.building(a_buildingSlot)->skin(), a_statTemplate, a_gameInstance.networkPool().spawn(std::make_shared<CreatureNetworkState>(a_statTemplate, a_buildingSlot))),
	onArrive(onArriveSignal),
	onBlocked(onBlockedSignal),
	onStop(onStopSignal),
	onStart(onStartSignal),
	targeting(this) {
}

void ServerCreature::initialize() {
	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);
	spineAnimator = newNode->componentInChildren<MV::Scene::Spine>().get();
	spineAnimator->animate("run");
	
	pathAgent = owner()->attach<MV::Scene::PathAgent>(gameInstance.path().self(), gameInstance.path()->gridFromLocal(gameInstance.path()->owner()->localFromWorld(owner()->worldPosition())), 3)->
		gridSpeed(statTemplate.moveSpeed);

	pathAgent->onStart.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStartSignal(std::static_pointer_cast<ServerCreature>(shared_from_this()));
	});
	pathAgent->onStop.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStopSignal(std::static_pointer_cast<ServerCreature>(shared_from_this()));
	});
	pathAgent->onArrive.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onArriveSignal(std::static_pointer_cast<ServerCreature>(shared_from_this()));
	});
	pathAgent->onBlocked.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onBlockedSignal(std::static_pointer_cast<ServerCreature>(shared_from_this()));
	});
	
	auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);

	gameInstance.registerCreature(self);
}

void ServerCreature::updateImplementation(double a_delta) {
	if (alive()) {
		auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);
		targeting.update(a_delta);

 		if (state->self()->position != owner()->position()) {
 			state->modify()->position = owner()->position();
 		}
		if (!state->destroyed()) {
			if (state->self()->animationName != spine()->track().name()) {
				state->modify()->animationName = spine()->track().name();
				state->modify()->animationLoops = spine()->track().looping();
			}
			state->modify()->animationTime = spine()->track().time();
		}
	}
}

TargetPolicy::TargetPolicy(ServerCreature *a_self) :
	selfCreature(a_self) {

	selfDeathReceiver = selfCreature->onDeath.connect([&](std::shared_ptr<Creature>) {
		clearTarget();
		stopAndFail();
		selfDeathReceiver.reset();
	});
}

TargetPolicy::~TargetPolicy() {
	clearTarget();
}

void TargetPolicy::target(int64_t a_targetId, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail /*= std::function<void(TargetPolicy &)>()*/) {
	if (!selfCreature->alive()) {
		a_fail(*this);
	}
	auto a_target = self()->gameInstance.creature(a_targetId);

	if (a_target.get() == targetCreature) { return; }

	clearTarget();
	if (!stunDuration && selfCreature) {
		if (MV::distance(selfCreature->agent()->gridPosition(), a_target->agent()->gridPosition()) <= a_range) {
			if (selfCreature->agent()->pathfinding()) {
				selfCreature->agent()->stop();
			}
			if (a_succeed) {
				a_succeed(*this);
			}
			return;
		} else if (!rootDuration) {
			targetCreature = a_target.get();
			succeed = a_succeed;
			fail = a_fail;
			range = a_range;
			targetDeathReceiver = targetCreature->onDeath.connect([&](std::shared_ptr<Creature>) {
				clearTarget();
				stopAndFail();
			});
			registerPathfindingListeners();
			selfCreature->agent()->gridGoal(targetCreature->agent()->gridPosition(), range);
			return;
		}
	}
	if (a_fail) {
		a_fail(*this);
	}
}

void TargetPolicy::target(const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail /*= std::function<void(TargetPolicy &)>()*/) {
	clearTarget();
	if (!stunDuration && selfCreature) {
		if (MV::distance(selfCreature->agent()->gridPosition(), a_location) <= a_range) {
			if (a_succeed) {
				a_succeed(*this);
			}
			return;
		}
		else if (!rootDuration) {
			succeed = a_succeed;
			fail = a_fail;
			range = a_range;
			registerPathfindingListeners();
			selfCreature->agent()->gridGoal(a_location, range);
			return;
		}
	}
	if (a_fail) {
		a_fail(*this);
	}
}

std::shared_ptr<ServerCreature> TargetPolicy::self() const {
	return std::static_pointer_cast<ServerCreature>(selfCreature->shared_from_this());
}

bool TargetPolicy::active() const {
	return selfCreature && selfCreature->alive() && selfCreature->agent()->pathfinding();
}

void TargetPolicy::update(double a_dt) {
	if (selfCreature->alive()) {
		stunDuration = std::max(0.0f, stunDuration - static_cast<float>(a_dt));
		rootDuration = std::max(0.0f, rootDuration - static_cast<float>(a_dt));

		if (selfCreature && targetCreature && !stunDuration && !rootDuration) {
			selfCreature->agent()->gridGoal(targetCreature->agent()->gridPosition(), range);
		}
	}
}

void TargetPolicy::stun(float a_duration) {
	stunDuration = std::max(a_duration, stunDuration);
	if (stunDuration > 0.0f) {
		clearTarget();
		stopAndFail();
	}
}

void TargetPolicy::root(float a_duration) {
	rootDuration = std::max(a_duration, rootDuration);
	if (rootDuration > 0.0f) {
		clearTarget();
		stopAndFail();
	}
}

void TargetPolicy::clearTarget() {
	arriveReceiver.reset();
	stopReceiver.reset();
	blockedReceiver.reset();
	targetCreature = nullptr;
	targetDeathReceiver.reset();
}

void TargetPolicy::stopAndFail() {
	if (selfCreature->alive() && selfCreature->agent()->pathfinding()) {
		selfCreature->agent()->stop();
		callFail();
	}
}

void TargetPolicy::callSucceed() {
	if (selfCreature->alive()) {
		auto tmpSucceed = succeed;
		clearCallbacks();
		if (tmpSucceed) {
			tmpSucceed(*this);
		}
	}
}

void TargetPolicy::callFail() {
	if (selfCreature->alive()) {
		auto tmpFail = fail;
		clearCallbacks();
		if (tmpFail) {
			tmpFail(*this);
		}
	}
}

void TargetPolicy::clearCallbacks() {
	succeed = std::function<void(TargetPolicy&)>();
	fail = std::function<void(TargetPolicy&)>();
}

void TargetPolicy::registerPathfindingListeners() {
	arriveReceiver = selfCreature->onArrive.connect([&](std::shared_ptr<Creature>) {
		clearTarget();
		callSucceed();
	});
	blockedReceiver = selfCreature->onBlocked.connect([&](std::shared_ptr<Creature>) {
		clearTarget();
		callFail();
	});
	stopReceiver = selfCreature->onStop.connect([&](std::shared_ptr<Creature>) {
		clearTarget();
		callFail();
	});
}


ClientCreature::ClientCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> &a_state, GameInstance& a_gameInstance) :
	Creature(a_owner, a_gameInstance, 
		a_gameInstance.building(*a_state->self()->buildingSlot)->skin(),
		a_gameInstance.data().creatures().data(*a_state->self()->creatureTypeId),
		a_state
	){
	state->self()->onNetworkDeath = [&]() {
		animateDeathAndRemove();
	};
	state->self()->onNetworkSynchronize = [&] {
		onNetworkSynchronize();
	};
	state->self()->onAnimationChanged = [&] {
		onAnimationChanged();
	};
}

void ClientCreature::initialize() {
	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);
	spineAnimator = newNode->componentInChildren<MV::Scene::Spine>().get();

	auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);

	gameInstance.registerCreature(self);
	onAnimationChanged();
	onNetworkSynchronize();
}

void ClientCreature::onNetworkSynchronize() {
	networkDelta.add(MV::Stopwatch::programTime());
	startClientPosition = owner()->position();
	accumulatedDuration = 0;
	spine()->track().time(state->self()->animationTime);
}

void ClientCreature::onAnimationChanged() {
	spine()->animate(*state->self()->animationName, state->self()->animationLoops);
}

void ClientCreature::updateImplementation(double a_delta) {
	if (alive()) {
		accumulatedDuration = std::min(networkDelta.delta(), accumulatedDuration + a_delta);
		double percentNetTimestep = accumulatedDuration / networkDelta.delta();
		owner()->position(mix(startClientPosition, *state->self()->position, static_cast<MV::PointPrecision>(percentNetTimestep)));

		auto self = std::static_pointer_cast<ClientCreature>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		owner()->depth(owner()->position().y);
	}
}
