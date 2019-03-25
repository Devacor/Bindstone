#ifndef _GAME_MV_H_
#define _GAME_MV_H_

#include <SDL.h>
#include "Game/managers.h"
#include "Game/player.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/state.h"
#include "Game/Instance/gameInstance.h"
#include "Game/Interface/interfaceManager.h"
#include "Game/NetworkLayer/package.h"
#include "MV/Network/package.h"

#include <string>
#include <ctime>

#include "MV/Utility/chaiscriptUtility.h"

class Game {
public:
	Game(Managers &a_managers, std::string a_defaultLoginId, std::string a_defaultLoginPassword);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	GameData& data() {
		return gameData;
	}

	MV::InterfaceManager& gui() {
		return *ourGui;
	}

	Managers& managers() {
		return gameData.managers();
	}

	MV::TapDevice& mouse() {
		return ourMouse;
	}

	GameInstance* instance() {
		return ourInstance.get();
	}

	std::shared_ptr<MV::Scene::Node> root() {
		return rootScene;
	}

	void enterGameServer(const std::string &gameServer, int64_t secret) {
		std::cout << "Game Found: " << gameServer << " Secret: " << secret << std::endl;
		ourGameClient = MV::Client::make(MV::Url{ gameServer }, [=](const std::string &a_message) {
			auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
			value->execute(*this);
		}, [&](const std::string &a_dcreason) {
			MV::info("Disconnected: ", a_dcreason);
			killGame();
		}, [=] {
			MV::info("Connection Initialized");

			ourGameClient->send(makeNetworkString<GetInitialGameState>(secret));
		});
	}

	GameInstance* enterGame(const std::shared_ptr<InGamePlayer> &a_left, const std::shared_ptr<InGamePlayer> &a_right) {
		ourInstance = ClientGameInstance::make(a_left, a_right, *this);
		gui().page("Main").hide();
		return ourInstance.get();
	}

	void killGame() {
		ourInstance = nullptr;
		ourGameClient = nullptr;
		gui().page("Main").show();
	}

	void hook(chaiscript::ChaiScript &a_script);

	void authenticate(LoginResponse& a_response) {
		if (a_response.hasPlayerState()) {
			localPlayer = a_response.loadedPlayer();
		}
		if (a_response.success) {
			managers().renderer.window().setTitle("Bindstone [" + localPlayer->handle + "] ["+std::to_string(localPlayer->id)+"]");
		}
		managers().messages.lobbyAuthenticated(a_response.success, a_response.message);
	}

	std::shared_ptr<MV::Client> lobbyClient() {
		return ourLobbyClient;
	}

	std::shared_ptr<MV::Client> gameClient() {
		return ourGameClient;
	}

	std::shared_ptr<LocalPlayer> player() {
		return localPlayer;
	}

	void returnFromBackground() {
		gameData.managers().services.connect(&ourMouse);
		gameData.managers().services.connect(&scriptEngine);
	}
private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	void updateScreenScaler();
	void initializeData();
	void initializeWindow();

	void initializeClientConnection();

	GameData gameData;
	std::unique_ptr<MV::InterfaceManager> ourGui;
	std::unique_ptr<GameInstance> ourInstance;

	std::shared_ptr<MV::Scene::Node> rootScene;

	chaiscript::ChaiScript scriptEngine;

	std::shared_ptr<LocalPlayer> localPlayer;

	std::shared_ptr<MV::Client> ourLobbyClient;
	std::shared_ptr<MV::Client> ourGameClient;

	MV::Task task;
	
	bool done;

	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
	MV::TapDevice ourMouse;

	std::string loginId;
	std::string loginPassword;
	std::string defaultLoginId;
	std::string defaultPassword;

	const double START_BACKOFF_RECONNECT_TIME = .25;
	const double MAX_BACKOFF_RECONNECT_TIME = 10.0;
	double backoffLobbyReconnect = .25;
};

void sdl_quit(void);

#endif
