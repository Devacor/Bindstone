#ifndef _MV_GAME_EDITOR_H_
#define _MV_GAME_EDITOR_H_

#include "Game/game.h"
#include "Game/managers.h"
#include "Game/NetworkLayer/package.h"
#include "Editor/editor.h"

class GameEditor {
public:
	GameEditor(std::string a_username, std::string a_password);

	void start() {
		runLimbo();
	}
private:
	void resumeTitleMusic();

	void runLimbo() {
		managers.timer.start();
		done = false;

		while (!done) {
			managers.pool.run();
			handleInput();
			limbo->renderer().clearScreen();
			auto tick = managers.timer.delta("tick");
			limbo->drawUpdate(tick);
			limbo->renderer().updateScreen();
			std::this_thread::yield();
		}
	}

	void runEditor() {
		/*
		auto playlistGame = std::make_shared<MV::AudioPlayList>();
		playlistGame->addSoundBack("gameintro");
		playlistGame->addSoundBack("gameloop");
		playlistGame->addSoundBack("gameloop");
		playlistGame->addSoundBack("gameloop");
		playlistGame->addSoundBack("gameloop");
		playlistGame->addSoundBack("gameloop");

		playlistGame->loopSounds(true);
		*/
		//game.managers().audio.setMusicPlayList(playlistGame);

		//playlistGame->beginPlaying();

		editor.returnFromBackground();
		managers.timer.start();
		managers.timer.delta("tick");
		while (editor.update(managers.timer.delta("tick"))) {
			editor.handleInput();
			editor.render();
			std::this_thread::yield();
		}
	}

	void runGame() {
		game.returnFromBackground();
		managers.timer.start();
		managers.timer.delta("tick");
		while (game.update(managers.timer.delta("tick"))) {
			game.handleInput();
			game.render();
			std::this_thread::yield();
		}
		const Uint8 *state = SDL_GetKeyboardState(NULL);
		if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) {
			runLimbo();
		}
	}

	void updateFps(double a_dt) {
		lastUpdateDelta = a_dt;
		accumulatedTime += static_cast<float>(a_dt);
		++accumulatedFrames;
		if (accumulatedFrames > 60.0f) {
			accumulatedFrames /= 2.0f;
			accumulatedTime /= 2.0f;
		}
	}

	void handleInput();
	
	Managers managers;
	MV::TapDevice mouse;

	bool done = false;
	std::shared_ptr<MV::Scene::Node> limbo;

	double lastUpdateDelta = 0.0;

	float accumulatedTime = 0.0f;
	float accumulatedFrames = 0.0f;

	Game game;
	Editor editor;

	bool autoStartGame = false;

	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
};

#endif
