#ifndef _LOBBYSERVER_MV_H_
#define _LOBBYSERVER_MV_H_

#include <SDL.h>

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "chaiscript/chaiscript.hpp"

#include "pqxx/pqxx"

class LobbyConnectionState;

class ClientResponse {
public:
	virtual void execute() {}

	template <class Archive>
	void serialize(Archive & /*archive*/, std::uint32_t const /*version*/) { }
};

class MessageResponse : public ClientResponse {
public:
	MessageResponse(const std::string& a_message) : message(a_message) {}
	MessageResponse(){}

	virtual void execute() override {
		std::cout << "Message Got: " << message << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) { 
		archive(CEREAL_NVP(message));
	}
private:
	std::string message;
};

class ServerAction {
public:
	virtual void execute(LobbyConnectionState&) {
	}

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & /*archive*/, std::uint32_t const /*version*/) { }
};

class LobbyServer;

struct ServerDetails {
	int forceClientVersion = 1;
	std::map<std::string, std::string> configurationHashes;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(forceClientVersion), CEREAL_NVP(configurationHashes));
	}
};

class LobbyConnectionState : public MV::ConnectionStateBase {
public:
	LobbyConnectionState(MV::Connection *a_connection, LobbyServer& a_server) : 
		MV::ConnectionStateBase(a_connection),
		ourServer(a_server) {
		auto test = ServerDetails{};
		connection()->send(MV::toBinaryString(test));
	}

	virtual void message(const std::string &a_message) {
		auto action = MV::fromBinaryString<std::shared_ptr<ServerAction>>(a_message);
		action->execute(*this);
	}

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
		emailPool(1), //need to test values greater than 1 to make sure ssh does not break.
		db(std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123")),
		ourServer( std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 22325),
			[this](MV::Connection *a_connection) {
				return std::make_unique<LobbyConnectionState>(a_connection, *this);
			})) {
	}

	void update(double dt) {
		ourServer->update(dt);
	}

	Managers& managers() {
		return manager;
	}

	pqxx::connection& database() {
		return *db;
	}

	MV::ThreadPool& pool() {
		return threadPool;
	}

	std::shared_ptr<MV::Server> server() {
		return ourServer;
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

	Managers &manager;
	bool done;

	double lastUpdateDelta;
};

class LoginOrCreateAction : public ServerAction {
public:
	LoginOrCreateAction(){}
	LoginOrCreateAction(const std::string &a_email, const std::string &a_password, bool a_allowCreate = false) :
		email(a_email),
		password(a_password),
		create(a_allowCreate){
	}

	LoginOrCreateAction(const std::string &a_email, const std::string &a_handle, const std::string &a_password, bool a_allowCreate = false) :
		email(a_email),
		handle(a_handle),
		password(a_password),
		create(a_allowCreate) {
	}

	virtual void execute(LobbyConnectionState& a_server) override {
		std::shared_ptr<pqxx::result> result;
		a_server.server().pool().task([=]() mutable {
			if ((email.empty() && handle.empty()) || password.empty()) {
				a_server.server().server()->send(MV::toBinaryString(MessageResponse("Failed to supply an email/handle or password when trying to log in.")));
				doneFlag = true;
			} else {
				pqxx::work transaction(a_server.server().database());

				std::string queryString = "SELECT verified, passalt, passhash, passiterations FROM players WHERE ";
				bool needOr = false;
				if (!email.empty()) {
					queryString += "email = " + transaction.quote(email);
					needOr = true;
				}
				if (!handle.empty()) {
					queryString += (needOr ? " OR " : "");
					queryString += "handle = " + transaction.quote(handle);
				}

				*result = transaction.exec(queryString);
			}
		}, [=]() mutable {
			if (result->empty()) {
				if (create) {
					a_server.server().pool().task([=]() mutable {
						pqxx::work transaction(a_server.server().database());

						*result = transaction.exec(CreatePlayerQueryString(transaction, ));
					}, [=]() mutable {
						if (result->affected_rows() == 1) {
							a_server.server().server()->send(MV::toBinaryString(MessageResponse("User created, woot!")));
						} else {
							a_server.server().server()->send(MV::toBinaryString(MessageResponse("User already exists, cannot create.")));
						}
					});
				} else {
					doneFlag = true;
				}
			} else if (!(*result)[0][0].as<bool>()) {
				a_server.server().server()->send(MV::toBinaryString(MessageResponse("Not verified.")));
				std::cout << "Not verified." << std::endl;
				doneFlag = true;
			} else if (MV::sha512(password + (*result)[0][2].c_str(), (*result)[0][3].as<int>()) == (*result)[0][1].c_str()){
				a_server.server().server()->send(MV::toBinaryString(MessageResponse("Successful login.")));
				std::cout << "Yup, we good." << std::endl;
				doneFlag = true;
			}
		});
	}

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(email), CEREAL_NVP(password));
	}

private:
	std::string CreatePlayerQueryString(pqxx::work &transaction, const std::string &email, const std::string &handle, const std::string &password, const std::string& defaultSaveState) {
		static int work = 12;
		auto salt = MV::randomString(64);
		std::stringstream query;
		query << "INSERT INTO public.players(email, handle, passhash, passsalt, passiterations, softcurrency, hardcurrency, state)";
		query << "VALUES(" << transaction.quote(email) << ", " << transaction.quote(handle) << ", ";
		query << transaction.quote(MV::sha512(password, salt, work)) << ", " << transaction.quote(salt) << ", " << work << ", ";
		query << DEFAULT_SOFT_CURRENCY << ", " << DEFAULT_HARD_CURRENCY << ", ";
		query << transaction.quote(MV::toBase64(defaultSaveState)) << ")";
		return query.str();
	}

	std::string handle;
	std::string email;
	std::string password;
	bool create = false;
	bool doneFlag = false;

	const int DEFAULT_HARD_CURRENCY = 150;
	const int DEFAULT_SOFT_CURRENCY = 500;
};

CEREAL_REGISTER_TYPE(LoginOrCreateAction);

#endif
