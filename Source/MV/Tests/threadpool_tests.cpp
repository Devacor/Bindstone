// How MV drives jai::thread_pool: MV::initializeThreadPool, a decode on the pool whose upload continues on the main thread
// (SharedTextures::loadFile), the lobby's one-worker database pool whose continuations its update() drains, and the
// NetworkContext's network thread (DownloadPool, the lobby's email context). Foundry, built into mv_tests.exe.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/thread_pool.hpp>
#include "MV/Utility/log.h"
#include "MV/Utility/services.hpp"

#include "MV/Network/networkContext.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
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

	class CapturedErrors {
	public:
		CapturedErrors() :
			previous(MV::Logger::instance()),
			logger(MV::Severity::Debug, false) {

			logger.listen(MV::Severity::Error, [this](MV::Severity, const MV::LogData&, const std::string &a_message) {
				std::lock_guard<std::mutex> guard(lock);
				messages.push_back(a_message);
			});
			MV::Services::instance().connect(&logger);
		}
		~CapturedErrors() {
			MV::Services::instance().connect(previous);
		}

		std::vector<std::string> all() {
			std::lock_guard<std::mutex> guard(lock);
			return messages;
		}

	private:
		MV::Logger* previous;
		MV::Logger logger;
		std::mutex lock;
		std::vector<std::string> messages;
	};
}

class threadpool_suite : public suite {
public:
	threadpool_suite() : suite("MV Thread Pool") {}

	void forge_tests() override {
		test("an initialized pool parks at once and logs what its tasks and posted work throw", [this]() {
			CapturedErrors errors;
			{
				jai::thread_pool pool(2);
				MV::initializeThreadPool(pool);
				check(pool.spin_budget_ns() == 0, "MV's pools never spin between batches");
				pool.submit([]() { throw std::runtime_error("decode failed"); });
				pool.submit([]() { throw 7; });
				pool.wait_idle();
				pool.post([]() { throw std::runtime_error("upload failed"); });
				pool.run_posted();
			}
			auto messages = errors.all();
			check_eq(size_t{3}, messages.size());
			auto logged = [&](const std::string &a_text) {
				for (auto&& message : messages) {
					if (message.find(a_text) != std::string::npos) { return true; }
				}
				return false;
			};
			check(logged("Uncaught Exception in Thread Pool: decode failed"));
			check(logged("Uncaught Exception in Thread Pool: upload failed"));
			check(logged("non-standard exception"));
		});

		test("a decode on the pool continues on the main thread at the next drain, even when the decode throws", [this]() {
			jai::thread_pool pool;
			MV::initializeThreadPool(pool);
			CapturedErrors errors;
			const auto mainThread = std::this_thread::get_id();
			const int loads = 64;
			std::vector<int> decoded(loads, 0);
			std::atomic<int> uploadedOffMain{0};
			int uploaded = 0;
			for (int i = 0; i < loads; ++i) {
				pool.submit([&, i]() {
					if (i % 8 == 0) { throw std::runtime_error("bad image"); }
					decoded[i] = i + 1;
				}, [&, i]() {
					if (std::this_thread::get_id() != mainThread) { ++uploadedOffMain; }
					++uploaded;
				}, jai::thread_pool::then_on::posted);
			}
			check(waitFor([&]() { return uploaded == loads; }, [&]() { pool.run_posted(); }), "every load reaches its main-thread continuation");
			pool.wait_idle();
			check_eq(0, uploadedOffMain.load());
			for (int i = 0; i < loads; ++i) {
				check_eq(i % 8 == 0 ? 0 : i + 1, decoded[i]);
			}
			check_eq(size_t{loads / 8}, errors.all().size());
		});

		test("the lobby's one-worker database pool runs queries in order and its update() drains each continuation once", [this]() {
			jai::thread_pool dbPool(1);
			MV::initializeThreadPool(dbPool);
			const auto lobbyThread = std::this_thread::get_id();
			std::vector<int> queries;
			std::vector<int> continuations;
			std::atomic<int> continuationsOffLobby{0};
			std::atomic<int> concurrent{0};
			std::atomic<int> maxConcurrent{0};
			const int requests = 200;
			for (int i = 0; i < requests; ++i) {
				dbPool.submit([&, i]() {
					int now = ++concurrent;
					int seen = maxConcurrent.load();
					while (now > seen && !maxConcurrent.compare_exchange_weak(seen, now)) {}
					queries.push_back(i);
					--concurrent;
				}, [&, i]() {
					if (std::this_thread::get_id() != lobbyThread) { ++continuationsOffLobby; }
					continuations.push_back(i);
				}, jai::thread_pool::then_on::posted);
			}
			check(waitFor([&]() { return continuations.size() == static_cast<size_t>(requests); }, [&]() { dbPool.run_posted(); }), "every continuation must run");
			dbPool.run_posted();
			check_eq(1, maxConcurrent.load(), "one connection, one query at a time");
			check_eq(0, continuationsOffLobby.load());
			bool inOrder = queries.size() == static_cast<size_t>(requests) && continuations.size() == static_cast<size_t>(requests);
			for (int i = 0; inOrder && i < requests; ++i) {
				inOrder = queries[i] == i && continuations[i] == i;
			}
			check(inOrder, "queries and their continuations keep submission order");
		});

		test("a NetworkContext runs posted work in order on its one network thread, and its last owner may let go there", [this]() {
			auto context = std::make_shared<MV::NetworkContext>(1);
			std::mutex lock;
			std::vector<int> order;
			std::set<std::thread::id> threads;
			for (int i = 0; i < 100; ++i) {
				context->post([&, i]() {
					std::lock_guard<std::mutex> guard(lock);
					order.push_back(i);
					threads.insert(std::this_thread::get_id());
				});
			}
			check(waitFor([&]() { std::lock_guard<std::mutex> guard(lock); return order.size() == 100; }), "every task runs");
			{
				std::lock_guard<std::mutex> guard(lock);
				check(std::is_sorted(order.begin(), order.end()), "in the order posted");
				check(threads.size() == 1 && !threads.count(std::this_thread::get_id()), "on one thread that is not the poster's");
			}
			std::atomic<bool> released {false};
			auto* network = context.get();
			network->post([held = std::move(context), &released]() mutable {
				held.reset();
				released = true;
			});
			check(waitFor([&]() { return released.load(); }), "the context dies on its own network thread without joining itself");
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		});
	}
};

} // namespace mv_tests

using threadpool_suite = mv_tests::threadpool_suite;
FOUNDRY_REGISTER(threadpool_suite)
