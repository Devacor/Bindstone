#include "Game/NetworkLayer/gameNetworkHooks.cxx"
#include "Game/Interface/interfaceHooks.cxx"


#include "Game/game.h"

MV::Script::Registrar<BattleEffectNetworkState> _hookBattleEffectNetworkState([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<BattleEffectNetworkState>(), "BattleEffectNetworkState");
	a_script.add(chaiscript::constructor<BattleEffectNetworkState(GameInstance&, const std::string&, int64_t, const std::string&)>(), "BattleEffectNetworkState");
	a_script.add(chaiscript::constructor<BattleEffectNetworkState(GameInstance&, const std::string&, int64_t)>(), "BattleEffectNetworkState");

	a_script.add(chaiscript::fun(&BattleEffectNetworkState::dying), "dying");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::position), "position");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::buildingSlot), "buildingSlot");

	a_script.add(chaiscript::fun(&BattleEffectNetworkState::creatureOwnerId), "creatureOwnerId");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetCreatureId), "targetCreatureId");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetPosition), "targetPosition");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::targetType), "targetType");
	a_script.add(chaiscript::fun(&BattleEffectNetworkState::duration), "duration");

	a_script.add(chaiscript::fun([](BattleEffectNetworkState& a_self, const std::string& a_key) {
		return a_self.variables[a_key];
	}), "[]");
});

template<>
void MV::Script::Registrar<BattleEffect>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	if(GameInstance* gameInstance = a_services.get<GameInstance>(false))
	{
		a_script.add(chaiscript::user_type<TargetType>(), "TargetType");
		a_script.add_global_const(chaiscript::const_var(TargetType::NONE), "TargetType_NONE");
		a_script.add_global_const(chaiscript::const_var(TargetType::CREATURE), "TargetType_CREATURE");
		a_script.add_global_const(chaiscript::const_var(TargetType::GROUND), "TargetType_GROUND");
		a_script.add(chaiscript::fun([](TargetType& a_lhs, const TargetType& a_rhs) { return a_lhs = a_rhs; }), "=");
		a_script.add(chaiscript::fun([](const TargetType& a_lhs, const TargetType& a_rhs) { return a_lhs == a_rhs; }), "==");
		a_script.add(chaiscript::fun([](const TargetType& a_lhs, const TargetType& a_rhs) { return a_lhs != a_rhs; }), "!=");

		a_script.add(chaiscript::user_type<BattleEffect>(), "BattleEffect");
		a_script.add(chaiscript::base_class<MV::Scene::Component, BattleEffect>());

		a_script.add(chaiscript::fun(&BattleEffect::onArrive), "onArrive");
		a_script.add(chaiscript::fun(&BattleEffect::onFizzle), "onFizzle");

		a_script.add(chaiscript::fun([](BattleEffect& a_self) -> GameInstance& {return a_self.game(); }), "game");

		a_script.add(chaiscript::fun(&BattleEffect::alive), "alive");

		a_script.add(chaiscript::fun([&](BattleEffect& a_self) -> decltype(auto) {
			return a_self.state->self();
		}), "viewNetState");

		a_script.add(chaiscript::fun([&](BattleEffect& a_self) -> decltype(auto) {
			return a_self.state->modify();
		}), "modifyNetState");

		a_script.add(chaiscript::fun([&](BattleEffect& a_self) -> decltype(auto) {
			return a_self.state->modify()->variables;
		}), "setNetValue");

		a_script.add(chaiscript::fun([&](BattleEffect& a_self) -> decltype(auto) {
			return a_self.state->self()->variables;
		}), "getNetValue");

		a_script.add(chaiscript::fun([&](BattleEffect& a_self) {
			return a_self.state->id();
		}), "id");

		a_script.add(chaiscript::fun([](BattleEffect& a_self) {
			return a_self.statTemplate;
		}), "stats");

		a_script.add(chaiscript::fun(&BattleEffect::assetPath), "assetPath");

		a_script.add(chaiscript::fun([](BattleEffect& a_self, const std::string& a_key) {
			return a_self.localVariables[a_key];
		}), "[]"); 
		a_script.add(chaiscript::fun(&BattleEffect::ourElapsedTime), "ourElapsedTime");

		a_script.add(chaiscript::fun(&BattleEffect::skin), "skin");
		a_script.add(chaiscript::fun(&BattleEffect::sourceCreature), "sourceCreature");
		a_script.add(chaiscript::fun(&BattleEffect::targetCreature), "targetCreature");

		a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect>& a_self) {
			a_self.reset();
		}), "reset");

		a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect>& a_lhs, std::shared_ptr<BattleEffect>& a_rhs) {
			return a_lhs.get() == a_rhs.get();
		}), "==");
		a_script.add(chaiscript::fun([](std::shared_ptr<BattleEffect>& a_lhs, std::shared_ptr<BattleEffect>& a_rhs) {
			return a_lhs.get() != a_rhs.get();
		}), "!=");
		a_script.add(chaiscript::fun([]() {
			return std::shared_ptr<BattleEffect>();
		}), "nullEffect");

		a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<BattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<BattleEffect>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

		a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<BattleEffect>>>("VectorBattleEffect"));
	}
}

