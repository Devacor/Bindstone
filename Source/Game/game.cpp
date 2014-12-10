#include "game.h"

void quit(void){
	SDL_Quit();
	TTF_Quit();
}

Game::Game() :
	mainScene(MV::Scene::Node::make(renderer)),
	textLibrary(renderer),
	testShape(MV::Scene::Node::make(renderer, "TestShape")->attach<MV::Scene::Sprite>()),
	done(false),
	angleIncrement(0, 0, .005f),
	testBox(MV::Scene::Node::make(renderer, "TestBox")->attach<MV::Scene::Text>(textLibrary, MV::Size<>(100.0f, 50.0f), "blue")){

	initializeWindow();
	//auto clipped = mainScene->make<MV::Scene::Clipped>("clipped", MV::Size<>(200, 200));
	//auto clipped = mainScene->make<MV::Scene::Node>("clipped");
	auto clipped = mainScene->make("clipped")->attach<MV::Scene::Clickable>(mouse);
	clipped->owner()->add(initializeCatapultScene());
	mainScene->add(initializeTextScene());
	auto pointtest = MV::Point<>(-clipped->bounds().size().width / 2.0f, 0);
	//mainScene->get("clipped")->placeAt(MV::Point<>(-clipped->getLocalAABB().getSize().width / 2.0, 0));
	mainScene->position(MV::Point<>(renderer.world().width() / 2.0f, renderer.world().height() / 2.0f));

	auto containerNode = mainScene->make("container")->position(MV::Point<>(20, 80));
	std::shared_ptr<MV::Scene::Sprite> pattern = containerNode->make("pattern")->attach<MV::Scene::Sprite>();
	pattern->texture(textures.file("Assets/Images/patternTest1.png")->makeHandle());

	testShape->color(MV::Color(1, 0, 1, .25));

	std::cout << "RWindow: " << renderer.window().size() << std::endl << "RWorld: " << renderer.world().size() << std::endl << "-----" << std::endl;

	std::cout << "Screen: " << pattern->screenBounds() << std::endl << "World: " << pattern->worldBounds() << std::endl << "Local: " << pattern->bounds() << std::endl << "---" << std::endl;

	MV::BoxAABB<int> screenAABB = pattern->screenBounds();
	MV::BoxAABB<> worldAABB = pattern->worldBounds();

	testShape->bounds(worldAABB);

	std::cout << "testShapeWorldTest: " << testShape->worldBounds() << std::endl;
	hookUpInput();

	saveTest();
	saveTest();
}

