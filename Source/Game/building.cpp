#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "Utility/generalUtility.h"

Building::Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance) :
	Component(a_owner),
	buildingData(a_data),
	skin(a_skin),
	slot(a_slot),
	owningPlayer(a_player),
	gameInstance(a_instance),
	onUpgraded(onUpgradedSignal){

	if (!a_owner.expired()) {
		auto spawnObject = a_owner.lock()->get("spawn", false);
		if (spawnObject) {
			spawnPoint = spawnObject->position();
		}
	}
}

void Building::initialize() {
	auto newNode = owner()->make(assetPath(), [&](cereal::JSONInputArchive& archive) {
		archive.add(
			cereal::make_nvp("mouse", &gameInstance.mouse()),
			cereal::make_nvp("renderer", &gameInstance.data().managers().renderer),
			cereal::make_nvp("textLibrary", &gameInstance.data().managers().textLibrary),
			cereal::make_nvp("pool", &gameInstance.data().managers().pool),
			cereal::make_nvp("texture", &gameInstance.data().managers().textures)
			);
	});
	newNode->component<MV::Scene::Spine>()->animate("idle");
	owner()->scale(owner()->scale() * gameInstance.teamForPlayer(owningPlayer).scale());
	initializeBuildingButton(newNode);
}

void Building::spawnCurrentCreature() {
	auto creatureNode = gameInstance.path().owner()->make(MV::guid(currentCreature().id));
	creatureNode->worldPosition(owner()->worldFromLocal(spawnPoint));
	creatureNode->attach<Creature>(currentCreature().id, skin, owningPlayer, gameInstance);
}

void Building::incrementCreatureIndex() {
	++creatureIndex;
	if (creatureIndex >= current()->waves[waveIndex].creatures.size()) {
		creatureIndex = 0;
		++waveIndex;
		if (waveIndex >= current()->waves.size()) {
			waveIndex = 0;
		}
	}
}

void Building::updateImplementation(double a_dt) {
	countdown -= a_dt;
	if (countdown <= 0 && waveHasCreatures(waveIndex)) {
		spawnCurrentCreature();
		incrementCreatureIndex();
		countdown = countdown + currentCreature().delay;
	}
}

bool Building::waveHasCreatures(size_t a_waveIndex /*= 0*/, size_t a_creatureIndex /*= 0*/) const {
	auto* currentUpgrade = current();
	return (currentUpgrade->waves.size() > a_waveIndex && currentUpgrade->waves[a_waveIndex].creatures.size() > a_creatureIndex);
}

void Building::upgrade(size_t a_index) {
	buildTreeIndices.push_back(a_index);
	onUpgradedSignal(std::static_pointer_cast<Building>(shared_from_this()));

	waveIndex = 0;
	creatureIndex = 0;
	countdown = waveHasCreatures() ? 0 : current()->waves[0].creatures[0].delay;
}

const BuildTree* Building::current() const {
	const BuildTree* currentBuildTree = &buildingData.game;
	for (auto& index : buildTreeIndices) {
		currentBuildTree = currentBuildTree->upgrades[index].get();
	}
	return currentBuildTree;
}

const WaveCreature& Building::currentCreature() {
	return current()->waves[waveIndex].creatures[creatureIndex];
}

