#include "game.h"

void quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game() :
	mainScene(&renderer),
	textLibrary(&renderer),
	testShape(&renderer),
	done(false),
	angleIncrement(0, 0, .005){

	initializeWindow();
	loadCatapultTextures();
	loadPatternTextures();
	mainScene.add(initializeCatapultScene(), "catapult");
	mainScene.get("catapult")->placeAt(MV::Point(-mainScene.get("catapult")->getLocalAABB().getWidth() / 2.0, 0));
	mainScene.placeAt(MV::Point(renderer.world().width() / 2, renderer.world().height() / 2));

	mainScene.make<MV::DrawNode>("container");
	mainScene.get<MV::DrawNode>("container")->placeAt(MV::Point(20, 80));
	std::shared_ptr<MV::DrawRectangle> pattern = mainScene.get<MV::DrawNode>("container")->make<MV::DrawRectangle>("pattern");
	MV::AssignTextureToRectangle(*pattern, textures.getMainTexture("pattern1"));

	testShape.setColor(MV::Color(1, 0, 1, .25));

	std::cout << "RWindow: " << renderer.window().size() << std::endl << "RWorld: " << renderer.world().size() << std::endl << "-----" << std::endl;

	std::cout << "Screen: " << pattern->getScreenAABB() << std::endl << "World: " << pattern->getWorldAABB() << std::endl << "Local: " << pattern->getLocalAABB() << std::endl << "---" << std::endl;

	MV::BoxAABB worldAABB = pattern->getScreenAABB();
	MV::BoxAABB screenAABB = pattern->getWorldAABB();
	worldAABB.minPoint = renderer.worldFromScreen(worldAABB.minPoint);
	worldAABB.maxPoint = renderer.worldFromScreen(worldAABB.maxPoint);

	screenAABB.minPoint = renderer.screenFromWorld(screenAABB.minPoint);
	screenAABB.maxPoint = renderer.screenFromWorld(screenAABB.maxPoint);
	std::cout << "Screen: " << screenAABB << std::endl << "World: " << worldAABB << std::endl << "Local: " << pattern->getLocalAABB() << std::endl;

	testShape.setTwoCorners(worldAABB);

	std::cout << "testShapeWorldTest: " << testShape.getWorldAABB() << std::endl;
}

void Game::initializeWindow(){
	MV::initializeFilesystem();
	srand (static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(480, 320);

	//renderer.window().windowedMode();
	
	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	atexit(quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();
}

bool Game::passTime( double dt ) {
	/*std::cerr << MV::toDegrees(mainScene.get("catapult")->getThis<MV::DrawShape>()->get("arm")->getRotation().z) << std::endl;
	mainScene.get("catapult")->getThis<MV::DrawShape>()->get("arm")->incrementRotate(angleIncrement);
	if(MV::toDegrees(mainScene.get("catapult")->getThis<MV::DrawShape>()->get("arm")->getRotation().z) > 180 ||
		MV::toDegrees(mainScene.get("catapult")->getThis<MV::DrawShape>()->get("arm")->getRotation().z) < 0){
		angleIncrement*=-1;
	}*/
	auto pattern = mainScene.get<MV::DrawNode>("container")->get<MV::DrawRectangle>("pattern");
	MV::BoxAABB worldAABB = pattern->getScreenAABB();
	MV::BoxAABB screenAABB = pattern->getWorldAABB();
	MV::BoxAABB localAABB = pattern->getLocalAABB();

	worldAABB.minPoint = renderer.worldFromScreen(worldAABB.minPoint);
	worldAABB.maxPoint = renderer.worldFromScreen(worldAABB.maxPoint);

	screenAABB.minPoint = renderer.screenFromWorld(screenAABB.minPoint);
	screenAABB.maxPoint = renderer.screenFromWorld(screenAABB.maxPoint);
	testShape.setTwoCorners(renderer.worldFromScreen(MV::Point(mouse.position.x-5, mouse.position.y-5)), renderer.worldFromScreen(mouse.position));
	//std::cout << std::endl << pattern->localFromWorld(renderer.worldFromScreen(mouse.position));
	if(localAABB.pointContained(pattern->localFromWorld(renderer.worldFromScreen(mouse.position)))){
		pattern->removeTexture();
	}else{
		MV::AssignTextureToRectangle(*pattern, textures.getMainTexture("pattern1"));
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
					renderer.window().windowedMode().bordered();
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
	mainScene.draw();
	testShape.draw();
	renderer.updateScreen();
}

void Game::loadCatapultTextures(){
	textures.loadTexture("base", "Assets/Images/platform.png");
	textures.loadTexture("rock", "Assets/Images/rock.png");
	textures.loadTexture("arm", "Assets/Images/spatula.png");
	textures.loadTexture("joint", "Assets/Images/joint.png");
}

void Game::loadPatternTextures(){
	textures.loadTexture("pattern1", "Assets/Images/patternTest1.png");
}

std::shared_ptr<MV::DrawNode> Game::initializeCatapultScene(){
	auto catapaultScene = std::make_shared<MV::DrawNode>(&renderer);
	
	auto shape = catapaultScene->make<MV::DrawRectangle>("base");
	auto currentTexture = textures.getMainTexture("base");
	shape->setSizeAndCornerLocation(MV::Point(0, 0), currentTexture->width, currentTexture->height);
	shape->setTexture(&currentTexture->texture);
	MV::AssignTextureToRectangle(*shape, currentTexture);
	shape->setSortDepth(4);

	auto armScene = catapaultScene->make<MV::DrawNode>("arm");
	armScene->placeAt(MV::Point(0, -4));
	shape = armScene->make<MV::DrawRectangle>("arm");
	currentTexture = textures.getMainTexture("arm");
	shape->setSizeAndCornerLocation(MV::Point(0, 0), currentTexture->width, currentTexture->height);
	shape->setTexture(&currentTexture->texture);
	shape->setSortDepth(2);

	//yum, hard coding yay
	armScene->setRotateOrigin(MV::Point(currentTexture->width-62, 25));

	armScene->setSortDepth(2);

	shape = armScene->make<MV::DrawRectangle>("rock");
	currentTexture = textures.getMainTexture("rock");
	shape->setSizeAndCornerLocation(MV::Point(0, 0), currentTexture->width, currentTexture->height);
	shape->setTexture(&currentTexture->texture);
	shape->placeAt(MV::Point(7, -9));
	shape->setSortDepth(1);

	shape = catapaultScene->make<MV::DrawRectangle>("joint");
	currentTexture = textures.getMainTexture("joint");
	shape->setSizeAndCornerLocation(MV::Point(0, 0), currentTexture->width, currentTexture->height);
	shape->setTexture(&currentTexture->texture);
	shape->setSortDepth(3);

	catapaultScene->scale(.5);
	return catapaultScene;
}