#include "editor.h"
#include "editorControls.h"
#include "componentPanels.h"
#include "editorFactories.h"

Editor::Editor(MV::ThreadPool* a_pool, MV::Draw2D* a_renderer):
	pool(a_pool),
	renderer(a_renderer),
	textLibrary(*a_renderer),
	scene(MV::Scene::Node::make(*renderer, "root")),
	controls(MV::Scene::Node::make(*renderer)),
	controlPanel(controls, scene, SharedResources(this, pool, &textures, &textLibrary, &mouse)),
	selectorPanel(scene, controls, SharedResources(this, pool, &textures, &textLibrary, &mouse)),
	testNode(MV::Scene::Node::make(*renderer)){

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	initializeWindow();
	initializeControls();

	/*
	auto rockTexture = MV::FileTextureDefinition::make("Assets/Images/rock.png");

	auto middleSquare = testNode->make("middleSquare")->//position({ 480.0f, 320.0f })->
		//attach<MV::Scene::Sprite>()->size({ 100.0f, 100.0f }, true)->color({ 0x00ff00 })->texture(rockTexture->makeHandle())->shader(MV::PREMULTIPLY_ID)->owner()->
		attach<MV::Scene::Sprite>()->size({ 50.0f, 50.0f }, true)->color({ 0x884422 })->owner()->
		attach<MV::Scene::Clipped>()->size({ 100.0f, 100.0f }, true)->captureSize({ 100.0f, 100.0f }, false)->owner();
	//middleSquare->rotation({ 0.0f, 0.0f, 90.0f });

	auto innerSquare = middleSquare->make("twoDeep")->
		attach<MV::Scene::Sprite>()->size({ 25.0f, 25.0f }, true)->color({ 0x0000ff })->shader(MV::PREMULTIPLY_ID)->owner();

	auto mostInner = innerSquare->make("threeDeep")->
		attach<MV::Scene::Sprite>()->size({ 12.0f, 12.0f }, true)->color({ 0xff00ff })->shader(MV::PREMULTIPLY_ID)->owner();

	auto textSquare = testNode->make("ourText")->
		attach<MV::Scene::Text>(textLibrary)->text(UTF_CHAR_STR("This is a test!"));
	auto clickable = textSquare->owner()->attach<MV::Scene::Clickable>(mouse)->size({ 15.0f, 100.0f })->color({0xFFFF00})->show();
	clickable->onAccept.connect("Clicked", [](const std::shared_ptr<MV::Scene::Clickable> &a_clickable){
		std::cout << "Clicked: " << a_clickable->mouse().position() << std::endl;
	});

	innerSquare->position({ 100.0f, 100.0f });
	innerSquare->rotation({ 0.0f, 0.0f, 45.0f });

	testNode->position({ 300.0f, 300.0f });

	auto emitter = middleSquare->make("EmitterTest")->position({ 50.0f, 50.0f })->attach<MV::Scene::Emitter>(pool)->properties(MV::Scene::loadEmitterProperties("particle.txt"))->shader(MV::PREMULTIPLY_ID);

	auto emitter2 = testNode->make("EmitterTest")->position({ 150.0f, 50.0f })->attach<MV::Scene::Emitter>(pool)->properties(MV::Scene::loadEmitterProperties("particle.txt"))->shader(MV::PREMULTIPLY_ID);

	auto button = testNode->make("ButtonTest")->position({ -200.0f, -200.0f })->attach<MV::Scene::Button>(mouse)->size({ 100.0f, 15.0f });
	button->activeNode(button->owner()->make("Active")->attach<MV::Scene::Sprite>()->size({ 100.0f, 15.0f })->color({0x553355})->owner());
	button->idleNode(button->owner()->make("Idle")->attach<MV::Scene::Sprite>()->size({ 100.0f, 15.0f })->color({ 0x225522 })->owner());
	button->disabledNode(button->owner()->make("Disabled")->attach<MV::Scene::Sprite>()->size({ 100.0f, 15.0f })->color({ 0xCCCCCC })->owner());
	button->onAccept.connect("Accepted", [](const std::shared_ptr<MV::Scene::Clickable> &a_clickable) {
		auto button = std::static_pointer_cast<MV::Scene::Button>(a_clickable);
		button->disable();
	});


	auto gridNode = testNode->make("Grid")->position({ 100.0f, 100.0f })->attach<MV::Scene::Grid>()->cellSize({ 20.0f, 20.0f })->gridWidth(110.0f)->margin({ 1.0f, 1.0f })->padding({ 1.0f, 1.0f })->owner();
	for (int i = 0; i < 20; ++i) {
		gridNode->make()->attach<MV::Scene::Sprite>()->size({ 20.0f, 20.0f })->color({ 0x333333 });
	}

	auto gridNode2 = testNode->make("Grid2")->position({ 300.0f, 100.0f })->attach<MV::Scene::Grid>()->cellSize({ 20.0f, 20.0f })->columns(5)->margin({ 1.0f, 1.0f })->padding({ 1.0f, 1.0f })->owner();
	for (int i = 0; i < 20; ++i) {
		gridNode2->make()->attach<MV::Scene::Sprite>()->size({ 20.0f, 20.0f })->color({ 0x333333 });
	}

	auto sliderTest = testNode->make("Slider")->position({ 0.0f, 200.0f })->attach<MV::Scene::Slider>(mouse)->size({ 100.0f, 20.0f })->show()->color({0x777777});
	sliderTest->handle(sliderTest->owner()->make("Handle")->attach<MV::Scene::Sprite>()->size({ 20.0f, 20.0f })->owner());*/
	//mostInner->rotation({ 0.0f, 0.0f, 45.0f });
}

