#ifndef _MV_GAME_EDITOR_H_
#define _MV_GAME_EDITOR_H_

#include "Game/game.h"
#include "ClickerGame/clickerGame.h"
#include "DiggerGame/diggerGame.h"
#include "editor/editor.h"

class GameEditor {
public:
	GameEditor():
		editor(&pool, &renderer),
		game(&pool, &renderer),
		limbo(MV::Scene::Node::make(renderer)){

		renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
		renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
		renderer.loadShader(MV::COLOR_PICKER_ID, "Assets/Shaders/default.vert", "Assets/Shaders/colorPicker.frag");

		limbo->make("PaletteTest")->position({ 200.0f, 400.0f })->
			attach<MV::Scene::Palette>(mouse)->bounds(MV::size(256.0f, 256.0f));

		auto grid = limbo->make("Grid")->position({ (static_cast<float>(game.getRenderer()->window().width()) - 100.0f) / 2.0f, 200.0f })->
			attach<MV::Scene::Grid>()->columns(1)->padding({ 2.0f, 2.0f })->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND })->owner();

		auto editorButton = makeButton(grid, *game.getTextLibrary(), mouse, "Editor", { 100.0f, 20.0f }, UTF_CHAR_STR("Editor"));
		editorButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			runEditor();
		});
		auto gameButton = makeButton(grid, *game.getTextLibrary(), mouse, "Game", { 100.0f, 20.0f }, UTF_CHAR_STR("Game"));
		gameButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			runGame();
		});
		auto quitButton = makeButton(grid, *game.getTextLibrary(), mouse, "Quit", { 100.0f, 20.0f }, UTF_CHAR_STR("Quit"));
		quitButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			done = true;
		});
	}

	void start() {
		runLimbo();
	}
private:
	void runLimbo() {
		timer.start();
		done = false;

		while (!done) {
			pool.run();
			handleInput();
			limbo->renderer().clearScreen();
			limbo->drawUpdate(timer.delta("tick"));
			limbo->renderer().updateScreen();
			MV::systemSleep(0);
		}
	}

	void runEditor() {
		timer.start();
		timer.delta("tick");
		while (editor.update(timer.delta("tick"))) {
			editor.handleInput();
			editor.render();
			MV::systemSleep(0);
		}
	}

	void runGame() {
		timer.start();
		timer.delta("tick");
		while (game.update(timer.delta("tick"))) {
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
			if (!game.getRenderer()->handleEvent(event)) {
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
			}
		}
		mouse.update();
	}

	MV::Stopwatch timer;
	MV::ThreadPool pool;
	MV::Draw2D renderer;

	bool done = false;
	MV::MouseState mouse;
	std::shared_ptr<MV::Scene::Node> limbo;

	double lastUpdateDelta = 0.0;

	float accumulatedTime = 0.0f;
	float accumulatedFrames = 0.0f;

	DiggerGame game;
	Editor editor;
};

#endif
