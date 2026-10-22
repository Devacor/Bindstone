#include "MV/Network/networkContext.h"
#include "MV/Network/networkReactor.h"

namespace MV {

	NetworkContext::NetworkContext(size_t a_threads) :
		ourReactor(std::make_unique<NetworkReactor>(a_threads)) {
		worker = std::thread([reactor = ourReactor.get()] { reactor->run(); });
	}

	// The last owner can be work running on the network thread, which cannot join itself: another thread joins once
	// run() returns, then destroys the reactor.
	NetworkContext::~NetworkContext() {
		ourReactor->stop();
		if (ourReactor->runningInThisThread()) {
			std::thread([reactor = std::move(ourReactor), worker = std::move(worker)]() mutable {
				worker.join();
				reactor.reset();
			}).detach();
		} else if (worker.joinable()) {
			worker.join();
		}
	}

	void NetworkContext::post(std::function<void()> a_task) {
		ourReactor->post(std::move(a_task));
	}

	void NetworkContext::stop() {
		ourReactor->stop();
		if (!ourReactor->runningInThisThread() && worker.joinable()) {
			worker.join();
		}
	}

}
