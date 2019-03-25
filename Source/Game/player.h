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

struct LocalPlayer;
struct InGamePlayer;

//Only servers see this user data.
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
			volatility = MV::clamp(volatility + (streak * 2) - 3, 7.5, 40.0);
		}

		double getQ() const {
			return std::pow(10.0, rating / 400.0);
		}
	};

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(detailedRating),
			CEREAL_NVP(gamesChatRestricted),
			CEREAL_NVP(timesRestricted)
		);
	}

	Rating& queue(std::string a_id) {
		return detailedRating[a_id];
	}

	std::map<std::string, Rating> detailedRating;
	int gamesChatRestricted = 0;
	int timesRestricted = 0;

	//not serialized
	std::shared_ptr<LocalPlayer> client;
};

inline bool operator<(const ServerPlayer::Rating& left, const ServerPlayer::Rating& right) {
	return left.rating < right.rating;
}

struct PublicRating {
	int tier = 0;
	int points = 0;
	int series = 0;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(tier),
			CEREAL_NVP(points),
			CEREAL_NVP(series)
		);
	}
};

//no identifying characteristics. This is what's stored in the jsonb field in the players table.
struct IntermediateDbPlayer {
	std::string avatar;
	std::string flair;

	Wallet wallet;

	std::map<std::string, PublicRating> friendlyRating;

	std::map<std::string, LoadoutCollection> loadouts;
	std::string selectedLoadout;

	template <class Archive>
	void save(Archive & archive) const {
		archive(cereal::make_nvp("cereal_class_version", 0));
		archive(
			CEREAL_NVP(avatar),
			CEREAL_NVP(flair),
			CEREAL_NVP(wallet),
			CEREAL_NVP(friendlyRating),
			CEREAL_NVP(loadouts),
			CEREAL_NVP(selectedLoadout)
		);
	}

	template <class Archive>
	uint32_t load(Archive & archive) {
		uint32_t version = 0;
		archive(cereal::make_nvp("cereal_class_version", version));
		archive(
			CEREAL_NVP(avatar),
			CEREAL_NVP(flair),
			CEREAL_NVP(wallet),
			CEREAL_NVP(friendlyRating),
			CEREAL_NVP(loadouts),
			CEREAL_NVP(selectedLoadout)
		);
		return version; //so it can be used by LocalPlayer
	}
};


//The player who logs in to the Game gets this user data. The server also sees this.
struct LocalPlayer : public IntermediateDbPlayer {
	LocalPlayer() {}
	LocalPlayer(int64_t a_id, const std::string &a_email, const std::string &a_handle, const IntermediateDbPlayer &a_data) :
		id(a_id),
		email(a_email),
		handle(a_handle),
		IntermediateDbPlayer(a_data) {
	}

	int64_t id = -1;
	std::string email;
	std::string handle;

	template <class Archive>
	void save(Archive & archive) const {
		IntermediateDbPlayer::save(archive); //directly call this so it's inline.
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(email),
			CEREAL_NVP(handle)
		);
	}

	template <class Archive>
	void load(Archive & archive) {
		uint32_t version = IntermediateDbPlayer::load(archive); //directly call this so it's inline.
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(email),
			CEREAL_NVP(handle)
		);
	}
};

//All players in a GameInstance are represented by this user data.
struct InGamePlayer {
	static const int DEFAULT_GAME_CURRENCY = 10000;

	InGamePlayer() {}
	InGamePlayer(const LocalPlayer&a_rhs) :
		id(a_rhs.id),
		handle(a_rhs.handle),
		avatar(a_rhs.avatar),
		flair(a_rhs.flair){

		auto found = a_rhs.loadouts.find(a_rhs.selectedLoadout);
		if (found != a_rhs.loadouts.end()) {
			loadout = found->second;
		} else {
			MV::require<MV::LogicException>(!a_rhs.loadouts.empty(), "Loadout not found for player [", id, "] because loadouts was empty this may signal user data corruption!");
			MV::warning("Loadout not found for player [", id, "] and loadout id [", a_rhs.selectedLoadout, "] using fallback");
			loadout = a_rhs.loadouts.begin()->second;
		}

		gameWallet.add(Wallet::GAME, DEFAULT_GAME_CURRENCY);
	}
	InGamePlayer(const InGamePlayer &) = default;
	InGamePlayer(InGamePlayer &&) = default;

	int64_t id = -1;
	std::string handle;
	std::string avatar;
	std::string flair;

	Wallet gameWallet;

	LoadoutCollection loadout;

	TeamSide team = TeamSide::NEUTRAL;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(handle),
			CEREAL_NVP(avatar),
			CEREAL_NVP(flair),
			CEREAL_NVP(gameWallet),
			CEREAL_NVP(loadout),
			CEREAL_NVP(team)
		);
	}

	bool operator==(const InGamePlayer& a_rhs) const {
		return id == a_rhs.id;
	}

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
};

inline bool operator==(const InGamePlayer& a_lhs, const InGamePlayer& a_rhs) {
	return a_lhs.id == a_rhs.id;
}

inline bool operator==(const LocalPlayer& a_lhs, const InGamePlayer& a_rhs) {
	return a_lhs.id == a_rhs.id;
}

inline bool operator==(const InGamePlayer& a_lhs, const LocalPlayer& a_rhs) {
	return a_lhs.id == a_rhs.id;
}

inline bool operator==(const std::shared_ptr<InGamePlayer>& a_lhs, const std::shared_ptr<InGamePlayer>& a_rhs) {
	return a_lhs->id == a_rhs->id;
}

inline bool operator==(const std::shared_ptr<LocalPlayer>& a_lhs, const std::shared_ptr<InGamePlayer>& a_rhs) {
	return a_lhs->id == a_rhs->id;
}

inline bool operator==(const std::shared_ptr<InGamePlayer>& a_lhs, const std::shared_ptr<LocalPlayer>& a_rhs) {
	return a_lhs->id == a_rhs->id;
}

inline bool operator==(const std::shared_ptr<LocalPlayer>& a_lhs, const InGamePlayer& a_rhs) {
	return a_lhs->id == a_rhs.id;
}

inline bool operator==(const InGamePlayer& a_lhs, const std::shared_ptr<LocalPlayer>& a_rhs) {
	return a_lhs.id == a_rhs->id;
}

#endif
