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
public:
	MV::Signal<void(LoginResponse&)> onLoginResponse;
	MV::SignalRegister<void(LoginResponse&)> onLoginResponseScript;

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

	GameInstance& enterGame() {
		auto enemyPlayer = std::make_shared<Player>();
		enemyPlayer->name = "Jai";
		enemyPlayer->loadout.buildings = { "life", "life", "life", "life", "life", "life", "life", "life" };
		enemyPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };
		enemyPlayer->wallet.add(Wallet::CurrencyType::SOFT, 5000);

		ourInstance = std::make_unique<GameInstance>(localPlayer, enemyPlayer, *this);
		lastUpdateDelta = 0.0f;
		return *ourInstance;
	}

	void killGame() {
		ourInstance = nullptr;
		gui().page("Login").show();
	}

	void hook(chaiscript::ChaiScript &a_script);
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

	std::shared_ptr<MV::Client> client;

	MV::Task task;
	
	bool done;

	double lastUpdateDelta;
	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
	MV::MouseState ourMouse;
};

void sdl_quit(void);

#endif
