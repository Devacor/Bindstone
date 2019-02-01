#ifndef _GAMESERVERACTIONS_MV_H_
#define _GAMESERVERACTIONS_MV_H_

#include "Game/Instance/team.h"
#include "Game/NetworkLayer/networkAction.h"
#include "MV/Utility/chaiscriptUtility.h"

class GameServerAvailable : public NetworkAction {
public:
	GameServerAvailable() {}
	GameServerAvailable(const std::string &a_url, uint16_t a_port) : ourUrl(a_url), ourPort(a_port) {}

	virtual void execute(LobbyGameConnectionState* a_connection) override;

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

class GameServerStateChange : public NetworkAction {
public:
	enum State { 
		OCCUPIED,
		AVAILABLE
	};
	GameServerStateChange() {}
	GameServerStateChange(State a_state) : ourState(a_state) {}

	virtual void execute(LobbyGameConnectionState* a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			CEREAL_NVP(ourState),
			cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	State ourState;
};

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

class GetInitialGameState : public NetworkAction {
public:
	GetInitialGameState() {}
	GetInitialGameState(int64_t a_secret) : secret(a_secret) {}

	virtual void execute(GameUserConnectionState*, GameServer &) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(secret), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}
private:
	int64_t secret;
};

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

class RequestBuildingUpgrade : public NetworkAction {
public:
	RequestBuildingUpgrade() {}
	RequestBuildingUpgrade(int32_t a_slot, int64_t a_id) : slot(a_slot), id(static_cast<int32_t>(a_id)) {}

	virtual void execute(GameUserConnectionState*a_gameUser, GameServer &a_game) override;
	virtual void execute(Game &a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(slot), CEREAL_NVP(id), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	int32_t slot;
	int32_t id;
};

class RequestFullGameState : public NetworkAction {
public:
	RequestFullGameState() {}
	RequestFullGameState(const std::shared_ptr<Player> &a_left, const std::shared_ptr<Player> &a_right) : left(a_left), right(a_right) {}

	virtual void execute(GameUserConnectionState*, GameServer &) override;
	virtual void execute(Game& a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(left), CEREAL_NVP(right), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::shared_ptr<Player> left;
	std::shared_ptr<Player> right;
};

CEREAL_FORCE_DYNAMIC_INIT(mv_gameserveractions);

#endif
