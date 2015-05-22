#include "threadPool.h"
#include "log.hpp"

namespace MV{
	ThreadPool::ThreadTask::ThreadTask(const std::function<void()> &a_call):
		call(a_call),
		isRun(std::make_unique<std::atomic<bool>>(false)),
		isFinished(std::make_shared<std::atomic<bool>>(false)),
		handled(false){
	}

	ThreadPool::ThreadTask::ThreadTask(const std::function<void()> &a_call, const std::function<void()> &a_onFinish) :
		call(a_call),
		onFinish(a_onFinish),
		isRun(std::make_unique<std::atomic<bool>>(false)),
		isFinished(std::make_shared<std::atomic<bool>>(false)),
		handled(false){
	}

	bool ThreadPool::ThreadTask::finished() {
		if(isFinished->load()){
			if(onFinish && !handled){
				handled = true;
				onFinish();
				if(groupFinishWaitForFrame && (onGroupFinish && groupCounter && *groupCounter == 0)){
					(*onGroupFinish)();
				}
			}
			return true;
		}
		return false;
	}

	void ThreadPool::ThreadTask::operator()() {
		bool isFalse = false;
		if(isRun->compare_exchange_strong(isFalse, true)){
			call();
			if((groupCounter && --(*groupCounter) == 0)){
				if(!groupFinishWaitForFrame && onGroupFinish){
					(*onGroupFinish)();
				}
				*isGroupFinished = true;
			}
			*isFinished = true;
		}
	}

	void ThreadPool::ThreadTask::group(const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish, const std::shared_ptr<std::atomic<bool>> &a_isGroupFinished, bool a_groupFinishWaitForFrame) {
		groupCounter = a_groupCounter;
		onGroupFinish = a_onGroupFinish;
		groupFinishWaitForFrame = a_groupFinishWaitForFrame;
		isGroupFinished = a_isGroupFinished;
	}

	ThreadPool::ThreadPool():
		ThreadPool(std::thread::hardware_concurrency()) {
	}

	ThreadPool::~ThreadPool() {
		service.stop();
		for(auto&& worker : workers){
			worker->join();
		}
	}

	ThreadPool::ThreadPool(size_t a_threads):
		totalThreads(a_threads < 2 ? 1 : a_threads - 1),
		service(),
		working(new asio_worker::element_type(service)) {

		log(INFO, "Info: Generating ThreadPool [", totalThreads, "]");

		for(size_t i = 0; i < totalThreads; ++i) {
			workers.emplace_back(new std::thread([this]{
				service.run();
			}));
		}
	}

	TaskStatus ThreadPool::task(const std::function<void()> &a_task) {
		std::lock_guard<std::recursive_mutex> guard(lock);

		runningTasks.push_back({a_task});
		auto thisTask = runningTasks.end();
		--thisTask;

		service.post([=](){(*thisTask)();});

		return{thisTask->isFinished};
	}

	TaskStatus ThreadPool::task(const std::function<void()> &a_task, const std::function<void()> &a_onComplete) {
		std::lock_guard<std::recursive_mutex> guard(lock);

		runningTasks.emplace_back(a_task, a_onComplete);
		auto thisTask = runningTasks.end();
		--thisTask;
		auto completed = std::make_shared<std::atomic<bool>>(false);
		std::weak_ptr<std::atomic<bool>> weakCompleted = completed;

		service.post([=](){(*thisTask)(); if(!weakCompleted.expired()){ *weakCompleted.lock() = true; }});

		return{thisTask->isFinished};
	}

	TaskStatus ThreadPool::tasks(const TaskList &a_tasks, const std::function<void()> &a_onGroupComplete, bool a_groupFinishWaitForFrame) {
		auto groupCounter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
		auto onGroupComplete = std::make_shared<std::function<void()>>(a_onGroupComplete);
		auto isGroupComplete = std::make_shared<std::atomic<bool>>(false);

		std::lock_guard<std::recursive_mutex> guard(lock);
		for(auto&& currentTask : a_tasks){
			runningTasks.emplace_back(currentTask.task, currentTask.onComplete);
			auto thisTask = runningTasks.end();
			--thisTask;
			thisTask->group(groupCounter, onGroupComplete, isGroupComplete, a_groupFinishWaitForFrame);

			service.post([=](){(*thisTask)();});
		}
		return {isGroupComplete};
	}

	size_t ThreadPool::run() {
		std::lock_guard<std::recursive_mutex> guard(lock);

		runningTasks.remove_if([](ThreadTask& a_task){
			return a_task.finished();
		});

		return runningTasks.size();
	}


	ThreadPool::TaskDefinition::TaskDefinition(const std::function<void()> &a_task):
		task(a_task) {
	}

	ThreadPool::TaskDefinition::TaskDefinition(const std::function<void()> &a_task, const std::function<void()> &a_onComplete) :
		task(a_task),
		onComplete(a_onComplete) {
	}

}