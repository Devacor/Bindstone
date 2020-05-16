#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "MV/Utility/generalUtility.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

Building::Building(const std::weak_ptr<MV::Scene::Node> &a_owner, int a_slot, int a_loadoutSlot, const std::shared_ptr<InGamePlayer> &a_player, GameInstance& a_instance) :
	Component(a_owner),
	buildingData(a_instance.data().buildings().data(a_player->loadout.buildings[a_loadoutSlot])),
	slot(a_slot),
	loadoutSlot(a_loadoutSlot),
	owningPlayer(a_player),
	gameInstance(a_instance),
	onUpgraded(onUpgradedSignal){

	if (auto lockedOwner = a_owner.lock()) {
		auto spawnObject = lockedOwner->get("spawn", false);
		if (spawnObject) {
			spawnPoint = spawnObject->position();
		}
	}
}

void Building::initialize() {
	auto newNode = owner()->make(assetPath(), gameInstance.services());

	newNode->serializable(false);
	spineAnimator = newNode->componentInChildren<MV::Scene::Spine>(true, false).get();

	//newNode->component<MV::Scene::Spine>()->animate("idle");
	owner()->scale(owner()->scale() * gameInstance.teamForPlayer(owningPlayer).scale());
	initializeBuildingButton(newNode);

	auto self = std::static_pointer_cast<Building>(shared_from_this());
	buildingData.script(gameInstance.script()).spawn(self);
}

void Building::spawnNetworkCreature(std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_synchronizedCreature) {
	auto creatureNode = gameInstance.gameObjectContainer()->make("C_" + std::to_string(a_synchronizedCreature->id()));
	creatureNode->worldPosition(spawnPositionWorld());

	creatureNode->attach<ClientCreature>(a_synchronizedCreature, gameInstance);
}

std::string Building::skin() const {
	return player()->loadout.skins[loadoutSlot];
}

void Building::updateImplementation(double a_dt) {
	auto self = std::static_pointer_cast<Building>(shared_from_this());
	buildingData.script(gameInstance.script()).update(self, a_dt);

	if (spine() && state->self()->animationName != spine()->track().name()) {
		state->modify()->animationName = spine()->track().name();
		state->modify()->animationLoops = spine()->track().looping();
	}
}

void Building::upgrade(int a_index) {
	state->modify()->buildTreeIndices.push_back(static_cast<int32_t>(a_index));
	buildingUpgraded();
}

void Building::requestUpgrade(int a_index) {
	gameInstance.requestUpgrade(slot, a_index);
}

const BuildTree* Building::current() const {
	const BuildTree* currentBuildTree = &buildingData.game;
	for (auto& index : state->self()->buildTreeIndices) {
		currentBuildTree = currentBuildTree->upgrades[index].get();
	}
	return currentBuildTree;
}

void Building::initializeBuildingButton(const std::shared_ptr<MV::Scene::Node> &a_newNode) {
	if (gameInstance.canUpgradeBuildingFor(owningPlayer)) {
		auto spineBounds = a_newNode->component<MV::Scene::Spine>()->bounds();
		auto buildingButton = a_newNode->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
		//auto treeSprite = a_newNode->attach<MV::Scene::Sprite>()->bounds(newNode->bounds())->color({ 0x77FFFFFF });
		auto nodeBounds = a_newNode->bounds();
		buildingButton->onDrag.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self, const MV::Point<int> &, const MV::Point<int> &) {
			if (a_self->totalDragDistance() > 8.0f) {
				a_self->cancelPress(false);
				gameInstance.beginMapDrag();
			}
		});
		buildingButton->onAccept.connect("AcceptedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
			//gameInstance.moveCamera(a_self->owner(), 1.0f);
			auto modal = gameInstance.scene()->make("modal")->worldPosition(a_self->owner()->worldPosition());
			modal->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::LOCAL)->bounds({ MV::point(-10000.0f, -10000.0f), MV::point(10000.0f, 10000.0f) })->onAccept.connect("dismiss", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
				gameInstance.scene()->get("modal")->removeFromParent();
			});

			auto dialog = modal->make("dialog")->attach<MV::Scene::Grid>()->
				margin(MV::size(4.0f, 4.0f))->
				padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f))->
				color(0xCC659f23)->owner();

			for (size_t i = 0; i < current()->upgrades.size(); ++i) {
				auto&& currentUpgrade = current()->upgrades[i];
				auto upgradeButton = button(dialog, gameInstance.data().managers().textLibrary, gameInstance.mouse(), MV::size(256.0f, 20.0f), currentUpgrade->name() + ": " + MV::to_string(currentUpgrade->cost));
				auto* upgradePointer = currentUpgrade.get();
				upgradeButton->onAccept.connect("tryToBuy", [&, i, upgradePointer](std::shared_ptr<MV::Scene::Clickable> a_self) {
					if (owningPlayer->gameWallet.remove(Wallet::CurrencyType::GAME, upgradePointer->cost)) {
						requestUpgrade(static_cast<int>(i));
						//upgrade(i);
					}
					gameInstance.scene()->get("modal")->removeFromParent();
				});
			}
			auto dialogBounds = dialog->bounds().size();
			dialog->translate({ -(dialogBounds.width / 2), 50.0f });
		});
	}
}
