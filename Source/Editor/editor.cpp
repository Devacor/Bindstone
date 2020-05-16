#include "editor.h"
#include "editorControls.h"
#include "componentPanels.h"
#include "editorFactories.h"

Editor::Editor(Managers &a_managers):
	managers(a_managers),
	visor(MV::Scene::Node::make(a_managers.renderer, "visor")),
	scene(visor->make("root")),

	controls(MV::Scene::Node::make(a_managers.renderer)),
	script(managers.services),
	testNode(MV::Scene::Node::make(a_managers.renderer)){
	visor->cameraId(99);

	managers.services.connect(this);
	returnFromBackground();
	controlPanel = std::make_unique<EditorControls>(controls, scene, managers.services);
	selectorPanel = std::make_unique<SceneGraphPanel>(scene, controls, managers.services);

	initializeWindow();
	initializeControls();
}

void Editor::updateCameraWithKeyboardState(double dt) {
	const static float speed = 175.0f;
	const static float zoomSpeed = 7.5f;
	const Uint8* keystate = SDL_GetKeyboardState(NULL);
	if (keystate[SDL_SCANCODE_LEFT]) {
		scene->camera().translate(MV::point(speed, 0.0f) * static_cast<float>(dt));
		controlPanel->onSceneDrag(MV::cast<int>(MV::point(speed, 0.0f) * static_cast<float>(dt)));
	}
	if (keystate[SDL_SCANCODE_RIGHT]) {
		scene->camera().translate(MV::point(-speed, 0.0f) * static_cast<float>(dt));
		controlPanel->onSceneDrag(MV::cast<int>(MV::point(-speed, 0.0f) * static_cast<float>(dt)));
	}
	if (keystate[SDL_SCANCODE_UP]) {
		scene->camera().translate(MV::point(0.0f, speed) * static_cast<float>(dt));
		controlPanel->onSceneDrag(MV::cast<int>(MV::point(0.0f, speed) * static_cast<float>(dt)));
	}
	if (keystate[SDL_SCANCODE_DOWN]) {
		scene->camera().translate(MV::point(0.0f, -speed) * static_cast<float>(dt));
		controlPanel->onSceneDrag(MV::cast<int>(MV::point(0.0f, -speed) * static_cast<float>(dt)));
	}
	if (keystate[SDL_SCANCODE_V]) {
		handleScroll(-zoomSpeed * static_cast<float>(dt), mouse.position());
	}
	if (keystate[SDL_SCANCODE_B]) {
		handleScroll(zoomSpeed * static_cast<float>(dt), mouse.position());
	}
	if (keystate[SDL_SCANCODE_C]) {
		scene->camera().position({ 0.0f, 0.0f, 0.0f });
	}
	if (keystate[SDL_SCANCODE_SPACE]) {
		scene->camera().position(MV::point(0.0f, 0.0f));
		scene->scale({ 1.0f, 1.0f, 1.0f });
		scene->position(MV::point(0.0f, 0.0f));
	}
}

