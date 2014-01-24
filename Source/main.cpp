#include "game.h"
#include "vld.h"
#include "cerealtest.h"
int main(int argc, char *argv[]){
	saveTest();
	Game gameInstance;
	MV::Stopwatch timer;
	timer.start();
	while(gameInstance.passTime(timer.delta("tick"))){
		gameInstance.handleInput();
		gameInstance.render();
		MV::systemSleep(0);
	}

	system("pause");
	return 0;
}
/*/

#include "exampleCode.h"
#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include <string>
#include <ctime>

bool acceptInput(SDL_Event &event, FoxJump &foxJump, MV::Draw2D &renderer);
void quit(void);

int main(int argc, char *argv[]){
	MV::initializeFilesystem();
	srand((unsigned int)time(0));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Draw2D renderer;
	MV::Size<int> worldSize(800, 600, 0), windowSize(480, 320, 0);

	renderer.window().windowedMode();

	if(!renderer.initialize(windowSize, worldSize)){
		exit(0);
	}
	renderer.setBackgroundColor(MV::Color(0.25f, 0.45f, 0.65f));
	atexit(quit);

	//WORLD SETUP::::::::::::::::::::::::::::::
	//Set up managing objects

	MV::FrameSwapperRegister animationLibrary;
	LoadTexturesAndAnimations(animationLibrary);

	MV::TextLibrary textLibrary(&renderer);
	auto mainScene = MV::Scene::Node::make(&renderer);

	CreateTextScene(mainScene, textLibrary);
	CreateDogFoxScene(mainScene);

	//Initialize the individual animation objects for the dog and fox.
	MV::FrameSwapper dogAnimation, foxAnimation;
	dogAnimation.setFrameList(animationLibrary.getDefinition("DogStand"));
	dogAnimation.start();

	foxAnimation.setFrameList(animationLibrary.getDefinition("FoxStand"));
	foxAnimation.start();

	//Create the fox jump state object
	FoxJump foxJump(&animationLibrary, &foxAnimation, GetFoxShape(mainScene));

	//AUDIO SETUP::::::::::::::::::::::::::::::
	AudioPlayer *player = AudioPlayer::instance();
	player->initAudio();
	player->loadMusic("Assets/Audio/lifefortwo.ogg", "RepeatSong1");
	player->loadMusic("Assets/Audio/countrysides.ogg", "RepeatSong2");

	AudioPlayList MusicPlayList;
	player->setMusicPlayList(&MusicPlayList);
	MusicPlayList.setPlayListType(MUSIC_PLAYLIST);
	MusicPlayList.addSoundBack("RepeatSong1");
	MusicPlayList.addSoundBack("RepeatSong2");
	MusicPlayList.resetPlayHead();
	MusicPlayList.shuffleSounds(false);
	MusicPlayList.loopSounds(true);
	MusicPlayList.continuousPlay(true);

	MusicPlayList.beginPlaying();

	MV::TextBox textBox(&textLibrary, "bluehighway1", UTF_CHAR_STR("The quick [[f|bluehighway2]][[c|.6:.25:0]]brown fox [[f|bluehighway3]][[c|1:1:0]]jumped[[f|]][[c|]] over the [[f|annabel]][[c|.25:.5:1]]lazy[[f|]][[c|]] dog!"), MV::Size<>(300, 100));
	textBox.scene()->locate(MV::Point<>(300, 100));
	mainScene->add("TextBoxScene", textBox.scene());
	//GAME LOOP::::::::::::::::::::::::::::::::::::
	SDL_Event event;
	bool done = false;
	while(!done){
		//Handle window closing/SDL quitting, and the jump button
		done = acceptInput(event, foxJump, renderer);

		//Spin the text and alter its color and spin the sky
		ManipulateText(mainScene);
		UpdateSky(mainScene);

		//Update the fox position and animation based on the jump state
		foxJump.updateJump();

		//Update the animation on the fox and dog
		UpdateAnimation(mainScene, GetDogShape(mainScene), dogAnimation);
		UpdateAnimation(mainScene, GetFoxShape(mainScene), foxAnimation, foxJump.isFlipped());

		//Update the screen

		renderer.clearScreen();

		mainScene->draw();

		renderer.updateScreen();

		//Delay a moment to avoid hogging the CPU
#ifdef __APPLE__
		sleep(0);
#else
		Sleep(0);
#endif
	}
	return 0;
}

bool acceptInput(SDL_Event &event, FoxJump &foxJump, MV::Draw2D &renderer){
	while(SDL_PollEvent(&event)){
		//textBox.setText(event);
		switch(event.type){
		case SDL_QUIT:
			return true;
			break;
		case SDL_FINGERDOWN:
			foxJump.initiateJump();
		case SDL_KEYDOWN:
			switch(event.key.keysym.sym){
			case SDLK_ESCAPE:
				return true;
				break;
			case SDLK_SPACE:
				foxJump.initiateJump();
				break;
			case SDLK_DOWN:
				//textBox.translateScrollPosition(MV::Point(0, 10));
				break;
			case SDLK_UP:
				//textBox.translateScrollPosition(MV::Point(0, -10));
				break;
			default:
				break;
			}
			break;
		}
	}
	return false;
}

void quit(void){
	SDL_Quit();
	TTF_Quit();
}*/
