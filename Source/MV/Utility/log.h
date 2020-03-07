#ifndef _MV_LOG_H_
#define _MV_LOG_H_
#include <string>
#include <iostream>
#include <vector>
#include <array>
#include <mutex>
#include <functional>
#include <tuple>
#include <any>
#include "require.hpp"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace MV {
	struct LogData {
	public:
		LogData(const std::any &a_value) :value(a_value) {}
		LogData() {}
		bool empty() const {
			return value.has_value();
		}
		template <typename T>
		const T* get() const {
			if (value.has_value()) {
				try {
					return &std::any_cast<const T&>(value);
				} catch (const std::bad_any_cast& e) {
				}
			}
			return nullptr;
		}
	private:
		std::any value;
	};
	struct LogReciever {
		virtual void log(Severity, const LogData &, const std::string &) = 0;
	};
	struct CoutLogReciever : public LogReciever {
		virtual void log(Severity severity, const LogData &, const std::string & message) override {
			std::cout << message << std::endl;
		}
	};
	struct CerrLogReciever : public LogReciever {
		virtual void log(Severity severity, const LogData &, const std::string & message) override {
			std::cerr << message << std::endl;
		}
	};

#ifdef __ANDROID__
	struct AndroidLogReciever : public LogReciever {
		AndroidLogReciever(int a_android_LogPriority, const std::string &a_prefix) : errorLevel(a_android_LogPriority), prefix(a_prefix) {}
		int errorLevel = 0;
		const std::string prefix;
		virtual void log(Severity severity, const LogData&, const std::string& message) override {
			__android_log_print(errorLevel, prefix.c_str(), "%s", message.c_str());
		}
	};