MV::Script::Registrar<BattleEffect> _hookBattleEffect {};

MV::ScriptSignalRegistrar<BattleEffect::CallbackSignature> _battleEffectCallback {};

MV::Script::Registrar<ClientCreature> _hookServerBattleEffect([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<ServerBattleEffect>(), "ServerBattleEffect");
	a_script.add(chaiscript::base_class<BattleEffect, ServerBattleEffect>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<ServerBattleEffect>>([](const MV::Scene::SafeComponent<ServerBattleEffect>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<BattleEffect>>([](const MV::Scene::SafeComponent<ServerBattleEffect>& a_item) { return std::static_pointer_cast<BattleEffect>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerBattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ServerBattleEffect>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));
});

MV::Script::Registrar<ClientBattleEffect> _hookClientBattleEffect([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<ClientBattleEffect>(), "ClientBattleEffect");
	a_script.add(chaiscript::base_class<BattleEffect, ClientBattleEffect>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<ClientBattleEffect>>([](const MV::Scene::SafeComponent<ClientBattleEffect>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<BattleEffect>>([](const MV::Scene::SafeComponent<ClientBattleEffect>& a_item) { return std::static_pointer_cast<BattleEffect>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientBattleEffect>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ClientBattleEffect>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));
});

template <>
void MV::Script::Registrar<StandardScriptMethods<BattleEffect>>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<StandardScriptMethods<BattleEffect>>(), "BattleEffectScriptMethods");

	a_script.add(chaiscript::fun(&StandardScriptMethods<BattleEffect>::scriptSpawn), "spawn");
	a_script.add(chaiscript::fun(&StandardScriptMethods<BattleEffect>::scriptUpdate), "update");
	a_script.add(chaiscript::fun(&StandardScriptMethods<BattleEffect>::scriptDeath), "death");
}

MV::Script::Registrar<StandardScriptMethods<BattleEffect>> _hookBattleEffectScriptMethods {};

MV::Script::Registrar<BattleEffect> _hookBattleEffectData([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::fun(&BattleEffectData::id), "id");
});

MV::Script::Registrar<BuildTree> _hookBuildTree([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<BuildTree>(), "BuildTree");

	a_script.add(chaiscript::fun(&BuildTree::id), "id");
	a_script.add(chaiscript::fun(&BuildTree::cost), "cost");
	a_script.add(chaiscript::fun(&BuildTree::income), "income");
	a_script.add(chaiscript::fun(&BuildTree::upgrades), "upgrades");
});

template <>
void MV::Script::Registrar<StandardScriptMethods<Building>>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<StandardScriptMethods<Building>>(), "BuildingScriptMethods");

	a_script.add(chaiscript::fun(&StandardScriptMethods<Building>::scriptSpawn), "spawn");
	a_script.add(chaiscript::fun(&StandardScriptMethods<Building>::scriptUpdate), "update");
	a_script.add(chaiscript::fun(&StandardScriptMethods<Building>::scriptDeath), "death");
}

