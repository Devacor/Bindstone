#ifndef __MV_GAME_INSTANCE_H__
#define __MV_GAME_INSTANCE_H__

#include <memory>
#include <string>
#include "Game/player.h"

enum TeamSide { LEFT, RIGHT };

inline TeamSide operator!(TeamSide a_side) { return (a_side == LEFT) ? RIGHT : LEFT; }

inline std::string sideToString(TeamSide a_side) { return (a_side == LEFT) ? "left" : "right"; }


struct Constants;
class GameInstance;
class Building;
class Game;

class Team {
	friend GameInstance;
public:
	Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game);

	MV::Point<> ourWell() const { return ourWellPosition; }
	MV::Point<> enemyWell() const { return enemyWellPosition; }

	MV::Scale scale() const { return side == TeamSide::LEFT ? MV::Scale(1, 1) : MV::Scale(-1, 1); }

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Team>(), "Team");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return &a_self.game;
		}), "game");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.enemyWellPosition;
		}), "enemyWell");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.ourWellPosition;
		}), "ourWell");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.health;
		}), "health");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.buildings;
		}), "buildings");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.creatures;
		}), "creatures");

		a_script.add(chaiscript::fun([](Team &a_self, const MV::Point<> &a_location, float a_radius) {
			return a_self.creaturesInRange(a_location, a_radius);
		}), "creaturesInRange");

		a_script.add(chaiscript::fun([](Team &a_self) {
			std::vector<int> testVector = { 1, 2, 3, 4, 5 };
			return testVector;
		}), "vectorSizeTest");

		return a_script;
	}

	std::vector<std::shared_ptr<Creature>> creaturesInRange(const MV::Point<> &a_location, float a_radius);

	void spawn(std::shared_ptr<Creature> &a_registerCreature);
private:
	std::vector<std::shared_ptr<Building>> buildings;
	std::vector<std::shared_ptr<Creature>> creatures;

	GameInstance& game;

	std::shared_ptr<Player> player;
	int health;
	TeamSide side;

	MV::Point<> ourWellPosition;
	MV::Point<> enemyWellPosition;
};

class Missile;
class GameInstance {
	friend Team;
public:
	GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, Game& a_game);
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

	bool update(double dt);

	bool handleEvent(const SDL_Event &a_event);

	MV::MouseState& mouse() {
		return ourMouse;
	}

	std::shared_ptr<MV::Scene::Node> missileContainer() const {
		return pathMap->owner();
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

	Team& teamForPlayer(const std::shared_ptr<Player> &a_player) {
		return left.player == a_player ? left : right;
	}

	Team& teamAgainstPlayer(const std::shared_ptr<Player> &a_player) {
		return left.player == a_player ? right : left;
	}

	void moveCamera(MV::Point<> a_startPosition, MV::Scale a_scale);
	void moveCamera(std::shared_ptr<MV::Scene::Node> a_targetNode, MV::Scale a_scale);

	void spawnMissile(std::shared_ptr<Creature> a_source, std::shared_ptr<Creature> a_target, std::string a_prefab, float a_speed, std::function<void(Missile&)> a_onArrive);

	void removeMissile(Missile* a_toRemove);

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<Player> &a_player) const {
		return true;
	}
private:
	void removeExpiredMissiles();
	void handleScroll(int a_amount);

	void hook();

	GameData& gameData;
	Game& ourGame;

	MV::MouseState &ourMouse;
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	chaiscript::ChaiScript scriptEngine;

	std::vector<std::unique_ptr<Missile>> missiles;
	std::vector<Missile*> expiredMissiles;

	MV::MouseState::SignalType mouseSignal;

	Team left;
	Team right;

	MV::Task worldTimestep;
	MV::Task cameraAction;
};

class ClientGameInstance : public GameInstance {
public:
	ClientGameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, Game& a_game, const std::shared_ptr<Player> &a_localPlayer):
		GameInstance(a_leftPlayer, a_rightPlayer, a_game),
		localPlayer(a_localPlayer){
	}

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<Player> &a_player) const override {
		return a_player == localPlayer;
	}
private:
	std::shared_ptr<Player> localPlayer;
	std::shared_ptr<MV::Client> client;
};

class ServerGameInstance : public GameInstance {
public:
	ServerGameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, Game& a_game) :
		GameInstance(a_leftPlayer, a_rightPlayer, a_game) {
	}

private:
	std::shared_ptr<MV::Server> server;
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

		missile = gameInstance.missileContainer()->make("Assets/Prefabs/Missiles/" + a_prefab + ".prefab", gameInstance.jsonLoadBinder(), gameInstance.missileContainer()->getUniqueId("missile"));
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
