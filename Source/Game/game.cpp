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

	data.managers().renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!data.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	data.managers().renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	data.managers().renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	data.managers().textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	data.managers().textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	data.managers().textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	data.managers().textures.assemblePacks("Assets/Atlases", &data.managers().renderer);
	data.managers().textures.files("Assets/Map");
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::MouseState& a_mouse, LocalData& a_data)
	instance = std::make_unique<GameInstance>(data.player(), std::make_shared<Player>(), mouse, data);
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
