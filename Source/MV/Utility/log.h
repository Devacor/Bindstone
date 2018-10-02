#ifndef _MV_LOG_H_
#define _MV_LOG_H_

#include <string>
#include <iostream>
#include <vector>
#include <mutex>
#include "require.hpp"

namespace MV {
	enum class Severity {Debug, Info, Warning, Error, None};

	struct LogReciever {
		virtual void log(Severity, const std::string &) {}
	};

	struct LogData {
		LogData() : level(Severity::Debug), 
			targets({ &std::cout, &std::cout, &std::cout, &std::cerr }),
			listeners({nullptr, nullptr, nullptr, nullptr}) {
		}
		Severity level;
		std::vector<std::ostream*> targets;
		std::vector<LogReciever*> listeners;
	};

	extern LogData logFilter;

	inline std::mutex& logMutex() {
		static std::mutex m;
		return m;
	}

	template <typename... Args>
	inline void log(Severity a_level, Args&&... a_args) {
		if (a_level != Severity::None && a_level >= logFilter.level) {
			std::lock_guard<std::mutex> lock(logMutex());
			auto output = MV::to_string(std::make_tuple(std::forward<Args>(a_args)...));
			auto logIndex = static_cast<int>(a_level);
			(*logFilter.targets[logIndex]) << output << std::endl;
			if (logFilter.listeners[logIndex]) {
				logFilter.listeners[logIndex]->log(a_level, output);
			}
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
