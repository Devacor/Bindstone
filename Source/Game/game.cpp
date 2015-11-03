#include "game.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game(Managers& a_managers) :
	managers(a_managers),
	done(false){

	initializeWindow();
}

void Game::initializeWindow(){
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	managers.renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!managers.renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	managers.renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	managers.renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	managers.mouse.update();

	managers.textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	managers.textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	managers.textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	managers.textures.assemblePacks("Assets/Atlases", &managers.renderer);
	managers.textures.files("Assets/Map");

	instance = std::make_unique<GameInstance>(managers, std::make_shared<Player>(), std::make_shared<Player>(), Constants());
// 	for (int i = 1; i < 9; ++i) {
// 		auto treeButton = worldScene->get("left_" + std::to_string(i))->attach<MV::Scene::Clickable>(managers.mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
// 		treeButton->onAccept.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
// 			spawnCreature(a_self->worldBounds().bottomRightPoint());
// 		});
// 	}
}

void Game::spawnCreature(const MV::Point<> &a_position) {
	/*
	auto voidTexture = managers.textures.pack("VoidGuy")->handle(0);
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
	*/
//  	script.eval(R"(
// 			{
//  			auto newNode = worldScene.make()
//  			newNode.position(Point(10, 10, 10))
//  			auto spriteDude = newNode.attachSprite()
//  			spriteDude.size(Size(128, 128))
//				spriteDude.texture(textures.pack("VoidGuy").handle(0))
// 			}
//  	)");
}

bool Game::update(double dt) {
	lastUpdateDelta = dt;
	managers.pool.run();
	if (!done && instance) {
		done = instance->update(dt);
	}
	if (done) {
		done = false;
		return false;
	}
	handleInput();
	return true;
}

void Game::handleInput() {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!managers.renderer.handleEvent(event) && (!instance || (instance && !instance->handleEvent(event)))){
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
	managers.mouse.update();
}

void Game::render() {
	managers.renderer.clearScreen();
	if (instance) {
		instance->update(lastUpdateDelta);
	}
	managers.renderer.updateScreen();
}

void GameInstance::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (worldScene->scale().x + screenScale.x > .2f) {
		auto originalScreenPosition = worldScene->localFromScreen(managers.mouse.position()) * (MV::toPoint(screenScale));
		worldScene->addScale(screenScale);
		worldScene->translate(originalScreenPosition * -1.0f);
	}
}

GameInstance::GameInstance(Managers& a_managers, const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const Constants& a_constants) :
	managers(a_managers),
	left(a_leftPlayer, a_constants),
	right(a_rightPlayer, a_constants),
	script(chaiscript::Std_Lib::library()) {

	MV::TexturePoint::hook(script);
	MV::Color::hook(script);
	MV::Size<MV::PointPrecision>::hook(script);
	MV::Size<int>::hook(script, "i");
	MV::Point<MV::PointPrecision>::hook(script);
	MV::Point<int>::hook(script, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(script);
	MV::BoxAABB<int>::hook(script, "i");

	MV::TexturePack::hook(script);
	MV::TextureDefinition::hook(script);
	MV::FileTextureDefinition::hook(script);
	MV::TextureHandle::hook(script);
	MV::SharedTextures::hook(script);

	Wallet::hook(script);

	MV::PathNode::hook(script);
	MV::NavigationAgent::hook(script);

	MV::Scene::Node::hook(script);
	MV::Scene::Component::hook(script);
	MV::Scene::Drawable::hook(script);
	MV::Scene::Sprite::hook(script);
	MV::Scene::Text::hook(script);
	MV::Scene::PathMap::hook(script);
	MV::Scene::PathAgent::hook(script);

	std::ifstream stream("map.scene");

	cereal::JSONInputArchive archive(stream);

	archive.add(
		cereal::make_nvp("mouse", &managers.mouse),
		cereal::make_nvp("renderer", &managers.renderer),
		cereal::make_nvp("textLibrary", &managers.textLibrary),
		cereal::make_nvp("pool", &managers.pool)
		);

	archive(cereal::make_nvp("scene", worldScene));

	managers.mouse.onLeftMouseDown.connect(MV::guid("initDrag"), [&](MV::MouseState& a_mouse) {
		a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, { 10 }, [&]() {
			auto signature = managers.mouse.onMove.connect(MV::guid("inDrag"), [&](MV::MouseState& a_mouse2) {
				worldScene->translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
			});
			auto cancelId = MV::guid("cancelDrag");
			managers.mouse.onLeftMouseUp.connect(cancelId, [=](MV::MouseState& a_mouse2) {
				a_mouse2.onMove.disconnect(signature);
				a_mouse2.onLeftMouseUp.disconnect(cancelId);
			});
		}, []() {}, "MapDrag"));
	});
	for (int i = 1; i < 9; ++i) {
		auto treeButton = worldScene->get("left_" + std::to_string(i))->attach<MV::Scene::Clickable>(managers.mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
		treeButton->onAccept.connect("TappedBuilding", [&](std::shared_ptr<MV::Scene::Clickable> a_self) {
			//spawnCreature(a_self->worldBounds().bottomRightPoint());
		});
	}
	pathMap = worldScene->get("PathMap")->component<MV::Scene::PathMap>();
}

bool GameInstance::update(double a_dt) {
	worldScene->drawUpdate(static_cast<float>(a_dt));
	return false;
}

bool GameInstance::handleEvent(const SDL_Event &a_event) {
	if (a_event.type == SDL_MOUSEWHEEL) {
		handleScroll(a_event.wheel.y);
	}
	return false;
}
