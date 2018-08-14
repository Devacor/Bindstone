#include "creature.h"
#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "Utility/generalUtility.h"

#include "chaiscript/chaiscript_stdlib.hpp"

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int a_buildingSlot, GameInstance& a_gameInstance) :
	Component(a_owner),
	statTemplate(a_gameInstance.data().creatures().data(a_id)),
	skin(a_gameInstance.building(a_buildingSlot)->skin()),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onBlocked(onBlockedSignal),
	onStop(onStopSignal),
	onStart(onStartSignal),
	onStatus(onStatusSignal),
	onDeath(onDeathSignal),
	onFall(onFallSignal),
	onHealthChange(onHealthChangeSignal),
	targeting(this),
	state(a_gameInstance.networkPool().spawn(std::make_shared<Creature::NetworkState>(statTemplate, a_buildingSlot))),
	isOnServer(true) {
}

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<Creature::NetworkState>> &a_state, GameInstance& a_gameInstance) :
	Component(a_owner),
	statTemplate(a_gameInstance.data().creatures().data(a_state->self()->creatureTypeId)),
	skin(a_gameInstance.building(a_state->self()->buildingSlot)->skin()),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onBlocked(onBlockedSignal),
	onStop(onStopSignal),
	onStart(onStartSignal),
	onStatus(onStatusSignal),
	onDeath(onDeathSignal),
	onFall(onFallSignal),
	onHealthChange(onHealthChangeSignal),
	targeting(this),
	state(a_state),
	isOnServer(false){
}

std::string Creature::assetPath() const {
	return "Assets/Creatures/" + statTemplate.id + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
}

void Creature::initialize() {
	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);
	newNode->componentInChildren<MV::Scene::Spine>()->animate("run");
	
	pathAgent = owner()->attach<MV::Scene::PathAgent>(gameInstance.path().self(), gameInstance.path()->gridFromLocal(gameInstance.path()->owner()->localFromWorld(owner()->worldPosition())), 3)->
		gridSpeed(statTemplate.moveSpeed);

	task().also("UpdateZOrder", [&](const MV::Task &, double) {
		owner()->depth(pathAgent->gridPosition().y);
		return false;
	});

	pathAgent->onStart.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStartSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	pathAgent->onStop.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStopSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	pathAgent->onArrive.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onArriveSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	pathAgent->onBlocked.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onBlockedSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	
	auto self = std::static_pointer_cast<Creature>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);

	gameInstance.registerCreature(self);
}

void Creature::updateImplementation(double a_delta) {
	if (alive()) {
		auto self = std::static_pointer_cast<Creature>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);
		targeting.update(a_delta);
	}
}

chaiscript::ChaiScript& Creature::hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/) {
	CreatureData::hook(a_script);
	a_script.add(chaiscript::user_type<Creature>(), "Creature");
	a_script.add(chaiscript::base_class<Component, Creature>());

	MV::SignalRegister<CallbackSignature>::hook(a_script);
	MV::SignalRegister<void(std::shared_ptr<Creature>, int)>::hook(a_script);
	a_script.add(chaiscript::fun(&Creature::onArrive), "onArrive");
	a_script.add(chaiscript::fun(&Creature::onBlocked), "onBlocked");
	a_script.add(chaiscript::fun(&Creature::onStop), "onStop");
	a_script.add(chaiscript::fun(&Creature::onStart), "onStart");

	a_script.add(chaiscript::fun(&Creature::onStatus), "onStatus");
	a_script.add(chaiscript::fun(&Creature::onHealthChange), "onHealthChange");
	a_script.add(chaiscript::fun(&Creature::onDeath), "onDeath");
	a_script.add(chaiscript::fun(&Creature::onFall), "onFall");

	a_script.add(chaiscript::fun([](Creature &a_self) -> GameInstance& {return a_self.gameInstance; }), "game");

	a_script.add(chaiscript::fun(&Creature::alive), "alive");

	a_script.add(chaiscript::fun(&Creature::variables), "variables");

	a_script.add(chaiscript::fun([&](Creature &a_self) {
		return a_self.state->id();
	}), "id");

	a_script.add(chaiscript::fun([](Creature &a_self){
		a_self.fall();
	}), "fall");

	a_script.add(chaiscript::fun([](Creature &a_self, int a_amount) {
		return a_self.changeHealth(a_amount);
	}), "changeHealth");

	a_script.add(chaiscript::fun([](Creature &a_self, float a_range) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "enemiesInRange");

	a_script.add(chaiscript::fun([](Creature &a_self, float a_range) {
		return a_self.gameInstance.teamForPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "alliesInRange");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.statTemplate;
	}), "stats");
	a_script.add(chaiscript::fun(&Creature::skin), "skin");
	a_script.add(chaiscript::fun(&Creature::pathAgent), "agent");
	a_script.add(chaiscript::fun(&Creature::player), "player");

	a_script.add(chaiscript::fun(&Creature::assetPath), "assetPath");
	a_script.add(chaiscript::fun([](Creature &a_self) {
		return &a_self.gameInstance;
	}), "gameInstance");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamForPlayer(a_self.player());
	}), "team");

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<Creature>>>("VectorCreature"));

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player());
	}), "enemyTeam");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_self) {
		a_self.reset();
	}), "reset");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs){
		return a_lhs.get() == a_rhs.get();
	}), "==");
	a_script.add(chaiscript::fun([](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs) {
		return a_lhs.get() != a_rhs.get();
	}), "!=");
	a_script.add(chaiscript::fun([]() {
		return std::shared_ptr<Creature>();
	}), "nullCreature");

	a_script.add(chaiscript::fun(&Creature::targeting), "targeting");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	TargetPolicy::hook(a_script);

	return a_script;
}

std::shared_ptr<Player> Creature::player() {
	return gameInstance.building(state->self()->buildingSlot)->player();
}

CreatureScriptMethods& CreatureScriptMethods::loadScript(chaiscript::ChaiScript &a_script, const std::string &a_id) {
	if (scriptContents == "NIL") {
		scriptContents = MV::fileContents("Assets/Creatures/" + a_id + "/main.script");
		if (!scriptContents.empty()) {
			auto localVariables = std::map<std::string, chaiscript::Boxed_Value>{
				{ "self", chaiscript::Boxed_Value(this) }
			};
			auto resetLocals = a_script.get_locals();
			a_script.set_locals(localVariables);
			SCOPE_EXIT{ a_script.set_locals(resetLocals); };
			a_script.eval(scriptContents);
		} else {
			MV::error("Failed to load script for creature: ", a_id);
		}
	}
	return *this;
}

TargetPolicy::TargetPolicy(Creature *a_self) :
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

void TargetPolicy::target(Creature* a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail /*= std::function<void(TargetPolicy &)>()*/) {
	if (a_target == targetCreature) { return; }

	clearTarget();
	if (!stunDuration && selfCreature) {
		if (MV::distance(selfCreature->agent()->gridPosition(), a_target->agent()->gridPosition()) <= a_range) {
			if (a_succeed) {
				a_succeed(*this);
			}
			return;
		}
		else if (!rootDuration) {
			targetCreature = a_target;
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

std::shared_ptr<Creature> TargetPolicy::self() const {
	return std::static_pointer_cast<Creature>(selfCreature->shared_from_this());
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

	a_script.add(chaiscript::fun([](TargetPolicy& a_self, Creature* a_target, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_target, 0.0f, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, Creature* a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed) {
		a_self.target(a_target, a_range, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, Creature* a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail) {
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
