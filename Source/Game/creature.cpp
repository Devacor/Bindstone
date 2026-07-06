#include "creature.h"
#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "MV/Utility/generalUtility.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/signals/signal_binding.hpp>

static jai::registrar<CreatureData, MV::Services> _hookCreatureData("CreatureData",
	[](jai::dynamic_binder<CreatureData>& builder, const MV::Services&) {
	builder.property("id", &CreatureData::id);
	builder.property("name", &CreatureData::name);
	builder.property("description", &CreatureData::description);
	builder.property("moveSpeed", &CreatureData::moveSpeed);
	builder.property("actionSpeed", &CreatureData::actionSpeed);
	builder.property("castSpeed", &CreatureData::castSpeed);
	builder.property("health", &CreatureData::health);
	builder.property("defense", &CreatureData::defense);
	builder.property("will", &CreatureData::will);
	builder.property("strength", &CreatureData::strength);
	builder.property("ability", &CreatureData::ability);
	builder.property("isServer", &CreatureData::isServer);
});

static jai::registrar<StandardScriptMethods<Creature>, MV::Services> _hookCreatureScriptMethods("CreatureScriptMethods",
	[](jai::dynamic_binder<StandardScriptMethods<Creature>>& builder, const MV::Services&) {
	builder.property("spawn", &StandardScriptMethods<Creature>::scriptSpawn);
	builder.property("update", &StandardScriptMethods<Creature>::scriptUpdate);
	builder.property("death", &StandardScriptMethods<Creature>::scriptDeath);
});

static jai::registrar<TargetPolicy, MV::Services> _hookTargetPolicy("TargetPolicy",
	[](jai::dynamic_binder<TargetPolicy>& builder, const MV::Services&) {
	builder.method("target", [](TargetPolicy& a_self, int64_t a_target, double a_range, jai::script_value a_succeed) {
		a_self.target(a_target, static_cast<float>(a_range), jai::convert_script_function<void(TargetPolicy&)>(a_succeed));
	});
	builder.method("target", [](TargetPolicy& a_self, int64_t a_target, double a_range, jai::script_value a_succeed, jai::script_value a_fail) {
		a_self.target(a_target, static_cast<float>(a_range),
			jai::convert_script_function<void(TargetPolicy&)>(a_succeed),
			jai::convert_script_function<void(TargetPolicy&)>(a_fail));
	});
	builder.method("target", [](TargetPolicy& a_self, const MV::Point<>& a_location, jai::script_value a_succeed) {
		a_self.target(a_location, 0.0f, jai::convert_script_function<void(TargetPolicy&)>(a_succeed));
	});
	builder.method("self", &TargetPolicy::self);
	builder.method("active", &TargetPolicy::active);
	builder.method("stunned", &TargetPolicy::stunned);
	builder.method("rooted", &TargetPolicy::rooted);
});

void Creature::jai_auto_bind(jai::dynamic_binder<Creature>& builder) {
	builder.property("onStatus", [](Creature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onStatus; }, nullptr);
	builder.property("onHealthChange", [](Creature& a_self) -> jai::signal<void(std::shared_ptr<Creature>, int)>& { return a_self.onHealthChange; }, nullptr);
	builder.property("onDeath", [](Creature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onDeath; }, nullptr);
	builder.property("onFall", [](Creature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onFall; }, nullptr);
	builder.method("game", [](Creature& a_self) -> GameInstance& { return a_self.gameInstance; });
	builder.method("stats", [](Creature& a_self) { return a_self.statTemplate; });
	builder.method("team", [](Creature& a_self) -> Team& { return a_self.gameInstance.teamForPlayer(a_self.player()); });
	builder.method("enemyTeam", [](Creature& a_self) -> Team& { return a_self.gameInstance.teamAgainstPlayer(a_self.player()); });
	builder.method("setVar", [](Creature& a_self, jai::script_value a_key, jai::script_value a_value) {
		a_self.localVariables[a_key.as_string()] = std::move(a_value);
	});
	builder.method("getVar", [](Creature& a_self, jai::script_value a_key) {
		auto found = a_self.localVariables.find(a_key.as_string());
		return found != a_self.localVariables.end() ? found->second : jai::script_value(std::monostate{}, a_key.get_engine());
	});
}

