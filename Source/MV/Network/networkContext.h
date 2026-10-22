#ifndef _MV_NETWORK_CONTEXT_H_
#define _MV_NETWORK_CONTEXT_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

namespace MV {
	class NetworkReactor;

	// A network thread running a NetworkReactor (sockets, timers and posted work), plus the threads that resolve host
	// names. Downloads, email and anything else asynchronous share one by holding it.
	class NetworkContext {
	public:
		// a_threads resolve host names; 0 = jai::thread_pool's default worker count
		explicit NetworkContext(size_t a_threads = 1);
		~NetworkContext();

		NetworkContext(const NetworkContext&) = delete;
		NetworkContext& operator=(const NetworkContext&) = delete;

		NetworkReactor& reactor() {
			return *ourReactor;
		}

		// Runs a_task on the network thread, after the work posted before it; from any thread.
		void post(std::function<void()> a_task);

		// Ends the network thread and joins it (unless called on it); sockets and unrun work wait for destruction.
		void stop();

	private:
		std::unique_ptr<NetworkReactor> ourReactor;
		std::thread worker;
	};
}

#endif