//return true if we're still good to go
bool Editor::update(double dt){
	updateFps(dt);
	if (controlPanel->activePanel() == nullptr || !controlPanel->activePanel()->hasActiveText()) {
		updateCameraWithKeyboardState(dt);
	}
	lastUpdateDelta = dt;
	selectorPanel->update();
	managers.pool.run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void Editor::sceneUpdated(){
	selectorPanel->refresh();
}

void Editor::initializeWindow(){
	mouse.update();
	
	mouse.onLeftMouseDown.connect("initDrag", [&](MV::TapDevice& a_mouse){
		a_mouse.queueExclusiveAction(MV::ExclusiveTapAction(true, {10}, [&](){
			auto signature = mouse.onMove.connect("inDrag", [&](MV::TapDevice& a_mouse2){
				const Uint8* keystate = SDL_GetKeyboardState(NULL);
				if (!keystate[SDL_SCANCODE_LSHIFT]) {
					scene->camera().translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
					scene->renderer().updateCameraProjectionMatrices();
				} else {
					scene->translate(MV::round<MV::PointPrecision>(a_mouse2.position() - a_mouse2.oldPosition()));
				}
				controlPanel->onSceneDrag(a_mouse2.position() - a_mouse2.oldPosition());
			});
			mouse.onLeftMouseUp.connect("cancelDrag", [=](MV::TapDevice& a_mouse2){
				a_mouse2.onMove.disconnect(signature);
				a_mouse2.onLeftMouseUp.disconnect("cancelDrag");
			});
		}, [](){}, "ControlPanelDrag"));
	});

	mouse.onPinchZoom.connect("initZoom", [&](const MV::Point<int> &a_position, float a_zoom, float a_rotate) {
		mouse.onMove.disconnect("inDrag");
		handleScroll(a_zoom * 20.0f, a_position);
	});

#ifndef __ANDROID__
	managers.textures.assemblePacks("Assets/Atlases", &managers.renderer);
#endif
	managers.textures.files("Assets/Map");
	managers.textures.files("Assets/Images");

	//fps = controls->make<MV::Scene::Text>("FPS", &textLibrary, MV::size(50.0f, 15.0f))->set(0.0f)->position({960.0f - 50.0f, 0.0f});
	fps = controls->make("FPS")->
		attach<MV::Scene::Text>(managers.textLibrary)->set(0.0f)->wrapping(MV::TextWrapMethod::NONE)->bounds({ MV::Point<>(), MV::size(50.0f, 15.0f) });
}

void Editor::handleInput(){
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		if(!managers.renderer.handleEvent(event)){
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
					//std::cout << "SCENE:\n" << scene << "\n______________" << std::endl;
					break;
				case SDLK_DOWN:
					//std::cout << "CONTROLS:\n" << controls << "\n______________" << std::endl;
					break;
				case SDLK_SPACE:
					scene->position({ 0, 0, 0 });
					scene->scale({ 1, 1, 1 });
					break;
				case SDLK_LEFT:
					break;
				case SDLK_RIGHT:
					break;
				}
				break;
			case SDL_WINDOWEVENT:
				break;
			case SDL_MOUSEWHEEL:
				handleScroll(static_cast<float>(event.wheel.y), mouse.position());
				break;
			}
			controlPanel->handleInput(event);
			mouse.updateTouch(event, managers.renderer.window().drawableSize());
		}
	}
	mouse.update();
}

void Editor::render(){
	managers.renderer.clearScreen();
	
	if(controlPanel->root() != scene){
		scene = controlPanel->root();
		scene->id("root");
		selectorPanel->refresh(scene);
	}
	auto scaler = visor->component<MV::Scene::Drawable>("ScreenScaler", false);
	if (!scaler) {
		scaler = visor->attach<MV::Scene::Drawable>()->id("ScreenScaler")->bounds({ MV::Point<>(0, 0), managers.renderer.world().size() });
		fps->anchors().anchor(MV::point(1.0f, 0.0f)).parent(scaler.get());
		fps->anchors().offset({ { -50.0f, 15.0f }, MV::point(0.0f, 0.0f)});
	} else {
		scaler->bounds({ MV::Point<>(0, 0), managers.renderer.world().size() });
	}
	scene->drawUpdate(lastUpdateDelta);
	controls->drawUpdate(lastUpdateDelta);
	managers.renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel->loadPanel<DeselectedEditorPanel>();
}

void Editor::handleScroll(float a_amount, const MV::Point<int> &a_position) {
	auto screenScale = MV::Scale(.05f, .05f, .05f) * a_amount + (scene->scale() / 10.0f * a_amount);
	if (scene->scale().x + screenScale.x > .001f) {
		auto originalScreenPosition = scene->localFromScreen(a_position) * (MV::toPoint(screenScale));
		scene->addScale(screenScale);
		scene->translate(originalScreenPosition * -1.0f);
		controlPanel->onSceneZoom();
	}
}
