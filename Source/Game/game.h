#include <SDL.h>
#include "Game/managers.h"
#include "Game/building.h"
#include <string>
#include <ctime>
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/chaiscript_stdlib.hpp"

class LocalData {
public:
	std::shared_ptr<Player> player() const {
		return localPlayer;
	}

	BuildingData& building(const std::string &a_id) const {
		
	}
private:
	std::shared_ptr<Player> localPlayer;
	std::vector<BuildingData> buildings;
};

struct Constants {
	int startHealth = 20;
};

class Team {
public:
	Team(const std::shared_ptr<Player> &a_player, const Constants& a_constants) :
		player(a_player),
		health(a_constants.startHealth) {
	}


private:
	std::vector<std::shared_ptr<Building>> buildings;
	int health;
	std::vector<std::shared_ptr<Creature>> creatures;
	std::vector<Gem> gems;

	std::shared_ptr<Player> player;
};

class GameInstance {
public:
	GameInstance(Managers &a_managers, MV::MouseState& a_mouse, const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const Constants& a_constants);

	bool update(double dt);

	bool handleEvent(const SDL_Event &a_event);
private:
	void handleScroll(int a_amount);

	Managers &managers;
	MV::MouseState& mouse;

	std::shared_ptr<MV::Scene::Node> worldScene;
	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;

	Team left;
	Team right;

	chaiscript::ChaiScript script;
};

class Game {
public:
	Game(Managers &a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	Managers& getManager() {
		return managers;
	}

	MV::MouseState& getMouse() {
		return mouse;
	}

private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	void initializeWindow();
	void spawnCreature(const MV::Point<> &a_position);

	Managers &managers;

	std::unique_ptr<GameInstance> instance;

	bool done;

	double lastUpdateDelta;

	MV::MouseState mouse;

	//MV::Scene::Clickable::Signals armInputHandles;
};

void sdl_quit(void);
