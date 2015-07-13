#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <map>
#include "Utility/scopeGuard.hpp"

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
	};

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

				for (auto i = observers.begin(); i != observers.end();) {
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
				} else{
					++i;
				}
			}
			return observers.size();
		}
	private:
		std::set< std::weak_ptr< Reciever<T> >, std::owner_less<std::weak_ptr<Reciever<T>>> > observers;
		size_t observerLimit = std::numeric_limits<size_t>::max();
		bool inCall = false;
		bool isBlocked = false;
		std::function<T> blockedCallback;
		std::vector< std::shared_ptr<Reciever<T>> > disconnectQueue;
		bool calledWhileBlocked = false;
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
				slot.disconnect(*connectionToRemove);
			}
		}

		bool connected(const std::string &a_id) {
			return ownedConnections.find(a_id) != ownedConnections.end();
		}
	private:
		std::map<std::string, SharedRecieverType> ownedConnections;
		Signal<T> &slot;
	};

}

#endif