MV::Script::Registrar<StandardScriptMethods<Building>> _hookBuildingScriptMethods{};

template <>
void MV::Script::Registrar<Building>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<Building>(), "Building");
	a_script.add(chaiscript::base_class<MV::Scene::Component, Building>());

	a_script.add(chaiscript::fun(&Building::current), "current");
	a_script.add(chaiscript::fun(&Building::upgrade), "upgrade");

	a_script.add(chaiscript::fun(&Building::buildingData), "data");
	a_script.add(chaiscript::fun(&Building::skin), "skin");
	a_script.add(chaiscript::fun(&Building::slot), "slot");
	a_script.add(chaiscript::fun(&Building::owningPlayer), "player");

	a_script.add(chaiscript::fun(&Building::assetPath), "assetPath");

	a_script.add(chaiscript::fun([](Building& a_self, const std::string& a_key) {
		return a_self.localVariables[a_key];
	}), "[]");

	a_script.add(chaiscript::fun([&](Building& a_self) -> decltype(auto) {
		return a_self.state->modify()->variables;
	}), "setNetValue");

	a_script.add(chaiscript::fun([&](Building& a_self) -> decltype(auto) {
		return a_self.state->self()->variables;
	}), "getNetValue");

	a_script.add(chaiscript::fun([&](Building& a_self, const std::string& a_key) {
		a_self.gameInstance.spawnCreature(a_self.slotIndex(), a_key);
	}), "spawn");

	a_script.add(chaiscript::fun([&](Building& a_self) {
		return a_self.state->id();
	}), "networkId");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<Building>>([](const MV::Scene::SafeComponent<Building>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Building>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));
}

MV::Script::Registrar<Building> _hookBuilding {};

MV::Script::Registrar<CreatureNetworkState> _hookCreatureNetworkState([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<CreatureNetworkState>(), "CreatureNetworkState");

	a_script.add(chaiscript::fun(&CreatureNetworkState::dying), "dying");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "health");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "position");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "animationName");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "animationTime");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "animationLoops");
	a_script.add(chaiscript::fun(&CreatureNetworkState::health), "buildingSlot");

	a_script.add(chaiscript::fun([](CreatureNetworkState& a_self, const std::string& a_key) -> decltype(auto) {
		return a_self.variables.modify()[a_key];
	}), "[]");
});

template <>
void MV::Script::Registrar<StandardScriptMethods<Creature>>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<StandardScriptMethods<Creature>>(), "CreatureScriptMethods");

	a_script.add(chaiscript::fun(&StandardScriptMethods<Creature>::scriptSpawn), "spawn");
	a_script.add(chaiscript::fun(&StandardScriptMethods<Creature>::scriptUpdate), "update");
	a_script.add(chaiscript::fun(&StandardScriptMethods<Creature>::scriptDeath), "death");
}

MV::Script::Registrar<StandardScriptMethods<Creature>> _hookCreatureScriptMethods {};

MV::Script::Registrar<CreatureData> _hookCreatureData([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<CreatureData>(), "CreatureData");

	a_script.add(chaiscript::fun(&CreatureData::id), "id");
	a_script.add(chaiscript::fun(&CreatureData::name), "name");

	a_script.add(chaiscript::fun(&CreatureData::description), "description");

	a_script.add(chaiscript::fun(&CreatureData::moveSpeed), "moveSpeed");
	a_script.add(chaiscript::fun(&CreatureData::actionSpeed), "actionSpeed");
	a_script.add(chaiscript::fun(&CreatureData::castSpeed), "castSpeed");

	a_script.add(chaiscript::fun(&CreatureData::health), "health");
	a_script.add(chaiscript::fun(&CreatureData::defense), "defense");
	a_script.add(chaiscript::fun(&CreatureData::will), "will");
	a_script.add(chaiscript::fun(&CreatureData::strength), "strength");
	a_script.add(chaiscript::fun(&CreatureData::ability), "ability");

	a_script.add(chaiscript::fun(&CreatureData::isServer), "isServer");
});

