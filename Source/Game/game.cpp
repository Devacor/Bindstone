#include "game.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game() :
	textLibrary(renderer),
	done(false){

	initializeWindow();

}

void Game::initializeWindow(){
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	std::ifstream stream("map.scene");

	cereal::JSONInputArchive archive(stream);

	archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &renderer),
		cereal::make_nvp("textLibrary", &textLibrary),
		cereal::make_nvp("pool", &pool)
	);

	archive(cereal::make_nvp("scene", worldScene));

	mouse.onLeftMouseDown.connect("initDrag", [&](MV::MouseState& a_mouse) {
		a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = mouse.onMove.connect("inDrag", [&](MV::MouseState& a_mouse2) {
				worldScene->translate(MV::cast<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
			});
			mouse.onLeftMouseUp.connect("cancelDrag", [=](MV::MouseState& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
			});
		}, []() {}));
	});

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	textures.assemblePacks("Assets/Atlases", &renderer);
	textures.files("Assets/Map");
}



bool Game::update(double dt) {
	lastUpdateDelta = dt;
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!renderer.handleEvent(event)){
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
	renderer.clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	//testShape->draw();
	renderer.updateScreen();
}
