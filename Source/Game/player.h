#ifndef __MV_PLAYER_H__
#define __MV_PLAYER_H__

#include "Utility/package.h"
#include <string>
#include "Game/wallet.h"
#include "cereal/cereal.hpp"
#include "Game/building.h"
#include "Game/state.h"

namespace chaiscript { class ChaiScript; }
namespace MV { class MouseState; }

struct LoadoutCollection {
	std::vector<std::string> buildings;
	std::vector<std::string> skins;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(buildings),
			CEREAL_NVP(skins)
		);
	}
};

struct Player {
	std::string id;
	std::string name;
	std::string email;
	std::string passwordHash;

	Wallet wallet;

	std::map<std::string, std::vector<std::string>> unlocked;
	LoadoutCollection loadout;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(
			CEREAL_NVP(name),
			CEREAL_NVP(email),
			CEREAL_NVP(passwordHash),
			CEREAL_NVP(wallet),
			CEREAL_NVP(unlocked),
			CEREAL_NVP(loadout)
		);
	}

	bool operator==(const Player& a_rhs) const {
		return id == a_rhs.id;
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
};

#endif
