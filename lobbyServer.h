#ifndef _LOBBYSERVER_MV_H_
#define _LOBBYSERVER_MV_H_

#include <SDL.h>

#include "Utility/generalUtility.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "chaiscript/chaiscript.hpp"

#include "pqxx/pqxx"

void CreatePlayer() {

}

class ServerAction {
public:
	virtual void execute() {
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {}
};

class LoginAction {
public:
	virtual void execute() {
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {}
};

class LobbyConnectionState : public MV::ConnectionStateBase {
public:
	LobbyConnectionState(MV::Connection *a_connection) : 
		MV::ConnectionStateBase(a_connection) {
	}

	virtual void message(const std::string &a_message) {
		
	}
};

class LobbyServer {
public:
	LobbyServer(Managers &a_managers) :
		db(std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123")),
		server( std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 22325), 
			[](MV::Connection *a_connection) {
				return std::make_unique<LobbyConnectionState>(a_connection);
			})) {
		
	}

	//return true if we're still good to go
	bool update(double dt) {
		server->update(dt);
	}

	Managers& managers() {
		return manager;
	}

private:
	LobbyServer(const LobbyServer &) = delete;
	LobbyServer& operator=(const LobbyServer &) = delete;

	Managers &managers;

	std::shared_ptr<pqxx::connection> db;
	std::shared_ptr<MV::Server> server;

	Managers manager;
	bool done;

	double lastUpdateDelta;
};

#endif