void Game::initializeWindow(){
	MV::Matrix testMat(4);
	testMat.access(1, 1) = 1.0;
	testMat[1][2] = 2.0;
	testMat.access(1, 3) = 1.0;
	testMat.print();

	MV::initializeFilesystem();
	srand (static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);
	//MV::Size<int> worldSize(480, 320);

	renderer.window().windowedMode();
	
	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	atexit(quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();
}



bool Game::update(double dt) {
	//std::cerr << MV::toDegrees(mainScene->get("catapult")->get<MV::DrawNode>()->get("arm")->getRotation().z) << std::endl;
	/*
	mainScene->get("clipped")->get("catapult")->get("arm")->incrementRotate(angleIncrement);
	if(MV::toDegrees(mainScene->get("clipped")->get("catapult")->get("arm")->getRotation().z) > 180 ||
		MV::toDegrees(mainScene->get("clipped")->get("catapult")->get("arm")->getRotation().z) < 0){
		angleIncrement *= -1;
	}
	auto pattern = mainScene->get<MV::Scene::Node>("container")->get<MV::Scene::Sprite>("pattern");

	mainScene->get("clipped")->depth(0);

	auto testItem = mainScene->make<MV::Scene::Sprite>("testThing");
	std::vector<MV::Point<>> points = {mainScene->get("clipped")->getWorldAABB().minPoint, mainScene->get("clipped")->getWorldAABB().maxPoint};
	points = testItem->localFromWorld(points);
	testItem->bounds({points[0], points[1]});
	testItem->setColor(MV::Color(1.0, 0.0, 0.0, .25));
	testItem->depth(-100);

	auto testItem2 = mainScene->make<MV::Scene::Sprite>("testThing2");
	std::vector<MV::Point<>> points2 = {mainScene->get("clipped")->get("catapult")->get("arm")->getWorldAABB().minPoint, mainScene->get("clipped")->get("catapult")->get("arm")->getWorldAABB().maxPoint};
	points2 = testItem2->localFromWorld(points2);
	testItem2->bounds({points2[0], points2[1]});

	testItem2->setColor(MV::Color(0.0, 0.0, 1.0, .5));
	testItem2->depth(101);
	
	*/
	//*/
	/*MV::BoxAABB<int> screenAABB = pattern->getScreenAABB();
	MV::BoxAABB<> worldAABB = pattern->getWorldAABB();
	MV::BoxAABB<> localAABB = pattern->getLocalAABB();

	worldAABB.minPoint = renderer.worldFromScreen(MV::cast<int>(worldAABB.minPoint));
	worldAABB.maxPoint = renderer.worldFromScreen(MV::cast<int>(worldAABB.maxPoint));

	screenAABB.minPoint = MV::cast<PointPrecision>(renderer.screenFromWorld(screenAABB.minPoint));
	screenAABB.maxPoint = MV::cast<PointPrecision>(renderer.screenFromWorld(screenAABB.maxPoint));
	testShape->bounds({renderer.worldFromScreen(MV::Point<int>(mouse.position.x - 5, mouse.position.y - 5)), renderer.worldFromScreen(mouse.position)}); 
	//std::cout << std::endl << pattern->localFromWorld(renderer.worldFromScreen(mouse.position));
	if(localAABB.pointContained(pattern->localFromWorld(renderer.worldFromScreen(mouse.position)))){
		pattern->clearTexture();
	}else{
		pattern->setTexture(textures.getFileTexture("Assets/Images/patternTest1.png")->makeHandle());
	}*/
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
	static int staticint = 0;
	renderer.clearScreen();
	mainScene->drawUpdate(0.0f);
	//testShape->draw();
	renderer.updateScreen();
	if(staticint++ < 100)
		saveTest();
}

void Game::hookUpInput(){
	/*armInputHandles.drag = armScene->onDrag.connect([](std::shared_ptr<MV::Scene::Clickable> armScene, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		armScene->translate(MV::cast<MV::PointPrecision>(deltaPosition));
	});*/
}

std::shared_ptr<MV::Scene::Node> Game::initializeCatapultScene(){
	static int counterthing = 0;
	auto catapaultScene = MV::Scene::Node::make(renderer);

	auto platformTexture = textures.file("Assets/Images/platform.png");
	auto textureHandle = platformTexture->makeHandle();
	auto shape = catapaultScene->make("base")->attach<MV::Scene::Sprite>()->size(MV::cast<MV::PointPrecision>(platformTexture->size()));
	//auto currentTexture = textures.getMainTexture("base"); TEXTURE
	shape->texture(textureHandle);
	std::cout << "PT: " << platformTexture->size() << std::endl;
	shape->owner()->depth(4);
	
	armScene = catapaultScene->make("clickArm")->attach<MV::Scene::Clickable>(mouse)->size(MV::Size<>(60, 60));
	armScene->clickDetectionType(MV::Scene::Clickable::BoundsType::LOCAL);
	armScene->owner()->position(MV::Point<>(0, -4));

	shape = armScene->owner()->make("arm")->attach<MV::Scene::Sprite>();
	auto armTexture = textures.file("Assets/Images/spatula.png");
	shape->texture(armTexture->makeHandle());
	shape->size(MV::cast<MV::PointPrecision>(armTexture->size()));
	std::cout << "AT: " << armTexture->size() << std::endl;
	shape->owner()->depth(2);

	//yum, hard coding yay!
	//armScene->rotationOrigin(MV::Point<>(static_cast<MV::PointPrecision>(armTexture->size().width) - 62, 25));

	armScene->owner()->depth(2);

	shape = armScene->owner()->make("rock")->attach<MV::Scene::Sprite>();
	auto rockTexture = textures.file("Assets/Images/rock.png");
	shape->texture(rockTexture->makeHandle());
	//shape->setSizeAndCornerPoint(MV::Point<>(7, -90), rockTexture->size());
	shape->size(MV::cast<MV::PointPrecision>(rockTexture->size()));
	shape->owner()->position(MV::Point<>(70, -90));

	shape = armScene->owner()->make("rock2")->attach<MV::Scene::Sprite>();
	shape->texture(rockTexture->makeHandle());
	shape->size(MV::cast<MV::PointPrecision>(rockTexture->size()));
	//shape->setSizeAndCornerPoint(MV::Point<>(0, 0), rockTexture->size());
	shape->owner()->position(MV::Point<>(0, 0));
	std::cout << "RT: " << rockTexture->size() << std::endl;
	shape->owner()->depth(1);

	shape = catapaultScene->make("joint")->attach<MV::Scene::Sprite>();
	auto jointTexture = textures.file("Assets/Images/joint.png");
	shape->texture(jointTexture->makeHandle());
	shape->size(MV::cast<MV::PointPrecision>(jointTexture->size()));
	std::cout << "JT: " << jointTexture->size() << std::endl;
	shape->owner()->depth(3);

	//catapaultScene->scale(.5);*/
	return catapaultScene;
}

std::shared_ptr<MV::Scene::Node> Game::initializeTextScene() {
	textLibrary.loadFont("blue", "Assets/Fonts/bluehigh.ttf", 24);
	testBox->text(MV::stringToWide("This is a clear and obvious test!"));
	//testBox->scrollPosition(MV::Point<>(0, 8));
	return testBox->owner();
}
