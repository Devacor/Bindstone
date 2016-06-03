#ifndef _MV_LOG_H_
#define _MV_LOG_H_

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

	extern LogData logFilter;

	template <typename... Args>
	inline void log(LogLevel a_level, Args&&... a_args) {
		if (a_level >= logFilter.level && a_level != ERROR) {
			std::cout << MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)) << std::endl;
		} else if(a_level >= logFilter.level) {
			std::cerr << MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)) << std::endl;
		}
	}
}

#endif
