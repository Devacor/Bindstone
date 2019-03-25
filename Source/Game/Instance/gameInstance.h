#ifndef __MV_GAME_INSTANCE_H__
#define __MV_GAME_INSTANCE_H__

#include <memory>
#include <string>
#include "Game/player.h"
#include "MV/Network/networkObject.h"
#include "Game/NetworkLayer/synchronizeAction.h"

#include "Game/NetworkLayer/gameServer.h"

#include "Game/Instance/team.h"

class Missile;
class GameInstance {
	friend Team;

	GameInstance(const GameInstance &) = delete;
	GameInstance& operator=(const GameInstance &) = delete;
protected:
	virtual void initialize(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer);
	GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse);

public:
	~GameInstance();

	GameData& data() {
		return gameData;
	}

	void beginMapDrag();

	JsonNodeLoadBinder jsonLoadBinder() {
		return { gameData.managers(), ourMouse, scriptEngine };
	}
	BinaryNodeLoadBinder binaryLoadBinder() {
		return{ gameData.managers(), ourMouse, scriptEngine };
	}

	MV::Services& services() {
		return gameData.managers().services;
	}

	void fixedUpdate(double dt);
	bool update(double dt);

	virtual void requestUpgrade(int a_slot, size_t a_upgrade) {
		std::cout << "Building Upgrade Request: " << a_slot << ", " << a_upgrade << std::endl;
	}

	virtual void performUpgrade(int a_slot, size_t a_upgrade) {
		building(a_slot)->upgrade(a_upgrade);
	}

	std::shared_ptr<Building> building(int a_slot) {
		return buildings[a_slot];
	}

	bool handleEvent(const SDL_Event &a_event);

	MV::TapDevice& mouse() {
		return ourMouse;
	}

	std::shared_ptr<MV::Scene::Node> creatureContainer() const {
		return pathMap->owner()->makeOrGet("Creatures");
	}

	std::shared_ptr<MV::Scene::Node> missileContainer() const {
		return pathMap->owner()->makeOrGet("Missiles");
	}

	std::shared_ptr<MV::Scene::Node> scene() const {
		return worldScene;
	}
	
	MV::Scene::SafeComponent<MV::Scene::PathMap> path() const {
		return pathMap;
	}

	chaiscript::ChaiScript& script() {
		return scriptEngine;
	}

	Team& teamForPlayer(const std::shared_ptr<InGamePlayer> &a_player) {
		return left->player == a_player ? *left : *right;
	}

	Team& teamAgainstPlayer(const std::shared_ptr<InGamePlayer> &a_player) {
		return left->player == a_player ? *right : *left;
	}

	void moveCamera(MV::Point<> a_startPosition, MV::Scale a_scale);
	void moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::Scale a_scale);

	void spawnMissile(std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void(Missile&)> a_onArrive);

	void removeMissile(Missile* a_toRemove);

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &) const = 0;

	Team& teamForSide(TeamSide a_side) {
		return a_side == LEFT ? *left : *right;
	}

	BindstoneNetworkObjectPool& networkPool() {
		return syncronizedObjects;
	}

	virtual void spawnCreature(int /*a_buildingSlot*/) {
	}

	void registerCreature(std::shared_ptr<Creature> &a_registerCreature) {
		if (a_registerCreature->alive()) {
			creatures.push_back(a_registerCreature);
			a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
				creatures.erase(std::remove(creatures.begin(), creatures.end(), a_creature), creatures.end());
			});
		}
	}
protected:

	virtual void fixedUpdateImplementation(double /*a_dt*/) {}
	virtual void updateImplementation(double /*a_dt*/) {}

	void removeExpiredMissiles();
	void handleScroll(int a_amount);

	void hook();

	std::vector<std::shared_ptr<Building>> buildings;
	std::vector<std::shared_ptr<Creature>> creatures;

	GameData& gameData;

	MV::TapDevice &ourMouse;
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	chaiscript::ChaiScript scriptEngine;

	std::vector<std::unique_ptr<Missile>> missiles;
	std::vector<Missile*> expiredMissiles;

	MV::TapDevice::SignalType mouseSignal;

	std::unique_ptr<Team> left;
	std::unique_ptr<Team> right;

	MV::Task worldTimestep;
	MV::Task cameraAction;

	BindstoneNetworkObjectPool syncronizedObjects;
};

