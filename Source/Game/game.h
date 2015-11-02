#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include "Game/building.h"
#include <string>
#include <ctime>
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/chaiscript_stdlib.hpp"

class GamePlayer {
public:
	GamePlayer(const std::shared_ptr<Player> &a_player):
		player(a_player){
	}

private:
	std::vector<std::shared_ptr<Building>> buildings;
	int health;
	std::vector<std::shared_ptr<Creature>> creatures;
	std::vector<Gem> gems;

	std::shared_ptr<Player> player;
};

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



class Game {
public:
	Game(MV::ThreadPool* a_pool, MV::Draw2D* a_renderer);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	MV::ThreadPool* getPool() {
		return pool;
	}

	MV::Draw2D* getRenderer() {
		return renderer;
	}

	MV::TextLibrary* getTextLibrary() {
		return &textLibrary;
	}

private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	void initializeWindow();
	void spawnCreature(const MV::Point<> &a_position);

	void handleScroll(int a_amount);

	MV::ThreadPool* pool;
	MV::Draw2D* renderer;
	MV::TextLibrary textLibrary;

	MV::SharedTextures textures;
	
	std::shared_ptr<MV::Scene::Node> worldScene;

	MV::Scene::SafeComponent<MV::Scene::PathMap> pathMap;
	chaiscript::ChaiScript script;

	bool done;
	MV::MouseState mouse;

	double lastUpdateDelta;

	//MV::Scene::Clickable::Signals armInputHandles;
};

void sdl_quit(void);
