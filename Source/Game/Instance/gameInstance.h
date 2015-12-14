#ifndef __MV_GAME_INSTANCE_H__
#define __MV_GAME_INSTANCE_H__

#include <memory>
#include <string>
#include "Game/player.h"

enum TeamSide { LEFT, RIGHT };

inline std::string sideToString(TeamSide a_side) { return (a_side == LEFT) ? "left" : "right"; }

struct Constants;
class GameInstance;
class Building;

class Team {
public:
	Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game);

private:
	std::vector<MV::Scene::SafeComponent<Building>> buildings;
	std::vector<MV::Scene::SafeComponent<Creature>> creatures;

	GameInstance& game;

	std::shared_ptr<Player> player;
	int health;
	TeamSide side;
};

class GameInstance {
	friend Team;
public:
	GameInstance(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, MV::MouseState& a_mouse, LocalData& a_data);

	void nodeLoadBinder(cereal::JSONInputArchive &a_archive);

	bool update(double dt);

	bool handleEvent(const SDL_Event &a_event);

	LocalData& data() {
		return localData;
	}

	MV::MouseState& mouse() {
		return ourMouse;
	}
private:
	void handleScroll(int a_amount);

	void hook();

	LocalData& localData;
	MV::MouseState &ourMouse;
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> buildingDialog;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	chaiscript::ChaiScript script;

	Team left;
	Team right;
};

#endif
