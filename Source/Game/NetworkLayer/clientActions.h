#ifndef _CLIENTACTIONS_MV_H_
#define _CLIENTACTIONS_MV_H_

#include "MV/Utility/package.h"
#include "MV/Network/package.h"
#include "Game/managers.h"
#include "Game/NetworkLayer/networkAction.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

class Game;
struct LocalPlayer;

class MessageAction : public NetworkAction {
public:
	MessageAction(const std::string& a_message) : message(a_message) {}
	MessageAction() {}

	virtual void execute(Game& /*a_game*/) override {
		std::cout << "Message Got: " << message << std::endl;
	}
#ifdef BINDSTONE_SERVER
	virtual void execute(GameUserConnectionState*, GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyUserConnectionState* /*a_game*/) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyGameConnectionState*) override {
		std::cout << "Message Got: " << message << std::endl;
	}
#endif
	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(message));
		NetworkAction::serialize(archive);
	}

	std::string message;
};

class LoginResponse : public NetworkAction {
public:
	LoginResponse(const std::string& a_message, const std::string& a_player = "", bool a_success = false) : message(a_message), player(a_player), success(a_success) {}
	LoginResponse() {}

	virtual void execute(Game& a_game) override;

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(message), JAI_NVP(player), JAI_NVP(success));
		NetworkAction::serialize(archive);
	}

	//useful to do in C++.
	std::shared_ptr<LocalPlayer> loadedPlayer();

	bool hasPlayerState() const { return !player.empty(); }

	std::string message;
	bool success = false;

	std::shared_ptr<LocalPlayer> playerObject;
	std::string player;
};

class IllegalResponse : public NetworkAction {
public:
	IllegalResponse(const std::string& a_message) : message(a_message) {}
	IllegalResponse() {}

	virtual void execute(Game& a_game) override;

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(message));
		NetworkAction::serialize(archive);
	}

	std::string message;
};

class ServerDetails : public NetworkAction {
public:
	virtual void execute(Game&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}
#ifdef BINDSTONE_SERVER
	virtual void execute(GameServer&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}
	virtual void execute(GameUserConnectionState*, GameServer&) override {
		std::cout << "Connected and expecting client version: " << forceClientVersion << std::endl;
	}
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(forceClientVersion), JAI_NVP(configurationHashes));
		NetworkAction::serialize(archive);
	}

	int forceClientVersion = 1;
	std::map<std::string, std::string> configurationHashes;
};

class MatchedResponse : public NetworkAction {
public:
	MatchedResponse(const std::string& a_gameServer, uint16_t a_port, int64_t a_secret) : gameServer(a_gameServer), port(a_port), secret(a_secret) {}
	MatchedResponse() {}

	virtual void execute(Game& a_game) override;

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(gameServer), JAI_NVP(port), JAI_NVP(secret));
		NetworkAction::serialize(archive);
	}

	std::string gameServer;
	uint16_t port = 0;
	int64_t secret = 0;
};

#endif