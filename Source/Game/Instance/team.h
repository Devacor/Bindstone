#ifndef _MV_TEAM_H_
#define _MV_TEAM_H_

#include <string>
#include <memory>
#include "MV/Render/points.h"

enum TeamSide { NEUTRAL, LEFT, RIGHT };

inline TeamSide operator!(TeamSide a_side) { return (a_side == LEFT) ? RIGHT : (a_side == RIGHT) ? LEFT : NEUTRAL; }

inline std::string sideToString(TeamSide a_side) { return (a_side == LEFT) ? "left" : (a_side == RIGHT) ? "right" : "neutral"; }

struct Constants;
class GameInstance;
class Building;
class ServerCreature;
class Game;
struct InGamePlayer;
struct LocalPlayer;

namespace MV { class Script; }

class Team {
	friend GameInstance;
	friend MV::Script;
public:
	Team(std::shared_ptr<InGamePlayer> a_player, TeamSide a_side, GameInstance& a_game);
	void initialize();

	MV::Point<> ourWell() const { return ourWellPosition; }
	MV::Point<> enemyWell() const { return enemyWellPosition; }

	MV::Scale scale() const { return ourSide == TeamSide::LEFT ? MV::Scale(1, 1) : MV::Scale(-1, 1); }

	std::vector<std::shared_ptr<ServerCreature>> creaturesInRange(const MV::Point<> &a_location, float a_radius);
	// nearest live, targetable member within a_radius, or nullptr
	std::shared_ptr<ServerCreature> nearestCreature(const MV::Point<> &a_location, float a_radius);
	void addMember(const std::shared_ptr<ServerCreature> &a_creature) { members.push_back(a_creature); }
	void removeMember(int64_t a_netId);
	size_t memberCount() const { return members.size(); }

	TeamSide side() const { return ourSide; }

	int health() const { return ourHealth; }
	void damage(int a_amount);

private:
	GameInstance& game;

	std::shared_ptr<InGamePlayer> player;
	int ourHealth;
	TeamSide ourSide;

	MV::Point<> ourWellPosition;
	MV::Point<> enemyWellPosition;
	std::vector<std::shared_ptr<ServerCreature>> members;
};

#endif
