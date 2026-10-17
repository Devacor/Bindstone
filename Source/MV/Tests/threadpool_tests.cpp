// MV::ThreadPool and MV::AsioThreadPool regression tests, Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/asioThreadPool.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace jai::foundry;

namespace mv_tests {

namespace {
	template<typename Condition, typename Pump>
	bool waitFor(Condition a_condition, Pump a_pump, std::chrono::milliseconds a_timeout = std::chrono::milliseconds(10000)) {
		auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (!a_condition()) {
			if (std::chrono::steady_clock::now() > deadline) {
				return false;
			}
			a_pump();
			std::this_thread::yield();
		}
		return true;
	}

	template<typename Condition>
	bool waitFor(Condition a_condition) {
		return waitFor(a_condition, []() {});
	}
}

class threadpool_suite : public suite {
public:
	threadpool_suite() : suite("MV ThreadPool") {}

	void forge_tests() override {
		test("a group job's finish callback and the group completion run once each, after the job that queued them is gone", [this]() {
			for (auto continuation : {MV::ThreadPool::Job::Continue::POOL, MV::ThreadPool::Job::Continue::MAIN_THREAD}) {
				MV::ThreadPool pool(1);
				std::atomic<int> exceptions{0};
				pool.exceptionHandler([&](std::exception&) { ++exceptions; });
				std::atomic<int> finished{0};
				std::atomic<int> groupFinished{0};
				std::vector<MV::ThreadPool::Job> jobs;
				jobs.emplace_back([]() {}, [&]() { ++finished; }, continuation);
				pool.tasks(std::move(jobs), [&]() { ++groupFinished; }, continuation);
				check(waitFor([&]() { return finished.load() == 1 && groupFinished.load() == 1; }, [&]() { pool.run(); }), "both callbacks must run");
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				pool.run();
				check_eq(1, finished.load());
				check_eq(1, groupFinished.load());
				check_eq(0, exceptions.load(), "a callback must not see another job's (or a destroyed job's) members");
			}
		});

		test("the group completion runs exactly once after every job of a large group", [this]() {
			MV::ThreadPool pool(4);
			for (int round = 0; round < 200; ++round) {
				std::atomic<int> ran{0};
				std::atomic<int> sawAll{0};
				std::atomic<int> completions{0};
				std::vector<MV::ThreadPool::Job> jobs;
				for (int i = 0; i < 16; ++i) {
					jobs.emplace_back([&]() { ++ran; });
				}
				pool.tasks(std::move(jobs), [&]() {
					if (ran.load() == 16) { ++sawAll; }
					++completions;
				});
				check(waitFor([&]() { return completions.load() >= 1; }), "completion must run");
				check_eq(16, ran.load());
				check_eq(1, sawAll.load());
				check_eq(1, completions.load());
			}
		});

		test("an empty group still completes", [this]() {
			MV::ThreadPool pool(1);
			std::atomic<int> immediate{0};
			std::atomic<int> onPool{0};
			std::atomic<int> onMain{0};
			pool.tasks(std::vector<MV::ThreadPool::Job>{}, [&]() { ++immediate; }, MV::ThreadPool::Job::Continue::IMMEDIATE);
			pool.tasks(std::vector<MV::ThreadPool::Job>{}, [&]() { ++onPool; }, MV::ThreadPool::Job::Continue::POOL);
			pool.tasks(std::vector<MV::ThreadPool::Job>{}, [&]() { ++onMain; }, MV::ThreadPool::Job::Continue::MAIN_THREAD);
			check_eq(1, immediate.load());
			check(waitFor([&]() { return onPool.load() == 1 && onMain.load() == 1; }, [&]() { pool.run(); }), "POOL and MAIN_THREAD completions must run");
		});

		test("a scheduled callback that pumps run() again runs nothing twice", [this]() {
			MV::ThreadPool pool(1);
			std::vector<char> order;
			pool.schedule([&]() {
				order.push_back('a');
				pool.run();
			});
			pool.schedule([&]() { order.push_back('b'); });
			pool.run();
			pool.run();
			check_eq(size_t{2}, order.size());
			check(order.size() == 2 && order[0] == 'a' && order[1] == 'b', "each callback runs once, in order");
		});

		test("a callback scheduled from inside run() runs in the same run()", [this]() {
			MV::ThreadPool pool(1);
			std::vector<int> order;
			pool.schedule([&]() {
				order.push_back(1);
				pool.schedule([&]() { order.push_back(3); });
			});
			pool.schedule([&]() { order.push_back(2); });
			pool.run();
			check(order == std::vector<int>{1, 2, 3}, "FIFO, including work scheduled during the pass");
		});

		test("callbacks scheduled from other threads while run() drains each run once, in each thread's order", [this]() {
			MV::ThreadPool pool(1);
			const int producers = 4;
			const int perProducer = 20000;
			std::vector<std::vector<int>> seen(producers);
			std::atomic<int> total{0};
			std::atomic<bool> go{false};
			std::vector<std::thread> threads;
			for (int p = 0; p < producers; ++p) {
				threads.emplace_back([&, p]() {
					while (!go.load()) { std::this_thread::yield(); }
					for (int i = 0; i < perProducer; ++i) {
						pool.schedule([&, p, i]() {
							seen[p].push_back(i);
							++total;
						});
					}
				});
			}
			go = true;
			bool drained = waitFor([&]() { return total.load() == producers * perProducer; }, [&]() { pool.run(); });
			for (auto&& thread : threads) { thread.join(); }
			pool.run();
			check(drained, "every scheduled callback must run");
			check_eq(producers * perProducer, total.load());
			for (int p = 0; p < producers; ++p) {
				bool inOrder = static_cast<int>(seen[p].size()) == perProducer;
				for (int i = 0; inOrder && i < perProducer; ++i) {
					inOrder = seen[p][i] == i;
				}
				check(inOrder, "one producer's callbacks run once each in its order");
			}
		});

		test("pool jobs that schedule main-thread work and more pool work under load lose nothing", [this]() {
			MV::ThreadPool pool(4);
			const int jobs = 5000;
			std::atomic<int> onMain{0};
			std::atomic<int> chained{0};
			for (int i = 0; i < jobs; ++i) {
				pool.task(MV::ThreadPool::Job([&]() {
					pool.task([&]() { ++chained; });
				}, [&]() { ++onMain; }, MV::ThreadPool::Job::Continue::MAIN_THREAD));
			}
			check(waitFor([&]() { return onMain.load() == jobs && chained.load() == jobs; }, [&]() { pool.run(); }), "every continuation must run");
		});

		test("the destructor finishes an in-flight job while every pool member is still alive", [this]() {
			// Runs past 5 s: a destructor that stops waiting on a timeout destroys the handler and queues under it.
			std::atomic<int> handled{0};
			std::atomic<bool> started{false};
			{
				MV::ThreadPool pool(1);
				pool.exceptionHandler([&](std::exception&) { ++handled; });
				pool.task([&]() {
					started = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(5300));
					pool.schedule([]() {});
					pool.task([]() {});
					throw std::runtime_error("late failure");
				});
				check(waitFor([&]() { return started.load(); }), "the job must start");
			}
			check_eq(1, handled.load(), "the in-flight job's exception must reach the live handler");
		});

