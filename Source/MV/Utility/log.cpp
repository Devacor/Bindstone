#include "log.h"
#include "services.hpp"

namespace MV {
	Logger* MV::Logger::instance() {
		static Logger logger;
		Logger* found = MV::Services::instance().get<Logger>(false);
		if (found) {
			return found;
		}
		MV::Services::instance().connect(&logger);
		return &logger;
	}
}