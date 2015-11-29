#include "Game/Instance/gameInstance.h"
#include <iostream>

Team::Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	side(a_side),
	health(a_game.data.constants().startHealth) {

	auto sideString = sideToString(side);
	for (int i = 0; i < 8; ++i) {
		auto buildingNode = game.scene->get(sideString + "_" + std::to_string(i));

		auto newNode = buildingNode->make("Assets/Prefabs/life_0.prefab", [&](cereal::JSONInputArchive& archive) {
			archive.add(
				cereal::make_nvp("mouse", &game.mouse),
				cereal::make_nvp("renderer", &game.data.managers().renderer),
				cereal::make_nvp("textLibrary", &game.data.managers().textLibrary),
				cereal::make_nvp("pool", &game.data.managers().pool),
				cereal::make_nvp("texture", &game.data.managers().textures)
			);
		});
		newNode->component<MV::Scene::Spine>()->animate("idle");

		if (game.data.isLocal(player)) {
			auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
			auto treeButton = newNode->attach<MV::Scene::Clickable>(game.mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::CHILDREN)->show()->color({ 0xFFFFFFFF });
			treeButton->onAccept.connect("TappedBuilding", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
				//spawnCreature(a_self->worldBounds().bottomRightPoint());
				std::cout << "Left Building: " << i << std::endl;
			});
		}
	}
}

void GameInstance::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (scene->scale().x + screenScale.x > .2f) {
		auto originalScreenPosition = scene->localFromScreen(mouse.position()) * (MV::toPoint(screenScale));
		scene->addScale(screenScale);
		scene->translate(originalScreenPosition * -1.0f);
	}
}

GameInstance::GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, MV::MouseState& a_mouse, LocalData& a_data) :
	scene(MV::Scene::Node::load("map.scene", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1))),
	mouse(a_mouse),
	data(a_data),
	left(a_leftPlayer, LEFT, *this),
	right(a_rightPlayer, RIGHT, *this),
	script(MV::create_chaiscript_stdlib()) {

	MV::TexturePoint::hook(script);
	MV::Color::hook(script);
	MV::Size<MV::PointPrecision>::hook(script);
	MV::Size<int>::hook(script, "i");
	MV::Point<MV::PointPrecision>::hook(script);
	MV::Point<int>::hook(script, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(script);
	MV::BoxAABB<int>::hook(script, "i");

	MV::TexturePack::hook(script);
	MV::TextureDefinition::hook(script);
	MV::FileTextureDefinition::hook(script);
	MV::TextureHandle::hook(script);
	MV::SharedTextures::hook(script);

	Wallet::hook(script);

	MV::PathNode::hook(script);
	MV::NavigationAgent::hook(script);

	MV::Scene::Node::hook(script);
	MV::Scene::Component::hook(script);
	MV::Scene::Drawable::hook(script);
	MV::Scene::Sprite::hook(script);
	MV::Scene::Text::hook(script);
	MV::Scene::PathMap::hook(script);
	MV::Scene::PathAgent::hook(script);

	mouse.onLeftMouseDown.connect(MV::guid("initDrag"), [&](MV::MouseState& a_mouse) {
		a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = mouse.onMove.connect(MV::guid("inDrag"), [&](MV::MouseState& a_mouse2) {
				scene->translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
			});
			auto cancelId = MV::guid("cancelDrag");
			mouse.onLeftMouseUp.connect(cancelId, [=](MV::MouseState& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
				a_mouse2.onLeftMouseUp.disconnect(cancelId);
			});
		}, []() {}, "MapDrag"));
	});

	for (int i = 0; i < 8; ++i) {
		auto leftNode = scene->get("left_" + std::to_string(i));
		auto rightNode = scene->get("right_" + std::to_string(i));

		auto newNode = leftNode->make("Assets/Prefabs/life_0.prefab", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1));
		newNode->component<MV::Scene::Spine>()->animate("idle");

		auto newNode2 = rightNode->make("Assets/Prefabs/life_0.prefab", std::bind(&GameInstance::nodeLoadBinder, this, std::placeholders::_1));
		newNode2->scale({ -1.0f, 1.0f, 1.0f });
		newNode2->component<MV::Scene::Spine>()->animate("idle");
		auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
		auto treeButton = leftNode->attach<MV::Scene::Clickable>(mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::CHILDREN)->show()->color({ 0xFFFFFFFF });
		treeButton->onAccept.connect("TappedBuilding", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
			//spawnCreature(a_self->worldBounds().bottomRightPoint());
			std::cout << "Left Building: " << i << std::endl;
		});
	}
	//pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();
}

void GameInstance::nodeLoadBinder(cereal::JSONInputArchive &a_archive) {
	a_archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &data.managers().renderer),
		cereal::make_nvp("textLibrary", &data.managers().textLibrary),
		cereal::make_nvp("pool", &data.managers().pool),
		cereal::make_nvp("texture", &data.managers().textures)
	);
}

bool GameInstance::update(double a_dt) {
	scene->drawUpdate(static_cast<float>(a_dt));
	return false;
}

bool GameInstance::handleEvent(const SDL_Event &a_event) {
	if (a_event.type == SDL_MOUSEWHEEL) {
		handleScroll(a_event.wheel.y);
	}
	return false;
}