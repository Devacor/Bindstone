#ifndef _MV_NETWORK_CONTEXT_H_
#define _MV_NETWORK_CONTEXT_H_

#include <cstddef>
#include <memory>

namespace MV {
	// An asio io_context and the worker threads that run it, with no asio in this header: networking .cpp files reach the
	// context through networkContextAsio.h. Downloads, email and anything else asynchronous share one by holding it.
	class NetworkContext {
	public:
		struct Asio;

		// 0 threads = jai::thread_pool's default worker count
		explicit NetworkContext(size_t a_threads = 1);
		~NetworkContext();

		NetworkContext(const NetworkContext&) = delete;
		NetworkContext& operator=(const NetworkContext&) = delete;

		Asio& asio() { return *ourAsio; }

	private:
		std::unique_ptr<Asio> ourAsio;
	};
}

#endif
