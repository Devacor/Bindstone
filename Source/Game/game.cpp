#include "game.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(MV::ThreadPool* a_pool, MV::Draw2D* a_renderer) :
	pool(a_pool),
	renderer(a_renderer),
	textLibrary(*a_renderer),
	done(false){

	initializeWindow();

}

void Game::initializeWindow(){
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	renderer->window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!renderer->initialize(windowSize, worldSize)) {
		exit(0);
	}
	renderer->loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer->loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	std::ifstream stream("map.scene");

	cereal::JSONInputArchive archive(stream);

	archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", renderer),
		cereal::make_nvp("textLibrary", &textLibrary),
		cereal::make_nvp("pool", pool)
	);

	archive(cereal::make_nvp("scene", worldScene));

	mouse.onLeftMouseDown.connect("initDrag", [&](MV::MouseState& a_mouse) {
		a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = mouse.onMove.connect("inDrag", [&](MV::MouseState& a_mouse2) {
				worldScene->translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
			});
			mouse.onLeftMouseUp.connect("cancelDrag", [=](MV::MouseState& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
			});
		}, []() {}, "MapDrag"));
	});

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	textures.assemblePacks("Assets/Atlases", renderer);
	textures.files("Assets/Map");

	pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();

	for (int i = 1; i < 9; ++i) {
		auto treeButton = worldScene->get("left_" + std::to_string(i))->attach<MV::Scene::Clickable>(mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
		treeButton->onAccept.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
			spawnCreature(a_self->worldBounds().bottomRightPoint());
		});
	}
}

void Game::spawnCreature(const MV::Point<> &a_position) {
	auto voidTexture = textures.pack("VoidGuy")->handle(0);
	auto creatureNode = pathMap->owner()->make(MV::guid("Creature_"));
	creatureNode->attach<MV::Scene::Sprite>()->texture(voidTexture)->size(MV::cast<MV::PointPrecision>(voidTexture->bounds().size()));
	creatureNode->attach<MV::Scene::PathAgent>(pathMap.self(), pathMap->gridFromLocal(pathMap->owner()->localFromWorld(a_position)))->
		gridSpeed(4.0f)->
		gridGoal(pathMap->gridFromLocal(pathMap->owner()->localFromWorld(worldScene->get("RightWell")->worldFromLocal(MV::Point<>()))))->
		onArrive.connect("!", [](std::shared_ptr<MV::Scene::PathAgent> a_self){
			std::weak_ptr<MV::Scene::Node> self = a_self->owner();
			a_self->owner()->task().then("Countdown", [=](const MV::Task& a_task, double a_dt) mutable {
				if (a_task.elapsed() > 4.0f) {
					self.lock()->removeFromParent();
					return true;
				}
				return false;
			});
		});
	
	std::weak_ptr<MV::Scene::Node> weakCreatureNode{ creatureNode };
	creatureNode->task().also("UpdateZOrder", [=](const MV::Task &a_self, double a_dt) {
		weakCreatureNode.lock()->depth(weakCreatureNode.lock()->position().y);
		return false;
	});
}

bool Game::update(double dt) {
	lastUpdateDelta = dt;
	pool->run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!renderer->handleEvent(event)){
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
			case SDL_MOUSEWHEEL:
				handleScroll(event.wheel.y);
				break;
			}
		}
	}
	mouse.update();
}

void Game::render() {
	renderer->clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	//testShape->draw();
	renderer->updateScreen();
}

void Game::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (worldScene->scale().x + screenScale.x > .2f) {
		auto originalScreenPosition = worldScene->localFromScreen(mouse.position()) * (MV::toPoint(screenScale));
		worldScene->addScale(screenScale);
		worldScene->translate(originalScreenPosition * -1.0f);
	}
}