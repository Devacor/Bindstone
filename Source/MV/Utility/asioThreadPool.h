#ifndef _MV_ASIOTHREADPOOL_H_
#define _MV_ASIOTHREADPOOL_H_

#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include "log.h"
#include "threadPool.hpp"
#include <asio.hpp>

namespace MV {
	struct ThreadPoolDetails;

	class AsioThreadPool {
	public:
		class Job {
			friend AsioThreadPool;
		public:
			Job(const std::function<void()> &a_action) :
				action(a_action) {
			}
			Job(const std::function<void()> &a_action, const std::function<void()> &a_onFinish) :
				action(a_action),
				onFinish(a_onFinish) {
			}
			Job(const std::function<void()> &a_action, const std::function<void()> &a_onFinish, const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish) :
				action(a_action),
				onFinish(a_onFinish),
				groupCounter(a_groupCounter),
				onGroupFinish(a_onGroupFinish) {
			}
			Job(const std::function<void()> &a_action, const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish) :
				action(a_action),
				groupCounter(a_groupCounter),
				onGroupFinish(a_onGroupFinish) {
			}
			Job(Job&& a_rhs) = default;
			Job(const Job& a_rhs) = default;

			void group(const std::shared_ptr<std::atomic<size_t>> &a_groupCounter, const std::shared_ptr<std::function<void()>> &a_onGroupFinish) {
				groupCounter = a_groupCounter;
				onGroupFinish = a_onGroupFinish;
			}

			void operator()() noexcept {
				try { action(); } catch (std::exception &e) { parent->exception(e); }
				bool groupFinished = groupCounter && --(*groupCounter) == 0 && onGroupFinish && *onGroupFinish;
				if (onFinish && !groupFinished) {
					parent->task(onFinish);
				} else if (onFinish && groupFinished) {
					//by value: the continuation outlives this job, which dies with the handler that ran it
					parent->task([parent = parent, onFinish = onFinish, onGroupFinish = onGroupFinish]{
						try { onFinish(); } catch (std::exception &e) { parent->exception(e); }
						try { (*onGroupFinish)(); } catch (std::exception &e) { parent->exception(e); }
					});
				} else if (groupFinished) {
					parent->task((*onGroupFinish));
				}
			}
		private:
			AsioThreadPool* parent = nullptr;

			std::function<void()> action;
			std::function<void()> onFinish;
			std::shared_ptr<std::atomic<size_t>> groupCounter;
			std::shared_ptr<std::function<void()>> onGroupFinish;
		};
		friend Job;
	public:
		AsioThreadPool() :
			AsioThreadPool(std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1) {
		}

		AsioThreadPool(size_t a_threads);

		AsioThreadPool(const AsioThreadPool&) = delete;
		AsioThreadPool& operator=(const AsioThreadPool&) = delete;

		~AsioThreadPool();

		void task(Job a_newWork);
		void task(const std::function<void()> &a_task) {
			task(Job{ a_task });
		}
		void task(const std::function<void()> &a_task, const std::function<void()> &a_onComplete) {
			task(Job{ a_task, a_onComplete });
		}

		void tasks(const std::vector<Job> &a_tasks, const std::function<void()> &a_onGroupComplete) {
			std::shared_ptr<std::atomic<size_t>> counter = std::make_shared<std::atomic<size_t>>(a_tasks.size());
			std::shared_ptr<std::function<void()>> sharedGroupComplete = std::make_shared<std::function<void()>>(a_onGroupComplete);
			for (auto&& job : a_tasks) {
				task(Job{job.action, job.onFinish, counter, sharedGroupComplete});
			}
		}

		void schedule(const std::function<void()> &a_method) {
			scheduled.push(a_method);
		}

		void run() {
			scheduled.run([this](std::exception &e) { exception(e); });
		}

		void exceptionHandler(std::function<void(std::exception &e)> a_onException) {
			std::lock_guard<std::mutex> guard(exceptionLock);
			onException = a_onException;
		}

		size_t threads() const {
			return workers.size();
		}

		std::shared_ptr<asio::io_context> io_context() const;
	private:
		void stopAndJoin() noexcept;

		void exception(std::exception &e) {
			std::lock_guard<std::mutex> guard(exceptionLock);
			if (onException) {
				onException(e);
			} else {
				MV::error("Uncaught Exception in Thread Pool: ", e.what());
			}
		}

		std::unique_ptr<ThreadPoolDetails> details;
		std::mutex exceptionLock;
		ScheduledCallbacks scheduled;
		std::function<void(std::exception &e)> onException;
		std::vector<std::thread> workers;
	};
}

#endif