#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/game.h"
#include "Game/battleEffect.h"
#include <cmath>
#include <iostream>

#include <jaiscript/core/registrar.hpp>

static jai::registrar<GameInstance, MV::Services> _hookGameInstance([](jai::dynamic_binder<GameInstance>& builder, const MV::Services&) {
	builder.method("creature", [](GameInstance& a_self, int64_t a_id) { return a_self.creature(a_id); });
	builder.method("spawnCreature", [](GameInstance& a_self, int64_t a_slot, const std::string& a_id) {
		a_self.spawnCreature(static_cast<int>(a_slot), a_id);
	}, jai::serial);
	builder.method("data", [](GameInstance& a_self) -> GameData& { return a_self.data(); });
	builder.method("pathMap", [](GameInstance& a_self) { return a_self.path().self(); });
	builder.method("camera", [](GameInstance& a_self) { return a_self.camera(); });
});

void GameInstance::handleScroll(float a_amount, const MV::Point<int>& a_position) {
	if (worldCamera && requestCamera()) {
		auto around = worldView->viewFromScreen(a_position);
		auto zoom = worldCamera->zoom();
		auto target = zoom + .05f * a_amount + (zoom / 10.0f * a_amount);
		if (cameraLimits) {
			target = std::max(std::min(target, cameraLimits->maximumZoom(false)), cameraLimits->minimumZoom(false));
		}
		if (std::abs(target - zoom) > 0.00000001f) {
			worldCamera->zoomAround(target, around);
		}
		if (cameraLimits) {
			cameraLimits->hold()->clamp(around)->release(around);
		}
	}
}

void GameInstance::releaseCamera(const MV::Point<int>& a_position) {
	if (cameraLimits && worldView) {
		cameraLimits->release(worldView->viewFromScreen(a_position));
	}
}

GameInstance::GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep, size_t a_maxFixedStepsPerUpdate) :
	worldScene(a_root->make("Scenes/map.scene", services())->depth(-1)),
	worldView(a_root->component<MV::Scene::View>(false, false)),
	ourMouse(a_mouse),
	gameData(a_gameData),
	timeStep(a_timeStep),
	maxFixedStepsPerUpdate(a_maxFixedStepsPerUpdate),
	scriptEngine(a_gameData.managers().services),
	onTeamDefeated(onTeamDefeatedSignal) {
}

void GameInstance::initialize(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer) {
	left = std::make_unique<Team>(a_leftPlayer, LEFT, *this);
	right = std::make_unique<Team>(a_rightPlayer, RIGHT, *this);

	//manually updating this.
	worldScene->silence().forget()->pause();

	if (worldView) {
		worldCamera = worldView->camera();
	}
	if (worldCamera) {
		cameraStartPosition = worldCamera->owner()->position();
		cameraStartZoom = worldCamera->zoom();
	}
	cameraLimits = worldScene->component<MV::Scene::CameraConstraint>(false, false);

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
		releaseCamera(ourMouse.position());
	});

	zoomSignal = ourMouse.onPinchZoom.connect([&](MV::Point<int> a_point, float a_zoomAmount, float a_rotateAmount) {
		handleScroll(a_zoomAmount * 22.5f, a_point);
	});

	left->initialize();
	right->initialize();

	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		fixedUpdate(a_dt);
		return true;
	}).recent()->interval(timeStep, maxFixedStepsPerUpdate);
}

GameInstance::~GameInstance() {
	worldScene->removeFromParent();
	if (worldCamera && worldCamera->ownerIsAlive()) {
		worldCamera->owner()->position(cameraStartPosition);
		worldCamera->zoom(cameraStartZoom);
	}
}

