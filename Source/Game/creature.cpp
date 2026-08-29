#include "creature.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <cstdlib>
#include <iostream>
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
	builder.method("target", [](TargetPolicy& a_self, const MV::Point<>& a_location, int64_t a_mode, jai::script_value a_succeed) {
		a_self.target(a_location, 0.0f, static_cast<MV::PathMode>(a_mode), jai::convert_script_function<void(TargetPolicy&)>(a_succeed));
	});
	builder.method("target", [](TargetPolicy& a_self, const MV::Point<>& a_location, double a_range, int64_t a_mode, jai::script_value a_succeed) {
		a_self.target(a_location, static_cast<float>(a_range), static_cast<MV::PathMode>(a_mode), jai::convert_script_function<void(TargetPolicy&)>(a_succeed));
	});
	builder.method("self", &TargetPolicy::self);
	builder.method("active", &TargetPolicy::active);
	builder.method("stunned", &TargetPolicy::stunned);
	builder.method("clear", &TargetPolicy::clear);
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

static jai::registrar<Creature, MV::Services, jai::bases<MV::Scene::Component>> _hookCreature("Creature",
	[](jai::dynamic_binder<Creature>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	auto& eng = builder.bound_engine();
	jai::bind_signal_type<Creature::CallbackSignature>(eng, "SignalCreature");
	jai::bind_signal_type<void(std::shared_ptr<Creature>, int)>(eng, "SignalCreatureHealth");
	builder.method("spine", &Creature::spine);
	builder.method("alive", &Creature::alive);
	builder.method("networkId", &Creature::netId);
	builder.method("element", &Creature::element);
	builder.method("elements", &Creature::elements);
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
	// ids cross into script without touching the creature objects (no copies, no handles)
	builder.method("nearestEnemyId", [](ServerCreature& a_self, double a_range) -> int64_t {
		auto found = a_self.gameInstance.teamAgainstPlayer(a_self.player()).nearestCreature(a_self.agent()->gridPosition(), static_cast<float>(a_range));
		return found ? found->netId() : 0;
	});
	builder.method("nearestAllyId", [](ServerCreature& a_self, double a_range) -> int64_t {
		auto found = a_self.gameInstance.teamForPlayer(a_self.player()).nearestCreature(a_self.agent()->gridPosition(), static_cast<float>(a_range));
		return (found && found.get() != &a_self) ? found->netId() : 0;
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

	builder.method("damage", [](ServerCreature& a_self, int64_t a_amount, int64_t a_source) { return static_cast<int64_t>(a_self.damage(static_cast<int>(a_amount), a_source)); });
	builder.method("heal", [](ServerCreature& a_self, int64_t a_amount) { a_self.heal(static_cast<int>(a_amount)); });
	builder.method("shield", [](ServerCreature& a_self, int64_t a_amount) { a_self.shield(static_cast<int>(a_amount)); });
	builder.method("shieldAmount", [](ServerCreature& a_self) { return static_cast<int64_t>(a_self.shieldAmount()); });
	builder.method("dot", [](ServerCreature& a_self, double a_perSecond, double a_seconds, int64_t a_source) { a_self.dot(a_perSecond, a_seconds, a_source); });
	builder.method("hot", [](ServerCreature& a_self, double a_perSecond, double a_seconds) { a_self.hot(a_perSecond, a_seconds); });
	builder.method("slow", [](ServerCreature& a_self, double a_fraction, double a_seconds) { a_self.slow(a_fraction, a_seconds); });
	builder.method("haste", [](ServerCreature& a_self, double a_fraction, double a_seconds) { a_self.haste(a_fraction, a_seconds); });
	builder.method("attackHaste", [](ServerCreature& a_self, double a_fraction, double a_seconds) { a_self.attackHaste(a_fraction, a_seconds); });
	builder.method("moveSpeedMultiplier", &ServerCreature::moveSpeedMultiplier);
	builder.method("attackSpeedMultiplier", &ServerCreature::attackSpeedMultiplier);
	builder.method("moveSpeed", static_cast<double(ServerCreature::*)() const>(&ServerCreature::moveSpeed));
	builder.method("moveSpeed", static_cast<void(ServerCreature::*)(double)>(&ServerCreature::moveSpeed));
	builder.method("stun", &ServerCreature::stun);
	builder.method("root", &ServerCreature::root);
	builder.method("visible", static_cast<bool(ServerCreature::*)() const>(&ServerCreature::visible));
	builder.method("visible", static_cast<void(ServerCreature::*)(bool)>(&ServerCreature::visible));
	builder.method("taunt", [](ServerCreature& a_self, int64_t a_attacker, double a_seconds) { a_self.taunt(a_attacker, a_seconds); });
	builder.method("tauntedBy", &ServerCreature::tauntedBy);
	builder.method("untargetable", static_cast<bool(ServerCreature::*)() const>(&ServerCreature::untargetable));
	builder.method("untargetable", static_cast<void(ServerCreature::*)(bool)>(&ServerCreature::untargetable));
	builder.method("absorbFor", [](ServerCreature& a_self, int64_t a_host, double a_fraction, double a_statusScale) { a_self.absorbFor(a_host, a_fraction, a_statusScale); });
	builder.method("spawnCreature", &ServerCreature::spawnCreature);
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
	return "Creatures/" + MV::toUpperFirstChar(statTemplate.id) + "/" + (skin.empty() ? "Default" : skin) + "/unit.bindsnap";
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
	baseMoveSpeed = statTemplate.moveSpeed;
	appliedMoveSpeed = statTemplate.moveSpeed;

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
	std::weak_ptr<ServerCreature> weakSelf = self;
	spineAnimator->onEnd.connect("!", [weakSelf](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
		auto creature = weakSelf.lock();
		if (creature && a_self->track(a_track).name() == creature->state->self()->animationName) {
			creature->owner()->removeFromParent();
		}
	});
}

std::shared_ptr<ServerCreature> ServerCreature::serverCreature(int64_t a_id) const {
	auto found = std::dynamic_pointer_cast<ServerCreature>(gameInstance.creature(a_id));
	return (found && found->alive()) ? found : nullptr;
}

double ServerCreature::scaledDuration(double a_seconds) const {
	return (absorberId != 0 && serverCreature(absorberId)) ? a_seconds * absorbStatusScale : a_seconds;
}

int ServerCreature::damage(int a_amount, int64_t a_sourceId) {
	auto source = serverCreature(a_sourceId);
	return damageScaled(a_amount, source ? source->elements() : std::vector<std::string>{}, a_sourceId);
}

// every attacker type x every defender type from the table, then every synergy with an element
// already working on this unit (an active dot, the ward on it); a hit that scales below one point
// still lands one
double ServerCreature::elementMultiplier(const std::vector<std::string>& a_attackerElements) const {
	if (a_attackerElements.empty()) { return 1.0; }
	auto& table = gameInstance.data().elements();
	double multiplier = table.damageMultiplier(a_attackerElements, elements());
	std::vector<std::string> affecting;
	auto affect = [&](const std::vector<std::string>& a_elements) {
		for (auto& affectingElement : a_elements) {
			if (std::find(affecting.begin(), affecting.end(), affectingElement) == affecting.end()) { affecting.push_back(affectingElement); }
		}
	};
	for (auto& effect : timedEffects) {
		if (effect.perSecond < 0.0) { affect(effect.elements); }
	}
	if (auto absorber = (absorberId != 0) ? serverCreature(absorberId) : nullptr) { affect(absorber->elements()); }
	multiplier *= table.synergyMultiplier(a_attackerElements, affecting);
	static const bool diag = std::getenv("MV_DAMAGE_DIAG") != nullptr;
	if (diag) {
		static std::set<std::string> seen;
		auto join = [](const std::vector<std::string>& a_list) { std::string result; for (auto& item : a_list) { result += (result.empty() ? "" : "+") + item; } return result; };
		auto line = join(a_attackerElements) + " -> " + join(elements()) + (affecting.empty() ? "" : " [" + join(affecting) + "]");
		if (seen.insert(line).second) { std::cout << "[damage] " << line << " x" << multiplier << std::endl; }
	}
	return multiplier;
}

int ServerCreature::damageScaled(int a_amount, const std::vector<std::string>& a_attackerElements, int64_t a_sourceId) {
	if (!alive() || a_amount <= 0) { return 0; }
	int remaining = std::max(1, static_cast<int>(std::lround(a_amount * elementMultiplier(a_attackerElements))));
	int fromShield = std::min(shieldPoints, remaining);
	shieldPoints -= fromShield;
	remaining -= fromShield;
	if (remaining > 0 && absorberId != 0) {
		if (auto absorber = serverCreature(absorberId)) {
			int redirected = static_cast<int>(remaining * absorbFraction);
			if (redirected > 0) {
				absorber->changeHealth(-redirected);
				remaining -= redirected;
			}
		} else {
			absorberId = 0;
		}
	}
	if (remaining > 0) {
		changeHealth(-remaining);
	}
	return remaining;
}

void ServerCreature::stun(double a_seconds) {
	targeting.stun(static_cast<float>(scaledDuration(a_seconds)));
}

void ServerCreature::root(double a_seconds) {
	targeting.root(static_cast<float>(scaledDuration(a_seconds)));
}

void ServerCreature::dot(double a_perSecond, double a_seconds, int64_t a_sourceId) {
	auto source = serverCreature(a_sourceId);
	timedEffects.push_back({ -a_perSecond, scaledDuration(a_seconds), a_sourceId, 0.0, source ? source->elements() : std::vector<std::string>{} });
}

// a ward's share scales with the table (water on lightning takes more, on fire less) and the
// status relief scales the same way
void ServerCreature::visible(bool a_value) {
	if (*state->self()->hidden == !a_value) { return; }
	state->modify()->hidden = !a_value;
}

void ServerCreature::absorbFor(int64_t a_hostId, double a_fraction, double a_statusScale) {
	if (auto host = serverCreature(a_hostId)) {
		double scale = gameInstance.data().elements().wardScale(elements(), host->elements());
		host->absorberId = netId();
		host->absorbFraction = std::clamp(a_fraction * scale, 0.0, 1.0);
		host->absorbStatusScale = std::clamp(scale > 0.0 ? a_statusScale / scale : 1.0, 0.05, 1.0);
	}
}

void ServerCreature::spawnCreature(const std::string& a_creatureId) {
	gameInstance.spawnCreatureAt(buildingSlot(), a_creatureId, owner()->worldPosition());
}

void ServerCreature::updateStatus(double a_dt) {
	for (auto effect = timedEffects.begin(); effect != timedEffects.end();) {
		double step = std::min(a_dt, effect->remaining);
		effect->carry += effect->perSecond * step;
		int whole = static_cast<int>(effect->carry);
		if (whole != 0) {
			effect->carry -= whole;
			if (whole < 0) { damageScaled(-whole, effect->elements, effect->source); } else { heal(whole); }
		}
		effect->remaining -= a_dt;
		effect = effect->remaining <= 0.0 ? timedEffects.erase(effect) : effect + 1;
	}
	for (auto* effect : { &slowEffect, &hasteEffect, &attackHasteEffect }) {
		if (effect->remaining > 0.0 && (effect->remaining -= a_dt) <= 0.0) { effect->fraction = 0.0; effect->remaining = 0.0; }
	}
	if (tauntRemaining > 0.0 && ((tauntRemaining -= a_dt) <= 0.0 || !serverCreature(tauntId))) { tauntRemaining = 0.0; tauntId = 0; }
	if (absorberId != 0 && !serverCreature(absorberId)) { absorberId = 0; }
	double speed = baseMoveSpeed * moveSpeedMultiplier();
	if (pathAgent && speed != appliedMoveSpeed) {
		pathAgent->gridSpeed(static_cast<MV::PointPrecision>(speed));
		appliedMoveSpeed = speed;
	}
}

void ServerCreature::updateImplementation(double a_delta) {
	if (alive()) {
		auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
		updateStatus(a_delta);
		if (!alive()) { return; }
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
	pathMode = MV::PathMode::search;
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
			chaseCell = MV::round<int>(targetCreature->agent()->gridPosition());
			chasing = true;
			selfCreature->agent()->gridGoal(chaseCell, range, pathMode);
			return;
		}
	}
	if (a_fail) {
		a_fail(*this);
	}
}

void TargetPolicy::target(const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail /*= std::function<void(TargetPolicy &)>()*/) {
	target(a_location, a_range, MV::PathMode::search, a_succeed, a_fail);
}

// The mode belongs to the current target: a location keeps the one it was given, a creature is
// always chased by search (a field per chase cell would thrash the cache)
void TargetPolicy::target(const MV::Point<> &a_location, float a_range, MV::PathMode a_mode, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail /*= std::function<void(TargetPolicy &)>()*/) {
	clearTarget();
	pathMode = a_mode;
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
			selfCreature->agent()->gridGoal(a_location, range, pathMode);
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
			// re-path only once the target has left the acceptance disc of the goal we already hold:
			// a melee chaser tolerates ~a cell of drift, a ranged one its whole range
			auto cell = MV::round<int>(targetCreature->agent()->gridPosition());
			auto drift = MV::distance(cell, chaseCell);
			if (!chasing || drift > std::max(1.0f, range * 0.75f)) {
				chasing = true;
				chaseCell = cell;
				selfCreature->agent()->gridGoal(cell, range, pathMode);
			}
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
	chasing = false;
	chasing = false;
	chasing = false;
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
}

void ClientCreature::initialize() {
	auto self = std::static_pointer_cast<ClientCreature>(shared_from_this());
	std::weak_ptr<ClientCreature> weakSelf = self;
	state->self()->onNetworkDeath = [weakSelf] {
		if (auto creature = weakSelf.lock()) {
			creature->animateDeathAndRemove();
		}
	};
	state->self()->onNetworkSynchronize = [weakSelf] {
		if (auto creature = weakSelf.lock()) {
			creature->onNetworkSynchronize();
		}
	};
	state->self()->onAnimationChanged = [weakSelf] {
		if (auto creature = weakSelf.lock()) {
			creature->onAnimationChanged();
		}
	};

	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);
	spineAnimator = newNode->componentInChildren<MV::Scene::Spine>().get();
	buildHealthBar();

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
		std::weak_ptr<ClientCreature> weakSelf = std::static_pointer_cast<ClientCreature>(shared_from_this());
		spineAnimator->onEnd.connect("!", [weakSelf](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
			auto creature = weakSelf.lock();
			if (creature && a_self->track(a_track).name() == creature->state->self()->animationName) {
				creature->owner()->removeFromParent();
			}
		});
	});
}

