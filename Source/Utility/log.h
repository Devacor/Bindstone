#ifndef _MV_LOG_H_
#define _MV_LOG_H_

#include <string>
#include <iostream>
#include <vector>
#include <mutex>
#include "require.hpp"

namespace MV {
	enum class Severity {Debug, Info, Warning, Error, None};

	struct LogData {
		LogData() : level(Severity::Debug), targets({ &std::cout, &std::cout, &std::cout, &std::cerr }) {}
		Severity level;
		std::vector<std::ostream*> targets;
	};

	extern LogData logFilter;

	template <typename... Args>
	inline void log(Severity a_level, Args&&... a_args) {
		if (a_level != Severity::None && a_level >= logFilter.level) {
			static std::mutex mutex;
			std::lock_guard<std::mutex> lock(mutex);

			(*logFilter.targets[static_cast<int>(a_level)]) << MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)) << std::endl;
		}
	}

	template <typename... Args>
	inline void debug(Args&&... a_args) {
		log(Severity::Debug, std::forward<Args>(a_args)...);
	}

	template <typename... Args>
	inline void info(Args&&... a_args) {
		log(Severity::Info, std::forward<Args>(a_args)...);
	}

	template <typename... Args>
	inline void warning(Args&&... a_args) {
		log(Severity::Warning, std::forward<Args>(a_args)...);
	}

	template <typename... Args>
	inline void error(Args&&... a_args) {
		log(Severity::Error, std::forward<Args>(a_args)...);
	}
}

#endif
