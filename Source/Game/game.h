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
	MV::Signal<void(LoginResponse&)> onLoginResponseSignal;
public:
	MV::SignalRegister<void(LoginResponse&)> onLoginResponse;

	Game(Managers &a_managers);

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
			std::cout << "Disconnected: " << a_dcreason << std::endl;
			killGame();
		}, [=] {
			std::cout << "Connection Initialized" << std::endl;

			ourGameClient->send(makeNetworkString<GetInitialGameState>(secret));
		});
	}

	GameInstance* enterGame(const std::shared_ptr<Player> &a_left, const std::shared_ptr<Player> &a_right) {
		ourInstance = std::make_unique<MockClientGameInstance>(a_left, a_right, *this);
		gui().page("Main").hide();
		return ourInstance.get();
	}

	void killGame() {
		ourInstance = nullptr;
		gui().page("Login").show();
	}

	void hook(chaiscript::ChaiScript &a_script);

	void authenticate(LoginResponse& a_response) {
		if (a_response.hasPlayerState()) {
			localPlayer = a_response.loadedPlayer();
		}
		onLoginResponseSignal(a_response);
	}

	std::shared_ptr<MV::Client> lobbyClient() {
		return ourLobbyClient;
	}

	std::shared_ptr<MV::Client> gameClient() {
		return ourGameClient;
	}

	std::shared_ptr<Player> player() {
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

	std::shared_ptr<Player> localPlayer;

	std::shared_ptr<MV::Client> ourLobbyClient;
	std::shared_ptr<MV::Client> ourGameClient;

	MV::Task task;
	
	bool done;

	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
	MV::TapDevice ourMouse;

	std::string loginId;
	std::string loginPassword;
};

void sdl_quit(void);

#endif
