#include "player.h"
#include "state.h"
#include "Render/package.h"
#include "cereal/cereal.hpp"
#include "cereal/archives/json.hpp"

Team::Team(const std::shared_ptr<Player> &a_player, LocalData& a_data, MV::MouseState &a_mouse, std::shared_ptr<MV::Scene::Node> a_gameBoard, TeamSide a_side) :
	player(a_player),
	mouse(a_mouse),
	gameBoard(a_gameBoard),
	side(a_side),
	data(a_data),
	health(a_data.constants().startHealth){

	auto sideString = sideToString(side);
	for (int i = 0; i < 8; ++i) {
		auto buildingNode = gameBoard->get(sideString + "_" + std::to_string(i));

		auto newNode = buildingNode->make("Assets/Prefabs/life_0.prefab", [&](cereal::JSONInputArchive& archive) {
			archive.add(
				cereal::make_nvp("mouse", &mouse),
				cereal::make_nvp("renderer", &LocalData.managers().renderer),
				cereal::make_nvp("textLibrary", &managers.textLibrary),
				cereal::make_nvp("pool", &managers.pool),
				cereal::make_nvp("texture", &managers.textures)
				);
		});
		newNode->component<MV::Scene::Spine>()->animate("idle");

		auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
		auto treeButton = leftNode->attach<MV::Scene::Clickable>(mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::CHILDREN)->show()->color({ 0xFFFFFFFF });
		treeButton->onAccept.connect("TappedBuilding", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
			//spawnCreature(a_self->worldBounds().bottomRightPoint());
			std::cout << "Left Building: " << i << std::endl;
		});
	}
}
