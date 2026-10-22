#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include "MV/Network/networkReactor.h"
#include "MV/Utility/log.h"

#include <jaiscript/thread_pool.hpp>

#include <algorithm>
#include <stdexcept>

namespace MV {

	namespace {
#ifdef _WIN32
		using PollEntry = WSAPOLLFD;

		int pollSockets(PollEntry *a_entries, size_t a_count, int a_timeout) {
			return WSAPoll(a_entries, static_cast<ULONG>(a_count), a_timeout);
		}
#else
		using PollEntry = pollfd;

		int pollSockets(PollEntry *a_entries, size_t a_count, int a_timeout) {
			return ::poll(a_entries, static_cast<nfds_t>(a_count), a_timeout);
		}
#endif

		PollEntry pollEntry(SocketHandle a_handle, bool a_read, bool a_write) {
			PollEntry result {};
			result.fd = a_handle;
			result.events = static_cast<short>((a_read ? POLLIN : 0) | (a_write ? POLLOUT : 0));
			return result;
		}
	}

	// The wake socket is a UDP socket connected to itself on loopback: poll() waits on it the same way on every platform.
	NetworkMailbox::NetworkMailbox() {
		auto loopback = *SocketAddress::parse("127.0.0.1");
		std::error_code error;
		wakeSocket = Socket::udp(loopback, error);
		if (!error) {
			error = wakeSocket.bind(loopback);
		}
		std::optional<SocketAddress> bound;
		if (!error) {
			bound = wakeSocket.localAddress();
			error = bound ? wakeSocket.connect(*bound) : make_error_code(NetworkError::INVALID_ADDRESS);
		}
		if (!error) {
			error = wakeSocket.nonBlocking(true);
		}
		if (error) {
			throw std::system_error(error, "Network reactor wake socket");
		}
	}

	bool NetworkMailbox::post(std::function<void()> a_task) {
		{
			std::lock_guard<std::mutex> guard(lock);
			if (closed) {
				return false;
			}
			tasks.push_back(std::move(a_task));
		}
		wake();
		return true;
	}

	void NetworkMailbox::wake() {
		if (!wakePending.exchange(true)) {
			char signal = 1;
			std::error_code ignored;
			wakeSocket.send(&signal, 1, ignored);
		}
	}

	// The flag clears after the drain and before take(): a post that lands later sends a fresh byte.
	void NetworkMailbox::drainWake() {
		char discard[64];
		std::error_code error;
		while (wakeSocket.receive(discard, sizeof(discard), error) > 0 && !error) {
		}
		wakePending = false;
	}

	std::vector<std::function<void()>> NetworkMailbox::take() {
		std::vector<std::function<void()>> result;
		std::lock_guard<std::mutex> guard(lock);
		result.swap(tasks);
		return result;
	}

	std::vector<std::function<void()>> NetworkMailbox::close() {
		std::vector<std::function<void()>> result;
		std::lock_guard<std::mutex> guard(lock);
		closed = true;
		result.swap(tasks);
		return result;
	}

	NetworkReactor::NetworkReactor(size_t a_resolverThreads) :
		ourMailbox(std::make_shared<NetworkMailbox>()),
		resolverThreads(a_resolverThreads) {
	}

	// Watchers drop their callbacks before the posted work and timers die, so nothing calls back into a dying reactor.
	NetworkReactor::~NetworkReactor() {
		stop();
		auto unrun = ourMailbox->close();
		auto abandoned = std::move(watchers);
		watchers.clear();
		for (auto&& [pointer, watcher] : abandoned) {
			watcher->abandon();
		}
		resolvers.reset();
	}

	void NetworkReactor::stop() {
		stopping = true;
		ourMailbox->wake();
	}

	void NetworkReactor::run() {
		networkThread = std::this_thread::get_id();
		while (!stopping) {
			runPosted();
			runTimers();
			if (!stopping) {
				pollOnce();
			}
			retired.clear();
			retiredWatchers.clear();
		}
		networkThread = std::thread::id();
	}

