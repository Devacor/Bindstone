#include "game.h"

void quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game() :
	mainScene(MV::Scene::Node::make(&renderer)),
	textLibrary(&renderer),
	testShape(MV::Scene::Rectangle::make(&renderer)),
	done(false),
	//testBox(&textLibrary, "blue", MV::Size<>(200, 120)),
	angleIncrement(0, 0, .005){

	initializeWindow();

	auto clipped = mainScene->make<MV::Scene::Clipped>("clipped", MV::Size<>(20, 200));
	//auto clipped = mainScene->make<MV::Scene::Node>("clipped");
	clipped->add("catapult", initializeCatapultScene());
	//mainScene->add("textbox", initializeTextScene());
	//mainScene->get("clipped")->placeAt(MV::Point<>(-clipped->getLocalAABB().getSize().width / 2.0, 0));
	//mainScene->placeAt(MV::Point<>(renderer.world().width() / 2, renderer.world().height() / 2));

	mainScene->make<MV::Scene::Node>("container");
	mainScene->get<MV::Scene::Node>("container")->placeAt(MV::Point<>(20, 80));
	std::shared_ptr<MV::Scene::Rectangle> pattern = mainScene->get<MV::Scene::Node>("container")->make<MV::Scene::Rectangle>("pattern");
	pattern->setTexture(textures.getFileTexture("Assets/Images/patternTest1.png")->makeHandle());

	testShape->setColor(MV::Color(1, 0, 1, .25));

	std::cout << "RWindow: " << renderer.window().size() << std::endl << "RWorld: " << renderer.world().size() << std::endl << "-----" << std::endl;

	std::cout << "Screen: " << pattern->getScreenAABB() << std::endl << "World: " << pattern->getWorldAABB() << std::endl << "Local: " << pattern->getLocalAABB() << std::endl << "---" << std::endl;

	MV::BoxAABB worldAABB = pattern->getScreenAABB();
	MV::BoxAABB screenAABB = pattern->getWorldAABB();

	testShape->setTwoCorners(worldAABB);

	std::cout << "testShapeWorldTest: " << testShape->getWorldAABB() << std::endl;
}

void Game::initializeWindow(){
	MV::initializeFilesystem();
	srand (static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	//renderer.window().windowedMode();
	
	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	atexit(quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();
}

bool Game::passTime( double dt ) {
	//std::cerr << MV::toDegrees(mainScene->get("catapult")->get<MV::DrawNode>()->get("arm")->getRotation().z) << std::endl;
	mainScene->get("clipped")->get("catapult")->get("arm")->incrementRotate(angleIncrement);
	if(MV::toDegrees(mainScene->get("clipped")->get("catapult")->get("arm")->getRotation().z) > 180 ||
		MV::toDegrees(mainScene->get("clipped")->get("catapult")->get("arm")->getRotation().z) < 0){
		angleIncrement*=-1;
	}
	auto pattern = mainScene->get<MV::Scene::Node>("container")->get<MV::Scene::Rectangle>("pattern");
	MV::BoxAABB worldAABB = pattern->getScreenAABB();
	MV::BoxAABB screenAABB = pattern->getWorldAABB();
	MV::BoxAABB localAABB = pattern->getLocalAABB();

	worldAABB.minPoint = renderer.worldFromScreen(MV::castPoint<int>(worldAABB.minPoint));
	worldAABB.maxPoint = renderer.worldFromScreen(MV::castPoint<int>(worldAABB.maxPoint));

	screenAABB.minPoint = MV::castPoint<double>(renderer.screenFromWorld(screenAABB.minPoint));
	screenAABB.maxPoint = MV::castPoint<double>(renderer.screenFromWorld(screenAABB.maxPoint));
	testShape->setTwoCorners(renderer.worldFromScreen(MV::Point<int>(mouse.position.x-5, mouse.position.y-5)), renderer.worldFromScreen(mouse.position));
	//std::cout << std::endl << pattern->localFromWorld(renderer.worldFromScreen(mouse.position));
	if(localAABB.pointContained(pattern->localFromWorld(renderer.worldFromScreen(mouse.position)))){
		pattern->clearTexture();
	}else{
		pattern->setTexture(textures.getFileTexture("Assets/Images/patternTest1.png")->makeHandle());
	}
	return !done;
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
					renderer.window().fullScreenMode();
					break;
				case SDLK_LEFT:
					renderer.window().fullScreenWindowedMode();
					break;
				case SDLK_DOWN:
					//testBox.setScrollPosition(MV::Point<>(0, testBox.getScrollPosition().y + 1));
					//renderer.window().windowedMode().bordered();
					break;
				case SDLK_SPACE:
					renderer.window().allowUserResize();
					break;
				case SDLK_RIGHT:
					renderer.window().lockUserResize();
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
	mainScene->draw();
	testShape->draw();
	renderer.updateScreen();
}

std::shared_ptr<MV::Scene::Node> Game::initializeCatapultScene(){
	auto catapaultScene = MV::Scene::Node::make(&renderer);
	
	auto platformTexture = textures.getFileTexture("Assets/Images/platform.png");
	auto textureHandle = platformTexture->makeHandle();
	auto shape = catapaultScene->make<MV::Scene::Rectangle>("base", MV::Point<>(0, 0), MV::castSize<double>(platformTexture->size()));
	//auto currentTexture = textures.getMainTexture("base"); TEXTURE
	shape->setTexture(textureHandle);
	std::cout << "PT: " << platformTexture->size() << std::endl;
	shape->setSortDepth(4);

	auto armScene = catapaultScene->make<MV::Scene::Node>("arm");
	armScene->placeAt(MV::Point<>(0, -4));
	shape = armScene->make<MV::Scene::Rectangle>("arm");
	auto armTexture = textures.getFileTexture("Assets/Images/spatula.png");
	shape->setTexture(armTexture->makeHandle());
	shape->setSizeAndCornerLocation(MV::Point<>(0, 0), armTexture->size());
	std::cout << "AT: " << armTexture->size() << std::endl;
	shape->setSortDepth(2);

	//yum, hard coding yay
	armScene->setRotateOrigin(MV::Point<>(armTexture->size().width - 62, 25));

	armScene->setSortDepth(2);

	shape = armScene->make<MV::Scene::Rectangle>("rock");
	auto rockTexture = textures.getFileTexture("Assets/Images/rock.png");
	shape->setTexture(rockTexture->makeHandle());
	shape->setSizeAndCornerLocation(MV::Point<>(0, 0), rockTexture->size());
	shape->placeAt(MV::Point<>(7, -9));
	std::cout << "RT: " << rockTexture->size() << std::endl;
	shape->setSortDepth(1);

	shape = catapaultScene->make<MV::Scene::Rectangle>("joint");
	auto jointTexture = textures.getFileTexture("Assets/Images/joint.png");
	shape->setTexture(jointTexture->makeHandle());
	shape->setSizeAndCornerLocation(MV::Point<>(0, 0), jointTexture->size());
	std::cout << "JT: " << jointTexture->size() << std::endl;
	shape->setSortDepth(3);

	catapaultScene->scale(.5);
	return catapaultScene;
}

/*std::shared_ptr<MV::Scene::Node> Game::initializeTextScene() {
	textLibrary.loadFont("blue", 24, "Assets/Fonts/bluehigh.ttf");
	testBox.setText(MV::stringToWide("This\nis\na\nclear and\nobvious\ntest!"));
	return testBox.scene();
}*/
