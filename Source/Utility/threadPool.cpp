#include "threadPool.h"

namespace MV{
	ThreadPool::ThreadTask::ThreadTask(std::function<void()> a_call):
		call(a_call),
		justFinished(false) {
	}

	ThreadPool::ThreadTask::ThreadTask(std::function<void()> a_call, std::function<void()> a_onFinish):
		call(a_call),
		onFinish(a_onFinish),
		justFinished(false) {
	}

	bool ThreadPool::ThreadTask::finished() {
		if(justFinished){
			if(onFinish){
				onFinish();
			}
			justFinished = false;
			return true;
		}
		return false;
	}

	void ThreadPool::ThreadTask::operator()() {
		if(!justFinished){
			call();
			justFinished = true;
		}
	}


	ThreadPool::~ThreadPool() {
		working.reset();
		service.run();
	}

	ThreadPool::ThreadPool():
		ThreadPool(std::thread::hardware_concurrency()) {
	}

	ThreadPool::ThreadPool(size_t a_threads):
		totalThreads(a_threads < 2 ? 1 : a_threads - 1),
		service(),
		working(new asio_worker::element_type(service)) {

		std::cout << "Info: Generating ThreadPool [" << totalThreads << "]" << std::endl;

		for(std::size_t i = 0; i < totalThreads; ++i) {
			workers.push_back(std::unique_ptr<std::thread>(new std::thread([this]{
				service.run();
			})));
		}
	}

	void ThreadPool::task(std::function<void()> a_task) {
		auto newTask = std::make_shared<ThreadTask>(a_task);
		runningTasks.push_back(newTask);
		service.post([=](){(*newTask)();});
	}

	void ThreadPool::task(std::function<void()> a_task, std::function<void()> a_onComplete) {
		auto newTask = std::make_shared<ThreadTask>(a_task, a_onComplete);
		runningTasks.push_back(newTask);
		service.post([=](){(*newTask)();});
	}

	size_t ThreadPool::run() {
		std::vector<std::list<std::shared_ptr<ThreadTask>>::iterator> removeQueue;
		for(auto a_task = runningTasks.begin();a_task != runningTasks.end();++a_task){
			if((*a_task)->finished()){
				removeQueue.push_back(a_task);
			}
		}
		for(std::list<std::shared_ptr<ThreadTask>>::iterator toRemove : removeQueue){
			runningTasks.erase(toRemove);
		}
		return runningTasks.size();
	}

}