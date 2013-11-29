#include "game.h"

int main(int argc, char *argv[]){
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
