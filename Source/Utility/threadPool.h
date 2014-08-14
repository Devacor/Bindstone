#ifndef _MV_THREADPOOL_H_
#define _MV_THREADPOOL_H_

#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

#include "boost/asio.hpp"

namespace MV{

	class ThreadPool {
	public:
		ThreadPool();
		ThreadPool(std::size_t);
		~ThreadPool();

		void task(std::function<void ()> a_task);
		void task(std::function<void()> a_task, std::function<void()> a_onComplete);

		size_t run();

		size_t threads() const{
			return totalThreads;
		}
	private:

		class ThreadTask {
		public:
			ThreadTask(std::function<void()> a_call);
			ThreadTask(std::function<void()> a_call, std::function<void()> a_onFinish);

			void operator()();

			bool finished();
		private:
			std::mutex lock;

			std::atomic<bool> justFinished;
			std::function<void()> call;
			std::function<void()> onFinish;
		};

		boost::asio::io_service service;
		using asio_worker = std::unique_ptr<boost::asio::io_service::work>;
		asio_worker working;
		size_t totalThreads;
		std::list<std::shared_ptr<ThreadTask>> runningTasks;
		std::vector<std::unique_ptr<std::thread>> workers;
	};
}

#endif
