#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"
#include "MV/Utility/generalUtility.h"
#include "chaiscript/chaiscript_stdlib.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

chaiscript::ChaiScript& BuildTree::hook(chaiscript::ChaiScript& a_script) {
	a_script.add(chaiscript::user_type<BuildTree>(), "BuildTree");

	a_script.add(chaiscript::fun(&BuildTree::id), "id");
	a_script.add(chaiscript::fun(&BuildTree::cost), "cost");
	a_script.add(chaiscript::fun(&BuildTree::income), "income");
	a_script.add(chaiscript::fun(&BuildTree::upgrades), "upgrades");

	return a_script;
}

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
}

void Building::upgrade(size_t a_index) {
	buildTreeIndices.push_back(a_index);
	onUpgradedSignal(std::static_pointer_cast<Building>(shared_from_this()));

	creatureIndex = 0;
}

void Building::requestUpgrade(size_t a_index) {
	gameInstance.requestUpgrade(slot, a_index);
}

const BuildTree* Building::current() const {
	const BuildTree* currentBuildTree = &buildingData.game;
	for (auto& index : buildTreeIndices) {
		currentBuildTree = currentBuildTree->upgrades[index].get();
	}
	return currentBuildTree;
}

chaiscript::ChaiScript& Building::hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance) {
	BuildTree::hook(a_script);

	StandardScriptMethods<Building>::hook(a_script, "Building");

	a_script.add(chaiscript::user_type<Building>(), "Building");
	a_script.add(chaiscript::base_class<Component, Building>());

	a_script.add(chaiscript::fun(&Building::current), "current");
	a_script.add(chaiscript::fun(&Building::upgrade), "upgrade");

	a_script.add(chaiscript::fun(&Building::buildingData), "data");
	a_script.add(chaiscript::fun(&Building::skin), "skin");
	a_script.add(chaiscript::fun(&Building::slot), "slot");
	a_script.add(chaiscript::fun(&Building::owningPlayer), "player");

	a_script.add(chaiscript::fun(&Building::assetPath), "assetPath");

	a_script.add(chaiscript::fun([](Building& a_self, const std::string& a_key) {
		return a_self.localVariables[a_key];
	}), "[]");

	a_script.add(chaiscript::fun([&](Building& a_self, const std::string& a_key) {
		gameInstance.spawnCreature(a_self.slotIndex(), a_key);
	}), "spawn");

	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<Building>>([](const MV::Scene::SafeComponent<Building> &a_item) { return a_item.self(); }));
	a_script.add(chaiscript::type_conversion<MV::Scene::SafeComponent<Building>, std::shared_ptr<MV::Scene::Component>>([](const MV::Scene::SafeComponent<Building> &a_item) { return std::static_pointer_cast<MV::Scene::Component>(a_item.self()); }));

	return a_script;
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
					if (owningPlayer->gameWallet.remove(Wallet::CurrencyType::GAME, upgradePointer->cost)) {
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
