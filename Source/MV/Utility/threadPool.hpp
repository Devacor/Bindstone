#ifndef _MV_THREADPOOL_H_
#define _MV_THREADPOOL_H_

#include <thread>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
#include <list>
#include "log.h"

namespace MV {
	class ThreadPool {
		class Worker;
	public:
		class Job {
			friend Worker;
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
					continueMethod([=](){
						try { onFinish(); } catch (std::exception &e) { parent->exception(e); }
						try { (*onGroupFinish)(); } catch (std::exception &e) { parent->exception(e); }
					}, onFinishContinue);
				}else{
					if (onFinish) { continueMethod(onFinish, onFinishContinue); }
					if (groupFinished) { continueMethod(*onGroupFinish, onGroupFinishContinue); }
				}
			}
		private:
			const void continueMethod(const std::function<void()> &a_method, Continue a_plan){
				switch(a_plan){
					case Continue::IMMEDIATE:
						try { a_method(); } catch (std::exception &e) { parent->exception(e); }
					break;
					case Continue::MAIN_THREAD:
						parent->schedule(a_method);
					break;
					case Continue::POOL:
						parent->task(a_method);
					break;
				}
			}

			ThreadPool* parent;

			Continue onFinishContinue = Continue::MAIN_THREAD;
			Continue onGroupFinishContinue = Continue::MAIN_THREAD;
			std::function<void()> action;
			std::function<void()> onFinish;
			std::shared_ptr<std::atomic<size_t>> groupCounter;
			std::shared_ptr<std::function<void()>> onGroupFinish;
		};
		friend Job;
	private:
		class Worker {
		public:
			Worker(ThreadPool* a_parent) :
				parent(a_parent) {
				thread = std::make_unique<std::thread>([=]() { work(); });
			}
			~Worker() { if (thread && thread->joinable()) { thread->join(); } }

			void cleanup() {
				const double cleanupTimeout = 5.0;
				auto start = std::chrono::high_resolution_clock::now();
				while (!finished && std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() < cleanupTimeout) {
					std::this_thread::yield();
				}
				if(!finished){
					MV::error("Cleanup Timeout Exceeded in MV::ThreadPool::Worker!");
				}
			}
		private:
			void work() {
				finished = false;
				while (!parent->stopped) {
					std::unique_lock<std::mutex> guard(parent->lock);
					parent->notify.wait(guard, [=] {return parent->jobs.size() > 0 || parent->stopped; });
					if (parent->stopped) { break; }

					auto job = std::move(parent->jobs.front());
					parent->jobs.pop_front();
					guard.unlock();

					job.parent = parent;
					job();
					--parent->active;
				}
				finished = true;
			}
			ThreadPool* parent;
			bool finished = true;
			std::unique_ptr<std::thread> thread;
		};
		friend Worker;

	public:
		ThreadPool() :
			ThreadPool(std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1) {
		}

		ThreadPool(size_t a_threads) {
			for (size_t i = 0; i < a_threads; ++i) {
				workers.push_back(std::make_unique<Worker>(this));
			}
		}

		~ThreadPool() {
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.clear();
				stopped = true;
			}
			notify.notify_all();
			for (auto&& worker : workers) {
				worker->cleanup();
			}
		}

		void task(const Job &a_newWork) {
			++active;
			{
				std::lock_guard<std::mutex> guard(lock);
				jobs.emplace_back(std::move(a_newWork));
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
			active+=static_cast<int>(a_tasks.size());
			std::shared_ptr<std::atomic<size_t>> counter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
			std::shared_ptr<std::function<void()>> sharedGroupComplete = std::make_shared<std::function<void()>>(std::move(a_onGroupComplete));
			for (auto&& job : a_tasks)
			{
				{
					job.group(counter, sharedGroupComplete, a_continueCompletionCallback);
					std::lock_guard<std::mutex> guard(lock);
					jobs.push_back(std::move(job));
				}
				notify.notify_one();
			}
		}

		void tasks(const std::vector<Job> &a_tasks, const std::function<void()> &a_onGroupComplete, Job::Continue a_continueCompletionCallback = Job::Continue::POOL) {
			active+=static_cast<int>(a_tasks.size());
			std::shared_ptr<std::atomic<size_t>> counter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
			std::shared_ptr<std::function<void()>> sharedGroupComplete = std::make_shared<std::function<void()>>(std::move(a_onGroupComplete));
			for (auto&& job : a_tasks)
			{
				{
					std::lock_guard<std::mutex> guard(lock);
					jobs.push_back(job);
					jobs.back().group(counter, sharedGroupComplete, a_continueCompletionCallback);
				}
				notify.notify_one();
			}
		}

		void schedule(const std::function<void()> &a_method) {
			std::lock_guard<std::mutex> guard(scheduleLock);
			scheduled.push_back(a_method);
		}

		bool run() {
			auto wasActive = active > 0;
			auto endNode = scheduled.end();
			for (auto i = scheduled.begin(); i != endNode;) {
				try {
					(*i)();
				} catch (std::exception &e) {
					exception(e);
				}

				std::lock_guard<std::mutex> guard(scheduleLock);
				scheduled.erase(i++);
			}
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
		std::mutex scheduleLock;
		std::mutex exceptionLock;
		std::vector<std::unique_ptr<Worker>> workers;
		std::deque<Job> jobs;
		std::list<std::function<void()>> scheduled;
		std::deque<std::string> exceptionMessages;
		std::function<void(std::exception &e)> onException;
		std::atomic<int> active;
	};
}

#endif