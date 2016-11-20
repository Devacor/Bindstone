#include "game.h"
#include <functional>

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers) :
	gameData(a_managers),
	done(false),
	scriptEngine(MV::create_chaiscript_stdlib(), MV::chaiscript_module_paths(), MV::chaiscript_use_paths()),
	onLoginResponseScript(onLoginResponse){
	
	MV::initializeFilesystem();
	initializeData();
	initializeWindow();
	if (!MV::RUNNING_IN_HEADLESS) {
		initializeClientConnection();
	}
}

void Game::initializeData() {
	
}

void Game::initializeClientConnection() {
	client = MV::Client::make(MV::Url{ /*"http://96.229.120.252:22325"*/ "http://54.218.22.3:22325" }, [=](const std::string &a_message) {
		auto value = MV::fromBinaryString<std::shared_ptr<ClientAction>>(a_message);
		value->execute(*this);
	}, [&](const std::string &a_dcreason) {
		std::cout << "Disconnected: " << a_dcreason << std::endl;
		killGame();
	}, [=] {});
}

void Game::initializeWindow(){
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	gameData.managers().renderer.//makeHeadless().
		window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!gameData.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit);

	MV::AudioPlayer::instance()->initAudio();
	ourMouse.update();

	rootScene = MV::Scene::Node::make(gameData.managers().renderer);

	MV::FontDefinition::make(gameData.managers().textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(gameData.managers().textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(gameData.managers().textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	if (!gameData.managers().renderer.headless()) {
		gameData.managers().textures.assemblePacks("Assets/Atlases", &gameData.managers().renderer);
		gameData.managers().textures.files("Assets/Map");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::MouseState& a_mouse, LocalData& a_data)
	localPlayer = std::make_shared<Player>();
	localPlayer->name = "Dervacor";
	localPlayer->loadout.buildings = {"life", "life", "life", "life", "life", "life", "life", "life"};
	localPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };

	localPlayer->wallet.add(Wallet::CurrencyType::SOFT, 5000);

// 	{
// 		std::ofstream stream("playerExample.json");
// 		cereal::JSONOutputArchive archive(stream);
// 		archive(localPlayer);
// 	}

	hook(scriptEngine);
	if (!MV::RUNNING_IN_HEADLESS) {
		ourGui = std::make_unique<MV::InterfaceManager>(rootScene, ourMouse, gameData.managers(), scriptEngine, "./Assets/Interface/interfaceManager.script");
	}

	SDL_SetClipboardText(MV::sha512("SuperTinker123", "oREr1ybrCNxCrckhzAEr_v-4xSNtciOV", 12).c_str());
}

bool Game::update(double dt) {
	lastUpdateDelta = dt;
	gameData.managers().pool.run();
	client->update();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!gameData.managers().renderer.handleEvent(event) && (!ourInstance || (ourInstance && !ourInstance->handleEvent(event)))){
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
		}
	}
	ourMouse.update();
}

void Game::render() {
	gameData.managers().renderer.clearScreen();
	updateScreenScaler();
	if (ourInstance) {
		ourInstance->update(lastUpdateDelta);
	}
	rootScene->update(lastUpdateDelta);
	rootScene->draw();
	
	gameData.managers().renderer.updateScreen();
}

void Game::hook(chaiscript::ChaiScript &a_script) {
	MV::TexturePoint::hook(a_script);
	MV::Color::hook(a_script);
	MV::Size<MV::PointPrecision>::hook(a_script);
	MV::Size<int>::hook(a_script, "i");
	MV::Point<MV::PointPrecision>::hook(a_script);
	MV::Point<int>::hook(a_script, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(a_script);
	MV::BoxAABB<int>::hook(a_script, "i");

	MV::TexturePack::hook(a_script);
	MV::TextureDefinition::hook(a_script);
	MV::FileTextureDefinition::hook(a_script);
	MV::TextureHandle::hook(a_script);
	MV::SharedTextures::hook(a_script);

	Wallet::hook(a_script);
	Player::hook(a_script);
	Team::hook(a_script);
	MV::Task::hook(a_script);
	GameData::hook(a_script);

	MV::PathNode::hook(a_script);
	MV::NavigationAgent::hook(a_script);

	MV::Scene::Node::hook(a_script);
	MV::Scene::Component::hook(a_script);
	MV::Scene::Drawable::hook(a_script);
	MV::Scene::Sprite::hook(a_script);
	MV::Scene::Spine::hook(a_script);
	MV::Scene::Text::hook(a_script);
	MV::Scene::PathMap::hook(a_script);
	MV::Scene::PathAgent::hook(a_script);
	MV::Scene::Emitter::hook(a_script, gameData.managers().pool);

	MV::Client::hook(a_script);
	CreatePlayer::hook(a_script);
	ClientAction::hook(a_script);
	ServerAction::hook(a_script);
	LoginRequest::hook(a_script);
	LoginResponse::hook(a_script);

	MV::InterfaceManager::hook(a_script);

	a_script.add(chaiscript::user_type<Game>(), "Game");
	a_script.add(chaiscript::fun(&Game::gui), "gui");
	a_script.add(chaiscript::fun(&Game::instance), "instance");
	a_script.add(chaiscript::fun(&Game::root), "root");
	a_script.add(chaiscript::fun(&Game::localPlayer), "localPlayer");
	a_script.add(chaiscript::fun(&Game::ourMouse), "mouse");
	a_script.add(chaiscript::fun(&Game::enterGame), "enterGame");
	a_script.add(chaiscript::fun(&Game::killGame), "killGame");
	a_script.add(chaiscript::fun(&Game::client), "client");

	MV::SignalRegister<void(LoginResponse&)>::hook(a_script);
	a_script.add(chaiscript::fun(&Game::onLoginResponseScript), "onLoginResponse");

	a_script.add_global(chaiscript::var(this), "game");

	a_script.add(chaiscript::fun([](int a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](size_t a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](float a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](double a_from) {return MV::to_string(a_from); }), "to_string");
}

void Game::updateScreenScaler() {
	auto scaler = rootScene->component<MV::Scene::Drawable>("ScreenScaler", false);
	if (!scaler) {
		rootScene->attach<MV::Scene::Drawable>()->id("ScreenScaler")->screenBounds({ MV::Point<int>(0, 0), gameData.managers().renderer.window().size() });
	}
	else {
		scaler->screenBounds({ MV::Point<int>(0, 0), gameData.managers().renderer.window().size() });
	}
}

