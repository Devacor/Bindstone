#ifndef _GAMESERVERACTIONS_MV_H_
#define _GAMESERVERACTIONS_MV_H_

#include "Game/Instance/team.h"
#include "Game/NetworkLayer/networkAction.h"
#include "Utility/chaiscriptUtility.h"

class GameServerAvailable : public NetworkAction {
public:
	GameServerAvailable() {}
	GameServerAvailable(const std::string &a_url, uint16_t a_port) : ourUrl(a_url), ourPort(a_port) {}

#ifdef BINDSTONE_SERVER
	virtual void execute(LobbyGameConnectionState* a_connection) override;
#endif

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(ourUrl),
			CEREAL_NVP(ourPort),
			cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::string ourUrl;
	uint16_t ourPort;
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

#ifdef BINDSTONE_SERVER
	virtual void execute(GameServer& a_connection) override;
#endif

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

class GetInitialGameState : public NetworkAction {
public:
	GetInitialGameState() {}
	GetInitialGameState(int64_t a_secret) : secret(a_secret) {}

#ifdef BINDSTONE_SERVER
	virtual void execute(GameUserConnectionState*, GameServer &) override;
#endif

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(secret), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}
private:
	int64_t secret;
};

CEREAL_REGISTER_TYPE(GetInitialGameState);

class SuppliedInitialGameState : public NetworkAction {
public:
	SuppliedInitialGameState() {}
	SuppliedInitialGameState(const std::shared_ptr<Player> &a_left, const std::shared_ptr<Player> &a_right) : left(a_left), right(a_right) {}

	virtual void execute(Game& a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(left), CEREAL_NVP(right), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::shared_ptr<Player> left;
	std::shared_ptr<Player> right;
};

CEREAL_REGISTER_TYPE(SuppliedInitialGameState);

class RequestBuildingUpgrade : public NetworkAction {
public:
	RequestBuildingUpgrade() {}
	RequestBuildingUpgrade(TeamSide a_side, int32_t a_slot, int64_t a_id) : side(a_side), slot(a_slot), id(static_cast<int32_t>(a_id)) {}

#ifdef BINDSTONE_SERVER
	virtual void execute(GameUserConnectionState*a_gameUser, GameServer &a_game) override;
#endif
	virtual void execute(Game &a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(side), CEREAL_NVP(slot), CEREAL_NVP(id), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	TeamSide side;
	int32_t slot;
	int32_t id;
};

CEREAL_REGISTER_TYPE(RequestBuildingUpgrade);

class RequestFullGameState : public NetworkAction {
public:
	RequestFullGameState() {}
	RequestFullGameState(const std::shared_ptr<Player> &a_left, const std::shared_ptr<Player> &a_right) : left(a_left), right(a_right) {}

#ifdef BINDSTONE_SERVER
	virtual void execute(GameUserConnectionState*, GameServer &) override;
#endif
	virtual void execute(Game& a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(left), CEREAL_NVP(right), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::shared_ptr<Player> left;
	std::shared_ptr<Player> right;
};

CEREAL_REGISTER_TYPE(RequestFullGameState);

#endif
