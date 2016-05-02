#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include "Game/creature.h"
#include <iostream>

Team::Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	side(a_side),
	health(a_game.data().constants().startHealth) {

	auto sideString = sideToString(side);
	for (int i = 0; i < 8; ++i) {
		auto buildingNode = game.worldScene->get(sideString + "_" + std::to_string(i));

		buildings.push_back(buildingNode->attach<Building>(game.data().buildings().data(a_player->loadout.buildings[i]), a_player->loadout.skins[i], i, a_player, game).self());
	}
}

std::vector<std::shared_ptr<Creature>> Team::creaturesInRange(const MV::Point<> &a_location, float a_radius) {
	std::vector<std::shared_ptr<Creature>> result;
	std::copy_if(creatures.begin(), creatures.end(), std::back_inserter(result), [&](std::shared_ptr<Creature> &a_creature) {
		return a_creature->alive() && MV::distance(a_location, a_creature->agent()->gridPosition()) <= a_radius;
	});
	std::sort(result.begin(), result.end(), [&](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs) {
		return MV::distance(a_location, a_lhs->agent()->gridPosition()) < MV::distance(a_location, a_rhs->agent()->gridPosition());
	});
	return result;
}

void Team::spawn(std::shared_ptr<Creature> &a_registerCreature) {
	if (a_registerCreature->alive()) {
		creatures.push_back(a_registerCreature);
		a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
			creatures.erase(std::remove(creatures.begin(), creatures.end(), a_creature), creatures.end());
		});
	}
}

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

GameInstance::GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, MV::MouseState& a_mouse, GameData& a_data) :
	worldScene(MV::Scene::Node::load("map.scene", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1))),
	ourMouse(a_mouse),
	gameData(a_data),
	left(a_leftPlayer, LEFT, *this),
	right(a_rightPlayer, RIGHT, *this),
	scriptEngine(MV::create_chaiscript_stdlib()) {

	pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();

	right.enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(LEFT) + "Goal")->worldFromLocal(MV::Point<>())));
	left.enemyWellPosition = path()->gridFromLocal(path()->owner()->localFromWorld(scene()->get(sideToString(RIGHT) + "Goal")->worldFromLocal(MV::Point<>())));

	right.ourWellPosition = left.enemyWellPosition;
	left.ourWellPosition = right.enemyWellPosition;

	ourMouse.onLeftMouseDown.connect(MV::guid("initDrag"), [&](MV::MouseState& a_mouse) {
		beginMapDrag();
	});

	hook();

	worldTimestep.then("update", [&](MV::Task& a_self, double a_dt) {
		worldScene->update(static_cast<float>(a_dt));
		return false;
	}).last()->interval(1.0 / 30, 15);
}

void GameInstance::beginMapDrag() {
	if (cameraAction.finished()) {
		ourMouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = ourMouse.onMove.connect(MV::guid("inDrag"), [&](MV::MouseState& a_mouse) {
				worldScene->translate(MV::round<MV::PointPrecision>(a_mouse.position() - a_mouse.oldPosition()));
			});
			auto cancelId = MV::guid("cancelDrag");
			ourMouse.onLeftMouseUp.connect(cancelId, [=](MV::MouseState& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
				a_mouse2.onLeftMouseUp.disconnect(cancelId);
			});
		}, []() {}, "MapDrag"));
	}
}

void GameInstance::hook() {
	MV::TexturePoint::hook(scriptEngine);
	MV::Color::hook(scriptEngine);
	MV::Size<MV::PointPrecision>::hook(scriptEngine);
	MV::Size<int>::hook(scriptEngine, "i");
	MV::Point<MV::PointPrecision>::hook(scriptEngine);
	MV::Point<int>::hook(scriptEngine, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(scriptEngine);
	MV::BoxAABB<int>::hook(scriptEngine, "i");

	MV::TexturePack::hook(scriptEngine);
	MV::TextureDefinition::hook(scriptEngine);
	MV::FileTextureDefinition::hook(scriptEngine);
	MV::TextureHandle::hook(scriptEngine);
	MV::SharedTextures::hook(scriptEngine);

	Wallet::hook(scriptEngine);
	Player::hook(scriptEngine);
	Team::hook(scriptEngine);
	MV::Task::hook(scriptEngine);
	GameData::hook(scriptEngine);

	MV::PathNode::hook(scriptEngine);
	MV::NavigationAgent::hook(scriptEngine);

	MV::Scene::Node::hook(scriptEngine);
	MV::Scene::Component::hook(scriptEngine);
	MV::Scene::Drawable::hook(scriptEngine);
	MV::Scene::Sprite::hook(scriptEngine);
	MV::Scene::Spine::hook(scriptEngine);
	MV::Scene::Text::hook(scriptEngine);
	MV::Scene::PathMap::hook(scriptEngine);
	MV::Scene::PathAgent::hook(scriptEngine);
	MV::Scene::Emitter::hook(scriptEngine, gameData.managers().pool);

	Building::hook(scriptEngine, *this);
	Creature::hook(scriptEngine, *this);
	Missile::hook(scriptEngine);

	scriptEngine.add(chaiscript::user_type<GameInstance>(), "GameInstance");
	scriptEngine.add(chaiscript::fun([](GameInstance& a_self, std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void(Missile&)> a_onArrive) { 
		a_self.spawnMissile(a_source, a_target, a_prefab, a_speed, a_onArrive);
	}), "spawnMissile");

	scriptEngine.add(chaiscript::fun([](int a_from) {return MV::to_string(a_from); }), "to_string");
	scriptEngine.add(chaiscript::fun([](size_t a_from) {return MV::to_string(a_from); }), "to_string");
	scriptEngine.add(chaiscript::fun([](float a_from) {return MV::to_string(a_from); }), "to_string");
	scriptEngine.add(chaiscript::fun([](double a_from) {return MV::to_string(a_from); }), "to_string");
}

void GameInstance::nodeLoadBinder(cereal::JSONInputArchive &a_archive) {
	a_archive.add(
		cereal::make_nvp("mouse", &ourMouse),
		cereal::make_nvp("renderer", &gameData.managers().renderer),
		cereal::make_nvp("textLibrary", &gameData.managers().textLibrary),
		cereal::make_nvp("pool", &gameData.managers().pool),
		cereal::make_nvp("texture", &gameData.managers().textures)
	);
}

bool GameInstance::update(double a_dt) {
	for (auto&& missile : missiles) {
		missile->update(a_dt);
	}
	removeExpiredMissiles();
	worldTimestep.update(a_dt);
	cameraAction.update(a_dt);
	worldScene->draw();
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
	cameraAction.finish();
	cameraAction.then("zoomAndPan", [&, startPosition, endPosition, startScale, endScale](MV::Task &a_self, double a_dt) {
		auto percent = std::min(static_cast<MV::PointPrecision>(a_self.elapsed() / 0.5f), 1.0f);
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