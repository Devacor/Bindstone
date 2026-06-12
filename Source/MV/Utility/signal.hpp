#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <map>
#include "MV/Utility/services.hpp"
#include "MV/Utility/tupleHelpers.hpp"
#include "MV/Utility/scopeGuard.hpp"
#include <jaiscript/serialization/archive.hpp>
namespace jai { class engine; }

namespace MV {

	template <typename T>
	class Receiver {
	public:
		typedef std::function<T> FunctionType;
		typedef std::shared_ptr<Receiver<T>> SharedType;
		typedef std::weak_ptr<Receiver<T>> WeakType;

		static std::shared_ptr< Receiver<T> > make(std::function<T> a_callback){
			return std::shared_ptr< Receiver<T> >(new Receiver<T>(a_callback, ++uniqueId));
		}
		static std::shared_ptr< Receiver<T> > make(const std::string &a_script, jai::engine *a_engine, const std::shared_ptr<std::vector<std::string>> &a_parameterNames = nullptr) {
			return std::shared_ptr< Receiver<T> >(new Receiver<T>(a_script, a_engine, a_parameterNames, ++uniqueId));
		}

		template <class ...Arg>
		void notify(Arg &&... a_parameters){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback(std::forward<Arg>(a_parameters)...);
				} else {
					executeScript(scriptEnginePointer, scriptCallback);
				}
			}
		}

		template <class ...Arg>
		bool predicate(Arg &&... a_parameters) {
			if (!blocked() && !invalid()) {
				if (scriptCallback.empty()) {
					return callback(std::forward<Arg>(a_parameters)...);
				} else {
					return executeScriptPredicate(scriptEnginePointer, scriptCallback, false);
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
					executeScript(scriptEnginePointer, scriptCallback);
				}
			}
		}

		template <class ...Arg>
		bool predicate() {
			if (!blocked() && !invalid()) {
				if (scriptCallback.empty()) {
					return callback();
				} else {
					return executeScriptPredicate(scriptEnginePointer, scriptCallback, false);
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
					executeScript(scriptEnginePointer, scriptCallback);
				}
			}
		}
		template <class ...Arg>
		void operator()(){
			if(!blocked() && !invalid()){
				if (scriptCallback.empty()) {
					callback();
				} else {
					executeScript(scriptEnginePointer, scriptCallback);
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

		std::string script() const {
			return scriptCallback;
		}
		bool hasScript() const {
			return !scriptCallback.empty();
		}

		void scriptEngine(jai::engine* a_scriptEnginePointer) {
			scriptEnginePointer = a_scriptEnginePointer;
		}
		jai::engine* scriptEngine() const {
			return scriptEnginePointer;
		}
	private:
		Receiver() :
			id(0) {
		}
		Receiver(std::function<T> a_callback, long long a_id):
			callback(a_callback),
            id(a_id){
		}
		Receiver(const std::string& a_callback, jai::engine* a_scriptEngine, const std::shared_ptr<std::vector<std::string>> &a_parameterNames, long long a_id) :
			id(a_id),
			scriptCallback(a_callback),
			scriptEnginePointer(a_scriptEngine),
			orderedParameterNames(a_parameterNames) {
		}

		// Legacy script receivers run their source directly on the jai
		// engine; parameters are not forwarded (live script callbacks use jai::signal's
		// own receiver machinery). Dependent E defers the member lookup to instantiation
		// — jai::engine is only forward-declared in this widely-included header.
		template <typename E>
		static void executeScript(E* a_engine, const std::string& a_script) {
			if (a_engine && !a_script.empty()) {
				try {
					a_engine->execute(a_script);
				} catch (const std::exception& e) {
					std::cerr << "Signal script failed: " << e.what() << "\n";
				}
			} else if (!a_script.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a script engine handle!\n";
			}
		}

		template <typename E>
		static bool executeScriptPredicate(E* a_engine, const std::string& a_script, bool a_default) {
			if (a_engine && !a_script.empty()) {
				try {
					auto result = a_engine->execute(a_script);
					return result.is_bool() ? result.as_bool() : a_default;
				} catch (const std::exception& e) {
					std::cerr << "Signal script failed: " << e.what() << "\n";
				}
			} else if (!a_script.empty()) {
				std::cerr << "Failed to run script in receiver, you need to supply a script engine handle!\n";
			}
			return a_default;
		}

		int isBlocked = 0;
		std::function< T > callback;
		std::string scriptCallback;
		std::shared_ptr<std::vector<std::string>> orderedParameterNames;
		jai::engine* scriptEnginePointer = nullptr;

		int64_t id;
		static int64_t uniqueId;
	};

	template <typename T>
	int64_t Receiver<T>::uniqueId = 0;

	template <typename T>
	class Signal {
	public:
		typedef std::function<T> FunctionType;
		typedef Receiver<T> ReceiverType;
		typedef std::shared_ptr<Receiver<T>> SharedReceiverType;
		typedef std::weak_ptr<Receiver<T>> WeakReceiverType;

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

		//Duplicate Receivers will not be added. If std::function ever becomes comparable this can all be much safer.
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
			return SharedReceiverType();
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

		Signal<T>& scriptEngine(jai::engine *a_scriptEngine) {
			scriptEnginePointer = a_scriptEngine;
			for (auto&& observer : observers) {
				if (auto lockedObserver = observer.lock()) {
					lockedObserver->scriptEngine(a_scriptEngine);
				}
			}
			return *this;
		}
		jai::engine* scriptEngine() const{
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
		std::set< std::weak_ptr< Receiver<T> >, std::owner_less<std::weak_ptr<Receiver<T>>> > observers;

		bool inCall = false;
		int isBlocked = 0;
		std::function<T> blockedCallback;
		std::set< std::shared_ptr<Receiver<T>> > disconnectQueue;
		bool calledWhileBlocked = false;

		std::shared_ptr<std::vector<std::string>> orderedParameterNames;

		jai::engine *scriptEnginePointer = nullptr;

		std::map<std::string, SharedReceiverType> ownedConnections;
	};

	//Can be used as a public SignalRegister member for connecting signals to a private Signal member.
	//In this way you won't have to write forwarding connect/disconnect boilerplate for your classes.
	template <typename T>
	class SignalRegister {
	public:
		typedef std::function<T> FunctionType;
		typedef Receiver<T> ReceiverType;
		typedef std::shared_ptr<Receiver<T>> SharedReceiverType;
		typedef std::weak_ptr<Receiver<T>> WeakReceiverType;

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
		void scriptEngine(jai::engine *a_scriptEngine) {
			signal.scriptEngine(a_scriptEngine);
		}
		jai::engine* scriptEngine() const {
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

	private:
		Signal<T> &signal;
	};
}

#endif
