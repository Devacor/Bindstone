#include "bankProject.h"
#include "Editor/editorFactories.h"

void sdl_quit_3(void) {
	SDL_Quit();
	TTF_Quit();
}

ExampleGame::ExampleGame(Managers &a_managers) :
	managers(a_managers),
	done(false) {
	managers.services.connect(&mouse);
	initializeWindow();
}

void ExampleGame::initializeWindow() {
	MV::initializeFilesystem();
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	//iphone 6 resolution
	MV::Size<> worldSize(1334, 750);
	MV::Size<int> windowSize(1334, 750);

	managers.renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!managers.renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit_3);

	MV::AudioPlayer::instance()->initAudio();
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

	MV::FontDefinition::make(managers.textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	InitializeWorldScene();


}
void ExampleGame::InitializeWorldScene() {
	worldScene = MV::Scene::Node::make(managers.renderer);
	worldScene->scale(1.0f);
	world = std::make_shared<ExampleWorld>(worldScene, managers.textures, mouse);
}

bool ExampleGame::update(double dt) {
	lastUpdateDelta = dt;
	
	managers.pool.run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void ExampleGame::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!managers.renderer.handleEvent(event)) {
			switch (event.type) {
			case SDL_QUIT:
				done = true;
				return;
				break;
			case SDL_MOUSEWHEEL:
				handleScroll(event.wheel.y);
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					done = true;
					return;
				}
				break;
			}
		}
	}
	world->controlPlayer();
	mouse.update();
}

void ExampleGame::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (worldScene->scale().x + screenScale.x > .2f) {
		worldScene->addScale(screenScale);
	}
}

void ExampleGame::render() {
	worldScene->worldPosition({ 0.0f, 0.0f });
	//worldScene->worldPosition((world->thing->physicsWorldPosition() - ((MV::toPoint(managers.renderer.world().size() / 2.0f) - MV::point(12.0f, 24.0f)))) * -1.0f);

	managers.renderer.clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	managers.renderer.updateScreen();
}

ExampleWorld::ExampleWorld(const std::shared_ptr<MV::Scene::Node>& a_node, MV::SharedTextures& a_sharedTextures, MV::TapDevice& a_mouse) :
	background(a_node->make("Background")),
	environment(a_node->make("Environment")),
	foreground(a_node->make("Foreground")),
	mouse(a_mouse),
	sharedTextures(a_sharedTextures) {

	loadTextures();

	//add background texture here.

	//load player here
}

void ExampleWorld::controlPlayer() {
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if (state[SDL_SCANCODE_RIGHT]) {

	}
	if (state[SDL_SCANCODE_LEFT]) {

	}
	if (state[SDL_SCANCODE_UP]) {

	}
	if (state[SDL_SCANCODE_DOWN]) {

	}
}
