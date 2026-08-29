#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/game.h"
#include "Game/battleEffect.h"
#include <cmath>
#include <iostream>

#include <jaiscript/core/registrar.hpp>

static jai::registrar<GameInstance, MV::Services> _hookGameInstance("GameInstance",
	[](jai::dynamic_binder<GameInstance>& builder, const MV::Services&) {
	builder.method("creature", [](GameInstance& a_self, int64_t a_id) { return a_self.creature(a_id); });
	builder.method("spawnCreature", [](GameInstance& a_self, int64_t a_slot, const std::string& a_id) {
		a_self.spawnCreature(static_cast<int>(a_slot), a_id);
	});
	builder.method("data", [](GameInstance& a_self) -> GameData& { return a_self.data(); });
	builder.method("pathMap", [](GameInstance& a_self) { return a_self.path().self(); });
});

void GameInstance::handleScroll(float a_amount, const MV::Point<int>& a_position) {
	if (requestCamera()) {
		auto screenScale = .05f * a_amount + (worldScene->scale().x / 10.0f * a_amount);
		auto finalScale = screenScale + worldScene->scale().x;

		if (finalScale > maxScaleHard) {
			screenScale -= finalScale - maxScaleHard;
		}
		auto floorScale = minScale(false);
		if (finalScale < floorScale) {
			screenScale -= finalScale - floorScale;
		}
		if (std::abs(screenScale) > 0.00000001f) {
			rawScaleAroundScreenPoint(worldScene->scale().x + screenScale, a_position);
		}
		clampToHardBounds(a_position);
		easeToBoundsIfExceeded(a_position);
	}
}

MV::BoxAABB<> GameInstance::visibleMapBounds() {
	return worldScene->localFromScreen(MV::BoxAABB<int>(MV::Point<int>(), MV::toPoint(gameData.managers().renderer.window().drawableSize())));
}

bool GameInstance::cornerBounds(MV::BoxAABB<>& a_bounds, const std::string& a_suffix) {
	auto topLeft = worldScene->get("TopLeft_" + a_suffix, false);
	auto bottomRight = worldScene->get("BottomRight_" + a_suffix, false);
	if (!topLeft || !bottomRight) { return false; }
	a_bounds = MV::BoxAABB<>(worldScene->localFromWorld(topLeft->worldPosition()), worldScene->localFromWorld(bottomRight->worldPosition()));
	a_bounds.sanitize();
	return a_bounds.width() > 0.0f && a_bounds.height() > 0.0f;
}

bool GameInstance::mapViewBounds(MV::BoxAABB<>& a_bounds, bool a_soft) {
	if (cornerBounds(a_bounds, a_soft ? "Soft" : "Hard")) { return true; }
	if (!pathMap) { return false; }
	a_bounds = worldScene->localFromWorld(pathMap->owner()->worldBounds(false));
	if (a_soft) {
		auto inset = MV::toPoint(a_bounds.size()) * viewSoftInset;
		a_bounds.minPoint += inset;
		a_bounds.maxPoint -= inset;
	}
	return a_bounds.width() > 0.0f && a_bounds.height() > 0.0f;
}

float GameInstance::minScale(bool a_soft) {
	MV::BoxAABB<> bounds;
	return mapViewBounds(bounds, a_soft) ? std::max(scaleSanityFloor, scaleToFit(bounds)) : scaleSanityFloor;
}

float GameInstance::scaleToFit(const MV::BoxAABB<>& a_bounds) {
	auto visible = visibleMapBounds();
	auto ratio = std::max(visible.width() / a_bounds.width(), visible.height() / a_bounds.height());
	return worldScene->scale().x * ratio;   // the scale at which the view exactly fits, larger or smaller than now
}

