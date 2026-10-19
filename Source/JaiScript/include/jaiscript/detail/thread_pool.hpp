#pragma once

#ifndef __JAISCRIPT_DETAIL_THREAD_POOL_HPP__
#define __JAISCRIPT_DETAIL_THREAD_POOL_HPP__

// Persistent worker pool (internal/parallel_design.md Q5: JaiScript owns the pool).
// Standard C++ only (std::thread/mutex/condition_variable) — no OS APIs.
//
// Two submission modes, because the parallel design's pinned worker engines must
// always run on their own thread (internal/parallel_design.md §1):
//   submit(task)            — shared queue, any worker runs it.
//   submit_to(worker, task) — that worker's dedicated queue; ONLY worker `worker`'s
//                             thread ever runs it (stable std::this_thread::get_id()).
// Continuations: submit(task, then, where) and submit_batch(tasks, then, where) run `then`
// once after the task (every task of the batch) finished, threw or not, either on the worker
// that finished last (then_on::pool) or posted (then_on::posted). post(task) queues work for
// the host's own thread, which run_posted() drains; the posted queue has its own lock, so
// posting never contends with the workers.
// Zero static state: the pool is a plain object; every queue, lock, and handler
// lives in the instance. Task exceptions are routed to on_exception (or swallowed),
// never allowed to kill a worker. Destruction drops queued-but-unstarted tasks,
// finishes in-flight ones, and joins every thread.
//
// Wake protocol: a worker that just finished a task spins on its own flag for up to
// spin_budget before parking on the condition variable, and only when the pool's last two
// batches arrived within that budget (dense fork-join regions, a frame's worth of them);
// a pool whose regions come one per frame parks at once and burns nothing between them.
// The submitter wakes the kernel only for parked targets, and a worker pings the joiners
// only while one is parked; the flags and the parked-joiner count that decide this are read
// under the mutex the waiter sets them under, so no wake is lost. Task order and placement
// never change.

#include <jaiscript/detail/fault_injection.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <immintrin.h>
#endif

namespace jai {

    class thread_pool {
    public:
        using task_type = std::function<void()>;

        static size_t default_worker_count() noexcept {
            const unsigned hw = std::thread::hardware_concurrency();
            return hw > 1 ? hw - 1 : 1;
        }

        explicit thread_pool(size_t worker_count = 0, const detail::fault_injection* faults = nullptr) {
            if (worker_count == 0) worker_count = default_worker_count();
            try {
                if (faults) { faults->checkpoint("pool.worker_storage"); }
                workers_.reserve(worker_count);
                for (size_t i = 0; i < worker_count; ++i) {
                    if (faults) { faults->checkpoint("pool.worker_create", i); }
                    workers_.push_back(std::make_unique<worker>());
                }
                for (size_t i = 0; i < worker_count; ++i) {
                    if (faults) { faults->checkpoint("pool.thread_start", i); }
                    workers_[i]->thread = std::thread([this, i] { run_worker(i); });
                    workers_[i]->id = workers_[i]->thread.get_id();
                }
            } catch (...) {
                stop_and_join();
                throw;
            }
        }

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

        ~thread_pool() {
            stop_and_join();
        }

        size_t worker_count() const noexcept { return workers_.size(); }

        // How long a worker (and the joiner) spins for the next batch before parking, when
        // batches are arriving at least that densely. 0 parks immediately (the kernel wake
        // on every batch).
        void spin_budget(std::chrono::nanoseconds budget) noexcept { spin_budget_ns_.store(budget.count(), std::memory_order_relaxed); }
        std::chrono::nanoseconds spin_budget() const noexcept { return std::chrono::nanoseconds(spin_budget_ns_.load(std::memory_order_relaxed)); }

        // The std::thread::id of worker `worker` — every submit_to(worker, ...) task
        // observes exactly this id from std::this_thread::get_id().
        std::thread::id worker_thread_id(size_t worker) const { return workers_[worker]->id; }

