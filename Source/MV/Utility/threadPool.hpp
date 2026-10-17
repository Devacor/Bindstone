#ifndef _MV_THREADPOOL_H_
#define _MV_THREADPOOL_H_

#include <thread>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include "log.h"

namespace MV {
	//Callbacks any thread queues for the thread that calls run(), FIFO. Work queued while run() drains runs in the same run().
	class ScheduledCallbacks {
	public:
		void push(const std::function<void()> &a_method) {
			std::lock_guard<std::mutex> guard(lock);
			scheduled.push_back(a_method);
		}

		template<typename OnException>
		void run(OnException &&a_onException) {
			std::list<std::function<void()>> pending;
			for (;;) {
				{
					std::lock_guard<std::mutex> guard(lock);
					if (scheduled.empty()) { return; }
					pending.swap(scheduled);
				}
				while (!pending.empty()) {
					auto method = std::move(pending.front());
					pending.pop_front();
					try {
						method();
					} catch (std::exception &e) {
						a_onException(e);
					} catch (...) {
						std::lock_guard<std::mutex> guard(lock);
						scheduled.splice(scheduled.begin(), pending);
						throw;
					}
				}
			}
		}

	private:
		std::mutex lock;
		std::list<std::function<void()>> scheduled;
	};

	class ThreadPool {
	public:
		class Job {
			friend ThreadPool;
		public:
			enum class Continue {
				MAIN_THREAD,
				POOL,
				IMMEDIATE
			};

			Job(const std::function<void()> &a_action) :
				action(a_action) {
			}
			Job(const std::function<void()> &a_action, const std::function<void()> &a_onFinish, Continue a_continueMethod = Continue::POOL) :
				action(a_action),
				onFinish(a_onFinish),
				onFinishContinue(a_continueMethod){
			}
			Job(Job&& a_rhs) = default;
			Job(const Job& a_rhs) = default;

			void group(const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish, Continue a_continueMethod = Continue::POOL) {
				onGroupFinishContinue = a_continueMethod;
				groupCounter = a_groupCounter;
				onGroupFinish = a_onGroupFinish;
			}

			void operator()() noexcept {
				try { action(); } catch (std::exception &e) { parent->exception(e); }
				bool groupFinished = groupCounter && --(*groupCounter) == 0 && onGroupFinish && *onGroupFinish;
				if (onFinish && groupFinished && onFinishContinue == onGroupFinishContinue) {
					//by value: the continuation outlives this job, which the worker destroys once it returns
					continueMethod(parent, [parent = parent, onFinish = onFinish, onGroupFinish = onGroupFinish](){
						try { onFinish(); } catch (std::exception &e) { parent->exception(e); }
						try { (*onGroupFinish)(); } catch (std::exception &e) { parent->exception(e); }
					}, onFinishContinue);
				}else{
					if (onFinish) { continueMethod(parent, onFinish, onFinishContinue); }
					if (groupFinished) { continueMethod(parent, *onGroupFinish, onGroupFinishContinue); }
				}
			}
		private:
			static void continueMethod(ThreadPool* a_pool, const std::function<void()> &a_method, Continue a_plan){
				switch(a_plan){
					case Continue::IMMEDIATE:
						try { a_method(); } catch (std::exception &e) { a_pool->exception(e); }
					break;
					case Continue::MAIN_THREAD:
						a_pool->schedule(a_method);
					break;
					case Continue::POOL:
						a_pool->task(a_method);
					break;
				}
			}

			ThreadPool* parent = nullptr;

			Continue onFinishContinue = Continue::MAIN_THREAD;
			Continue onGroupFinishContinue = Continue::MAIN_THREAD;
			std::function<void()> action;
			std::function<void()> onFinish;
			std::shared_ptr<std::atomic<size_t>> groupCounter;
			std::shared_ptr<std::function<void()>> onGroupFinish;
		};
		friend Job;

		ThreadPool() :
			ThreadPool(std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1) {
		}

		ThreadPool(size_t a_threads) {
			try {
				workers.reserve(a_threads);
				for (size_t i = 0; i < a_threads; ++i) {
					workers.emplace_back([this]() { work(); });
				}
			} catch (...) {
				stopAndJoin();
				throw;
			}
		}

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;

		//Drops queued jobs; in-flight jobs finish against a fully alive pool before any member dies.
		~ThreadPool() {
			stopAndJoin();
		}

