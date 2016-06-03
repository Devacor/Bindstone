#ifndef _MV_GAME_EDITOR_H_
#define _MV_GAME_EDITOR_H_

#include "Game/game.h"
#include "ClickerGame/clickerGame.h"
#include "DiggerGame/diggerGame.h"
#include "editor/editor.h"
#include "Game/managers.h"
#include "Game/Server/lobbyServer.h"

class GameEditor {
public:
	GameEditor():
		game(managers),
		editor(managers),
		limbo(MV::Scene::Node::make(managers.renderer)){

		managers.renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
		managers.renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
		managers.renderer.loadShader(MV::COLOR_PICKER_ID, "Assets/Shaders/default.vert", "Assets/Shaders/colorPicker.frag");

		//auto spineTestNode = limbo->make("SpineTest")->position({ 400.0f, 600.0f })->attach<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Assets/Spine/Tree/life.json", "Assets/Spine/Tree/life.atlas", 0.5f))->shader(MV::DEFAULT_ID)->animate("idle")->bindNode("effects", "tree_particle")->bindNode("effects", "simple")->owner();
// 		spineTestNode->make("PaletteTest")->position({ -50.0f, -100.0f })->
// 			attach<MV::Scene::Palette>(mouse)->bounds(MV::size(256.0f, 256.0f));
// 		auto populateArchive = [&](cereal::JSONInputArchive& archive) {
// 			archive.add(cereal::make_nvp("renderer", &managers.renderer));
// 			archive.add(cereal::make_nvp("pool", &managers.pool));
// 		};
// 		spineTestNode->loadChild("simple.scene", populateArchive);
// 		spineTestNode->loadChild("tree_particle.scene", populateArchive);

		auto grid = limbo->make("Grid")->position({ (static_cast<float>(game.getManager().renderer.window().width()) - 100.0f) / 2.0f, 200.0f })->
			attach<MV::Scene::Grid>()->columns(1)->padding({ 2.0f, 2.0f })->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND })->owner();

		auto editorButton = makeButton(grid, game.getManager().textLibrary, mouse, "Editor", { 100.0f, 20.0f }, UTF_CHAR_STR("Editor"));
		editorButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			runEditor();
		});
		auto gameButton = makeButton(grid, game.getManager().textLibrary, mouse, "Game", { 100.0f, 20.0f }, UTF_CHAR_STR("Game"));
		gameButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			runGame();
		});
		auto quitButton = makeButton(grid, game.getManager().textLibrary, mouse, "Quit", { 100.0f, 20.0f }, UTF_CHAR_STR("Quit"));
		quitButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			done = true;
		});

		auto serverButton = makeButton(grid, game.getManager().textLibrary, mouse, "Server", { 100.0f, 20.0f }, UTF_CHAR_STR("Server"));
		serverButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			std::cout << "serving" << std::endl;
			server = std::make_shared<LobbyServer>(managers);
		});

		auto clientButton = makeButton(grid, game.getManager().textLibrary, mouse, "Client", { 100.0f, 20.0f }, UTF_CHAR_STR("Client"));
		clientButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			client = MV::Client::make(MV::Url{ "http://ec2-54-218-22-3.us-west-2.compute.amazonaws.com:22325" }, [=](const std::string &a_message) {
			//client = MV::Client::make(MV::Url{ "http://96.229.120.252:22325" }, [=](const std::string &a_message) {
				static int i = 0;
				std::cout << "GOT MESSAGE: [" << a_message << "]" << std::endl;
				client->send(std::to_string(++i));
			}, [](const std::string &a_dcreason) {
				std::cout << "Disconnected: " << a_dcreason << std::endl;
			}, [=] { client->send("UUUUNG"); });
			//client->send("UUUUH!");
		});

		auto sendButton = makeButton(grid, game.getManager().textLibrary, mouse, "Send", { 100.0f, 20.0f }, UTF_CHAR_STR("Send"));
		sendButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (client) {
				client->send("Client to server message!");
			}
		});

		if (MV::RUNNING_IN_HEADLESS) {
			serverButton->press();
		}
	}

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
};

#endif
