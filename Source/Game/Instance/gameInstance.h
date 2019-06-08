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
	GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep);

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

	std::shared_ptr<MV::Scene::Node> gameObjectContainer() const {
		auto result = pathMap->owner()->makeOrGet("Creatures");
		auto result2 = pathMap->owner()->makeOrGet("Objects");
		return result;
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

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &) const = 0;

	Team& teamForSide(TeamSide a_side) {
		return a_side == LEFT ? *left : *right;
	}

	BindstoneNetworkObjectPool& networkPool() {
		return synchronizedObjects;
	}

	virtual void spawnCreature(int /*a_buildingSlot*/) {
	}

	void registerCreature(std::shared_ptr<ServerCreature> &a_registerCreature) {
		if (a_registerCreature->alive()) {
			creatures[a_registerCreature->netId()] = a_registerCreature;
			a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
				creatures.erase(a_creature->netId());
			});
		}
	}

	const std::shared_ptr<ServerCreature> &creature(uint64_t a_id) {
		static std::shared_ptr<ServerCreature> nullCreature;
		if (a_id == 0) {
			return nullCreature;
		}
		auto found = creatures.find(a_id);
		if (found != creatures.end()) {
			return found->second;
		} else {
			return nullCreature;
		}
	}
protected:

	virtual void fixedUpdateImplementation(double /*a_dt*/) {}
	virtual void updateImplementation(double /*a_dt*/) {}

	void handleScroll(int a_amount);

	virtual void hook();

	std::vector<std::shared_ptr<Building>> buildings;
	std::map<uint64_t, std::shared_ptr<ServerCreature>> creatures;

	GameData& gameData;

	MV::TapDevice &ourMouse;
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	chaiscript::ChaiScript scriptEngine;

	MV::TapDevice::SignalType mouseSignal;

	std::unique_ptr<Team> left;
	std::unique_ptr<Team> right;

	MV::Task worldTimestep;
	MV::Task cameraAction;

	float timeStep = 0.0f;

	BindstoneNetworkObjectPool synchronizedObjects;
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

protected:
	void hook() override;

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
		auto creatureNode = gameObjectContainer()->make(MV::guid(spawner->currentCreature().id));
		creatureNode->worldPosition(spawner->spawnPositionWorld());

		creatureNode->attach<ServerCreature>(spawner->currentCreature().id, spawner->slotIndex(), *this);
	}

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &/*a_player*/) const override {
		return true;
	}

protected:
	virtual void updateImplementation(double dt) override;
	void hook() override;

private:
	GameServer &gameServer;
};

#endif