void Building::initializeBuildingButton(const std::shared_ptr<MV::Scene::Node> &a_newNode) {
	if (gameInstance.data().isLocal(owningPlayer)) {
		auto spineBounds = a_newNode->component<MV::Scene::Spine>()->bounds();
		auto buildingButton = a_newNode->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
		//auto treeSprite = a_newNode->attach<MV::Scene::Sprite>()->bounds(newNode->bounds())->color({ 0x77FFFFFF });
		auto nodeBounds = a_newNode->bounds();
		buildingButton->onDrag.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self, const MV::Point<int> &, const MV::Point<int> &) {
			if (a_self->totalDragDistance() > 8.0f) {
				a_self->cancelPress(false);
				gameInstance.beginMapDrag();
			}
		});
		buildingButton->onAccept.connect("AcceptedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
			gameInstance.moveCamera(a_self->owner(), 1.0f);
			auto modal = gameInstance.scene()->make("modal")->nodePosition(a_self->owner());
			modal->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::LOCAL)->bounds({ MV::point(-10000.0f, -10000.0f), MV::point(10000.0f, 10000.0f) })->onAccept.connect("dismiss", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
				gameInstance.scene()->get("modal")->removeFromParent();
			});

			auto dialog = modal->make("dialog")->attach<MV::Scene::Grid>()->
				margin(MV::size(4.0f, 4.0f))->
				padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f))->
				color(0xCC659f23)->owner();

			for (size_t i = 0; i < current()->upgrades.size(); ++i) {
				auto&& currentUpgrade = current()->upgrades[i];
				auto upgradeButton = button(dialog, gameInstance.data().managers().textLibrary, gameInstance.mouse(), MV::size(256.0f, 20.0f), MV::toWide(currentUpgrade->name + ": " + MV::to_string(currentUpgrade->cost)));
				auto* upgradePointer = currentUpgrade.get();
				upgradeButton->onAccept.connect("tryToBuy", [&, i, upgradePointer](std::shared_ptr<MV::Scene::Clickable> a_self) {
					if (owningPlayer->wallet.remove(Wallet::CurrencyType::SOFT, upgradePointer->cost)) {
						upgrade(i);
					}
					gameInstance.scene()->get("modal")->removeFromParent();
				});
			}
			auto dialogBounds = dialog->bounds().size();
			dialog->translate({ -(dialogBounds.width / 2), 50.0f });
		});
	}
}

// MV::SignalRegister<CallbackSignature> onArrive;
// MV::SignalRegister<CallbackSignature> onBlocked;
// MV::SignalRegister<CallbackSignature> onStop;
// MV::SignalRegister<CallbackSignature> onStart;
// 
// MV::SignalRegister<CallbackSignature> onStatus;
// MV::SignalRegister<CallbackSignature> onHealthChange;
// MV::SignalRegister<CallbackSignature> onKilled;
// MV::SignalRegister<CallbackSignature> onFall;

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, const std::string &a_skin, const std::shared_ptr<Player> &a_player, GameInstance& a_gameInstance) :
	Component(a_owner),
	statTemplate(a_gameInstance.data().creatures().data(a_id)),
	skin(a_skin),
	owningPlayer(a_player),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onBlocked(onBlockedSignal),
	onStop(onStopSignal),
	onStart(onStartSignal),
	onStatus(onStatusSignal),
	onDeath(onDeathSignal),
	onFall(onFallSignal),
	onHealthChange(onHealthChangeSignal){
}

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const CreatureData &a_stats, const std::string &a_skin, const std::shared_ptr<Player> &a_player, GameInstance& a_gameInstance) :
	Component(a_owner),
	statTemplate(a_stats),
	skin(a_skin),
	owningPlayer(a_player),
	gameInstance(a_gameInstance),
	onArrive(onArriveSignal),
	onBlocked(onBlockedSignal),
	onStop(onStopSignal),
	onStart(onStartSignal),
	onStatus(onStatusSignal),
	onDeath(onDeathSignal),
	onFall(onFallSignal),
	onHealthChange(onHealthChangeSignal) {
}

std::string Creature::assetPath() const {
	return "Assets/Prefabs/Creatures/" + statTemplate.id + "/" + (skin.empty() ? statTemplate.id : skin) + ".prefab";
}

