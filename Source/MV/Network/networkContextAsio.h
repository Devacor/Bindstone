#ifndef _MV_NETWORK_CONTEXT_ASIO_H_
#define _MV_NETWORK_CONTEXT_ASIO_H_

// The asio half of NetworkContext. Include it only from .cpp files: headers stay free of asio.
#include "MV/Network/networkContext.h"
#include <asio.hpp>
#include <jaiscript/thread_pool.hpp>

namespace MV {
	struct NetworkContext::Asio {
		explicit Asio(size_t a_threads) :
			workers(a_threads) {
		}

		asio::io_context ioContext;
		asio::executor_work_guard<asio::io_context::executor_type> keepRunning {asio::make_work_guard(ioContext)};
		jai::thread_pool workers;
	};
}

#endif
