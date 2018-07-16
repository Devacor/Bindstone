#ifndef __MV_PLAYER_H__
#define __MV_PLAYER_H__

#include <string>
#include <cmath>
#include <map>
#include "Game/wallet.h"
#include "cereal/cereal.hpp"
#include "Game/building.h"
#include "Game/state.h"

namespace MV { class TapDevice; }

struct LoadoutCollection {
	std::vector<std::string> buildings;
	std::vector<std::string> skins;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(buildings),
			CEREAL_NVP(skins)
		);
	}
};

struct Player;
struct ServerPlayer {
	struct Rating {
		enum GameResult { LOSS, DRAW, WIN };

		double rating = 1000;
		double volatility = 40;
		int games = 0;
		int streak = 0;
		GameResult lastResult = DRAW;

		void adjust(Rating& a_antagonist, GameResult a_result) {
			auto expectedP = expected(a_antagonist);
			auto expectedA = 1.0 - expectedP;
			update(expectedP, a_result);
			a_antagonist.update(expectedA, oppositeResult(a_result));
		}

		double expected(const Rating &a_enemy) const {
			auto myQ = getQ();
			return myQ / (myQ + a_enemy.getQ());
		}

		//0 is most fit, 1 is least fit
		double skillDifference(const Rating& a_enemy) const {
			auto percentDiff = (std::abs(.5 - expected(a_enemy)) * 2.0);
			auto volatilityDifference = std::abs(volatility - a_enemy.volatility);

			return MV::clamp(percentDiff + ((volatilityDifference / 30.0) / 10.0), 0.0, 1.0);
		}

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(
				CEREAL_NVP(rating),
				CEREAL_NVP(volatility),
				CEREAL_NVP(games),
				CEREAL_NVP(streak),
				CEREAL_NVP(lastResult)
			);
		}

	private:
		static float scoreForResult(GameResult a_result) {
			return static_cast<float>(a_result) / 2.0f;
		}

		static GameResult oppositeResult(GameResult a_result) {
			return a_result == WIN ? LOSS : a_result == LOSS ? WIN : DRAW;
		}

		void update(double a_expected, GameResult a_result) {
			games++;
			if (lastResult == a_result && a_result != DRAW) {
				streak++;
			} else {
				streak = 0;
			}
			lastResult = a_result;
			auto score = scoreForResult(a_result);
			rating += volatility * (score - a_expected);
			volatility = MV::clamp(volatility + (streak * 2) - 3, 10.0, 40.0);
		}

		double getQ() const {
			return std::pow(10.0, rating / 400.0);
		}
	};

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(queues),
			CEREAL_NVP(gamesChatRestricted),
			CEREAL_NVP(timesRestricted)
		);
	}

	Rating& queue(std::string a_id) {
		return queues[a_id];
	}

	std::map<std::string, Rating> queues;
	int gamesChatRestricted = 0;
	int timesRestricted = 0;

	//not serialized
	std::shared_ptr<Player> client;
};

inline bool operator<(const ServerPlayer::Rating& left, const ServerPlayer::Rating& right) {
	return left.rating < right.rating;
}

struct Player {
	std::string email;
	std::string handle;

	Wallet wallet;

	std::map<std::string, std::vector<std::string>> unlocked;
	LoadoutCollection loadout;

	int experience = 0;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(email),
			CEREAL_NVP(handle),
			CEREAL_NVP(experience),
			CEREAL_NVP(wallet),
			CEREAL_NVP(unlocked),
			CEREAL_NVP(loadout)
		);
	}

	bool operator==(const Player& a_rhs) const {
		return email == a_rhs.email;
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
};

CEREAL_CLASS_VERSION(Player, 1);

#endif
