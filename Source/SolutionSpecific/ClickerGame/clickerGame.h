#ifndef __CLICKER_GAME_H__
#define __CLICKER_GAME_H__
#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include <string>
#include <ctime>
#include <stdint.h>

class ClickerPlayer {
private:
	MV::Signal<void(uint64_t)> onGoldChangeSignal;

public:
	MV::SignalRegister<void(uint64_t)> onGoldChange;
	ClickerPlayer():
		onGoldChange(onGoldChangeSignal){
	}

	void click() {
		gold(goldHeld + goldPerClick);
	}

	void gold(uint64_t a_newGold) {
		goldHeld = a_newGold;
		onGoldChangeSignal(goldHeld);
	}

	uint64_t gold() const {
		return goldHeld;
	}
private:
	uint64_t goldHeld = 0;
	uint64_t goldPerClick = 1;
	uint64_t goldPerSecond = 0;
};

class ClickerGame {
public:
	ClickerGame(MV::ThreadPool* pool, MV::Draw2D* renderer);

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
	ClickerGame(const ClickerGame &) = delete;
	ClickerGame& operator=(const ClickerGame &) = delete;

	void InitializeWorldScene();

	void initializeWindow();

	MV::ThreadPool* pool;
	MV::Draw2D* renderer;
	MV::TextLibrary textLibrary;

	MV::SharedTextures textures;

	std::shared_ptr<MV::Scene::Node> worldScene;

	bool done;
	MV::MouseState mouse;

	double lastUpdateDelta;

	ClickerPlayer player;

	//MV::Scene::Clickable::Signals armInputHandles;
};

void sdl_quit_2(void);

#endif