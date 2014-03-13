#include "editor.h"
#include "vld.h"
#include "cerealtest.h"

int main(int argc, char *argv[]){
	//saveTest();
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
