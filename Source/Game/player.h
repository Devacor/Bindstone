#ifndef __MV_PLAYER_H__
#define __MV_PLAYER_H__

#include "Utility/package.h"
#include <string>
#include "Game/wallet.h"
#include "cereal/cereal.hpp"

namespace chaiscript { class ChaiScript; }

struct AssetCollection {
	std::map<std::string, std::string> buildings;
	std::map<std::string, int> gems;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(buildings),
			CEREAL_NVP(gems)
		);
	}
};

class Player {
	std::string name;
	std::string email;
	std::string passwordHash;

	Wallet wallet;

	AssetCollection unlocked;
	AssetCollection loadout;

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

#endif
