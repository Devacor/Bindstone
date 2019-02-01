#include "diggerGame.h"
#include "Editor/editorFactories.h"

void sdl_quit_3(void) {
	SDL_Quit();
	TTF_Quit();
}

DiggerGame::DiggerGame(Managers &a_managers) :
	managers(a_managers),
	done(false) {
	managers.services.connect(&mouse);
	initializeWindow();
}

void DiggerGame::initializeWindow() {
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
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
void DiggerGame::InitializeWorldScene() {
	worldScene = MV::Scene::Node::make(managers.renderer);
	worldScene->scale(1.0f);
	world = std::make_shared<DiggerWorld>(worldScene, managers.textures, mouse);
	world->thing->onContactStart.connect("land", [&](size_t a_id, MV::Scene::CollisionParameters a_parameters, const MV::Point<> &a_normal) {
		if (a_parameters.fixtureA->id() == "foot") {
			world->thing->body().velocity({ world->thing->body().velocity().x, 0.0f });
			grounded++;
		}
	});
	world->thing->onContactEnd.connect("jump", [&](size_t a_id, MV::Scene::CollisionParameters a_parameters) {
		if (a_parameters.fixtureA->id() == "foot") {
			grounded--;
		}
	});
	world->thing->onContactKilled.connect("jump", [&](bool a_usDying, size_t a_id, MV::Scene::CollisionParameters a_parameters) {
		if (!a_usDying && a_parameters.fixtureA->id() == "foot") {
			grounded--;
		}
	});
}

bool DiggerGame::update(double dt) {
	lastUpdateDelta = dt;
	
	managers.pool.run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void DiggerGame::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!managers.renderer.handleEvent(event)) {
			switch (event.type) {
			case SDL_QUIT:
				done = true;
				break;
			case SDL_MOUSEWHEEL:
				handleScroll(event.wheel.y);
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					done = true;
				} else if (event.key.keysym.sym == SDLK_DOWN) {
					std::ofstream outstream("digger.scene");
					cereal::JSONOutputArchive outarchive(outstream);
					outarchive(cereal::make_nvp("scene", worldScene));
				} else if (event.key.keysym.sym == SDLK_SPACE) {
					
					//world->thing->body().impulse({ 0.0f, -10000.0f });
					//renderer.window().allowUserResize();
					std::ifstream stream("digger.scene");

					cereal::UserDataAdapter<MV::Services, cereal::JSONInputArchive> archive(managers.services, stream);

					archive(cereal::make_nvp("scene", worldScene));
					world->thing = worldScene->get("thing")->component<MV::Scene::Collider>();
				}
				break;
			}
		}
	}
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	if (state[SDL_SCANCODE_RIGHT]) {
		if (grounded == 0) {
			world->thing->body().velocity({ 160.0f, world->thing->body().velocity().y });
		}
	}
	if (state[SDL_SCANCODE_LEFT]) {
		if (grounded == 0) {
			world->thing->body().velocity({ -160.0f, world->thing->body().velocity().y });
		}
	}
	if (state[SDL_SCANCODE_UP]) {
		if (grounded > 0 && jumpTimer.check() > .2f) {
			jumpTimer.reset();
			world->thing->body().velocity({ world->thing->body().velocity().x, 0.0f });
			world->thing->body().impulse({ 0.0f, -190.0f });
		}
	}
	if (state[SDL_SCANCODE_RIGHT] && !state[SDL_SCANCODE_LEFT]) {
		world->thing->rotationJoint()->speed(-12.0f);
	} else if (state[SDL_SCANCODE_LEFT] && !state[SDL_SCANCODE_RIGHT]) {
		world->thing->rotationJoint()->speed(12.0f);
	} else {
		world->thing->rotationJoint()->speed(0.0f);
	}
	mouse.update();
}

void DiggerGame::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (worldScene->scale().x + screenScale.x > .2f) {
		worldScene->addScale(screenScale);
	}
}

void DiggerGame::render() {
	worldScene->worldPosition({ 0.0f, 0.0f });
	//worldScene->worldPosition((world->thing->physicsWorldPosition() - ((MV::toPoint(managers.renderer.world().size() / 2.0f) - MV::point(12.0f, 24.0f)))) * -1.0f);

	managers.renderer.clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	managers.renderer.updateScreen();
}