class ClientGameInstance : public GameInstance {
	ClientGameInstance(Game& a_game);
public:
	static std::unique_ptr<ClientGameInstance> make(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer, Game& a_game) {
		auto result = std::unique_ptr<ClientGameInstance>(new ClientGameInstance(a_game));
		result->initialize(a_leftPlayer, a_rightPlayer);
		return result;
	}

	virtual void requestUpgrade(int a_slot, size_t a_upgrade);

	virtual void performUpgrade(int a_slot, size_t a_upgrade) override;

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &a_player) const override;

private:
	Game &game;
};

class GameServer;
class ServerGameInstance : public GameInstance {
	ServerGameInstance(GameServer& a_game);
public:
	static std::unique_ptr<ServerGameInstance> make(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer, GameServer& a_game) {
		auto result = std::unique_ptr<ServerGameInstance>(new ServerGameInstance(a_game));
		result->initialize(a_leftPlayer, a_rightPlayer);
		return result;
	}

	virtual void spawnCreature(int a_buildingSlot) override {
		auto spawner = building(a_buildingSlot);
		auto creatureNode = creatureContainer()->make(MV::guid(spawner->currentCreature().id));
		creatureNode->worldPosition(spawner->spawnPositionWorld());

		creatureNode->attach<Creature>(spawner->currentCreature().id, spawner->slotIndex(), *this);
	}

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &/*a_player*/) const override {
		return true;
	}
protected:
	virtual void updateImplementation(double dt) override;

private:
	GameServer &gameServer;
};

class Missile {
public:
	Missile(GameInstance &a_gameInstance, std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void (Missile&)> a_onArrive):
		gameInstance(a_gameInstance),
		speed(a_speed),
		sourceCreature(a_source),
		targetCreature(a_target),
		groundLocation(a_target->owner()->position()),
		onArrive(a_onArrive){

		sourceDeathWatcher = sourceCreature->onDeath.connect([&](std::shared_ptr<Creature> a_self){
			sourceCreature.reset();
			sourceDeathWatcher.reset();
		});

		targetDeathWatcher = targetCreature->onDeath.connect([&](std::shared_ptr<Creature> a_self){
			targetCreature.reset();
			targetDeathWatcher.reset();
		});

		missile = gameInstance.missileContainer()->make("Assets/Prefabs/Missiles/" + a_prefab + ".prefab", gameInstance.services(), gameInstance.missileContainer()->getUniqueId("missile"));
		missile->position(a_source->owner()->position());
		missile->serializable(false);
	}

	std::shared_ptr<Creature> source() {
		return sourceCreature;
	}

	std::shared_ptr<Creature> target() {
		return targetCreature;
	}


	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Missile>(), "Missile");

		a_script.add(chaiscript::fun(&Missile::source), "source");
		a_script.add(chaiscript::fun(&Missile::target), "target");

		return a_script;
	}

	void update(double a_dt) {
		if(targetCreature && targetCreature->alive()){
			groundLocation = targetCreature->owner()->position();
		}
		auto nextMissileLocation = MV::moveToward(missile->position(), groundLocation, speed * static_cast<float>(a_dt));
		if (nextMissileLocation != groundLocation) {
			missile->position(nextMissileLocation);
		} else {
			if (onArrive) { onArrive(*this); }
			gameInstance.removeMissile(this);
			missile->removeFromParent();
		}
	}
private:
	std::shared_ptr<MV::Scene::Node> missile;
	std::shared_ptr<Creature> sourceCreature;
	std::shared_ptr<Creature> targetCreature;
	MV::Point<> groundLocation;
	float speed;

	std::function<void(Missile&)> onArrive;
	GameInstance &gameInstance;

	Creature::SharedRecieverType targetDeathWatcher;
	Creature::SharedRecieverType sourceDeathWatcher;
};

#endif
