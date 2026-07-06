#pragma once

#ifndef __JAISCRIPT_DETAIL_THREAD_POOL_HPP__
#define __JAISCRIPT_DETAIL_THREAD_POOL_HPP__

// Persistent worker pool. Hoisted from MV::ThreadPool per Dev ruling 2026-07-06
// (parallel_design.md Q5: JaiScript owns the pool; MV may migrate to it later).
// Standard C++ only (std::thread/mutex/condition_variable) — no OS APIs.
//
// Two submission modes, because the parallel design's pinned worker engines must
// always run on their own thread (parallel_design.md §1):
//   submit(task)            — shared queue, any worker runs it.
//   submit_to(worker, task) — that worker's dedicated queue; ONLY worker `worker`'s
//                             thread ever runs it (stable std::this_thread::get_id()).
// Zero static state: the pool is a plain object; every queue, lock, and handler
// lives in the instance. Task exceptions are routed to on_exception (or swallowed),
// never allowed to kill a worker. Destruction drops queued-but-unstarted tasks,
// finishes in-flight ones, and joins every thread.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace jai {

    class thread_pool {
    public:
        using task_type = std::function<void()>;

        static size_t default_worker_count() noexcept {
            const unsigned hw = std::thread::hardware_concurrency();
            return hw > 1 ? hw - 1 : 1;
        }

        explicit thread_pool(size_t worker_count = 0) {
            if (worker_count == 0) worker_count = default_worker_count();
            workers_.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) workers_.push_back(std::make_unique<worker>());
            for (size_t i = 0; i < worker_count; ++i) {
                workers_[i]->thread = std::thread([this, i] { run_worker(i); });
                workers_[i]->id = workers_[i]->thread.get_id();
            }
        }

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

        ~thread_pool() {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                stop_ = true;
                outstanding_ -= shared_.size();
                shared_.clear();
                for (auto& w : workers_) {
                    outstanding_ -= w->pinned.size();
                    w->pinned.clear();
                }
            }
            work_cv_.notify_all();
            idle_cv_.notify_all();
            for (auto& w : workers_) {
                if (w->thread.joinable()) w->thread.join();
            }
        }

        size_t worker_count() const noexcept { return workers_.size(); }

        // The std::thread::id of worker `worker` — every submit_to(worker, ...) task
        // observes exactly this id from std::this_thread::get_id().
        std::thread::id worker_thread_id(size_t worker) const { return workers_[worker]->id; }

        // Shared queue: first free worker runs it.
        void submit(task_type task) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stop_) return;
                shared_.push_back(std::move(task));
                ++outstanding_;
            }
            work_cv_.notify_one();
        }

        // Pinned queue: runs on worker `worker`'s thread, in submission order,
        // interleaved with any shared tasks that worker also picks up.
        void submit_to(size_t worker, task_type task) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stop_) return;
                workers_[worker]->pinned.push_back(std::move(task));
                ++outstanding_;
            }
            // Pinned work satisfies only one worker's wake predicate; notify_all so the
            // target cannot lose the wake to an uninterested sibling consuming notify_one.
            work_cv_.notify_all();
        }

        // Receives every exception a task lets escape (worker stays alive either way).
        void on_exception(std::function<void(std::exception_ptr)> handler) {
            std::lock_guard<std::mutex> guard(mutex_);
            exception_handler_ = std::move(handler);
        }

        // Blocks until every submitted task (shared and pinned) has finished.
        void wait_idle() {
            std::unique_lock<std::mutex> guard(mutex_);
            idle_cv_.wait(guard, [this] { return outstanding_ == 0; });
        }

    private:
        struct worker {
            std::thread thread;
            std::thread::id id;
            std::deque<task_type> pinned;
        };

        void run_worker(size_t index) {
            worker& me = *workers_[index];
            for (;;) {
                task_type task;
                {
                    std::unique_lock<std::mutex> guard(mutex_);
                    work_cv_.wait(guard, [&] { return stop_ || !me.pinned.empty() || !shared_.empty(); });
                    if (stop_) return;
                    if (!me.pinned.empty()) {
                        task = std::move(me.pinned.front());
                        me.pinned.pop_front();
                    } else {
                        task = std::move(shared_.front());
                        shared_.pop_front();
                    }
                }
                try {
                    task();
                } catch (...) {
                    dispatch_exception(std::current_exception());
                }
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    --outstanding_;
                }
                idle_cv_.notify_all();
            }
        }

        void dispatch_exception(std::exception_ptr e) noexcept {
            std::function<void(std::exception_ptr)> handler;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                handler = exception_handler_;
            }
            if (handler) {
                try { handler(e); } catch (...) {}  // a throwing handler must not kill the worker either
            }
        }

        std::mutex mutex_;
        std::condition_variable work_cv_;
        std::condition_variable idle_cv_;
        std::vector<std::unique_ptr<worker>> workers_;
        std::deque<task_type> shared_;
        std::function<void(std::exception_ptr)> exception_handler_;
        size_t outstanding_ = 0;
        bool stop_ = false;
    };

} // namespace jai

#endif // __JAISCRIPT_DETAIL_THREAD_POOL_HPP__
