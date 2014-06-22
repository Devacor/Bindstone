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
	renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer.loadShader("unblend", "Assets/Shaders/default.vert", "Assets/Shaders/unblend.frag");
	atexit(sdl_quit);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	test = MV::Scene::Rectangle::make(&renderer, MV::BoxAABB({0.0f, 0.0f}, {100.0f, 110.0f}));
	test->texture(MV::FileTextureDefinition::make("Assets/Images/dogfox.png")->makeHandle());

	box = std::shared_ptr<MV::TextBox>(new MV::TextBox(&textLibrary, MV::size(110.0f, 106.0f)));
	//box->wrapping(MV::TextWrapMethod::HARD);
	box->setText(UTF_CHAR_STR("ABCDE FGHIJKLM NOPQRS TUVWXYZ"));
	//box->formattedText.removeCharacters(3, 13);
	/*box->formattedText.addCharacters(0, UTF_CHAR_STR("0"));
	box->formattedText.addCharacters(2, UTF_CHAR_STR("1"));
	box->formattedText.addCharacters(4, UTF_CHAR_STR("2"));
	box->formattedText.removeCharacters(0, 1);
	box->formattedText.removeCharacters(1, 1);
	box->formattedText.removeCharacters(2, 1);*/
	/*box->formattedText.removeCharacters(4, 4);
	box->formattedText.addCharacters(4, UTF_CHAR_STR("INSERTED"));
	box->formattedText.removeCharacters(6, 4);
	box->formattedText.addCharacters(6, UTF_CHAR_STR("[INSERTED]"));*/
	renderer.backgroundColor({0, 0, 0, 0});
	box->scene()->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({0, 0, 1, .5})->position({80.0f, 10.0f})->sortDepth(100);
	box->scene()->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({1, 0, 0, .25})->position({80.0f, 40.0f})->sortDepth(101);
	box->scene()->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({0, .75, 0, .75})->position({80.0f, 60.0f})->sortDepth(102);
	test->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({.0, 0, 1, .5})->position({110.0f, 10.0f})->sortDepth(100);
	test->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({1, 0, 0, .25})->position({110.0f, 40.0f})->sortDepth(101);
	test->make<MV::Scene::Rectangle>(MV::size(65.0f, 36.0f))->color({.0, .75, 0, .75})->position({110.0f, 60.0f})->sortDepth(102);

	//test->color({1, 1, 1});
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
					box->formattedText.addCharacters(6, UTF_CHAR_STR("+"));
					break;
				case SDLK_DOWN:

					break;
				case SDLK_SPACE:

					break;
				case SDLK_RIGHT:
					box->formattedText.removeCharacters(6, 1);
					break;
				}
				break;
			}
		}
	}
	mouse.update();
	controlPanel.handleInput(event);
	//testButton = makeButton(controls, textLibrary, mouse, MV::size(110.0f, 77.0f), UTF_CHAR_STR("ABC DEF GHI JKL MNO PQR STU VWX YZ"));
}

void Editor::render(){
	renderer.clearScreen();
	//scene->draw();
	//controls->draw();
	renderer.defaultBlendFunction();
	test->draw();
	box->scene()->draw();
	//textBox.scene()->draw();
	renderer.updateScreen();
}

void Editor::initializeControls(){
	controlPanel.loadPanel<DeselectedEditorPanel>();
}