MV::Script::Registrar<TargetPolicy> _hookTargetPolicy([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<TargetPolicy>(), "TargetPolicy");

	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, std::function<void(TargetPolicy&)> a_succeed) {
		a_self.target(a_target, 0.0f, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, float a_range, std::function<void(TargetPolicy&)> a_succeed) {
		a_self.target(a_target, a_range, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, int64_t a_target, float a_range, std::function<void(TargetPolicy&)> a_succeed, std::function<void(TargetPolicy&)> a_fail) {
		a_self.target(a_target, a_range, a_succeed, a_fail);
	}), "target");

	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<>& a_location, std::function<void(TargetPolicy&)> a_succeed) {
		a_self.target(a_location, 0.0f, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<>& a_location, float a_range, std::function<void(TargetPolicy&)> a_succeed) {
		a_self.target(a_location, a_range, a_succeed);
	}), "target");
	a_script.add(chaiscript::fun([](TargetPolicy& a_self, const MV::Point<>& a_location, float a_range, std::function<void(TargetPolicy&)> a_succeed, std::function<void(TargetPolicy&)> a_fail) {
		a_self.target(a_location, a_range, a_succeed, a_fail);
	}), "target");
	a_script.add(chaiscript::fun(&TargetPolicy::active), "active");

	a_script.add(chaiscript::fun(&TargetPolicy::stun), "stunned");
	a_script.add(chaiscript::fun(&TargetPolicy::root), "rooted");

	a_script.add(chaiscript::fun(&TargetPolicy::self), "self");
});

MV::ScriptSignalRegistrar<Creature::CallbackSignature> _creatureCallbackSignature{};
MV::ScriptSignalRegistrar<void(std::shared_ptr<Creature>, int)> _creatureCallbackSignatureInt{};

template<>
void MV::Script::Registrar<Creature>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<Creature>(), "Creature");
	a_script.add(chaiscript::base_class<MV::Scene::Component, Creature>());

	a_script.add(chaiscript::fun(&Creature::onStatus), "onStatus");
	a_script.add(chaiscript::fun(&Creature::onHealthChange), "onHealthChange");
	a_script.add(chaiscript::fun(&Creature::onDeath), "onDeath");
	a_script.add(chaiscript::fun(&Creature::onFall), "onFall");
	a_script.add(chaiscript::fun(&Creature::spineAnimator), "spine");

	a_script.add(chaiscript::fun([](Creature& a_self) -> GameInstance& {return a_self.gameInstance; }), "game");

	a_script.add(chaiscript::fun(&Creature::alive), "alive");

	a_script.add(chaiscript::fun([](Creature& a_self, const std::string& a_key) -> decltype(auto) {
		return a_self.localVariables[a_key];
	}), "[]");

	a_script.add(chaiscript::fun([&](Creature& a_self) -> decltype(auto) {
		return a_self.state->modify()->variables.modify();
	}), "setNetValue");

	a_script.add(chaiscript::fun([&](Creature& a_self) -> decltype(auto) {
		return a_self.state->self()->variables.view();
	}), "getNetValue");

	a_script.add(chaiscript::fun([&](Creature& a_self) {
		return a_self.state->id();
	}), "networkId");

	a_script.add(chaiscript::fun([](Creature& a_self, int a_amount) {
		return a_self.changeHealth(a_amount);
	}), "changeHealth");

	a_script.add(chaiscript::fun([](Creature& a_self) {
		return a_self.statTemplate;
	}), "stats");
	a_script.add(chaiscript::fun(&Creature::skin), "skin");
	a_script.add(chaiscript::fun(&Creature::player), "player");

	a_script.add(chaiscript::fun(&Creature::assetPath), "assetPath");
	a_script.add(chaiscript::fun([](Creature& a_self) {
		return &a_self.gameInstance;
	}), "gameInstance");

	a_script.add(chaiscript::fun([](Creature& a_self) {
		return a_self.gameInstance.teamForPlayer(a_self.player());
	}), "team");

	a_script.add(chaiscript::fun([](Creature& a_self) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player());
	}), "enemyTeam");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature>& a_self) {
		a_self.reset();
	}), "reset");

	a_script.add(chaiscript::fun([](std::shared_ptr<Creature>& a_lhs, std::shared_ptr<Creature>& a_rhs) {
		return a_lhs.get() == a_rhs.get();
	}), "==");
	a_script.add(chaiscript::fun([](std::shared_ptr<Creature>& a_lhs, std::shared_ptr<Creature>& a_rhs) {
		return a_lhs.get() != a_rhs.get();
	}), "!=");
	a_script.add(chaiscript::fun([]() {
		return std::shared_ptr<Creature>();
	}), "nullCreature");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<Creature>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Creature>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<Creature>>>("VectorCreature"));
}

