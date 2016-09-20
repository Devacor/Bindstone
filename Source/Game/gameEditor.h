#ifndef _MV_GAME_EDITOR_H_
#define _MV_GAME_EDITOR_H_

#include "Game/game.h"
#include "DiggerGame/diggerGame.h"
#include "editor/editor.h"
#include "Game/managers.h"
#include "Game/Server/package.h"

class GameEditor {
public:
	GameEditor();

	void start() {
		runLimbo();
	}
private:
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
			if (server) { server->update(tick); }
			if (client) { client->update(); }
			MV::systemSleep(0);
		}
	}

	void runEditor() {
		managers.timer.start();
		managers.timer.delta("tick");
		while (editor.update(managers.timer.delta("tick"))) {
			editor.handleInput();
			editor.render();
			MV::systemSleep(0);
		}
	}

	void runGame() {
		managers.timer.start();
		managers.timer.delta("tick");
		while (game.update(managers.timer.delta("tick"))) {
			game.handleInput();
			game.render();
			MV::systemSleep(0);
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

	void handleInput() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (!game.getManager().renderer.handleEvent(event)) {
				switch (event.type) {
				case SDL_QUIT:
					done = true;
					break;
				case SDL_KEYDOWN:
					switch (event.key.keysym.sym) {
					case SDLK_ESCAPE:
						done = true;
						break;
					case SDLK_UP:

						break;
					case SDLK_LEFT:
						break;
					case SDLK_DOWN:

						break;
					case SDLK_SPACE:

						break;
					case SDLK_RIGHT:
						break;
					}
					break;
				case SDL_WINDOWEVENT:
					break;
				case SDL_MOUSEWHEEL:
					break;
				}
			}else{
				screenScaler->bounds({ MV::point(0.0f, 0.0f), game.getManager().renderer.world().size() });
			}
		}
		mouse.update();
	}
	
	Managers managers;
	MV::MouseState mouse;

	bool done = false;
	std::shared_ptr<MV::Scene::Node> limbo;

	double lastUpdateDelta = 0.0;

	float accumulatedTime = 0.0f;
	float accumulatedFrames = 0.0f;

	Game game;
	Editor editor;

	std::shared_ptr<LobbyServer> server;
	std::shared_ptr<MV::Client> client;

	MV::Scene::SafeComponent<MV::Scene::Sprite> screenScaler;
};

#endif
