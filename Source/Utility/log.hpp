#ifndef __MV_LOG_H__
#define __MV_LOG_H__

#include <string>
#include <iostream>
#include "require.hpp"

#undef ERROR

namespace MV {
	enum LogLevel {DEBUG, INFO, WARNING, ERROR, NONE};

	struct LogData {
		LogData(): level(DEBUG) {}
		LogLevel level;
	};

	LogData logFilter;

	template <typename... Args>
	inline void log(LogLevel a_level, Args&&... a_args) {
		if (a_level >= logFilter.level) {
			std::cout << MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)) << std::endl;
		}
	}
}

#endif