void Creature::initialize() {
	auto newNode = owner()->make(assetPath(), [&](cereal::JSONInputArchive& archive) {
		archive.add(
			cereal::make_nvp("mouse", &gameInstance.mouse()),
			cereal::make_nvp("renderer", &gameInstance.data().managers().renderer),
			cereal::make_nvp("textLibrary", &gameInstance.data().managers().textLibrary),
			cereal::make_nvp("pool", &gameInstance.data().managers().pool),
			cereal::make_nvp("texture", &gameInstance.data().managers().textures)
			);
	});
	newNode->serializable(false);
	newNode->componentInChildren<MV::Scene::Spine>()->animate("run");
	agent = newNode->attach<MV::Scene::PathAgent>(gameInstance.path().self(), gameInstance.path()->gridFromLocal(gameInstance.path()->owner()->localFromWorld(owner()->worldPosition())), 3)->
		gridSpeed(statTemplate.moveSpeed);

	task().also("UpdateZOrder", [&](const MV::Task &a_self, double a_dt) {
		owner()->depth(agent->gridPosition().y);
		return false;
	});

	agent->onStart.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStartSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	agent->onStop.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onStopSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	agent->onArrive.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onArriveSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	agent->onBlocked.connect("_PARENT", [&](std::shared_ptr<MV::Scene::PathAgent>) {
		onBlockedSignal(std::static_pointer_cast<Creature>(shared_from_this()));
	});
	
	auto self = std::static_pointer_cast<Creature>(shared_from_this());
	statTemplate.script(gameInstance.script()).spawn(self);

	owner()->scale(owner()->scale() * gameInstance.teamForPlayer(owningPlayer).scale());
	/* 
	auto voidTexture = managers.textures.pack("VoidGuy")->handle(0);
	auto creatureNode = pathMap->owner()->make(MV::guid("Creature_"));
	creatureNode->attach<MV::Scene::Sprite>()->texture(voidTexture)->size(MV::cast<MV::PointPrecision>(voidTexture->bounds().size()));
	creatureNode->attach<MV::Scene::PathAgent>(pathMap.self(), pathMap->gridFromLocal(pathMap->owner()->localFromWorld(a_position)))->
	gridSpeed(4.0f)->
	gridGoal(pathMap->gridFromLocal(pathMap->owner()->localFromWorld(worldScene->get("RightWell")->worldFromLocal(MV::Point<>()))))->
	onArrive.connect("!", [](std::shared_ptr<MV::Scene::PathAgent> a_self){
	std::weak_ptr<MV::Scene::Node> self = a_self->owner();
	a_self->owner()->task().then("Countdown", [=](const MV::Task& a_task, double a_dt) mutable {
	if (a_task.elapsed() > 4.0f) {
	self.lock()->removeFromParent();
	return true;
	}
	return false;
	});
	});
	std::weak_ptr<MV::Scene::Node> weakCreatureNode{ creatureNode };
	creatureNode->task().also("UpdateZOrder", [=](const MV::Task &a_self, double a_dt) {
	weakCreatureNode.lock()->depth(weakCreatureNode.lock()->position().y);
	return false;
	});
	*/
	//  	script.eval(R"(
	// 			{
	//  			auto newNode = worldScene.make()
	//  			newNode.position(Point(10, 10, 10))
	//  			auto spriteDude = newNode.attachSprite()
	//  			spriteDude.size(Size(128, 128))
	// 				spriteDude.texture(textures.pack("VoidGuy").handle(0))
	// 			}
	//  	)");
}

void Creature::updateImplementation(double a_delta) {
	auto self = std::static_pointer_cast<Creature>(shared_from_this());
	statTemplate.script(gameInstance.script()).update(self, a_delta);
}

chaiscript::ChaiScript& Creature::hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance) {
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

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.statTemplate;
	}), "stats");
	a_script.add(chaiscript::fun(&Creature::skin), "skin");
	a_script.add(chaiscript::fun(&Creature::agent), "agent");
	a_script.add(chaiscript::fun(&Creature::owningPlayer), "player");

	a_script.add(chaiscript::fun(&Creature::assetPath), "assetPath");
	a_script.add(chaiscript::fun([](Creature &a_self) {
		return &a_self.gameInstance;
	}), "gameInstance");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamForPlayer(a_self.owningPlayer);
	}), "team");

	a_script.add(chaiscript::fun([](Creature &a_self) {
		return a_self.gameInstance.teamAgainstPlayer(a_self.owningPlayer);
	}), "enemyTeam");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<Creature>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Creature>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Creature> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	return a_script;
}

CreatureScriptMethods& CreatureScriptMethods::loadScript(chaiscript::ChaiScript &a_script, const std::string &a_id) {
	if (scriptContents == "NIL") {
		scriptContents = MV::fileContents("Assets/Scripts/Creatures/" + a_id + ".script");
		if (!scriptContents.empty()) {
			auto localVariables = std::map<std::string, chaiscript::Boxed_Value>{
				{ "self", chaiscript::Boxed_Value(this) }
			};
			auto resetLocals = a_script.get_locals();
			a_script.set_locals(localVariables);
			SCOPE_EXIT{ a_script.set_locals(resetLocals); };
			a_script.eval(scriptContents);
		}
	}
	return *this;
}
