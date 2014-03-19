#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <set>
#include "Utility/scopeGuard.hpp"

namespace MV {

	template <typename T>
	class Signal {
	public:
		typedef std::function<T> FunctionType;
		typedef std::shared_ptr<Signal<T>> SharedType;

		static std::shared_ptr< Signal<T> > make(std::function<T> a_callback){
			return std::shared_ptr< Signal<T> >(new Signal<T>(a_callback, ++uniqueId));
		}

		template <class ...Arg>
		void notify(Arg... a_parameters){
			if(!isBlocked){
				callback(std::forward<Arg>(a_parameters)...);
			}
		}
		template <class ...Arg>
		void operator()(Arg... a_parameters){
			if(!isBlocked){
				callback(std::forward<Arg>(a_parameters)...);
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
		bool operator<(const Signal<T>& a_rhs){
			return id < a_rhs.id;
		}
		bool operator>(const Signal<T>& a_rhs){
			return id > a_rhs.id;
		}
		bool operator==(const Signal<T>& a_rhs){
			return id == a_rhs.id;
		}
		bool operator!=(const Signal<T>& a_rhs){
			return id != a_rhs.id;
		}

	private:
		Signal(std::function<T> a_callback, long long a_id):
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
	long long Signal<T>::uniqueId = 0;

	template <typename T>
	class Slot {
	public:
		typedef std::function<T> FunctionType;
		typedef Signal<T> SignalType;
		typedef std::shared_ptr<Signal<T>> SharedSignalType;

		//No protection against duplicates.
		std::shared_ptr<Signal<T>> connect(std::function<T> a_callback){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				auto signal = Signal<T>::make(a_callback);
				observers.insert(signal);
				return signal;
			} else{
				return nullptr;
			}
		}
		//Duplicate Signals will not be added. If std::function ever becomes comparable this can all be much safer.
		bool connect(std::shared_ptr<Signal<T>> a_value){
			if(observerLimit == std::numeric_limits<size_t>::max() || cullDeadObservers() < observerLimit){
				observers.insert(a_value);
				return true;
			}else{
				return false;
			}
		}

		void disconnect(std::shared_ptr<Signal<T>> a_value){
			if(!inCall){
				observers.erase(a_value);
			} else{
				disconnectQueue.push_back(a_value);
			}
		}

		template <typename ...Arg>
		void operator()(Arg... a_parameters){
			inCall = true;
			SCOPE_EXIT{
				inCall = false;
				for(auto& i : disconnectQueue){
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
			for(auto i = observers.begin(); i != observers.end();) {
				if(i->expired()) {
					observers.erase(i++);
				}
			}
			return observers.size();
		}
	private:
		std::set< std::weak_ptr< Signal<T> >, std::owner_less<std::weak_ptr<Signal<T>>> > observers;
		size_t observerLimit = std::numeric_limits<size_t>::max();
		bool inCall = false;
		std::vector< std::shared_ptr<Signal<T>> > disconnectQueue;
	};

	//Can be used as a public SlotRegister member for connecting slots to a private Slot member.
	//In this way you won't have to write forwarding connect/disconnect boilerplate for your classes.
	template <typename T>
	class SlotRegister {
	public:
		typedef std::function<T> FunctionType;
		typedef Signal<T> SignalType;
		typedef std::shared_ptr<Signal<T>> SharedSignalType;

		SlotRegister(Slot<T> &a_slot) :
			slot(a_slot){
		}

		//no protection against duplicates
		std::shared_ptr<Signal<T>> connect(std::function<T> a_callback){
			return slot.connect(a_callback);
		}
		//duplicate shared_ptr's will not be added
		bool connect(std::shared_ptr<Signal<T>> a_value){
			return slot.connect(a_value);
		}

		void disconnect(std::shared_ptr<Signal<T>> a_value){
			slot.disconnect(a_value);
		}
	private:
		Slot<T> &slot;
	};

}

#endif
