#ifndef _MV_CHAISCRIPT_UTILITY_H_
#define _MV_CHAISCRIPT_UTILITY_H_

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

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

	template <typename Callable>
	inline bool scriptExceptionWrapper(const std::string &a_entryPointName, Callable a_callable) {
		try {
			a_callable();
			return true;
		} catch (chaiscript::Boxed_Value &bv) {
			error(a_entryPointName, " Exception [", chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what(), "]");
		} catch (const std::exception& e) {
			error(a_entryPointName, " Exception [", e.what(), "]");
		} catch (...) {
			error(a_entryPointName, " Unknown Exception");
		}
		return false;
	}
}

#endif