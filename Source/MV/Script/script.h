#ifndef _MV_SCRIPT_H_
#define _MV_SCRIPT_H_

#include <string>
#include <vector>
#include <memory>

#include "chaiscript/dispatchkit/boxed_cast.hpp"

namespace MV {
	class Script {
	public:
		struct IScriptImplementation {
			virtual chaiscript::Boxed_Value eval(const std::string& scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) = 0;
			virtual chaiscript::Boxed_Value fileEval(const std::string& scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) = 0;
		};

		Script(const std::vector<std::string> &a_paths = { "", "Interface/", "Scripts/" });

		template <typename T>
		T eval(const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) {
			return chaiscript::boxed_cast<T>(guts->eval(a_scriptContents, a_localVariables));
		}

		template <typename T>
		T fileEval(const std::string& a_scriptFile, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) {
			return chaiscript::boxed_cast<T>(guts->fileEval(a_scriptFile, a_localVariables));
		}

		void eval(const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) {
			guts->eval(a_scriptContents, a_localVariables);
		}

		void fileEval(const std::string& a_scriptFile, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) {
			guts->fileEval(a_scriptFile, a_localVariables);
		}
	private:
		std::unique_ptr<IScriptImplementation> guts;
	};
}

#endif
