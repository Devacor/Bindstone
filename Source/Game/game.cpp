#include "game.h"
#include "MV\Utility\stringUtility.h"
#include <functional>

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers) :
	gameData(a_managers, false),
	done(false),
	scriptEngine(a_managers.services){

	returnFromBackground();

	MV::initializeSpineBindings();
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
	std::string gameServerAddress = MV::explode(MV::fileContents("ServerConfig/gameServerAddress.config"), [](char c) {return c == '\n'; })[0];

	ourLobbyClient = MV::Client::make(MV::Url{ gameServerAddress }, [=](const std::string &a_message) {
		auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
		value->execute(*this);
	}, [=](const std::string &a_dcreason) {
		MV::info("Disconnected [", gameServerAddress, "]: ", a_dcreason);
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

	gameData.managers().renderer.//makeHeadless().
#ifdef WIN32
		window().windowedMode().allowUserResize(false, MV::Size(800, 600)).resizeWorldWithWindow(true).highResolution();
#else
        window().fullScreenMode().borderless().resizeWorldWithWindow(true).highResolution();
#endif

	MV::Size<int> windowSize = gameData.managers().renderer.monitorSize();

	auto aspectX = static_cast<float>(windowSize.width) / windowSize.height;
	MV::Size<> worldSize(1080 * aspectX, 1080);

#ifdef WIN32
	windowSize /= 2;
#endif

	MV::info("PRE-SCALE: ", windowSize);

	if (!gameData.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit);

	managers().renderer.window().setTitle("Bindstone");

	//MV::AudioPlayer::instance()->initAudio();
	ourMouse.update();

	rootScene = MV::Scene::Node::make(gameData.managers().renderer);

	uiRoot = rootScene->make("UI")->cameraId(1);
	screenScaler = rootScene->attach<MV::Scene::Sprite>();
	screenScaler->hide()->id("ScreenScaler");
	auto scaledDpi = gameData.managers().renderer.window().uiScale();
	screenScaler->bounds({ MV::point(0.0f, 0.0f), gameData.managers().renderer.world().size() / scaledDpi });
	rootScene->scale(scaledDpi);

	gameData.managers().renderer.loadShader("vortex", "Shaders/default.vert", "Shaders/vortex.frag");
	gameData.managers().renderer.loadShader("lillypad", "Shaders/lillypad.vert", "Shaders/default.frag");
	gameData.managers().renderer.loadShader("wave", "Shaders/wave.vert", "Shaders/wave.frag");
	gameData.managers().renderer.loadShader("waterfall", "Shaders/default.vert", "Shaders/waterfall.frag");
	gameData.managers().renderer.loadShader("pool", "Shaders/default.vert", "Shaders/pool.frag");
	gameData.managers().renderer.loadShader("shimmer", "Shaders/default.vert", "Shaders/shimmer.frag");

	MV::FontDefinition::make(gameData.managers().textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(gameData.managers().textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(gameData.managers().textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	if (!gameData.managers().renderer.headless()) {
		//gameData.managers().textures.assemblePacks("Assets/Atlases", &gameData.managers().renderer);
		//gameData.managers().textures.files("Map");
		//gameData.managers().textures.files("Images");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::TapDevice& a_mouse, LocalData& a_data)

	if (!MV::RUNNING_IN_HEADLESS) {
		ourGui = std::make_unique<MV::InterfaceManager>(uiRoot, ourMouse, gameData.managers(), scriptEngine, "Interface/interfaceManager.script"s);
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
			auto scale = gameData.managers().renderer.window().uiScale();
			screenScaler->bounds({ MV::point(0.0f, 0.0f), gameData.managers().renderer.world().size() / scale });
			screenScaler->owner()->scale(scale);
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

void Game::updateScreenScaler() {
	auto scaler = rootScene->component<MV::Scene::Drawable>("ScreenScaler", false);
	if (!scaler) {
		auto scale = gameData.managers().renderer.window().uiScale();
		rootScene->attach<MV::Scene::Drawable>()->id("ScreenScaler")->worldBounds({ MV::Point<>(0, 0), gameData.managers().renderer.world().size() / scale });
		rootScene->scale(scale);
	}
}

