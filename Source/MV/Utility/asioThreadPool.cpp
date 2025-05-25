#include "asioThreadPool.h"

#include <asio.hpp>

namespace MV{
	struct ThreadPoolDetails {
		ThreadPoolDetails() :
			service(std::make_shared<asio::io_context>()),
			working(std::make_unique<asio::io_context::work>(*service)) {
		}

		std::shared_ptr<asio::io_context> service;
		std::unique_ptr<asio::io_context::work> working;
	};

	AsioThreadPool::AsioThreadPool(size_t a_threads) :
		details(std::make_unique<ThreadPoolDetails>()) {

		for (size_t i = 0; i < a_threads; ++i) {
			workers.emplace_back(std::make_unique<std::thread>([this] {
				details->service->run();
			}));
		}
	}

	AsioThreadPool::~AsioThreadPool() {
		details->service->stop();
		for (auto&& worker : workers) {
			if (worker->joinable()) {
				worker->join();
			}
		}
	}

	void AsioThreadPool::task(Job a_newWork) {
		details->service->post([=]() mutable {
			a_newWork.parent = this;
			a_newWork();
		});
	}

	std::shared_ptr<asio::io_context> AsioThreadPool::io_context() const {
		return details->service;
	}
}