#include "script.h"

#include "MV/Utility/generalUtility.h"
#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/utility/utility.hpp"

#include "MV/Utility/chaiscriptStdLib.h"
#include "chaiscript/chaiscript_stdlib.hpp"

#include "chaiscriptHooks.ixx"

namespace MV {

}


namespace MV {
	struct ScriptImplementation : public Script::IScriptImplementation {
		ScriptImplementation(const std::vector<std::string> &a_paths):
			scriptEngine({ "" }, a_paths, [](const std::string& a_file) {return MV::fileContents(a_file, true); }, chaiscript::default_options()) {
		}

		template <typename Callable>
		inline bool scriptExceptionWrapper(const std::string& a_entryPointName, Callable a_callable) {
			try {
				a_callable();
				return true;
			} catch (chaiscript::Boxed_Value& bv) {
				error(a_entryPointName, " Exception [", chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what(), "]");
			} catch (const std::exception& e) {
				error(a_entryPointName, " Exception [", e.what(), "]");
			} catch (...) {
				error(a_entryPointName, " Unknown Exception");
			}
			return false;
		}
		
		chaiscript::Boxed_Value eval(const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) override {
			auto resetLocals = script.get_locals();
			SCOPE_EXIT{ script.set_locals(resetLocals); };

			script.set_locals(a_localVariables);
			return script.eval(a_scriptContents);
		}
		chaiscript::Boxed_Value fileEval(const std::string& a_file, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) override {
			auto resetLocals = script.get_locals();
			SCOPE_EXIT{ script.set_locals(resetLocals); };

			script.set_locals(a_localVariables);
			script.eval_file(a_file);
		}

		chaiscript::ChaiScript scriptEngine;
	};

	Script::Script(const std::vector<std::string>& a_paths) :
		guts(std::make_unique<ScriptImplementation>(a_paths)){
	}
}