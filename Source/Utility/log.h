#ifndef _MV_LOG_H_
#define _MV_LOG_H_

#include <string>
#include <iostream>
#include <vector>
#include <mutex>
#include "require.hpp"

//seriously fuck right off wingdi.h
#undef ERROR

namespace MV {
	enum LogLevel {DEBUG, INFO, WARNING, ERROR, NONE};

	struct LogData {
		LogData() : level(LogLevel::DEBUG), targets({ &std::cout, &std::cout, &std::cout, &std::cerr }) {}
		LogLevel level;
		std::vector<std::ostream*> targets;
	};

	extern LogData logFilter;

	template <typename... Args>
	inline void log(LogLevel a_level, Args&&... a_args) {
		if (a_level != LogLevel::NONE && a_level >= logFilter.level) {
			static std::mutex mutex;
			std::lock_guard<std::mutex> lock(mutex);

			(*logFilter.targets[static_cast<int>(a_level)]) << MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)) << std::endl;
		}
	}
}

#endif
