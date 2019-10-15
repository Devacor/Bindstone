#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <map>
#include "MV/Utility/tupleHelpers.hpp"
#include "MV/Utility/scopeGuard.hpp"
#include "MV/Utility/chaiscriptUtility.h"
#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

namespace MV {

	template <typename T>
	class Receiver {
		friend cereal::access;
	public:
		typedef std::function<T> FunctionType;
		typedef std::shared_ptr<Receiver<T>> SharedType;
		typedef std::weak_ptr<Receiver<T>> WeakType;

		static std::shared_ptr< Receiver<T> > make(std::function<T> a_callback){
			return std::shared_ptr< Receiver<T> >(new Receiver<T>(a_callback, ++uniqueId));
		}
		static std::shared_ptr< Receiver<T> > make(const std::string &a_script, chaiscript::ChaiScript *a_engine, const std::shared_ptr<std::vector<std::string>> &a_parameterNames = nullptr) {
			return std::shared_ptr< Receiver<T> >(new Receiver<T>(a_script, a_engine, a_parameterNames, ++uniqueId));
		}

		template <class ...Arg>
		void notify(Arg &&... a_parameters){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback(std::forward<Arg>(a_parameters)...);
				} else {
					callScript(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		template <class ...Arg>
		bool predicate(Arg &&... a_parameters) {
			if (!blocked() && !invalid()) {
				if (scriptCallback.empty()) {
					return callback(std::forward<Arg>(a_parameters)...);
				} else {
					return callScriptPredicate(std::forward<Arg>(a_parameters)...);
				}
			}
			return false;
		}

		bool invalid() const {
			return scriptCallback.empty() && !callback;
		}

		template <class ...Arg>
		void operator()(Arg &&... a_parameters){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback(std::forward<Arg>(a_parameters)...);
				}else{
					callScript(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		template <class ...Arg>
		bool predicate() {
			if (!blocked() && !invalid()) {
				if (scriptCallback.empty()) {
					return callback();
				} else {
					return callScriptPredicate();
				}
			}
			return false;
		}
		template <class ...Arg>
		void notify(){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback();
				} else {
					callScript();
				}
			}
		}
		template <class ...Arg>
		void operator()(){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback();
				} else {
					callScript();
				}
			}
		}

		void block(){
			++isBlocked;
		}
		void unblock(){
			--isBlocked;
			if (isBlocked < 0) {
				isBlocked = 0;
			}
		}
		bool blocked() const{
			return isBlocked != 0;
		}

		//For sorting and comparison (removal/avoiding duplicates)
		bool operator<(const Receiver<T>& a_rhs){
			return id < a_rhs.id;
		}
		bool operator>(const Receiver<T>& a_rhs){
			return id > a_rhs.id;
		}
		bool operator==(const Receiver<T>& a_rhs){
			return id == a_rhs.id;
		}
		bool operator!=(const Receiver<T>& a_rhs){
			return id != a_rhs.id;
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			if (!scriptHookedUp[reinterpret_cast<size_t>(&a_script)]) {
				scriptHookedUp[reinterpret_cast<size_t>(&a_script)] = true;

				a_script.add(chaiscript::fun(&Receiver<T>::block), "block");
				a_script.add(chaiscript::fun(&Receiver<T>::blocked), "blocked");
				a_script.add(chaiscript::fun(&Receiver::unblock), "unblock");
				a_script.add(chaiscript::fun(&Receiver::hasScript), "hasScript");
				a_script.add(chaiscript::fun(&Receiver::script), "script");

				a_script.add(chaiscript::fun([](Receiver<T>::SharedType &a_pointer) {a_pointer.reset(); }), "reset");
			}
			return a_script;
		}

		std::string script() const {
			return scriptCallback;
		}
		bool hasScript() const {
			return !scriptCallback.empty();
		}

