#include <SDL.h>
#include "Game/managers.h"
#include "Game/player.h"
#include "Game/building.h"
#include "Game/creature.h"
#include "Game/state.h"
#include "Game/Instance/gameInstance.h"

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

	MV::MouseState& getMouse() {
		return mouse;
	}

	GameInstance& getInstance() {
		return *instance;
	}
private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	void initializeData();
	void initializeWindow();
	void spawnCreature(const MV::Point<> &a_position);

	GameData data;
	std::unique_ptr<GameInstance> instance;

	std::shared_ptr<Player> localPlayer;

	bool done;

	double lastUpdateDelta;

	MV::MouseState mouse;
	//MV::Scene::Clickable::Signals armInputHandles;
};

class Server {
public:
	Server(Managers &a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	Managers& getManager() {
		return data.managers();
	}

	MV::MouseState& getMouse() {
		return mouse;
	}

	GameInstance& getInstance() {
		return *instance;
	}
private:
	Server(const Server &) = delete;
	Server& operator=(const Server &) = delete;

	void initializeData();
	void initializeWindow();
	void spawnCreature(const MV::Point<> &a_position);

	GameData data;
	std::vector<std::unique_ptr<ServerGameInstance>> instance;

	bool done;

	double lastUpdateDelta;

	MV::MouseState mouse;
};

void sdl_quit(void);