		void task(Job&& a_newWork) {
			++active;
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.emplace_back(std::move(a_newWork));
			}
			notify.notify_one();
		}
		void task(const Job &a_newWork) {
			++active;
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.emplace_back(a_newWork);
			}
			notify.notify_one();
		}
		void task(const std::function<void()> &a_task) {
			++active;
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.emplace_back(a_task);
			}
			notify.notify_one();
		}
		void task(const std::function<void()> &a_task, const std::function<void()> &a_onComplete) {
			++active;
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.emplace_back(a_task, a_onComplete);
			}
			notify.notify_one();
		}

		void tasks(std::vector<Job> &&a_tasks, const std::function<void()> &a_onGroupComplete, Job::Continue a_continueCompletionCallback = Job::Continue::POOL) {
			if (a_tasks.empty()) {
				completeEmptyGroup(a_onGroupComplete, a_continueCompletionCallback);
				return;
			}
			active+=static_cast<int>(a_tasks.size());
			std::shared_ptr<std::atomic<size_t>> counter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
			std::shared_ptr<std::function<void()>> sharedGroupComplete = std::make_shared<std::function<void()>>(a_onGroupComplete);
			{
				std::lock_guard<std::mutex> guard(lock);
				for (auto&& job : a_tasks) {
					job.group(counter, sharedGroupComplete, a_continueCompletionCallback);
					jobs.push_back(std::move(job));
				}
			}
			notify.notify_all();
		}

		void tasks(const std::vector<Job> &a_tasks, const std::function<void()> &a_onGroupComplete, Job::Continue a_continueCompletionCallback = Job::Continue::POOL) {
			if (a_tasks.empty()) {
				completeEmptyGroup(a_onGroupComplete, a_continueCompletionCallback);
				return;
			}
			active+=static_cast<int>(a_tasks.size());
			std::shared_ptr<std::atomic<size_t>> counter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
			std::shared_ptr<std::function<void()>> sharedGroupComplete = std::make_shared<std::function<void()>>(a_onGroupComplete);
			{
				std::lock_guard<std::mutex> guard(lock);
				for (auto&& job : a_tasks) {
					jobs.push_back(job);
					jobs.back().group(counter, sharedGroupComplete, a_continueCompletionCallback);
				}
			}
			notify.notify_all();
		}

		void schedule(const std::function<void()> &a_method) {
			scheduled.push(a_method);
		}

		bool run() {
			auto wasActive = active > 0;
			scheduled.run([this](std::exception &e) { exception(e); });
			return wasActive;
		}

		void exceptionHandler(std::function<void(std::exception &e)> a_onException) {
			std::lock_guard<std::mutex> guard(exceptionLock);
			onException = a_onException;
		}

		size_t threads() const {
			return workers.size();
		}
	private:
		void work() {
			for (;;) {
				std::unique_lock<std::mutex> guard(lock);
				notify.wait(guard, [this] { return !jobs.empty() || stopped; });
				if (stopped) { return; }

				auto job = std::move(jobs.front());
				jobs.pop_front();
				guard.unlock();

				job.parent = this;
				job();
				--active;
			}
		}

		void stopAndJoin() noexcept {
			std::deque<Job> abandoned;
			{
				std::lock_guard<std::mutex> guard(lock);
				stopped = true;
				abandoned.swap(jobs);
			}
			notify.notify_all();
			for (auto&& worker : workers) {
				if (worker.joinable()) { worker.join(); }
			}
		}

		void completeEmptyGroup(const std::function<void()> &a_onGroupComplete, Job::Continue a_continueCompletionCallback) {
			if (a_onGroupComplete) {
				Job::continueMethod(this, a_onGroupComplete, a_continueCompletionCallback);
			}
		}

		void exception(std::exception &e) {
			std::lock_guard<std::mutex> guard(exceptionLock);
			if (onException) {
				onException(e);
			} else {
				MV::error("Uncaught Exception in Thread Pool: ", e.what());
			}
		}

		std::condition_variable notify;
		bool stopped = false;
		std::mutex lock;
		std::mutex exceptionLock;
		std::deque<Job> jobs;
		ScheduledCallbacks scheduled;
		std::function<void(std::exception &e)> onException;
		std::atomic<int> active {0};
		std::vector<std::thread> workers;
	};
}

#endif