		test("destruction drops queued jobs and joins with no work, idle work and a full queue", [this]() {
			{
				MV::ThreadPool idle(3);
			}
			std::atomic<int> ran{0};
			{
				MV::ThreadPool pool(2);
				for (int i = 0; i < 1000; ++i) {
					pool.task([&]() {
						++ran;
						std::this_thread::sleep_for(std::chrono::microseconds(50));
					});
				}
			}
			check(ran.load() <= 1000);
			check_eq(size_t{3}, MV::ThreadPool(3).threads());
		});

		test("AsioThreadPool: a group job's finish callback and the group completion run once each", [this]() {
			MV::AsioThreadPool pool(1);
			std::atomic<int> exceptions{0};
			pool.exceptionHandler([&](std::exception&) { ++exceptions; });
			for (int round = 0; round < 20; ++round) {
				std::atomic<int> finished{0};
				std::atomic<int> groupFinished{0};
				std::vector<MV::AsioThreadPool::Job> jobs;
				jobs.emplace_back([]() {}, [&]() { ++finished; });
				pool.tasks(jobs, [&]() { ++groupFinished; });
				check(waitFor([&]() { return finished.load() == 1 && groupFinished.load() == 1; }), "both callbacks must run");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				check_eq(1, finished.load());
				check_eq(1, groupFinished.load());
			}
			check_eq(0, exceptions.load(), "a callback must not see another job's (or a destroyed job's) members");
		});

		test("AsioThreadPool: scheduled callbacks from other threads each run once while run() drains", [this]() {
			MV::AsioThreadPool pool(1);
			std::atomic<int> total{0};
			std::thread producer([&]() {
				for (int i = 0; i < 20000; ++i) {
					pool.schedule([&]() { ++total; });
				}
			});
			bool drained = waitFor([&]() { return total.load() == 20000; }, [&]() { pool.run(); });
			producer.join();
			check(drained, "every scheduled callback must run");
			check_eq(20000, total.load());
		});
	}
};

} // namespace mv_tests

using threadpool_suite = mv_tests::threadpool_suite;
FOUNDRY_REGISTER(threadpool_suite)