	void NetworkReactor::runPosted() {
		ourMailbox->drainWake();
		auto running = ourMailbox->take();
		for (auto&& task : running) {
			try {
				task();
			} catch (std::exception &e) {
				error("Exception caught on the network thread: ", e.what());
			}
			task = nullptr;
		}
	}

	void NetworkReactor::runTimers() {
		auto now = Clock::now();
		while (!timers.empty() && timers.begin()->first.first <= now) {
			auto due = timers.begin();
			auto task = std::move(due->second);
			timerDeadlines.erase(due->first.second);
			timers.erase(due);
			try {
				task();
			} catch (std::exception &e) {
				error("Exception caught in a network timer: ", e.what());
			}
		}
	}

	int NetworkReactor::pollTimeoutMilliseconds() const {
		if (timers.empty()) {
			return -1;
		}
		auto wait = std::chrono::ceil<std::chrono::milliseconds>(timers.begin()->first.first - Clock::now()).count();
		return static_cast<int>(std::clamp<long long>(wait, 0, 60 * 1000));
	}

	void NetworkReactor::pollOnce() {
		std::vector<PollEntry> entries;
		std::vector<std::shared_ptr<NetworkWatcher>> polled;
		entries.reserve(watchers.size() + 1);
		polled.reserve(watchers.size());
		entries.push_back(pollEntry(ourMailbox->wakeSocket.handle(), true, false));
		for (auto&& [pointer, watcher] : watchers) {
			auto handle = watcher->watchedHandle();
			bool read = watcher->wantsRead();
			bool write = watcher->wantsWrite();
			if (handle != INVALID_SOCKET_HANDLE && (read || write)) {
				entries.push_back(pollEntry(handle, read, write));
				polled.push_back(watcher);
			}
		}
		auto ready = pollSockets(entries.data(), entries.size(), pollTimeoutMilliseconds());
		if (ready < 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (ready <= 0) {
			return;
		}
		for (size_t i = 0; i < polled.size(); ++i) {
			auto events = entries[i + 1].revents;
			if (events && watchers.count(polled[i].get())) {
				bool failed = (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
				polled[i]->ready((events & POLLIN) != 0 || failed, (events & POLLOUT) != 0 || failed);
			}
		}
	}

	NetworkReactor::TimerId NetworkReactor::schedule(Clock::time_point a_when, std::function<void()> a_task) {
		auto result = ++nextTimer;
		timers.emplace(std::make_pair(a_when, result), std::move(a_task));
		timerDeadlines.emplace(result, a_when);
		return result;
	}

	void NetworkReactor::cancel(TimerId a_timer) {
		if (auto found = timerDeadlines.find(a_timer); found != timerDeadlines.end()) {
			auto timer = timers.find(std::make_pair(found->second, a_timer));
			retired.push_back(std::move(timer->second));
			timers.erase(timer);
			timerDeadlines.erase(found);
		}
	}

	void NetworkReactor::watch(const std::shared_ptr<NetworkWatcher> &a_watcher) {
		watchers[a_watcher.get()] = a_watcher;
	}

	void NetworkReactor::unwatch(NetworkWatcher *a_watcher) {
		if (auto found = watchers.find(a_watcher); found != watchers.end()) {
			retiredWatchers.push_back(std::move(found->second));
			watchers.erase(found);
		}
	}

	void NetworkReactor::resolve(const std::string &a_host, const std::string &a_service, std::function<void(const std::error_code &, std::vector<SocketAddress>)> a_onResolved) {
		if (!resolvers) {
			resolvers = std::make_unique<jai::thread_pool>(resolverThreads);
			initializeThreadPool(*resolvers);
		}
		resolvers->submit([mailbox = ourMailbox, a_host, a_service, onResolved = std::move(a_onResolved)]() mutable {
			std::error_code error;
			auto addresses = resolveAddresses(a_host, a_service, error);
			mailbox->post([onResolved = std::move(onResolved), error, addresses = std::move(addresses)]() mutable {
				onResolved(error, std::move(addresses));
			});
		});
	}
}