static jai::registrar<Creature, MV::Services> _hookCreature("Creature",
	[](jai::dynamic_binder<Creature>& builder, const MV::Services&) {
	auto& eng = builder.bound_engine();
	jai::bind_signal_type<Creature::CallbackSignature>(eng, "SignalCreature");
	jai::bind_signal_type<void(std::shared_ptr<Creature>, int)>(eng, "SignalCreatureHealth");
	builder.method("spine", &Creature::spine);
	builder.method("alive", &Creature::alive);
	builder.method("networkId", &Creature::netId);
	builder.method("changeHealth", &Creature::changeHealth);
	builder.method("assetPath", &Creature::assetPath);
	builder.method("player", &Creature::player);
});

void ServerCreature::jai_auto_bind(jai::dynamic_binder<ServerCreature>& builder) {
	builder.property("onArrive", [](ServerCreature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onArrive; }, nullptr);
	builder.property("onBlocked", [](ServerCreature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onBlocked; }, nullptr);
	builder.property("onStop", [](ServerCreature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onStop; }, nullptr);
	builder.property("onStart", [](ServerCreature& a_self) -> jai::signal<Creature::CallbackSignature>& { return a_self.onStart; }, nullptr);
	builder.method("targeting", [](ServerCreature& a_self) -> TargetPolicy& { return a_self.targeting; });
	builder.method("enemiesInRange", [](ServerCreature& a_self, double a_range) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), static_cast<float>(a_range));
	});
	builder.method("alliesInRange", [](ServerCreature& a_self, double a_range) {
		return a_self.gameInstance.teamForPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), static_cast<float>(a_range));
	});
}

static jai::registrar<ServerCreature, MV::Services> _hookServerCreature("ServerCreature",
	[](jai::dynamic_binder<ServerCreature>& builder, const MV::Services&) {
	builder.base_class<Creature>();
	builder.method("agent", &ServerCreature::agent);
	builder.method("fall", &ServerCreature::fall);
});

static jai::registrar<ClientCreature, MV::Services> _hookClientCreature("ClientCreature",
	[](jai::dynamic_binder<ClientCreature>& builder, const MV::Services&) {
	builder.base_class<Creature>();
});

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

void ServerCreature::animateDeathAndRemove() {
	auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
	agent()->stop();
	statTemplate.script(gameInstance.script()).death(self);
	onDeathSignal(self);
	spineAnimator->animate(*state->self()->animationName, false);
	spineAnimator->onEnd.connect("!", [&](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
		if (a_self->track(a_track).name() == state->self()->animationName) {
			owner()->removeFromParent();
		}
	});
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
        auto a_target = std::dynamic_pointer_cast<ServerCreature>(self()->gameInstance.creature(a_targetId));
        
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

	auto self = std::static_pointer_cast<ClientCreature>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);

	gameInstance.registerCreature(self);
	onAnimationChanged();
	onNetworkSynchronize();
}

void ClientCreature::animateDeathAndRemove() {
	auto self = std::static_pointer_cast<Creature>(shared_from_this());
	statTemplate.script(gameInstance.script()).death(self);
	onDeathSignal(self);
	task().cancel();
	if (owner()->position() != *state->self()->position) {
		task().now("Tween", [&](MV::Task&, double a_dt) {
			owner()->position(MV::moveToward(owner()->position(), *state->self()->position, static_cast<MV::PointPrecision>(a_dt) * 200.0f));
			return owner()->position() != *state->self()->position;
		});
	}
	task().then("animateAway", [&](MV::Task&) {
		spineAnimator->animate(*state->self()->animationName, false);
		spineAnimator->onEnd.connect("!", [&](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
			if (a_self->track(a_track).name() == state->self()->animationName) {
				owner()->removeFromParent();
			}
		});
	});
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
