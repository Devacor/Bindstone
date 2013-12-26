#include "game.h"

void ourTestFunction(int a_arg){
	std::cout << "Merry Christmas: " << a_arg << std::endl;
}

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
