#include "MV/Network/networkContextAsio.h"
#include "MV/Utility/log.h"

#include <thread>

namespace MV {

	NetworkContext::NetworkContext(size_t a_threads) :
		ourAsio(std::make_unique<Asio>(a_threads)) {
		initializeThreadPool(ourAsio->workers);
		for (size_t i = 0; i < ourAsio->workers.worker_count(); ++i) {
			ourAsio->workers.submit([context = ourAsio.get()] { context->ioContext.run(); });
		}
	}

	// The context stops before the workers join (the pool's destructor), so every run() returns first. The last owner
	// can be a handler on one of those workers, which cannot join itself: another thread joins once its run() returns.
	NetworkContext::~NetworkContext() {
		ourAsio->keepRunning.reset();
		ourAsio->ioContext.stop();
		if (ourAsio->ioContext.get_executor().running_in_this_thread()) {
			std::thread([asio = std::move(ourAsio)]() mutable { asio.reset(); }).detach();
		}
	}

}
