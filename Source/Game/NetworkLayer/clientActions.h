#ifndef _CLIENTACTIONS_MV_H_
#define _CLIENTACTIONS_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"
#include "Game/NetworkLayer/networkAction.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

class Game;
struct Player;

class MessageAction : public NetworkAction {
public:
	MessageAction(const std::string& a_message) : message(a_message) {}
	MessageAction() {}

	virtual void execute(Game& /*a_game*/) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(GameUserConnectionState*, GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyUserConnectionState* /*a_game*/) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyGameConnectionState*) override {
		std::cout << "Message Got: " << message << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<MessageAction>(), "MessageResponse");
		a_script.add(chaiscript::base_class<NetworkAction, MessageAction>());

		a_script.add(chaiscript::fun(&MessageAction::message), "message");
	}
private:
	std::string message;
};

CEREAL_REGISTER_TYPE(MessageAction);

class LoginResponse : public NetworkAction {
public:
	LoginResponse(const std::string& a_message, const std::string& a_player = "", bool a_success = false) : message(a_message), player(a_player), success(a_success) {}
	LoginResponse() {}

	virtual void execute(Game& a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), CEREAL_NVP(player), CEREAL_NVP(success), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	//Response actually happens in chaiscript anyway, so we just expose these in script and move on.
	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<LoginResponse>(), "LoginResponse");
		a_script.add(chaiscript::base_class<NetworkAction, LoginResponse>());

		a_script.add(chaiscript::fun(&LoginResponse::message), "message");
		a_script.add(chaiscript::fun(&LoginResponse::success), "success");
	}

	//useful to do in C++.
	std::shared_ptr<Player> loadedPlayer();

	bool hasPlayerState() const { return !player.empty(); }
private:
	std::string message;
	std::string player;

	bool success = false;
};

CEREAL_REGISTER_TYPE(LoginResponse);

class IllegalResponse : public NetworkAction {
public:
	IllegalResponse(const std::string& a_message) : message(a_message) {}
	IllegalResponse() {}

	virtual void execute(Game& a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(message), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<IllegalResponse>(), "IllegalResponse");
		a_script.add(chaiscript::base_class<NetworkAction, IllegalResponse>());

		a_script.add(chaiscript::fun(&IllegalResponse::message), "message");
	}
private:
	std::string message;
};

CEREAL_REGISTER_TYPE(IllegalResponse);

class ServerDetails : public NetworkAction {
public:
	virtual void execute(Game&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}
	virtual void execute(GameServer&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}
	virtual void execute(GameUserConnectionState*, GameServer&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(forceClientVersion), CEREAL_NVP(configurationHashes), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	int forceClientVersion = 1;
	std::map<std::string, std::string> configurationHashes;
};

CEREAL_REGISTER_TYPE(ServerDetails);

class MatchedResponse : public NetworkAction {
public:
	MatchedResponse(const std::string& a_gameServer, int16_t a_port, int64_t a_secret) : gameServer(a_gameServer), port(a_port), secret(a_secret) {}
	MatchedResponse() {}

	virtual void execute(Game& a_game) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(gameServer), CEREAL_NVP(port), CEREAL_NVP(secret), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	std::string gameServer;
	int16_t port;
	int64_t secret;
};

CEREAL_REGISTER_TYPE(MatchedResponse);

#endif