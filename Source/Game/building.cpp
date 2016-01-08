#include "building.h"
#include "Game/Instance/gameInstance.h"
#include "Game/player.h"

Building::Building(const std::weak_ptr<MV::Scene::Node> &a_owner, const BuildingData &a_data, const std::string &a_skin, int a_slot, const std::shared_ptr<Player> &a_player, GameInstance& a_instance) :
	Component(a_owner),
	buildingData(a_data),
	skin(a_skin),
	slot(a_slot),
	owningPlayer(a_player),
	gameInstance(a_instance),
	onUpgraded(onUpgradedSignal){

	if (!a_owner.expired()) {
		auto spawnObject = a_owner.lock()->get("spawn", false);
		if (spawnObject) {
			spawnPoint = spawnObject->position();
		}
	}
}

void Building::initialize() {
	auto newNode = owner()->make(assetPath(), [&](cereal::JSONInputArchive& archive) {
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
			gameInstance.moveCamera(a_self->owner(), 1.0f);
			auto modal = gameInstance.scene()->make("modal")->nodePosition(a_self->owner());
			modal->attach<MV::Scene::Clickable>(gameInstance.mouse())->clickDetectionType(MV::Scene::Clickable::BoundsType::LOCAL)->bounds({ MV::point(-10000.0f, -10000.0f), MV::point(10000.0f, 10000.0f) })->onAccept.connect("dismiss", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
				gameInstance.scene()->get("modal")->removeFromParent();
			});

			auto dialog = modal->make("dialog")->attach<MV::Scene::Grid>()->
				margin(MV::size(4.0f, 4.0f))->
				padding(MV::point(0.0f, 0.0f), MV::point(2.0f, 2.0f))->
				color(0xCC659f23)->owner();

			for (size_t i = 0; i < current()->upgrades.size();++i) {
				auto&& currentUpgrade = current()->upgrades[i];
				auto upgradeButton = button(dialog, gameInstance.data().managers().textLibrary, gameInstance.mouse(), MV::size(256.0f, 20.0f), MV::toWide(currentUpgrade->name + ": " + MV::to_string(currentUpgrade->cost)));
				auto* upgradePointer = currentUpgrade.get();
				upgradeButton->onAccept.connect("tryToBuy", [&, i, upgradePointer](std::shared_ptr<MV::Scene::Clickable> a_self){
					if (owningPlayer->wallet.remove(Wallet::CurrencyType::SOFT, upgradePointer->cost)) {
						upgrade(i);
					}
					gameInstance.scene()->get("modal")->removeFromParent();
				});
			}
			auto dialogBounds = dialog->bounds().size();
			dialog->translate({ -(dialogBounds.width / 2), 50.0f });
		});
	}
}

void Building::spawnCurrentCreature() {
	auto creatureNode = gameInstance.path().owner()->make(MV::guid(currentCreature().id));
	creatureNode->attach<Creature>(currentCreature().id, gameInstance);
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

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, GameInstance& a_gameInstance) :
	Component(a_owner),
	ourStats(a_gameInstance.data().creatures().data(a_id)),
	gameInstance(a_gameInstance) {
}

Creature::Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const CreatureData &a_stats, GameInstance& a_gameInstance) :
	Component(a_owner),
	ourStats(a_stats),
	gameInstance(a_gameInstance) {
}