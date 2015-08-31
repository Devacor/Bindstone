#include "diggerGame.h"
#include "Editor/editorFactories.h"

void sdl_quit_3(void) {
	SDL_Quit();
	TTF_Quit();
}

DiggerGame::DiggerGame(MV::ThreadPool* a_pool, MV::Draw2D* a_renderer) :
	pool(a_pool),
	renderer(a_renderer),
	textLibrary(*a_renderer),
	done(false) {

	initializeWindow();

}

void DiggerGame::initializeWindow() {
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	//iphone 6 resolution
	MV::Size<> worldSize(375, 667);
	MV::Size<int> windowSize(375, 667);

	renderer->window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!renderer->initialize(windowSize, worldSize)) {
		exit(0);
	}
	renderer->loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer->loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit_3);

	AudioPlayer::instance()->initAudio();
	mouse.update();

// 	std::ifstream stream("clicker.scene");
// 
// 	cereal::JSONInputArchive archive(stream);
// 
// 	archive.add(
// 		cereal::make_nvp("mouse", &mouse),
// 		cereal::make_nvp("renderer", renderer),
// 		cereal::make_nvp("textLibrary", &textLibrary),
// 		cereal::make_nvp("pool", pool)
// 	);
// 
// 	archive(cereal::make_nvp("scene", worldScene));

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	InitializeWorldScene();
}
void DiggerGame::InitializeWorldScene() {
	worldScene = MV::Scene::Node::make(*renderer);
	worldScene->scale(1.0f);
	world = std::make_shared<DiggerWorld>(worldScene, textures);

}

bool DiggerGame::update(double dt) {
	lastUpdateDelta = dt;
	pool->run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void DiggerGame::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!renderer->handleEvent(event)) {
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
					world->thing->body().impulse({ 0.0f, -10000.0f });
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
					//world->thing->body().impulse({ 0.0f, -10000.0f });
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
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	if (state[SDL_SCANCODE_RIGHT]) {
		auto currentVelocity = world->thing->body().velocity();
		world->thing->body().velocity({300.0f, currentVelocity.y});
	}
	if (state[SDL_SCANCODE_LEFT]) {
		auto currentVelocity = world->thing->body().velocity();
		world->thing->body().velocity({ -300.0f, currentVelocity.y });
	}
	mouse.update();
}

void DiggerGame::render() {
	renderer->clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	//testShape->draw();
	renderer->updateScreen();
}
