#include "Editor/editor.h"
#include "vld.h"
#include "Utility/threadPool.h"

std::mutex coutLock;
void LockPrint(const std::string &a_out){
	std::lock_guard<std::mutex> guard(coutLock);
	std::cout << a_out << std::endl;
}

struct TimerTask {
	TimerTask(double a_duration):
		duration(a_duration){
	}

	bool update(const MV::Task& a_self, double a_dt){
		std::cout << a_self.name() << ": " << a_self.elapsed() << "   dt: " << a_dt << std::endl;
		return a_self.elapsed() >= duration;
	}

	void onStart(const MV::Task& a_self){
		std::cout << a_self.name() << ": Started" << std::endl;
	}

	void onFinish(const MV::Task& a_self){
		std::cout << a_self.name() << ": Finished" << std::endl;
	}

	void onFinishChildren(const MV::Task& a_self){
		std::cout << a_self.name() << ": Finished Children" << std::endl;
	}

	double duration;
};

int main(int argc, char *argv[]){
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

	MV::Task newTask;
	MV::SequentialTask("First", newTask, TimerTask(5.0));
	auto& task2 = MV::SequentialTask("Second", newTask, TimerTask(10.0));

	MV::SequentialTask("    NestedThird", newTask, TimerTask(5.0));
	MV::SequentialTask("    NestedFourth", newTask, TimerTask(10.0));
	MV::ParallelTask("Parallel", newTask, TimerTask(1000), false);

	MV::ParallelTask("    NestedParallel", task2, TimerTask(1000), false);

	double dt = 0.0;
	while(!newTask.update(dt)){ dt = 1.0; }
	/*Editor editor;
	MV::Stopwatch timer;
	timer.start();

	while(editor.update(timer.delta("tick"))){
		editor.handleInput();
		editor.render();
		MV::systemSleep(0);
	}
	*/
	system("pause");
	return 0;
}
