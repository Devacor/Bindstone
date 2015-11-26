#ifndef __MV_PLAYER_H__
#define __MV_PLAYER_H__

#include "Utility/package.h"
#include <string>
#include "Game/wallet.h"
#include "cereal/cereal.hpp"
#include "Game/building.h"

namespace chaiscript { class ChaiScript; }

struct LoadoutCollection {
	std::vector<std::string> buildings;
	std::vector<std::string> skins;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(buildings),
			CEREAL_NVP(skins)
		);
	}
};

struct Player {
	std::string name;
	std::string email;
	std::string passwordHash;

	Wallet wallet;

	std::map<std::string, std::vector<std::string>> unlocked;
	LoadoutCollection loadout;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(name),
			CEREAL_NVP(email),
			CEREAL_NVP(passwordHash),
			CEREAL_NVP(wallet),
			CEREAL_NVP(unlocked),
			CEREAL_NVP(loadout)
		);
	}
};

enum TeamSide {LEFT, RIGHT};

inline std::string sideToString(TeamSide a_side) { return (a_side == LEFT) ? "left" : "right"; }

struct Constants;
class Team {
public:
	Team(const std::shared_ptr<Player> &a_player, LocalData& a_data, std::shared_ptr<MV::Scene::Node> a_gameBoard, TeamSide a_side);

private:
	std::shared_ptr<MV::Scene::Node> gameBoard;
	std::vector<MV::Scene::SafeComponent<Building>> buildings;
	std::vector<MV::Scene::SafeComponent<Creature>> creatures;

	LocalData& data;
	MouseState& mouse;

	std::shared_ptr<Player> player;
	int health;
	TeamSide side;
};
#endif