#endif

	class Logger {
	public:
		//Makes use of Service, must Services::instance().connect(logger) to allow this to work.
		static Logger* instance();
		Logger(Severity defaultLevel = Severity::Debug, bool useDefaultListeners = true) : level(defaultLevel) {
			if (useDefaultListeners) {
				addDefaultListeners();
			}
		}
		void filter(Severity a_level) {
			level = a_level;
		}
		virtual Severity filter() const {
			return level;
		}
		template <typename... Args>
		inline void log(Severity a_level, Args&&... a_args) {
			if (a_level != Severity::Silence && a_level >= level) {
				std::lock_guard<std::mutex> lock(m);
				broadcastLog(a_level, {}, MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)));
			}
		}
		template <typename... Args>
		inline void log(Severity a_level, const LogData &a_data, Args&&... a_args) {
			if (a_level != Severity::Silence && a_level >= level) {
				std::lock_guard<std::mutex> lock(m);
				broadcastLog(a_level, a_data, MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)));
			}
		}
		template <typename... Args>
		inline void log(Severity a_level, LogData &&a_data, Args&&... a_args) {
			if (a_level != Severity::Silence && a_level >= level) {
				std::lock_guard<std::mutex> lock(m);
				broadcastLog(a_level, a_data, MV::to_string(std::make_tuple(std::forward<Args>(a_args)...)));
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
		template <typename... Args>
		inline void debug(const LogData &a_data, Args&&... a_args) {
			log(Severity::Debug, a_data, std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void info(const LogData &a_data, Args&&... a_args) {
			log(Severity::Info, a_data, std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void warning(const LogData &a_data, Args&&... a_args) {
			log(Severity::Warning, a_data, std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void error(const LogData &a_data, Args&&... a_args) {
			log(Severity::Error, a_data, std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void debug(LogData &&a_data, Args&&... a_args) {
			log(Severity::Debug, std::move(a_data), std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void info(LogData &&a_data, Args&&... a_args) {
			log(Severity::Info, std::move(a_data), std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void warning(LogData &&a_data, Args&&... a_args) {
			log(Severity::Warning, std::move(a_data), std::forward<Args>(a_args)...);
		}
		template <typename... Args>
		inline void error(LogData &&a_data, Args&&... a_args) {
			log(Severity::Error, std::move(a_data), std::forward<Args>(a_args)...);
		}
		//This override provides an easy to declare non-owning interface wrapper for the std::function interface.
		//Mostly this is to avoid accidental copies and to allow for a descriptively named "log" function to be used.
		//Caller owns the lifespan of the receiver, be aware not to dangle! :)
		void listen(Severity level, LogReciever* receiver) {
			listen(level, [receiver](Severity level, const LogData& data, const std::string &message) {receiver->log(level, data, message); });
		}
		void listen(LogReciever* receiver) {
			listen([receiver](Severity level, const LogData& data, const std::string &message) {receiver->log(level, data, message); });
		}
		void listen(Severity a_level, std::function<void(Severity, const LogData &, const std::string&)> a_receiver) {
			int levelIndex = static_cast<int>(a_level);
			MV::require<MV::RangeException>(levelIndex >= 0 && levelIndex < listeners.size(), "Failed to listen to logger on Severity Level [", a_level, "]");
			std::lock_guard<std::mutex> lock(m);
			listeners[static_cast<size_t>(a_level)].push_back(a_receiver);
		}
		void listen(std::function<void(Severity, const LogData &, const std::string&)> a_receiver) {
			std::lock_guard<std::mutex> lock(m);
			for (size_t i = 0; i < static_cast<size_t>(Severity::Count); ++i) {
				listeners[i].push_back(a_receiver);
			}
		}
		void clearListeners() {
			std::lock_guard<std::mutex> lock(m);
			for (auto&& listenerList : listeners) {
				listenerList.clear();
			}
		}
		void addDefaultListeners() {
			listen(Severity::Debug, &debugReceiver);
			listen(Severity::Info, &infoReceiver);
			listen(Severity::Warning, &errorReceiver);
			listen(Severity::Error, &warningReceiver);
		}
	protected:
		virtual void broadcastLog(Severity a_level, const LogData &a_data, const std::string& a_message) {
			auto logIndex = static_cast<size_t>(a_level);
			for (auto&& listener : listeners[logIndex]) {
				listener(a_level, a_data, a_message);
			}
		}
	private:
#ifdef __ANDROID__
		AndroidLogReciever debugReceiver = AndroidLogReciever(ANDROID_LOG_DEBUG, "MV_DEBUG");
		AndroidLogReciever infoReceiver = AndroidLogReciever(ANDROID_LOG_INFO, "MV_INFO");
		AndroidLogReciever errorReceiver = AndroidLogReciever(ANDROID_LOG_ERROR, "MV_ERROR");
		AndroidLogReciever warningReceiver = AndroidLogReciever(ANDROID_LOG_WARN, "MV_WARN");
#else
		CoutLogReciever debugReceiver;
		CoutLogReciever infoReceiver;
		CoutLogReciever warningReceiver;
		CerrLogReciever errorReceiver;
#endif
		std::array<std::vector<std::function<void(Severity, const LogData &, const std::string&)>>, static_cast<int>(Severity::Count)> listeners;
		Severity level;
		std::mutex m;
	};
	//Can be hooked up to services to completely squelch the logger.
	class SilencedLogger : public Logger {
	public:
		virtual Severity filter() const override { return Severity::Silence; }
	protected:
		virtual void broadcastLog(Severity a_level, const LogData &a_data, const std::string& a_message) override { }
	};
	//Root level convenience methods which require a registered Logger service.
	template <typename... Args>
	inline void log(Severity a_level, Args&&... a_args) {
		Logger::instance()->log(a_level, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void debug(Args&&... a_args) {
		Logger::instance()->debug(std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void info(Args&&... a_args) {
		Logger::instance()->info(std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void warning(Args&&... a_args) {
		Logger::instance()->warning(std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void error(Args&&... a_args) {
		Logger::instance()->error(std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void log(Severity a_level, const LogData &a_data, Args&&... a_args) {
		Logger::instance()->log(a_level, a_data, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void debug(const LogData &a_data, Args&&... a_args) {
		Logger::instance()->debug(a_data, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void info(const LogData &a_data, Args&&... a_args) {
		Logger::instance()->info(a_data, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void warning(const LogData &a_data, Args&&... a_args) {
		Logger::instance()->warning(a_data, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void error(const LogData &a_data, Args&&... a_args) {
		Logger::instance()->error(a_data, std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void log(Severity a_level, LogData &&a_data, Args&&... a_args) {
		Logger::instance()->log(a_level, std::move(a_data), std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void debug(LogData &&a_data, Args&&... a_args) {
		Logger::instance()->debug(std::move(a_data), std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void info(LogData &&a_data, Args&&... a_args) {
		Logger::instance()->info(std::move(a_data), std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void warning(LogData &&a_data, Args&&... a_args) {
		Logger::instance()->warning(std::move(a_data), std::forward<Args>(a_args)...);
	}
	template <typename... Args>
	inline void error(LogData &&a_data, Args&&... a_args) {
		Logger::instance()->error(std::move(a_data), std::forward<Args>(a_args)...);
	}
}
#endif
