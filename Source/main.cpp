#include "Editor/editor.h"
#include "vld.h"

int main(int argc, char *argv[]){
	for(float t = 0.0f; t <= 1.0001f; t += .01f){
		std::cout << t << ": " << MV::mixInOut(0.0f, 10.0f, t, 2.0f) << std::endl;
	}

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
