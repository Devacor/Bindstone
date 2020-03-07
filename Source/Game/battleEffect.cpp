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

void BattleEffectNetworkState::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<BattleEffectNetworkState>(), "BattleEffectNetworkState");
	a_script.add(chaiscript::constructor<BattleEffectNetworkState(GameInstance&, const std::string &, int64_t, const std::string &)>(), "BattleEffectNetworkState");
	a_script.add(chaiscript::constructor<BattleEffectNetworkState(GameInstance&, const std::string &, int64_t)>(), "BattleEffectNetworkState");

	a_script.add(chaiscript::fun(&BattleEffectNetworkState::dying), "dying");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::position), "position");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::buildingSlot), "buildingSlot");

	a_script.add(chaiscript::fun(&BattleEffectNetworkState::creatureOwnerId), "creatureOwnerId");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetCreatureId), "targetCreatureId");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetPosition), "targetPosition");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetType), "targetType");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::duration), "duration");

	a_script.add(chaiscript::fun([](BattleEffectNetworkState &a_self, const std::string &a_key) {
		return a_self.variables[a_key];
	}), "[]");
}

std::string BattleEffect::assetPath() const {
	return "BattleEffects/" + statTemplate.id + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
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

chaiscript::ChaiScript& BattleEffect::hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/) {
	a_script.add(chaiscript::user_type<TargetType>(), "TargetType");
	a_script.add_global_const(chaiscript::const_var(TargetType::NONE), "TargetType_NONE");
	a_script.add_global_const(chaiscript::const_var(TargetType::CREATURE), "TargetType_CREATURE");
	a_script.add_global_const(chaiscript::const_var(TargetType::GROUND), "TargetType_GROUND");
	a_script.add(chaiscript::fun([](TargetType &a_lhs, const TargetType &a_rhs) { return a_lhs = a_rhs; }), "=");
	a_script.add(chaiscript::fun([](const TargetType &a_lhs, const TargetType &a_rhs) { return a_lhs == a_rhs; }), "==");
	a_script.add(chaiscript::fun([](const TargetType &a_lhs, const TargetType &a_rhs) { return a_lhs != a_rhs; }), "!=");

	BattleEffectNetworkState::hook(a_script);
	BattleEffectData::hook(a_script);

	a_script.add(chaiscript::user_type<BattleEffect>(), "BattleEffect");
	a_script.add(chaiscript::base_class<Component, BattleEffect>());

	MV::SignalRegister<CallbackSignature>::hook(a_script);
	a_script.add(chaiscript::fun(&BattleEffect::onArrive), "onArrive");
	a_script.add(chaiscript::fun(&BattleEffect::onFizzle), "onFizzle");

	a_script.add(chaiscript::fun(&BattleEffect::sourceCreature), "sourceCreature");
	a_script.add(chaiscript::fun(&BattleEffect::targetCreature), "targetCreature");

	a_script.add(chaiscript::fun(&BattleEffect::ourElapsedTime), "ourElapsedTime");

	a_script.add(chaiscript::fun([](BattleEffect &a_self) -> GameInstance& {return a_self.gameInstance; }), "game");

	a_script.add(chaiscript::fun(&BattleEffect::alive), "alive");
	
	a_script.add(chaiscript::fun([](BattleEffect &a_self, const std::string &a_key) {
		return a_self.localVariables[a_key];
	}), "[]");

	a_script.add(chaiscript::fun([&](BattleEffect &a_self) -> decltype(auto) {
		return a_self.state->self();
	}), "viewNetState");

	a_script.add(chaiscript::fun([&](BattleEffect &a_self) -> decltype(auto) {
		return a_self.state->modify();
	}), "modifyNetState");

	a_script.add(chaiscript::fun([&](BattleEffect &a_self) -> decltype(auto) {
		return a_self.state->modify()->variables;
	}), "setNetValue");

	a_script.add(chaiscript::fun([&](BattleEffect &a_self) -> decltype(auto) {
		return a_self.state->self()->variables;
	}), "getNetValue");

	a_script.add(chaiscript::fun([&](BattleEffect &a_self) {
		return a_self.state->id();
	}), "id");

	a_script.add(chaiscript::fun([](BattleEffect &a_self) {
		return a_self.statTemplate;
	}), "stats");
	a_script.add(chaiscript::fun(&BattleEffect::skin), "skin");

	a_script.add(chaiscript::fun(&BattleEffect::assetPath), "assetPath");
	a_script.add(chaiscript::fun([](BattleEffect &a_self) {
		return &a_self.gameInstance;
	}), "gameInstance");

	a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect> &a_self) {
		a_self.reset();
	}), "reset");

	a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect> &a_lhs, std::shared_ptr<BattleEffect> &a_rhs) {
		return a_lhs.get() == a_rhs.get();
	}), "==");
	a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect> &a_lhs, std::shared_ptr<BattleEffect> &a_rhs) {
		return a_lhs.get() != a_rhs.get();
	}), "!=");
	a_script.add(chaiscript::fun([]() {
		return std::shared_ptr<BattleEffect>();
	}), "nullEffect");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<BattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<BattleEffect> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<BattleEffect>>>("VectorBattleEffect"));

	return a_script;
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

chaiscript::ChaiScript& ServerBattleEffect::hook(chaiscript::ChaiScript &a_script, GameInstance& a_gameInstance) {
	BattleEffect::hook(a_script, a_gameInstance);
	a_script.add(chaiscript::user_type<ServerBattleEffect>(), "ServerBattleEffect");
	a_script.add(chaiscript::base_class<BattleEffect, ServerBattleEffect>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<ServerBattleEffect>>([](const MV::Scene::SafeComponent<ServerBattleEffect> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<BattleEffect>>([](const MV::Scene::SafeComponent<ServerBattleEffect> &a_item) { return std::static_pointer_cast<BattleEffect>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ServerBattleEffect> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	return a_script;
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

chaiscript::ChaiScript& ClientBattleEffect::hook(chaiscript::ChaiScript &a_script, GameInstance& a_gameInstance) {
	BattleEffect::hook(a_script, a_gameInstance);
	a_script.add(chaiscript::user_type<ClientCreature>(), "ClientBattleEffect");
	a_script.add(chaiscript::base_class<BattleEffect, ClientBattleEffect>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<ClientBattleEffect>>([](const MV::Scene::SafeComponent<ClientBattleEffect> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<BattleEffect>>([](const MV::Scene::SafeComponent<ClientBattleEffect> &a_item) { return std::static_pointer_cast<BattleEffect>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ClientBattleEffect> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	return a_script;
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
