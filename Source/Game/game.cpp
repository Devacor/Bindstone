#include "game.h"
#include <functional>

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers) :
	data(a_managers),
	done(false){

	MV::initializeFilesystem();
	initializeData();
	initializeWindow();
}

void Game::initializeData() {
	
}

void Game::initializeWindow(){
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	data.managers().renderer.//makeHeadless().
		window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!data.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit);

	MV::AudioPlayer::instance()->initAudio();
	mouse.update();

	MV::FontDefinition::make(data.managers().textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(data.managers().textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(data.managers().textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	if (!data.managers().renderer.headless()) {
		data.managers().textures.assemblePacks("Assets/Atlases", &data.managers().renderer);
		data.managers().textures.files("Assets/Map");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::MouseState& a_mouse, LocalData& a_data)
	localPlayer = std::make_shared<Player>();
	localPlayer->name = "Dervacor";
	localPlayer->loadout.buildings = {"life", "life", "life", "life", "life", "life", "life", "life"};
	localPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };

	localPlayer->wallet.add(Wallet::CurrencyType::SOFT, 5000);


	{
		std::ofstream stream("playerExample.json");
		cereal::JSONOutputArchive archive(stream);
		archive(localPlayer);
	}

	auto enemyPlayer = std::make_shared<Player>();
	enemyPlayer->name = "Jai";
	enemyPlayer->loadout.buildings = { "life", "life", "life", "life", "life", "life", "life", "life" };
	enemyPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };
	enemyPlayer->wallet.add(Wallet::CurrencyType::SOFT, 5000);

	instance = std::make_unique<GameInstance>(localPlayer, enemyPlayer, mouse, data);
}

bool Game::update(double dt) {
	lastUpdateDelta = dt;
	data.managers().pool.run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!data.managers().renderer.handleEvent(event) && (!instance || (instance && !instance->handleEvent(event)))){
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
	mouse.update();
}

void Game::render() {
	data.managers().renderer.clearScreen();
	if (instance) {
		instance->update(lastUpdateDelta);
	}
	data.managers().renderer.updateScreen();
}