//return true if we're still good to go
bool Editor::update(double dt){
	//testNode->translate(MV::Point<>(10.0, 10.0f) * static_cast<float>(dt));
	//testNode->get("middleSquare")->get("twoDeep")->addRotation(MV::AxisAngles(0.0f, 0.0f, 45.0f) * static_cast<float>(dt))->translate(MV::Point<>(-10.0, -10.0f) * static_cast<float>(dt));
	//fps->number(accumulatedTime > 0.0f ? accumulatedFrames / accumulatedTime : 0.0f);
	lastUpdateDelta = dt;
	selectorPanel.update();
	pool->run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Editor::sceneUpdated(){
	selectorPanel.refresh();
}

void Editor::initializeWindow(){
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
		}, [](){}, "ControlPanelDrag"));
	});

	textures.assemblePacks("Assets/Atlases", renderer);
	textures.files("Assets/Map");

	//fps = controls->make<MV::Scene::Text>("FPS", &textLibrary, MV::size(50.0f, 15.0f))->number(0.0f)->position({960.0f - 50.0f, 0.0f});
	fps = controls->make("FPS")->position({960.0f - 50.0f, 0.0f})->
		attach<MV::Scene::Text>(textLibrary, MV::size(50.0f, 15.0f))->number(0.0f)->wrapping(MV::TextWrapMethod::NONE);
}

void Editor::handleInput(){
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
					std::cout << "SCENE:\n" << scene << "\n______________" << std::endl;
					break;
				case SDLK_LEFT:
					break;
				case SDLK_DOWN:
					std::cout << "CONTROLS:\n" << controls << "\n______________" << std::endl;
					break;
				case SDLK_SPACE:

					break;
				case SDLK_RIGHT:
					break;
				}
				break;
			case SDL_WINDOWEVENT:
				break;
			case SDL_MOUSEWHEEL:
				handleScroll(event.wheel.y);
				break;
			}
		}
	}
	mouse.update();
	controlPanel.handleInput(event);
}

void Editor::render(){
	renderer->clearScreen();
	
	if(controlPanel.root() != scene){
		scene = controlPanel.root();
		scene->id("root");
		selectorPanel.refresh(scene);
	}
	scene->drawUpdate(lastUpdateDelta);
	controls->drawUpdate(lastUpdateDelta);
	renderer->updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}

void Editor::handleScroll(int a_amount) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * static_cast<float>(a_amount);
	if (scene->scale().x + screenScale.x > .05f) {
		auto originalScreenPosition = scene->localFromScreen(mouse.position()) * (MV::toPoint(screenScale));
		scene->addScale(screenScale);
		scene->translate(originalScreenPosition * -1.0f);
		controlPanel.onSceneZoom();
	}
}