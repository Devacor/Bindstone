#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/game.h"
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

GameInstance::GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse) :
	worldScene(a_root->make("Assets/Scenes/map.scene", services())->depth(0)),
	ourMouse(a_mouse),
	gameData(a_gameData),
	left(a_leftPlayer, LEFT, *this),
	right(a_rightPlayer, RIGHT, *this),
	scriptEngine(MV::chaiscript_module_paths(), MV::chaiscript_use_paths(), chaiscript::default_options()) {

	//manually updating this.
	worldScene->silence().forget()->pause();

	pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();

	right.enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(LEFT) + "Goal")->worldFromLocal(MV::Point<>())));
	left.enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(RIGHT) + "Goal")->worldFromLocal(MV::Point<>())));

	right.ourWellPosition = left.enemyWellPosition;
	left.ourWellPosition = right.enemyWellPosition;

	mouseSignal = ourMouse.onLeftMouseDown.connect([&](MV::TapDevice& /*a_mouse*/) {
		beginMapDrag();
	});

	hook();

	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		worldScene->update(static_cast<float>(a_dt), true);
		return false;
	}).recent()->interval(1.0 / 60, 15);
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

	Building::hook(scriptEngine, *this);
	Creature::hook(scriptEngine, *this);
	Missile::hook(scriptEngine);

	scriptEngine.add(chaiscript::user_type<GameInstance>(), "GameInstance");
	scriptEngine.add(chaiscript::fun([](GameInstance& a_self, std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void(Missile&)> a_onArrive) { 
		a_self.spawnMissile(a_source, a_target, a_prefab, a_speed, a_onArrive);
	}), "spawnMissile");
}

bool GameInstance::update(double a_dt) {
	for (auto&& missile : missiles) {
		missile->update(a_dt);
	}
	removeExpiredMissiles();
	worldTimestep.update(a_dt);
	cameraAction.update(a_dt);
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

void GameInstance::spawnMissile(std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void(Missile&)> a_onArrive) {
	missiles.push_back(std::make_unique<Missile>(*this, a_source, a_target, a_prefab, a_speed, a_onArrive));
}

void GameInstance::removeMissile(Missile* a_toRemove) {
	expiredMissiles.push_back(a_toRemove);
}

void GameInstance::removeExpiredMissiles() {
	missiles.erase(std::remove_if(missiles.begin(), missiles.end(), [&](std::unique_ptr<Missile> &a_missile){
		return std::find_if(expiredMissiles.begin(), expiredMissiles.end(), [&](Missile* a_compareWith) {return a_missile.get() == a_compareWith; }) != expiredMissiles.end();
	}), missiles.end());
	expiredMissiles.clear();
}

ClientGameInstance::ClientGameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, Game& a_game) :
	GameInstance(a_leftPlayer, a_rightPlayer, a_game.root(), a_game.data(), a_game.mouse()),
	game(a_game) {

	syncronizedObjects.onSpawn<Creature::NetworkState>([this](std::shared_ptr<MV::NetworkObject<Creature::NetworkState>> a_newItem) {
		auto creatureState = a_newItem->self();
		building(creatureState->buildingSlot)->spawnNetworkCreature(a_newItem);
	});

	auto playlistGame = std::make_shared<MV::AudioPlayList>();
	playlistGame->addSoundBack("gameintro");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");

	playlistGame->loopSounds(true);

	//game.managers().audio.setMusicPlayList(playlistGame);

	//playlistGame->beginPlaying();
}

void ClientGameInstance::requestUpgrade(int a_slot, size_t a_upgrade) {
	std::cout << "Building Upgrade Request: " << a_slot << ", " << a_upgrade << std::endl;
	game.gameClient()->send(makeNetworkString<RequestBuildingUpgrade>(a_slot, a_upgrade));
}

void ClientGameInstance::performUpgrade(int a_slot, size_t a_upgrade) {
	auto selectedBuilding = building(a_slot);
	selectedBuilding->upgrade(a_upgrade);
	//selectedBuilding->update(game.gameClient()->clientServerTimeDelta());
}

bool ClientGameInstance::canUpgradeBuildingFor(const std::shared_ptr<Player> &a_player) const {
	return a_player == game.player();
}

#ifdef BINDSTONE_SERVER
ServerGameInstance::ServerGameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, GameServer& a_game) :
	GameInstance(a_leftPlayer, a_rightPlayer, a_game.root(), a_game.data(), a_game.mouse()),
	gameServer(a_game){
}
#endif


MockClientGameInstance::MockClientGameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, Game& a_game) :
	GameInstance(a_leftPlayer, a_rightPlayer, a_game.root(), a_game.data(), a_game.mouse()),
	game(a_game) {

	syncronizedObjects.onSpawn<Creature::NetworkState>([this](std::shared_ptr<MV::NetworkObject<Creature::NetworkState>> a_newItem) {
		auto creatureState = a_newItem->self();
		building(creatureState->buildingSlot)->spawnNetworkCreature(a_newItem);
	});
	auto playlistGame = std::make_shared<MV::AudioPlayList>();
	playlistGame->addSoundBack("gameintro");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");
	playlistGame->addSoundBack("gameloop");

	playlistGame->loopSounds(true);

	//game.managers().audio.setMusicPlayList(playlistGame);

	//playlistGame->beginPlaying();
}

void MockClientGameInstance::requestUpgrade(int a_slot, size_t a_upgrade) {
	performUpgrade(a_slot, a_upgrade);
	GameInstance::requestUpgrade(a_slot, a_upgrade);

	//game.gameClient()->send(makeNetworkString<RequestBuildingUpgrade>(teamForPlayer(a_owner).side(), a_slot, a_upgrade));
}

void MockClientGameInstance::performUpgrade(int a_slot, size_t a_upgrade) {
	auto selectedBuilding = building(a_slot);
	selectedBuilding->upgrade(a_upgrade);
	//selectedBuilding->update(game.gameClient()->clientServerTimeDelta());
}

bool MockClientGameInstance::canUpgradeBuildingFor(const std::shared_ptr<Player> &a_player) const {
	return true; //
}

void MockClientGameInstance::spawnCreature(int a_buildingSlot) {
	auto spawner = building(a_buildingSlot);
	auto creatureStatTemplate = data().creatures().data(spawner->currentCreature().id);
	networkPool().spawn(std::make_shared<Creature::NetworkState>(creatureStatTemplate, a_buildingSlot));
}