void GameInstance::beginMapDrag() {
	if (worldCamera && requestCamera()) {
		ourMouse.queueExclusiveAction(MV::ExclusiveTapAction(true, { 10 }, [&]() {
			if (cameraLimits) {
				cameraLimits->hold();
			}
			activeDrag = ourMouse.onMove.connect([this](MV::TapDevice& a_mouse) {
				auto around = worldView->viewFromScreen(a_mouse.position());
				worldCamera->sceneTranslate(worldCamera->sceneFromView(worldView->viewFromScreen(a_mouse.oldPosition())) - worldCamera->sceneFromView(around));
				if (cameraLimits) {
					cameraLimits->clamp(around);
				}
			});
		}, [this]() {
			activeDrag.reset();
			releaseCamera(ourMouse.position());
		}, "MapDrag"));
	}
}

void GameInstance::fixedUpdate(double a_dt) {
	if (pathMap) { pathMap->beginTick(); }
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
	if (a_event.type == SDL_EVENT_MOUSE_WHEEL) {
		handleScroll(static_cast<MV::PointPrecision>(a_event.wheel.integer_y), mouse().position());
	}
	ourMouse.updateTouch(a_event, gameData.managers().renderer.window().drawableSize());
	return false;
}

void GameInstance::moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::PointPrecision a_zoom, bool a_interruptable) {
	moveCamera(a_targetNode->worldTransform().position(), a_zoom, a_interruptable);
}

void GameInstance::moveCamera(MV::Point<> a_center, MV::PointPrecision a_zoom, bool a_interruptable) {
	if (!worldCamera) {
		return;
	}
	auto startCenter = worldCamera->center();
	auto startZoom = worldCamera->zoom();
	cameraAction.cancel();
	if (cameraLimits) {
		cameraLimits->hold();
	}
	cameraAction.then(a_interruptable ? "interruptable" : "force", [this, startCenter, a_center, startZoom, a_zoom](MV::Task &a_self, double /*a_dt*/) {
		float percent = std::min(static_cast<MV::PointPrecision>(a_self.localElapsed() / 0.5f), 1.0f);
		worldCamera->zoom(MV::mix(startZoom, a_zoom, percent, 2.0f));
		worldCamera->sceneTranslate(MV::mix(startCenter, a_center, percent, 2.0f) - worldCamera->center());
		if (percent == 1.0f && cameraLimits) {
			cameraLimits->release();
		}
		return percent != 1.0f;
	});
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
	GameInstance(a_game.worldLayer(), a_game.data(), a_game.mouse(), 1.0f / 60.0f, 4),
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

int64_t ClientGameInstance::localPlayerId() const {
	return game.player() ? game.player()->id : -1;
}

bool ClientGameInstance::canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &a_player) const {
	return a_player == game.player();
}

#ifdef BINDSTONE_SERVER
ServerGameInstance::ServerGameInstance(GameServer& a_game) :
	GameInstance(a_game.root(), a_game.data(), a_game.mouse(), 1.0f / 10.0f, 1),
	gameServer(a_game) {
	onTeamDefeated.connect("reportMatchResult", [this](Team& a_team) {
		gameServer.reportMatchResult(!a_team.side());
	});

	synchronizedObjects.onSpawn<CreatureNetworkState>([this](std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_newItem) {
		a_newItem->self()->netId = a_newItem->id();
	});

	synchronizedObjects.onSpawn<BattleEffectNetworkState>([this](std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_newItem) {
		a_newItem->self()->netId = a_newItem->id();
	});

	// Server scripts spawn effects as a free function: spawnOnNetwork(BattleEffectNetworkState(...))
	script().engine().add_function("spawnOnNetwork", [this](std::shared_ptr<BattleEffectNetworkState> a_effect) {
		auto networkBattleEffect = networkPool().spawn(a_effect);
		auto effectNode = gameObjectContainer()->make("E_" + std::to_string(networkBattleEffect->id()));
		effectNode->position(networkBattleEffect->self()->position);
		return effectNode->attach<ServerBattleEffect>(networkBattleEffect, *this).self();
	}, jai::serial);   // the network pool and the scene graph
}

void ServerGameInstance::updateImplementation(double /*a_dt*/) {
	auto updated = makeSynchronizeNetworkString(networkPool());
	if (!updated.empty()) {
		gameServer.sendToPlayers(updated);
	}
}
#endif
