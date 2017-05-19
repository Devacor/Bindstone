#ifndef _GAMESERVER_MV_H_
#define _GAMESERVER_MV_H_

#include "Game/Instance/gameInstance.h"

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include "Game/player.h"

#include "Game/NetworkLayer/gameServerActions.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <tuple>

#include "pqxx/pqxx"

#include "Utility/cerealUtility.h"

#include <conio.h>
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

	void authenticate(const std::shared_ptr<Player> &a_player, int64_t a_secret) {
		ourPlayer = a_player;
		ourSecret = a_secret;
	}

	std::shared_ptr<Player> player() const {
		return ourPlayer;
	}

protected:
	virtual void connectImplementation() override;

private:
	std::string queueType;
	GameServer& ourServer;
	int64_t ourSecret = 0;
	std::shared_ptr<Player> ourPlayer;
};

class GameServer {
public:
	GameServer(Managers &a_managers, unsigned short a_port);

	void update(double dt) {
		ourUserServer->update(dt);
		ourLobbyClient->update();
		threadPool.run();

		if (_kbhit()) {
			switch (_getch()) {
			case 's':
				std::cout << "State query not really sure?" << std::endl;
				break;
			case 'c':
				std::cout << "Connections: " << ourUserServer->connections().size() << std::endl;
				break;
			}
		}
	}

	Managers& managers() {
		return manager;
	}

	MV::ThreadPool& pool() {
		return threadPool;
	}

	std::shared_ptr<MV::Server> server() {
		return ourUserServer;
	}

	void assign(const AssignedPlayer &a_left, const AssignedPlayer &a_right, const std::string &a_queueId) {
		left = a_left;
		right = a_right;
		queueId = a_queueId;
		ourInstance = std::make_unique<ServerGameInstance>(left->player, right->player, *this);
	}

	std::shared_ptr<MV::Client> lobby() {
		return ourLobbyClient;
	}

	std::shared_ptr<MV::Scene::Node> root() {
		return rootScene;
	}

	GameData& data() {
		return gameData;
	}
	
	MV::MouseState& mouse() {
		return nullMouse;
	}

	ServerGameInstance* instance() {
		return ourInstance.get();
	}

	std::shared_ptr<Player> userConnected(int64_t a_secret) {
		if (left->secret == a_secret) {
			return left->player;
		} else if (right->secret == a_secret) {
			return right->player;
		}
		return std::shared_ptr<Player>();
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

	std::shared_ptr<Player> leftPlayer() const {
		return left->player;
	}

	std::shared_ptr<Player> rightPlayer() const {
		return right->player;
	}

private:
	GameServer(const GameServer &) = delete;

	void initializeClientToLobbyServer() {
		ourLobbyClient = MV::Client::make(MV::Url{ "http://192.168.1.237:22326" /*"http://54.218.22.3:22325"*/ }, [=](const std::string &a_message) {
			auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
			value->execute(*this);
		}, [&](const std::string &a_dcreason) {
			std::cout << "Disconnected: " << a_dcreason << std::endl;
		}, [=] {
			ourLobbyClient->send(makeNetworkString<GameServerAvailable>("http://192.168.1.237", port));
		});
	}

	GameServer& operator=(const GameServer &) = delete;

	std::shared_ptr<MV::Server> ourUserServer;

	std::shared_ptr<MV::Client> ourLobbyClient;

	std::shared_ptr<MV::Scene::Node> rootScene;

	GameData gameData;

	std::unique_ptr<ServerGameInstance> ourInstance;

	MV::MouseState nullMouse;

	MV::ThreadPool threadPool;

	Managers &manager;
	bool done;

	double lastUpdateDelta;

	std::optional<AssignedPlayer> left;
	std::optional<AssignedPlayer> right;
	std::string queueId;
	
	uint32_t port;
};

#endif
