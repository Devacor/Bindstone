#ifndef _GAMESERVERACTIONS_MV_H_
#define _GAMESERVERACTIONS_MV_H_

#include "Game/NetworkLayer/networkAction.h"
#include "Utility/chaiscriptUtility.h"

class GameServerAvailable : public NetworkAction {
public:
	GameServerAvailable() {}
	GameServerAvailable(const std::string &a_url, uint32_t a_port) : ourUrl(a_url), ourPort(a_port) {}
	
	virtual void execute(LobbyGameConnectionState* a_connection) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(ourUrl),
			CEREAL_NVP(ourPort),
			cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::string ourUrl;
	uint32_t ourPort;
};

CEREAL_REGISTER_TYPE(GameServerAvailable);

struct AssignedPlayer {
	AssignedPlayer() {}
	AssignedPlayer(const AssignedPlayer &a_rhs) = default;
	AssignedPlayer(const std::shared_ptr<Player> &a_player, int64_t a_secret) : player(a_player), secret(a_secret) {}

	AssignedPlayer& operator=(const AssignedPlayer &a_rhs) = default;

	std::shared_ptr<Player> player;
	int64_t secret;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(player),
			CEREAL_NVP(secret));
	}
};

class AssignPlayersToGame : public NetworkAction {
public:
	AssignPlayersToGame() {}
	AssignPlayersToGame(const AssignedPlayer &a_left, const AssignedPlayer &a_right, const std::string &a_matchQueueId) : left(a_left), right(a_right), matchQueueId(a_matchQueueId) {}

	virtual void execute(GameServer& a_connection) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(left),
			CEREAL_NVP(right),
			cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	AssignedPlayer left;
	AssignedPlayer right;
	std::string matchQueueId;
};

CEREAL_REGISTER_TYPE(AssignPlayersToGame);

class GetFullGameState : public NetworkAction {
public:
	GetFullGameState() {}

	virtual void execute(GameUserConnectionState*, GameServer &) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}
};

CEREAL_REGISTER_TYPE(GetFullGameState);

class SuppliedFullGameState : public NetworkAction {
public:
	SuppliedFullGameState() {}
	SuppliedFullGameState(const std::shared_ptr<Player> &a_left, const std::shared_ptr<Player> &a_right) : left(a_left), right(a_right) {}

	virtual void execute(Game& a_connection) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		CEREAL_NVP(left),
		CEREAL_NVP(right),
		archive(cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::shared_ptr<Player> left;
	std::shared_ptr<Player> right;
};

CEREAL_REGISTER_TYPE(SuppliedFullGameState);

#endif
