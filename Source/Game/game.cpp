#include "game.h"
#include "MV\Utility\stringUtility.h"
#include "MV/Utility/generalUtility.h"
#include <functional>

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

static jai::registrar<StandardMessages, MV::Services> _hookStandardMessages("StandardMessages",
	[](jai::dynamic_binder<StandardMessages>& builder, const MV::Services&) {
	auto& eng = builder.bound_engine();
	jai::bind_signal_type<void()>(eng, "SignalVoid");
	jai::bind_signal_type<void(const std::string&)>(eng, "SignalString");
	jai::bind_signal_type<void(bool, const std::string&)>(eng, "SignalBoolString");
	builder.property("lobbyConnected", &StandardMessages::lobbyConnected);
	builder.property("lobbyDisconnect", &StandardMessages::lobbyDisconnect);
	builder.property("lobbyAuthenticated", &StandardMessages::lobbyAuthenticated);
});

void Game::jai_auto_bind(jai::dynamic_binder<Game>& builder) {
	builder.property("loginId", &Game::loginId);
	builder.property("loginPassword", &Game::loginPassword);
	builder.property("client", [](Game& a_self) { return a_self.lobbyClient(); }, nullptr);
}

static jai::registrar<Game, MV::Services> _hookGame("Game",
	[](jai::dynamic_binder<Game>& builder, const MV::Services&) {
	builder.method("gui", &Game::gui);
	builder.method("instance", [](Game& a_self) {
		return std::shared_ptr<GameInstance>(a_self.instance(), [](GameInstance*) {});
	});
	builder.method("root", &Game::root);
	builder.method("player", &Game::player);
	builder.method("killGame", &Game::killGame);
});

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers) :
	gameData(a_managers, false),
	done(false),
	jaiEngine_(jai::engine::make()) {

	jai::stdlib::register_all(*jaiEngine_);
	jaiEngine_->set_script_error_handler([](const std::string& a_message) {
		MV::error("Script callback failed: ", a_message);
	});
	jai::bind_registrar<MV::Services>(*jaiEngine_, a_managers.services);

	// Script globals (non-owning: this/messages outlive the engine)
	jaiEngine_->add_global("game", jaiEngine_->make_object(std::shared_ptr<Game>(this, [](Game*) {})));
	jaiEngine_->add_global("messages", jaiEngine_->make_object(std::shared_ptr<StandardMessages>(&a_managers.messages, [](StandardMessages*) {})));
	jaiEngine_->add_global("DefaultLoginId", jai::script_value(a_managers.defaultLogin.id, jaiEngine_.get()));
	jaiEngine_->add_global("DefaultPassword", jai::script_value(a_managers.defaultLogin.password, jaiEngine_.get()));

	jaiEngine_->add_function("eval_file", [eng = jaiEngine_.get()](const std::string& a_path) {
		eng->execute(MV::fileContents("Interface/" + a_path, true));
	});

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
	auto serverAddressConfig = MV::fileLines("ServerConfig/gameServerAddress.config");
	std::string gameServerAddress = serverAddressConfig.empty() ? "http://localhost:22325" : serverAddressConfig[0];

	ourLobbyClient = MV::Client::make(MV::Url{ gameServerAddress }, [=](const std::string &a_message) {
		auto value = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message, gameData.managers().services);
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
		// Interface scenes reference atlas art by packId - the packs must be assembled
		// before InterfaceManager loads the pages or handles resolve to empty bounds.
		gameData.managers().textures.assemblePacks("Assets/Atlases", &gameData.managers().renderer);
		//gameData.managers().textures.files("Map");
		//gameData.managers().textures.files("Images");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::TapDevice& a_mouse, LocalData& a_data)

	if (!MV::RUNNING_IN_HEADLESS) {
		ourGui = std::make_unique<MV::InterfaceManager>(uiRoot, ourMouse, gameData.managers(), *jaiEngine_, "Interface/interfaceManager.script"s);
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

