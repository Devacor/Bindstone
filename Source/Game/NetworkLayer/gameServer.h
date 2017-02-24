#ifndef _LOBBYSERVER_MV_H_
#define _LOBBYSERVER_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include "Game/player.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <tuple>

#include "pqxx/pqxx"

#include "Utility/cerealUtility.h"

#include <conio.h>

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
	GameServer(Managers &a_managers) :
		manager(a_managers),
		db(std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123")),
		emailPool(1), //need to test values greater than 1 to make sure ssh does not break.
		dbPool(1), //currently locked to 1 as pqxx requires one per thread. We can expand this later with more connections and a different query interface.
		ourUserServer(std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 22325),
			[this](const std::shared_ptr<MV::Connection> &a_connection) {
				return std::make_unique<GameUserConnectionState>(a_connection, *this);
			})) {
	}

	void update(double dt) {
		ourUserServer->update(dt);
		threadPool.run();
		emailPool.run();
		dbPool.run();

		if (_kbhit()) {
			switch (_getch()) {
			case 'c':
				std::cout << "Connections: " << ourUserServer->connections().size() << std::endl;
				break;
			}
		}
	}

	Managers& managers() {
		return manager;
	}

	std::shared_ptr<pqxx::connection> database() {
		return db;
	}

	MV::ThreadPool& pool() {
		return threadPool;
	}

	std::shared_ptr<MV::Server> server() {
		return ourUserServer;
	}

	MV::ThreadPool& databasePool() {
		return dbPool;
	}

private:
	GameServer(const GameServer &) = delete;
	GameServer& operator=(const GameServer &) = delete;

	std::shared_ptr<pqxx::connection> db;
	std::shared_ptr<MV::Server> ourUserServer;

	MV::ThreadPool threadPool;
	MV::ThreadPool emailPool;
	MV::ThreadPool dbPool;

	Managers &manager;
	bool done;

	double lastUpdateDelta;
};

#endif
