#include "Editor/editor.h"
#include "vld.h"
#include "Utility/threadPool.h"

std::mutex coutLock;
void LockPrint(const std::string &a_out){
	std::lock_guard<std::mutex> guard(coutLock);
	std::cout << a_out << std::endl;
}

int main(int argc, char *argv[]){
	MV::TexturePack pack;

	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});
	pack.add({64, 64});

	pack.print();
	/*
	MV::ThreadPool pool;

	MV::ThreadPool::TaskList tasks;
	for(int task = 1; task < 12; ++task){
		tasks.emplace_back([task](){
			for(int i = 1; i <= 50; ++i){
				MV::atomic_cout() << task << ": " << i << std::endl;
			}
		}, [task](){
			MV::atomic_cout() << "!!" << task << "!!" << std::endl;
		});
	}

	pool.tasks(tasks, [](){
		std::cout << "!DONE!" << std::endl;
	});
	while(pool.run()){};
	*/
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
