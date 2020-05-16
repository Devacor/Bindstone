#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/game.h"
#include "Game/battleEffect.h"
#include <cmath>
#include <iostream>

void GameInstance::handleScroll(float a_amount, const MV::Point<int>& a_position) {
	if (requestCamera()) {
		auto screenScale = .05f * a_amount + (worldScene->scale().x / 10.0f * a_amount);
		auto finalScale = screenScale + worldScene->scale().x;

		if (finalScale > maxScaleHard) {
			screenScale -= finalScale - maxScaleHard;
		}
		if (finalScale < minScaleHard) {
			screenScale -= finalScale - minScaleHard;
		}
		if (std::abs(screenScale) > 0.00000001f) {
			rawScaleAroundScreenPoint(worldScene->scale().x + screenScale, a_position);
		}
		easeToBoundsIfExceeded(a_position);
	}
}

void GameInstance::rawScaleAroundCenter(float a_scale) {
	rawScaleAroundScreenPoint(a_scale, MV::toPoint(gameData.managers().renderer.window().drawableSize() / 2));
}

void GameInstance::rawScaleAroundScreenPoint(float a_scale, const MV::Point<int>& a_position) {
	auto originalScreenPosition = worldScene->localFromScreen(a_position) * (MV::toPoint(a_scale - worldScene->scale()));
	worldScene->scale(a_scale);
	worldScene->translate(originalScreenPosition * -1.0f);
	MV::info("CurrentScale: ", worldScene->scale());
}

GameInstance::GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep) :
	worldScene(a_root->make("Scenes/map.scene", services())->depth(0)->cameraId(GameCameraId)),
	ourMouse(a_mouse),
	gameData(a_gameData),
	timeStep(a_timeStep),
	scriptEngine(a_gameData.managers().services) {
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

	ourMouse.onLeftMouseDown.connect("beginDragObserver", [this](MV::TapDevice& /*a_mouse*/) {
		beginMapDrag();
	});

	ourMouse.onLeftMouseUp.connect("cancelDragObserver", [this](MV::TapDevice& a_mouse2) {
		activeDrag.reset();
		easeToBoundsIfExceeded(ourMouse.position());
	});

	zoomSignal = ourMouse.onPinchZoom.connect([&](MV::Point<int> a_point, float a_zoomAmount, float a_rotateAmount) {
		handleScroll(a_zoomAmount * 22.5f, a_point);
	});

	left->initialize();
	right->initialize();

#ifdef BINDSTONE_SERVER
	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		fixedUpdate(a_dt);
		return true;
	}).recent()->interval(timeStep, 1);
#else
	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		fixedUpdate(a_dt);
		return true;
	});
#endif
}

GameInstance::~GameInstance() {
	worldScene->removeFromParent();
}

void GameInstance::beginMapDrag() {
	if (requestCamera()) {
		ourMouse.queueExclusiveAction(MV::ExclusiveTapAction(true, { 10 }, [&]() {
			activeDrag = ourMouse.onMove.connect([this](MV::TapDevice& a_mouse) {
				if (worldScene) {
					worldScene->camera().translate(MV::round<MV::PointPrecision>(a_mouse.position() - a_mouse.oldPosition()));
				}
			});
		}, []() {}, "MapDrag"));
	}
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
		handleScroll(static_cast<MV::PointPrecision>(a_event.wheel.y), mouse().position());
	}
	ourMouse.updateTouch(a_event, gameData.managers().renderer.window().drawableSize());
	return false;
}

void GameInstance::moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::Scale a_scale, bool interruptable) {
	moveCamera(((worldScene->worldPosition() - a_targetNode->worldPosition()) / a_targetNode->worldScale()) + MV::toPoint(gameData.managers().renderer.world().size() / 2.0f), a_scale);
}

void GameInstance::moveCamera(MV::Point<> endPosition, MV::Scale endScale, bool interruptable) {
	auto startPosition = worldScene->position();
	auto startScale = worldScene->scale();
	cameraAction.cancel();
	cameraAction.then(interruptable ? "interruptable" : "force", [&, startPosition, endPosition, startScale, endScale](MV::Task &a_self, double /*a_dt*/) {
        float percent = std::min(static_cast<MV::PointPrecision>(a_self.localElapsed() / 0.5f), 1.0f);
		worldScene->silence()->
			position(MV::mix(startPosition, endPosition, percent, 2.0f))->
			scale(MV::mix(startScale, endScale, percent, 2.0f));
		return percent != 1.0f;
	});
}

void GameInstance::easeToBoundsIfExceeded(const MV::Point<int> &a_pointerCenter) {
	if (!cameraIsFree()) {
		return;
	}
	if (worldScene->scale().x < minScaleSoft || worldScene->scale().x > maxScaleSoft) {
		auto startScale = worldScene->scale().x;
		auto endScale = worldScene->scale().x > maxScaleSoft ? maxScaleSoft : minScaleSoft;
		cameraAction.cancel();
		cameraAction.then(std::make_shared<MV::BlockForSeconds>(0.15f));

		cameraAction.thenAlso("interruptable", [this, startScale, endScale, a_pointerCenter](MV::Task& a_self, double /*a_dt*/) {
			float percent = std::min(static_cast<MV::PointPrecision>(a_self.localElapsed() * 3.0f), 1.0f);
			rawScaleAroundScreenPoint(MV::mixOut(startScale, endScale, percent, 2.0f), a_pointerCenter);
			MV::info("WorldBounds: ", worldScene->screenBounds(), " BasicBounds: ", worldScene->bounds(), " DrawableBounds: ", worldScene->renderer().window().drawableSize());
			return percent != 1.0f;
		});
	}
}

bool GameInstance::cameraIsFree() const {
	return !cameraAction.contains("force");
}

bool GameInstance::requestCamera() {
	if (cameraIsFree()) {
		activeDrag.reset();
		cameraAction.cancel();
		return true;
	}
	return false;
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
