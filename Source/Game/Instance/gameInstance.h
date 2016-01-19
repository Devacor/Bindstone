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

class Team {
	friend GameInstance;
public:
	Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game);

	MV::Point<> goal() const { return goalPosition; }

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Team>(), "Team");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return &a_self.game;
		}), "game");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.goalPosition;
		}), "goal");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.health;
		}), "health");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.buildings;
		}), "buildings");

		a_script.add(chaiscript::fun([](Team &a_self) {
			return a_self.creatures;
		}), "creatures");

		return a_script;
	}
private:
	std::vector<MV::Scene::SafeComponent<Building>> buildings;
	std::vector<MV::Scene::SafeComponent<Creature>> creatures;

	GameInstance& game;

	std::shared_ptr<Player> player;
	int health;
	TeamSide side;

	MV::Point<> goalPosition;
};

class GameInstance {
	friend Team;
public:
	GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, MV::MouseState& a_mouse, LocalData& a_data);

	void beginMapDrag();

	void nodeLoadBinder(cereal::JSONInputArchive &a_archive);

	bool update(double dt);

	bool handleEvent(const SDL_Event &a_event);

	LocalData& data() {
		return localData;
	}

	MV::MouseState& mouse() {
		return ourMouse;
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
private:
	void handleScroll(int a_amount);

	void hook();

	LocalData& localData;
	MV::MouseState &ourMouse;
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	chaiscript::ChaiScript scriptEngine;

	Team left;
	Team right;

	MV::Task cameraAction;
};

#endif
