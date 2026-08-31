#ifndef __MV_GAME_INSTANCE_H__
#define __MV_GAME_INSTANCE_H__

#include <memory>
#include <string>
#include "Game/player.h"
#include "MV/Network/networkObject.h"
#include "Game/NetworkLayer/synchronizeAction.h"

#include "Game/NetworkLayer/gameServer.h"

#include "Game/Instance/team.h"
#include <jaiscript/signals/signal.hpp>

class Missile;
class GameInstance {
	friend Team;
	friend MV::Script;

	GameInstance(const GameInstance &) = delete;
	GameInstance& operator=(const GameInstance &) = delete;
protected:
	virtual void initialize(const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer);
	GameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_gameData, MV::TapDevice& a_mouse, float a_timeStep, size_t a_maxFixedStepsPerUpdate);

	jai::signal_emitter<void(Team&)> onTeamDefeatedSignal;

public:
	static const int GameCameraId = 2;

	jai::signal<void(Team&)> onTeamDefeated;

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
	// the player behind this process (a client); servers have none
	virtual int64_t localPlayerId() const { return -1; }

	Team& teamForSide(TeamSide a_side) {
		return a_side == LEFT ? *left : *right;
	}

	BindstoneNetworkObjectPool& networkPool() {
		return synchronizedObjects;
	}

	virtual void spawnCreature(int /*a_buildingSlot*/, const std::string &/*a_creatureId*/) {
	}

	// returns the new creature's net id (0 when nothing spawned)
	virtual int64_t spawnCreatureAt(int /*a_buildingSlot*/, const std::string &/*a_creatureId*/, const MV::Point<> &/*a_worldPosition*/) {
		return 0;
	}

        void registerCreature(std::shared_ptr<Creature> a_registerCreature) {
                if (a_registerCreature->alive()) {
                        creatures[a_registerCreature->netId()] = a_registerCreature;
                        if (auto serverCreature = std::dynamic_pointer_cast<ServerCreature>(a_registerCreature)) {
                                teamForPlayer(serverCreature->player()).addMember(serverCreature);
                        }
                        a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
                                lastKnownCreaturePositions[a_creature->netId()] = a_creature->owner()->position();
                                teamForPlayer(a_creature->player()).removeMember(a_creature->netId());
                                creatures.erase(a_creature->netId());
                        });
                }
        }

	// Where a creature stood when it died, for effects that outlive their target
	bool lastKnownCreaturePosition(int64_t a_id, MV::Point<> &a_position) const {
		auto found = lastKnownCreaturePositions.find(a_id);
		if (found == lastKnownCreaturePositions.end()) { return false; }
		a_position = found->second;
		return true;
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

	// map.scene marks the view's limits with TopLeft_Hard/BottomRight_Hard and the _Soft pair: the
	// view may never show past the hard box; past the soft box it is allowed briefly, then eases
	// back. Zoom-out has no limit of its own - it stops where the view fits each box (the tighter
	// axis wins); zoom-in clamps at maxScaleHard and eases back to maxScaleSoft.
	MV::BoxAABB<> visibleMapBounds();
	bool mapViewBounds(MV::BoxAABB<>& a_bounds, bool a_soft);
	bool cornerBounds(MV::BoxAABB<>& a_bounds, const std::string& a_suffix);
	float scaleToFit(const MV::BoxAABB<>& a_bounds);
	MV::Point<> viewCorrection(const MV::BoxAABB<>& a_bounds);
	void clampToHardBounds(const MV::Point<int>& a_pointerCenter);
	void easeToBoundsIfExceeded(const MV::Point<int>& a_pointerCenter);

        std::vector<std::shared_ptr<Building>> buildings;
        std::map<int64_t, std::shared_ptr<Creature>> creatures;
        std::unordered_map<int64_t, MV::Point<>> lastKnownCreaturePositions;

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
	size_t maxFixedStepsPerUpdate = 1;

	bool hasActiveTouch = false;

	const MV::PointPrecision maxScaleHard = 3.5f;
	const MV::PointPrecision maxScaleSoft = 3.0f;

	const MV::PointPrecision viewSoftInset = .04f;   // PathMap fallback for a scene without corner markers
	const MV::PointPrecision scaleSanityFloor = .01f;   // keeps the transform invertible, not a design limit
	float minScale(bool a_soft);
	const double viewEaseSeconds = 0.25;
	const double viewEaseHoldSeconds = 0.1;

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
	int64_t localPlayerId() const override;

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
		spawnCreatureAt(a_buildingSlot, a_creatureId, building(a_buildingSlot)->spawnPositionWorld());
	}

	virtual int64_t spawnCreatureAt(int a_buildingSlot, const std::string& a_creatureId, const MV::Point<> &a_worldPosition) override {
		auto creatureNode = gameObjectContainer()->make(MV::guid(a_creatureId));
		creatureNode->worldPosition(a_worldPosition);
		return creatureNode->attach<ServerCreature>(a_creatureId, building(a_buildingSlot)->slotIndex(), *this)->netId();
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