MV::Point<> GameInstance::viewCorrection(const MV::BoxAABB<>& a_bounds) {
	auto visible = visibleMapBounds();
	MV::Point<> correction;
	if (visible.width() >= a_bounds.width()) { correction.x = ((a_bounds.minPoint.x + a_bounds.maxPoint.x) - (visible.minPoint.x + visible.maxPoint.x)) / 2.0f; }
	else if (visible.minPoint.x < a_bounds.minPoint.x) { correction.x = a_bounds.minPoint.x - visible.minPoint.x; }
	else if (visible.maxPoint.x > a_bounds.maxPoint.x) { correction.x = a_bounds.maxPoint.x - visible.maxPoint.x; }
	if (visible.height() >= a_bounds.height()) { correction.y = ((a_bounds.minPoint.y + a_bounds.maxPoint.y) - (visible.minPoint.y + visible.maxPoint.y)) / 2.0f; }
	else if (visible.minPoint.y < a_bounds.minPoint.y) { correction.y = a_bounds.minPoint.y - visible.minPoint.y; }
	else if (visible.maxPoint.y > a_bounds.maxPoint.y) { correction.y = a_bounds.maxPoint.y - visible.maxPoint.y; }
	return correction;
}

void GameInstance::clampToHardBounds(const MV::Point<int>& a_pointerCenter) {
	MV::BoxAABB<> hard;
	if (!mapViewBounds(hard, false)) { return; }
	auto fit = scaleToFit(hard);
	if (fit > worldScene->scale().x) {
		rawScaleAroundScreenPoint(fit, a_pointerCenter);
	}
	auto correction = viewCorrection(hard);
	if (correction != MV::Point<>()) {
		worldScene->translate(correction * -worldScene->scale().x);
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

GameInstance::GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep, size_t a_maxFixedStepsPerUpdate) :
	worldScene(a_root->make("Scenes/map.scene", services())->depth(-1)->cameraId(GameCameraId)),
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

	worldTimestep.then("update", [&](MV::Task& /*a_self*/, double a_dt) {
		fixedUpdate(a_dt);
		return true;
	}).recent()->interval(timeStep, maxFixedStepsPerUpdate);
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
					clampToHardBounds(a_mouse.position());
				}
			});
		}, [this]() {
			activeDrag.reset();
			easeToBoundsIfExceeded(ourMouse.position());
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
	MV::BoxAABB<> soft;
	bool haveSoft = mapViewBounds(soft, true);
	auto startScale = worldScene->scale().x;
	auto endScale = std::clamp(startScale, minScale(true), maxScaleSoft);
	bool outsideSoft = haveSoft && viewCorrection(soft) != MV::Point<>();
	if (endScale == startScale && !outsideSoft) {
		return;
	}
	cameraAction.cancel();
	cameraAction.then(std::make_shared<MV::BlockForSeconds>(static_cast<float>(viewEaseHoldSeconds)));

	auto eased = std::make_shared<float>(0.0f);
	cameraAction.thenAlso("interruptable", [this, startScale, endScale, a_pointerCenter, soft, haveSoft, eased](MV::Task& a_self, double /*a_dt*/) {
		float percent = std::min(static_cast<MV::PointPrecision>(a_self.localElapsed() / viewEaseSeconds), 1.0f);
		float easedNow = MV::mixOut(0.0f, 1.0f, percent, 2.0f);
		rawScaleAroundScreenPoint(MV::mixOut(startScale, endScale, percent, 2.0f), a_pointerCenter);
		if (haveSoft) {
			// each frame closes the share of the remaining distance that this frame's easing covers
			float step = percent >= 1.0f ? 1.0f : std::clamp((easedNow - *eased) / std::max(1.0f - *eased, 0.0001f), 0.0f, 1.0f);
			worldScene->translate(viewCorrection(soft) * (-worldScene->scale().x * step));
		}
		*eased = easedNow;
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
	GameInstance(a_game.root(), a_game.data(), a_game.mouse(), 1.0f / 60.0f, 4),
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
	});
}

void ServerGameInstance::updateImplementation(double /*a_dt*/) {
	auto updated = makeSynchronizeNetworkString(networkPool());
	if (!updated.empty()) {
		gameServer.server()->sendAll(updated);
	}
}
#endif
