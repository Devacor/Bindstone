#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "Utility/generalUtility.h"

#include "chaiscript/chaiscript_stdlib.hpp"

Building::Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance) :
	Component(a_owner),
	buildingData(a_data),
	skin(a_skin),
	slot(a_slot),
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
	//newNode->component<MV::Scene::Spine>()->animate("idle");
	owner()->scale(owner()->scale() * gameInstance.teamForPlayer(owningPlayer).scale());
	initializeBuildingButton(newNode);
}

void Building::spawnCurrentCreature() {
	auto creatureNode = gameInstance.creatureContainer()->make(MV::guid(currentCreature().id));
	creatureNode->worldPosition(owner()->worldFromLocal(spawnPoint));
	
	creatureNode->attach<Creature>(currentCreature().id, skin, owningPlayer, gameInstance);
}

void Building::spawnNetworkCreature(std::shared_ptr<MV::NetworkObject<Creature::NetworkState>> a_synchronizedCreature) {
	auto creatureNode = gameInstance.creatureContainer()->make(std::to_string(a_synchronizedCreature->id()));
	creatureNode->worldPosition(owner()->worldFromLocal(spawnPoint));


	creatureNode->attach<Creature>(a_synchronizedCreature->self()->creatureTypeId, skin, owningPlayer, gameInstance);
}

void Building::incrementCreatureIndex() {
	++creatureIndex;
	if (creatureIndex >= current()->waves[waveIndex].creatures.size()) {
		creatureIndex = 0;
		++waveIndex;
		if (waveIndex >= current()->waves.size()) {
			waveIndex = 0;
		}
	}
}

void Building::updateImplementation(double a_dt) {
	countdown -= a_dt;
	if (countdown <= 0 && waveHasCreatures(waveIndex)) {
		spawnCurrentCreature();
		incrementCreatureIndex();
		countdown = countdown + currentCreature().delay;
	}
}

bool Building::waveHasCreatures(size_t a_waveIndex /*= 0*/, size_t a_creatureIndex /*= 0*/) const {
	auto* currentUpgrade = current();
	return (currentUpgrade->waves.size() > a_waveIndex && currentUpgrade->waves[a_waveIndex].creatures.size() > a_creatureIndex);
}

void Building::upgrade(size_t a_index) {
	buildTreeIndices.push_back(a_index);
	onUpgradedSignal(std::static_pointer_cast<Building>(shared_from_this()));

	waveIndex = 0;
	creatureIndex = 0;
	countdown = waveHasCreatures() ? 0 : current()->waves[0].creatures[0].delay;
}

void Building::requestUpgrade(size_t a_index) {
	gameInstance.requestUpgrade(owningPlayer, slot, a_index);
}

const BuildTree* Building::current() const {
	const BuildTree* currentBuildTree = &buildingData.game;
	for (auto& index : buildTreeIndices) {
		currentBuildTree = currentBuildTree->upgrades[index].get();
	}
	return currentBuildTree;
}

const WaveCreature& Building::currentCreature() {
	return current()->waves[waveIndex].creatures[creatureIndex];
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
			auto modal = gameInstance.scene()->make("modal")->nodePosition(a_self->owner());
			modal->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::LOCAL)->bounds({ MV::point(-10000.0f, -10000.0f), MV::point(10000.0f, 10000.0f) })->onAccept.connect("dismiss", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
				gameInstance.scene()->get("modal")->removeFromParent();
			});

			auto dialog = modal->make("dialog")->attach<MV::Scene::Grid>()->
				margin(MV::size(4.0f, 4.0f))->
				padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f))->
				color(0xCC659f23)->owner();

			for (size_t i = 0; i < current()->upgrades.size(); ++i) {
				auto&& currentUpgrade = current()->upgrades[i];
				auto upgradeButton = button(dialog, gameInstance.data().managers().textLibrary, gameInstance.mouse(), MV::size(256.0f, 20.0f), currentUpgrade->name + ": " + MV::to_string(currentUpgrade->cost));
				auto* upgradePointer = currentUpgrade.get();
				upgradeButton->onAccept.connect("tryToBuy", [&, i, upgradePointer](std::shared_ptr<MV::Scene::Clickable> a_self) {
					if (owningPlayer->wallet.remove(Wallet::CurrencyType::SOFT, upgradePointer->cost)) {
						requestUpgrade(i);
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
