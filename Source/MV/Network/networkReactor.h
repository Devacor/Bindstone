#ifndef _MV_NETWORK_REACTOR_H_
#define _MV_NETWORK_REACTOR_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>
#include "MV/Network/socket.h"

namespace jai {
	class thread_pool;
}

namespace MV {
	// A socket the reactor polls. Every call comes from the network thread.
	class NetworkWatcher {
	public:
		virtual ~NetworkWatcher() = default;

		virtual SocketHandle watchedHandle() const = 0;
		virtual bool wantsRead() const = 0;
		virtual bool wantsWrite() const = 0;
		virtual void ready(bool a_readable, bool a_writable) = 0;
		// The reactor is going away: close the socket and drop every callback, which may own this watcher's owner.
		virtual void abandon() = 0;
	};

	// The thread-safe door into a reactor: work posted here runs on its network thread, in order. It outlives the reactor
	// so a late post from another thread is dropped instead of reaching a dead reactor.
	class NetworkMailbox {
	public:
		NetworkMailbox();

		// False, dropping a_task, once the reactor is gone.
		bool post(std::function<void()> a_task);

	private:
		friend class NetworkReactor;

		void wake();
		void drainWake();
		std::vector<std::function<void()>> take();
		std::vector<std::function<void()>> close();

		std::mutex lock;
		std::vector<std::function<void()>> tasks;
		bool closed = false;
		std::atomic<bool> wakePending {false};
		Socket wakeSocket;
	};

	// One network thread's poll loop: sockets (WSAPoll / poll), timers and posted work. run() makes the calling thread the
	// network thread until stop(); watch, schedule, cancel and resolve belong to that thread (or to the thread that will
	// call run(), before it does).
	class NetworkReactor {
	public:
		using TimerId = uint64_t;
		using Clock = std::chrono::steady_clock;

		// a_resolverThreads resolve host names (made on the first resolve); 0 = jai::thread_pool's default worker count.
		explicit NetworkReactor(size_t a_resolverThreads = 1);
		~NetworkReactor();

		NetworkReactor(const NetworkReactor&) = delete;
		NetworkReactor& operator=(const NetworkReactor&) = delete;

		void post(std::function<void()> a_task) {
			ourMailbox->post(std::move(a_task));
		}

		const std::shared_ptr<NetworkMailbox>& mailbox() const {
			return ourMailbox;
		}

		bool runningInThisThread() const {
			return networkThread.load() == std::this_thread::get_id();
		}

		void run();
		// From any thread; run() returns after the work in hand, and a stopped reactor stays stopped.
		void stop();

		TimerId schedule(Clock::time_point a_when, std::function<void()> a_task);
		TimerId scheduleAfter(std::chrono::milliseconds a_delay, std::function<void()> a_task) {
			return schedule(Clock::now() + a_delay, std::move(a_task));
		}
		// Cancelling a timer that already ran (or id 0) does nothing.
		void cancel(TimerId a_timer);

		void watch(const std::shared_ptr<NetworkWatcher> &a_watcher);
		void unwatch(NetworkWatcher *a_watcher);

		// a_onResolved runs on the network thread.
		void resolve(const std::string &a_host, const std::string &a_service, std::function<void(const std::error_code &, std::vector<SocketAddress>)> a_onResolved);

	private:
		void runPosted();
		void runTimers();
		int pollTimeoutMilliseconds() const;
		void pollOnce();

		std::shared_ptr<NetworkMailbox> ourMailbox;
		std::atomic<bool> stopping {false};
		std::atomic<std::thread::id> networkThread;

		TimerId nextTimer = 0;
		std::map<std::pair<Clock::time_point, TimerId>, std::function<void()>> timers;
		std::unordered_map<TimerId, Clock::time_point> timerDeadlines;

		std::unordered_map<NetworkWatcher*, std::shared_ptr<NetworkWatcher>> watchers;
		// Released after the pass that cancelled or unwatched them: the canceller may be what they keep alive.
		std::vector<std::function<void()>> retired;
		std::vector<std::shared_ptr<NetworkWatcher>> retiredWatchers;

		size_t resolverThreads;
		std::unique_ptr<jai::thread_pool> resolvers;
	};
}

#endif
