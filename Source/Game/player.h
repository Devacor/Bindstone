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

class Player {
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

class PlayerInGame {
	std::shared_ptr<Player> player;
	std::vector<std::shared_ptr<Building>> buildings;

};

struct Constants;
class Team {
public:
	Team(const std::shared_ptr<Player> &a_player, const Constants& a_constants);


private:
	std::vector<std::shared_ptr<Building>> buildings;
	int health;
	std::vector<std::shared_ptr<Creature>> creatures;
	std::vector<Gem> gems;

	std::shared_ptr<Player> player;
};

#endif