// League-style bar: dark frame, team-coloured fill, a divider every 10 hp
void ClientCreature::buildHealthBar() {
	static constexpr float WIDTH = 56.0f, HEIGHT = 7.0f, ABOVE_HEAD = -96.0f;
	bool friendly = player() && player()->id == gameInstance.localPlayerId();
	if (std::getenv("MV_HPBAR_DIAG")) { std::cout << "[hpbar] " << statTemplate.id << " player=" << (player() ? player()->id : -2) << " local=" << gameInstance.localPlayerId() << " friendly=" << friendly << std::endl; }
	healthBar = owner()->make("HealthBar");
	healthBar->serializable(false);
	healthBar->position(MV::point(-WIDTH / 2.0f, ABOVE_HEAD));
	healthBar->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(-1.5f, -1.5f), MV::size(WIDTH + 3.0f, HEIGHT + 3.0f)))->color(MV::Color(0.05f, 0.07f, 0.09f, 0.95f));
	healthFill = healthBar->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(WIDTH, HEIGHT)))->color(friendly ? MV::Color(0.18f, 0.80f, 0.44f, 1.0f) : MV::Color(0.88f, 0.27f, 0.23f, 1.0f));
	for (int hp = 10; hp < statTemplate.health; hp += 10) {
		float x = WIDTH * static_cast<float>(hp) / static_cast<float>(statTemplate.health);
		healthBar->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(x, 0.0f), MV::size(1.0f, HEIGHT)))->color(MV::Color(0.05f, 0.07f, 0.09f, 0.75f));
	}
	refreshHealthBar();
}

void ClientCreature::refreshHealthBar() {
	if (!healthFill || !healthBar) { return; }
	int health = *state->self()->health;
	if (health <= 0 || *state->self()->hidden) { healthBar->hide(); return; }
	healthBar->show();
	// sit just above the animated skeleton, whatever its height; fall back to a fixed lift until it has bounds
	auto skeleton = spineAnimator ? spineAnimator->bounds() : MV::BoxAABB<>();
	float top = skeleton.size().height > 1.0f ? skeleton.minPoint.y - 14.0f : -60.0f;
	healthBar->position(MV::point(-28.0f, top));
	float fraction = statTemplate.health > 0 ? std::clamp(static_cast<float>(health) / static_cast<float>(statTemplate.health), 0.0f, 1.0f) : 0.0f;
	healthFill->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(std::max(0.5f, 56.0f * fraction), 7.0f)));
}

void ClientCreature::onNetworkSynchronize() {
	networkDelta.add(MV::Stopwatch::programTime());
	if (*state->self()->hidden) { owner()->hide(); } else { owner()->show(); }
	refreshHealthBar();
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
