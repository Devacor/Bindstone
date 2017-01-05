#ifndef _MV_CHAISCRIPT_UTILITY_H_
#define _MV_CHAISCRIPT_UTILITY_H_

#include <map>

#include "chaiscript/chaiscript.hpp"
#include "chaiscript/utility/utility.hpp"
#include "generalUtility.h"
#include "scopeGuard.hpp"

namespace MV {
	inline void scriptEval(const std::string &scriptContents, chaiscript::ChaiScript &script, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) {
		auto resetLocals = script.get_locals();
		SCOPE_EXIT{ script.set_locals(resetLocals); };

		script.set_locals(localVariables);
		script.eval(scriptContents);
	}

	inline void scriptFileEval(const std::string &scriptFile, chaiscript::ChaiScript &script, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) {
		auto resetLocals = script.get_locals();
		SCOPE_EXIT{ script.set_locals(resetLocals); };

		script.set_locals(localVariables);
		script.eval_file(scriptFile);
	}
}

#endif