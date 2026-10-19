#include "log.h"
#include "services.hpp"
#include <jaiscript/thread_pool.hpp>

namespace MV {
	Logger* Logger::instance() {
		static Logger logger;
		Logger* found = Services::instance().get<Logger>(false);
		if (found) {
			return found;
		}
		Services::instance().connect(&logger);
		return &logger;
	}

	jai::thread_pool& initializeThreadPool(jai::thread_pool& a_pool) {
		a_pool.spin_budget_ns(0);
		a_pool.on_exception([](std::exception_ptr a_exception) {
			try {
				std::rethrow_exception(a_exception);
			} catch (std::exception &e) {
				MV::error("Uncaught Exception in Thread Pool: ", e.what());
			} catch (...) {
				MV::error("Uncaught non-standard exception in Thread Pool");
			}
		});
		return a_pool;
	}
}