		void scriptEngine(chaiscript::ChaiScript *a_scriptEnginePointer) {
			scriptEnginePointer = a_scriptEnginePointer;
		}
		chaiscript::ChaiScript* scriptEngine() const {
			return scriptEnginePointer;
		}
	private:
		template <class Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("script", scriptCallback)
			);
		}

		template <class Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("script", scriptCallback)
			);
			auto& services = cereal::get_user_data<MV::Services>(archive);
			scriptEnginePointer = services.get<chaiscript::ChaiScript>(false);
		}

		Receiver() :
			id(0) {
		}
		Receiver(std::function<T> a_callback, long long a_id):
			callback(a_callback),
            id(a_id){
		}
		Receiver(const std::string& a_callback, chaiscript::ChaiScript* a_scriptEngine, const std::shared_ptr<std::vector<std::string>> &a_parameterNames, long long a_id) :
			id(a_id),
			scriptCallback(a_callback),
			scriptEnginePointer(a_scriptEngine),
			orderedParameterNames(a_parameterNames) {
		}

		template <typename ...Arg>
		void callScript(Arg &&... a_parameters) {
			if (scriptEnginePointer && !scriptCallback.empty()) {
				std::map<std::string, chaiscript::Boxed_Value> localVariables;
				auto tupleReference = std::forward_as_tuple(std::forward<Arg>(a_parameters)...);
				auto parameterValues = toVector<chaiscript::Boxed_Value>(tupleReference);
				for (size_t i = 0; i < parameterValues.size(); ++i) {
					localVariables.emplace(
						(orderedParameterNames && i < orderedParameterNames->size()) ? (*orderedParameterNames)[i] : "arg_" + std::to_string(i),
						parameterValues[i]);
				}
				auto resetLocals = scriptEnginePointer->get_locals();
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->set_locals(localVariables);
				scriptEnginePointer->eval(scriptCallback);
			} else if (!scriptCallback.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
			}
		}

		template <typename ...Arg>
		bool callScriptPredicate(Arg &&... a_parameters) {
			if (scriptEnginePointer && !scriptCallback.empty()) {
				std::map<std::string, chaiscript::Boxed_Value> localVariables;
				auto tupleReference = std::forward_as_tuple(std::forward<Arg>(a_parameters)...);
				auto parameterValues = toVector<chaiscript::Boxed_Value>(tupleReference);
				for (size_t i = 0; i < parameterValues.size(); ++i) {
					localVariables.emplace(
						(orderedParameterNames && i < orderedParameterNames->size()) ? (*orderedParameterNames)[i] : "arg_" + std::to_string(i),
						parameterValues[i]);
				}
				auto resetLocals = scriptEnginePointer->get_locals();
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->set_locals(localVariables);
				return chaiscript::boxed_cast<bool>(scriptEnginePointer->eval(scriptCallback));
			} else if (!scriptCallback.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
				return false;
			}
			return false;
		}

		template <class ...Arg>
		void callScript() {
			if (scriptEnginePointer && !scriptCallback.empty()) {
				std::map<std::string, chaiscript::Boxed_Value> localVariables;
				auto resetLocals = scriptEnginePointer->get_locals();
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->set_locals(localVariables);
				scriptEnginePointer->eval(scriptCallback);
			} else if (!scriptCallback.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
			}
		}

		int isBlocked = 0;
		std::function< T > callback;
		std::string scriptCallback;
		std::shared_ptr<std::vector<std::string>> orderedParameterNames;
		chaiscript::ChaiScript *scriptEnginePointer = nullptr;

		long long id;
		static long long uniqueId;

		static inline std::map<size_t, bool> scriptHookedUp = std::map<size_t, bool>();
	};

	template <typename T>
	long long Receiver<T>::uniqueId = 0;

	template <typename T>
	class Signal {
		friend cereal::access;
	public:
		typedef std::function<T> FunctionType;
		typedef Receiver<T> RecieverType;
		typedef std::shared_ptr<Receiver<T>> SharedRecieverType;
		typedef std::weak_ptr<Receiver<T>> WeakRecieverType;

		//No protection against duplicates.
		[[nodiscard]]
		std::shared_ptr<Receiver<T>> connect(std::function<T> a_callback){
			auto signal = Receiver<T>::make(a_callback);
			observers.insert(signal);
			return signal;
		}
		[[nodiscard]]
		std::shared_ptr<Receiver<T>> connect(const std::string &a_callback) {
			auto signal = Receiver<T>::make(a_callback, scriptEnginePointer, orderedParameterNames);
			observers.insert(signal);
			return signal;
		}

		//Duplicate Recievers will not be added. If std::function ever becomes comparable this can all be much safer.
		bool connect(std::shared_ptr<Receiver<T>> a_value){
			observers.insert(a_value);
			return true;
		}

		//Add owned connections. Note: these should be disconnected via ID instead of by the receiver.
		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, std::function<T> a_callback) {
			return ownedConnections[a_id] = connect(a_callback);
		}
		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, const std::string &a_scriptCallback) {
			return ownedConnections[a_id] = connect(a_scriptCallback);
		}

		std::shared_ptr<Receiver<T>> connection(const std::string &a_id) {
			auto foundConnection = ownedConnections.find(a_id);
			if (foundConnection != ownedConnections.end()) {
				return foundConnection->second;
			}
			return SharedRecieverType();
		}

		void disconnect(std::shared_ptr<Receiver<T>> a_value){
			if(a_value){
				if(!inCall){
					observers.erase(a_value);
				} else {
					disconnectQueue.insert(a_value);
				}
			}
		}

		bool connected(const std::string &a_id) {
			return ownedConnections.find(a_id) != ownedConnections.end();
		}

		void disconnect(const std::string &a_id) {
			auto connectionToRemove = ownedConnections.find(a_id);
			if (connectionToRemove != ownedConnections.end()) {
				disconnect(connectionToRemove->second);
				ownedConnections.erase(connectionToRemove);
			}
		}

		void clearObservers(){
			ownedConnections.clear();
			if (!inCall) {
				observers.clear();
			}else{
				disconnectQueue.clear();
				for (auto&& observer : observers) {
					if (auto lockedObserver = observer.lock()) {
						disconnectQueue.insert(lockedObserver);
					}
				}
			}
		}

		void clear(){
			clearObservers();
		}

		void block() {
			if (isBlocked++ == 0) {
				calledWhileBlocked = false;
			}
		}

		bool unblock() {
			if (--isBlocked == 0) {
				return calledWhileBlocked;
			}
			if (isBlocked < 0) {
				isBlocked = 0;
			}
			return false;
		}

		bool blocked() const {
			return isBlocked != 0;
		}

		template <typename ...Arg>
		void operator()(Arg &&... a_parameters){
			if (!blocked()) {
				inCall = true;
				SCOPE_EXIT{
					inCall = false;
					for (auto&& i : disconnectQueue) {
						observers.erase(i);
					}
					disconnectQueue.clear();
				};

				for (auto i = observers.begin(); !observers.empty() && i != observers.end();) {
					if (auto lockedI = i->lock()) {
						auto next = i;
						++next;
						lockedI->notify(std::forward<Arg>(a_parameters)...);
						i = next;
					} else {
						observers.erase(i++);
					}
				}
			}

			if (blocked()) {
				calledWhileBlocked = true;
				if (blockedCallback) {
					blockedCallback(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		template <typename ...Arg>
		void operator()(){
			if (!blocked()) {
				inCall = true;
				SCOPE_EXIT{
					inCall = false;
					for (auto&& i : disconnectQueue) {
						observers.erase(i);
					}
					disconnectQueue.clear();
				};

				for (auto i = observers.begin(); i != observers.end();) {
					if (auto lockedI = i->lock()) {
						auto next = i;
						++next;
						lockedI->notify();
						i = next;
					} else {
						observers.erase(i++);
					}
				}
			}

			if (blocked()){
				calledWhileBlocked = true;
				if (blockedCallback) {
					blockedCallback();
				}
			}
		}

		size_t cullDeadObservers(){
			for(auto i = observers.begin();!observers.empty() && i != observers.end();) {
				if(i->expired()) {
					observers.erase(i++);
				} else {
					++i;
				}
			}
			return observers.size();
		}

		Signal<T>& scriptEngine(chaiscript::ChaiScript *a_scriptEngine) {
			scriptEnginePointer = a_scriptEngine;
			for (auto&& observer : observers) {
				if (auto lockedObserver = observer.lock()) {
					lockedObserver->scriptEngine(a_scriptEngine);
				}
			}
			return *this;
		}
		chaiscript::ChaiScript* scriptEngine() const{
			return scriptEnginePointer;
		}

		//Supplied to the Receivers which are spawned.
		Signal<T>& parameterNames(const std::vector<std::string> &a_orderedParameterNames){
			orderedParameterNames = a_orderedParameterNames.empty() ? nullptr : std::make_shared<std::vector<std::string>>(a_orderedParameterNames);
			return *this;
		}

		std::vector<std::string> parameterNames() const{
			return orderedParameterNames ? *orderedParameterNames : std::vector<std::string>();
		}

		bool hasParameterNames() const{
			return !orderedParameterNames->empty();
		}

	private:
		template <class Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			std::vector< std::shared_ptr<Receiver<T>> > scriptObservers;
			std::map<std::string, std::shared_ptr<Receiver<T>>> ownedScriptObservers;
			for (auto&& observer : observers) {
				if (!observer.expired() && observer.lock()->hasScript()) {
					scriptObservers.emplace_back(observer);
				}
			}
			for (auto&& observerKV : ownedConnections) {
				if (observerKV.second->hasScript()) {
					ownedScriptObservers[observerKV.first] = observerKV.second;
				}
			}
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("observers", scriptObservers),
				cereal::make_nvp("ownedObservers", ownedScriptObservers)
			);
		}

		template <class Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			std::vector< std::shared_ptr<Receiver<T>> > scriptObservers;
			std::map<std::string, std::shared_ptr<Receiver<T>>> ownedScriptObservers;
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("observers", scriptObservers),
				cereal::make_nvp("ownedObservers", ownedScriptObservers)
			);
			for (auto&& scriptObserver : scriptObservers) {
				observers.insert(scriptObserver);
			}
			for (auto&& ownedScriptObserver : ownedScriptObservers) {
				ownedConnections[ownedScriptObserver.first] = ownedScriptObserver.second;
			}
			auto& services = cereal::get_user_data<MV::Services>(archive);
			scriptEnginePointer = services.get<chaiscript::ChaiScript>(false);
		}

		std::set< std::weak_ptr< Receiver<T> >, std::owner_less<std::weak_ptr<Receiver<T>>> > observers;

		bool inCall = false;
		int isBlocked = 0;
		std::function<T> blockedCallback;
		std::set< std::shared_ptr<Receiver<T>> > disconnectQueue;
		bool calledWhileBlocked = false;

		std::shared_ptr<std::vector<std::string>> orderedParameterNames;

		chaiscript::ChaiScript *scriptEnginePointer = nullptr;

		std::map<std::string, SharedRecieverType> ownedConnections;
	};

	//Can be used as a public SignalRegister member for connecting signals to a private Signal member.
	//In this way you won't have to write forwarding connect/disconnect boilerplate for your classes.
	template <typename T>
	class SignalRegister {
	public:
		typedef std::function<T> FunctionType;
		typedef Receiver<T> RecieverType;
		typedef std::shared_ptr<Receiver<T>> SharedRecieverType;
		typedef std::weak_ptr<Receiver<T>> WeakRecieverType;

		SignalRegister(Signal<T> &a_signal) :
			signal(a_signal){
		}

		SignalRegister(SignalRegister<T> &a_rhs) :
			signal(a_rhs.signal) {
		}

		//no protection against duplicates
		[[nodiscard]]
		std::shared_ptr<Receiver<T>> connect(std::function<T> a_callback){
			return signal.connect(a_callback);
		}
		[[nodiscard]]
		std::shared_ptr<Receiver<T>> connect(const std::string &a_callbackScript) {
			return signal.connect(a_callbackScript);
		}
		//duplicate shared_ptr's will not be added
		bool connect(std::shared_ptr<Receiver<T>> a_value){
			return signal.connect(a_value);
		}

		void disconnect(std::shared_ptr<Receiver<T>> a_value){
			signal.disconnect(a_value);
		}

		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, std::function<T> a_callback){
			return signal.connect(a_id, a_callback);
		}
		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, const std::string &a_scriptCallback) {
			return signal.connect(a_id, a_scriptCallback);
		}

		bool connected(const std::string &a_id) {
			return signal.connected(a_id);
		}

		void disconnect(const std::string &a_id){
			signal.disconnect(a_id);
		}

		std::shared_ptr<Receiver<T>> connection(const std::string &a_id){
			return signal.connection(a_id);
		}

		//script support
		void scriptEngine(chaiscript::ChaiScript *a_scriptEngine) {
			signal.scriptEngine(a_scriptEngine);
		}
		chaiscript::ChaiScript* scriptEngine() const {
			return signal.scriptEngine();
		}

		void parameterNames(const std::vector<std::string> &a_orderedParameterNames) {
			signal.parameterNames(a_orderedParameterNames);
		}

		std::vector<std::string> parameterNames() const {
			return signal.parameterNames();
		}

		bool hasParameterNames() const {
			return signal.hasParameterNames();
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			if (!scriptHookedUp[reinterpret_cast<size_t>(&a_script)]) {
				scriptHookedUp[reinterpret_cast<size_t>(&a_script)] = true;

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(Signal<T>::*)(const std::string &, std::function<T>)>(&Signal<T>::connect)), "connect");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(Signal<T>::*)(const std::string &, const std::string &)>(&Signal<T>::connect)), "connect");
				a_script.add(chaiscript::fun(static_cast<void(Signal<T>::*)(const std::string &)>(&Signal<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(static_cast<void(Signal<T>::*)(std::shared_ptr<Receiver<T>>)>(&Signal<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(&Signal<T>::connection), "connection");
				a_script.add(chaiscript::fun(&Signal<T>::connected), "connected");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(SignalRegister<T>::*)(const std::string &, std::function<T>)>(&SignalRegister<T>::connect)), "connect");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Receiver<T>>(SignalRegister<T>::*)(const std::string &, const std::string &)>(&SignalRegister<T>::connect)), "connect");
				a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(const std::string &)>(&SignalRegister<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(std::shared_ptr<Receiver<T>>)>(&SignalRegister<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(&SignalRegister<T>::connection), "connection");
				a_script.add(chaiscript::fun(&SignalRegister<T>::connected), "connected");
			}

			Receiver<T>::hook(a_script);

			return a_script;
		}

	private:
		Signal<T> &signal;

		static inline std::map<size_t, bool> scriptHookedUp = std::map<size_t, bool>();
	};
}

#endif
