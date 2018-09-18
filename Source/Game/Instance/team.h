#ifndef _MV_TEAM_H_
#define _MV_TEAM_H_

#include <string>
#include <memory>
#include "MV/Render/points.h"

enum TeamSide { LEFT, RIGHT };

inline TeamSide operator!(TeamSide a_side) { return (a_side == LEFT) ? RIGHT : LEFT; }

inline std::string sideToString(TeamSide a_side) { return (a_side == LEFT) ? "left" : "right"; }

struct Constants;
class GameInstance;
class Building;
class Creature;
class Game;
struct Player;

namespace chaiscript { class ChaiScript; }

class Team {
	friend GameInstance;
public:
	Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game);

	MV::Point<> ourWell() const { return ourWellPosition; }
	MV::Point<> enemyWell() const { return enemyWellPosition; }

	MV::Scale scale() const { return ourSide == TeamSide::LEFT ? MV::Scale(1, 1) : MV::Scale(-1, 1); }

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

	std::vector<std::shared_ptr<Creature>> creaturesInRange(const MV::Point<> &a_location, float a_radius);

	TeamSide side() const { return ourSide; }

private:
	GameInstance& game;

	std::shared_ptr<Player> player;
	int health;
	TeamSide ourSide;

	MV::Point<> ourWellPosition;
	MV::Point<> enemyWellPosition;
};

#endif
