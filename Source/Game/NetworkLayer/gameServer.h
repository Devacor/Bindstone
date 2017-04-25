#ifndef _GAMESERVER_MV_H_
#define _GAMESERVER_MV_H_

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

class GameUserConnectionState : public MV::ConnectionStateBase {
public:
	enum State {WAITING, OCCUPIED};

	GameUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, GameServer& a_server);

	virtual void message(const std::string &a_message);

	GameServer& server() {
		return ourServer;
	}

	State state() const {
		return activeState;
	}

	void state(const State a_newState) {
		activeState = a_newState;
	}

protected:
	virtual void connectImplementation() override;

private:
	std::string queueType; 
	State activeState;
	GameServer& ourServer;
};

class GameServer {
public:
	GameServer(Managers &a_managers, unsigned short a_port) :
		manager(a_managers),
		port(a_port),
		gameData(a_managers),
		ourUserServer(std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), a_port),
			[this](const std::shared_ptr<MV::Connection> &a_connection) {
				return std::make_unique<GameUserConnectionState>(a_connection, *this);
			})) {

		InitializeClientToLobbyServer();
	}

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
	}

	std::shared_ptr<MV::Client> lobby() {
		return ourLobbyClient;
	}

	std::shared_ptr<MV::Scene::Node> root() {
		return gameScene;
	}

	GameData& data() {
		return gameData;
	}
	
	MV::MouseState& mouse() {
		return nullMouse;
	}

private:
	GameServer(const GameServer &) = delete;

	void InitializeClientToLobbyServer() {
		ourLobbyClient = MV::Client::make(MV::Url{ "http://localhost:22326" /*"http://54.218.22.3:22325"*/ }, [=](const std::string &a_message) {
			auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
			value->execute(*this);
		}, [&](const std::string &a_dcreason) {
			std::cout << "Disconnected: " << a_dcreason << std::endl;
		}, [=] {
			ourLobbyClient->send(makeNetworkString<GameServerAvailable>("http://localhost", port));
		});
	}

	GameServer& operator=(const GameServer &) = delete;

	std::shared_ptr<MV::Server> ourUserServer;

	std::shared_ptr<MV::Client> ourLobbyClient;

	std::shared_ptr<MV::Scene::Node> gameScene;

	GameData gameData;

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
