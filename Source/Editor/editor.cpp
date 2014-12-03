#include "editor.h"
#include "editorControls.h"
#include "editorPanels.h"
#include "editorFactories.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

Editor::Editor():
	textLibrary(renderer),
	scene(MV::Scene::Node::make(renderer, "root")),
	controls(MV::Scene::Node::make(renderer)),
	controlPanel(controls, scene, SharedResources(this, &pool, &textures, &textLibrary, &mouse)),
	selectorPanel(scene, controls, SharedResources(this, &pool, &textures, &textLibrary, &mouse)),
	testNode(MV::Scene::Node::make(renderer)){

	initializeWindow();
	initializeControls();

	auto rockTexture = MV::FileTextureDefinition::make("Assets/Images/rock.png");

	auto middleSquare = testNode->make("middleSquare")->//position({ 480.0f, 320.0f })->
		attach<MV::Scene::Sprite>()->size({ 100.0f, 100.0f }, true)->color({ 0x00ff00 })->texture(rockTexture->makeHandle())->shader(MV::PREMULTIPLY_ID)->owner()->
		attach<MV::Scene::Sprite>()->size({ 50.0f, 50.0f }, true)->color({ 0xff0000 })->owner()->
		attach<MV::Scene::Clipped>()->size({ 100.0f, 100.0f }, true)->captureSize({ 100.0f, 100.0f }, false)->color({ 0x882288 })->owner();
	//middleSquare->rotation({ 0.0f, 0.0f, 90.0f });

	auto innerSquare = middleSquare->make("twoDeep")->
		attach<MV::Scene::Sprite>()->size({ 25.0f, 25.0f }, true)->color({ 0x0000ff })->owner();

	auto mostInner = innerSquare->make("threeDeep")->
		attach<MV::Scene::Sprite>()->size({ 12.0f, 12.0f }, true)->color({ 0xff00ff })->owner();

	innerSquare->position({ 100.0f, 100.0f });
	innerSquare->rotation({ 0.0f, 0.0f, 45.0f });

	//mostInner->rotation({ 0.0f, 0.0f, 45.0f });
}

//return true if we're still good to go
bool Editor::update(double dt){
	accumulatedTime += static_cast<float>(dt);
	++accumulatedFrames;
	if(accumulatedFrames > 60.0f){
		accumulatedFrames /= 2.0f;
		accumulatedTime /= 2.0f;
	}
	testNode->translate(MV::Point<>(10.0, 10.0f) * static_cast<float>(dt));
	testNode->get("middleSquare")->get("twoDeep")->addRotation(MV::AxisAngles(0.0f, 0.0f, 45.0f) * static_cast<float>(dt))->translate(MV::Point<>(-10.0, -10.0f) * static_cast<float>(dt));
	fps->number(accumulatedTime > 0.0f ? accumulatedFrames / accumulatedTime : 0.0f);
	selectorPanel.update();
	pool.run();
	return !done;
}

void Editor::sceneUpdated(){
	selectorPanel.refresh();
}

void Editor::initializeWindow(){
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();
	
	mouse.onLeftMouseDown.connect("initDrag", [&](MV::MouseState& a_mouse){
		a_mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, {10}, [&](){
			auto signature = mouse.onMove.connect("inDrag", [&](MV::MouseState& a_mouse2){
				scene->translate(MV::cast<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
				controlPanel.onSceneDrag(a_mouse2.position() - a_mouse2.oldPosition());
			});
			mouse.onLeftMouseUp.connect("cancelDrag", [=](MV::MouseState& a_mouse2){
				a_mouse2.onMove.disconnect(signature);
			});
		}, [](){}));
	});

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	textures.loadPacks("Assets/Atlases", &renderer);

	//fps = controls->make<MV::Scene::Text>("FPS", &textLibrary, MV::size(50.0f, 15.0f))->number(0.0f)->position({960.0f - 50.0f, 0.0f});
	fps = controls->make("FPS")->position({960.0f - 50.0f, 0.0f})->
		attach<MV::Scene::Text>(textLibrary, MV::size(50.0f, 15.0f))->number(0.0f);
	
	std::vector<std::string> names{"patternTest1.png", "platform.png", "rock.png", "joint.png", "slice.png", "spatula.png"};
	//selectorPanel.refresh();
	/*MV::TexturePack pack(&renderer);

	std::ifstream stream("Combined.texture");
	cereal::JSONInputArchive archive(stream);
	archive.add(cereal::make_nvp("renderer", &renderer));
	archive(cereal::make_nvp("pack", pack));

	auto rockHandle = pack.handle("rock.png");
	scene->make<MV::Scene::Sprite>(MV::BoxAABB<>(MV::point(100.0f, 100.0f), MV::cast<MV::PointPrecision>(rockHandle->bounds().size())))->texture(rockHandle);*/

	/*pack.addToScene(scene);

	auto slicedthing = scene->make<MV::Scene::Sliced>(MV::Scene::SliceDimensions({8.0f, 8.0f}, {32.0f, 32.0f}), MV::size(100.0f, 50.0f))->
		position({300.0f, 300.0f})->
		texture(texture->makeHandle(MV::size(32, 32)))->
		rotate(45.0f)->
		scale(2.0f)->
		shader(MV::PREMULTIPLY_ID);
	scene->make<MV::Scene::Sprite>(MV::size(100.0f, 100.0f))->position({500.0f, 400.0f})->texture(texture->makeHandle(MV::size(256, 256)))->shader(MV::PREMULTIPLY_ID);

	/*auto spineGuy = scene->make<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Assets/Spine/Example/spineboy.json", "Assets/Spine/Example/spineboy.atlas"))->
		position({500.0f, 500.0f})->
		scale(.5f)->
		animate("run")->
		queueAnimation("death", false, 5)->
		queueAnimation("run");*/

	/*auto emitter = scene->make<MV::Scene::Emitter>("Emitter", &pool, MV::Scene::loadEmitterProperties("particle.txt"))->position({300.0f, 300.0f})->depth(100000.0f)->
		texture(texture->makeHandle({MV::point(32, 32), MV::size(32, 32)}))->shader(MV::PREMULTIPLY_ID);*/

	//spineGuy->crossfade("death", "run", 1); //*/
}

void Editor::handleInput(){
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

					break;
				case SDLK_LEFT:
					break;
				case SDLK_DOWN:

					break;
				case SDLK_SPACE:

					break;
				case SDLK_RIGHT:
					break;
				}
				break;
			case SDL_WINDOWEVENT:
				break;
			}
		}
	}
	mouse.update();
	controlPanel.handleInput(event);
}

void Editor::render(){
	renderer.clearScreen();
	testNode->draw();
	
// 	if(controlPanel.root() != scene){
// 		scene = controlPanel.root();
// 		scene->id("root");
// 		selectorPanel.refresh(scene);
// 	}
// 	scene->draw();
// 	controls->draw();
	renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}