#include "editor.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
}

long Selection::gid = 0;

void Selection::callback(std::function<void(const MV::BoxAABB &)> a_callback){
	selectedCallback = a_callback;
}

void Selection::enable(std::function<void(const MV::BoxAABB &)> a_callback){
	selectedCallback = a_callback;
	enable();
}

void Selection::enable(){
	onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MV::MouseState &mouse){
		inSelection = true;
		selection.initialize(MV::castPoint<double>(mouse.position()));
		visibleSelection = scene->make<MV::Scene::Rectangle>("Selection_" + boost::lexical_cast<std::string>(id), selection.minPoint, MV::Size<>());
		visibleSelection->color(MV::Color(1.0, 1.0, 0.0, .25));
		auto originalPosition = visibleSelection->localFromScreen(mouse.position());
		onMouseMoveHandle = mouse.onMove.connect([&, originalPosition](MV::MouseState &mouse){
			visibleSelection->setTwoCorners(originalPosition, visibleSelection->localFromScreen(mouse.position()));
		});
	});

	onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MV::MouseState &mouse){
		if(!inSelection){
			return;
		}
		SCOPE_EXIT{
			exitSelection(); //callback might throw, let's be safe.
		};

		selection.expandWith(MV::castPoint<double>(mouse.position()));
		if(selectedCallback){
			selectedCallback(selection);
		}
	});
}

void Selection::exitSelection(){
	inSelection = false;
	onMouseMoveHandle.reset();
	if(visibleSelection){
		visibleSelection->removeFromParent();
		visibleSelection.reset();
	}
}

void Selection::disable(){
	onMouseDownHandle.reset();
	onMouseUpHandle.reset();
	exitSelection();
}

Editor::Editor():
	textLibrary(&renderer),
	scene(MV::Scene::Node::make(&renderer)),
	controls(MV::Scene::Node::make(&renderer)),
	controlPanel(controls, scene, &textLibrary, &mouse){

	initializeWindow();
	initializeControls();
}

//return true if we're still good to go
bool Editor::update(double dt){
	return true;
}

void Editor::initializeWindow(){
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	renderer.window().windowedMode();

	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	textLibrary.loadFont("default", 14, "Assets/Fonts/Verdana.ttf");
	textLibrary.loadFont("small", 9, "Assets/Fonts/Verdana.ttf");
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
			}
		}
	}
	mouse.update();
	controlPanel.handleInput(event);
}

void Editor::render(){
	renderer.clearScreen();
	scene->draw();
	controls->draw();
	//textBox.scene()->draw();
	renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}


void colorTopAndBottom(const std::shared_ptr<MV::Scene::Rectangle> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->applyToCorners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier){
	static long buttonId = 0;
	auto button = a_parent->make<MV::Scene::Button>(MV::wideToString(a_text) + boost::lexical_cast<std::string>(buttonId++), &a_mouse, a_size);
	auto activeScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), MV::Point<>(), a_size, false);
	colorTopAndBottom(activeScene, {BUTTON_TOP_ACTIVE}, {BUTTON_BOTTOM_ACTIVE});

	auto idleScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), MV::Point<>(), a_size, false);
	colorTopAndBottom(idleScene, {BUTTON_TOP_IDLE}, {BUTTON_BOTTOM_IDLE});

	MV::TextBox activeBox(&a_library, a_fontIdentifier, a_text, a_size), idleBox(&a_library, a_fontIdentifier, a_text, a_size);
	activeBox.justification(MV::CENTER);
	idleBox.justification(MV::CENTER);

	activeBox.scene()->position(activeBox.scene()->basicAABB().centerPoint() - activeScene->basicAABB().centerPoint());
	activeScene->add("text", activeBox.scene());
	idleScene->add("text", idleBox.scene());

	button->activeScene(activeScene);
	button->idleScene(idleScene);

	return button;
}

void EditorControls::updateBoxHeader(double a_width) {
	if(!boxHeader){
		boxHeader = draggableBox->make<MV::Scene::Clickable>("ContextMenuHandle", mouseHandle, MV::size(a_width, 20.0));
		boxHeader->color({BOX_HEADER});

		boxHeaderDrag = boxHeader->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			if(currentPanel){
				currentPanel->cancelInput();
			}
			draggableBox->translate(MV::castPoint<double>(deltaPosition));
		});
	} else{
		boxHeader->setSize({a_width, 20.0});
	}
}

void EditorControls::handleInput(SDL_Event &a_event) {
	if(currentPanel){
		currentPanel->handleInput(a_event);
	}
}
