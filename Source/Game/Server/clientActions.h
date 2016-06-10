#ifndef _CLIENTACTIONS_MV_H_
#define _CLIENTACTIONS_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "pqxx/pqxx"
#undef ERROR

class LobbyConnectionState;

class ClientAction : public std::enable_shared_from_this<ClientAction> {
public:
	virtual ~ClientAction() = default;

	virtual void execute() {}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}
};

class MessageResponse : public ClientAction {
public:
	MessageResponse(const std::string& a_message) : message(a_message) {}
	MessageResponse() {}

	virtual void execute() override {
		std::cout << "Message Got: " << message << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), cereal::make_nvp("ClientResponse", cereal::base_class<ClientAction>(this)));
	}
private:
	std::string message;
};

CEREAL_REGISTER_TYPE(MessageResponse);

class ServerDetails : public ClientAction {
public:
	virtual void execute() override {
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