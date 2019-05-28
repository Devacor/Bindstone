#include "battleEffect.h"
#include "creature.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"

std::string BattleEffect::assetPath() const {
	return "Assets/BattleEffects/" + statTemplate.id + "/" + (skin.empty() ? "Default" : skin) + "/unit.prefab";
}

BattleEffect::BattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const BattleEffectData& a_statTemplate, std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_state) :
	Component(a_owner),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onFizzle(onFizzleSignal),
	skin(a_skin),
	statTemplate(a_statTemplate),
	state(a_state) {
}

chaiscript::ChaiScript& BattleEffect::hook(chaiscript::ChaiScript &a_script, GameInstance& /*gameInstance*/) {
	chaiscript::utility::add_class<TargetType>(a_script, "TargetType", {
		{ TargetType::NONE, "TargetType_NONE" },
		{ TargetType::CREATURE, "TargetType_CREATURE" },
		{ TargetType::GROUND, "TargetType_GROUND" }
	});

	BattleEffectData::hook(a_script);
	a_script.add(chaiscript::user_type<BattleEffect>(), "BattleEffect");
	a_script.add(chaiscript::base_class<Component, BattleEffect>());

	MV::SignalRegister<CallbackSignature>::hook(a_script);
	a_script.add(chaiscript::fun(&BattleEffect::onArrive), "onArrive");
	a_script.add(chaiscript::fun(&BattleEffect::onFizzle), "onFizzle");

	a_script.add(chaiscript::fun([](BattleEffect &a_self) -> GameInstance& {return a_self.gameInstance; }), "game");

	a_script.add(chaiscript::fun(&BattleEffect::alive), "alive");
	
	a_script.add(chaiscript::fun([](BattleEffect &a_self, const std::string &a_key) {
		return a_self.localVariables[a_key];
	}), "[]");

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

ServerBattleEffect::ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int a_buildingSlot, GameInstance& a_gameInstance) :
	ServerBattleEffect(a_owner, a_gameInstance.data().battleEffects().data(a_id), a_buildingSlot, a_gameInstance) {
}

ServerBattleEffect::ServerBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const BattleEffectData &a_statTemplate, int a_buildingSlot, GameInstance& a_gameInstance) :
	BattleEffect(a_owner, a_gameInstance, a_gameInstance.building(a_buildingSlot)->skin(), a_statTemplate, a_gameInstance.networkPool().spawn(std::make_shared<BattleEffectNetworkState>(a_statTemplate, a_buildingSlot))) {
}

void ServerBattleEffect::initialize() {
	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);

	auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);
}

void ServerBattleEffect::updateImplementation(double a_delta) {
	if (alive()) {
		auto self = std::static_pointer_cast<ServerBattleEffect>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		if (state->self()->targetType == TargetType::NONE && state->self()->position != owner()->position()) {
			state->modify()->position = owner()->position();
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

	TargetPolicy::hook(a_script);

	return a_script;
}


ClientBattleEffect::ClientBattleEffect(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> &a_state, GameInstance& a_gameInstance) :
	BattleEffect (a_owner, a_gameInstance,
		a_gameInstance.building(a_state->self()->buildingSlot)->skin(),
		a_gameInstance.data().battleEffects().data(a_state->self()->effectTypeId),
		a_state
	) {
	state->self()->onNetworkDeath = [&]() {
		animateDeathAndRemove();
	};
	state->self()->onNetworkSynchronize = [&] {
		onNetworkSynchronize();
	};
}

void ClientBattleEffect::initialize() {
	offsetDepth = owner()->componentInParents<MV::Scene::PathMap>()->cellSize().height / 2.0f;

	auto newNode = owner()->make(assetPath(), gameInstance.services());
	newNode->serializable(false);
	newNode->componentInChildren<MV::Scene::Spine>()->animate("run");

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
	networkDelta.add(MV::Stopwatch::systemTime());
	startClientPosition = owner()->position();
	accumulatedDuration = 0;
}

void ClientBattleEffect::updateImplementation(double a_delta) {
	if (alive()) {
		if (state->self()->targetType == TargetType::NONE) {
			accumulatedDuration = std::min(networkDelta.delta(), accumulatedDuration + a_delta);
			double percentNetTimestep = accumulatedDuration / networkDelta.delta();
			MV::info("NetStep: ", percentNetTimestep, "% ==> [", accumulatedDuration, " / ", networkDelta.delta(), "]");
			owner()->position(mix(startClientPosition, state->self()->position, static_cast<MV::PointPrecision>(percentNetTimestep)));
		}

		auto self = std::static_pointer_cast<ClientBattleEffect>(shared_from_this());
		statTemplate.script(gameInstance.script()).update(self, a_delta);

		owner()->depth(owner()->position().y - offsetDepth);
	}
}
