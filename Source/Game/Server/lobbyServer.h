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

class ClientResponse : public std::enable_shared_from_this<ClientResponse> {
public:
	virtual ~ClientResponse() = default;

	virtual void execute() {}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) { 
		archive(0);
	}
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
		archive(CEREAL_NVP(message), cereal::make_nvp("ClientResponse", cereal::base_class<ClientResponse>(this)));
	}
private:
	std::string message;
};

CEREAL_REGISTER_TYPE(MessageResponse);

class ServerAction : public std::enable_shared_from_this<ServerAction> {
public:
	virtual ~ServerAction() = default;

	virtual void execute(LobbyConnectionState&) {
	}

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) { 
		archive(0);
	}
};

class LobbyServer;

class ServerDetails: public ClientResponse{
public:
	virtual void execute() override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(forceClientVersion), CEREAL_NVP(configurationHashes), cereal::make_nvp("ClientResponse", cereal::base_class<ClientResponse>(this)));
	}

	int forceClientVersion = 1;
	std::map<std::string, std::string> configurationHashes;
};

CEREAL_REGISTER_TYPE(ServerDetails);

class LobbyConnectionState : public MV::ConnectionStateBase {
public:
	LobbyConnectionState(MV::Connection *a_connection, LobbyServer& a_server);

	virtual void connect() override;

	virtual void message(const std::string &a_message) {
		auto action = MV::fromBase64<std::shared_ptr<ServerAction>>(a_message);
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

class CreatePlayer : public ServerAction {
public:
	CreatePlayer() {}

	CreatePlayer(const std::string &a_email, const std::string &a_handle, const std::string &a_password) :
		email(a_email),
		handle(a_handle),
		password(a_password) {
	}

	virtual void execute(LobbyConnectionState& a_connection) override;

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(handle), CEREAL_NVP(email), CEREAL_NVP(password), cereal::make_nvp("ServerAction", cereal::base_class<ServerAction>(this)));
	}

private:
	void sendValidationEmail(LobbyConnectionState &a_connection, const std::string &a_passSalt);

	bool validateHandle(const std::string &a_handle) {
		return a_handle.size() > 3 && MV::simpleFilter(a_handle) == a_handle;
	}

	std::string makeSaveString();

	std::string createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt);

	std::string handle;
	std::string email;
	std::string password;
	bool doneFlag = false;

	const int DEFAULT_HARD_CURRENCY = 150;
	const int DEFAULT_SOFT_CURRENCY = 500;
};

CEREAL_REGISTER_TYPE(CreatePlayer);

// class LoginRequest : public ServerAction {
// public:
// 	LoginRequest() {}
// 	LoginRequest(const std::string &a_identifier, const std::string &a_password) :
// 		identifier(a_identifier),
// 		password(a_password) {
// 	}
// 
// 	virtual void execute(LobbyConnectionState& a_connection) override {
// 		std::shared_ptr<pqxx::result> result;
// 		a_connection.server().pool().task([=]() mutable {
// 			if (identifier.empty() || password.empty()) {
// 				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Failed to supply an email/handle or password when trying to log in.")));
// 				doneFlag = true;
// 			} else {
// 				pqxx::work transaction(a_connection.server().database());
// 
// 				auto quotedIdentifier = transaction.quote(identifier);
// 
// 				std::string queryString = "SELECT verified, passalt, passhash, passiterations FROM players WHERE email = " + 
// 					quotedIdentifier + " OR handle = " + quotedIdentifier;
// 
// 				*result = transaction.exec(queryString);
// 			}
// 		}, [=]() mutable {
// 			if (result->empty()) {
// 				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("No account.")));
// 				doneFlag = true;
// 			} else if (!(*result)[0][0].as<bool>()) {
// 				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Not verified.")));
// 				std::cout << "Not verified [" << identifier << "]" << std::endl;
// 				doneFlag = true;
// 			} else if (MV::sha512(password, (*result)[0][2].c_str(), (*result)[0][3].as<int>()) == (*result)[0][1].c_str()) {
// 				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Successful login.")));
// 				std::cout << "Yup, we good." << std::endl;
// 				doneFlag = true;
// 			} else {
// 				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Login failed.")));
// 				std::cout << "Login failed." << std::endl;
// 				doneFlag = true;
// 			}
// 		});
// 	}
// 
// 	virtual bool done() const override { return doneFlag; }
// 
// 	template <class Archive>
// 	void serialize(Archive & archive, std::uint32_t const /*version*/) {
// 		archive(CEREAL_NVP(identifier), CEREAL_NVP(password), cereal::make_nvp("ServerAction", cereal::base_class<ServerAction>(this)));
// 		doneFlag = false;
// 	}
// 
// private:
// 
// 	std::string identifier;
// 	std::string password;
// 		
// 	bool doneFlag = false;
// };
// 
// CEREAL_REGISTER_TYPE(LoginRequest);

#endif
