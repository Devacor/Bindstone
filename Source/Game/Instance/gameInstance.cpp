#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/game.h"
#include "Game/battleEffect.h"
#include <iostream>

void GameInstance::handleScroll(int a_amount) {
	if (cameraAction.finished()) {
		auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
		if (worldScene->scale().x + screenScale.x > .2f) {
			auto originalScreenPosition = worldScene->localFromScreen(ourMouse.position()) * (MV::toPoint(screenScale));
			worldScene->addScale(screenScale);
			worldScene->translate(originalScreenPosition * -1.0f);
		}
	}
}

GameInstance::GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep) :
	worldScene(a_root->make("Scenes/map.scene", services())->depth(0)),
	ourMouse(a_mouse),
	gameData(a_gameData),
	timeStep(a_timeStep),
	scriptEngine(MV::chaiscript_module_paths(), MV::chaiscript_use_paths(), [](const std::string& a_file) {return MV::fileContents(a_file, true); }, chaiscript::default_options()) {
}

void GameInstance::initialize(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer) {
	left = std::make_unique<Team>(a_leftPlayer, LEFT, *this);
	right = std::make_unique<Team>(a_rightPlayer, RIGHT, *this);

	//manually updating this.
	worldScene->silence().forget()->pause();

	pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();

	right->enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(LEFT) + "Goal")->worldFromLocal(MV::Point<>())));
	left->enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(RIGHT) + "Goal")->worldFromLocal(MV::Point<>())));

	right->ourWellPosition = left->enemyWellPosition;
	left->ourWellPosition = right->enemyWellPosition;

	mouseSignal = ourMouse.onLeftMouseDown.connect([&](MV::TapDevice& /*a_mouse*/) {
		beginMapDrag();
	});

	hook();

	left->initialize();
	right->initialize();

	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		fixedUpdate(a_dt);
		return true;
	}).recent()->interval(timeStep, 1);
}

GameInstance::~GameInstance() {
	worldScene->removeFromParent();
}

void GameInstance::beginMapDrag() {
	if (cameraAction.finished()) {
		ourMouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = ourMouse.onMove.connect(MV::guid("inDrag"), [&](MV::TapDevice& a_mouse) {
				if (worldScene) {
					worldScene->translate(MV::round<MV::PointPrecision>(a_mouse.position() - a_mouse.oldPosition()));
				}
			});
			auto cancelId = MV::guid("cancelDrag");
			ourMouse.onLeftMouseUp.connect(cancelId, [=](MV::TapDevice& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
				a_mouse2.onLeftMouseUp.disconnect(cancelId);
			});
		}, []() {}, "MapDrag"));
	}
}

void GameInstance::hook() {
	bindstoneScriptHook(scriptEngine, ourMouse, gameData.managers().pool);

	gameData.managers().messages.hook(scriptEngine);

	Building::hook(scriptEngine, *this);

	scriptEngine.add(chaiscript::user_type<GameInstance>(), "GameInstance");

	scriptEngine.add(chaiscript::fun(&GameInstance::creature), "creature");
	scriptEngine.add(chaiscript::fun(&GameInstance::spawnCreature), "spawnCreature");
}

void ClientGameInstance::hook() {
	GameInstance::hook();

	ClientCreature::hook(scriptEngine, *this);
	ClientBattleEffect::hook(scriptEngine, *this);
}

void GameInstance::fixedUpdate(double a_dt) {
	worldScene->update(static_cast<float>(a_dt), true);
	fixedUpdateImplementation(a_dt);
}

bool GameInstance::update(double a_dt) {
	worldTimestep.update(a_dt);
	cameraAction.update(a_dt);
	updateImplementation(a_dt);
	return false;
}

bool GameInstance::handleEvent(const SDL_Event &a_event) {
	if (a_event.type == SDL_MOUSEWHEEL) {
		handleScroll(a_event.wheel.y);
	}
	return false;
}

void GameInstance::moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::Scale a_scale) {
	moveCamera(((worldScene->worldPosition() - a_targetNode->worldPosition()) / a_targetNode->worldScale()) + MV::toPoint(gameData.managers().renderer.world().size() / 2.0f), a_scale);
}

