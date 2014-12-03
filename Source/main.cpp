#include "Editor/editor.h"
#include "vld.h"
#include "Utility/threadPool.h"

bool isDone() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return true;
		}
	}
}

int main(int argc, char *argv[]){
	Editor editor;
	MV::Stopwatch timer;
	timer.start();

	while(editor.update(timer.delta("tick"))){
		editor.handleInput();
		editor.render();
		MV::systemSleep(0);
	}
	
	system("pause");
	return 0;
}
