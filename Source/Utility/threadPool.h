#ifndef _MV_THREADPOOL_H_
#define _MV_THREADPOOL_H_

#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <list>
#include <chrono>

#include "boost/asio.hpp"

namespace MV{

	class TaskStatus {
	public:
		TaskStatus(){}
		TaskStatus(std::shared_ptr<std::atomic<bool>> a_isFinished):
			isFinished(a_isFinished){
		}
		bool clear() {
			isFinished = nullptr;
		}
		bool active() const{
			return isFinished != nullptr;
		}
		bool finished(){
			return !isFinished || isFinished->load();
		}
		void join(){
			while(!finished()){
				std::this_thread::sleep_for(std::chrono::nanoseconds(100));
			}
		}
	private:
		std::shared_ptr<std::atomic<bool>> isFinished;
	};

	class ThreadPool {
	private:
		class ThreadTask {
		public:
			ThreadTask(const std::function<void()> &a_call);
			ThreadTask(const std::function<void()> &a_call, const std::function<void()> &a_onFinish);
			ThreadTask(ThreadTask&& a_rhs):
				groupCounter(std::move(a_rhs.groupCounter)),
				onGroupFinish(std::move(a_rhs.onGroupFinish)),
				isGroupFinished(std::move(a_rhs.isGroupFinished)),
				isRun(std::move(a_rhs.isRun)),
				isFinished(std::move(a_rhs.isFinished)),
				handled(std::move(a_rhs.handled)),
				call(std::move(a_rhs.call)),
				onFinish(std::move(a_rhs.onFinish)){
			}

			void operator()();

			bool finished();

			void group(const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish, const std::shared_ptr<std::atomic<bool>> &a_isGroupFinished, bool a_groupFinishWaitForFrame);

			std::shared_ptr<std::atomic<bool>> isFinished;
			std::shared_ptr<std::atomic<bool>> isGroupFinished;
		private:
			ThreadTask(const ThreadTask& a_rhs) = delete;
			ThreadTask& operator=(const ThreadTask& a_rhs) = delete;

			std::shared_ptr<std::atomic<size_t>> groupCounter;
			std::shared_ptr<std::function<void()>> onGroupFinish;
			bool groupFinishWaitForFrame;
			std::unique_ptr<std::atomic<bool>> isRun;
			bool handled;
			std::function<void()> call;
			std::function<void()> onFinish;
		};

	public:
		struct TaskDefinition {
			TaskDefinition(const std::function<void()> &a_task);
			TaskDefinition(const std::function<void()> &a_task, const std::function<void()> &a_onComplete);
			std::function<void()> task;
			std::function<void()> onComplete;
		};

		ThreadPool();
		ThreadPool(std::size_t a_threads);
		~ThreadPool();

		TaskStatus task(const std::function<void()> &a_task);
		TaskStatus task(const std::function<void()> &a_task, const std::function<void()> &a_onComplete);

		typedef std::vector<TaskDefinition> TaskList;
		TaskStatus tasks(const TaskList &a_tasks, const std::function<void()> &a_onGroupComplete, bool a_groupFinishWaitForFrame = true);

		size_t run();

		size_t threads() const{
			return totalThreads;
		}
	private:
		std::recursive_mutex lock;
		boost::asio::io_service service;
		using asio_worker = std::unique_ptr<boost::asio::io_service::work>;
		asio_worker working;
		size_t totalThreads;
		std::list<ThreadTask> runningTasks;
		std::vector<std::unique_ptr<std::thread>> workers;
	};
}

#endif
