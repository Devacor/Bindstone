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
#include <jaiscript/signals/signal.hpp>
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
		a_script.add(chaiscript::fun(&jai::receiver<T>::block), "block");
		a_script.add(chaiscript::fun(&jai::receiver<T>::blocked), "blocked");
		a_script.add(chaiscript::fun(&jai::receiver<T>::unblock), "unblock");
		a_script.add(chaiscript::fun(&jai::receiver<T>::has_script), "hasScript");
		a_script.add(chaiscript::fun(&jai::receiver<T>::script), "script");

		a_script.add(chaiscript::fun([](typename jai::receiver<T>::shared_type& a_pointer) {a_pointer.reset(); }), "reset");
	}

	template <typename T>
	void hookSignal(chaiscript::ChaiScript& a_script) {
		// Hook signal_emitter (internal emitter type)
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<jai::receiver<T>>(jai::signal_emitter<T>::*)(const std::string&, std::function<T>)>(&jai::signal_emitter<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<jai::receiver<T>>(jai::signal_emitter<T>::*)(const std::string&, const std::string&)>(&jai::signal_emitter<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<void(jai::signal_emitter<T>::*)(const std::string&)>(&jai::signal_emitter<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(static_cast<void(jai::signal_emitter<T>::*)(std::shared_ptr<jai::receiver<T>>)>(&jai::signal_emitter<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(&jai::signal_emitter<T>::connection), "connection");
		a_script.add(chaiscript::fun(&jai::signal_emitter<T>::connected), "connected");

		// Hook signal (public wrapper type)
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<jai::receiver<T>>(jai::signal<T>::*)(const std::string&, std::function<T>)>(&jai::signal<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<jai::receiver<T>>(jai::signal<T>::*)(const std::string&, const std::string&)>(&jai::signal<T>::connect)), "connect");
		a_script.add(chaiscript::fun(static_cast<void(jai::signal<T>::*)(const std::string&)>(&jai::signal<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(static_cast<void(jai::signal<T>::*)(std::shared_ptr<jai::receiver<T>>)>(&jai::signal<T>::disconnect)), "disconnect");
		a_script.add(chaiscript::fun(&jai::signal<T>::connection), "connection");
		a_script.add(chaiscript::fun(&jai::signal<T>::connected), "connected");
	}

	template <typename T>
	class ScriptSignalRegistrar {
	public:
		typedef jai::signal_emitter<T> OurSignalType;
		ScriptSignalRegistrar() {
			ScriptImplementation::template addRegistrationMethod<OurSignalType>([&](chaiscript::ChaiScript& a_scriptEngine, const MV::Services&){
				hookSignal<T>(a_scriptEngine);
				hookReceiver<T>(a_scriptEngine);
			});
		}
	};
}

#include "engineHooks.cxx"
