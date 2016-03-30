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

	LocalData data;
	std::unique_ptr<GameInstance> instance;

	bool done;

	double lastUpdateDelta;

	MV::MouseState mouse;
	//MV::Scene::Clickable::Signals armInputHandles;
};

void sdl_quit(void);
