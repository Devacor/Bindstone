#include "editor.h"
#include "editorControls.h"
#include "editorPanels.h"

void sdl_quit(void){
	SDL_Quit();
	TTF_Quit();
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
	renderer.loadShader("default", "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
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
	test = MV::Scene::Rectangle::make(&renderer, MV::BoxAABB({0.05f, 0.05f}, {-.050f, -0.050f}), false);
	test->color({1, 0, 0});
}

void Editor::render(){
	renderer.clearScreen();
	test->draw();
	//scene->draw();
	//controls->draw();
	//textBox.scene()->draw();
	renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}
