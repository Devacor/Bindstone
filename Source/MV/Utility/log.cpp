#include "log.h"
#include "services.hpp"

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
}