#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <map>
#include "Utility/tupleHelpers.hpp"
#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"

//change to [[nodiscard]] when we get support.
#define MV_NODISCARD

namespace cereal {
	class access;
}

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
			if(!isBlocked){
				if (scriptCallback.empty()) {
					callback(std::forward<Arg>(a_parameters)...);
				} else {
					callScript(std::forward<Arg>(a_parameters)...);
				}
			}
		}
		template <class ...Arg>
		void operator()(Arg &&... a_parameters){
			if(!isBlocked){
				if (scriptCallback.empty()) {
					callback(std::forward<Arg>(a_parameters)...);
				}else{
					callScript(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		template <class ...Arg>
		void notify(){
			if(!isBlocked){
				if (scriptCallback.empty()) {
					callback();
				} else {
					callScript();
				}
			}
		}
		template <class ...Arg>
		void operator()(){
			if(!isBlocked){
				if (scriptCallback.empty()) {
					callback();
				} else {
					callScript();
				}
			}
		}

		void block(){
			isBlocked = true;
		}
		void unblock(){
			isBlocked = false;
		}
		bool blocked() const{
			return isBlocked;
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
			}
			return a_script;
		}

		std::string script() const {
			return scriptCallback;
		}
		bool hasScript() const {
			return !scriptCallback.empty();
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
			std::vector<std::string> scripts;
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("scripts", scriptCallback)
			);
			archive.extract(cereal::make_nvp("script", scriptEnginePointer));
			id = ++uniqueId;
		}

		Receiver() {
		}
		Receiver(std::function<T> a_callback, long long a_id):
			id(a_id),
			callback(a_callback){
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
						(!orderedParameterNames || i < orderedParameterNames->size()) ? (*orderedParameterNames)[i] : "arg_" + std::to_string(i),
						parameterValues[i]);
				}
				auto resetLocals = scriptEnginePointer->get_locals();
				scriptEnginePointer->set_locals(localVariables);
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->eval(scriptCallback);
			} else if (!scriptCallback.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
			}
		}

		template <class ...Arg>
		void callScript() {
			if (scriptEnginePointer && !scriptCallback.empty()) {
				std::map<std::string, chaiscript::Boxed_Value> localVariables;
				auto resetLocals = scriptEnginePointer->get_locals();
				scriptEnginePointer->set_locals(localVariables);
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->eval(scriptCallback);
			} else if (!scriptCallback.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
			}
		}

		bool isBlocked = false;
		std::function< T > callback;
		std::string scriptCallback;
		std::shared_ptr<std::vector<std::string>> orderedParameterNames;
		chaiscript::ChaiScript *scriptEnginePointer = nullptr;

		long long id;
		static long long uniqueId;

		static std::map<size_t, bool> scriptHookedUp;
	};

	template <typename T>
	std::map<size_t, bool> Receiver<T>::scriptHookedUp = std::map<size_t, bool>();

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
		MV_NODISCARD std::shared_ptr<Receiver<T>> connect(std::function<T> a_callback){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				auto signal = Receiver<T>::make(a_callback);
				observers.insert(signal);
				return signal;
			} else {
				return nullptr;
			}
		}
		MV_NODISCARD std::shared_ptr<Receiver<T>> connect(const std::string &a_callback) {
			if (observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit) {
				auto signal = Receiver<T>::make(a_callback, scriptEnginePointer, orderedParameterNames);
				observers.insert(signal);
				return signal;
			} else {
				return nullptr;
			}
		}

		//Duplicate Recievers will not be added. If std::function ever becomes comparable this can all be much safer.
		bool connect(std::shared_ptr<Receiver<T>> a_value){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				observers.insert(a_value);
				return true;
			}else{
				return false;
			}
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
					if (!observer.expired()) {
						disconnectQueue.insert(observer.lock());
					}
				}
			}
		}

		void clear(){
			clearObservers();
			clearScript();
		}

		void block(std::function<T> a_blockedCallback = nullptr) {
			isBlocked = true;
			blockedCallback = a_blockedCallback;
			calledWhileBlocked = false;
		}

		bool unblock() {
			if (isBlocked) {
				isBlocked = false;
				return calledWhileBlocked;
			}
			return false;
		}

		bool blocked() const {
			return isBlocked;
		}

		template <typename ...Arg>
		void operator()(Arg &&... a_parameters){
			if (!isBlocked) {
				inCall = true;
				SCOPE_EXIT{
					inCall = false;
					for (auto&& i : disconnectQueue) {
						observers.erase(i);
					}
					disconnectQueue.clear();
				};

				for (auto i = observers.begin(); !observers.empty() && i != observers.end();) {
					if (i->expired()) {
						observers.erase(i++);
					} else {
						auto next = i;
						++next;
						i->lock()->notify(std::forward<Arg>(a_parameters)...);
						i = next;
					}
				}
			}

			if (isBlocked) {
				calledWhileBlocked = true;
				if (blockedCallback) {
					blockedCallback(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		template <typename ...Arg>
		void operator()(){
			if (!isBlocked) {
				inCall = true;
				SCOPE_EXIT{
					inCall = false;
					for (auto&& i : disconnectQueue) {
						observers.erase(i);
					}
					disconnectQueue.clear();
				};

				for (auto i = observers.begin(); i != observers.end();) {
					if (i->expired()) {
						observers.erase(i++);
					} else {
						auto next = i;
						++next;
						i->lock()->notify();
						i = next;
					}
				}
			}

			if (isBlocked){
				calledWhileBlocked = true;
				if (blockedCallback) {
					blockedCallback(std::forward<Arg>(a_parameters)...);
				}
			}
		}

		void setObserverLimit(size_t a_newLimit){
			observerLimit = a_newLimit;
		}
		void clearObserverLimit(){
			observerLimit = std::numeric_limits<size_t>::max();
		}
		int getObserverLimit(){
			return observerLimit;
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
			return *this;
		}
		chaiscript::ChaiScript* scriptEngine() const{
			return scriptEnginePointer;
		}

		Signal<T>& parameterNames(const std::vector<std::string> &a_orderedParameterNames){
			orderedParameterNames = a_orderedParameterNames.empty() ? nullptr : std::make_shared<std::vector<std::string>>(a_orderedParameterNames);
			return *this;
		}

		std::vector<std::string> parameterNames() const{
			return orderedParameterNames ? *orderedParameterNames : std::vector<std::string>();
		}

		bool hasParameterNames() const{
			return !orderedParameterNames.empty();
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
			archive.extract(cereal::make_nvp("script", scriptEnginePointer));
		}

		std::set< std::weak_ptr< Receiver<T> >, std::owner_less<std::weak_ptr<Receiver<T>>> > observers;
		size_t observerLimit = std::numeric_limits<size_t>::max();
		bool inCall = false;
		bool isBlocked = false;
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

		SignalRegister(Signal<T> &a_slot) :
			slot(a_slot){
		}

		SignalRegister(SignalRegister<T> &a_rhs) :
			slot(a_rhs.slot) {
		}

		//no protection against duplicates
		MV_NODISCARD std::shared_ptr<Receiver<T>> connect(std::function<T> a_callback){
			return slot.connect(a_callback);
		}
		//duplicate shared_ptr's will not be added
		bool connect(std::shared_ptr<Receiver<T>> a_value){
			return slot.connect(a_value);
		}

		void disconnect(std::shared_ptr<Receiver<T>> a_value){
			slot.disconnect(a_value);
		}

		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, std::function<T> a_callback){
			return slot.connect(a_id, a_callback);
		}
		std::shared_ptr<Receiver<T>> connect(const std::string &a_id, const std::string &a_scriptCallback) {
			return slot.connect(a_id, a_scriptCallback);
		}

		bool connected(const std::string &a_id) {
			return slot.connected(a_id);
		}

		void disconnect(const std::string &a_id){
			slot.disconnect(a_id);
		}

		std::shared_ptr<Receiver<T>> connection(const std::string &a_id){
			return slot.connection(a_id);
		}

		//script support
		void scriptEngine(chaiscript::ChaiScript *a_scriptEngine) {
			slot.scriptEngine(a_scriptEngine);
		}
		chaiscript::ChaiScript* scriptEngine() const {
			return slot.scriptEngine();
		}

		std::string script() const {
			return slot.script();
		}

		void script(const std::string &a_script) {
			slot.script(a_script);
		}

		void clearScript() {
			slot.clearScript();
		}

		void parameterNames(const std::vector<std::string> &a_orderedParameterNames) {
			slot.parameterNames(a_orderedParameterNames);
		}

		std::vector<std::string> parameterNames() const {
			return slot.parameterNames();
		}

		bool hasParameterNames() const {
			return slot.hasParameterNames();
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			if (!scriptHookedUp[reinterpret_cast<size_t>(&a_script)]) {
				scriptHookedUp[reinterpret_cast<size_t>(&a_script)] = true;

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
		Signal<T> &slot;

		static std::map<size_t, bool> scriptHookedUp;
	};

	template <typename T>
	std::map<size_t, bool> SignalRegister<T>::scriptHookedUp = std::map<size_t, bool>();
}

#endif
