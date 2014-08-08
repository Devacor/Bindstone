#include "editor.h"
#include "editorControls.h"
#include "editorPanels.h"
#include "editorFactories.h"

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
	return !done;
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
	
	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	
	/*auto texture = MV::FileTextureDefinition::make("Assets/Images/dogfox.png");
	auto slicedthing = scene->make<MV::Scene::Sliced>(MV::Scene::SliceDimensions({8.0f, 8.0f}, {32.0f, 32.0f}), MV::size(100.0f, 50.0f))->
		position({300.0f, 300.0f})->
		texture(texture->makeHandle({0, 0}, {32, 32}))->
		rotate(45.0f)->
		scale(2.0f)->
		shader(MV::PREMULTIPLY_ID);
	scene->make<MV::Scene::Rectangle>(MV::size(10.0f, 10.0f))->position({300.0f, 300.0f})->shader(MV::PREMULTIPLY_ID);

	auto spineGuy = scene->make<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Assets/Spine/Example/spineboy.json", "Assets/Spine/Example/spineboy.atlas"))->
		position({500.0f, 500.0f})->
		scale(.5f)->
		animate("run")->
		queueAnimation("death", false, 5)->
		queueAnimation("run");

	auto emitter = scene->make<MV::Scene::Emitter>("Emitter", MV::Scene::loadEmitterProperties("particle.txt"))->position({300.0f, 300.0f})->depth(100000.0f)->
		texture(texture->makeHandle({32, 32}, {32, 32}))->shader(MV::PREMULTIPLY_ID);

	spineGuy->crossfade("death", "run", 1); //*/
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
	scene->draw();
	controls->draw();
	renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}
