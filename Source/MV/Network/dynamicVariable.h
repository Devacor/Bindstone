#ifndef _MV_DYNAMIC_VARIABLE_H_
#define _MV_DYNAMIC_VARIABLE_H_

#include <boost/variant.hpp>
#include <string>

namespace chaiscript { class ChaiScript; }

namespace MV {
	typedef boost::variant<bool, int64_t, double, std::string> DynamicVariable;
	void hookDynamicVariable(chaiscript::ChaiScript &a_script);
}

#endif
