#ifndef _GAMESERVER_MV_H_
#define _GAMESERVER_MV_H_
#ifdef BINDSTONE_SERVER
#include "Game/Instance/gameInstance.h"

#include "MV/Utility/package.h"
#include "MV/Network/package.h"
#include "Game/managers.h"

#include "Game/player.h"

#include "Game/NetworkLayer/gameServerActions.h"
#include "MV/Utility/stringUtility.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <tuple>

#include "pqxx/pqxx"

#include "MV/Utility/cerealUtility.h"

#include <optional>

class ServerGameAction;
class GameServer;
class ServerGameInstance;
class ClientGameInstance;

class GameUserConnectionState : public MV::ConnectionStateBase {
public:
	GameUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, GameServer& a_server);

	virtual void message(const std::string &a_message);

	GameServer& server() {
		return ourServer;
	}

	void authenticate(const std::shared_ptr<InGamePlayer> &a_player, int64_t a_secret) {
		ourPlayer = a_player;
		ourSecret = a_secret;
	}

	std::shared_ptr<InGamePlayer> player() const {
		return ourPlayer;
	}

protected:
	virtual void connectImplementation() override;
	virtual void disconnectImplementation() override;

private:
	std::string queueType;
	GameServer& ourServer;
	int64_t ourSecret = 0;
	std::shared_ptr<InGamePlayer> ourPlayer;
};

class GameServer {
public:
	GameServer(Managers &a_managers, unsigned short a_port = 0);

	void update(double dt);

	Managers& managers() {
		return manager;
	}

	MV::ThreadPool& pool() {
		return threadPool;
	}

	std::shared_ptr<MV::Server> server() {
		return ourUserServer;
	}

	void assign(const AssignedPlayer &a_left, const AssignedPlayer &a_right, const std::string &a_queueId);

	std::shared_ptr<MV::Client> lobby() {
		return ourLobbyClient;
	}

	std::shared_ptr<MV::Scene::Node> root() {
		return rootScene;
	}

	GameData& data() {
		return gameData;
	}
	
	MV::TapDevice& mouse() {
		return nullMouse;
	}

	ServerGameInstance* instance() {
		return ourInstance.get();
	}

	std::shared_ptr<InGamePlayer> userConnected(int64_t a_secret) {
		if (left->secret == a_secret) {
			return left->player;
		} else if (right->secret == a_secret) {
			return right->player;
		}
		return std::shared_ptr<InGamePlayer>();
	}

	bool allUsersConnected() {
		for (auto&& connection : ourUserServer->connections()) {
			GameUserConnectionState* state = static_cast<GameUserConnectionState*>(connection->state());
			if (!state->player()) {
				return false;
			}
		}
		return true;
	}

	void userDisconnected(int64_t a_secret) {
		if (left->secret == a_secret) {
			
		} else if (right->secret == a_secret) {

		}
		for (auto&& connection : ourUserServer->connections()) {
			if (!connection->disconnected()) {
				connection->disconnect();
			}
		}
		makeUsAvailableToTheLobby();
	}

	std::shared_ptr<InGamePlayer> leftPlayer() const {
		return left->player;
	}

	std::shared_ptr<InGamePlayer> rightPlayer() const {
		return right->player;
	}

private:
	GameServer(const GameServer &) = delete;

	void handleInput();

	void initializeClientToLobbyServer() {
		auto localHostServerAddress = MV::explode(MV::fileContents("ServerConfig/lobbyServerAddress.config"), [](char c) {return c == '\n'; })[0];
		ourLobbyClient = MV::Client::make(MV::Url{ localHostServerAddress }, [=](const std::string &a_message) {
			auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
			try {
				value->execute(*this);
			} catch (std::exception &e) {
				MV::error("Failed to execute NetworkAction: ", e.what());
				ourLobbyClient->send(makeNetworkString<GameServerStateChange>(GameServerStateChange::AVAILABLE));
			}
		}, [&](const std::string &a_dcreason) {
			MV::info("Disconnected [", localHostServerAddress, "]: ", a_dcreason);
			ourLobbyClient = nullptr;
		}, [=] {
			makeUsAvailableToTheLobby();
		});
	}

	void makeUsAvailableToTheLobby() {
		if (!ourLobbyClient) { return; }
		auto gameServerAddressNoPort = MV::explode(MV::fileContents("ServerConfig/gameServerAddress.config"), [](char c) {return c == '\n'; })[0];
		auto found = gameServerAddressNoPort.rfind(':');
		if (found != std::string::npos && found < (gameServerAddressNoPort.length() - 1) && gameServerAddressNoPort[found + 1] != '/') {
			gameServerAddressNoPort = gameServerAddressNoPort.substr(0, found);
		}
		ourLobbyClient->send(makeNetworkString<GameServerAvailable>(gameServerAddressNoPort, ourUserServer->port()));
	}

	GameServer& operator=(const GameServer &) = delete;

	std::shared_ptr<MV::Server> ourUserServer;

	std::shared_ptr<MV::Client> ourLobbyClient;

	std::shared_ptr<MV::Scene::Node> rootScene;

	GameData gameData;

	std::unique_ptr<ServerGameInstance> ourInstance;

	MV::TapDevice nullMouse;

	MV::ThreadPool threadPool;

	Managers &manager;
	bool done;

	std::optional<AssignedPlayer> left;
	std::optional<AssignedPlayer> right;
	std::string queueId;

	MV::Task rootTask;
};
#endif
#endif
