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
#include "Network/package.h"

#include <string>
#include <ctime>

#include "chaiscript/chaiscript.hpp"

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

	MV::MouseState& mouse() {
		return ourMouse;
	}

	GameInstance& instance() {
		return *ourInstance;
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
			//ourGameClient->send(makeNetworkString<FullGameState>());
			enterGame();
		});
	}

	GameInstance& enterGame() {
		auto enemyPlayer = std::make_shared<Player>();
		enemyPlayer->handle = "Jai";
		enemyPlayer->loadout.buildings = { "life", "life", "life", "life", "life", "life", "life", "life" };
		enemyPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };
		enemyPlayer->wallet.add(Wallet::CurrencyType::SOFT, 5000);

		ourInstance = std::make_unique<GameInstance>(localPlayer, enemyPlayer, rootScene->makeOrGet("GameCamera"), gameData, ourMouse);
		lastUpdateDelta = 0.0f;
		return *ourInstance;
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

	double lastUpdateDelta;
	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
	MV::MouseState ourMouse;

	std::string loginId;
	std::string loginPassword;
};

void sdl_quit(void);

#endif
