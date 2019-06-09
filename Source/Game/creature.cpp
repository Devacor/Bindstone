#include "creature.h"
#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "MV/Utility/generalUtility.h"

#include "chaiscript/chaiscript_stdlib.hpp"

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
	return "Assets/Creatures/" + statTemplate.id + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
}

std::shared_ptr<InGamePlayer> Creature::player() {
	return gameInstance.building(state->self()->buildingSlot)->player();
}

void CreatureNetworkState::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<CreatureNetworkState>(), "CreatureNetworkState");

	a_script.add(chaiscript::fun(&CreatureNetworkState::dying), "dying");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "health");
	a_script.add(chaiscript::fun(&CreatureNetworkState::position), "position");
	a_script.add(chaiscript::fun(&CreatureNetworkState::buildingSlot), "buildingSlot");
	a_script.add(chaiscript::fun(&CreatureNetworkState::animationName), "animationName");
	a_script.add(chaiscript::fun(&CreatureNetworkState::animationLoops), "animationLoops");

	a_script.add(chaiscript::fun([](CreatureNetworkState &a_self, const std::string &a_key) {
		return a_self.variables[a_key];
	}), "[]");
}

chaiscript::ChaiScript& Creature::hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/) {
	CreatureData::hook(a_script);
	CreatureNetworkState::hook(a_script);
	a_script.add(chaiscript::user_type<Creature>(), "Creature");
	a_script.add(chaiscript::base_class<Component, Creature>());

	MV::SignalRegister<CallbackSignature>::hook(a_script);
	MV::SignalRegister<void(std::shared_ptr<Creature>, int)>::hook(a_script);
	a_script.add(chaiscript::fun(&Creature::onStatus), "onStatus");
	a_script.add(chaiscript::fun(&Creature::onHealthChange), "onHealthChange");
	a_script.add(chaiscript::fun(&Creature::onDeath), "onDeath");
	a_script.add(chaiscript::fun(&Creature::onFall), "onFall");
	a_script.add(chaiscript::fun(&Creature::spineAnimator), "spine");

	a_script.add(chaiscript::fun([](Creature &a_self) -> GameInstance& {return a_self.gameInstance; }), "game");

	a_script.add(chaiscript::fun(&Creature::alive), "alive");

	a_script.add(chaiscript::fun([](Creature &a_self, const std::string &a_key){
		return a_self.localVariables[a_key];
	}), "[]");

	a_script.add(chaiscript::fun([&](Creature &a_self) -> decltype(auto) {
		return a_self.state->modify()->variables;
	}), "setNetValue");

	a_script.add(chaiscript::fun([&](Creature &a_self) -> decltype(auto) {
		return a_self.state->self()->variables;
	}), "getNetValue");

	a_script.add(chaiscript::fun([&](Creature &a_self) {
		return a_self.state->id();
	}), "networkId");

	a_script.add(chaiscript::fun([](Creature &a_self, int a_amount) {
		return a_self.changeHealth(a_amount);
	}), "changeHealth");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.statTemplate;
	}), "stats");
	a_script.add(chaiscript::fun(&Creature::skin), "skin");
	a_script.add(chaiscript::fun(&Creature::player), "player");

	a_script.add(chaiscript::fun(&Creature::assetPath), "assetPath");
	a_script.add(chaiscript::fun([](Creature &a_self) {
		return &a_self.gameInstance;
	}), "gameInstance");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamForPlayer(a_self.player());
	}), "team");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player());
	}), "enemyTeam");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_self) {
		a_self.reset();
	}), "reset");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs) {
		return a_lhs.get() == a_rhs.get();
	}), "==");
	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs) {
		return a_lhs.get() != a_rhs.get();
	}), "!=");
	a_script.add(chaiscript::fun([]() {
		return std::shared_ptr<Creature>();
	}), "nullCreature");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<Creature>>>("VectorCreature"));

	return a_script;
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

