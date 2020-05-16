#ifndef _MV_SCRIPT_H_
#define _MV_SCRIPT_H_

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "MV/Utility/services.hpp"
#include "chaiscript/dispatchkit/boxed_cast.hpp"

namespace MV {
	class Script {
	public:
		template <typename T>
		class Registrar;

		struct IScriptImplementation {
			virtual chaiscript::Boxed_Value eval(const std::string& a_scriptIdentifier, const std::string& scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) = 0;
			virtual chaiscript::Boxed_Value fileEval(const std::string& a_scriptIdentifier, const std::string& scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& localVariables) = 0;
		};

		Script(const MV::Services &a_services, const std::vector<std::string>& a_paths = { "", "Interface/", "Scripts/" });

		template <typename T>
		std::optional<T> eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables = {}) {
			try {
				return chaiscript::boxed_cast<T>(guts->eval(a_scriptIdentifier, a_scriptContents, a_localVariables));
			} catch (...) {
				return {};
			}
		}

		template <typename T>
		std::optional<T> fileEval(const std::string& a_scriptIdentifier, const std::string& a_scriptFile, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables = {}) {
			try {
				return chaiscript::boxed_cast<T>(guts->fileEval(a_scriptIdentifier, a_scriptFile, a_localVariables));
			} catch (...) {
				return {};
			}
		}

		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables = {}) {
			try {
				guts->eval(a_scriptIdentifier, a_scriptContents, a_localVariables);
				return true;
			} catch (...) {
				return false;
			}
		}

		bool fileEval(const std::string& a_scriptIdentifier, const std::string& a_scriptFile, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables = {}) {
			try {
				guts->fileEval(a_scriptIdentifier, a_scriptFile, a_localVariables);
				return true;
			} catch (...) {
				return false;
			}
		}
	private:
		std::unique_ptr<IScriptImplementation> guts;
	};
}

#endif
