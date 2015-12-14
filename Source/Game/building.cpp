#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"

Building::Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance) :
	Component(a_owner),
	buildingData(a_data),
	skin(a_skin),
	slot(a_slot),
	owningPlayer(a_player),
	gameInstance(a_instance) {

	if (!a_owner.expired()) {
		auto spawnObject = a_owner.lock()->get("spawn", false);
		if (spawnObject) {
			spawnPoint = spawnObject->position();
		}
	}
}

void Building::initialize() {
	auto newNode = owner()->make(buildingData.skinAssetPath(skin), [&](cereal::JSONInputArchive& archive) {
		archive.add(
			cereal::make_nvp("mouse", &gameInstance.mouse()),
			cereal::make_nvp("renderer", &gameInstance.data().managers().renderer),
			cereal::make_nvp("textLibrary", &gameInstance.data().managers().textLibrary),
			cereal::make_nvp("pool", &gameInstance.data().managers().pool),
			cereal::make_nvp("texture", &gameInstance.data().managers().textures)
			);
	});
	newNode->component<MV::Scene::Spine>()->animate("idle");

	if (gameInstance.data().isLocal(owningPlayer)) {
		auto spineBounds = newNode->component<MV::Scene::Spine>()->bounds();
		std::cout << "BOUNDS: " << spineBounds << std::endl;
		auto buildingButton = newNode->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
		//auto treeSprite = newNode->attach<MV::Scene::Sprite>()->bounds(newNode->bounds())->color({ 0x77FFFFFF });
		auto nodeBounds = newNode->bounds();
		buildingButton->onAccept.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
			auto dialog = a_self->owner()->make("dialog");

			dialog->attach<MV::Scene::Grid>()->margin(MV::point(4.0f, 4.0f), MV::point(2.0f, 2.0f))->padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f))->color(0x77659f23);

			for (size_t i = 0; i < buildingData.current()->upgrades.size();++i) {
				auto&& upgrade = buildingData.current()->upgrades[i];
				auto upgradeButton = button(dialog, gameInstance.data().managers().textLibrary, gameInstance.mouse(), MV::size(128.0f, 20.0f), MV::toWide(upgrade->name + ": " + MV::to_string(upgrade->cost)));
				auto* upgradePointer = upgrade.get();
				upgradeButton->onAccept.connect("tryToBuy", [&, i](std::shared_ptr<MV::Scene::Clickable> a_self){
					if (owningPlayer->wallet.remove(Wallet::CurrencyType::SOFT, upgradePointer->cost)) {
						buildingData.upgrade(i);
					}
					a_self->owner()->parent()->removeFromParent();
				});
			}
			auto dialogBounds = dialog->bounds().size();
			dialog->translate({ -(dialogBounds.width / 2), 50.0f });
		});
	}
}