MV::Script::Registrar<Creature> _hookCreature {};

MV::Script::Registrar<ClientCreature> _hookClientCreature([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<ClientCreature>(), "ClientCreature");
	a_script.add(chaiscript::base_class<Creature, ClientCreature>());
	a_script.add(chaiscript::base_class<MV::Scene::Component, ClientCreature>());

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<ClientCreature>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<ClientCreature>& a_item) { return std::static_pointer_cast<Creature>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ClientCreature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ClientCreature>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<ClientCreature>>>("VectorClientCreature"));
});

template<>
void MV::Script::Registrar<ServerCreature>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<ServerCreature>(), "ServerCreature");
	a_script.add(chaiscript::base_class<Creature, ServerCreature>());
	a_script.add(chaiscript::base_class<MV::Scene::Component, ServerCreature>());

	a_script.add(chaiscript::fun(&ServerCreature::onArrive), "onArrive");
	a_script.add(chaiscript::fun(&ServerCreature::onBlocked), "onBlocked");
	a_script.add(chaiscript::fun(&ServerCreature::onStop), "onStop");
	a_script.add(chaiscript::fun(&ServerCreature::onStart), "onStart");

	a_script.add(chaiscript::fun(&ServerCreature::pathAgent), "agent");

	a_script.add(chaiscript::fun(&ServerCreature::targeting), "targeting");

	a_script.add(chaiscript::fun([](ServerCreature& a_self, float a_range) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "enemiesInRange");

	a_script.add(chaiscript::fun([](ServerCreature& a_self, float a_range) {
		return a_self.gameInstance.teamForPlayer(a_self.player()).creaturesInRange(a_self.agent()->gridPosition(), a_range);
	}), "alliesInRange");

	a_script.add(chaiscript::fun([](ServerCreature& a_self) {
		a_self.fall();
	}), "fall");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<ServerCreature>>([](const MV::Scene::SafeComponent<ServerCreature>& a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<ServerCreature>& a_item) { return std::static_pointer_cast<Creature>(a_item.self()); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<ServerCreature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<ServerCreature>& a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<std::shared_ptr<ServerCreature>>>("VectorServerCreature"));
}

MV::Script::Registrar<ServerCreature> _hookServerCreature {};

template<>
void MV::Script::Registrar<Team>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
a_script.add(chaiscript::user_type<Team>(), "Team");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return &a_self.game;
	}), "game");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.enemyWellPosition;
	}), "enemyWell");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.ourWellPosition;
	}), "ourWell");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.health;
	}), "health");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.game.buildings;
	}), "buildings");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.game.creatures;
	}), "creatures");

	a_script.add(chaiscript::fun([](Team &a_self, const MV::Point<> &a_location, float a_radius) {
		return a_self.creaturesInRange(a_location, a_radius);
	}), "creaturesInRange");
}

MV::Script::Registrar<Team> _hookTeam{};

MV::Script::Registrar<GameInstance> _hookGameInstance([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<GameInstance>(), "GameInstance");

	a_script.add(chaiscript::fun(&GameInstance::creature), "creature");
	a_script.add(chaiscript::fun(&GameInstance::spawnCreature), "spawnCreature");
});