void GameInstance::moveCamera(MV::Point<> endPosition, MV::Scale endScale) {
	auto startPosition = worldScene->position();
	auto startScale = worldScene->scale();
	cameraAction.cancel();
	cameraAction.then("zoomAndPan", [&, startPosition, endPosition, startScale, endScale](MV::Task &a_self, double /*a_dt*/) {
        float percent = std::min(static_cast<MV::PointPrecision>(a_self.elapsed() / 0.5f), 1.0f);
		worldScene->position(MV::mix(startPosition, endPosition, percent, 2.0f));
		worldScene->scale(MV::mix(startScale, endScale, percent, 2.0f));
		return percent == 1.0f;
	});
}

ClientGameInstance::ClientGameInstance(Game& a_game) :
	GameInstance(a_game.root(), a_game.data(), a_game.mouse(), 1.0f / 60.0f),
	game(a_game) {

	synchronizedObjects.onSpawn<BuildingNetworkState>([this](std::shared_ptr<MV::NetworkObject<BuildingNetworkState>> a_newItem) {
		building(a_newItem->self()->buildingSlot)->initializeNetworkState(a_newItem);
	});

	synchronizedObjects.onSpawn<CreatureNetworkState>([this](std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_newItem) {
		auto creatureState = a_newItem->self();
		creatureState->netId = a_newItem->id();
		building(*creatureState->buildingSlot)->spawnNetworkCreature(a_newItem);
	});

	synchronizedObjects.onSpawn<BattleEffectNetworkState>([this](std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_newItem) {
		auto battleEffectState = a_newItem->self();
		battleEffectState->netId = a_newItem->id();

		auto battleEffectNode = gameObjectContainer()->make("E_" + std::to_string(battleEffectState->netId));
		battleEffectNode->position(battleEffectState->position);

		battleEffectNode->attach<ClientBattleEffect>(a_newItem, *this);
	});

	/*auto playlistGame = std::make_shared<MV::AudioPlayList>();
	playlistGame->addSoundBack("gameintro");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");*/

	//playlistGame->loopSounds(true);

	//game.managers().audio.setMusicPlayList(playlistGame);

	//playlistGame->beginPlaying();
}

void ClientGameInstance::requestUpgrade(int a_slot, int a_upgrade) {
	std::cout << "Building Upgrade Request: " << a_slot << ", " << a_upgrade << std::endl;
	game.gameClient()->send(makeNetworkString<RequestBuildingUpgrade>(a_slot, a_upgrade));
}

void ClientGameInstance::performUpgrade(int a_slot, int a_upgrade) {
	auto selectedBuilding = building(a_slot);
	selectedBuilding->upgrade(a_upgrade);
	//selectedBuilding->update(game.gameClient()->clientServerTimeDelta());
}

bool ClientGameInstance::canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &a_player) const {
	return a_player == game.player();
}

#ifdef BINDSTONE_SERVER
void ServerGameInstance::hook() {
	GameInstance::hook();

	ServerCreature::hook(scriptEngine, *this);
	ServerBattleEffect::hook(scriptEngine, *this);

	scriptEngine.add(chaiscript::fun([&](std::shared_ptr<BattleEffectNetworkState> a_effect) {
		auto networkBattleEffect = networkPool().spawn(a_effect);

		auto recentlyCreatedBattleEffect = gameObjectContainer()->make("E_" + std::to_string(networkBattleEffect->id()));
		recentlyCreatedBattleEffect->position(networkBattleEffect->self()->position);

		return recentlyCreatedBattleEffect->attach<ServerBattleEffect>(networkBattleEffect, *this);
		}), "spawnOnNetwork");
}

ServerGameInstance::ServerGameInstance(GameServer& a_game) :
	GameInstance(a_game.root(), a_game.data(), a_game.mouse(), 1.0f / 10.0f),
	gameServer(a_game){
	synchronizedObjects.onSpawn<CreatureNetworkState>([this](std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_newItem) {
		a_newItem->self()->netId = a_newItem->id();
	});

	synchronizedObjects.onSpawn<BattleEffectNetworkState>([this](std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_newItem) {
		a_newItem->self()->netId = a_newItem->id();
	});
}

void ServerGameInstance::updateImplementation(double /*a_dt*/) {
	auto updated = makeSynchronizeNetworkString(networkPool());
	if (!updated.empty()) {
		gameServer.server()->sendAll(updated);
	}
}
#endif