        // Shared queue: first free worker runs it.
        void submit(task_type task) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stop_) return;
                shared_.push_back(std::move(task));
                outstanding_.fetch_add(1, std::memory_order_relaxed);
                shared_pending_.store(true, std::memory_order_release);
                note_batch();
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
                workers_[worker]->has_work.store(true, std::memory_order_release);
                outstanding_.fetch_add(1, std::memory_order_relaxed);
                note_batch();
            }
            // Pinned work satisfies only one worker's wake predicate; notify_all so the
            // target cannot lose the wake to an uninterested sibling consuming notify_one.
            work_cv_.notify_all();
        }

        // A fork-join region's whole pinned task set in ONE lock + at most ONE wake: the
        // per-task submit_to costs a W-worker region W lock round-trips and W
        // notify_alls (W x W wake predicate evaluations). Order within a worker's
        // queue matches the span's order. Spinning targets see their flag; the kernel
        // is asked only when a target is parked.
        void submit_pinned_batch(std::pair<size_t, task_type>* tasks, size_t count,
                                 const detail::fault_injection* faults = nullptr) {
            // Declare before the lock: exceptional unwinding unlocks FIRST, then wakes
            // the accepted prefix. Otherwise its owner can wait forever trying to join.
            struct notify_on_exit {
                std::condition_variable& changed;
                bool wake = true;
                ~notify_on_exit() { if (wake) { changed.notify_all(); } }
            } notify{work_cv_};
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stop_) { notify.wake = false; return; }
                bool any_parked = false;
                for (size_t i = 0; i < count; ++i) {
                    if (faults) { faults->checkpoint("pool.batch_enqueue", i); }
                    worker& w = *workers_[tasks[i].first];
                    w.pinned.push_back(std::move(tasks[i].second));
                    w.has_work.store(true, std::memory_order_release);
                    any_parked = any_parked || w.parked;
                    outstanding_.fetch_add(1, std::memory_order_relaxed);
                }
                note_batch();
                notify.wake = any_parked;
            }
        }

        // Where a continuation runs: on the pool worker that finished the work, or posted for
        // the thread that calls run_posted().
        enum class then_on { pool, posted };

        // `then` runs once after `task` returns or throws (the exception goes to on_exception).
        void submit(task_type task, task_type then, then_on where = then_on::pool) {
            submit([this, task = std::move(task), then = std::move(then), where]() mutable {
                run_reporting(task);
                continue_with(std::move(then), where);
            });
        }

        // The batch's tasks go on the shared queue under one lock; `then` runs exactly once,
        // after the last of them returns or throws. An empty batch continues at once.
        void submit_batch(std::vector<task_type> tasks, task_type then, then_on where = then_on::pool) {
            if (tasks.empty()) {
                if (then && where == then_on::posted) {
                    post(std::move(then));
                } else if (then) {
                    submit(std::move(then));
                }
                return;
            }
            auto batch = std::make_shared<batch_state>(tasks.size(), std::move(then), where);
            const size_t count = tasks.size();
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stop_) return;
                for (auto& task : tasks) {
                    shared_.push_back([this, batch, task = std::move(task)]() mutable {
                        run_reporting(task);
                        task = nullptr;
                        if (batch->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            continue_with(std::move(batch->then), batch->where);
                        }
                    });
                    outstanding_.fetch_add(1, std::memory_order_relaxed);
                }
                shared_pending_.store(true, std::memory_order_release);
                note_batch();
            }
            if (count >= workers_.size()) {
                work_cv_.notify_all();
            } else {
                for (size_t i = 0; i < count; ++i) { work_cv_.notify_one(); }
            }
        }

        // Queues `task` for the thread that drains run_posted(); any thread may post.
        void post(task_type task) {
            std::lock_guard<std::mutex> guard(posted_mutex_);
            posted_.push_back(std::move(task));
        }

        // Runs every posted task in order, including tasks posted while it runs, and returns how
        // many ran. A std::exception goes to on_exception; any other exception leaves with the
        // tasks not yet run queued ahead of later posts. Safe to call from inside a posted task.
        size_t run_posted() {
            size_t result = 0;
            std::deque<task_type> pending;
            for (;;) {
                {
                    std::lock_guard<std::mutex> guard(posted_mutex_);
                    if (posted_.empty()) { return result; }
                    pending.swap(posted_);
                }
                while (!pending.empty()) {
                    task_type task = std::move(pending.front());
                    pending.pop_front();
                    ++result;
                    try {
                        task();
                    } catch (const std::exception&) {
                        dispatch_exception(std::current_exception());
                    } catch (...) {
                        std::lock_guard<std::mutex> guard(posted_mutex_);
                        posted_.insert(posted_.begin(), std::make_move_iterator(pending.begin()), std::make_move_iterator(pending.end()));
                        throw;
                    }
                }
            }
        }

        // Receives every exception a task lets escape (worker stays alive either way).
        void on_exception(std::function<void(std::exception_ptr)> handler) {
            std::lock_guard<std::mutex> guard(mutex_);
            exception_handler_ = std::move(handler);
        }

        // Blocks until every submitted task (shared and pinned) has finished.
        void wait_idle() {
            if (spin_allowed()) {
                const int64_t budget = spin_budget_ns_.load(std::memory_order_relaxed);
                const auto t0 = std::chrono::steady_clock::now();
                while (outstanding_.load(std::memory_order_acquire) != 0) {
                    if (elapsed_ns(t0) > budget) { break; }
                    cpu_relax();
                }
            }
            std::unique_lock<std::mutex> guard(mutex_);
            ++joiners_parked_;
            idle_cv_.wait(guard, [this] { return outstanding_.load(std::memory_order_relaxed) == 0; });
            --joiners_parked_;
        }

    private:
        struct worker {
            std::thread thread;
            std::thread::id id;
            std::deque<task_type> pinned;
            std::atomic<bool> has_work{false};   // pinned queue non-empty (a spinning worker reads it lock-free)
            bool parked = false;                 // waiting on work_cv_; read and written under mutex_
        };

        struct batch_state {
            batch_state(size_t count, task_type continuation, then_on continuation_place) :
                remaining(count), then(std::move(continuation)), where(continuation_place) {}
            std::atomic<size_t> remaining;
            task_type then;
            then_on where;
        };

        void run_reporting(task_type& task) noexcept {
            try {
                task();
            } catch (...) {
                dispatch_exception(std::current_exception());
            }
        }

        // Called on the worker that finished the work: a pool continuation runs right there.
        void continue_with(task_type then, then_on where) {
            if (!then) { return; }
            if (where == then_on::posted) {
                post(std::move(then));
            } else {
                run_reporting(then);
            }
        }

        static void cpu_relax() noexcept {
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        }
        static int64_t elapsed_ns(std::chrono::steady_clock::time_point t0) noexcept {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        }
        // Under mutex_: the gap between this batch and the previous one decides whether the
        // workers spin for the next
        void note_batch() noexcept {
            const auto now = std::chrono::steady_clock::now();
            if (last_batch_.time_since_epoch().count() != 0) {
                last_gap_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_batch_).count(), std::memory_order_relaxed);
            }
            last_batch_ = now;
        }
        bool spin_allowed() const noexcept {
            const int64_t budget = spin_budget_ns_.load(std::memory_order_relaxed);
            return budget > 0 && last_gap_ns_.load(std::memory_order_relaxed) <= budget;
        }

        // Shared by normal destruction and failed construction. All worker storage
        // stays alive until every thread started so far has observed stop and joined.
        void stop_and_join() noexcept {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                stop_ = true;
                stop_flag_.store(true, std::memory_order_release);
                outstanding_.fetch_sub(shared_.size(), std::memory_order_relaxed);
                shared_.clear();
                for (auto& w : workers_) {
                    outstanding_.fetch_sub(w->pinned.size(), std::memory_order_relaxed);
                    w->pinned.clear();
                }
            }
            work_cv_.notify_all();
            idle_cv_.notify_all();
            for (auto& w : workers_) {
                if (w->thread.joinable()) w->thread.join();
            }
        }

        void run_worker(size_t index) {
            worker& me = *workers_[index];
            for (;;) {
                task_type task;
                {
                    std::unique_lock<std::mutex> guard(mutex_);
                    if (!stop_ && me.pinned.empty() && shared_.empty() && spin_allowed()) {
                        guard.unlock();
                        const int64_t budget = spin_budget_ns_.load(std::memory_order_relaxed);
                        const auto t0 = std::chrono::steady_clock::now();
                        while (!me.has_work.load(std::memory_order_acquire) &&
                               !shared_pending_.load(std::memory_order_acquire) &&
                               !stop_flag_.load(std::memory_order_acquire)) {
                            if (elapsed_ns(t0) > budget) { break; }
                            cpu_relax();
                        }
                        guard.lock();
                    }
                    while (!stop_ && me.pinned.empty() && shared_.empty()) {
                        me.parked = true;
                        work_cv_.wait(guard);
                        me.parked = false;
                    }
                    if (stop_) return;
                    if (!me.pinned.empty()) {
                        task = std::move(me.pinned.front());
                        me.pinned.pop_front();
                        if (me.pinned.empty()) { me.has_work.store(false, std::memory_order_relaxed); }
                    } else {
                        task = std::move(shared_.front());
                        shared_.pop_front();
                        if (shared_.empty()) { shared_pending_.store(false, std::memory_order_relaxed); }
                    }
                }
                try {
                    task();
                } catch (...) {
                    dispatch_exception(std::current_exception());
                }
                bool ping_joiner;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    outstanding_.fetch_sub(1, std::memory_order_release);
                    ping_joiner = joiners_parked_ != 0;
                }
                if (ping_joiner) { idle_cv_.notify_all(); }
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
        std::mutex posted_mutex_;
        std::deque<task_type> posted_;
        std::atomic<size_t> outstanding_{0};       // written under mutex_; the joiner's spin reads it
        std::atomic<bool> shared_pending_{false};
        std::atomic<bool> stop_flag_{false};
        std::atomic<int64_t> spin_budget_ns_{1000000};   // 1 ms
        std::atomic<int64_t> last_gap_ns_{INT64_MAX};
        std::chrono::steady_clock::time_point last_batch_{};
        size_t joiners_parked_ = 0;                // under mutex_; a count: a waiter that returns never hides one still parked
        bool stop_ = false;
    };

} // namespace jai

#endif // __JAISCRIPT_DETAIL_THREAD_POOL_HPP__
