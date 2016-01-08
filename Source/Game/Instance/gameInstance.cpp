#include "Game/Instance/gameInstance.h"
#include "Game/building.h"
#include <iostream>

Team::Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	side(a_side),
	health(a_game.data().constants().startHealth) {

	auto sideString = sideToString(side);
	for (int i = 0; i < 8; ++i) {
		auto buildingNode = game.worldScene->get(sideString + "_" + std::to_string(i));

		buildingNode->attach<Building>(game.data().buildings().data(a_player->loadout.buildings[i]), a_player->loadout.skins[i], i, a_player, game);
// 		auto newNode = buildingNode->make("Assets/Prefabs/life_0.prefab", [&](cereal::JSONInputArchive& archive) {
// 			archive.add(
// 				cereal::make_nvp("mouse", &game.mouse),
// 				cereal::make_nvp("renderer", &game.data().managers().renderer),
// 				cereal::make_nvp("textLibrary", &game.data().managers().textLibrary),
// 				cereal::make_nvp("pool", &game.data().managers().pool),
// 				cereal::make_nvp("texture", &game.data().managers().textures)
// 				);
// 		});
// 		newNode->component<MV::Scene::Spine>()->animate("idle");
// 
// 		if (game.data().isLocal(player)) {
// 			auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
// 			auto treeButton = newNode->attach<MV::Scene::Clickable>(game.mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::CHILDREN)->show()->color({ 0xFFFFFFFF });
// 			treeButton->onAccept.connect("TappedBuilding", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
// 				auto dialog = a_self->owner()->make("dialog");
// 
// 				dialog->attach<MV::Scene::Grid>()->margin(MV::point(4.0f, 4.0f), MV::point(2.0f, 2.0f))->padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f));
// 				dialog->translate({ 0.0f, (i > 3) ? a_self->owner()->bounds().size().height + 100.0f : dialog->bounds().size().height - 100.0f });
// 			});
// 		}
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

GameInstance::GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, MV::MouseState& a_mouse, LocalData& a_data) :
	worldScene(MV::Scene::Node::load("map.scene", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1))),
	ourMouse(a_mouse),
	localData(a_data),
	left(a_leftPlayer, LEFT, *this),
	right(a_rightPlayer, RIGHT, *this),
	scriptEngine(MV::create_chaiscript_stdlib()) {

	hook();

	ourMouse.onLeftMouseDown.connect(MV::guid("initDrag"), [&](MV::MouseState& a_mouse) {
		if (cameraAction.finished()) {
			a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
				auto signature = ourMouse.onMove.connect(MV::guid("inDrag"), [&](MV::MouseState& a_mouse2) {
					worldScene->translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
				});
				auto cancelId = MV::guid("cancelDrag");
				ourMouse.onLeftMouseUp.connect(cancelId, [=](MV::MouseState& a_mouse2) {
					a_mouse2.onMove.disconnect(signature);
					a_mouse2.onLeftMouseUp.disconnect(cancelId);
				});
			}, []() {}, "MapDrag"));
		}
	});
// 
// 	for (int i = 0; i < 8; ++i) {
// 		auto leftNode = scene->get("left_" + std::to_string(i));
// 		auto rightNode = scene->get("right_" + std::to_string(i));
// 
// 		auto newNode = leftNode->make("Assets/Prefabs/Buildings/life/life.prefab", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1));
// 		newNode->component<MV::Scene::Spine>()->animate("idle");
// 
// 		auto newNode2 = rightNode->make("Assets/Prefabs/Buildings/life/life.prefab", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1));
// 		newNode2->scale({ -1.0f, 1.0f, 1.0f });
// 		newNode2->component<MV::Scene::Spine>()->animate("idle");
// 		auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
// 		auto treeButton = leftNode->attach<MV::Scene::Clickable>(ourMouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::CHILDREN)->show()->color({ 0xFFFFFFFF });
// 		treeButton->onAccept.connect("TappedBuilding", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
// 			//spawnCreature(a_self->worldBounds().bottomRightPoint());
// 			std::cout << "Left Building: " << i << std::endl;
// 		});
// 	}
	//pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();
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

	MV::PathNode::hook(scriptEngine);
	MV::NavigationAgent::hook(scriptEngine);

	MV::Scene::Node::hook(scriptEngine);
	MV::Scene::Component::hook(scriptEngine);
	MV::Scene::Drawable::hook(scriptEngine);
	MV::Scene::Sprite::hook(scriptEngine);
	MV::Scene::Text::hook(scriptEngine);
	MV::Scene::PathMap::hook(scriptEngine);
	MV::Scene::PathAgent::hook(scriptEngine);
	MV::Scene::Emitter::hook(scriptEngine, localData.managers().pool);
}

void GameInstance::nodeLoadBinder(cereal::JSONInputArchive &a_archive) {
	a_archive.add(
		cereal::make_nvp("mouse", &ourMouse),
		cereal::make_nvp("renderer", &localData.managers().renderer),
		cereal::make_nvp("textLibrary", &localData.managers().textLibrary),
		cereal::make_nvp("pool", &localData.managers().pool),
		cereal::make_nvp("texture", &localData.managers().textures)
	);
}

bool GameInstance::update(double a_dt) {
	worldScene->drawUpdate(static_cast<float>(a_dt));
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
	moveCamera(((worldScene->worldPosition() - a_targetNode->worldPosition()) / a_targetNode->worldScale()) + MV::toPoint(localData.managers().renderer.world().size() / 2.0f), a_scale);
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


