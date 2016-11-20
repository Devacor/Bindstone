#ifndef _CLIENTACTIONS_MV_H_
#define _CLIENTACTIONS_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

//#include "pqxx/pqxx"

class Game;

class ClientAction : public std::enable_shared_from_this<ClientAction> {
public:
	virtual ~ClientAction() = default;

	virtual void execute(Game& /*a_game*/) {}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}

	std::string toNetworkString() {
		return MV::toBinaryString(shared_from_this());
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<ClientAction>(), "ClientAction");
		a_script.add(chaiscript::fun(&ClientAction::toNetworkString), "toNetworkString");
	}
};

class MessageResponse : public ClientAction {
public:
	MessageResponse(const std::string& a_message) : message(a_message) {}
	MessageResponse() {}

	virtual void execute(Game& /*a_game*/) override {
		std::cout << "Message Got: " << message << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), cereal::make_nvp("ClientResponse", cereal::base_class<ClientAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<MessageResponse>(), "MessageResponse");
		a_script.add(chaiscript::base_class<ClientAction, MessageResponse>());

		a_script.add(chaiscript::fun(&MessageResponse::message), "message");
	}
private:
	std::string message;
};

CEREAL_REGISTER_TYPE(MessageResponse);

class LoginResponse : public ClientAction {
public:
	LoginResponse(const std::string& a_message, bool a_success = false) : message(a_message), success(a_success) {}
	LoginResponse() {}

	virtual void execute(Game& a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), CEREAL_NVP(success), cereal::make_nvp("ClientResponse", cereal::base_class<ClientAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<LoginResponse>(), "LoginResponse");
		a_script.add(chaiscript::base_class<ClientAction, LoginResponse>());

		a_script.add(chaiscript::fun(&LoginResponse::message), "message");
		a_script.add(chaiscript::fun(&LoginResponse::success), "success");
	}
private:
	std::string message;
	bool success = false;
};

CEREAL_REGISTER_TYPE(LoginResponse);

class ServerDetails : public ClientAction {
public:
	virtual void execute(Game& /*a_game*/) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(forceClientVersion), CEREAL_NVP(configurationHashes), cereal::make_nvp("ClientResponse", cereal::base_class<ClientAction>(this)));
	}

	int forceClientVersion = 1;
	std::map<std::string, std::string> configurationHashes;
};

CEREAL_REGISTER_TYPE(ServerDetails);

#endif