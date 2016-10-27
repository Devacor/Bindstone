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

#include <string>
#include <ctime>

#include "chaiscript/chaiscript.hpp"

class Game {
public:
	Game(Managers &a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	Managers& getManager() {
		return data.managers();
	}

	MV::MouseState& mouse() {
		return ourMouse;
	}

	GameInstance& getInstance() {
		return *instance;
	}
private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	void updateScreenScaler();
	void initializeData();
	void initializeWindow();

	GameData data;
	std::unique_ptr<MV::InterfaceManager> gui;
	std::unique_ptr<GameInstance> instance;

	std::shared_ptr<MV::Scene::Node> root;

	chaiscript::ChaiScript scriptEngine;

	std::shared_ptr<Player> localPlayer;

	bool done;

	double lastUpdateDelta;

	MV::MouseState ourMouse;
};

void sdl_quit(void);

#endif
