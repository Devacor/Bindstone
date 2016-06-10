#ifndef _LOBBYSERVER_MV_H_
#define _LOBBYSERVER_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "pqxx/pqxx"
#undef ERROR

class ServerAction;
class LobbyServer;

class LobbyConnectionState : public MV::ConnectionStateBase {
public:
	LobbyConnectionState(MV::Connection *a_connection, LobbyServer& a_server);

	virtual void connect() override;

	virtual void message(const std::string &a_message);

	LobbyServer& server() {
		return ourServer;
	}


private:
	LobbyServer& ourServer;
};

class LobbyServer {
public:
	LobbyServer(Managers &a_managers) :
		manager(a_managers),
		db(std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123")),
		emailPool(1), //need to test values greater than 1 to make sure ssh does not break.
		dbPool(1), //currently locked to 1 as pqxx requires one per thread. We can expand this later with more connections and a different query interface.
		ourServer( std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 22325),
			[this](MV::Connection *a_connection) {
				return std::make_unique<LobbyConnectionState>(a_connection, *this);
			})) {
	}

	void update(double dt) {
		ourServer->update(dt);
		threadPool.run();
		emailPool.run();
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
		return ourServer;
	}

	MV::ThreadPool& databasePool() {
		return dbPool;
	}

	void email(const MV::Email::Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
		auto emailer = MV::Email::make(emailPool.service(), "email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
		emailer->send(a_addresses, a_title, a_message);
	}

private:
	LobbyServer(const LobbyServer &) = delete;
	LobbyServer& operator=(const LobbyServer &) = delete;

	std::shared_ptr<pqxx::connection> db;
	std::shared_ptr<MV::Server> ourServer;

	MV::ThreadPool threadPool;
	MV::ThreadPool emailPool;
	MV::ThreadPool dbPool;

	Managers &manager;
	bool done;

	double lastUpdateDelta;
};

#endif