chaiscript::ChaiScript& ServerCreature::hook(chaiscript::ChaiScript &a_script, GameInstance& a_gameInstance) {
	Creature::hook(a_script, a_gameInstance);
	a_script.add(chaiscript::user_type<ServerCreature>(), "ServerCreature");
	a_script.add(chaiscript::base_class<Creature, ServerCreature>());
	a_script.add(chaiscript::base_class<Component, ServerCreature>());

	a_script.add(chaiscript::fun(&ServerCreature::onArrive), "onArrive");
	a_script.add(chaiscript::fun(&ServerCreature::onBlocked), "onBlocked");
	a_script.add(chaiscript::fun(&ServerCreature::onStop), "onStop");
	a_script.add(chaiscript::fun(&ServerCreature::onStart), "onStart");

	a_script.add(chaiscript::fun(&ServerCreature::pathAgent), "agent");

	a_script.add(chaiscript::fun(&ServerCreature::targeting), "targeting");

	a_script.add(chaiscript::fun([](ServerCreature &a_self, float a_range) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "enemiesInRange");

	a_script.add(chaiscript::fun([](ServerCreature &a_self, float a_range) {
		return a_self.gameInstance.teamForPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "alliesInRange");

	a_script.add(chaiscript::fun([](ServerCreature &a_self) {
		a_self.fall();
	}), "fall");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<ServerCreature> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<ServerCreature> &a_item) { return std::static_pointer_cast<Creature>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ServerCreature> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<ServerCreature>>>("VectorServerCreature"));

	TargetPolicy::hook(a_script);

	return a_script;
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

chaiscript::ChaiScript& TargetPolicy::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<TargetPolicy>(), "TargetPolicy");

	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_target, 0.0f, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_target, a_range, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail) {
		a_self.target(a_target, a_range, a_succeed, a_fail);
	}), "target");

	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<> &a_location, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_location, 0.0f, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_location, a_range, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail) {
		a_self.target(a_location, a_range, a_succeed, a_fail);
	}), "target");
	a_script.add(chaiscript::fun(&TargetPolicy::active), "active");

	a_script.add(chaiscript::fun(&TargetPolicy::stun), "stunned");
	a_script.add(chaiscript::fun(&TargetPolicy::root), "rooted");

	a_script.add(chaiscript::fun(&TargetPolicy::self), "self");

	return a_script;
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
		a_gameInstance.building(a_state->self()->buildingSlot)->skin(), 
		a_gameInstance.data().creatures().data(a_state->self()->creatureTypeId), 
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

chaiscript::ChaiScript& ClientCreature::hook(chaiscript::ChaiScript &a_script, GameInstance& a_gameInstance) {
	Creature::hook(a_script, a_gameInstance);
	a_script.add(chaiscript::user_type<ClientCreature>(), "ClientCreature");
	a_script.add(chaiscript::base_class<Creature, ClientCreature>());
	a_script.add(chaiscript::base_class<Component, ClientCreature>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<ClientCreature> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<ClientCreature> &a_item) { return std::static_pointer_cast<Creature>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ClientCreature> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<ClientCreature>>>("VectorClientCreature"));

	return a_script;
}

void ClientCreature::onNetworkSynchronize() {
	networkDelta.add(MV::Stopwatch::systemTime());
	startClientPosition = owner()->position();
	accumulatedDuration = 0;
	spine()->track().time(state->self()->animationTime);
}

void ClientCreature::onAnimationChanged() {
	spine()->animate(state->self()->animationName, state->self()->animationLoops);
}

void ClientCreature::updateImplementation(double a_delta) {
	if (alive()) {
		accumulatedDuration = std::min(networkDelta.delta(), accumulatedDuration + a_delta);
		double percentNetTimestep = accumulatedDuration / networkDelta.delta();
		MV::info("NetStep: ", percentNetTimestep, "% ==> [", accumulatedDuration, " / ", networkDelta.delta(), "]");
		owner()->position(mix(startClientPosition, state->self()->position, static_cast<MV::PointPrecision>(percentNetTimestep)));

		auto self = std::static_pointer_cast<ClientCreature>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		owner()->depth(owner()->position().y);
	}
}
