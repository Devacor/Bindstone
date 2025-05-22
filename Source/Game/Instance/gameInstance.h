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
	friend MV::Script;

	GameInstance(const GameInstance &) = delete;
	GameInstance& operator=(const GameInstance &) = delete;
protected:
	virtual void initialize(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer);
	GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep);

public:
	static const int GameCameraId = 1;

	~GameInstance();

	GameData& data() {
		return gameData;
	}
	
	MV::Services& services() {
		return gameData.managers().services;
	}

	void beginMapDrag();

	void fixedUpdate(double dt);
	bool update(double dt);

	virtual void requestUpgrade(int a_slot, int a_upgrade) {
		std::cout << "Building Upgrade Request: " << a_slot << ", " << a_upgrade << std::endl;
	}

	virtual void performUpgrade(int a_slot, int a_upgrade) {
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
		return pathMap->owner();
	}

	std::shared_ptr<MV::Scene::Node> scene() const {
		return worldScene;
	}
	
	MV::Scene::SafeComponent<MV::Scene::PathMap> path() const {
		return pathMap;
	}

	MV::Script& script() {
		return scriptEngine;
	}

	Team& teamForPlayer(const std::shared_ptr<InGamePlayer> &a_player) {
		return left->player == a_player ? *left : *right;
	}

	Team& teamAgainstPlayer(const std::shared_ptr<InGamePlayer> &a_player) {
		return left->player == a_player ? *right : *left;
	}

	void moveCamera(MV::Point<> a_startPosition, MV::Scale a_scale, bool interruptable = false);
	void moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::Scale a_scale, bool interruptable = false);

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &) const = 0;

	Team& teamForSide(TeamSide a_side) {
		return a_side == LEFT ? *left : *right;
	}

	BindstoneNetworkObjectPool& networkPool() {
		return synchronizedObjects;
	}

	virtual void spawnCreature(int /*a_buildingSlot*/, const std::string &/*a_creatureId*/) {
	}

        void registerCreature(std::shared_ptr<Creature> a_registerCreature) {
                if (a_registerCreature->alive()) {
                        creatures[a_registerCreature->netId()] = a_registerCreature;
                        a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
                                creatures.erase(a_creature->netId());
                        });
                }
        }

        const std::shared_ptr<Creature> &creature(int64_t a_id) {
                static std::shared_ptr<Creature> nullCreature;
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

	virtual void initializeBuilding(std::shared_ptr<Building> a_building) {
		buildings.push_back(a_building);
	}

protected:

	virtual void fixedUpdateImplementation(double /*a_dt*/) {}
	virtual void updateImplementation(double /*a_dt*/) {}

	bool cameraIsFree() const;
	bool requestCamera(); //returns the result of cameraIsFree and cancels any interruptable camera action.

	void handleScroll(float a_amount, const MV::Point<int> &a_position);
	void rawScaleAroundCenter(float a_amount);
	void rawScaleAroundScreenPoint(float a_amount, const MV::Point<int>& a_position);
	void easeToBoundsIfExceeded(const MV::Point<int>& a_pointerCenter);

        std::vector<std::shared_ptr<Building>> buildings;
        std::map<int64_t, std::shared_ptr<Creature>> creatures;

	GameData& gameData;

	MV::TapDevice &ourMouse;
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	MV::Script scriptEngine;

	MV::TapDevice::SignalType activeDrag;

	MV::TapDevice::TouchSignalType zoomSignal;

	std::unique_ptr<Team> left;
	std::unique_ptr<Team> right;

	MV::Task worldTimestep;
	MV::Task cameraAction;

	float timeStep = 0.0f;

	bool hasActiveTouch = false;

	const MV::PointPrecision maxScaleHard = 3.5f;
	const MV::PointPrecision maxScaleSoft = 3.0f;

	const MV::PointPrecision minScaleHard = .625f;
	const MV::PointPrecision minScaleSoft = .675f;

	BindstoneNetworkObjectPool synchronizedObjects;
};

class ClientGameInstance : public GameInstance {
	friend MV::Script;
	ClientGameInstance(Game& a_game);
public:
	static std::unique_ptr<ClientGameInstance> make(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer, const std::vector<BindstoneNetworkObjectPool::VariantType>& a_poolObjects, Game& a_game) {
		auto result = std::unique_ptr<ClientGameInstance>(new ClientGameInstance(a_game));
		result->initialize(a_leftPlayer, a_rightPlayer);
		result->networkPool().synchronize(a_poolObjects);
		return result;
	}

	void requestUpgrade(int a_slot, int a_upgrade) override;

	void performUpgrade(int a_slot, int a_upgrade) override;

	bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &a_player) const override;

private:
	Game &game;
};

#ifdef BINDSTONE_SERVER
class GameServer;
class ServerGameInstance : public GameInstance {
	friend MV::Script;
	ServerGameInstance(GameServer& a_game);
public:
	static std::unique_ptr<ServerGameInstance> make(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer, GameServer& a_game) {
		auto result = std::unique_ptr<ServerGameInstance>(new ServerGameInstance(a_game));
		result->initialize(a_leftPlayer, a_rightPlayer);
		return result;
	}

	virtual void initializeBuilding(std::shared_ptr<Building> a_building) override {
		a_building->initializeNetworkState(networkPool().spawn(std::make_shared<BuildingNetworkState>(static_cast<int32_t>(buildings.size()))));
		buildings.push_back(a_building);
	}

	virtual void spawnCreature(int a_buildingSlot, const std::string& a_creatureId) override {
		auto spawner = building(a_buildingSlot);
		auto creatureNode = gameObjectContainer()->make(MV::guid(a_creatureId));
		creatureNode->worldPosition(spawner->spawnPositionWorld());

		creatureNode->attach<ServerCreature>(a_creatureId, spawner->slotIndex(), *this);
	}

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &/*a_player*/) const override {
		return true;
	}

	void requestUpgrade(int a_slot, int a_upgrade) override {
		performUpgrade(a_slot, a_upgrade);
	}

protected:
	void updateImplementation(double dt) override;

private:
	GameServer &gameServer;
};
#endif

#endif
