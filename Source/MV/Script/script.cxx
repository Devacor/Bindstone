//This file is intended to be included in the game project's code and then including additional game-specific
//script bindings from that location. The goal here is to include chaiscript once and only once in the project by
//using the PIMPL idiom, but also to allow extensibility within client code. This requires including .cxx files from
//within a .cpp file to merge these all into the same translation unit.
/*
//MyGameCodeFolder/Script.hpp File Contents:
#include "MV/Script/script.cxx"
#include "MyGameCodeFolder/gameHooks.cxx"
*/

#include "script.h"

#include <functional>

#include "MV/Utility/generalUtility.h"
#include "MV/Utility/scopeGuard.hpp"
#include "MV/Utility/signal.hpp"
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/utility/utility.hpp"

#include "chaiscript/chaiscript_stdlib.hpp"

namespace MV {
	struct ScriptImplementation : public Script::IScriptImplementation {
		ScriptImplementation(const MV::Services& a_services, const std::vector<std::string>& a_paths) :
			scriptEngine({ "" }, a_paths, [](const std::string& a_file) {return MV::fileContents(a_file, true); }, chaiscript::default_options()) {

			for (auto&& registrationMethod : registrationMethods) {
				registrationMethod.second(scriptEngine, a_services);
			}
		}

		template <typename T>
		static void addRegistrationMethod(const std::function<void(chaiscript::ChaiScript&, const MV::Services&)> &a_registrationMethod) {
			registrationMethods[std::type_index(typeid(T))] = a_registrationMethod;
		}

		template <typename Callable>
		inline chaiscript::Boxed_Value scriptExceptionWrapper(const std::string& a_entryPointName, Callable a_callable) {
			try {
				return a_callable();
			} catch (chaiscript::Boxed_Value& bv) {
				error(a_entryPointName, " Exception [", chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what(), "]");
				throw;
			} catch (const std::exception& e) {
				error(a_entryPointName, " Exception [", e.what(), "]");
				throw;
			} catch (...) {
				error(a_entryPointName, " Unknown Exception");
				throw;
			}
		}

		chaiscript::Boxed_Value eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) override {
			return scriptExceptionWrapper(a_scriptIdentifier, [&]() {
				auto resetLocals = scriptEngine.get_locals();
				SCOPE_EXIT{ scriptEngine.set_locals(resetLocals); };

				scriptEngine.set_locals(a_localVariables);
				return scriptEngine.eval(a_scriptContents);
			});
		}
		chaiscript::Boxed_Value fileEval(const std::string& a_scriptIdentifier, const std::string& a_file, const std::map<std::string, chaiscript::Boxed_Value>& a_localVariables) override {
			return scriptExceptionWrapper(a_scriptIdentifier, [&]() {
				auto resetLocals = scriptEngine.get_locals();
				SCOPE_EXIT{ scriptEngine.set_locals(resetLocals); };

				scriptEngine.set_locals(a_localVariables);
				return scriptEngine.eval_file(a_file);
			});
		}

		chaiscript::ChaiScript scriptEngine;
		inline static std::map<std::type_index, std::function<void(chaiscript::ChaiScript&, const MV::Services&)>> registrationMethods {};
	};

	Script::Script(const MV::Services& a_services, const std::vector<std::string>& a_paths) :
		guts(std::make_unique<ScriptImplementation>(a_services, a_paths)) {
	}

	template <typename T>
	class Script::Registrar {
	public:
		Registrar(){
			ScriptImplementation::addRegistrationMethod<T>([&](chaiscript::ChaiScript& scriptEngine, const MV::Services& services){
				privateAccess(scriptEngine, services);
			});
		}
		Registrar(const std::function<void(chaiscript::ChaiScript&, const MV::Services&)>& a_method) {
			if (!a_method)
			{
				std::cout << "wtf";
			}
			ScriptImplementation::addRegistrationMethod<T>([=](chaiscript::ChaiScript& scriptEngine, const MV::Services& services){
				a_method(scriptEngine, services);
				privateAccess(scriptEngine, services);
			});
		}

		void privateAccess(chaiscript::ChaiScript& scriptEngine, const MV::Services& a_services){}
	};


	template <typename T>
	void hookReceiver(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::fun(&Receiver<T>::block), "block");
		a_script.add(chaiscript::fun(&Receiver<T>::blocked), "blocked");
		a_script.add(chaiscript::fun(&Receiver<T>::unblock), "unblock");
		a_script.add(chaiscript::fun(&Receiver<T>::hasScript), "hasScript");
		a_script.add(chaiscript::fun(&Receiver<T>::script), "script");

		a_script.add(chaiscript::fun([](typename Receiver<T>::SharedType& a_pointer) {a_pointer.reset(); }), "reset");
	}

	template <typename T>
	void hookSignal(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(Signal<T>::*)(const std::string&, std::function<T>)>(&Signal<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(Signal<T>::*)(const std::string&, const std::string&)>(&Signal<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<void(Signal<T>::*)(const std::string&)>(&Signal<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(static_cast<void(Signal<T>::*)(std::shared_ptr<Receiver<T>>)>(&Signal<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(&Signal<T>::connection), "connection");
		a_script.add(chaiscript::fun(&Signal<T>::connected), "connected");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(SignalRegister<T>::*)(const std::string&, std::function<T>)>(&SignalRegister<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(SignalRegister<T>::*)(const std::string&, const std::string&)>(&SignalRegister<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(const std::string&)>(&SignalRegister<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(std::shared_ptr<Receiver<T>>)>(&SignalRegister<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(&SignalRegister<T>::connection), "connection");
		a_script.add(chaiscript::fun(&SignalRegister<T>::connected), "connected");
	}

	template <typename T>
	class ScriptSignalRegistrar {
	public:
		typedef MV::Signal<T> OurSignalType;
		ScriptSignalRegistrar() {
			ScriptImplementation::template addRegistrationMethod<OurSignalType>([&](chaiscript::ChaiScript& a_scriptEngine, const MV::Services&){
				hookSignal<T>(a_scriptEngine);
				hookReceiver<T>(a_scriptEngine);
			});
		}
	};
}

#include "engineHooks.cxx"