#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <map>
#include <tuple>
#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"

namespace MV {

	template <typename T>
	class Reciever {
	public:
		typedef std::function<T> FunctionType;
		typedef std::shared_ptr<Reciever<T>> SharedType;
		typedef std::weak_ptr<Reciever<T>> WeakType;

		static std::shared_ptr< Reciever<T> > make(std::function<T> a_callback){
			return std::shared_ptr< Reciever<T> >(new Reciever<T>(a_callback, ++uniqueId));
		}

		template <class ...Arg>
		void notify(Arg &&... a_parameters){
			if(!isBlocked){
				callback(std::forward<Arg>(a_parameters)...);
			}
		}
		template <class ...Arg>
		void operator()(Arg &&... a_parameters){
			if(!isBlocked){
				callback(std::forward<Arg>(a_parameters)...);
			}
		}

		template <class ...Arg>
		void notify(){
			if(!isBlocked){
				callback();
			}
		}
		template <class ...Arg>
		void operator()(){
			if(!isBlocked){
				callback();
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
		bool operator<(const Reciever<T>& a_rhs){
			return id < a_rhs.id;
		}
		bool operator>(const Reciever<T>& a_rhs){
			return id > a_rhs.id;
		}
		bool operator==(const Reciever<T>& a_rhs){
			return id == a_rhs.id;
		}
		bool operator!=(const Reciever<T>& a_rhs){
			return id != a_rhs.id;
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			if (!scriptHookedUp[reinterpret_cast<size_t>(&a_script)]) {
				scriptHookedUp[reinterpret_cast<size_t>(&a_script)] = true;

				a_script.add(chaiscript::fun(&Reciever<T>::block), "block");
				a_script.add(chaiscript::fun(&Reciever<T>::blocked), "blocked");
				a_script.add(chaiscript::fun(&Reciever::unblock), "unblock");
			}
			return a_script;
		}

	private:
		Reciever(std::function<T> a_callback, long long a_id):
			id(a_id),
			callback(a_callback),
			isBlocked(false){
		}
		bool isBlocked;
		std::function< T > callback;
		long long id;
		static long long uniqueId;

		static std::map<size_t, bool> scriptHookedUp;
	};

	template <typename T>
	std::map<size_t, bool> Reciever<T>::scriptHookedUp = std::map<size_t, bool>();

	template <typename T>
	long long Reciever<T>::uniqueId = 0;

	template <typename T>
	class Signal {
	public:
		typedef std::function<T> FunctionType;
		typedef Reciever<T> RecieverType;
		typedef std::shared_ptr<Reciever<T>> SharedRecieverType;
		typedef std::weak_ptr<Reciever<T>> WeakRecieverType;

		//No protection against duplicates.
		std::shared_ptr<Reciever<T>> connect(std::function<T> a_callback){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				auto signal = Reciever<T>::make(a_callback);
				observers.insert(signal);
				return signal;
			} else{
				return nullptr;
			}
		}
		//Duplicate Recievers will not be added. If std::function ever becomes comparable this can all be much safer.
		bool connect(std::shared_ptr<Reciever<T>> a_value){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				observers.insert(a_value);
				return true;
			}else{
				return false;
			}
		}

		void disconnect(std::shared_ptr<Reciever<T>> a_value){
			if(a_value){
				if(!inCall){
					observers.erase(a_value);
				} else{
					disconnectQueue.push_back(a_value);
				}
			}
		}

		void clearObservers(){
			if (!inCall) {
				observers.clear();
			}else{
				disconnectQueue.resize(a.size());
				std::copy(disconnectQueue.begin(), disconnectQueue.end(), observers.begin());
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

				SCOPE_EXIT{
					callScript(std::forward<Arg>(a_parameters)...);
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

				SCOPE_EXIT{
					callScript();
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
				} else{
					++i;
				}
			}
			return observers.size();
		}

		void scriptEngine(chaiscript::ChaiScript *a_scriptEngine) {
			scriptEnginePointer = a_scriptEngine;
		}
		chaiscript::ChaiScript* scriptEngine() const{
			return scriptEnginePointer;
		}

		std::string script() const {
			return scriptString;
		}

		void script(const std::string &a_script) {
			scriptString = a_script;
		}

		void clearScript() {
			scriptString.clear();
		}

		void parameterNames(const std::vector<std::string> &a_orderedParameterNames){
			orderedParameterNames = a_orderedParameterNames;
		}

		std::vector<std::string> parameterNames() const{
			return orderedParameterNames;
		}

		bool hasParameterNames() const{
			return !orderedParameterNames.empty();
		}

		template <class Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("scriptString", scriptString)
			);
		}