#ifdef BINDSTONE_SERVER

MV::Script::Registrar<ServerGameInstance> _hookServerGameInstance([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	auto serverGame = a_services.get<GameInstance>();
	if(serverGame){
		a_script.add(chaiscript::fun([serverGame](std::shared_ptr<BattleEffectNetworkState> a_effect) {
			auto networkBattleEffect = serverGame->networkPool().spawn(a_effect);

			auto recentlyCreatedBattleEffect = serverGame->gameObjectContainer()->make("E_" + std::to_string(networkBattleEffect->id()));
			recentlyCreatedBattleEffect->position(networkBattleEffect->self()->position);

			return recentlyCreatedBattleEffect->attach<ServerBattleEffect>(networkBattleEffect, *serverGame);
		}), "spawnOnNetwork");
	}
});

#endif

template<>
void MV::Script::Registrar<Game>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<Game>(), "Game");
	a_script.add(chaiscript::fun(&Game::gui), "gui");
	a_script.add(chaiscript::fun(&Game::instance), "instance");
	a_script.add(chaiscript::fun(&Game::root), "root");
	a_script.add(chaiscript::fun(&Game::localPlayer), "localPlayer");
	a_script.add(chaiscript::fun(&Game::ourMouse), "mouse");
	a_script.add(chaiscript::fun(&Game::enterGame), "enterGame");
	a_script.add(chaiscript::fun(&Game::killGame), "killGame");
	a_script.add(chaiscript::fun(&Game::ourLobbyClient), "client");
	a_script.add(chaiscript::fun(&Game::loginId), "loginId");
	a_script.add(chaiscript::fun(&Game::loginPassword), "loginPassword");
	auto login = a_services.get<DefaultLogin>();
	a_script.add_global_const(chaiscript::const_var(login->id), "DefaultLoginId");
	a_script.add_global_const(chaiscript::const_var(login->password), "DefaultPassword");

	a_script.add_global(chaiscript::var(this), "game");
}

MV::Script::Registrar<Game> _hookGame {};

MV::Script::Registrar<Constants> _hookConstants([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<Constants>(), "Constants");

	a_script.add(chaiscript::fun([](Constants& a_self) {
		return a_self.startHealth;
	}), "startHealth");
});

template<>
void MV::Script::Registrar<GameData>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
	a_script.add(chaiscript::user_type<GameData>(), "GameData");

	a_script.add(chaiscript::fun([](GameData& a_self) {
		return a_self.buildingCatalog.get();
	}), "buildings");
	a_script.add(chaiscript::fun([](GameData& a_self) {
		return a_self.creatureCatalog.get();
	}), "creatures");
	a_script.add(chaiscript::fun([](GameData& a_self) {
		return a_self.metadataConstants;
	}), "constants");
}

MV::Script::Registrar<GameData> _hookGameData {};

MV::Script::Registrar<InGamePlayer> _hookInGamePlayer([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<InGamePlayer>(), "Player");

// 	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
// 		return a_self.add(CurrencyType::GAME, a_amount);
// 	}), "add");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
// 		return a_self.remove(CurrencyType::GAME, a_amount);
// 	}), "remove");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
// 		return a_self.value(CurrencyType::GAME, a_amount);
// 	}), "value");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
// 		return a_self.hasEnough(CurrencyType::GAME, a_amount);
// 	}), "hasEnough");
});

MV::Script::Registrar<Wallet> _hookWallet([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<Wallet>(), "Wallet");

	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
		return a_self.add(Wallet::CurrencyType::GAME, a_amount);
	}), "add");
	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
		return a_self.remove(Wallet::CurrencyType::GAME, a_amount);
	}), "remove");
	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
		return a_self.value(Wallet::CurrencyType::GAME, a_amount);
	}), "value");
	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
		return a_self.hasEnough(Wallet::CurrencyType::GAME, a_amount);
	}), "hasEnough");
});