#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include <string>
#include <ctime>

class Game {
public:
	Game();

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	MV::ThreadPool* getPool() {
		return &pool;
	}

	MV::Draw2D* getRenderer() {
		return &renderer;
	}

	MV::TextLibrary* getTextLibrary() {
		return &textLibrary;
	}

private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

	std::shared_ptr<MV::Scene::Node> initializeCatapultScene();
	void initializeWindow();
	std::shared_ptr<MV::Scene::Node> initializeTextScene();

	MV::ThreadPool pool;
	MV::Draw2D renderer;
	MV::TextLibrary textLibrary;

	MV::SharedTextures textures;
	
	std::shared_ptr<MV::Scene::Node> worldScene;
	
	bool done;
	MV::MouseState mouse;

	double lastUpdateDelta;

	//MV::Scene::Clickable::Signals armInputHandles;
};

void sdl_quit(void);
