#include "exampleCode.h"
#include "boost/asio.hpp"

bool acceptInput(SDL_Event &event, FoxJump &foxJump, MV::Draw2D &renderer, MV::TextureManager &textures/*, MV::TextLibrary &text, MV::TextBox &textBox*/);
void quit(void);

int main(int argc, char *argv[]){
	MV::initializeFilesystem();
	srand ((unsigned int)time(0));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Draw2D renderer;
	MV::Point worldSize(800, 600, 0), windowSize(480, 320, 0);

	renderer.useFullScreen(false);
	renderer.allowWindowResize(true);
	if(!renderer.initialize((int)windowSize.x, (int)windowSize.y, (int)worldSize.x, (int)worldSize.y)){
		exit(0);
	}
	renderer.setBackgroundColor(MV::Color(0.25f, 0.45f, 0.65f));
	atexit(quit);
	
	//WORLD SETUP::::::::::::::::::::::::::::::
	//Set up managing objects
	MV::TextureManager textures;
	
	MV::FrameSwapperRegister animationLibrary;
	LoadTexturesAndAnimations(textures, animationLibrary);

	MV::TextLibrary textLibrary(&renderer);
	MV::Scene mainScene(&renderer);

	CreateTextScene(mainScene, textLibrary);
	CreateDogFoxScene(mainScene, textures);

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

	MV::TextBox textBox(&textures, &textLibrary, "bluehighway1", UTF_CHAR_STR("The quick [[f|bluehighway2]][[c|.6:.25:0]]brown fox [[f|bluehighway3]][[c|1:1:0]]jumped[[f|]][[c|]] over the [[f|annabel]][[c|.25:.5:1]]lazy[[f|]][[c|]] dog!"), 300, 100);
	textBox.scene()->placeAt(MV::Point(300, 100));
	mainScene.add(textBox.scene(), "TextBoxScene");
	//GAME LOOP::::::::::::::::::::::::::::::::::::
	SDL_Event event;
	bool done = false;
	while(!done){
		//Handle window closing/SDL quitting, and the jump button
		done = acceptInput(event, foxJump, renderer, textures/*, textLibrary, textBox*/);
		
		//Spin the text and alter its color and spin the sky
		ManipulateText(mainScene);
		UpdateSky(mainScene);

		//Update the fox position and animation based on the jump state
		foxJump.updateJump();

		//Update the animation on the fox and dog
		UpdateAnimation(mainScene, textures, *GetDogShape(mainScene), dogAnimation);
		UpdateAnimation(mainScene, textures, *GetFoxShape(mainScene), foxAnimation, foxJump.isFlipped());
		
		//Update the screen
		
		renderer.clearScreen();
		
		mainScene.draw();
		
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

bool acceptInput(SDL_Event &event, FoxJump &foxJump, MV::Draw2D &renderer, MV::TextureManager &textures/*, MV::TextLibrary &text, MV::TextBox &textBox*/){
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
}
