#include "game.h"
#include <functional>

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers, std::string a_defaultLoginId, std::string a_defaultLoginPassword) :
	gameData(a_managers),
	defaultLoginId(a_defaultLoginId),
	defaultPassword(a_defaultLoginPassword),
	done(false),
	scriptEngine(MV::chaiscript_module_paths(), MV::chaiscript_use_paths(), chaiscript::default_options()){

	returnFromBackground();

	MV::initializeFilesystem();
	if (!MV::RUNNING_IN_HEADLESS) {
		initializeClientConnection();
	}
	initializeData();
	initializeWindow();
}

void Game::initializeData() {
	
}

void Game::initializeClientConnection() {
	ourLobbyClient = MV::Client::make(MV::Url{ "http://localhost:22325" /*"http://54.218.22.3:22325"*/ }, [=](const std::string &a_message) {
		auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
		value->execute(*this);
	}, [&](const std::string &a_dcreason) {
		MV::info("Disconnected: ", a_dcreason);
		gameData.managers().messages.lobbyDisconnect(a_dcreason);
		task.also("LobbyReconnect").recent()->
			then("Wait", std::make_shared<MV::BlockForSeconds>(backoffLobbyReconnect)).
			then("Reconnect", [&](MV::Task&) {
				initializeClientConnection();
			});
		backoffLobbyReconnect = std::min(2.0 * backoffLobbyReconnect, MAX_BACKOFF_RECONNECT_TIME);
	}, [=] {
		backoffLobbyReconnect = START_BACKOFF_RECONNECT_TIME;
		gameData.managers().messages.lobbyConnected();
	});
}

void Game::initializeWindow(){
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	gameData.managers().renderer.//makeHeadless().
		//window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);
        window().fullScreenMode().resizeWorldWithWindow(true);//.highResolution();

	if (!gameData.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit);

	managers().renderer.window().setTitle("Bindstone");

	MV::AudioPlayer::instance()->initAudio();
	ourMouse.update();

	rootScene = MV::Scene::Node::make(gameData.managers().renderer);
	screenScaler = rootScene->attach<MV::Scene::Sprite>();
	screenScaler->hide()->id("ScreenScaler");
	screenScaler->bounds({ MV::point(0.0f, 0.0f), gameData.managers().renderer.world().size() });

	gameData.managers().renderer.loadShader("vortex", "Assets/Shaders/default.vert", "Assets/Shaders/vortex.frag");
	gameData.managers().renderer.loadShader("lillypad", "Assets/Shaders/lillypad.vert", "Assets/Shaders/default.frag");
	gameData.managers().renderer.loadShader("wave", "Assets/Shaders/wave.vert", "Assets/Shaders/wave.frag");
	gameData.managers().renderer.loadShader("waterfall", "Assets/Shaders/default.vert", "Assets/Shaders/waterfall.frag");
	gameData.managers().renderer.loadShader("pool", "Assets/Shaders/default.vert", "Assets/Shaders/pool.frag");
	gameData.managers().renderer.loadShader("shimmer", "Assets/Shaders/default.vert", "Assets/Shaders/shimmer.frag");

	MV::FontDefinition::make(gameData.managers().textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(gameData.managers().textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(gameData.managers().textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	if (!gameData.managers().renderer.headless()) {
		gameData.managers().textures.assemblePacks("Assets/Atlases", &gameData.managers().renderer);
		gameData.managers().textures.files("Assets/Map");
		gameData.managers().textures.files("Assets/Images");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::TapDevice& a_mouse, LocalData& a_data)

	hook(scriptEngine);
	if (!MV::RUNNING_IN_HEADLESS) {
		ourGui = std::make_unique<MV::InterfaceManager>(rootScene, ourMouse, gameData.managers(), scriptEngine, "./Assets/Interface/interfaceManager.script");
		ourGui->initialize();
	}
}

bool Game::update(double dt) {
	gameData.managers().pool.run();
	task.update(dt);
	ourLobbyClient->update();
	if (ourGameClient) {
		ourGameClient->update();
	}

	if (ourInstance) {
		ourInstance->update(dt);
	}
	rootScene->update(dt);

	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		auto windowResized = gameData.managers().renderer.handleEvent(event);
		if(!windowResized && (!ourGui || (ourGui && !ourGui->handleInput(event))) && (!ourInstance || (ourInstance && !ourInstance->handleEvent(event)))){
			switch(event.type){
			case SDL_QUIT:
				done = true;
				break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym){
				case SDLK_ESCAPE:
					done = true;
					break;
				case SDLK_UP:
					//testBox->translateScrollPosition(MV::Point<>(0, -2));
					break;
				case SDLK_LEFT:
					//testBox->translateScrollPosition(MV::Point<>(-2, 0));
					break;
				case SDLK_DOWN:
					//testBox->translateScrollPosition(MV::Point<>(0, 2));
					//renderer.window().windowedMode().bordered();
					break;
				case SDLK_SPACE:
					//renderer.window().allowUserResize();
					break;
				case SDLK_RIGHT:
					//testBox->translateScrollPosition(MV::Point<>(2, 0));
					break;
				}
				break;
			}
		} else if (windowResized) {
			screenScaler->bounds({ MV::point(0.0f, 0.0f), gameData.managers().renderer.world().size() });
		}
	}
	ourMouse.update();
}

void Game::render() {
	gameData.managers().renderer.clearScreen();
	updateScreenScaler();
	if (ourInstance) {
		ourInstance->scene()->draw();
	}
	rootScene->draw();
	
	gameData.managers().renderer.updateScreen();
}

void Game::hook(chaiscript::ChaiScript &a_script) {
	bindstoneScriptHook(a_script, ourMouse, gameData.managers().pool);

	gameData.managers().messages.hook(a_script);

	a_script.add(chaiscript::user_type<Game>(), "Game");
	a_script.add(chaiscript::fun(&Game::gui), "gui");
	a_script.add(chaiscript::fun(&Game::instance), "instance");
	a_script.add(chaiscript::fun(&Game::root), "root");
	a_script.add(chaiscript::fun(&Game::localPlayer), "localPlayer");
	a_script.add(chaiscript::fun(&Game::ourMouse), "mouse");
	a_script.add(chaiscript::fun(&Game::enterGame), "enterGame");
	a_script.add(chaiscript::fun(&Game::killGame), "killGame");
	a_script.add(chaiscript::fun(&Game::ourLobbyClient), "client");
	a_script.add(chaiscript::fun(&Game::loginId), "loginId");
	a_script.add(chaiscript::fun(&Game::loginPassword), "loginPassword");
	a_script.add_global_const(chaiscript::const_var(defaultLoginId), "DefaultLoginId");
	a_script.add_global_const(chaiscript::const_var(defaultPassword), "DefaultPassword");

	a_script.add_global(chaiscript::var(this), "game");
}

void Game::updateScreenScaler() {
	auto scaler = rootScene->component<MV::Scene::Drawable>("ScreenScaler", false);
	if (!scaler) {
		rootScene->attach<MV::Scene::Drawable>()->id("ScreenScaler")->screenBounds({ MV::Point<int>(0, 0), gameData.managers().renderer.window().size() });
	} else {
		scaler->screenBounds({ MV::Point<int>(0, 0), gameData.managers().renderer.window().size() });
	}
}

