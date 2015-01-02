#include "Game/gameEditor.h"
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
	GameEditor menu;

	menu.start();

	return 0;
}