		template <class Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			archive(
				cereal::make_nvp("parameterNames", orderedParameterNames),
				cereal::make_nvp("scriptString", scriptString)
			);

			archive.extract(cereal::make_nvp("script", scriptEnginePointer));
		}

	private:
		template <typename ...Arg>
		void callScript(Arg &&... a_parameters) {
			if (scriptEnginePointer && !scriptString.empty()) {
				std::map<std::string, chaiscript::Boxed_Value> localVariables;
				auto tupleReference = std::make_tuple(std::forward<Arg>(a_parameters)...);
				auto parameterValues = toVector<chaiscript::Boxed_Value>(tupleReference);
				for (size_t i = 0; i < parameterValues.size(); ++i) {
					localVariables.emplace(
						(i < orderedParameterNames.size()) ? orderedParameterNames[i] : "arg_" + std::to_string(i), 
						parameterValues[i]);
				}
				auto resetLocals = scriptEnginePointer->get_locals();
				scriptEnginePointer->set_locals(localVariables);
				SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
				scriptEnginePointer->eval(scriptString);
			}
		}

		template <typename ...Arg>
		void callScript() {
			if (scriptEnginePointer && !scriptString.empty()) {
				scriptEnginePointer->eval(scriptString);
			}
		}

		std::set< std::weak_ptr< Reciever<T> >, std::owner_less<std::weak_ptr<Reciever<T>>> > observers;
		size_t observerLimit = std::numeric_limits<size_t>::max();
		bool inCall = false;
		bool isBlocked = false;
		std::function<T> blockedCallback;
		std::vector< std::shared_ptr<Reciever<T>> > disconnectQueue;
		bool calledWhileBlocked = false;

		std::vector<std::string> orderedParameterNames;

		chaiscript::ChaiScript *scriptEnginePointer = nullptr;
		std::string scriptString;
	};

	//Can be used as a public SignalRegister member for connecting signals to a private Signal member.
	//In this way you won't have to write forwarding connect/disconnect boilerplate for your classes.
	template <typename T>
	class SignalRegister {
	public:
		typedef std::function<T> FunctionType;
		typedef Reciever<T> RecieverType;
		typedef std::shared_ptr<Reciever<T>> SharedRecieverType;
		typedef std::weak_ptr<Reciever<T>> WeakRecieverType;

		SignalRegister(Signal<T> &a_slot) :
			slot(a_slot){
		}

		SignalRegister(SignalRegister<T> &a_rhs) :
			slot(a_rhs.slot) {
		}

		//no protection against duplicates
		std::shared_ptr<Reciever<T>> connect(std::function<T> a_callback){
			return slot.connect(a_callback);
		}
		//duplicate shared_ptr's will not be added
		bool connect(std::shared_ptr<Reciever<T>> a_value){
			return slot.connect(a_value);
		}

		void disconnect(std::shared_ptr<Reciever<T>> a_value){
			slot.disconnect(a_value);
		}

		std::shared_ptr<Reciever<T>> connect(const std::string &a_id, std::function<T> a_callback){
			return ownedConnections[a_id] = slot.connect(a_callback);
		}

		void disconnect(const std::string &a_id){
			auto connectionToRemove = ownedConnections.find(a_id);
			if (connectionToRemove != ownedConnections.end()) {
				slot.disconnect(connectionToRemove->second);
			}
		}

		bool connected(const std::string &a_id) {
			return ownedConnections.find(a_id) != ownedConnections.end();
		}

		std::shared_ptr<Reciever<T>> connection(const std::string &a_id){
			return ownedConnections[a_id];
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

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Reciever<T>>(SignalRegister<T>::*)(const std::string &, std::function<T>)>(&SignalRegister<T>::connect)), "connect");
				a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(const std::string &)>(&SignalRegister<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(static_cast<void(SignalRegister<T>::*)(std::shared_ptr<Reciever<T>>)>(&SignalRegister<T>::disconnect)), "disconnect");
				a_script.add(chaiscript::fun(&SignalRegister<T>::connection), "connection");
				a_script.add(chaiscript::fun(&SignalRegister<T>::connected), "connected");
			}

			Reciever<T>::hook(a_script);

			return a_script;
		}
	private:
		std::map<std::string, SharedRecieverType> ownedConnections;
		Signal<T> &slot;

		static std::map<size_t, bool> scriptHookedUp;
	};

	template <typename T>
	std::map<size_t, bool> SignalRegister<T>::scriptHookedUp = std::map<size_t, bool>();
}

#endif
